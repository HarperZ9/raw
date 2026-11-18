"""Runner steps that drive the game window directly: the Survival Mode prompt, clicks, desktop
pictures and the minimise check. Mixed into run.Runner. Original work, MIT."""
from __future__ import annotations

import time

import game as G


class Actions:
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

