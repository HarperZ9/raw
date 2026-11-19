"""The live session's request queue: one JSON file a request, run in arrival order, one at a time.

    <root>/queue/<arrival ms>-<seq>-<id>.json     waiting (written as .tmp, then renamed, so a reader never sees half)
    <root>/running/<same name>                    the one request the service is running
    <root>/done/<same name>                       finished; the answer is in results/<id>/result.json
    <root>/results/<id>/result.json               ok, error, data, the files the request made, and its timings

The service claims a request by renaming it from queue/ into running/, so two services could never run the same
one, and requests never interleave. Shared by service.py (the owner) and req.py (the workers). Original work, MIT.
"""
from __future__ import annotations

import json
import os
import random
import re
import string
import time
from pathlib import Path

DEFAULT_ROOT = Path(os.environ.get("RAW_LIVE_ROOT", "E:/raw-live"))
NAME = re.compile(r"^(\d{17})-(\d{6})-([A-Za-z0-9_.-]+)\.json$")
SAFE = re.compile(r"[^A-Za-z0-9_.-]+")
_last_us = 0


def now_ms() -> int:
    return int(time.time() * 1000)


def write_json(path: Path, obj: dict) -> None:
    """Atomic: a reader sees the old file or the whole new one."""
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_name(path.name + f".{os.getpid()}.tmp")
    tmp.write_text(json.dumps(obj, indent=1), encoding="utf-8")
    os.replace(tmp, path)


class LiveQueue:
    def __init__(self, root: Path = DEFAULT_ROOT):
        self.root = Path(root)
        self.q, self.running, self.done, self.results = (self.root / d for d in ("queue", "running", "done", "results"))
        for d in (self.q, self.running, self.done, self.results):
            d.mkdir(parents=True, exist_ok=True)

    # ---- the worker's side --------------------------------------------------------------------------------
    def submit(self, kind: str, requester: str, args: dict | None = None) -> str:
        """Queues one request; returns its id (requester, time and a short tag)."""
        who = SAFE.sub("_", requester or "anon")[:24]
        rid = f"{who}-{time.strftime('%H%M%S')}-{''.join(random.choices(string.ascii_lowercase + string.digits, k=4))}"
        global _last_us
        body = {"id": rid, "kind": kind, "requester": who, "args": args or {}, "submitted_ms": now_ms()}
        # microseconds, never repeated in one process, then the pid: names sort in arrival order
        _last_us = max(time.time_ns() // 1000, _last_us + 1)
        write_json(self.q / f"{_last_us:017d}-{os.getpid() % 1000000:06d}-{rid}.json", body)
        return rid

    def result_path(self, rid: str) -> Path:
        return self.results / rid / "result.json"

    def wait(self, rid: str, timeout: float = 900, poll: float = 0.1) -> dict | None:
        """The answer, or None when it has not come in `timeout` seconds."""
        p, end = self.result_path(rid), time.time() + timeout
        while time.time() < end:
            if p.exists():
                try:
                    return json.loads(p.read_text(encoding="utf-8"))
                except (OSError, ValueError):
                    pass   # being replaced at this instant
            time.sleep(poll)
        return None

    def pending(self) -> list[dict]:
        out = []
        for f in sorted(self.q.glob("*.json")) + sorted(self.running.glob("*.json")):
            try:
                body = json.loads(f.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                continue
            body["state"] = "running" if f.parent == self.running else "waiting"
            out.append(body)
        return out

    # ---- the service's side -------------------------------------------------------------------------------
    def claim(self) -> tuple[Path, dict] | None:
        """The oldest waiting request, moved into running/; None when the queue is empty."""
        for f in sorted(self.q.glob("*.json")):
            if not NAME.match(f.name):
                f.rename(self.done / (f.name + ".badname"))
                continue
            dst = self.running / f.name
            # The claim is an exclusive create (O_EXCL), which NTFS makes atomic. A rename alone is not: under
            # contention two claimers both "moved" the same request in a test (tests/test_live_queue.py).
            try:
                os.close(os.open(self.running / (f.name + ".claim"), os.O_CREAT | os.O_EXCL | os.O_WRONLY))
            except FileExistsError:
                continue   # taken by someone else
            try:
                os.replace(f, dst)
            except FileNotFoundError:   # finished already by the claimer that won
                (self.running / (f.name + ".claim")).unlink(missing_ok=True)
                continue
            err: Exception | None = None
            for _ in range(10):   # Windows can refuse a read for an instant while a rename touches the folder
                try:
                    return dst, json.loads(dst.read_text(encoding="utf-8"))
                except FileNotFoundError:
                    err = None
                    break          # another claimer has it
                except (OSError, ValueError) as e:
                    err = e
                    time.sleep(0.02)
            if err is not None:
                try:
                    os.replace(dst, self.done / (f.name + ".unreadable"))
                except FileNotFoundError:
                    continue
                write_json(self.results / f"unreadable-{f.stem}" / "result.json", {"ok": False, "error": f"unreadable request: {err}"})
        return None

    def finish(self, running: Path, result: dict) -> None:
        write_json(self.result_path(result["id"]), result)
        os.replace(running, self.done / running.name)
        (running.parent / (running.name + ".claim")).unlink(missing_ok=True)

    def recover(self) -> list[str]:
        """Requests left in running/ by a service that died: answered as failed, so their callers stop waiting."""
        ids = []
        for f in sorted(self.running.glob("*.json")):
            try:
                body = json.loads(f.read_text(encoding="utf-8"))
            except (OSError, ValueError):
                body = {"id": f.stem}
            body.update(ok=False, error="the service stopped while this request ran; it was not finished")
            self.finish(f, body)
            ids.append(body["id"])
        for c in self.running.glob("*.claim"):   # a claim whose request never moved: it can be claimed again
            c.unlink(missing_ok=True)
        return ids
