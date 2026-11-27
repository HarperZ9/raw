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
import ctypes
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
DEFAULT_LIVE_CAM_SWEEP_YAW = 0.0022
DEFAULT_CAM_SWEEP_PITCH = 0.0012
DEFAULT_LIVE_CAM_SWEEP_PITCH = 0.0
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
    in_extended6: bool = False


CANONICAL_SCENES: tuple[AuditSceneSpec, ...] = (
    AuditSceneSpec("riverwood_day", "Riverwood", "fw 81a; set gamehour to 13", "Riverwood midday clear exterior (trees, pine, actors, river)", in_quick=True, is_interior=False, in_extended6=True),
    AuditSceneSpec("riverwood_sunset", "Riverwood", "fw 81a; set gamehour to 19.5", "Riverwood golden-hour twilight & shadows", is_interior=False),
    AuditSceneSpec("riverwood_rain", "Riverwood", "fw c8220; set gamehour to 14", "Riverwood heavy rain, wet puddles & overcast", is_interior=False),
    AuditSceneSpec("riverwood_night", "Riverwood", "fw 81a; set gamehour to 23.5", "Riverwood midnight aurora, torches & dark exposure", is_interior=False),
    AuditSceneSpec("riverwood_river", "Riverwood", "fw 81a; set gamehour to 11; tfc", "Riverwood shallow/deep river water & shoreline", is_interior=False),
    AuditSceneSpec("inn_interior", "RiverwoodSleepingGiantInn", "set gamehour to 20", "Sleeping Giant Inn interior hearth fire & candlelight", in_quick=True, is_interior=True, in_extended6=True),
    AuditSceneSpec("septimus_cave", "SeptimusSignusOutpost", "set gamehour to 12", "Septimus Signus glacial ice cave interior", in_quick=True, is_interior=True, in_extended6=True),
    AuditSceneSpec("windhelm_ext_snow", "WindhelmExterior01", "fw c8221; set gamehour to 14", "Windhelm snowy exterior, snow tracks & sky horizon", in_quick=True, is_interior=False, in_extended6=True),
    AuditSceneSpec("whiterun_ext", "WhiterunExterior01", "fw 81a; set gamehour to 13", "Whiterun exterior city walls, tundra plains, LOD & culling", is_interior=False, in_extended6=True),
    AuditSceneSpec("riften_canal", "RiftenCityNorthGateExterior", "fw 81a; set gamehour to 13", "Riften north gate & Lake Honrich exterior, autumn birch foliage & reflections", is_interior=False, in_extended6=True),
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

        # Wave 67: temporarily disable gamepad in SkyrimCustom.ini (restored bit-exact on exit) so
        # Skyrim SE accepts hardware keyboard/mouse dismissal of Creation Club Survival Mode modals.
        has_main_gamepad = any(ln.strip().lower().startswith("bgamepaddisable") or ln.strip().lower().startswith("bgamepadenable=") for ln in out_lines)
        if not has_main_gamepad:
            out_lines.extend(["[MAIN]", "bGamepadEnable=0"])

        self.ini_path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")

    def restore(self) -> None:
        if self._existed:
            self.ini_path.write_bytes(self._orig_bytes)
        else:
            self.ini_path.unlink(missing_ok=True)

    def __exit__(self, exc_type, exc, tb) -> None:
        self.restore()


class SkyrimPrefsGamepadGuard:
    """Temporarily sets `bGamepadEnable=0` in SkyrimPrefs.ini so hardware keyboard modal dismissal works,
    restoring the exact original file bytes in `__exit__`."""

    def __init__(self, prefs_path: Path):
        self.prefs_path = Path(prefs_path)
        self._existed = False
        self._orig_bytes: bytes = b""

    def __enter__(self) -> "SkyrimPrefsGamepadGuard":
        if self.prefs_path.exists():
            self._existed = True
            self._orig_bytes = self.prefs_path.read_bytes()
            text = self._orig_bytes.decode("utf-8", errors="replace")
            out_lines: list[str] = []
            replaced = False
            for ln in text.splitlines():
                if ln.strip().lower().startswith("bgamepadenable="):
                    out_lines.append("bGamepadEnable=0")
                    replaced = True
                else:
                    out_lines.append(ln)
            if replaced:
                self.prefs_path.write_text("\n".join(out_lines) + "\n", encoding="utf-8")
        return self

    def __exit__(self, exc_type, exc, tb) -> None:
        if self._existed:
            self.prefs_path.write_bytes(self._orig_bytes)


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


def parse_scene_gamehour(console_cmd: str) -> float | None:
    """Extracts `<H>` from `set gamehour to <H>` inside a scene console command string."""
    for raw_part in console_cmd.split(";"):
        toks = raw_part.strip().lower().split()
        if len(toks) >= 4 and toks[0] == "set" and toks[1] == "gamehour" and toks[2] == "to":
            try:
                return float(toks[3])
            except ValueError:
                return None
    return None


def emit_engine_commands(game_dir: Path, commands: Sequence[str]) -> Path:
    """Writes one or more newline-separated directives to `game_dir/raw_engine_go.txt`."""
    go_path = Path(game_dir) / "raw_engine_go.txt"
    clean = [str(c).strip() for c in commands if str(c).strip()]
    go_path.write_text("\n".join(clean) + "\n", encoding="utf-8")
    return go_path


def emit_set_time(game_dir: Path, hour: float) -> Path:
    """Writes `set_time <hour>` to `game_dir/raw_engine_go.txt` for `rt_control.cpp`."""
    clamped = max(0.0, min(23.99, float(hour)))
    return emit_engine_commands(game_dir, [f"set_time {clamped:g}"])


def emit_dismiss_modal(game_dir: Path) -> Path:
    """Writes `dismiss_modal` to `game_dir/raw_engine_go.txt` to close Creation Club modals cleanly."""
    return emit_engine_commands(game_dir, ["dismiss_modal"])


def _send_win32_scancode_key(scan_code: int, extended: bool = False, hold_sec: float = 0.07) -> bool:
    """Sends a real hardware scancode keydown+hold+keyup via Win32 SendInput (for DirectInput/Scaleform menus)."""
    if sys.platform != "win32":
        return False
    try:
        user32 = ctypes.windll.user32

        class _KeybdInput(ctypes.Structure):
            _fields_ = [
                ("wVk", ctypes.c_ushort),
                ("wScan", ctypes.c_ushort),
                ("dwFlags", ctypes.c_uint32),
                ("time", ctypes.c_uint32),
                ("dwExtraInfo", ctypes.c_size_t),
            ]

        class _InputUnion(ctypes.Union):
            _fields_ = [("ki", _KeybdInput), ("_pad", ctypes.c_ubyte * 32)]

        class _Input(ctypes.Structure):
            _fields_ = [("type", ctypes.c_uint32), ("u", _InputUnion)]

        flags_down = 0x0008 | (0x0001 if extended else 0)  # KEYEVENTF_SCANCODE | KEYEVENTF_EXTENDEDKEY
        flags_up = flags_down | 0x0002  # KEYEVENTF_KEYUP

        down = _Input()
        down.type = 1  # INPUT_KEYBOARD
        down.u.ki = _KeybdInput(0, scan_code, flags_down, 0, 0)
        up = _Input()
        up.type = 1
        up.u.ki = _KeybdInput(0, scan_code, flags_up, 0, 0)

        sent1 = user32.SendInput(1, ctypes.byref(down), ctypes.sizeof(_Input))
        time.sleep(max(0.02, float(hold_sec)))
        sent2 = user32.SendInput(1, ctypes.byref(up), ctypes.sizeof(_Input))
        return bool(sent1 == 1 and sent2 == 1)
    except OSError:
        return False


def _focus_skyrim_window(pid: int | None = None) -> bool:
    """Brings the live SkyrimSE.exe window to foreground so DirectInput receives modal dismissal keys."""
    if sys.platform != "win32":
        return False
    try:
        user32 = ctypes.windll.user32
        found_hwnd = ctypes.c_void_p(0)

        @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        def _enum_cb(hwnd: int, _lp: int) -> bool:
            if not user32.IsWindowVisible(hwnd):
                return True
            if pid is not None:
                wpid = ctypes.c_uint32(0)
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
                if int(wpid.value) != int(pid):
                    return True
            buf = ctypes.create_unicode_buffer(128)
            user32.GetWindowTextW(hwnd, buf, 128)
            if "skyrim" in buf.value.lower() or pid is not None:
                found_hwnd.value = hwnd
                return False
            return True

        user32.EnumWindows(_enum_cb, 0)
        if found_hwnd.value:
            user32.ShowWindow(found_hwnd.value, 9)  # SW_RESTORE
            user32.SetForegroundWindow(found_hwnd.value)
            return True
    except OSError:
        return False
    return False


def _click_skyrim_client_frac(pid: int | None, fx: float = 0.531, fy: float = 0.761, hold_sec: float = 0.06) -> bool:
    """Moves cursor to normalized `(fx, fy)` inside SkyrimSE's client area (default: Survival Mode 'No' button)
    and sends a real hardware left mouse click via Win32 SendInput."""
    if sys.platform != "win32":
        return False
    try:
        user32 = ctypes.windll.user32
        found_hwnd = ctypes.c_void_p(0)

        @ctypes.WINFUNCTYPE(ctypes.c_bool, ctypes.c_void_p, ctypes.c_void_p)
        def _enum_cb(hwnd: int, _lp: int) -> bool:
            if not user32.IsWindowVisible(hwnd):
                return True
            if pid is not None:
                wpid = ctypes.c_uint32(0)
                user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
                if int(wpid.value) != int(pid):
                    return True
            buf = ctypes.create_unicode_buffer(128)
            user32.GetWindowTextW(hwnd, buf, 128)
            if "skyrim" in buf.value.lower() or pid is not None:
                found_hwnd.value = hwnd
                return False
            return True

        user32.EnumWindows(_enum_cb, 0)
        hwnd = found_hwnd.value
        if not hwnd:
            return False

        class _Rect(ctypes.Structure):
            _fields_ = [("left", ctypes.c_long), ("top", ctypes.c_long), ("right", ctypes.c_long), ("bottom", ctypes.c_long)]

        class _Point(ctypes.Structure):
            _fields_ = [("x", ctypes.c_long), ("y", ctypes.c_long)]

        class _MouseInput(ctypes.Structure):
            _fields_ = [
                ("dx", ctypes.c_long),
                ("dy", ctypes.c_long),
                ("mouseData", ctypes.c_uint32),
                ("dwFlags", ctypes.c_uint32),
                ("time", ctypes.c_uint32),
                ("dwExtraInfo", ctypes.c_size_t),
            ]

        class _InputUnion(ctypes.Union):
            _fields_ = [("mi", _MouseInput), ("_pad", ctypes.c_ubyte * 32)]

        class _Input(ctypes.Structure):
            _fields_ = [("type", ctypes.c_uint32), ("u", _InputUnion)]

        rc = _Rect()
        if not user32.GetClientRect(hwnd, ctypes.byref(rc)):
            return False
        cw = max(1, int(rc.right - rc.left))
        ch = max(1, int(rc.bottom - rc.top))
        pt = _Point(int(cw * fx), int(ch * fy))
        user32.ClientToScreen(hwnd, ctypes.byref(pt))

        user32.ShowWindow(hwnd, 9)
        # Attach to current foreground thread so Windows grants SetForegroundWindow cleanly
        fg_hwnd = user32.GetForegroundWindow()
        cur_tid = ctypes.windll.kernel32.GetCurrentThreadId()
        fg_tid = user32.GetWindowThreadProcessId(fg_hwnd, None) if fg_hwnd else 0
        tgt_tid = user32.GetWindowThreadProcessId(hwnd, None)
        if fg_tid and fg_tid != cur_tid:
            user32.AttachThreadInput(cur_tid, fg_tid, True)
        if tgt_tid and tgt_tid != cur_tid:
            user32.AttachThreadInput(cur_tid, tgt_tid, True)
        user32.BringWindowToTop(hwnd)
        user32.SetForegroundWindow(hwnd)
        user32.SetFocus(hwnd)
        if tgt_tid and tgt_tid != cur_tid:
            user32.AttachThreadInput(cur_tid, tgt_tid, False)
        if fg_tid and fg_tid != cur_tid:
            user32.AttachThreadInput(cur_tid, fg_tid, False)
        time.sleep(0.08)

        # Skyrim SE Scaleform reads relative DirectInput mouse deltas from screen center (0.5, 0.5).
        # Step relative mouse movement smoothly from center -> (fx, fy) so Scaleform highlights "No", then click!
        rel_dx = int(cw * (fx - 0.5))
        rel_dy = int(ch * (fy - 0.5))
        steps = 8
        for step_i in range(steps):
            sdx = rel_dx // steps + (1 if step_i < abs(rel_dx) % steps and rel_dx > 0 else (-1 if step_i < abs(rel_dx) % steps and rel_dx < 0 else 0))
            sdy = rel_dy // steps + (1 if step_i < abs(rel_dy) % steps and rel_dy > 0 else (-1 if step_i < abs(rel_dy) % steps and rel_dy < 0 else 0))
            mv = _Input()
            mv.type = 0  # INPUT_MOUSE
            mv.u.mi = _MouseInput(sdx, sdy, 0, 0x0001, 0, 0)  # relative MOUSEEVENTF_MOVE
            user32.SendInput(1, ctypes.byref(mv), ctypes.sizeof(_Input))
            time.sleep(0.015)

        user32.SetCursorPos(pt.x, pt.y)
        lp = (int(ch * fy) << 16) | (int(cw * fx) & 0xFFFF)
        user32.PostMessageW(hwnd, 0x0200, 0, lp)
        time.sleep(0.06)

        dn = _Input()
        dn.type = 0
        dn.u.mi = _MouseInput(0, 0, 0, 0x0002, 0, 0)  # MOUSEEVENTF_LEFTDOWN
        up = _Input()
        up.type = 0
        up.u.mi = _MouseInput(0, 0, 0, 0x0004, 0, 0)  # MOUSEEVENTF_LEFTUP
        user32.SendInput(1, ctypes.byref(dn), ctypes.sizeof(_Input))
        user32.PostMessageW(hwnd, 0x0201, 1, lp)
        time.sleep(max(0.04, float(hold_sec)))
        user32.SendInput(1, ctypes.byref(up), ctypes.sizeof(_Input))
        user32.PostMessageW(hwnd, 0x0202, 0, lp)
        return True
    except OSError:
        return False


def dismiss_survival_modal_if_paused(
    game_dir: Path,
    engine_out: Path,
    proc: subprocess.Popen[bytes] | None = None,
    max_attempts: int = 3,
    force_check_first: bool = False,
) -> bool:
    """Checks `view` for Creation Club Survival Mode modal pause and dismisses it cleanly via mouse click on 'No' + Right/Enter."""
    dismissed_any = False
    for attempt in range(max(1, int(max_attempts))):
        v = query_engine_view_state(game_dir, engine_out, timeout_sec=1.5)
        paused = (v is not None) and (int(v.get("menu_paused", 0)) != 0 or int(v.get("menu_stack_depth", 1)) > 1)
        if not paused and not (force_check_first and attempt == 0 and v is None):
            break
        emit_dismiss_modal(game_dir)
        dismissed_any = True
        time.sleep(0.25)
        v_after = query_engine_view_state(game_dir, engine_out, timeout_sec=1.5)
        still_paused = (v_after is not None) and (
            int(v_after.get("menu_paused", 0)) != 0 or int(v_after.get("menu_stack_depth", 1)) > 1
        )
        if v_after is not None and not still_paused:
            break
        # Fallback if in-process UIMessageQueue kHide did not close a non-MessageBox modal
        pid = proc.pid if (proc is not None and proc.poll() is None) else None
        _click_skyrim_client_frac(pid, fx=0.531, fy=0.761, hold_sec=0.06)
        time.sleep(0.15)
        if _focus_skyrim_window(pid):
            _send_win32_scancode_key(0x4D, extended=True, hold_sec=0.07)   # Right Arrow
            time.sleep(0.12)
            _send_win32_scancode_key(0x1C, extended=False, hold_sec=0.07)  # Enter
        time.sleep(0.25)
    return dismissed_any


def emit_audit_flywheel_receipt(
    out_dir: Path,
    report_json: Path,
    report_md: Path,
    repo_root: Path | None = None,
    catalogue: bool = False,
    run_oracle: bool = True,
) -> dict[str, Any] | None:
    """Emits a canonical `engine-receipt/1` JSON envelope via `engine_to_flywheel_receipt.py`."""
    bridge = Path(r"C:\dev\tools\interop\engine_to_flywheel_receipt.py")
    root = repo_root or Path(__file__).resolve().parents[2]
    receipt_out = Path(out_dir) / "engine_receipt.json"
    if not bridge.is_file():
        return None
    cmd = [
        sys.executable,
        str(bridge),
        "emit",
        "--engine", "raw",
        "--flagship", "superframe",
        "--task-id", f"raw-live-audit-{Path(out_dir).name}",
        "--root", str(root),
        "--artifact", str(report_json),
        "--artifact", str(report_md),
        "--out", str(receipt_out),
    ]
    if catalogue:
        cmd.append("--catalogue")
    test_script = root / "tests" / "test_visual_heuristics.py"
    if run_oracle and test_script.is_file():
        cmd.extend(["--oracle-cmd", "--", sys.executable, str(test_script)])
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=180)
        if proc.returncode == 0 and receipt_out.is_file():
            return json.loads(proc.stdout.strip() or "{}")
    except (OSError, subprocess.SubprocessError, json.JSONDecodeError):
        return None
    return None


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
    return emit_engine_commands(game_dir, [cmd])


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
    burst_stride: int = 3,
) -> dict[str, Any]:
    """Audits an existing multi-scene capture directory without launching Skyrim SE."""
    out_dir.mkdir(parents=True, exist_ok=True)
    selected = [
        s for s in CANONICAL_SCENES
        if (profile == "full" or (profile == "extended6" and s.in_extended6) or (profile == "quick" and s.in_quick))
    ]
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
            burst_stride=max(1, int(burst_stride)),
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
    report_json = out_dir / "full_pipeline_report.json"
    report_md = out_dir / "full_pipeline_report.md"
    report_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    report_md.write_text(_format_markdown(report), encoding="utf-8")
    receipt_meta = emit_audit_flywheel_receipt(out_dir, report_json, report_md, run_oracle=False)
    if receipt_meta is not None:
        report["engine_receipt"] = receipt_meta
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
    cam_sweep_yaw: float = DEFAULT_LIVE_CAM_SWEEP_YAW,
    cam_sweep_pitch: float = DEFAULT_LIVE_CAM_SWEEP_PITCH,
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
    selected = [
        s for s in CANONICAL_SCENES
        if (profile == "full" or (profile == "extended6" and s.in_extended6) or (profile == "quick" and s.in_quick))
    ]
    engine_out = game_dir / "raw_engine_out"
    env = dict(os.environ, SteamAppId="489830", SteamGameId="489830")
    scene_reports: list[dict[str, Any]] = []
    sentinel_cleaned = False
    scene_init_path = game_dir / SCENE_INIT_FILENAME

    prefs_ini_path = Path(custom_ini_path).with_name("SkyrimPrefs.ini")
    with SkyrimCustomIniGuard(custom_ini_path) as ini_guard, SkyrimPrefsGamepadGuard(prefs_ini_path):
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
                        # Wave 67: apply exact gamehour (set_time), wait briefly on exteriors for CC Survival Mode
                        # modal to settle, and reliably dismiss any MessageBoxMenu BEFORE starting bursts!
                        post_ready_cmds: list[str] = []
                        scene_hour = parse_scene_gamehour(spec.console_cmd)
                        if scene_hour is not None:
                            post_ready_cmds.append(f"set_time {max(0.0, min(23.99, scene_hour)):g}")
                        post_ready_cmds.append("dismiss_modal")
                        emit_engine_commands(game_dir, post_ready_cmds)
                        time.sleep(1.5 if not spec.is_interior else 0.45)
                        dismiss_survival_modal_if_paused(game_dir, engine_out, proc=proc, max_attempts=4)

                        # Wave 67: lock sweep_sign = +1.0 (unidirectional) across improved, view_motion_outline,
                        # and view_sun_shadow so MV reprojection compares identical camera pan direction (+mv)!
                        sweep_sign = 1.0
                        sweep_pulse_frames = max(480, int(cam_sweep_frames))
                        last_sweep_emit = 0.0
                        last_modal_poll = time.time()
                        pre_cap_cmds: list[str] = ["dismiss_modal"]
                        if cam_sweep:
                            pre_cap_cmds.append(
                                build_cam_sweep_command(
                                    cam_sweep_yaw * sweep_sign,
                                    cam_sweep_pitch * sweep_sign,
                                    cam_sweep_fwd,
                                    sweep_pulse_frames,
                                )
                            )
                            last_sweep_emit = time.time()
                        emit_engine_commands(game_dir, pre_cap_cmds)
                        time.sleep(0.20)

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
                            now_t = time.time()
                            # Poll every 1.5s for delayed Survival Mode modal pauses and dismiss immediately
                            if (now_t - last_modal_poll) >= 1.5 and not engine_go.exists():
                                last_modal_poll = now_t
                                dismiss_survival_modal_if_paused(game_dir, engine_out, proc=proc, max_attempts=2)
                            # Keep unidirectional gentle camera sweep alive (never flip sweep_sign mid-burst!)
                            if cam_sweep and (now_t - last_sweep_emit) >= 4.5 and not engine_go.exists():
                                emit_cam_sweep(
                                    game_dir,
                                    cam_sweep_yaw * sweep_sign,
                                    cam_sweep_pitch * sweep_sign,
                                    cam_sweep_fwd,
                                    sweep_pulse_frames,
                                )
                                last_sweep_emit = now_t
                            time.sleep(0.35)
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
                    burst_stride=stride_n,
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
    report_json = out_dir / "full_pipeline_report.json"
    report_md = out_dir / "full_pipeline_report.md"
    report_json.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    report_md.write_text(_format_markdown(report), encoding="utf-8")
    receipt_meta = emit_audit_flywheel_receipt(out_dir, report_json, report_md, run_oracle=True)
    if receipt_meta is not None:
        report["engine_receipt"] = receipt_meta
    return report


def _main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="RAW 10-Scene Headless Full-Pipeline Visual, Debug & Motion Auditor (Wave 65)")
    ap.add_argument("--profile", choices=("quick", "extended6", "full"), default="quick", help="quick=4 scenes, extended6=6 scenes (+whiterun_ext, riften_canal), full=all 11 scenes")
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
    ap.add_argument("--cam-sweep-yaw", type=float, default=DEFAULT_LIVE_CAM_SWEEP_YAW, help="Yaw rad/frame for cam_sweep (default 0.0022 gentle horizontal pan)")
    ap.add_argument("--cam-sweep-pitch", type=float, default=DEFAULT_LIVE_CAM_SWEEP_PITCH, help="Pitch rad/frame for cam_sweep (default 0.0 horizontal pan)")
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
        rep = run_offline_audit(
            Path(args.offline_dir),
            out_dir,
            profile=args.profile,
            steps=steps,
            engine_out_dir=eout,
            burst_stride=args.stride,
        )
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
