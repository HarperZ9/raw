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
    quit

Original work, MIT.
"""
from __future__ import annotations

import argparse
import json
import shlex
import subprocess
import sys
import threading
import time
from pathlib import Path

import game as G

HERE = Path(__file__).resolve().parent
ALL_STEPS = ("game,match,match_game_mask,control_half_viewport,improved_base,improved_soft,improved_ggx,improved_fog,"
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


class Runner:
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

    def wait_load(self, timeout: float = 120) -> None:
        """A cell load: world frames stop (the loading screen), then 300 more are drawn."""
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
        self.log(f"cell up after {time.time() - start:.0f} s (world frames {self.world()})")

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

    def prepare(self, save: str) -> None:
        """New game state has the Survival Mode message box up. At a resolution the screen can
        show, the keyboard moves to No and Enter accepts it; a click on No follows in case the
        box ignored the keys. Then the state is saved for 'start load'."""
        time.sleep(8)
        G.focus()
        self.shot("prepare_1_prompt")
        # The message box cursor starts near the centre, at (0.494, 0.516) of the frame, and follows
        # relative mouse motion only, at a gain measured in game (1.92 across, 1.54 down at 1080p).
        # No sits at (0.531, 0.760). A picture follows each move and each click.
        w, h = (int(x) for x in self.a.resolution.split("x"))
        for k in range(int(self.a.prompt_tries)):
            G.mouse_move(int((0.531 - 0.494) * w / 1.92), int((0.760 - 0.516) * h / 1.54), steps=10)
            time.sleep(0.5)
            self.shot(f"prepare_2_moved_{k}")
            G.mouse_click()
            time.sleep(2)
            self.shot(f"prepare_3_clicked_{k}")
        G.console(f"save {save}")
        time.sleep(6)
        self.shot("prepare_4_saved")
        self.log(f"typed save {save}")

    def click(self, fx: float, fy: float) -> None:
        h = G.window()
        rect = G.wintypes.RECT()
        G.u32.GetClientRect(h, G.ctypes.byref(rect))
        pt = G.wintypes.POINT(int(rect.right * fx), int(rect.bottom * fy))
        G.u32.ClientToScreen(h, G.ctypes.byref(pt))
        sw, sh = G.u32.GetSystemMetrics(0), G.u32.GetSystemMetrics(1)
        for flags in (0x8001, 0x0002, 0x0004):   # move (absolute), left down, left up
            i = G.INPUT(type=0)
            i.mi = G.MI(int(pt.x * 65535 / max(sw - 1, 1)), int(pt.y * 65535 / max(sh - 1, 1)), 0, flags, 0, None)
            G.u32.SendInput(1, G.ctypes.byref(i), G.ctypes.sizeof(G.INPUT))
            time.sleep(0.1)
        self.log(f"clicked at {fx:.3f}, {fy:.3f} of the window")

    def shot(self, name: str) -> None:
        try:
            from PIL import ImageGrab
            im = ImageGrab.grab()
            im.thumbnail((1600, 900))
            im.save(self.out / f"desktop_{name}.png")
        except Exception as e:   # a missing screenshot never stops a run
            self.log(f"desktop screenshot failed: {e}")

    def minimise(self, seconds: float) -> None:
        w, hh = (int(x) for x in self.a.resolution.split("x"))
        if w > G.u32.GetSystemMetrics(0) or hh > G.u32.GetSystemMetrics(1):
            # A window larger than the display did not come back from a minimise (2160p on 1080p).
            self.log("minimise skipped: the window is larger than the display")
            self.report["minimised"] = {"skipped": "window larger than the display"}
            return
        h = G.window()
        before = G.status(self.inst.game)
        G.u32.ShowWindow(h, 6)   # SW_MINIMIZE
        time.sleep(seconds)
        after = G.status(self.inst.game)
        G.u32.ShowWindow(h, 9)   # SW_RESTORE
        time.sleep(2)
        if not G.focus(20):
            self.log("the window did not come back to the foreground after the minimise")
        drew = after.get("presents", 0) - before.get("presents", 0)
        self.report["minimised"] = {"seconds": seconds, "presents": drew, "world_frames": after.get("world", 0) - before.get("world", 0)}
        self.log(f"minimised {seconds:.0f} s: {drew} presents")

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
            self.inst.deploy(Path(self.a.dll), PROXY_INI)
            self.inst.set_option("SkyrimPrefs.ini", "Display", "iSize W", w)
            self.inst.set_option("SkyrimPrefs.ini", "Display", "iSize H", h)
            self.inst.set_option("SkyrimPrefs.ini", "Interface", "bShowTutorials", "0")
            self.inst.set_option("SkyrimCustom.ini", "General", "bAlwaysActive", "1")
            subprocess.Popen([str(self.inst.game / "skse64_loader.exe")], cwd=self.inst.game)
            self.log(f"launched at {w}x{h}")
            for line in script:
                line = line.split("#", 1)[0].strip()
                if line and not self.command(shlex.split(line, posix=False)):
                    break
            G.console("qqq")
            self.log("typed qqq")
            for _ in range(60):
                if not G.running():
                    break
                time.sleep(1)
            self.report["quit_cleanly"] = not G.running()
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
            self.report["install_after"] = self.inst.after(self.out)
            self.report["gpu_after"] = G.gpu()
            results = self.out / "harness_results.jsonl"
            if results.exists():
                self.report["results"] = [json.loads(l) for l in results.read_text().splitlines() if l.strip()]
            self.compact()
            (self.out / "report.json").write_text(json.dumps(self.report, indent=1))
            self.log(f"report: {self.out / 'report.json'}; ENB {self.report['install_after']['game_after']}, "
                     f"settings restored {self.report['install_after']['settings_restored']}")
        return 0 if ok else 1

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
    return Runner(p.parse_args()).run()


if __name__ == "__main__":
    sys.exit(main())
