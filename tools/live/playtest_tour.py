"""Automated live Skyrim SE playtest tour & visual audit harness (Wave 63/64+).

Capabilities:
  1. Live launch mode (--launch): uses the proven headless `SkyrimCustom.ini`
     `[General] sStartingCell=` + `sStartingConsoleCommand=` + `raw_harness_go.txt`
     GPU backbuffer capture protocol (zero Main-Menu focus-lock hang!), verifies
     `raw_engine_out/boot_selfcheck.txt` and `bridge_live_telemetry.json`, converts
     captured BMPs to PNGs, runs `visual_heuristics.py`, emits `playtest_report.json` +
     `playtest_report.md`, restores `SkyrimCustom.ini` bit-exact, and cleans
     `raw_engine_out/boot_sentinel.state` on clean harness exit.
  2. Offline audit mode (--offline-dir): audits an existing screenshot directory
     + optional `raw_engine_out` telemetry directory without launching Skyrim SE,
     producing identical `playtest_report.json` & `playtest_report.md` artifacts.

Original work, MIT.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
from pathlib import Path
from typing import Any, Sequence

from PIL import Image

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import full_pipeline_audit as fpa  # noqa: E402
import visual_heuristics as vh  # noqa: E402


DEFAULT_GAME_DIR = fpa.DEFAULT_GAME_DIR
DEFAULT_DEBUG_VIEWS = [0, 28, 29, 30, 31, 32, 33, 34, 35, 36]

DEBUG_VIEW_ID_TO_STEP: dict[int, str] = {
    0: "improved",
    1: "view_normals",
    2: "view_sun_shadow",
    3: "view_ambient",
    4: "view_sky_vis",
    5: "view_gi_bounce",
    6: "view_rt_refl",
    8: "view_weather",
    11: "view_cloud_shadow",
    12: "view_classes",
    16: "view_albedo",
    19: "view_char_light",
    22: "view_wireframe",
    24: "view_roughness",
    25: "view_metallic",
    26: "view_parallax",
    27: "view_gi_contrast",
    28: "view_full_classes",
    29: "view_depth_edge",
    30: "view_shadow_stab",
    31: "view_moss_mask",
    32: "view_skin_sss",
    33: "view_nan_hdr",
    34: "view_flora_trees",
    35: "view_motion_outline",
    36: "view_fire_froxel",
}


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
    starting_cell: str = "Riverwood",
    console_cmd: str = "fw 81a; set gamehour to 13",
    hold_frames: int = 35,
    burst: int = 6,
    stride: int = 3,
    cam_sweep: bool = True,
    boot_wait_sec: float = 120.0,
    custom_ini_path: Path = fpa.DEFAULT_CUSTOM_INI,
) -> dict[str, Any]:
    """Launches SkyrimSE.exe headlessly into `starting_cell` via `bat raw_scene_init`, readiness wait,
    headless `cam_sweep` camera motion, and `raw_harness_go.txt`."""
    out_dir.mkdir(parents=True, exist_ok=True)
    engine_out = game_dir / "raw_engine_out"
    exe = game_dir / "SkyrimSE.exe"
    if not exe.exists():
        raise FileNotFoundError(f"SkyrimSE.exe not found at {exe}")
    if _is_skyrim_running():
        raise RuntimeError("SkyrimSE.exe is already running; close it before starting playtest_tour")

    steps = [DEBUG_VIEW_ID_TO_STEP[v] for v in debug_views if v in DEBUG_VIEW_ID_TO_STEP]
    if "improved" not in steps:
        steps.insert(0, "improved")

    env = dict(os.environ, SteamAppId="489830", SteamGameId="489830")
    go_file = game_dir / "raw_harness_go.txt"
    done_file = game_dir / "raw_harness_done.txt"
    status_file = game_dir / "raw_harness_status.txt"
    engine_go = game_dir / "raw_engine_go.txt"
    scene_init_path = game_dir / fpa.SCENE_INIT_FILENAME
    boot_info: dict[str, Any] = {"present": False}
    bridge_info: dict[str, Any] = {"present": False}
    sentinel_cleaned = False

    with fpa.SkyrimCustomIniGuard(custom_ini_path) as ini_guard:
        proc: subprocess.Popen[bytes] | None = None
        try:
            fpa.write_scene_init_bat(game_dir, console_cmd)
            ini_guard.write_scene_boot(starting_cell, fpa.SCENE_INIT_BAT_CMD)
            for stale in (go_file, done_file, status_file, engine_go):
                stale.unlink(missing_ok=True)
            proc = subprocess.Popen([str(exe)], cwd=str(game_dir), env=env)
            fpa.wait_for_world_ready(game_dir, engine_out, proc=proc, timeout_sec=min(90.0, boot_wait_sec * 0.55))
            if proc.poll() is None:
                post_cmds: list[str] = []
                scene_hr = fpa.parse_scene_gamehour(console_cmd)
                if scene_hr is not None:
                    post_cmds.append(f"set_time {max(0.0, min(23.99, scene_hr)):g}")
                post_cmds.append("dismiss_modal")
                fpa.emit_engine_commands(game_dir, post_cmds)
                time.sleep(0.45)

                pre_cmds: list[str] = ["dismiss_modal"]
                if cam_sweep:
                    pre_cmds.append(fpa.build_cam_sweep_command())
                fpa.emit_engine_commands(game_dir, pre_cmds)
                time.sleep(0.20)
                go_file.write_text(
                    "\n".join([
                        f"scene={starting_cell.lower()}",
                        "keyframe=tour",
                        f"steps={','.join(steps)}",
                        f"hold={int(hold_frames)}",
                        f"burst={max(1, min(16, int(burst)))}",
                        f"stride={max(1, min(30, int(stride)))}",
                        f"dir={out_dir}",
                    ]) + "\n",
                    encoding="utf-8",
                )
                deadline = time.time() + boot_wait_sec
                while time.time() < deadline:
                    if done_file.exists():
                        break
                    if proc.poll() is not None:
                        break
                    time.sleep(0.5)
            boot_info = read_boot_selfcheck(engine_out)
            bridge_info = read_bridge_telemetry(game_dir, engine_out)
        finally:
            fpa._stop_skyrim(proc)
            for tmpf in (go_file, done_file, engine_go, scene_init_path):
                tmpf.unlink(missing_ok=True)
            sentinel_cleaned = cleanup_boot_sentinel(engine_out)

    # Convert BMP captures to PNGs and audit all frames
    for bmp in sorted(out_dir.glob("*.bmp")):
        png = bmp.with_suffix(".png")
        with Image.open(bmp) as im:
            im.convert("RGB").save(png, compress_level=6)
        bmp.unlink(missing_ok=True)

    captured_files = sorted(out_dir.glob("*.png"))
    frames = [vh.audit_image(p, label=p.name) for p in captured_files]
    hist: dict[str, int] = {}
    for rep in frames:
        for dname in rep["defects"]:
            hist[dname] = hist.get(dname, 0) + 1

    report: dict[str, Any] = {
        "mode": "live",
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "game_dir": str(game_dir),
        "starting_cell": starting_cell,
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
    ap.add_argument("--launch", action="store_true", help="Launch live headless SkyrimSE.exe playtest tour")
    ap.add_argument("--cell", default="Riverwood", help="sStartingCell for --launch mode")
    ap.add_argument("--console-cmd", default="fw 81a; set gamehour to 13", help="sStartingConsoleCommand for --launch mode")
    ap.add_argument("--views", default="0,28,29,30,31,32,33,34,35,36", help="Comma-separated DebugView IDs")
    ap.add_argument("--hold", type=int, default=35, help="Frames to hold per debug step")
    ap.add_argument("--boot-wait", type=float, default=90.0, help="Seconds timeout for headless capture")
    args = ap.parse_args(argv)

    out_dir = Path(args.out_dir)
    game_dir = Path(args.game_dir)

    if args.offline_dir:
        eout = Path(args.engine_out_dir) if args.engine_out_dir else None
        rep = run_offline_tour(Path(args.offline_dir), out_dir, game_dir=game_dir, engine_out_dir=eout, glob_pattern=args.glob)
    elif args.launch:
        views = [int(v.strip()) for v in args.views.split(",") if v.strip()]
        rep = run_live_tour(
            game_dir,
            out_dir,
            debug_views=views,
            starting_cell=args.cell,
            console_cmd=args.console_cmd,
            hold_frames=args.hold,
            boot_wait_sec=args.boot_wait,
        )
    else:
        ap.error("Specify either --offline-dir <dir> or --launch")
        return 2

    print(json.dumps(rep["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
