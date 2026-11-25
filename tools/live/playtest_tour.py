"""Automated live Skyrim SE playtest tour & visual audit harness (Wave 63+).

Capabilities:
  1. Live launch mode (--launch): launches SkyrimSE.exe with SteamAppId=489830 /
     SteamGameId=489830, verifies raw_engine_out/boot_selfcheck.txt and
     bridge_live_telemetry.json, captures live frames via Windows GDI / Pillow
     ImageGrab, cycles RAW debug views (0, 28, 29, 30, 31, 32, 33), runs
     visual_heuristics.py on all frames, writes playtest_report.json +
     playtest_report.md, and cleans raw_engine_out/boot_sentinel.state on exit.
  2. Offline audit mode (--offline-dir): audits an existing screenshot directory
     + optional raw_engine_out telemetry directory without launching Skyrim SE,
     producing identical playtest_report.json & playtest_report.md artifacts.

Original work, MIT.
"""
from __future__ import annotations

import argparse
import ctypes
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Sequence

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import visual_heuristics as vh  # noqa: E402


DEFAULT_GAME_DIR = Path(r"H:\SteamLibrary\steamapps\common\Skyrim Special Edition")
DEFAULT_DEBUG_VIEWS = [0, 28, 29, 30, 31, 32, 33]


def read_boot_selfcheck(engine_out_dir: Path) -> dict[str, Any]:
    """Parses raw_engine_out/boot_selfcheck.txt if present."""
    p = engine_out_dir / "boot_selfcheck.txt"
    if not p.exists():
        return {"present": False, "path": str(p)}
    text = p.read_text(encoding="utf-8", errors="replace")
    overall = "UNKNOWN"
    for line in text.splitlines():
        if "Overall:" in line:
            overall = line.strip()
            break
    return {
        "present": True,
        "path": str(p),
        "bytes": len(text.encode("utf-8")),
        "summary_line": overall,
        "passed": ("Overall: PASS" in text),
    }


def read_bridge_telemetry(game_dir: Path, engine_out_dir: Path) -> dict[str, Any]:
    """Loads bridge_live_telemetry.json from game_dir or raw_engine_out."""
    for cand in (game_dir / "bridge_live_telemetry.json", engine_out_dir / "bridge_live_telemetry.json"):
        if cand.exists():
            try:
                data = json.loads(cand.read_text(encoding="utf-8", errors="replace"))
                return {"present": True, "path": str(cand), "data": data}
            except Exception as exc:
                return {"present": True, "path": str(cand), "error": str(exc)}
    return {"present": False}


def cleanup_boot_sentinel(engine_out_dir: Path) -> bool:
    """Removes raw_engine_out/boot_sentinel.state on clean harness exit to avoid false crash-loop rollovers."""
    sentinel = engine_out_dir / "boot_sentinel.state"
    if sentinel.exists():
        sentinel.unlink(missing_ok=True)
        return True
    return False


def capture_desktop_frame(out_png: Path) -> bool:
    """Captures the live primary display via Pillow ImageGrab (GDI BitBlt)."""
    try:
        from PIL import ImageGrab
    except ImportError:
        return False
    out_png.parent.mkdir(parents=True, exist_ok=True)
    im = ImageGrab.grab()
    im.convert("RGB").save(out_png, compress_level=6)
    return out_png.exists()


def set_proxy_debug_view(game_dir: Path, view_id: int) -> bool:
    """Updates [Lighting] DebugView=<id> in d3d11_proxy.ini for hot-reload cycling."""
    ini_path = game_dir / "d3d11_proxy.ini"
    if not ini_path.exists():
        return False
    lines = ini_path.read_text(encoding="utf-8", errors="replace").splitlines()
    out_lines: list[str] = []
    in_lighting = False
    replaced = False
    for line in lines:
        stripped = line.strip()
        if stripped.startswith("[") and stripped.endswith("]"):
            if in_lighting and not replaced:
                out_lines.append(f"DebugView={int(view_id)}")
                replaced = True
            in_lighting = stripped.lower() == "[lighting]"
            out_lines.append(line)
        elif in_lighting and stripped.lower().startswith("debugview="):
            out_lines.append(f"DebugView={int(view_id)}")
            replaced = True
        else:
            out_lines.append(line)
    if not replaced:
        if not in_lighting:
            out_lines.append("[Lighting]")
        out_lines.append(f"DebugView={int(view_id)}")
    ini_path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
    return True


def _is_skyrim_running() -> bool:
    res = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq SkyrimSE.exe", "/NH"],
        capture_output=True,
        text=True,
        check=False,
    )
    return "SkyrimSE.exe" in (res.stdout or "")


def _format_markdown_report(report: dict[str, Any]) -> str:
    lines = [
        "# RAW Live Playtest Tour Report",
        "",
        f"- Mode: `{report['mode']}`",
        f"- Timestamp: `{report['timestamp']}`",
        f"- Frames Audited: `{report['summary']['frames_audited']}`",
        f"- Clean Frames: `{report['summary']['clean_frames']}`",
        f"- Flagged Frames: `{report['summary']['flagged_frames']}`",
        f"- Sentinel Cleaned: `{report['sentinel_cleaned']}`",
        "",
        "## Boot Self-Check & Telemetry",
        "",
        f"- `boot_selfcheck.txt`: present=`{report['boot_selfcheck'].get('present')}` "
        f"passed=`{report['boot_selfcheck'].get('passed', False)}`",
        f"- `bridge_live_telemetry.json`: present=`{report['bridge_telemetry'].get('present')}`",
        "",
        "## Defect Histogram",
        "",
    ]
    hist = report["summary"].get("defect_histogram", {})
    if not hist:
        lines.append("- None (0 visual defects detected across all frames).")
    else:
        for k, v in sorted(hist.items()):
            lines.append(f"- `{k}`: `{v}` frame(s)")
    lines.append("")
    lines.append("## Per-Frame Results")
    lines.append("")
    lines.append("| Frame | Resolution | Passed | Defects |")
    lines.append("| :--- | :--- | :--- | :--- |")
    for item in report.get("frames", []):
        defects = ", ".join(item.get("defects", [])) or "none"
        lines.append(
            f"| `{Path(item['image']).name}` | {item['width']}x{item['height']} | "
            f"{'PASS' if item['passed'] else 'FAIL'} | {defects} |"
        )
    lines.append("")
    return "\n".join(lines)


def run_offline_tour(
    shots_dir: Path,
    out_dir: Path,
    game_dir: Path | None = None,
    engine_out_dir: Path | None = None,
    glob_pattern: str = "*.png",
) -> dict[str, Any]:
    """Audits an existing folder of screenshots and emits playtest_report.json + playtest_report.md."""
    out_dir.mkdir(parents=True, exist_ok=True)
    gdir = game_dir or DEFAULT_GAME_DIR
    edir = engine_out_dir or (gdir / "raw_engine_out")

    try:
        audit = vh.audit_directory(shots_dir, glob_pattern=glob_pattern)
        boot_info = read_boot_selfcheck(edir)
        bridge_info = read_bridge_telemetry(gdir, edir)
    finally:
        sentinel_cleaned = cleanup_boot_sentinel(edir) if edir.exists() else False

    report: dict[str, Any] = {
        "mode": "offline",
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "shots_dir": str(shots_dir),
        "boot_selfcheck": boot_info,
        "bridge_telemetry": bridge_info,
        "sentinel_cleaned": sentinel_cleaned,
        "summary": {
            "frames_audited": audit["image_count"],
            "clean_frames": audit["clean_images"],
            "flagged_frames": audit["flagged_images"],
            "defect_histogram": audit["defect_histogram"],
        },
        "frames": audit["images"],
    }

    (out_dir / "playtest_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (out_dir / "playtest_report.md").write_text(_format_markdown_report(report), encoding="utf-8")
    return report


def run_live_tour(
    game_dir: Path,
    out_dir: Path,
    debug_views: Sequence[int] = DEFAULT_DEBUG_VIEWS,
    boot_wait_sec: float = 25.0,
    view_dwell_sec: float = 2.0,
) -> dict[str, Any]:
    """Launches SkyrimSE.exe with SteamAppId=489830, cycles debug views, captures & audits live frames."""
    out_dir.mkdir(parents=True, exist_ok=True)
    engine_out = game_dir / "raw_engine_out"
    exe = game_dir / "SkyrimSE.exe"
    if not exe.exists():
        raise FileNotFoundError(f"SkyrimSE.exe not found at {exe}")
    if _is_skyrim_running():
        raise RuntimeError("SkyrimSE.exe is already running; close it before starting playtest_tour")

    env = dict(os.environ, SteamAppId="489830", SteamGameId="489830")
    proc: subprocess.Popen[bytes] | None = None
    captured_files: list[Path] = []
    boot_info: dict[str, Any] = {"present": False}
    bridge_info: dict[str, Any] = {"present": False}
    sentinel_cleaned = False

    try:
        proc = subprocess.Popen([str(exe)], cwd=str(game_dir), env=env)
        deadline = time.time() + boot_wait_sec
        while time.time() < deadline:
            if proc.poll() is not None:
                raise RuntimeError(f"SkyrimSE.exe exited prematurely during boot (rc={proc.returncode})")
            if (engine_out / "boot_selfcheck.txt").exists():
                break
            time.sleep(0.5)

        boot_info = read_boot_selfcheck(engine_out)
        bridge_info = read_bridge_telemetry(game_dir, engine_out)

        for vid in debug_views:
            set_proxy_debug_view(game_dir, vid)
            time.sleep(view_dwell_sec)
            vname = vh.DEBUG_VIEW_NAMES.get(vid, f"view{vid}")
            shot_path = out_dir / f"live_view_{vid:02d}_{vname}.png"
            if capture_desktop_frame(shot_path):
                captured_files.append(shot_path)
        set_proxy_debug_view(game_dir, 0)
    finally:
        if proc is not None and proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=10.0)
            except subprocess.TimeoutExpired:
                proc.kill()
        if _is_skyrim_running():
            subprocess.run(["taskkill", "/IM", "SkyrimSE.exe", "/F"], capture_output=True, check=False)
            time.sleep(1.5)
        sentinel_cleaned = cleanup_boot_sentinel(engine_out)

    frames = [vh.audit_image(p, label=p.name) for p in captured_files]
    hist: dict[str, int] = {}
    for rep in frames:
        for dname in rep["defects"]:
            hist[dname] = hist.get(dname, 0) + 1

    report: dict[str, Any] = {
        "mode": "live",
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "game_dir": str(game_dir),
        "boot_selfcheck": boot_info,
        "bridge_telemetry": bridge_info,
        "sentinel_cleaned": sentinel_cleaned,
        "summary": {
            "frames_audited": len(frames),
            "clean_frames": sum(1 for r in frames if r["passed"]),
            "flagged_frames": sum(1 for r in frames if not r["passed"]),
            "defect_histogram": hist,
        },
        "frames": frames,
    }

    (out_dir / "playtest_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (out_dir / "playtest_report.md").write_text(_format_markdown_report(report), encoding="utf-8")
    return report


def _main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="RAW Automated Live Playtest Tour & Visual Audit Harness")
    ap.add_argument("--game-dir", default=str(DEFAULT_GAME_DIR), help="Skyrim SE installation directory")
    ap.add_argument("--out-dir", required=True, help="Output directory for playtest_report.{json,md} & captures")
    ap.add_argument("--offline-dir", help="Audit an existing screenshot directory without launching Skyrim SE")
    ap.add_argument("--engine-out-dir", help="Optional raw_engine_out path for offline mode")
    ap.add_argument("--glob", default="*.png", help="Filename glob for --offline-dir")
    ap.add_argument("--launch", action="store_true", help="Launch live SkyrimSE.exe playtest tour")
    ap.add_argument("--views", default="0,28,29,30,31,32,33", help="Comma-separated DebugView IDs to cycle")
    ap.add_argument("--boot-wait", type=float, default=25.0, help="Seconds to wait for boot_selfcheck.txt")
    ap.add_argument("--dwell", type=float, default=2.0, help="Seconds to dwell per debug view before capture")
    args = ap.parse_args(argv)

    out_dir = Path(args.out_dir)
    game_dir = Path(args.game_dir)

    if args.offline_dir:
        eout = Path(args.engine_out_dir) if args.engine_out_dir else None
        rep = run_offline_tour(Path(args.offline_dir), out_dir, game_dir=game_dir, engine_out_dir=eout, glob_pattern=args.glob)
    elif args.launch:
        views = [int(v.strip()) for v in args.views.split(",") if v.strip()]
        rep = run_live_tour(game_dir, out_dir, debug_views=views, boot_wait_sec=args.boot_wait, view_dwell_sec=args.dwell)
    else:
        ap.error("Specify either --offline-dir <dir> or --launch")
        return 2

    print(json.dumps(rep["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
