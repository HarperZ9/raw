"""The live queue without a game: arrival order, one claim per request, recovery, and the CLI's argument forms.
Run: python -m pytest tools/live/tests -q. Original work, MIT."""
from __future__ import annotations

import json
import sys
import threading
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from live_queue import LiveQueue   # noqa: E402
from req import parse_args   # noqa: E402


def test_requests_run_in_arrival_order(tmp_path):
    q = LiveQueue(tmp_path)
    ids = [q.submit("console", who, {"command": f"c{i}"}) for i, who in enumerate(["rt", "engine", "rt", "lead"])]
    got = []
    while (job := q.claim()) is not None:
        path, body = job
        got.append(body["id"])
        q.finish(path, {**body, "ok": True})
    assert got == ids
    assert all(q.result_path(i).exists() for i in ids)
    assert not list(q.q.glob("*.json")) and not list(q.running.glob("*.json"))


def test_a_request_is_claimed_once_under_contention(tmp_path):
    q = LiveQueue(tmp_path)
    for i in range(40):
        q.submit("measure", "w", {"i": i})
    claimed: list[str] = []
    lock = threading.Lock()

    def worker():
        while (job := LiveQueue(tmp_path).claim()) is not None:
            with lock:
                claimed.append(job[1]["id"])

    ts = [threading.Thread(target=worker) for _ in range(4)]
    for t in ts:
        t.start()
    for t in ts:
        t.join()
    assert len(claimed) == 40 and len(set(claimed)) == 40


def test_recover_answers_a_request_left_running(tmp_path):
    q = LiveQueue(tmp_path)
    rid = q.submit("capture", "rt")
    q.claim()   # the service dies here
    assert q.recover() == [rid]
    r = json.loads(q.result_path(rid).read_text())
    assert r["ok"] is False and "stopped" in r["error"]
    assert q.wait(rid, timeout=0.5)["id"] == rid


def test_wait_times_out_with_none(tmp_path):
    q = LiveQueue(tmp_path)
    assert q.wait(q.submit("status", "x"), timeout=0.2) is None


def test_cli_pairs_keep_types_and_nest_ini_keys():
    assert parse_args("measure", ["frames=600", "name=inn"], "") == {"frames": 600, "name": "inn"}
    assert parse_args("reload_ini", ["Mode=2", "View=0"], "") == {"keys": {"Mode": 2, "View": 0}}
    assert parse_args("capture", ["steps=game,improved"], "") == {"steps": "game,improved"}
    assert parse_args("set_weather", ["id=0x10A241", "force=false"], "") == {"id": "0x10A241", "force": False}
    assert parse_args("ini_set", [], '{"section": "EngineRT", "key": "Weather", "value": 1}')["section"] == "EngineRT"
