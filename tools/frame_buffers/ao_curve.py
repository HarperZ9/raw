"""Speed against accuracy for the engine's GTAO on a captured frame, by sample count.

usage: python ao_curve.py <engine_gtao_check.exe> <capture dir> <proj file> <work dir> [<out json>]

Runs the pass on the hardware GPU for several slice and step counts and compares each
visibility image with the engine's default (16 slices, 16 steps): mean absolute difference,
RMS difference, and the share of drawn pixels more than 0.05 away. GPU time is the pass's own
timestamp median. The capture supplies the depth, normal and frame files by role.

Original work, MIT.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from pathlib import Path

import numpy as np

import roles

CONFIGS = [(16, 16), (8, 16), (8, 8), (4, 8), (4, 4), (2, 8), (2, 4), (1, 4)]


def main() -> None:
    exe, capture, proj_file, work = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3]), Path(sys.argv[4])
    work.mkdir(parents=True, exist_ok=True)
    m = json.loads((capture / "manifest.json").read_text())
    by = {v: k for k, v in roles.assign(m).items()}
    ivs = [iv for iv in m["intervals"] if iv["draws"] > 0]
    n = by["normals_masks"]
    end = [iv["seq"] for iv in ivs if n in [r["tex"] for r in iv["rt"]] and n in iv["history"]][-1]
    blit = [iv["seq"] for iv in ivs if iv["rt"] and iv["rt"][0]["tex"] == by["backbuffer"]][0]
    w, h = m["backbuffer"]
    rows, ref = [], None
    for slices, steps in CONFIGS:
        dump = work / f"s{slices}x{steps}"
        out = subprocess.run([exe, "--depth", str(capture / f"t{by['depth_main']:02d}_i{end:03d}.rawtex"),
                              "--normal", str(capture / f"t{n:02d}_i{end:03d}.rawtex"),
                              "--color", str(capture / f"t{by['backbuffer']:02d}_i{blit:03d}.rawtex"),
                              "--proj", *proj_file.read_text().split(), "--slices", str(slices), "--steps", str(steps),
                              "--frames", "10", "--dump", str(dump)], capture_output=True, text=True).stdout
        t = re.search(r"engine pass median ([\d.]+) ms \(pack ([\d.]+), gtao ([\d.]+)", out)
        par = re.search(r"\(([\d.]+) %\), worst", out)
        vis = np.fromfile(str(dump) + "_visibility.f32", dtype=np.float32).reshape(h, w).astype(np.float64)
        if ref is None:
            ref = vis
        d = np.abs(vis - ref)
        rows.append({"slices": slices, "steps": steps, "samples_per_pixel": slices * steps * 2,
                     "pass_ms": float(t.group(1)) if t else None, "gtao_ms": float(t.group(3)) if t else None,
                     "outside_1e-3_vs_cpu_reference_percent": float(par.group(1)) if par else None,
                     "mean_abs_diff_vs_16x16": round(float(d.mean()), 5), "rms_diff_vs_16x16": round(float(np.sqrt((d * d).mean())), 5),
                     "pixels_over_0.05_percent": round(100.0 * float((d > 0.05).mean()), 3)})
        print(rows[-1])
        for suffix in ("_visibility.f32", "_reference.f32", "_composite.rgba"):
            if (slices, steps) != CONFIGS[0] or suffix != "_visibility.f32":
                Path(str(dump) + suffix).unlink(missing_ok=True)
    if len(sys.argv) > 5:
        Path(sys.argv[5]).write_text(json.dumps({"width": w, "height": h, "radius_game_units": 35, "rows": rows}, indent=1))


if __name__ == "__main__":
    main()
