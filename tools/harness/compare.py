"""Image comparison of two harness captures of the same view (the translate mode against native
D3D11, or any before and after).

    python tools/harness/compare.py <a.png> <b.png> [--diff out.png] [--json]

Prints the mean CIELAB dE76 over all pixels, the share under dE 2 and under dE 10, and the
largest channel difference; --diff writes a heat map (black: equal, white: dE 20 or more). The
scenes are live (wind, people, the sky), so a difference here is not by itself a defect: the
heat map shows where it sits. Original work, MIT.
"""
from __future__ import annotations

import argparse
import json
import sys

import numpy as np
from PIL import Image


def lab(rgb: np.ndarray) -> np.ndarray:
    c = rgb.astype(np.float64) / 255.0
    c = np.where(c <= 0.04045, c / 12.92, ((c + 0.055) / 1.055) ** 2.4)
    m = np.array([[0.4124564, 0.3575761, 0.1804375], [0.2126729, 0.7151522, 0.0721750], [0.0193339, 0.1191920, 0.9503041]])
    xyz = c @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > (6 / 29) ** 3, np.cbrt(xyz), xyz / (3 * (6 / 29) ** 2) + 4 / 29)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def compare(a_path: str, b_path: str, diff: str | None = None) -> dict:
    a = np.asarray(Image.open(a_path).convert("RGB"))
    b = np.asarray(Image.open(b_path).convert("RGB"))
    if a.shape != b.shape:
        return {"a": a_path, "b": b_path, "error": f"sizes differ: {a.shape} and {b.shape}"}
    de = np.linalg.norm(lab(a) - lab(b), axis=-1)
    out = {"a": a_path, "b": b_path, "pixels": int(de.size), "mean_de76": round(float(de.mean()), 3),
           "under_2": round(float((de < 2).mean()), 4), "under_10": round(float((de < 10).mean()), 4),
           "max_channel_diff": int(np.abs(a.astype(int) - b.astype(int)).max())}
    if diff:
        Image.fromarray(np.clip(de / 20.0 * 255.0, 0, 255).astype(np.uint8)).save(diff)
        out["diff"] = diff
    return out


def main() -> int:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("a")
    p.add_argument("b")
    p.add_argument("--diff")
    a = p.parse_args()
    print(json.dumps(compare(a.a, a.b, a.diff)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
