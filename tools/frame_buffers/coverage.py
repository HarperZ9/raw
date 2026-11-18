"""Per-frame coverage: which pixels of the frame have real surface data in the game's buffers.

usage: python coverage.py <capture dir> [<out json> [<out png>]]

Every pixel gets the class of the last scene draw span that changed it. Nothing is estimated:
a span's pixels are the ones whose scene colour differs between the copies the ledger took
before and after that span (History=1), and "has a normal" means the normal target texel
differs from its clear value. A draw that writes the exact colour already there is not seen.

Classes, in the order they are drawn:
  sky              no geometry at this pixel (depth still at the far plane after the opaque pass)
  opaque           opaque pass, normal written: depth, normal and motion describe this surface
  opaque_no_normal opaque pass, depth written, normal target left at its clear value
  water            the water draw: the buffers describe the bed under the water
  blended          blended draws (clouds, mist, particles, glass): the buffers describe what is behind
  late             draws that use the prepass depth copy as their depth buffer

Original work, MIT.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np

import rawtex as R
import roles

RG8, RGBA16F, RGB10A2 = 49, 10, 24
CLASSES = ["sky", "opaque", "opaque_no_normal", "water", "blended", "late"]
REAL = {"sky": "nothing to describe", "opaque": "depth, normal, motion", "opaque_no_normal": "depth, motion",
        "water": "none for the water surface", "blended": "none for the blended surface", "late": "colour and motion only"}
MISSING = {"sky": "", "opaque": "albedo, specular and gloss, emissive, material class (never written by the game)",
           "opaque_no_normal": "normal", "water": "water depth, normal, motion",
           "blended": "depth, normal, motion of the blended surface", "late": "depth in the scene depth buffer, normal"}


def _raw(capture: Path, tex: int, seq: int) -> np.ndarray:
    t = R.read(capture / f"t{tex:02d}_i{seq:03d}.rawtex")
    return t.data[0]


def analyse(capture: Path) -> dict:
    m = json.loads((capture / "manifest.json").read_text())
    role = roles.assign(m)
    by = {v: k for k, v in role.items()}
    scene, normal, depth = by["scene_hdr"], by["normals_masks"], by["depth_main"]
    tex = {t["id"]: t for t in m["textures"]}
    ivs = [iv for iv in m["intervals"] if iv["draws"] > 0]
    w, h = m["backbuffer"]

    def has(iv, t): return t in iv["history"]
    normal_ivs = [iv for iv in ivs if normal in [r["tex"] for r in iv["rt"]] and has(iv, normal)]
    opaque_end = normal_ivs[-1]["seq"]
    depth_at = max(iv["seq"] for iv in ivs if iv["seq"] <= opaque_end and has(iv, depth))

    d = _raw(capture, depth, depth_at)[:, : w * 4].copy().view(np.uint32) & 0xFFFFFF
    n = _raw(capture, normal, opaque_end)[:, : w * 4].copy().view(np.uint32)
    sky = d == 0xFFFFFF
    clear = np.bincount((n[sky] & 0xFFFF).ravel()).argmax() if sky.any() else 0x7F7F
    cls = np.where(sky, 0, np.where((n & 0xFFFF) == clear, 2, 1)).astype(np.uint8)
    del d, n

    spans = []
    prev = None
    for iv in ivs:
        if not has(iv, scene):
            continue
        cur = _raw(capture, scene, iv["seq"])
        rts = [r["tex"] for r in iv["rt"]]
        fmts = [tex[i]["format"] for i in rts]
        ds = iv["ds"]["tex"]
        kind = None
        if iv["seq"] > opaque_end and rts[0] == scene and ds >= 0:
            if role.get(ds) == "depth_copy_prepass": kind = 5
            elif len(rts) == 2 and fmts[1] == RGB10A2: kind = 3
            else: kind = 4
        if kind is not None and prev is not None:
            changed = (cur.view(np.uint64) != prev.view(np.uint64))[:, :w]
            cls[changed] = kind
            spans.append({"interval": iv["seq"], "class": CLASSES[kind], "draws": iv["draws"],
                          "pixels_changed": int(changed.sum())})
        prev = cur
    total = w * h
    counts = {c: int((cls == k).sum()) for k, c in enumerate(CLASSES)}
    real = counts["sky"] + counts["opaque"]
    return {
        "width": w, "height": h, "pixels": total,
        "classes": [{"class": c, "pixels": counts[c], "percent": round(100.0 * counts[c] / total, 4),
                     "real_in_game_buffers": REAL[c], "missing": MISSING[c]} for c in CLASSES],
        "surface_data_real": {"pixels": real, "percent": round(100.0 * real / total, 4)},
        "surface_data_missing": {"pixels": total - real, "percent": round(100.0 * (total - real) / total, 4)},
        "spans": spans, "opaque_end_interval": opaque_end,
        "method": "class of the last scene draw span that changed the pixel; spans from before/after copies of the scene colour",
    }, cls


if __name__ == "__main__":
    report, cls = analyse(Path(sys.argv[1]))
    for c in report["classes"]:
        print(f"{c['class']:17s} {c['pixels']:9d} px  {c['percent']:8.4f} %   real: {c['real_in_game_buffers']}")
    print(f"real surface data: {report['surface_data_real']['percent']} %, missing: {report['surface_data_missing']['percent']} %")
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(json.dumps(report, indent=1))
    if len(sys.argv) > 3:
        from PIL import Image
        palette = np.array([[70, 110, 170], [60, 160, 80], [230, 200, 40], [40, 200, 220], [220, 70, 160], [240, 120, 30]], np.uint8)
        im = Image.fromarray(palette[cls])
        im.thumbnail((1920, 1920))
        im.save(sys.argv[3])
