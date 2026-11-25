"""100% Headless Multi-Scene, Multi-Weather, Multi-TOD, Multi-Debug-View Live Skyrim SE Auditor.

Uses the proven `SkyrimCustom.ini` `[General] sStartingCell=` + `sStartingConsoleCommand=` +
`raw_harness_go.txt` / `raw_harness_done.txt` GPU backbuffer capture protocol (zero Main-Menu
focus-lock hang!). Always restores `SkyrimCustom.ini` bit-exact and removes
`raw_engine_out/boot_sentinel.state` in `finally:`.

Supports:
  - `--profile quick` (4 canonical scenes: riverwood_day, inn_interior, septimus_cave, windhelm_ext_snow)
  - `--profile full`  (all 10 canonical scenes across biomes, weather, time-of-day & interiors)
  - `--offline-dir <dir>` (audits an existing multi-scene capture directory without launching Skyrim SE)

Original work, MIT.
"""
from __future__ import annotations

import argparse
import dataclasses
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

import visual_heuristics as vh  # noqa: E402


DEFAULT_GAME_DIR = Path(r"H:\SteamLibrary\steamapps\common\Skyrim Special Edition")
DEFAULT_CUSTOM_INI = Path.home() / "Documents" / "My Games" / "Skyrim Special Edition" / "SkyrimCustom.ini"

DEFAULT_AUDIT_STEPS: tuple[str, ...] = (
    "improved",
    "game",
    "view_normals",
    "view_sun_shadow",
    "view_albedo",
    "view_full_classes",
    "view_depth_edge",
    "view_shadow_stab",
    "view_moss_mask",
    "view_skin_sss",
    "view_nan_hdr",
    "view_flora_trees",
    "view_motion_outline",
    "view_fire_froxel",
    "water_depth",
    "water_flow_diag",
)


@dataclasses.dataclass(frozen=True)
class AuditSceneSpec:
    name: str
    starting_cell: str
    console_cmd: str
    description: str
    in_quick: bool = False


CANONICAL_SCENES: tuple[AuditSceneSpec, ...] = (
    AuditSceneSpec("riverwood_day", "Riverwood", "fw 81a; set gamehour to 13", "Riverwood midday clear exterior (trees, pine, actors, river)", in_quick=True),
    AuditSceneSpec("riverwood_sunset", "Riverwood", "fw 81a; set gamehour to 19.5", "Riverwood golden-hour twilight & shadows"),
    AuditSceneSpec("riverwood_rain", "Riverwood", "fw c8220; set gamehour to 14", "Riverwood heavy rain, wet puddles & overcast"),
    AuditSceneSpec("riverwood_night", "Riverwood", "fw 81a; set gamehour to 23.5", "Riverwood midnight aurora, torches & dark exposure"),
    AuditSceneSpec("riverwood_river", "Riverwood", "fw 81a; set gamehour to 11; tfc", "Riverwood shallow/deep river water & shoreline"),
    AuditSceneSpec("inn_interior", "RiverwoodSleepingGiantInn", "set gamehour to 20", "Sleeping Giant Inn interior hearth fire & candlelight", in_quick=True),
    AuditSceneSpec("septimus_cave", "SeptimusSignusOutpost", "set gamehour to 12", "Septimus Signus glacial ice cave interior", in_quick=True),
    AuditSceneSpec("windhelm_ext_snow", "WindhelmExterior01", "fw c8221; set gamehour to 14", "Windhelm snowy exterior, snow tracks & sky horizon", in_quick=True),
    AuditSceneSpec("whiterun_ext", "WhiterunExterior01", "fw 81a; set gamehour to 13", "Whiterun tundra plains, 3D grass & distant mountain LOD"),
    AuditSceneSpec("high_hrothgar", "HighHrothgarExterior01", "fw c8221; set gamehour to 15", "High Hrothgar alpine snowstorm, clouds & froxels"),
)


class SkyrimCustomIniGuard:
    """Saves exact raw bytes of SkyrimCustom.ini before editing and restores bit-exact on exit."""

    def __init__(self, ini_path: Path):
        self.ini_path = Path(ini_path)
        self._existed = False
        self._orig_bytes: bytes = b""

    def __enter__(self) -> "SkyrimCustomIniGuard":
        if self.ini_path.exists():
            self._existed = True
            self._orig_bytes = self.ini_path.read_bytes()
        else:
            self._existed = False
            self._orig_bytes = b""
        return self

    def write_scene_boot(self, starting_cell: str, console_cmd: str) -> None:
        self.ini_path.parent.mkdir(parents=True, exist_ok=True)
        base_text = self._orig_bytes.decode("utf-8", errors="replace") if self._existed else ""
        lines = base_text.splitlines()
        out_lines: list[str] = []
        in_general = False
        wrote_cell = False
        wrote_cmd = False
        wrote_active = False

        for line in lines:
            st = line.strip()
            if st.startswith("[") and st.endswith("]"):
                if in_general:
                    if not wrote_cell:
                        out_lines.append(f"sStartingCell={starting_cell}")
                        wrote_cell = True
                    if not wrote_cmd and console_cmd:
                        out_lines.append(f"sStartingConsoleCommand={console_cmd}")
                        wrote_cmd = True
                    if not wrote_active:
                        out_lines.append("bAlwaysActive=1")
                        wrote_active = True
                in_general = st.lower() == "[general]"
                out_lines.append(line)
            elif in_general and "=" in st:
                k = st.split("=", 1)[0].strip().lower()
                if k == "sstartingcell":
                    out_lines.append(f"sStartingCell={starting_cell}")
                    wrote_cell = True
                elif k == "sstartingconsolecommand":
                    if console_cmd:
                        out_lines.append(f"sStartingConsoleCommand={console_cmd}")
                    wrote_cmd = True
                elif k == "balwaysactive":
                    out_lines.append("bAlwaysActive=1")
                    wrote_active = True
                else:
                    out_lines.append(line)
            else:
                out_lines.append(line)

        if not (wrote_cell and wrote_active and (wrote_cmd or not console_cmd)):
            if not in_general:
                out_lines.append("[General]")
            if not wrote_cell:
                out_lines.append(f"sStartingCell={starting_cell}")
            if not wrote_cmd and console_cmd:
                out_lines.append(f"sStartingConsoleCommand={console_cmd}")
            if not wrote_active:
                out_lines.append("bAlwaysActive=1")

        self.ini_path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")

    def restore(self) -> None:
        if self._existed:
            self.ini_path.write_bytes(self._orig_bytes)
        else:
            self.ini_path.unlink(missing_ok=True)

    def __exit__(self, exc_type, exc, tb) -> None:
        self.restore()


def _is_skyrim_running() -> bool:
    res = subprocess.run(
        ["tasklist", "/FI", "IMAGENAME eq SkyrimSE.exe", "/NH"],
        capture_output=True,
        text=True,
        check=False,
    )
    return "SkyrimSE.exe" in (res.stdout or "")


def _stop_skyrim(proc: subprocess.Popen[bytes] | None) -> None:
    if proc is not None and proc.poll() is None:
        proc.terminate()
        try:
            proc.wait(timeout=8.0)
        except subprocess.TimeoutExpired:
            proc.kill()
    if _is_skyrim_running():
        subprocess.run(["taskkill", "/IM", "SkyrimSE.exe", "/F"], capture_output=True, check=False)
        for _ in range(15):
            if not _is_skyrim_running():
                break
            time.sleep(0.5)


def _convert_bmps_to_pngs(folder: Path) -> list[Path]:
    out_pngs: list[Path] = []
    for bmp in sorted(folder.glob("*.bmp")):
        png = bmp.with_suffix(".png")
        with Image.open(bmp) as im:
            im.convert("RGB").save(png, compress_level=6)
        bmp.unlink(missing_ok=True)
        out_pngs.append(png)
    return out_pngs


def _discover_scene_steps(folder: Path, scene_name: str, steps: Sequence[str]) -> dict[str, Path]:
    found: dict[str, Path] = {}
    for step in steps:
        for ext in (".png", ".bmp"):
            for cand in (
                folder / f"{scene_name}_{step}{ext}",
                folder / f"{scene_name}_audit_{step}{ext}",
                folder / f"{scene_name}_audit_{step}_f00{ext}",
                folder / f"{scene_name}_{step}_f00{ext}",
                folder / f"w64_{scene_name}_{step}{ext}",
                folder / f"{step}{ext}",
            ):
                if cand.exists():
                    found[step] = cand
                    break
            if step in found:
                break
    return found


def _discover_burst_sequences(folder: Path, scene_name: str, steps: Sequence[str]) -> dict[str, list[Path]]:
    """Discovers multi-frame burst captures (`*_f00.png`, `*_f01.png`, ...) per step."""
    out: dict[str, list[Path]] = {}
    for step in steps:
        for prefix in (f"{scene_name}_audit_{step}_f", f"{scene_name}_{step}_f", f"{step}_f"):
            files = sorted(folder.glob(f"{prefix}[0-9][0-9].png"))
            if not files:
                files = sorted(folder.glob(f"{prefix}[0-9][0-9].bmp"))
            if len(files) >= 2:
                out[step] = files
                break
    return out


def _format_markdown(report: dict[str, Any]) -> str:
    lines = [
        "# RAW Full-Pipeline Live Audit Report",
        "",
        f"- Mode: `{report['mode']}`",
        f"- Profile: `{report['profile']}`",
        f"- Timestamp: `{report['timestamp']}`",
        f"- Scenes Audited: `{report['summary']['scenes_audited']}`",
        f"- Clean Scenes: `{report['summary']['clean_scenes']}`",
        f"- Flagged Scenes: `{report['summary']['flagged_scenes']}`",
        f"- Sentinel Cleaned: `{report['sentinel_cleaned']}`",
        "",
        "## Per-Scene Heuristic Results",
        "",
        "| Scene | Steps | Heuristics Run | Verdict | Defects |",
        "| :--- | :--- | :--- | :--- | :--- |",
    ]
    for sc in report.get("scenes", []):
        defs = ", ".join(sc.get("defects", [])) or "none"
        lines.append(
            f"| `{sc['scene']}` | {len(sc.get('steps_present', []))} | "
            f"{sc.get('heuristics_run', 0)} | {'PASS' if sc['passed'] else 'FAIL'} | {defs} |"
        )
    lines.append("")
    return "\n".join(lines)


def run_offline_audit(
    captures_dir: Path,
    out_dir: Path,
    profile: str = "quick",
    steps: Sequence[str] = DEFAULT_AUDIT_STEPS,
    engine_out_dir: Path | None = None,
) -> dict[str, Any]:
    """Audits an existing multi-scene capture directory without launching Skyrim SE."""
    out_dir.mkdir(parents=True, exist_ok=True)
    selected = [s for s in CANONICAL_SCENES if (profile == "full" or s.in_quick)]
    eout = engine_out_dir or (captures_dir / "raw_engine_out")
    telemetry = None
    if eout.exists():
        telemetry = {
            "engine_out_dir": eout,
            "bridge_telemetry_path": eout / "bridge_live_telemetry.json",
            "boot_selfcheck_path": eout / "boot_selfcheck.txt",
            "harness_results_path": captures_dir / "harness_results.jsonl" if (captures_dir / "harness_results.jsonl").exists() else None,
        }

    scene_reports: list[dict[str, Any]] = []
    for spec in selected:
        sub = captures_dir / spec.name
        search_dir = sub if sub.is_dir() else captures_dir
        step_map = _discover_scene_steps(search_dir, spec.name, steps)
        burst_map = _discover_burst_sequences(search_dir, spec.name, steps)
        if not step_map and not burst_map:
            continue
        rep = vh.audit_scene_bundle(step_map, scene_name=spec.name, telemetry=telemetry, burst_sequences=burst_map)
        scene_reports.append(rep)

    sentinel_cleaned = False
    if eout.exists():
        sent = eout / "boot_sentinel.state"
        if sent.exists():
            sent.unlink(missing_ok=True)
            sentinel_cleaned = True

    report: dict[str, Any] = {
        "mode": "offline",
        "profile": profile,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "captures_dir": str(captures_dir),
        "sentinel_cleaned": sentinel_cleaned,
        "summary": {
            "scenes_audited": len(scene_reports),
            "clean_scenes": sum(1 for r in scene_reports if r["passed"]),
            "flagged_scenes": sum(1 for r in scene_reports if not r["passed"]),
        },
        "scenes": scene_reports,
    }
    (out_dir / "full_pipeline_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (out_dir / "full_pipeline_report.md").write_text(_format_markdown(report), encoding="utf-8")
    return report


def run_live_audit(
    game_dir: Path,
    out_dir: Path,
    profile: str = "quick",
    steps: Sequence[str] = DEFAULT_AUDIT_STEPS,
    custom_ini_path: Path = DEFAULT_CUSTOM_INI,
    hold_frames: int = 35,
    burst: int = 6,
    stride: int = 3,
    boot_timeout_sec: float = 120.0,
) -> dict[str, Any]:
    """Runs the 100% headless multi-scene live SkyrimSE.exe audit using sStartingCell + raw_harness_go.txt."""
    out_dir.mkdir(parents=True, exist_ok=True)
    exe = game_dir / "SkyrimSE.exe"
    if not exe.exists():
        raise FileNotFoundError(f"SkyrimSE.exe not found at {exe}")
    if _is_skyrim_running():
        raise RuntimeError("SkyrimSE.exe is already running; close it before starting full_pipeline_audit")

    burst_n = max(1, min(16, int(burst)))
    stride_n = max(1, min(30, int(stride)))
    selected = [s for s in CANONICAL_SCENES if (profile == "full" or s.in_quick)]
    engine_out = game_dir / "raw_engine_out"
    env = dict(os.environ, SteamAppId="489830", SteamGameId="489830")
    scene_reports: list[dict[str, Any]] = []
    sentinel_cleaned = False

    with SkyrimCustomIniGuard(custom_ini_path) as ini_guard:
        try:
            for spec in selected:
                scene_out = out_dir / spec.name
                scene_out.mkdir(parents=True, exist_ok=True)
                ini_guard.write_scene_boot(spec.starting_cell, spec.console_cmd)

                go_file = game_dir / "raw_harness_go.txt"
                done_file = game_dir / "raw_harness_done.txt"
                done_file.unlink(missing_ok=True)
                go_lines = [
                    f"scene={spec.name}",
                    "keyframe=audit",
                    f"steps={','.join(steps)}",
                    f"hold={int(hold_frames)}",
                    f"burst={burst_n}",
                    f"stride={stride_n}",
                    f"dir={scene_out}",
                ]
                go_file.write_text("\n".join(go_lines) + "\n", encoding="utf-8")

                proc: subprocess.Popen[bytes] | None = None
                try:
                    proc = subprocess.Popen([str(exe)], cwd=str(game_dir), env=env)
                    deadline = time.time() + boot_timeout_sec
                    while time.time() < deadline:
                        if done_file.exists():
                            break
                        if proc.poll() is not None:
                            break
                        time.sleep(0.5)
                finally:
                    _stop_skyrim(proc)
                    go_file.unlink(missing_ok=True)
                    done_file.unlink(missing_ok=True)

                _convert_bmps_to_pngs(scene_out)
                step_map = _discover_scene_steps(scene_out, spec.name, steps)
                burst_map = _discover_burst_sequences(scene_out, spec.name, steps)
                bridge_cand = game_dir / "bridge_live_telemetry.json"
                if not bridge_cand.exists():
                    bridge_cand = engine_out / "bridge_live_telemetry.json"
                telemetry = {
                    "engine_out_dir": engine_out,
                    "bridge_telemetry_path": bridge_cand,
                    "boot_selfcheck_path": engine_out / "boot_selfcheck.txt",
                    "harness_results_path": scene_out / "harness_results.jsonl",
                }
                rep = vh.audit_scene_bundle(step_map, scene_name=spec.name, telemetry=telemetry, burst_sequences=burst_map)
                scene_reports.append(rep)
        finally:
            _stop_skyrim(None)
            sent = engine_out / "boot_sentinel.state"
            if sent.exists():
                sent.unlink(missing_ok=True)
                sentinel_cleaned = True

    report: dict[str, Any] = {
        "mode": "live",
        "profile": profile,
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "game_dir": str(game_dir),
        "sentinel_cleaned": sentinel_cleaned,
        "summary": {
            "scenes_audited": len(scene_reports),
            "clean_scenes": sum(1 for r in scene_reports if r["passed"]),
            "flagged_scenes": sum(1 for r in scene_reports if not r["passed"]),
        },
        "scenes": scene_reports,
    }
    (out_dir / "full_pipeline_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    (out_dir / "full_pipeline_report.md").write_text(_format_markdown(report), encoding="utf-8")
    return report


def _main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="RAW 10-Scene Headless Full-Pipeline Visual & Debug Layer Auditor")
    ap.add_argument("--profile", choices=("quick", "full"), default="quick", help="quick=4 scenes, full=all 10 scenes")
    ap.add_argument("--game-dir", default=str(DEFAULT_GAME_DIR), help="Skyrim SE installation directory")
    ap.add_argument("--custom-ini", default=str(DEFAULT_CUSTOM_INI), help="Path to SkyrimCustom.ini")
    ap.add_argument("--out-dir", required=True, help="Output directory for captures & full_pipeline_report.{json,md}")
    ap.add_argument("--offline-dir", help="Audit an existing capture directory without launching SkyrimSE.exe")
    ap.add_argument("--engine-out-dir", help="Optional raw_engine_out path for offline telemetry audit")
    ap.add_argument("--steps", default=",".join(DEFAULT_AUDIT_STEPS), help="Comma-separated LightingSchedule step names")
    ap.add_argument("--hold", type=int, default=35, help="Frames to hold per debug step before first capture")
    ap.add_argument("--burst", type=int, default=6, help="Consecutive burst frames per step (1..16, default 6)")
    ap.add_argument("--stride", type=int, default=3, help="Frames between burst captures (1..30, default 3)")
    ap.add_argument("--timeout", type=float, default=120.0, help="Seconds timeout per scene capture")
    args = ap.parse_args(argv)

    steps = [s.strip() for s in args.steps.split(",") if s.strip()]
    out_dir = Path(args.out_dir)

    if args.offline_dir:
        eout = Path(args.engine_out_dir) if args.engine_out_dir else None
        rep = run_offline_audit(Path(args.offline_dir), out_dir, profile=args.profile, steps=steps, engine_out_dir=eout)
    else:
        rep = run_live_audit(
            Path(args.game_dir),
            out_dir,
            profile=args.profile,
            steps=steps,
            custom_ini_path=Path(args.custom_ini),
            hold_frames=args.hold,
            burst=args.burst,
            stride=args.stride,
            boot_timeout_sec=args.timeout,
        )

    print(json.dumps(rep["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
