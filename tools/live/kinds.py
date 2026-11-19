"""What each request kind does in the running game (docs/LIVE_SESSION.md has the table). A handler gets the session,
the request's args and its result folder, and returns the data for result.json; it raises with the reason when
the request cannot be done. Original work, MIT.
"""
from __future__ import annotations

import json
import shlex
import shutil
import time
from pathlib import Path

from session import G, LiveSession

ENGINE = "raw_engine.dll"


def _wait_file(p: Path, timeout: float, s: LiveSession, what: str) -> str:
    start = time.time()
    while not p.exists():
        if time.time() - start > timeout or not G.running():
            raise RuntimeError(f"{what}: no answer in {timeout:.0f} s" if G.running() else f"{what}: the game exited")
        time.sleep(0.05)
    time.sleep(0.05)
    text = p.read_text(encoding="utf-8", errors="replace").strip()
    p.unlink(missing_ok=True)
    return text


def _engine_lines(s: LiveSession, commands: str, timeout: float = 300) -> list[dict]:
    """Runs engine commands (';'-separated) and returns the result lines they added to control_results.jsonl."""
    if not s.a.module:
        raise RuntimeError("this session runs without the engine module")
    res = s.inst.game / "raw_engine_out" / "control_results.jsonl"
    before = res.stat().st_size if res.exists() else 0
    s.engine(commands, timeout)
    if not res.exists():
        return []
    with open(res, "rb") as f:
        f.seek(before)
        return [json.loads(l) for l in f.read().decode("utf-8", "replace").splitlines() if l.strip().startswith("{")]


def _hot_wait(s: LiveSession, before: dict, key: str, timeout: float = 60) -> dict:
    t0 = time.time()
    while time.time() - t0 < timeout:
        now = s.hot()
        if now.get(key) != before.get(key) or (key == "reloads" and now.get("failures") != before.get("failures")):
            return now
        time.sleep(0.02)
    raise RuntimeError(f"no answer from the proxy's hot reload in {timeout:.0f} s (is [HotReload] on?)")


def reload_shader(s: LiveSession, a: dict, out: Path) -> dict:
    """{file, source} copies a whole file in; {file, old, new} edits the session's copy. Waits for the new pipelines,
    or for the compile error (ok false, the last good pipelines stay)."""
    f = s.inst.game / G.SHADER_COPY / a["file"]
    if not f.parent.is_dir():
        raise RuntimeError("this session has no shader copy (start it with --shaders)")
    text = Path(a["source"]).read_text(encoding="utf-8") if a.get("source") else f.read_text(encoding="utf-8")
    if "old" in a:
        if a["old"] not in text:
            raise RuntimeError(f"'{a['old'][:60]}' is not in {a['file']}")
        text = text.replace(a["old"], a.get("new", ""), 1)
    before = s.hot()
    t0 = time.time()
    f.write_text(text, encoding="utf-8")
    now = _hot_wait(s, before, "reloads")
    failed = now.get("failures") != before.get("failures")
    data = {"edit_to_applied_ms": round((time.time() - t0) * 1000), "compiled": not failed, "proxy": now}
    if failed:
        raise RuntimeError(f"compile failed, the last good shader stays: {now.get('error') or now}")
    return data


def reload_ini(s: LiveSession, a: dict, out: Path) -> dict:
    """{keys: {Key: value}} into [Lighting], applied by the proxy at once."""
    before = s.hot()
    t0 = time.time()
    s.reload(["ini"], {k: str(v) for k, v in a["keys"].items()})
    return {"edit_to_applied_ms": round((time.time() - t0) * 1000), "proxy": s.hot(), "was": before.get("params")}


def ini_set(s: LiveSession, a: dict, out: Path) -> dict:
    """{section, key, value}: one key in the session's d3d11_proxy.ini. [Lighting] applies live; other sections are
    read by their owner at start, so the answer says what reads it."""
    sec = a.get("section", "Lighting")
    if sec.lower() == "lighting":
        return reload_ini(s, {"keys": {a["key"]: a["value"]}}, out)
    p = s.inst.game / "d3d11_proxy.ini"
    lines, out_lines, inside, done = p.read_text(encoding="utf-8").splitlines(), [], False, False
    for line in lines:
        if line.strip().startswith("["):
            if inside and not done:
                out_lines.append(f"{a['key']}={a['value']}")
                done = True
            inside = line.strip().lower() == f"[{sec.lower()}]"
        elif inside and line.split("=")[0].strip() == a["key"]:
            line, done = f"{a['key']}={a['value']}", True
        out_lines.append(line)
    if not done:
        out_lines += ([] if inside else [f"[{sec}]"]) + [f"{a['key']}={a['value']}"]
    p.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    reader = "the engine module, at its next reload_module" if sec.lower() == "enginert" else "its owner, at the next restart"
    return {"written": f"[{sec}] {a['key']}={a['value']}", "applied_by": reader}


def reload_module(s: LiveSession, a: dict, out: Path) -> dict:
    """{source: path to the new build, name: raw_engine.dll}: copied beside the game under a versioned name, then the
    proxy stops the running copy (every patch restored), drops its callbacks and starts the new one."""
    name = a.get("name", ENGINE)
    s.module_gen = getattr(s, "module_gen", 0) + 1
    stem = Path(name).stem
    copy = s.inst.game / f"{stem}.r{s.module_gen}.dll"
    shutil.copy2(a["source"], copy)
    s.placed.append(copy)
    done = s.inst.game / "raw_module_done.txt"
    done.unlink(missing_ok=True)
    t0 = time.time()
    (s.inst.game / "raw_module_go.txt").write_text(f"reload={name}\nfile={copy.name}\n", encoding="utf-8")
    answer = _wait_file(done, 60, s, "reload_module")
    data = {"answer": answer, "file": copy.name, "request_to_answer_ms": round((time.time() - t0) * 1000)}
    if not answer.startswith("ok"):
        raise RuntimeError(answer)
    time.sleep(1.0)   # the new copy's first presents; a crash there shows in the next check
    if not G.running():
        raise RuntimeError(f"the game exited after the reload: {answer}")
    return data


def console(s: LiveSession, a: dict, out: Path) -> dict:
    line = a["command"].strip()
    if line.split()[0].lower() in ("coc", "cow", "load"):
        s.change_cell(line)
        s.last_move = line.lower()
    else:
        G.console(line)
        s.log(f"console: {line}")
    return {"typed": line}


def _view(s: LiveSession) -> dict:
    try:
        lines = _engine_lines(s, "view", 30)
        return next((l for l in lines if l.get("cmd") == "view"), {})
    except RuntimeError:
        return {}


def set_time(s: LiveSession, a: dict, out: Path) -> dict:
    console(s, {"command": f"set gamehour to {float(a['hour']):g}"}, out)
    time.sleep(1.5)
    v = _view(s)
    return {"asked": a["hour"], "game_hour": v.get("game_hour")}


def set_weather(s: LiveSession, a: dict, out: Path) -> dict:
    """{id: form id in hex}; force (fw, default) changes at once, force false (sw) lets the sky blend."""
    cmd = "fw" if a.get("force", True) else "sw"
    console(s, {"command": f"{cmd} {a['id']}"}, out)
    time.sleep(2.0)
    w = []
    try:
        w = [l for l in _engine_lines(s, "weather", 30) if l.get("cmd") == "weather"]
    except RuntimeError:
        pass
    return {"typed": f"{cmd} {a['id']}", "weather": w[-1] if w else None}


def move(s: LiveSession, a: dict, out: Path) -> dict:
    """{cell: editor id} (coc) or {worldspace, x, y} (cow, cell grid coordinates)."""
    line = f"coc {a['cell']}" if "cell" in a else f"cow {a['worldspace']} {int(a['x'])} {int(a['y'])}"
    # A coc to the cell the player stands in shows no loading screen and no lighting change, so change_cell
    # types it again and the keys land in the world (ray tracing worker, 2026-11-19). The same target is a no-op.
    if line.lower() == getattr(s, "last_move", "") and not a.get("again"):
        return {"typed": None, "already_there": line, "view": _view(s)}
    s.change_cell(line)
    s.last_move = line.lower()
    return {"typed": line, "view": _view(s)}


def _png(bmp: Path) -> Path:
    try:
        from PIL import Image
    except ImportError:
        return bmp
    png = bmp.with_suffix(".png")
    Image.open(bmp).convert("RGB").save(png, compress_level=6)
    bmp.unlink()
    return png


def capture(s: LiveSession, a: dict, out: Path) -> dict:
    """{kind: frame | keyframe | gbuffer}. frame: the backbuffer through the engine module. keyframe: the proxy's
    capture set ({steps, hold}: the lighting steps, debug views included, e.g. view_classes). gbuffer: one frame
    ledger (every target after its last write; {history: 0|1|2}). The frame is the session's resolution."""
    res = a.get("resolution")
    if res and res != s.a.resolution:
        raise RuntimeError(f"the session runs at {s.a.resolution}; a capture at {res} needs a restart request with that resolution")
    kind, name = a.get("kind", "frame"), a.get("name", "frame")
    if kind == "frame":
        _engine_lines(s, f"capture {name}", 60)
        bmp = s.inst.game / "raw_engine_out" / f"{name}.bmp"
        if not bmp.exists():
            raise RuntimeError("the engine module wrote no capture (backbuffer format not readable?)")
        dst = out / bmp.name
        shutil.move(str(bmp), dst)
        return {"files": [_png(dst).name], "resolution": s.a.resolution}
    if kind == "keyframe":
        s.out = out
        steps = a.get("steps", "improved")
        s.keyframe(name, {"steps": ",".join(steps) if isinstance(steps, list) else steps, "hold": a.get("hold", 40)})
        files = [_png(b).name for b in sorted(out.glob("*.bmp"))]
        rows = out / "harness_results.jsonl"
        return {"files": files, "rows": [json.loads(l) for l in rows.read_text().splitlines()] if rows.exists() else []}
    if kind == "gbuffer":
        done = s.inst.game / "raw_ledger_done.txt"
        done.unlink(missing_ok=True)
        (s.inst.game / "raw_ledger_go.txt").write_text(f"dir={out / 'ledger'}\nhistory={int(a.get('history', 0))}\n", encoding="utf-8")
        folder = Path(_wait_file(done, 180, s, "gbuffer capture"))
        return {"capture": str(folder), "files": len(list(folder.rglob("*"))) if folder.exists() else 0,
                "read_with": "tools/frame_buffers/inventory.py or gbuffer.py <capture>"}
    raise ValueError(f"capture kind {kind}: frame, keyframe or gbuffer")


def measure(s: LiveSession, a: dict, out: Path) -> dict:
    """{frames: 300, name}: frame time over N frames and the GPU time of each pass (GpuTiming=1), with the proxy's
    per-frame counters."""
    frames, name = int(a.get("frames", 300)), a.get("name", "measure")
    lines = _engine_lines(s, f"measure {name} {frames}", 60 + frames / 10)
    m = [l for l in lines if l.get("cmd") == "measure"]
    if not m:
        raise RuntimeError("the engine module returned no measure line")
    return m[-1]


def engine(s: LiveSession, a: dict, out: Path) -> dict:
    """{commands: "a; b"}: any engine-control command (set, grids, cascades, papyrus, view, weather, ...)."""
    return {"lines": _engine_lines(s, a["commands"], float(a.get("timeout", 300)))}


def script(s: LiveSession, a: dict, out: Path) -> dict:
    """{lines: [...]}: harness script lines (tools/harness/run.py), for command files written for `listen`."""
    s.out = out
    first = len(s.report["steps"])
    for line in a["lines"]:
        s.check_cancel()   # req.py cancel <id> stops a script between two lines
        line = line.split("#", 1)[0].strip()
        if line and line not in ("quit", "end"):
            s.command(shlex.split(line, posix=False))
    return {"log": s.report["steps"][first:], "files": sorted(p.name for p in out.iterdir())}


HANDLERS = {"reload_shader": reload_shader, "reload_ini": reload_ini, "ini_set": ini_set, "reload_module": reload_module,
            "console": console, "set_time": set_time, "set_weather": set_weather, "move": move, "capture": capture,
            "measure": measure, "engine": engine, "script": script, "keyframe": lambda s, a, o: capture(s, {**a, "kind": "keyframe"}, o)}
