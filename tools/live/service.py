"""RAW's live session service: one Skyrim kept up for hours, tuned in place by every worker through a request queue.

    node gpu-lock.mjs run --lock gpu "raw live session" -- python tools/live/service.py --game <Skyrim folder>
        --enb-backup <ENB backup with SHA256SUMS> --settings-backup <folder> --dll <built d3d11.dll>
        --module <built raw_engine.dll> --shaders src/d3d12_bridge/shaders [--ini <d3d11_proxy.ini>] [--resolution WxH]

The service is the only holder of the game: it starts it, runs the requests in arrival order (tools/live/req.py
submits them), writes <root>/state.json every 2 s, restarts the game after a crash, ends it below 3 GB available
and starts it again at 6 GB, and restores the install whenever the game goes down. `stop` ends the session; so
does <root>/STOP or Ctrl-C. --restore-only puts an install back after a service that was itself killed.
Original work, MIT.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
import traceback
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from live_queue import LiveQueue, now_ms, write_json   # noqa: E402
from session import ABORT_MB, START_MB, G, LiveSession, avail_mb   # noqa: E402
import kinds   # noqa: E402

MAX_RESTARTS = 6


class Service:
    def __init__(self, cfg: argparse.Namespace):
        self.cfg = cfg
        self.q = LiveQueue(Path(cfg.root))
        self.sid = time.strftime("%Y%m%d-%H%M%S")
        self.dir = Path(cfg.root) / "sessions" / self.sid
        self.dir.mkdir(parents=True, exist_ok=True)
        self.s = LiveSession(cfg, self.dir)
        self.t0 = time.time()
        self.restarts: list[dict] = []
        self.counts = {"done": 0, "failed": 0}
        self.current: dict | None = None
        self.last: dict | None = None
        self.stopping = False

    # ---- state ----------------------------------------------------------------------------------------------
    def state(self) -> dict:
        s = self.s
        return {"session": self.sid, "up": s.up and G.running(), "since": time.strftime("%H:%M:%S", time.localtime(s.started_at)) if s.up else None,
                "uptime_s": round(s.uptime()), "service_uptime_s": round(time.time() - self.t0), "resolution": s.a.resolution,
                "save": self.cfg.save, "scene": s.scene, "modules": [Path(m).name for m in s.a.module],
                "module_reloads": getattr(s, "module_gen", 0), "hot_reload": s.hot(), "proxy": G.status(s.inst.game),
                "available_mb": avail_mb(), "restarts": self.restarts, "requests": self.counts,
                "running": self.current, "last": self.last, "queue": len(list(self.q.q.glob("*.json"))),
                "updated": time.strftime("%H:%M:%S"), "dir": str(self.dir)}

    def write_state(self) -> None:
        write_json(Path(self.cfg.root) / "state.json", self.state())

    # ---- the game's life ------------------------------------------------------------------------------------
    def bring_up(self, why: str) -> None:
        while avail_mb() < START_MB and not self.stopping:
            self.s.log(f"waiting for {START_MB} MB available ({avail_mb()} MB) before {why}")
            self.write_state()
            time.sleep(15)
        t = time.time()
        self.s.start()
        self.s.log(f"{why}: up in {time.time() - t:.0f} s")

    def restart(self, why: str) -> None:
        self.restarts.append({"time": time.strftime("%H:%M:%S"), "why": why})
        self.s.teardown(why)
        if len(self.restarts) > MAX_RESTARTS:
            raise RuntimeError(f"{len(self.restarts)} restarts; the service stops rather than loop")
        self.bring_up(f"restart {len(self.restarts)}")

    def health(self) -> None:
        if self.stopping:
            return
        if not self.s.up:   # a restart that failed half way: try again, counted
            self.restart("the game is not up")
        elif not G.running():
            self.restart("the game exited (crash or closed)")
        elif avail_mb() < ABORT_MB:
            self.restart(f"memory below {ABORT_MB} MB available ({avail_mb()} MB)")

    # ---- requests -------------------------------------------------------------------------------------------
    def run_one(self, path: Path, req: dict) -> None:
        rid, kind, args = req["id"], req.get("kind", ""), req.get("args", {})
        out = self.q.results / rid
        out.mkdir(parents=True, exist_ok=True)
        started = now_ms()
        self.current = {"id": rid, "kind": kind, "requester": req.get("requester")}
        self.write_state()
        self.s.log(f"request {rid} ({req.get('requester')}): {kind} {json.dumps(args)[:200]}")
        result = {"id": rid, "kind": kind, "requester": req.get("requester"), "args": args, "ok": True, "error": None}
        try:
            if kind == "status":
                data = self.state()
            elif kind == "stop":
                self.stopping, data = True, {"stopping": True}
            elif kind == "restart":
                if args.get("resolution"):
                    self.s.a.resolution = args["resolution"]
                if args.get("dll"):
                    self.s.a.dll = args["dll"]
                self.restart(args.get("why", f"asked by {req.get('requester')}"))
                data = {"up": self.s.up, "resolution": self.s.a.resolution}
            elif kind in kinds.HANDLERS:
                if not self.s.up:
                    raise RuntimeError("the game is not up")
                data = kinds.HANDLERS[kind](self.s, args, out)
            else:
                raise ValueError(f"unknown kind {kind!r}; kinds: {', '.join(sorted(kinds.HANDLERS) + ['restart', 'status', 'stop'])}")
            result["data"] = data
        except Exception as e:
            result.update(ok=False, error=f"{type(e).__name__}: {e}")
            (out / "traceback.txt").write_text(traceback.format_exc(), encoding="utf-8")
        finally:
            self.s.out = self.dir
        fin = now_ms()
        result.update(submitted_ms=req.get("submitted_ms"), started_ms=started, finished_ms=fin,
                      queue_wait_ms=started - req.get("submitted_ms", started), run_ms=fin - started,
                      latency_ms=fin - req.get("submitted_ms", started), session_uptime_s=round(self.s.uptime()),
                      session=self.sid, restarts=len(self.restarts), files=sorted(p.name for p in out.iterdir()))
        self.q.finish(path, result)
        self.counts["done" if result["ok"] else "failed"] += 1
        self.current, self.last = None, {"id": rid, "kind": kind, "ok": result["ok"], "run_ms": result["run_ms"]}
        self.s.log(f"request {rid}: {'ok' if result['ok'] else 'failed: ' + str(result['error'])} in {result['run_ms']} ms")
        # a request that took the game down is answered first, then the game comes back
        self.health()

    def loop(self) -> int:
        stop_file = Path(self.cfg.root) / "STOP"
        stop_file.unlink(missing_ok=True)
        for rid in self.q.recover():
            self.s.log(f"answered {rid} as failed: left running by an earlier service")
        try:
            self.bring_up("start")
            tick = 0.0
            while not self.stopping and not stop_file.exists():
                job = self.q.claim()
                if job:
                    self.run_one(*job)
                    continue
                if time.time() - tick > 2:
                    self.health()
                    self.write_state()
                    tick = time.time()
                time.sleep(0.1)
        except KeyboardInterrupt:
            self.s.log("Ctrl-C")
        except Exception as e:
            self.s.log(f"service error: {type(e).__name__}: {e}")
            (self.dir / "service_error.txt").write_text(traceback.format_exc(), encoding="utf-8")
        finally:
            after = self.s.teardown("the service is stopping")
            self.write_state()
            (self.dir / "report.json").write_text(json.dumps(self.s.report, indent=1), encoding="utf-8")
        ok = after["game_after_bad"] == [] and after["settings_restored"] and after["plugins_txt_restored"] is not False and not after["leftovers"]
        return 0 if ok else 2


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("--root", default="E:/raw-live")
    p.add_argument("--game", required=True)
    p.add_argument("--enb-backup", required=True)
    p.add_argument("--settings-backup", required=True)
    p.add_argument("--dll", required=True)
    p.add_argument("--module", action="append", default=[])
    p.add_argument("--module-file", action="append", default=[])
    p.add_argument("--shaders", default="")
    p.add_argument("--ini", default="", help="a d3d11_proxy.ini; the default is the harness's lighting ini")
    p.add_argument("--versions", default=str(HERE.parent.parent / "data" / "versions"))
    p.add_argument("--resolution", default="3840x2160")
    p.add_argument("--save", default="rawharness")
    p.add_argument("--restore-only", action="store_true", help="restore the install after a killed service, then exit")
    cfg = p.parse_args()
    svc = Service(cfg)
    if cfg.restore_only:
        after = svc.s.teardown("restore-only")
        print(json.dumps(after, indent=1))
        return 0 if not after["game_after_bad"] and not after["leftovers"] else 2
    return svc.loop()


if __name__ == "__main__":
    sys.exit(main())
