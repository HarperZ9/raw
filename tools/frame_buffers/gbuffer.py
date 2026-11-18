"""RAW's own G-buffer targets in a capture: decode checks, coverage after the shader patches,
and one picture per channel.

usage: python gbuffer.py <capture dir> [<out json> [<picture dir>]]

The capture comes from a proxy with [GBuffer] Enabled=1 (docs/FRAME_BUFFERS.md, "Shader
patches"). With History=2 the ledger keeps the HDR colour after every span, and the coverage
report gives every pixel the class of the last scene span that changed it, as coverage.py does.
Without history only the decode checks and the channel statistics run.

A pixel counts as described when the buffers hold its own surface:
  opaque        the game's normal is written and RAW's class is set (albedo, specular, emissive)
  first person  the same, with the first-person flag in the class
  water         the layer holds the water surface: depth, a normal, covered
  blended       the layer holds the nearest blended surface that shows: depth, and its normal
                or, for a shader with no normal, its alpha
  sky           nothing to describe

Original work, MIT.
"""
from __future__ import annotations

import csv
import json
import sys
from pathlib import Path

import numpy as np

import rawtex as R
import roles
from lights import draw_constants, vec4

CLASS_NAMES = {0: "none", 1: "lit", 2: "environment-mapped", 3: "glow-mapped", 4: "landscape", 5: "soft-lit",
               6: "parallax", 7: "distant", 8: "effect", 9: "water"}
PALETTE = np.array([[0, 0, 0], [150, 150, 150], [60, 140, 255], [255, 160, 0], [110, 80, 40], [90, 200, 90],
                    [200, 60, 200], [40, 90, 40], [255, 255, 0], [0, 80, 255]], dtype=np.uint8)


def final(capture: Path, tex: int) -> np.ndarray:
    return R.channels(R.read(capture / f"t{tex:02d}_final.rawtex"))


def decode_normal(enc: np.ndarray) -> np.ndarray:
    """The game's two-channel view normal: enc = n.xy / sqrt(8 - 8 n.z) + 0.5."""
    e = enc.astype(np.float64) - 0.5
    s = (e * e).sum(-1)
    return np.concatenate([4 * e * np.sqrt(np.clip(1 - 4 * s, 0, None))[..., None], (8 * s - 1)[..., None]], -1)


def span_kind(iv: dict, by: dict) -> str | None:
    rts = [r["tex"] for r in iv["rt"]]
    if iv["ds"]["tex"] < 0 or not rts or rts[0] != by["scene_hdr"]: return None
    if rts[:4] == [by["scene_hdr"], by["motion_vectors"], by["normals_masks"], by.get("snow_mask")]: return "opaque"
    if by.get("water_normals_mask") in rts: return "water"
    if by.get("blend_colour") in rts: return "blended"
    if rts[:2] == [by["scene_hdr"], by["motion_vectors"]] and iv["ds"]["tex"] != by["depth_main"]: return "first_person"
    return None


def class_map(capture: Path, m: dict, by: dict) -> np.ndarray | None:
    """Per pixel, the kind of the last scene span that changed its colour (History=2)."""
    scene = by["scene_hdr"]
    w, h = m["backbuffer"]
    kinds = ["sky", "opaque", "water", "blended", "first_person"]
    out = np.zeros((h, w), np.uint8)
    prev, seen = None, False
    for iv in m["intervals"]:
        if not iv["draws"] or scene not in iv.get("history", []): continue
        cur = R.read(capture / f"t{scene:02d}_i{iv['seq']:03d}.rawtex").data[0]
        kind = span_kind(iv, by)
        if kind and prev is not None:
            changed = (cur != prev).reshape(h, -1, 8)[:, :w].any(-1)
            out[changed] = kinds.index(kind)
            seen = True
        elif kind == "opaque" and prev is None:
            changed = (cur.reshape(h, -1, 8)[:, :w] != 0).any(-1)
            out[changed] = 1
        prev = cur
    return out if seen else None


def run(capture: Path, pictures: Path | None = None) -> dict:
    m = json.loads((capture / "manifest.json").read_text())
    by = {v: k for k, v in roles.assign(m).items()}
    w, h = m["backbuffer"]
    albedo, spec, emis, layer = (final(capture, by[k]) for k in ("raw_albedo", "raw_specular", "raw_emissive", "raw_layer"))
    normals = final(capture, by["normals_masks"])
    depth = R.depth24_exact(R.read(capture / f"t{by['depth_main']:02d}_final.rawtex"))
    cls = np.round(spec[..., 2] * 255).astype(np.int32)
    first_person, base = cls >= 128, cls & 127
    has_normal = (np.round(normals * 255)[..., :2] != 127).any(-1)
    covered = layer[..., 0] < 1.0                           # the layer is cleared to depth 1
    layer_normal = covered & (layer[..., 2] >= 0)           # b is -1 where the shader has no normal
    checks = []

    def check(name, ok, **facts): checks.append({"check": name, "pass": bool(ok), **facts})

    # Material class and albedo over drawn pixels.
    drawn = depth < 1.0
    with_class = drawn & (base > 0)
    hist = {CLASS_NAMES.get(int(k), str(k)): round(float(v) / cls.size * 100, 3) for k, v in zip(*np.unique(base, return_counts=True))}
    check("every pixel with opaque depth and a game normal carries a material class",
          (with_class & has_normal).sum() >= 0.995 * (drawn & has_normal).sum(),
          pixels=int((drawn & has_normal).sum()), with_class=float((with_class & has_normal).sum() / max(1, (drawn & has_normal).sum())))
    check("albedo is written wherever a class is", (albedo[..., :3].sum(-1) > 0)[with_class].mean() > 0.99,
          share=float((albedo[..., :3].sum(-1) > 0)[with_class].mean()))
    if (~drawn & ~covered).any():
        check("control: sky pixels carry no class", (base[~drawn & ~covered] == 0).mean() > 0.999,
              share_without=float((base[~drawn & ~covered] == 0).mean()))

    # The layer lies at or in front of the scene depth (its draws pass the depth test).
    if covered.any():
        front = (layer[..., 0][covered] <= depth[covered] + 2e-7).mean()
        behind = (layer[..., 0][covered] >= depth[covered] + 2e-7).mean()
        check("layer depth is at or in front of the scene depth", front > 0.97, pixels=int(covered.sum()), share=float(front))
        check("control: the opposite order fails", behind < 0.5, share=float(behind))

    # Water normals: decoded, they point along view-space up (the game's world up through the view rotation).
    cbs = draw_constants(capture)
    # The per-frame constants of the main view: pixel slot 12 of a draw of the opaque set.
    opaque_seq = next((iv["seq"] for iv in m["intervals"] if iv["draws"] and span_kind(iv, by) == "opaque"), -1)
    with open(capture / "draws.csv", newline="") as f:
        main = [int(r["draw"]) for r in csv.DictReader(f) if int(r["interval"]) == opaque_seq]
    frame = next((vec4(cbs[d][7]) for d in main if d in cbs and len(cbs[d][7]) >= 720), None)
    water = layer_normal
    if frame is not None and water.sum() > 1000:
        n = decode_normal(layer[..., 1:3][water])
        up = frame[0:3, :3] @ np.array([0.0, 0.0, 1.0])
        dots = n @ up
        swapped = n[:, [1, 0, 2]] @ up
        check("layer normals of water point along the world's up axis in view space (mean dot over 0.9)", dots.mean() > 0.9,
              pixels=int(water.sum()), mean_dot=float(dots.mean()), unit_length_error=float(np.abs((n * n).sum(-1) - 1).max()))
        check("control: x and y swapped", swapped.mean() < dots.mean() - 0.2, mean_dot=float(swapped.mean()))

    report = {"capture": capture.name, "size": [w, h], "class_share_percent": hist,
              "first_person_percent": round(float(first_person.mean() * 100), 3),
              "first_person_with_normal": float(has_normal[first_person].mean()) if first_person.any() else None,
              "emissive_nonzero_percent": round(float((emis.sum(-1) > 0).mean() * 100), 4), "emissive_max": float(emis.max()),
              "gloss_set_percent": round(float((spec[..., 1] > 0).mean() * 100), 3),
              "layer_covered_percent": round(float(covered.mean() * 100), 3),
              "layer_with_normal_percent": round(float(layer_normal.mean() * 100), 3), "checks": checks}

    kinds = class_map(capture, m, by)
    if kinds is not None:
        sky = (kinds <= 1) & ~drawn
        rows, real_total = [], 0
        def row(name, sel, real, what):
            nonlocal real_total
            n, r = int(sel.sum()), int((sel & real).sum())
            real_total += r
            rows.append({"class": name, "pixels": n, "percent": round(n / kinds.size * 100, 3), "described": r,
                         "described_percent_of_class": round(r / n * 100, 2) if n else None, "by": what})
        opaque = (kinds <= 1) & drawn
        row("sky", sky, np.ones_like(sky), "nothing to describe")
        row("opaque", opaque, has_normal & (base > 0), "game depth, normal, motion; RAW albedo, specular, gloss, emissive, class")
        row("water", kinds == 2, layer_normal, "RAW layer: depth, normal")
        row("blended", kinds == 3, covered, "RAW layer: depth of the nearest blended surface that shows, its normal or its alpha")
        row("first person", kinds == 4, has_normal & first_person, "game normal bound by RAW, class with the first-person flag, depth in the prepass copy")
        report["coverage"] = {"classes": rows, "described_percent": round(real_total / kinds.size * 100, 2),
                              "missing_percent": round(100 - real_total / kinds.size * 100, 2)}
    if pictures:
        from PIL import Image
        pictures.mkdir(parents=True, exist_ok=True)
        def save(a, name): Image.fromarray((np.clip(a, 0, 1) * 255).astype(np.uint8)).save(pictures / name)
        save(albedo[..., :3], "raw_albedo.png")
        save(spec[..., 0], "raw_specular_mask.png"); save(spec[..., 1], "raw_gloss.png")
        img = PALETTE[np.clip(base, 0, 9)]
        img[first_person] = img[first_person] // 2 + np.array([127, 0, 0], dtype=np.uint8)
        Image.fromarray(img).save(pictures / "raw_material_class.png")
        save(emis / max(float(emis.max()), 1e-6), "raw_emissive.png")
        save(np.where(covered, 1 - np.clip((1 - layer[..., 0]) * 500, 0, 1) * 0.8, 0), "raw_layer_depth.png")
        save(np.stack([layer[..., 1], np.clip(layer[..., 2], 0, 1), covered * 1.0], -1), "raw_layer_normal.png")
        save(normals[..., :3], "game_normals_with_first_person.png")
    return report


if __name__ == "__main__":
    rep = run(Path(sys.argv[1]), Path(sys.argv[3]) if len(sys.argv) > 3 else None)
    for k, v in rep.items():
        if k not in ("checks", "coverage"): print(k, v)
    for c in rep["checks"]:
        print("PASS" if c["pass"] else "FAIL", {k: (round(v, 5) if isinstance(v, float) else v) for k, v in c.items() if k != "pass"})
    for r in rep.get("coverage", {}).get("classes", []): print(r)
    if "coverage" in rep: print("described", rep["coverage"]["described_percent"], "% missing", rep["coverage"]["missing_percent"], "%")
    if len(sys.argv) > 2: Path(sys.argv[2]).write_text(json.dumps(rep, indent=1))
    sys.exit(0 if all(c["pass"] for c in rep["checks"]) else 1)
