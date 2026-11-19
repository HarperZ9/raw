"""The game side of the RAW test harness: the install's safety (ENB backup, settings files),
deploying the proxy, launching through SKSE, input to the game window, and reading the proxy's
heartbeat. run.py drives a test script with it.

Every path comes from the command line or the environment (RAW_GAME_DIR, RAW_ENB_BACKUP,
RAW_SETTINGS_BACKUP); nothing here names a machine.

Original work, MIT.
"""
from __future__ import annotations

import ctypes
import hashlib
import os
import re
import shutil
import subprocess
import time
from ctypes import wintypes
from pathlib import Path

DOCS = Path(os.path.expanduser("~")) / "Documents" / "My Games" / "Skyrim Special Edition"
SETTINGS = ("SkyrimCustom.ini", "SkyrimPrefs.ini")
PROXY_FILES = ("d3d11_proxy.ini", "d3d11_proxy.log", "raw_harness_go.txt", "raw_harness_done.txt", "raw_harness_status.txt",
               "rawbat.txt", "raw_hot_reload.txt", "raw_engine_go.txt", "raw_engine_done.txt",
               "raw_module_go.txt", "raw_module_done.txt", "raw_ledger_go.txt", "raw_ledger_done.txt")   # the live session's
SHADER_COPY = "raw_shaders"   # hot-reload shader folder beside the game, made by a run and removed after it


def sha(p: Path) -> str:
    h = hashlib.sha256()
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


class Install:
    """The game folder, its ENB backup with SHA256SUMS, and the settings files, kept as found."""

    def __init__(self, game: Path, enb_backup: Path, settings_backup: Path):
        self.game, self.enb, self.settings = game, enb_backup, settings_backup

    def sums(self) -> list[tuple[str, str]]:
        out = []
        for line in (self.enb / "SHA256SUMS").read_text(encoding="utf-8").splitlines():
            m = re.match(r"^([0-9a-f]{64}) [ *](.+)$", line.strip())
            if m:
                out.append((m.group(1), m.group(2)))
        return out

    def check(self, root: Path) -> tuple[int, list[str]]:
        s = self.sums()
        bad = [rel for h, rel in s if not (root / rel).exists() or sha(root / rel) != h]
        return len(s), bad

    def before(self) -> dict:
        """Refuses to start unless the backup and the game folder both match the sums and the
        settings files match their backups (made on first use)."""
        n, bad = self.check(self.enb)
        if bad:
            raise SystemExit(f"ENB backup does not match its SHA256SUMS: {bad[:5]}")
        n2, bad2 = self.check(self.game)
        if bad2:
            raise SystemExit(f"game folder is not in its backed-up ENB state: {bad2[:5]}")
        self.settings.mkdir(parents=True, exist_ok=True)
        for name in SETTINGS:
            src, bk = DOCS / name, self.settings / name
            if not bk.exists():
                shutil.copy2(src, bk)
            if sha(src) != sha(bk):
                raise SystemExit(f"{name} differs from its backup before the run")
        return {"enb_backup": f"{n - len(bad)} of {n}", "game_before": f"{n2 - len(bad2)} of {n2}"}

    def set_option(self, name: str, section: str, key: str, value: str) -> None:
        """One key in a settings file, added to its section when missing."""
        p = DOCS / name
        lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
        out, inside, done = [], False, False
        for line in lines:
            if line.strip().startswith("["):
                if inside and not done:
                    out.append(f"{key}={value}")
                    done = True
                inside = line.strip().lower() == f"[{section.lower()}]"
            elif inside and line.split("=")[0].strip().lower() == key.lower():
                line, done = f"{key}={value}", True
            out.append(line)
        if not done:
            if not inside:
                out.append(f"[{section}]")
            out.append(f"{key}={value}")
        p.write_text("\n".join(out) + "\n", encoding="utf-8")

    def deploy(self, dll: Path, ini_text: str) -> None:
        shutil.copy2(dll, self.game / "d3d11.dll")
        (self.game / "d3d11_proxy.ini").write_text(ini_text, encoding="utf-8")
        for name in PROXY_FILES[1:]:
            (self.game / name).unlink(missing_ok=True)

    def after(self, out: Path) -> dict:
        """Collects the proxy log, restores ENB and the settings files, verifies every hash."""
        for name in ("d3d11_proxy.log",):
            if (self.game / name).exists():
                shutil.copy2(self.game / name, out / name)
        for name in PROXY_FILES:
            (self.game / name).unlink(missing_ok=True)
        shutil.rmtree(self.game / SHADER_COPY, ignore_errors=True)
        for name in SETTINGS:
            shutil.copy2(self.settings / name, DOCS / name)
        r = subprocess.run(["cmd", "/c", str(self.enb / "restore-enb.cmd")], capture_output=True, text=True)
        n, bad = self.check(self.game)
        restored = all(sha(DOCS / name) == sha(self.settings / name) for name in SETTINGS)
        return {"restore_enb": (r.stdout.strip().splitlines() or [str(r.returncode)])[-1], "game_after": f"{n - len(bad)} of {n}",
                "game_after_bad": bad[:5], "settings_restored": restored}


# ---- the game process and its window ----------------------------------------------------------
u32 = ctypes.windll.user32


def running() -> bool:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq SkyrimSE.exe"], capture_output=True, text=True).stdout
    return "SkyrimSE.exe" in out


def window() -> int:
    return u32.FindWindowW(None, "Skyrim Special Edition")


def focus(tries: int = 10) -> bool:
    for _ in range(tries):
        h = window()
        if h and u32.GetForegroundWindow() == h:
            return True
        if h:
            u32.keybd_event(0x12, 0, 0, 0)
            u32.keybd_event(0x12, 0, 2, 0)   # a tap on Alt lets this process set the foreground
            u32.ShowWindow(h, 9)
            u32.SetForegroundWindow(h)
        time.sleep(0.5)
    return False


class KI(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD), ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ctypes.c_void_p)]


class MI(ctypes.Structure):
    _fields_ = [("dx", wintypes.LONG), ("dy", wintypes.LONG), ("mouseData", wintypes.DWORD), ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD), ("dwExtraInfo", ctypes.c_void_p)]


class INPUT(ctypes.Structure):
    class U(ctypes.Union):
        _fields_ = [("ki", KI), ("mi", MI), ("pad", ctypes.c_byte * 32)]
    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", U)]


SCAN = {"`": 0x29, " ": 0x39, "\n": 0x1C, "$": 0x1C, ".": 0x34, "-": 0x0C, "_": 0x0C, '"': 0x28}
SCAN.update({c: v for c, v in zip("1234567890", range(0x02, 0x0C))})
SCAN.update({c: v for c, v in zip("qwertyuiop", range(0x10, 0x1A))})
SCAN.update({c: v for c, v in zip("asdfghjkl", range(0x1E, 0x27))})
SCAN.update({c: v for c, v in zip("zxcvbnm", range(0x2C, 0x33))})
EXTENDED = {"right": 0x4D, "left": 0x4B, "up": 0x48, "down": 0x50}
SHIFTED = set('_"')


def key(scan: int, up: bool, extended: bool = False) -> None:
    i = INPUT(type=1)
    i.ki = KI(0, scan, 0x0008 | (0x0002 if up else 0) | (0x0001 if extended else 0), 0, None)
    u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))


def tap(scan: int, gap: float = 0.06, extended: bool = False, shift: bool = False) -> None:
    """One key. The game window must hold the foreground at every key: a key never goes to
    another window, even when someone at the desk clicks away mid-line."""
    if u32.GetForegroundWindow() != window() and not focus(4):
        raise RuntimeError("the game window lost the foreground")
    if shift:
        key(0x2A, False)
    key(scan, False, extended); time.sleep(gap); key(scan, True, extended); time.sleep(gap)
    if shift:
        key(0x2A, True)


def type_text(text: str, gap: float = 0.06) -> None:
    """Scan codes through SendInput, which the game's input layer reads. Raises on a character
    with no scan code, so a step never drops silently."""
    if not focus():
        raise RuntimeError("the game window would not take the foreground")
    for ch in text:
        c = ch.lower()
        if c not in SCAN:
            raise ValueError(f"no scan code for {ch!r}")
        tap(SCAN[c], gap, shift=ch in SHIFTED)


def hold(ch: str, seconds: float) -> None:
    if not focus():
        raise RuntimeError("the game window would not take the foreground")
    key(SCAN[ch], False); time.sleep(seconds); key(SCAN[ch], True)


def mouse_move(dx: int, dy: int, steps: int = 20) -> None:
    """Relative motion in small steps: the game turns the camera by what DirectInput reports."""
    if not focus():
        raise RuntimeError("the game window would not take the foreground")
    for _ in range(steps):
        i = INPUT(type=0)
        i.mi = MI(dx // steps, dy // steps, 0, 0x0001, 0, None)
        u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))
        time.sleep(0.02)


def mouse_click() -> None:
    """A left click where the game's own menu cursor is (it follows relative motion only)."""
    if u32.GetForegroundWindow() != window() and not focus(4):
        raise RuntimeError("the game window lost the foreground")
    for flags in (0x0002, 0x0004):
        i = INPUT(type=0)
        i.mi = MI(0, 0, 0, flags, 0, None)
        u32.SendInput(1, ctypes.byref(i), ctypes.sizeof(INPUT))
        time.sleep(0.12)


# Commands after which the game closes the console itself: a cell load ('coc') does, and so does
# entering the free camera ('tfc 1'); leaving it ('tfc') does not. A close key typed after them
# was processed after the load and opened the console again, so every later command went into
# the world as key presses (the inventory and the map opened, the next coc never ran). Seen in
# eight harness runs, found from screenshots of the missed cell changes.
CLOSES_CONSOLE = ("tfc 1", "qqq")
CLOSES_CONSOLE_FIRST_WORD = ("coc", "cow", "load", "qqq")   # cow and load are cell loads too


def console(line: str) -> None:
    # Slow on purpose: a toggle key lost while the game hitches (right after a cell load) leaves the
    # console in the other state, and the next command goes into the world as key presses.
    time.sleep(1.0)
    type_text("`", 0.12); time.sleep(1.0)
    type_text(line + "\n", 0.08); time.sleep(0.8)
    words = line.lower().split()
    if " ".join(words) not in CLOSES_CONSOLE and (not words or words[0] not in CLOSES_CONSOLE_FIRST_WORD):
        type_text("`", 0.12)
    time.sleep(1.0)


def status(game: Path) -> dict:
    try:
        t = (game / "raw_harness_status.txt").read_text()
        return {k: int(v) for k, v in (kv.split("=") for kv in t.split())}
    except (OSError, ValueError):
        return {}


def gpu() -> dict:
    q = subprocess.run(["nvidia-smi", "--query-gpu=utilization.gpu,memory.used", "--format=csv,noheader"], capture_output=True, text=True)
    apps = subprocess.run(["nvidia-smi", "--query-compute-apps=pid,process_name", "--format=csv,noheader"], capture_output=True, text=True)
    return {"load": q.stdout.strip(), "compute_apps": [a.strip() for a in apps.stdout.strip().splitlines() if a.strip()]}
