"""RAW test harness: one command runs a named test in Skyrim with no one at the keyboard.

    python tools/harness/run.py <test> --game <Skyrim folder> --enb-backup <folder with SHA256SUMS>
        --settings-backup <folder> --dll <built d3d11.dll> --out <results folder> [--resolution 1920x1080]

It checks the ENB backup and the game folder against SHA256SUMS, deploys the proxy with the
test's ini, sets the resolution and switches off tutorials, launches through SKSE, runs the
test script (tests/<test>.txt), quits the game with qqq, restores ENB and the settings files,
verifies every hash, and writes report.json. A test that cannot type into the game stops with
the reason; no step is dropped. Commands of a test script, one a line:

    start load <save> | start new <console command>   from the main menu
    prepare <save>       answer the Survival Mode prompt (No) and save, for later 'start load'
    scene <name>         names the captures that follow
    console <command>    typed into the game console
    wait <seconds> | wait load                          'wait load': the cell is up and drawing
    keyframe <name> [steps=<a,b|all>] [hold=<frames>]   a capture set through the proxy
    mouse <dx> <dy>      turn the camera
    walk <key> <seconds> [keyframe ...]                 hold a key; with a keyframe, capture while moving
    minimise <seconds>   minimise the window and report whether the game kept drawing
    reload ini <key=value ...>          edit [Lighting] in the deployed ini; wait for the proxy to apply it
    reload shader <file> <old> <new>    edit the shader copy (with --shaders); wait for the new pipelines
    quit

Original work, MIT.
"""
from __future__ import annotations

import argparse
import json
import os
import shlex
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

import game as G
from actions import Actions

HERE = Path(__file__).resolve().parent
ALL_STEPS = ("game,match,match_cascades,control_half_viewport,improved_base,improved_soft,improved_ggx,improved_fog,"
             "improved_contact,improved_ao,improved,game_after")

PROXY_INI = """[General]
SafeMode=0
[D3D12Bridge]
Enabled=1
Pass=lighting
Marker=0
FallbackAtPresent=0
TimingLogEvery=300
[GBuffer]
Enabled=1
[Lighting]
Mode=0
Harness=1
Hold=40
"""


def empty_rt_scene(rows: list[dict]) -> list[str]:
    """Keyframes whose improved step ran the ray trace (rt_draws reported) with no draw in the scene."""
    return [f"{r.get('scene')}/{r.get('keyframe')}" for r in rows
            if str(r.get("step", "")).startswith("improved") and "rt_draws" in r and r.get("rt_draws", 0) == 0]


class Runner(Actions):
    def __init__(self, a: argparse.Namespace):
        self.a = a
        self.inst = G.Install(Path(a.game), Path(a.enb_backup), Path(a.settings_backup))
        self.out = Path(a.out)
        self.out.mkdir(parents=True, exist_ok=True)
        self.report: dict = {"test": a.test, "resolution": a.resolution, "steps": [], "keyframes": []}
        self.scene = "scene"
        self.t0 = time.time()

    def log(self, what: str) -> None:
        line = f"{time.time() - self.t0:7.1f} s  {what}"
        print(line, flush=True)
        self.report["steps"].append(line)

    def world(self) -> int:
        return G.status(self.inst.game).get("world", 0)

    def wait_load(self, timeout: float = 120) -> bool:
        """A cell load: world frames stop (the loading screen), then 300 more are drawn. True when
        the loading screen was seen."""
        start = time.time()
        last, still = self.world(), 0.0
        # A load into the cell the player already stands in draws on without a loading screen:
        # after 15 s with no still stretch the cell counts as loaded.
        while time.time() - start < min(timeout, 15) and still < 1.5:
            time.sleep(0.5)
            w = self.world()
            still = still + 0.5 if w == last else 0.0
            last = w
        base = self.world()
        while time.time() - start < timeout and self.world() < base + 300:
            time.sleep(0.5)
        seen = still >= 1.5
        self.log(f"cell up after {time.time() - start:.0f} s (world frames {self.world()}, loading screen {'seen' if seen else 'not seen'})")
        return seen

    def bat(self, lines: list[str]) -> None:
        """Several console commands from one console line: they go into a file beside the game and
        'bat' runs them in order. One open and one close of the console, so a lost toggle cannot
        leave later commands typed into the world. With a coc among them, a loading screen must follow."""
        lines = [l.strip() for l in lines if l.strip()]
        (self.inst.game / "rawbat.txt").write_text("\n".join(lines) + "\n")
        if any(l.lower().startswith("coc ") for l in lines):
            self.change_cell("bat rawbat")
        else:
            G.console("bat rawbat")
            self.log(f"bat: {' | '.join(lines)}")

    def change_cell(self, line: str) -> None:
        """A coc, typed once more if no loading screen follows; a second miss stops the run, so a
        capture never measures the cell before."""
        for attempt in (1, 2):
            before = G.status(self.inst.game)
            G.console(line)
            self.log(f"console: {line} (attempt {attempt})")
            self.wait_load()
            after = G.status(self.inst.game)
            # A loading screen, or the cell's decoded lighting changed (a cell still in memory loads
            # with no loading screen).
            if after.get("loads", 0) > before.get("loads", 0) or after.get("cell") != before.get("cell"):
                return
            self.shot(f"cell_change_missed_{self.scene}_{attempt}")   # what the screen showed instead
        raise RuntimeError(f"the cell change did not take: {line}")

    def wait_menu(self, timeout: float = 150) -> None:
        """The main menu: the game has been presenting for a while and the window takes input."""
        start = time.time()
        while time.time() - start < timeout:
            if G.status(self.inst.game).get("presents", 0) > 900 and G.focus(2):
                self.log("main menu up")
                time.sleep(5)
                return
            time.sleep(1)
        raise RuntimeError("the main menu did not come up")

    def keyframe(self, name: str, opts: dict) -> None:
        steps = opts.get("steps", "all")
        go = [f"scene={self.scene}", f"keyframe={name}", f"steps={ALL_STEPS if steps == 'all' else steps}",
              f"hold={opts.get('hold', 40)}", f"dir={self.out}"]
        done = self.inst.game / "raw_harness_done.txt"
        done.unlink(missing_ok=True)
        (self.inst.game / "raw_harness_go.txt").write_text("\n".join(go) + "\n")
        start = time.time()
        while not done.exists():
            if time.time() - start > 600 or not G.running():
                raise RuntimeError(f"keyframe {name}: no answer from the proxy")
            time.sleep(0.5)
        answer = done.read_text().strip()
        self.log(f"keyframe {self.scene}/{name}: {answer} after {time.time() - start:.0f} s")
        self.report["keyframes"].append({"scene": self.scene, "keyframe": name, "answer": answer})

    def hot(self) -> dict:
        p = self.inst.game / "raw_hot_reload.txt"
        if not p.exists():
            return {}
        out = {}
        for line in p.read_text(errors="replace").splitlines():
            k, _, v = line.partition("=")
            out[k] = v
        return out

    def reload(self, plain: list[str], opts: dict) -> None:
        """A live edit, timed from the write to the proxy's answer in raw_hot_reload.txt (it polls
        every 15 presents and compiles on a worker thread)."""
        before = self.hot()
        if plain[0] == "ini":
            text = (self.inst.game / "d3d11_proxy.ini").read_text(encoding="utf-8")
            lines, inside, seen = text.splitlines(), False, set()
            out = []
            for line in lines:
                if line.strip().startswith("["):
                    if inside:
                        out += [f"{k}={v}" for k, v in opts.items() if k not in seen]
                    inside = line.strip().lower() == "[lighting]"
                elif inside and line.split("=")[0].strip() in opts:
                    k = line.split("=")[0].strip()
                    line, _ = f"{k}={opts[k]}", seen.add(k)
                out.append(line)
            if inside:
                out += [f"{k}={v}" for k, v in opts.items() if k not in seen]
            t0 = time.time()
            (self.inst.game / "d3d11_proxy.ini").write_text("\n".join(out) + "\n", encoding="utf-8")
            key = "params"
        else:
            f = self.inst.game / G.SHADER_COPY / plain[1]
            src = f.read_text(encoding="utf-8")
            old, new = plain[2].strip('"'), plain[3].strip('"')
            if old not in src:
                raise RuntimeError(f"reload: '{old}' is not in {plain[1]}")
            t0 = time.time()
            f.write_text(src.replace(old, new, 1), encoding="utf-8")
            key = "reloads"
        while time.time() - t0 < 60:
            now = self.hot()
            if now.get(key) != before.get(key) or (key == "reloads" and now.get("failures") != before.get("failures")):
                ms = (time.time() - t0) * 1000
                self.log(f"reload {plain[0]}: applied {ms:.0f} ms after the edit; proxy: {now}")
                self.report.setdefault("reloads", []).append({"what": " ".join(plain), "edit_to_applied_ms": round(ms), "proxy": now})
                return
            time.sleep(0.05)
        raise RuntimeError(f"reload {plain[0]}: no answer from the proxy in 60 s")

    def command(self, words: list[str]) -> bool:
        op, args = words[0], words[1:]
        opts = dict(w.split("=", 1) for w in args if "=" in w)
        plain = [w for w in args if "=" not in w]
        if op == "start":
            self.wait_menu()
            if plain[0] == "load":
                G.console(f"load {plain[1]}")
            else:
                G.console(" ".join(plain[1:]))
            self.wait_load()
        elif op == "prepare":
            self.prepare(plain[0])
        elif op == "scene":
            self.scene = plain[0]
        elif op == "bat":
            self.bat(" ".join(args).split("|"))
        elif op == "console" and args and args[0].lower() in ("coc", "cow", "load"):   # cow: centre on a worldspace; load: a save
            self.change_cell(" ".join(args))
        elif op == "console":
            G.console(" ".join(args))
            self.log(f"console: {' '.join(args)}")
        elif op == "wait":
            if plain and plain[0] == "load":
                self.wait_load()
            else:
                time.sleep(float(plain[0]))
        elif op == "keyframe":
            self.keyframe(plain[0], opts)
        elif op == "mouse":
            G.mouse_move(int(plain[0]), int(plain[1]))
        elif op == "walk":
            k, secs = plain[0], float(plain[1])
            if "keyframe" in opts:
                t = threading.Thread(target=G.hold, args=(k, secs))
                t.start()
                self.keyframe(opts.pop("keyframe"), opts)
                t.join()
            else:
                G.hold(k, secs)
        elif op == "minimise":
            self.minimise(float(plain[0]))
        elif op == "engine":
            self.engine(" ".join(args))
        elif op == "reload":
            # shader edits hold '=' and spaces: the quoted words go through whole
            self.reload(args if args and args[0] == "shader" else plain, opts)
        elif op == "quit":
            return False
        else:
            raise ValueError(f"unknown command {op}")
        return True

    def run(self) -> int:
        script = (HERE / "tests" / f"{self.a.test}.txt").read_text(encoding="utf-8").splitlines()
        if G.running():
            raise SystemExit("Skyrim is already running")
        self.report["gpu_before"] = G.gpu()
        self.report["install_before"] = self.inst.before()
        w, h = self.a.resolution.split("x")
        ok = False
        try:
            ini = Path(self.a.ini).read_text(encoding="utf-8") if self.a.ini else PROXY_INI
            if self.a.shaders:
                dst = self.inst.game / G.SHADER_COPY
                shutil.rmtree(dst, ignore_errors=True)
                shutil.copytree(self.a.shaders, dst)
                ini += f"\n[HotReload]\nEnabled=1\nShaderDir={dst}\n"
            self.inst.deploy(Path(self.a.dll), ini + self.extra_ini())
            self.deploy_extras()
            if self.a.plugin:
                from plugin import TestPlugin
                self.plugin = TestPlugin(self.inst.game, Path(self.a.plugin), self.out)
                self.report["plugin"] = self.plugin.deploy()
            self.inst.set_option("SkyrimPrefs.ini", "Display", "iSize W", w)
            self.inst.set_option("SkyrimPrefs.ini", "Display", "iSize H", h)
            self.inst.set_option("SkyrimPrefs.ini", "Interface", "bShowTutorials", "0")
            self.inst.set_option("SkyrimCustom.ini", "General", "bAlwaysActive", "1")
            # RAW needs no script extender. The game starts directly with Steam's app id in the environment, which every
            # runtime accepts; skse64_loader 2.2.6 refuses 1.7.104 (--launcher skse keeps the old way).
            if self.a.launcher == "skse":
                subprocess.Popen([str(self.inst.game / "skse64_loader.exe")], cwd=self.inst.game)
            else:
                env = dict(os.environ, SteamAppId="489830", SteamGameId="489830")
                subprocess.Popen([str(self.inst.game / "SkyrimSE.exe")], cwd=self.inst.game, env=env)
            self.log(f"launched at {w}x{h}")
            for line in script:
                line = line.split("#", 1)[0].strip()
                if line and not self.command(shlex.split(line, posix=False)):
                    break
            self.quit()
            ok = True
        except Exception as e:
            self.report["error"] = f"{type(e).__name__}: {e}"
            self.log(f"stopped: {e}")
        finally:
            if G.running():
                subprocess.run(["taskkill", "/IM", "SkyrimSE.exe", "/F"], capture_output=True)
                for _ in range(20):
                    if not G.running():
                        break
                    time.sleep(1)
                time.sleep(2)
            self.collect_extras()
            if getattr(self, "plugin", None):
                self.report["plugin_after"] = self.plugin.restore()
            self.report["install_after"] = self.inst.after(self.out)
            self.report["gpu_after"] = G.gpu()
            results = self.out / "harness_results.jsonl"
            if results.exists():
                self.report["results"] = [json.loads(l) for l in results.read_text().splitlines() if l.strip()]
                empty = empty_rt_scene(self.report["results"])
                if empty:
                    # The bridge ran with ray tracing and the scene held nothing: the capture is broken
                    # (2026-11-19: the constant-buffer shadows were off, so no draw was decoded).
                    ok = False
                    self.report["error"] = f"ray-tracing scene empty at {', '.join(empty)}: 0 draws captured"
                    self.log(f"failed: {self.report['error']}")
            self.compact()
            (self.out / "report.json").write_text(json.dumps(self.report, indent=1))
            self.log(f"report: {self.out / 'report.json'}; ENB {self.report['install_after']['game_after']}, "
                     f"settings restored {self.report['install_after']['settings_restored']}")
        return 0 if ok else 1

    def engine(self, text: str, timeout: float = 180) -> None:
        """Commands for raw_engine.dll, ';'-separated (set, wait, measure, capture); waits for its answer."""
        game = self.inst.game
        done = game / "raw_engine_done.txt"
        done.unlink(missing_ok=True)
        lines = [c.strip() for c in text.split(";") if c.strip()]
        (game / "raw_engine_go.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
        start = time.time()
        while not done.exists():
            if time.time() - start > timeout:
                raise RuntimeError(f"engine module did not answer in {timeout:.0f} s: {text}")
            time.sleep(0.25)
        done.unlink(missing_ok=True)
        self.log(f"engine: {text} ({time.time() - start:.1f} s)")

    def extra_ini(self) -> str:
        """[Modules] for the engine modules, then any ini text the caller adds (--ini-extra)."""
        text = ""
        if self.a.module:
            text += "[Modules]\nLoad=" + ";".join(Path(m).name for m in self.a.module) + "\n"
        if self.a.ini_extra:
            text += Path(self.a.ini_extra).read_text(encoding="utf-8")
        return text

    def deploy_extras(self) -> None:
        """Engine modules and their input files go beside the game for this run only."""
        self.extras = [Path(m) for m in self.a.module] + [Path(f) for f in self.a.module_file]
        for src in self.extras:
            dst = self.inst.game / src.name
            if dst.exists():
                raise SystemExit(f"{dst.name} already exists in the game folder; refusing to overwrite it")
            shutil.copy2(src, dst)
        self.report["extras"] = [p.name for p in self.extras]
        if self.a.versions:
            # the address tables (data/versions): the module picks the one naming the running exe
            dst = self.inst.game / "raw_versions"
            if dst.exists():
                raise SystemExit("raw_versions already exists in the game folder; refusing to overwrite it")
            shutil.copytree(self.a.versions, dst)
            self.report["versions"] = sorted(p.name for p in dst.glob("*.tsv"))

    def collect_extras(self) -> None:
        """The module's output folder comes to --out; every file this run added leaves the game folder."""
        if self.a.collect:
            src = self.inst.game / self.a.collect
            if src.is_dir():
                shutil.copytree(src, self.out / self.a.collect, dirs_exist_ok=True)
                shutil.rmtree(src)
        for p in getattr(self, "extras", []):
            (self.inst.game / p.name).unlink(missing_ok=True)
        if self.a.versions:
            shutil.rmtree(self.inst.game / "raw_versions", ignore_errors=True)
        # The proxy's diagnostic logs (Data/SKSE/Plugins/RAW/live) come to --out and leave the game folder.
        live = self.inst.game / "Data" / "SKSE" / "Plugins" / "RAW" / "live"
        if live.is_dir():
            shutil.copytree(live, self.out / "proxy_live", dirs_exist_ok=True)
            shutil.rmtree(live)
            for d in (live.parent, live.parent.parent):
                if d.is_dir() and not any(d.iterdir()) and d.name in ("RAW",):
                    d.rmdir()

    def quit(self) -> None:
        """qqq, typed twice if needed, then WM_CLOSE; taskkill in the finally block is the last resort.
        The report names the step that ended the game."""
        for how in ("qqq", "qqq again", "WM_CLOSE"):
            try:
                if how == "WM_CLOSE":
                    G.u32.PostMessageW(G.window(), 0x0010, 0, 0)
                else:
                    G.console("qqq")
            except RuntimeError as e:
                self.log(f"quit by {how}: {e}")
            for _ in range(20):
                if not G.running():
                    self.report["quit_cleanly"], self.report["quit_by"] = True, how
                    self.log(f"the game quit by {how}")
                    return
                time.sleep(1)
        self.report["quit_cleanly"], self.report["quit_by"] = False, "taskkill"

    def compact(self) -> None:
        """Captures as PNG; the BMPs go."""
        try:
            from PIL import Image
        except ImportError:
            return
        for bmp in self.out.glob("*.bmp"):
            Image.open(bmp).convert("RGB").save(bmp.with_suffix(".png"), optimize=False, compress_level=6)
            bmp.unlink()


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("test")
    p.add_argument("--game", required=True)
    p.add_argument("--enb-backup", required=True)
    p.add_argument("--settings-backup", required=True)
    p.add_argument("--dll", required=True)
    p.add_argument("--out", required=True)
    p.add_argument("--resolution", default="3840x2160")
    p.add_argument("--prompt-tries", default="1")
    p.add_argument("--module", action="append", default=[], help="engine module DLL, loaded by the proxy")
    p.add_argument("--module-file", action="append", default=[], help="input file copied beside the game")
    p.add_argument("--ini-extra", help="text appended to the proxy ini")
    p.add_argument("--collect", help="folder in the game directory to bring back and remove")
    p.add_argument("--shaders", default="", help="a shader folder to copy beside the game for hot reload (src/d3d12_bridge/shaders)")
    p.add_argument("--versions", default="", help="the address-table folder (data/versions), deployed as raw_versions")
    p.add_argument("--launcher", default="direct", choices=["direct", "skse"], help="start SkyrimSE.exe (default) or skse64_loader")
    p.add_argument("--plugin", default="", help="a test plugin folder (tools/harness/plugin.py), deployed for this run only")
    p.add_argument("--ini", default="", help="a d3d11_proxy.ini to deploy instead of the lighting one (the translate tests)")
    return Runner(p.parse_args()).run()


if __name__ == "__main__":
    sys.exit(main())
