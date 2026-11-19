"""Ask the live session for something and wait for the answer (docs/LIVE_SESSION.md).

    python tools/live/req.py --as rt reload_shader file=Lighting.hlsl source=E:/wt/raw-rt8/src/d3d12_bridge/shaders/Lighting.hlsl
    python tools/live/req.py --as rt reload_ini Mode=2 View=0          # a kind's bare key=value pairs go into its args
    python tools/live/req.py --as engine measure frames=600 name=inn
    python tools/live/req.py --as rt capture kind=keyframe name=inn steps=game,improved
    python tools/live/req.py --as engine reload_module source=E:/wt/raw-ec9-build/raw_engine.dll
    python tools/live/req.py --as rt --json '{"keys": {"Mode": 2}}' reload_ini
    python tools/live/req.py status        python tools/live/req.py queue       (no waiting in the queue)

It prints the result as JSON and exits 0 when the request succeeded, 1 when it failed, 3 when no answer came in
--timeout seconds (the request stays queued). --no-wait prints the id and returns at once. Original work, MIT.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from live_queue import DEFAULT_ROOT, LiveQueue   # noqa: E402

# reload_ini and ini_set take their pairs in a nested form
NESTED = {"reload_ini": "keys"}


def parse_args(kind: str, pairs: list[str], raw: str) -> dict:
    args = json.loads(raw) if raw else {}
    loose = {}
    for p in pairs:
        k, sep, v = p.partition("=")
        if not sep:
            raise SystemExit(f"{p!r}: arguments are key=value")
        try:
            v = json.loads(v)   # numbers, true/false and lists stay typed; anything else is a string
        except ValueError:
            pass
        loose[k] = v
    if kind in NESTED and loose:
        args.setdefault(NESTED[kind], {}).update(loose)
    else:
        args.update(loose)
    return args


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("kind")
    p.add_argument("pairs", nargs="*")
    p.add_argument("--as", dest="who", default="anon", help="who is asking (shown in the log and the result)")
    p.add_argument("--json", default="", help="the args as one JSON object (merged with the key=value pairs)")
    p.add_argument("--root", default=str(DEFAULT_ROOT))
    p.add_argument("--timeout", type=float, default=900)
    p.add_argument("--no-wait", action="store_true")
    a = p.parse_args()
    q = LiveQueue(Path(a.root))
    if a.kind == "queue":
        print(json.dumps(q.pending(), indent=1))
        return 0
    if a.kind == "cancel":   # a file, not a queued request: the running request checks it between its steps
        (Path(a.root) / "cancel").mkdir(parents=True, exist_ok=True)
        for rid in a.pairs:
            (Path(a.root) / "cancel" / rid.split("=")[-1]).write_text(a.who, encoding="utf-8")
        print(json.dumps({"cancel": a.pairs}))
        return 0
    if a.kind == "state":   # the service's own file, no request
        print((Path(a.root) / "state.json").read_text(encoding="utf-8"))
        return 0
    rid = q.submit(a.kind, a.who, parse_args(a.kind, a.pairs, a.json))
    if a.no_wait:
        print(rid)
        return 0
    r = q.wait(rid, a.timeout)
    if r is None:
        print(json.dumps({"id": rid, "ok": None, "error": f"no answer in {a.timeout:.0f} s; still queued"}))
        return 3
    print(json.dumps(r, indent=1))
    return 0 if r.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())
