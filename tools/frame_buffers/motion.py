"""Motion vectors against a moving camera.

usage: python motion.py <capture N> [<capture N+1>] [<out json>]

  within one frame   the motion target equals the reprojection of each opaque pixel with the
                     game's current and previous view-projection (0.5 px), with the camera's
                     own motion stated; a control uses the current matrix for both
  frame N to N+1     every pixel of frame N+1 is fetched from frame N at uv + motion; the
                     finished frames must then agree better than with no motion and better
                     than with the motion negated

The motion target holds (current NDC - previous NDC) * (-0.5, 0.5), so the previous position
of a pixel is uv + motion. Original work, MIT.
"""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

import numpy as np

import rawtex as R
import roles
from gbuffer import final, span_kind
from lights import draw_constants, vec4


def load(capture: Path) -> dict:
    m = json.loads((capture / "manifest.json").read_text())
    by = {v: k for k, v in roles.assign(m).items()}
    seq = next(iv["seq"] for iv in m["intervals"] if iv["draws"] and span_kind(iv, by) == "opaque")
    cbs = draw_constants(capture)
    with open(capture / "draws.csv", newline="") as f:
        main = [int(r["draw"]) for r in csv.DictReader(f) if int(r["interval"]) == seq]
    frame = next(vec4(cbs[d][7]) for d in main if d in cbs and len(cbs[d][7]) >= 720)
    # World positions are relative to the camera, so a camera that only translates leaves both
    # view-projections equal. Its step is in the draws: each static draw carries its world matrix
    # and the one of the frame before (vertex buffer 2, rows 0 to 2 and 3 to 5).
    steps = [vec4(cbs[d][2])[3:6, 3] - vec4(cbs[d][2])[0:3, 3] for d in main if d in cbs and len(cbs[d][2]) == 240]
    step = np.median(np.array(steps), axis=0) if steps else np.zeros(3)
    return {"m": m, "by": by, "frame": frame, "capture": capture, "camera_step": step}


def within_frame(c: dict, step: int = 2) -> list[dict]:
    w, h = c["m"]["backbuffer"]
    by, f = c["by"], c["frame"]
    depth = R.depth24_exact(R.read(c["capture"] / f"t{by['depth_main']:02d}_final.rawtex"))[::step, ::step]
    mv = final(c["capture"], by["motion_vectors"])[::step, ::step].astype(np.float64)
    cls = np.round(final(c["capture"], by["raw_specular"])[::step, ::step, 2] * 255).astype(int) if "raw_specular" in by else np.zeros(depth.shape, int)
    ys, xs = np.mgrid[0:h:step, 0:w:step]
    ndc = np.stack([(xs + 0.5) / w * 2 - 1, 1 - (ys + 0.5) / h * 2, depth, np.ones_like(depth)], -1)
    cur, prev, inv = f[12:16], f[16:20], f[32:36]          # world to clip now and before (unjittered), clip to world (jittered)
    world = ndc @ inv.T
    world /= world[..., 3:4]
    shifted = world.copy()
    shifted[..., :3] += c["camera_step"]                   # the same point, relative to the camera of the frame before
    def project(mat, pts=world):
        p = pts @ mat.T
        return p[..., :2] / p[..., 3:4]
    keep = (depth < 0.9999) & (cls < 128)
    out = []
    for name, pts in (("motion vectors equal the reprojection with the previous view-projection and the camera's step (0.5 px)", shifted),
                      ("control: the camera's step left out", world)):
        expect = (project(cur) - project(prev, pts)) * np.array([-0.5, 0.5])
        err = np.abs((mv - expect) * np.array([w, h])).max(-1)[keep]
        cam = np.linalg.norm(expect[keep] * np.array([w, h]), axis=-1)
        out.append({"check": name, "pixels": int(keep.sum()), "within_half_pixel": float((err < 0.5).mean()),
                    "camera_motion_px_median": float(np.median(cam)), "camera_motion_px_max": float(cam.max()),
                    "camera_step_units": [round(float(x), 3) for x in c["camera_step"]]})
    out[0]["pass"] = out[0]["within_half_pixel"] > 0.97 and out[0]["camera_motion_px_median"] > 1.0
    out[1]["pass"] = out[1]["within_half_pixel"] < out[0]["within_half_pixel"] - 0.3
    return out


def across_frames(a: dict, b: dict) -> list[dict]:
    w, h = a["m"]["backbuffer"]
    before = final(a["capture"], a["by"]["backbuffer"])[..., :3].astype(np.float32)
    after = final(b["capture"], b["by"]["backbuffer"])[..., :3].astype(np.float32)
    mv = final(b["capture"], b["by"]["motion_vectors"]).astype(np.float64)
    depth = R.depth24_exact(R.read(b["capture"] / f"t{b['by']['depth_main']:02d}_final.rawtex"))
    ys, xs = np.mgrid[0:h, 0:w]
    out = []
    errors = {}
    for name, sign in (("with the motion vectors", 1.0), ("with no motion", 0.0), ("with the motion negated", -1.0)):
        sx = np.rint(xs + sign * mv[..., 0] * w).astype(int); sy = np.rint(ys + sign * mv[..., 1] * h).astype(int)
        ok = (sx >= 0) & (sx < w) & (sy >= 0) & (sy < h) & (depth < 0.9999)
        errors[name] = float(np.abs(after[ok] - before[sy[ok], sx[ok]]).mean())
    moved = float(np.median(np.linalg.norm(mv.reshape(-1, 2) * np.array([w, h]), axis=1)))
    out.append({"check": "frame N+1 fetched from frame N along the motion vectors agrees best", "mean_abs_difference": errors,
                "motion_px_median": moved,
                "pass": errors["with the motion vectors"] < 0.8 * errors["with no motion"] and
                        errors["with the motion vectors"] < 0.8 * errors["with the motion negated"]})
    return out


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.endswith(".json")]
    outs = [a for a in sys.argv[1:] if a.endswith(".json")]
    a = load(Path(args[0]))
    checks = within_frame(a)
    if len(args) > 1:
        checks += across_frames(a, load(Path(args[1])))
    for c in checks: print("PASS" if c["pass"] else "FAIL", {k: v for k, v in c.items() if k != "pass"})
    if outs: Path(outs[0]).write_text(json.dumps({"checks": checks}, indent=1))
    sys.exit(0 if all(c["pass"] for c in checks) else 1)
