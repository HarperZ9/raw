"""Tone map check for one harness run: the game's tone map against RAW's on a scene.

usage: python -I tonecheck.py <run folder> <scene> [--scale 4] [--ppd 16.75]

Reads the keyframes a tone-map test writes (tests/rt7tone.txt):
  <scene>_ref_improved.png / <scene>_ref_raw_improved.png      the path-traced reference through each tone map
  <scene>_irr_pre_improved.png / <scene>_irr_pre_raw_improved.png   RAW's irradiance (View 13) through each
  <scene>_frame_game.png, <scene>_frame_improved.png, <scene>_frame_raw_improved.png   the frames

FLIP between irradiance and reference through the same tone map measures RAW's lighting as each tone map shows
it. A tone map that flattens everything would also lower it, so the frames carry three guards beside it:
the share of clipped pixels (any channel at 250 or more), the mean, and detail (mean absolute Laplacian of luma).
Original work, MIT.
"""
import argparse
import sys
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parent))
import flip  # noqa: E402


def load(p: Path, scale: int) -> np.ndarray:
    """As flip.py loads and downsamples (the mean of each scale x scale block, in sRGB)."""
    img = flip.load(str(p), 1.0)
    if scale > 1:
        h, w = img.shape[0] // scale * scale, img.shape[1] // scale * scale
        img = img[:h, :w].reshape(h // scale, scale, w // scale, scale, 3).mean((1, 3))
    return img


def guards(img: np.ndarray) -> dict:
    y = img @ np.array([0.2126, 0.7152, 0.0722])
    lap = np.abs(4 * y[1:-1, 1:-1] - y[:-2, 1:-1] - y[2:, 1:-1] - y[1:-1, :-2] - y[1:-1, 2:])
    return {"mean": float(img.mean() * 255), "clipped": float((img.max(axis=2) >= 250 / 255).mean()), "detail": float(lap.mean() * 255)}


def main() -> int:
    a = argparse.ArgumentParser()
    a.add_argument("run")
    a.add_argument("scene")
    a.add_argument("--scale", type=int, default=4)
    a.add_argument("--ppd", type=float, default=16.75)
    a.add_argument("--irr", default="irr_pre", help="which irradiance keyframe: irr_pre, irr_post, irr_post300")
    o = a.parse_args()
    run, s = Path(o.run), o.scene
    out = {}
    for tm, ref, irr in (("game", f"{s}_ref_improved.png", f"{s}_{o.irr}_improved.png"),
                         ("raw", f"{s}_ref_raw_improved.png", f"{s}_{o.irr}_raw_improved.png")):
        if (run / ref).exists() and (run / irr).exists():
            e = flip.flip(load(run / ref, o.scale), load(run / irr, o.scale), o.ppd)
            out[tm] = float(e.mean())
            print(f"FLIP irradiance vs reference, {tm} tone map: {out[tm]:.4f}")
    for name in (f"{s}_frame_game.png", f"{s}_frame_improved.png", f"{s}_frame_raw_improved.png"):
        if (run / name).exists():
            g = guards(load(run / name, 1))
            print(f"{name}: mean {g['mean']:.1f}, clipped {g['clipped'] * 100:.2f} %, detail {g['detail']:.2f}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
