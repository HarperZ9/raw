"""The live game session: one Skyrim kept up for hours, started and restored by the same checks as the harness.

Start: 6 GB available or more, the ENB backup and the game folder checked against SHA256SUMS, both settings inis
equal to their backups, Plugins.txt hashed and copied. Then the proxy, its ini, the engine module, the address
tables and the shader copy go beside the game, the resolution and tutorials are set for the session only, the
game starts directly (SteamAppId 489830), and the rawharness save loads from the main menu.
End (by request, crash, or memory below 3 GB): the game is quit or ended, every placed file goes, ENB comes back
through restore-enb.cmd with its 232 hashes checked, and both inis and Plugins.txt are checked by hash.
Original work, MIT.
"""
from __future__ import annotations

import argparse
import ctypes
import hashlib
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent / "harness"))
import game as G          # noqa: E402  the harness's install, input and heartbeat
from run import Runner, PROXY_INI   # noqa: E402

START_MB, ABORT_MB = 6144, 3072
LIVE_INI_EXTRA = "[EngineRT]\nGpuTiming=0\nCallGraph=0\n"


def avail_mb() -> int:
    """Available physical memory (the same figure as \\Memory\\Available MBytes, to within the standby list)."""
    class MS(ctypes.Structure):
        _fields_ = [("dwLength", ctypes.c_ulong), ("dwMemoryLoad", ctypes.c_ulong)] + \
                   [(n, ctypes.c_ulonglong) for n in ("total", "avail", "tpf", "apf", "tv", "av", "aev")]
    m = MS()
    m.dwLength = ctypes.sizeof(MS)
    ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(m))
    return int(m.avail // (1024 * 1024))


def sha(p: Path) -> str:
    return hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else ""


def retry(fn, what: str, tries: int = 20, gap: float = 3.0):
    """A file the dying game still holds (the proxy log, 2026-11-19) is retried, not skipped."""
    for k in range(tries):
        try:
            return fn()
        except PermissionError as e:
            if k == tries - 1:
                raise RuntimeError(f"{what}: still locked after {tries * gap:.0f} s ({e})") from e
            time.sleep(gap)


class LiveSession(Runner):
    def __init__(self, cfg: argparse.Namespace, out: Path):
        a = argparse.Namespace(test="live", game=cfg.game, enb_backup=cfg.enb_backup, settings_backup=cfg.settings_backup,
                               out=str(out), resolution=cfg.resolution, prompt_tries="1", module=cfg.module,
                               module_file=cfg.module_file, ini_extra="", collect="raw_engine_out", shaders=cfg.shaders,
                               versions=cfg.versions, launcher="direct", plugin="", skse_file=[], ini=cfg.ini, dll=cfg.dll)
        super().__init__(a)
        self.cfg = cfg
        self.plugins_list = Path(os.environ["LOCALAPPDATA"]) / "Skyrim Special Edition" / "Plugins.txt"
        self.placed: list[Path] = []      # reloaded module copies and other files the session put beside the game
        self.started_at = 0.0
        self.up = False
        self.logfile = out / "session.log"

    def log(self, what: str) -> None:
        super().log(what)
        with open(self.logfile, "a", encoding="utf-8") as f:
            f.write(f"{time.strftime('%H:%M:%S')}  {what}\n")
        del self.report["steps"][:-200]   # the session runs for hours; the file keeps the whole log

    # ---- start ----------------------------------------------------------------------------------------------
    def start(self) -> None:
        if G.running():
            raise SystemExit("Skyrim is already running; the live session starts only on a free install")
        mb = avail_mb()
        if mb < START_MB:
            raise RuntimeError(f"{mb} MB available, the session needs {START_MB} MB to start")
        self.report["install_before"] = self.inst.before()
        self.plugins_sha = sha(self.plugins_list)
        shutil.copy2(self.plugins_list, self.out / "Plugins.txt.before")
        ini = Path(self.a.ini).read_text(encoding="utf-8") if self.a.ini else PROXY_INI
        if self.a.shaders:
            dst = self.inst.game / G.SHADER_COPY
            shutil.rmtree(dst, ignore_errors=True)
            shutil.copytree(self.a.shaders, dst)
            ini += f"\n[HotReload]\nEnabled=1\nShaderDir={dst}\n"
        if "[EngineRT]" not in ini:
            ini += "\n" + LIVE_INI_EXTRA
        self.inst.deploy(Path(self.a.dll), ini + "\n" + self.extra_ini())
        self.deploy_extras()
        w, h = self.a.resolution.split("x")
        self.inst.set_option("SkyrimPrefs.ini", "Display", "iSize W", w)
        self.inst.set_option("SkyrimPrefs.ini", "Display", "iSize H", h)
        self.inst.set_option("SkyrimPrefs.ini", "Interface", "bShowTutorials", "0")
        self.inst.set_option("SkyrimCustom.ini", "General", "bAlwaysActive", "1")
        env = dict(os.environ, SteamAppId="489830", SteamGameId="489830")
        subprocess.Popen([str(self.inst.game / "SkyrimSE.exe")], cwd=self.inst.game, env=env)
        self.log(f"launched at {w}x{h}, {mb} MB available")
        self.wait_menu()
        G.console(f"load {self.cfg.save}")
        self.wait_load()
        self.started_at = time.time()
        self.last_move = ""   # the save's own cell is not known by name
        self.up = True
        self.log(f"session up: save {self.cfg.save} loaded")

    # ---- end ------------------------------------------------------------------------------------------------
    def teardown(self, why: str) -> dict:
        """Quit (qqq, then WM_CLOSE) when the game still answers, end it otherwise, and restore every file."""
        self.log(f"ending the session: {why}")
        self.up = False
        if G.running():
            try:
                self.quit()
            except Exception as e:   # a hung game cannot type; taskkill follows
                self.log(f"quit failed: {e}")
        if G.running():
            subprocess.run(["taskkill", "/IM", "SkyrimSE.exe", "/F"], capture_output=True)
            for _ in range(30):
                if not G.running():
                    break
                time.sleep(1)
        time.sleep(2)
        # every reloaded copy, the ones a killed service left behind included
        for p in self.placed + sorted(self.inst.game.glob("raw_engine.r*.dll")):
            retry(lambda p=p: p.unlink(missing_ok=True), f"remove {p.name}")
        self.placed.clear()
        if not hasattr(self, "extras"):   # --restore-only: the modules named on the command line
            self.extras = [Path(m) for m in self.a.module] + [Path(f) for f in self.a.module_file]
        retry(self.collect_extras, "collect the module output")
        after = retry(lambda: self.inst.after(self.out), "restore the install")
        want = getattr(self, "plugins_sha", "")
        plugins_ok = sha(self.plugins_list) == want if want else None   # None: no start, nothing to compare with
        if plugins_ok is False:
            shutil.copy2(self.out / "Plugins.txt.before", self.plugins_list)
            plugins_ok = sha(self.plugins_list) == want
        after["plugins_txt_restored"] = plugins_ok
        leftovers = [n for n in ("d3d11_proxy.ini", G.SHADER_COPY, "raw_versions", "raw_engine_out", "raw_engine.dll")
                     if (self.inst.game / n).exists()]
        leftovers += [p.name for p in self.inst.game.glob("raw_engine.r*.dll")]
        after["leftovers"] = leftovers
        self.report.setdefault("restores", []).append({"why": why, "time": time.strftime("%H:%M:%S"), **after})
        self.log(f"restored: ENB {after['game_after']}, settings {after['settings_restored']}, Plugins.txt {plugins_ok}, "
                 f"left in the game folder: {leftovers or 'nothing'}")
        return after

    def uptime(self) -> float:
        return time.time() - self.started_at if self.up else 0.0
