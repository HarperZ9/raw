"""The live session's proof: 20 and more varied requests against one running game, timed, from a script.

    python tools/live/prove.py [--root E:/raw-live] [--module E:/wt/raw-live-build/raw_engine.dll] [--out proof.json]

Each request goes through the queue as a worker's would. The table at the end lists each request's kind, whether
it succeeded, the time from submit to answer, and the session's uptime at the answer; the JSON keeps every result.
A request that is meant to fail (the broken shader) counts as passed when it fails with the compiler's error and
the next request still runs. Original work, MIT.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from live_queue import LiveQueue   # noqa: E402

WARM_OLD = "return float4(knee3(lit), game.a);"
WARM_NEW = "return float4(knee3(lit) * float3(1.0f, 0.62f, 0.42f), game.a);"


def plan(module: str) -> list[tuple[str, dict, bool]]:
    """(kind, args, expected ok)"""
    return [
        ("status", {}, True),
        ("console", {"command": "tai"}, True),
        ("move", {"cell": "whiterunbanneredmare"}, True),
        ("measure", {"frames": 300, "name": "inn_start"}, True),
        ("capture", {"kind": "frame", "name": "inn_start"}, True),
        ("reload_ini", {"keys": {"Mode": 2}}, True),
        ("capture", {"kind": "frame", "name": "inn_improved"}, True),
        ("reload_shader", {"file": "Lighting.hlsl", "old": WARM_OLD, "new": WARM_NEW}, True),
        ("capture", {"kind": "frame", "name": "inn_warm"}, True),
        ("reload_shader", {"file": "Lighting.hlsl", "old": "float3(1.0f, 0.62f, 0.42f)",
                           "new": "float3(1.0f, 0.62f, 0.42f) * undefinedName"}, False),
        ("reload_shader", {"file": "Lighting.hlsl", "old": " * undefinedName", "new": ""}, True),
        ("reload_shader", {"file": "Lighting.hlsl", "old": WARM_NEW, "new": WARM_OLD}, True),
        ("ini_set", {"section": "Lighting", "key": "View", "value": "2"}, True),
        ("capture", {"kind": "frame", "name": "inn_sunview"}, True),
        ("ini_set", {"section": "Lighting", "key": "View", "value": "0"}, True),
        ("reload_module", {"source": module}, True),
        ("measure", {"frames": 300, "name": "inn_after_reload_1"}, True),
        ("engine", {"commands": "view"}, True),
        ("move", {"cell": "riverwood"}, True),
        ("set_time", {"hour": 9}, True),
        ("set_weather", {"id": "81a"}, True),
        ("capture", {"kind": "frame", "name": "riverwood_morning"}, True),
        ("set_time", {"hour": 21}, True),
        ("capture", {"kind": "frame", "name": "riverwood_night"}, True),
        ("measure", {"frames": 300, "name": "riverwood_night"}, True),
        ("capture", {"kind": "keyframe", "name": "rw", "steps": "game,improved", "hold": 30}, True),
        ("reload_module", {"source": module}, True),
        ("measure", {"frames": 300, "name": "riverwood_after_reload_2"}, True),
        ("capture", {"kind": "gbuffer", "history": 0}, True),
        ("set_time", {"hour": 13}, True),
        ("status", {}, True),
    ]


def plan_shared(module: str, shaders: Path) -> list[tuple[str, dict, bool]]:
    """For a session other workers are using: shader edits that change no pixel (a comment, or the file written back
    as it is, so the compile and the pipeline swap are real), a broken edit and its fix, three module reloads, and no
    cell moves."""
    rt = shaders / "RtShadow.hlsl"
    return [
        ("status", {}, True),
        ("reload_shader", {"file": "Lighting.hlsl", "old": "return float4(", "new": "return float4(" + "/* live */ "}, True),
        ("capture", {"kind": "frame", "name": "after_lighting_reload"}, True),
        ("reload_shader", {"file": "Lighting.hlsl", "old": "/* live */ ", "new": "/* live */ undefinedName + "}, False),
        ("reload_shader", {"file": "Lighting.hlsl", "old": "/* live */ undefinedName + ", "new": ""}, True),
        ("reload_shader", {"file": "RtShadow.hlsl", "source": str(rt)}, True),
        ("measure", {"frames": 300, "name": "shared_start"}, True),
        ("reload_module", {"source": module}, True),
        ("measure", {"frames": 300, "name": "after_module_reload_1"}, True),
        ("engine", {"commands": "view; weather"}, True),
        ("set_time", {"hour": 7}, True),
        ("capture", {"kind": "frame", "name": "hour7"}, True),
        ("set_weather", {"id": "81a"}, True),
        ("ini_set", {"section": "EngineRT", "key": "Weather", "value": "1"}, True),
        ("reload_module", {"source": module}, True),
        ("measure", {"frames": 300, "name": "after_module_reload_2"}, True),
        ("capture", {"kind": "keyframe", "name": "shared", "steps": "game,improved", "hold": 30}, True),
        ("capture", {"kind": "gbuffer", "history": 0}, True),
        ("reload_ini", {"keys": {"View": 0}}, True),
        ("set_time", {"hour": 13}, True),
        ("capture", {"kind": "frame", "name": "hour13"}, True),
        ("reload_module", {"source": module}, True),
        ("measure", {"frames": 300, "name": "after_module_reload_3"}, True),
        ("status", {}, True),
    ]


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--root", default="E:/raw-live")
    p.add_argument("--module", default="E:/wt/raw-live-build/raw_engine.dll")
    p.add_argument("--out", default="")
    p.add_argument("--plan", default="full", choices=["full", "shared"])
    p.add_argument("--game", default="H:/SteamLibrary/steamapps/common/Skyrim Special Edition")
    a = p.parse_args()
    q = LiveQueue(Path(a.root))
    rows, results = [], []
    steps = plan(a.module) if a.plan == "full" else plan_shared(a.module, Path(a.game) / "raw_shaders")
    for kind, args, want in steps:
        t0 = time.time()
        rid = q.submit(kind, "prove", args)
        r = q.wait(rid, 900) or {"id": rid, "ok": None, "error": "no answer in 900 s"}
        results.append(r)
        passed = r.get("ok") is want
        rows.append({"kind": kind, "ok": r.get("ok"), "expected": want, "passed": passed,
                     "latency_ms": r.get("latency_ms", round((time.time() - t0) * 1000)), "run_ms": r.get("run_ms"),
                     "queue_wait_ms": r.get("queue_wait_ms"), "uptime_s": r.get("session_uptime_s"),
                     "restarts": r.get("restarts"), "error": (r.get("error") or "")[:120]})
        print(f"{kind:14s} {'pass' if passed else 'FAIL'}  {rows[-1]['latency_ms']:>7} ms  uptime {rows[-1]['uptime_s']} s"
              f"  {rows[-1]['error']}", flush=True)
    summary = {"requests": len(rows), "passed": sum(r["passed"] for r in rows),
               "uptime_first_s": rows[0]["uptime_s"], "uptime_last_s": rows[-1]["uptime_s"],
               "restarts": rows[-1]["restarts"], "rows": rows, "results": results}
    out = Path(a.out) if a.out else Path(a.root) / f"proof-{time.strftime('%Y%m%d-%H%M%S')}.json"
    out.write_text(json.dumps(summary, indent=1), encoding="utf-8")
    print(f"{summary['passed']} of {summary['requests']} as expected; restarts {summary['restarts']}; {out}")
    return 0 if summary["passed"] == summary["requests"] else 1


if __name__ == "__main__":
    sys.exit(main())
