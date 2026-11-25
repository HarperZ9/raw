"""100% Headless Multi-Scene, Multi-Weather, Multi-TOD, Multi-Debug-View Live Skyrim SE Auditor (Wave 65).

Uses the proven `SkyrimCustom.ini` `[General] sStartingCell=` + `sStartingConsoleCommand=bat raw_scene_init`
+ in-world readiness polling (`raw_harness_status.txt` + `raw_engine_go.txt` `view` + 4.0s fade settle)
+ headless in-process camera motion (`cam_sweep`) + `raw_harness_go.txt` (`burst=6, stride=3`) GPU backbuffer
capture protocol (zero Main-Menu focus-lock hang, zero Survival Mode popup, zero loading-screen race!).
Always restores `SkyrimCustom.ini` bit-exact and removes `raw_scene_init.txt` +
`raw_engine_out/boot_sentinel.state` in `finally:`.

Supports:
  - `--profile quick` (4 canonical scenes: riverwood_day, inn_interior, septimus_cave, windhelm_ext_snow)
  - `--profile full`  (all 10 canonical scenes across biomes, weather, time-of-day & interiors)
  - `--cam-sweep` / `--no-cam-sweep` (default ON in live mode: emits `cam_sweep 0.008 0.0012 0.0 240`)
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
SCENE_INIT_FILENAME = "raw_scene_init.txt"
SCENE_INIT_BAT_CMD = "bat raw_scene_init"

DEFAULT_CAM_SWEEP_YAW = 0.008
DEFAULT_CAM_SWEEP_PITCH = 0.0012
DEFAULT_CAM_SWEEP_FWD = 0.0
DEFAULT_CAM_SWEEP_FRAMES = 240

DEFAULT_AUDIT_STEPS: tuple[str, ...] = (
    "improved",
    "view_motion_outline",
    "view_sun_shadow",
    "game",
    "view_normals",
    "view_albedo",
    "view_full_classes",
    "view_depth_edge",
    "view_shadow_stab",
    "view_moss_mask",
    "view_skin_sss",
    "view_nan_hdr",
    "view_flora_trees",
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
    is_interior: bool = False


CANONICAL_SCENES: tuple[AuditSceneSpec, ...] = (
    AuditSceneSpec("riverwood_day", "Riverwood", "fw 81a; set gamehour to 13", "Riverwood midday clear exterior (trees, pine, actors, river)", in_quick=True, is_interior=False),
    AuditSceneSpec("riverwood_sunset", "Riverwood", "fw 81a; set gamehour to 19.5", "Riverwood golden-hour twilight & shadows", is_interior=False),
    AuditSceneSpec("riverwood_rain", "Riverwood", "fw c8220; set gamehour to 14", "Riverwood heavy rain, wet puddles & overcast", is_interior=False),
    AuditSceneSpec("riverwood_night", "Riverwood", "fw 81a; set gamehour to 23.5", "Riverwood midnight aurora, torches & dark exposure", is_interior=False),
    AuditSceneSpec("riverwood_river", "Riverwood", "fw 81a; set gamehour to 11; tfc", "Riverwood shallow/deep river water & shoreline", is_interior=False),
    AuditSceneSpec("inn_interior", "RiverwoodSleepingGiantInn", "set gamehour to 20", "Sleeping Giant Inn interior hearth fire & candlelight", in_quick=True, is_interior=True),
    AuditSceneSpec("septimus_cave", "SeptimusSignusOutpost", "set gamehour to 12", "Septimus Signus glacial ice cave interior", in_quick=True, is_interior=True),
    AuditSceneSpec("windhelm_ext_snow", "WindhelmExterior01", "fw c8221; set gamehour to 14", "Windhelm snowy exterior, snow tracks & sky horizon", in_quick=True, is_interior=False),
    AuditSceneSpec("whiterun_ext", "WhiterunExterior01", "fw 81a; set gamehour to 13", "Whiterun tundra plains, 3D grass & distant mountain LOD", is_interior=False),
    AuditSceneSpec("high_hrothgar", "HighHrothgarExterior01", "fw c8221; set gamehour to 15", "High Hrothgar alpine snowstorm, clouds & froxels", is_interior=False),
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


def write_scene_init_bat(game_dir: Path, console_cmd: str) -> Path:
    """Writes multi-line `raw_scene_init.txt` in `game_dir` for `sStartingConsoleCommand=bat raw_scene_init`.

    Always suppresses Creation Club Survival Mode popups before applying scene weather and time-of-day
    commands so every capture starts cleanly in-world with exact requested weather and hour.
    """
    lines: list[str] = [
        "set Survival_ModeEnabled to 0",
        "stopquest ccQDRSSE001_SurvivalModeQuest",
    ]
    for raw_part in console_cmd.split(";"):
        part = raw_part.strip()
        if part:
            lines.append(part)
    bat_path = Path(game_dir) / SCENE_INIT_FILENAME
    bat_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return bat_path


def build_cam_sweep_command(
    yaw_rad_per_frame: float = DEFAULT_CAM_SWEEP_YAW,
    pitch_rad_per_frame: float = DEFAULT_CAM_SWEEP_PITCH,
    fwd_units_per_frame: float = DEFAULT_CAM_SWEEP_FWD,
    frames: int = DEFAULT_CAM_SWEEP_FRAMES,
) -> str:
    """Formats the `cam_sweep` directive for `raw_engine_go.txt`."""
    return f"cam_sweep {float(yaw_rad_per_frame)} {float(pitch_rad_per_frame)} {float(fwd_units_per_frame)} {max(1, int(frames))}"


def emit_cam_sweep(
    game_dir: Path,
    yaw_rad_per_frame: float = DEFAULT_CAM_SWEEP_YAW,
    pitch_rad_per_frame: float = DEFAULT_CAM_SWEEP_PITCH,
    fwd_units_per_frame: float = DEFAULT_CAM_SWEEP_FWD,
    frames: int = DEFAULT_CAM_SWEEP_FRAMES,
) -> Path:
    """Writes a `cam_sweep` command into `game_dir/raw_engine_go.txt` for `rt_control.cpp`."""
    cmd = build_cam_sweep_command(yaw_rad_per_frame, pitch_rad_per_frame, fwd_units_per_frame, frames)
    go_path = Path(game_dir) / "raw_engine_go.txt"
    go_path.write_text(cmd + "\n", encoding="utf-8")
    return go_path


def parse_harness_status(text: str) -> dict[str, int]:
    """Parses `presents=N world=N loads=N cell=N active=N` from `raw_harness_status.txt`."""
    out: dict[str, int] = {}
    for tok in text.replace("\r", " ").replace("\n", " ").split():
        if "=" not in tok:
            continue
        k, v = tok.split("=", 1)
        try:
            out[k.strip()] = int(v.strip())
        except ValueError:
            continue
    return out


def query_engine_view_state(
    game_dir: Path,
    engine_out: Path,
    timeout_sec: float = 3.0,
) -> dict[str, Any] | None:
    """Sends `view` to `raw_engine_go.txt` and awaits the matching JSON reply in `control_results.jsonl`."""
    results_path = Path(engine_out) / "control_results.jsonl"
    prior_lines = 0
    if results_path.exists():
        prior_lines = len(results_path.read_text(encoding="utf-8", errors="replace").splitlines())

    go_path = Path(game_dir) / "raw_engine_go.txt"
    go_path.write_text("view\n", encoding="utf-8")

    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        if results_path.exists():
            lines = results_path.read_text(encoding="utf-8", errors="replace").splitlines()
            for raw_ln in reversed(lines[prior_lines:]):
                ln = raw_ln.strip()
                if not ln.startswith("{"):
                    continue
                try:
                    obj = json.loads(ln)
                except json.JSONDecodeError:
                    continue
                if obj.get("cmd") == "view":
                    return obj
        time.sleep(0.15)
    return None


def wait_for_world_ready(
    game_dir: Path,
    engine_out: Path,
    proc: subprocess.Popen[bytes] | None = None,
    min_world_frames: int = 220,
    fade_settle_sec: float = 4.0,
    timeout_sec: float = 90.0,
) -> dict[str, Any]:
    """Waits until Skyrim SE finishes loading into the target cell, is unpaused, and finishes fade-in.

    1. Polls `raw_harness_status.txt` for `cell != 1 and world >= min_world_frames`.
    2. Queries `raw_engine_go.txt` `view` for `cell_id != 0 and menu_paused == 0` (when `raw_engine.dll` responds).
    3. Waits `fade_settle_sec` (default 4.0s) so black load fade-ins are completely gone before capture.
    """
    status_path = Path(game_dir) / "raw_harness_status.txt"
    deadline = time.time() + timeout_sec
    last_status: dict[str, int] = {}
    last_view: dict[str, Any] | None = None
    harness_ready = False
    view_ready = False

    while time.time() < deadline:
        if proc is not None and proc.poll() is not None:
            break
        if status_path.exists():
            last_status = parse_harness_status(status_path.read_text(encoding="utf-8", errors="replace"))
            if last_status.get("world", 0) >= min_world_frames and last_status.get("cell", 1) != 1:
                harness_ready = True
                break
        time.sleep(0.35)

    if harness_ready:
        view_deadline = min(deadline, time.time() + 18.0)
        while time.time() < view_deadline:
            if proc is not None and proc.poll() is not None:
                break
            v = query_engine_view_state(game_dir, engine_out, timeout_sec=2.5)
            if v is None:
                # If raw_engine.dll control_results.jsonl is absent on an older build, proceed after harness_ready.
                if not (Path(engine_out) / "control_results.jsonl").exists():
                    break
                time.sleep(0.3)
                continue
            last_view = v
            cell_id = v.get("cell_id", 0)
            menu_paused = v.get("menu_paused", 0)
            if cell_id not in (0, None, "0", "0x0") and int(menu_paused) == 0:
                view_ready = True
                break
            time.sleep(0.5)

        if fade_settle_sec > 0.0 and (proc is None or proc.poll() is None):
            time.sleep(fade_settle_sec)

    return {
        "harness_ready": harness_ready,
        "view_ready": view_ready,
        "last_status": last_status,
        "last_view": last_view,
    }


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
        f"- Camera Sweep: `{report.get('cam_sweep', False)}`",
        f"- Timestamp: `{report['timestamp']}`",
        f"- Scenes Audited: `{report['summary']['scenes_audited']}`",
        f"- Clean Scenes: `{report['summary']['clean_scenes']}`",
        f"- Flagged Scenes: `{report['summary']['flagged_scenes']}`",
        f"- Sentinel Cleaned: `{report['sentinel_cleaned']}`",
        "",
        "## Per-Scene Heuristic Results",
        "",
        "| Scene | Interior | Steps | Heuristics Run | Verdict | Defects |",
        "| :--- | :--- | :--- | :--- | :--- | :--- |",
    ]
    for sc in report.get("scenes", []):
        defs = ", ".join(sc.get("defects", [])) or "none"
        lines.append(
            f"| `{sc['scene']}` | `{sc.get('is_interior', False)}` | {len(sc.get('steps_present', []))} | "
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
        rep = vh.audit_scene_bundle(
            step_map,
            scene_name=spec.name,
            telemetry=telemetry,
            burst_sequences=burst_map,
            is_interior=spec.is_interior,
        )
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
        "cam_sweep": False,
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
    cam_sweep: bool = True,
    cam_sweep_yaw: float = DEFAULT_CAM_SWEEP_YAW,
    cam_sweep_pitch: float = DEFAULT_CAM_SWEEP_PITCH,
    cam_sweep_fwd: float = DEFAULT_CAM_SWEEP_FWD,
    cam_sweep_frames: int = DEFAULT_CAM_SWEEP_FRAMES,
    min_world_frames: int = 220,
    fade_settle_sec: float = 4.0,
    boot_timeout_sec: float = 180.0,
) -> dict[str, Any]:
    """Runs the 100% headless multi-scene live SkyrimSE.exe audit with `bat raw_scene_init`,
    in-world readiness polling, headless `cam_sweep` camera motion, and multi-frame bursts."""
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
    scene_init_path = game_dir / SCENE_INIT_FILENAME

    with SkyrimCustomIniGuard(custom_ini_path) as ini_guard:
        try:
            for spec in selected:
                scene_out = out_dir / spec.name
                scene_out.mkdir(parents=True, exist_ok=True)
                write_scene_init_bat(game_dir, spec.console_cmd)
                ini_guard.write_scene_boot(spec.starting_cell, SCENE_INIT_BAT_CMD)

                go_file = game_dir / "raw_harness_go.txt"
                done_file = game_dir / "raw_harness_done.txt"
                status_file = game_dir / "raw_harness_status.txt"
                engine_go = game_dir / "raw_engine_go.txt"
                for stale in (go_file, done_file, status_file, engine_go):
                    stale.unlink(missing_ok=True)

                proc: subprocess.Popen[bytes] | None = None
                readiness: dict[str, Any] = {}
                try:
                    proc = subprocess.Popen([str(exe)], cwd=str(game_dir), env=env)
                    readiness = wait_for_world_ready(
                        game_dir,
                        engine_out,
                        proc=proc,
                        min_world_frames=min_world_frames,
                        fade_settle_sec=fade_settle_sec,
                        timeout_sec=min(90.0, boot_timeout_sec * 0.55),
                    )

                    if proc.poll() is None:
                        sweep_sign = 1.0
                        last_sweep_emit = 0.0
                        if cam_sweep:
                            emit_cam_sweep(game_dir, cam_sweep_yaw, cam_sweep_pitch, cam_sweep_fwd, cam_sweep_frames)
                            last_sweep_emit = time.time()
                            time.sleep(0.15)

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

                        deadline = time.time() + boot_timeout_sec
                        while time.time() < deadline:
                            if done_file.exists():
                                break
                            if proc.poll() is not None:
                                break
                            # Keep gentle camera sweep alive across long multi-step captures by alternating yaw/pitch
                            if cam_sweep and (time.time() - last_sweep_emit) >= 3.5 and not engine_go.exists():
                                sweep_sign = -sweep_sign
                                emit_cam_sweep(
                                    game_dir,
                                    cam_sweep_yaw * sweep_sign,
                                    cam_sweep_pitch * sweep_sign,
                                    cam_sweep_fwd,
                                    cam_sweep_frames,
                                )
                                last_sweep_emit = time.time()
                            time.sleep(0.4)
                finally:
                    _stop_skyrim(proc)
                    for tmpf in (go_file, done_file, engine_go):
                        tmpf.unlink(missing_ok=True)

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
                rep = vh.audit_scene_bundle(
                    step_map,
                    scene_name=spec.name,
                    telemetry=telemetry,
                    burst_sequences=burst_map,
                    is_interior=spec.is_interior,
                )
                rep["readiness"] = readiness
                rep["cam_sweep_enabled"] = bool(cam_sweep)
                scene_reports.append(rep)
        finally:
            _stop_skyrim(None)
            scene_init_path.unlink(missing_ok=True)
            sent = engine_out / "boot_sentinel.state"
            if sent.exists():
                sent.unlink(missing_ok=True)
                sentinel_cleaned = True

    report: dict[str, Any] = {
        "mode": "live",
        "profile": profile,
        "cam_sweep": bool(cam_sweep),
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
    ap = argparse.ArgumentParser(description="RAW 10-Scene Headless Full-Pipeline Visual, Debug & Motion Auditor (Wave 65)")
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
    ap.add_argument("--cam-sweep", dest="cam_sweep", action="store_true", default=True, help="Drive headless cam_sweep camera motion during bursts (default: ON)")
    ap.add_argument("--no-cam-sweep", dest="cam_sweep", action="store_false", help="Disable headless cam_sweep camera motion")
    ap.add_argument("--cam-sweep-yaw", type=float, default=DEFAULT_CAM_SWEEP_YAW, help="Yaw rad/frame for cam_sweep (default 0.008)")
    ap.add_argument("--cam-sweep-pitch", type=float, default=DEFAULT_CAM_SWEEP_PITCH, help="Pitch rad/frame for cam_sweep (default 0.0012)")
    ap.add_argument("--cam-sweep-fwd", type=float, default=DEFAULT_CAM_SWEEP_FWD, help="Forward units/frame for cam_sweep (default 0.0)")
    ap.add_argument("--cam-sweep-frames", type=int, default=DEFAULT_CAM_SWEEP_FRAMES, help="Frames per cam_sweep pulse (default 240)")
    ap.add_argument("--min-world-frames", type=int, default=220, help="Minimum world frames in raw_harness_status.txt before capture (default 220)")
    ap.add_argument("--fade-settle", type=float, default=4.0, help="Seconds to wait after world readiness for load fade-in (default 4.0)")
    ap.add_argument("--timeout", type=float, default=180.0, help="Seconds timeout per scene capture")
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
            cam_sweep=args.cam_sweep,
            cam_sweep_yaw=args.cam_sweep_yaw,
            cam_sweep_pitch=args.cam_sweep_pitch,
            cam_sweep_fwd=args.cam_sweep_fwd,
            cam_sweep_frames=args.cam_sweep_frames,
            min_world_frames=args.min_world_frames,
            fade_settle_sec=args.fade_settle,
            boot_timeout_sec=args.timeout,
        )

    print(json.dumps(rep["summary"], indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
