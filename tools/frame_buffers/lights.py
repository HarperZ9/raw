"""The frame's lights and shadow maps, decoded from the per-draw constants of a capture.

usage: python lights.py <capture dir> [<out json> [<overlay png>]]

What the game hands its shaders (read from the disassembly of its own lit shader and its
shadow-mask shader):

  lit draws, pixel buffer 2 (30 vectors)
      [0] sun direction, [1] sun colour, both in the draw's model space
      [29].x number of point lights (at most 7)
      [15 + i] light position (xyz, model space) and radius (w); [22 + i] light colour
  lit draws, vertex buffer 2
      rows 0 to 2: the draw's world matrix (3 x 4, translation relative to the camera)
  shadow-mask draw, pixel buffer 2 (25 vectors)
      rows 16 to 18 and 19 to 21: one 3 x 4 matrix per cascade, camera-relative world position
      to shadow-map (u, v, depth); [2].y and [2].z: the depth bias of each cascade
  shadow-mask draw, pixel buffer 0
      [2].x: the scene depth where cascade 1 takes over

Checks, each with a control:
  lights   the same light seen from many draws lands on one world position after the world
           matrix (spread under 2 units); without the world matrix the spread is large
  shadows  a one-tap shadow test with the decoded matrices against the cascade depth maps
           agrees with the game's own shadow mask where that mask is fully lit or fully dark;
           with the two cascade matrices swapped the agreement falls

Original work, MIT.
"""
from __future__ import annotations

import csv
import json
import struct
import sys
from pathlib import Path

import numpy as np

import rawtex as R
import roles

LIT_PS2, LIT_VS2, MASK_PS2 = 480, 240, 400


def draw_constants(capture: Path) -> dict[int, list[bytes]]:
    """draw -> eight blobs: vertex slots 0, 1, 2, 12, then pixel slots 0, 1, 2, 12."""
    blob = (capture / "drawcb.bin").read_bytes()
    off, out = 0, {}
    while off < len(blob):
        draw, = struct.unpack_from("<I", blob, off); off += 4
        cbs = []
        for _ in range(8):
            n, = struct.unpack_from("<H", blob, off); off += 2
            cbs.append(blob[off:off + n]); off += n
        out[draw] = cbs
    return out


def vec4(b: bytes) -> np.ndarray:
    return np.frombuffer(b, dtype=np.float32).astype(np.float64).reshape(-1, 4)


def point_lights(cbs: dict[int, list[bytes]], use_world: bool = True) -> tuple[list[dict], dict]:
    """Unique point lights, and the sun, from every lit draw of the frame."""
    seen: dict[tuple, list[np.ndarray]] = {}
    sun_dirs, sun_colours, lit_draws = [], [], 0
    for c in cbs.values():
        if len(c[6]) != LIT_PS2 or len(c[2]) != LIT_VS2:
            continue
        g, world = vec4(c[6]), vec4(c[2])[:3]
        rot, move = world[:, :3], world[:, 3]
        scale = float(np.linalg.norm(rot[:, 0])) if use_world else 1.0     # a scaled object carries its lights scaled
        lit_draws += 1
        if np.abs(g[0, :3]).sum() > 0 and scale > 0:
            d = rot @ g[0, :3] if use_world else g[0, :3]
            sun_dirs.append(d / np.linalg.norm(d)); sun_colours.append(g[1, :3])
        for i in range(int(min(g[29, 0], 7))):
            p = g[15 + i, :3]
            pos = rot @ p + move if use_world else p
            key = (round(float(g[15 + i, 3]) * scale, 0),) + tuple(float(x) for x in np.round(g[22 + i, :3], 3))
            seen.setdefault(key, []).append(pos)
    lights = []
    for (radius, *colour), pts in seen.items():
        pts = np.array(pts)
        # One (radius, colour) can be several lamps: split by position, 64 units apart.
        groups: list[list[np.ndarray]] = []
        for p in pts:
            for grp in groups:
                if np.linalg.norm(grp[0] - p) < 64: grp.append(p); break
            else: groups.append([p])
        for grp in groups:
            a = np.array(grp)
            lights.append({"position": a.mean(0).round(2).tolist(), "radius": radius, "colour": colour,
                           "draws": len(grp), "spread": float(np.linalg.norm(a - a.mean(0), axis=1).max())})
    sun = {"lit_draws": lit_draws}
    if sun_dirs:
        d = np.array(sun_dirs)
        mid = np.median(d, axis=0)
        sun.update(direction=mid.round(4).tolist(), draws_within_0_01=float((np.abs(d - mid).max(1) < 0.01).mean()),
                   colour=np.array(sun_colours).mean(0).round(4).tolist())
    return sorted(lights, key=lambda l: -l["draws"]), sun


def shadow_setup(capture: Path, m: dict, by: dict, cbs) -> dict | None:
    if "shadow_mask" not in by: return None
    iv = next((i for i in m["intervals"] if i["draws"] and [r["tex"] for r in i["rt"]] == [by["shadow_mask"]]), None)
    if iv is None: return None
    with open(capture / "draws.csv", newline="") as f:
        row = next(r for r in csv.DictReader(f) if int(r["interval"]) == iv["seq"])
    c = cbs[int(row["draw"])]
    if len(c[6]) != MASK_PS2 or len(c[7]) < 720: return None
    g, k0, frame = vec4(c[6]), vec4(c[4]), vec4(c[7])
    return {"matrices": [g[16:19], g[19:22]], "bias": [g[2, 1], g[2, 2]], "split_depth": k0[2, 0],
            "cascades": int(round(k0[2, 3])), "clip_to_world": frame[32:36], "scale": frame[44, :2]}


def shadow_check(capture: Path, m: dict, by: dict, s: dict, swap: bool = False, step: int = 4) -> dict:
    """Predicted lit or dark for every step-th pixel, against the game's shadow mask."""
    w, h = m["backbuffer"]
    depth = R.depth24_exact(R.read(capture / f"t{by['depth_main']:02d}_final.rawtex"))[::step, ::step]
    mask = R.channels(R.read(capture / f"t{by['shadow_mask']:02d}_final.rawtex"))[::step, ::step, 0]
    maps = R.read(capture / f"t{by['shadow_cascades']:02d}_final.rawtex")
    ys, xs = np.mgrid[0:h:step, 0:w:step]
    u, v = (xs + 0.5) / w, (ys + 0.5) / h
    clip = np.stack([u * s["scale"][0] * 2 - 1, (1 - v * s["scale"][1]) * 2 - 1, depth, np.ones_like(depth)], -1)
    world = clip @ s["clip_to_world"].T
    world = world / world[..., 3:4]
    second = depth > s["split_depth"]
    mats = s["matrices"][::-1] if swap else s["matrices"]
    lit = np.zeros(depth.shape, bool); valid = np.zeros(depth.shape, bool)
    for k in (0, 1):
        sel = second == bool(k)
        sc = world[sel] @ mats[k].T
        layer = R.channels(maps, k)[..., 0]
        size = layer.shape[0]
        px = np.clip((sc[:, 0] * size).astype(int), 0, size - 1); py = np.clip((sc[:, 1] * size).astype(int), 0, size - 1)
        inside = (sc[:, 0] > 0) & (sc[:, 0] < 1) & (sc[:, 1] > 0) & (sc[:, 1] < 1)
        lit[sel] = (sc[:, 2] - s["bias"][k]) <= layer[py, px]
        valid[sel] = inside
    sure = valid & (depth < 0.9999) & ((mask == 0) | (mask == 1))
    agree = (lit == (mask == 1))[sure]
    return {"pixels": int(sure.sum()), "agree": float(agree.mean()) if agree.size else 0.0,
            "game_lit_share": float((mask == 1)[sure].mean()) if sure.any() else 0.0}


def overlay(capture: Path, m: dict, by: dict, lights: list[dict], cbs, out: Path) -> int:
    """The frame with a ring at every light that projects inside it. Returns how many did."""
    from PIL import Image, ImageDraw
    w, h = m["backbuffer"]
    opaque = next(iv["seq"] for iv in m["intervals"] if iv["draws"] and len(iv["rt"]) >= 4 and iv["rt"][0]["tex"] == by["scene_hdr"])
    with open(capture / "draws.csv", newline="") as f:
        main = [int(r["draw"]) for r in csv.DictReader(f) if int(r["interval"]) == opaque]
    frame = next(vec4(cbs[d][7]) for d in main if d in cbs and len(cbs[d][7]) >= 720)
    to_clip = frame[12:16]                                   # world to clip, unjittered
    img = Image.fromarray((R.channels(R.read(capture / f"t{by['backbuffer']:02d}_final.rawtex"))[..., :3] * 255).astype(np.uint8))
    d = ImageDraw.Draw(img)
    shown = 0
    for l in lights:
        c = to_clip @ np.array(l["position"] + [1.0])
        if c[3] <= 0: continue
        x, y = (c[0] / c[3] * 0.5 + 0.5) * w, (1 - (c[1] / c[3] * 0.5 + 0.5)) * h
        if not (0 <= x < w and 0 <= y < h): continue
        r = max(12.0, l["radius"] / c[3] * h * 0.5 * abs(to_clip[1, :3]).max() * 0.15)
        col = tuple(int(255 * min(1.0, v / max(l["colour"]))) for v in l["colour"])
        d.ellipse([x - r, y - r, x + r, y + r], outline=col, width=6)
        d.line([x - 24, y, x + 24, y], fill=(255, 255, 255), width=4); d.line([x, y - 24, x, y + 24], fill=(255, 255, 255), width=4)
        shown += 1
    img.save(out)
    return shown


def run(capture: Path, png: Path | None = None) -> dict:
    m = json.loads((capture / "manifest.json").read_text())
    by = {v: k for k, v in roles.assign(m).items()}
    cbs = draw_constants(capture)
    lights, sun = point_lights(cbs)
    raw_lights, _ = point_lights(cbs, use_world=False)
    many = [l for l in lights if l["draws"] >= 10]
    report = {"sun": sun, "point_lights": lights, "checks": []}
    report["checks"].append({"check": "one light seen from ten or more draws lands on one world position (spread under 2 units)",
                             "lights_seen_by_10_or_more_draws": len(many), "worst_spread": max((l["spread"] for l in many), default=0.0),
                             "pass": all(l["spread"] < 2 for l in many)})
    report["checks"].append({"check": "control: without the draw's world matrix the same lights do not agree",
                             "groups": len(raw_lights), "lights": len(lights), "pass": len(raw_lights) > len(lights) or not many})
    s = shadow_setup(capture, m, by, cbs)
    if s and "shadow_cascades" in by:
        good, bad = shadow_check(capture, m, by, s), shadow_check(capture, m, by, s, swap=True)
        report["shadows"] = {"cascades": s["cascades"], "split_depth": float(s["split_depth"]), "bias": [float(b) for b in s["bias"]],
                             "world_to_shadow": [mm.round(8).tolist() for mm in s["matrices"]]}
        report["checks"].append({"check": "a one-tap test with the decoded cascade matrices agrees with the game's shadow mask",
                                 **good, "pass": good["agree"] > 0.97})
        report["checks"].append({"check": "control: the two cascade matrices swapped", **bad, "pass": bad["agree"] < good["agree"] - 0.05})
    if png:
        report["lights_drawn_in_frame"] = overlay(capture, m, by, lights, cbs, png)
    return report


if __name__ == "__main__":
    cap = Path(sys.argv[1])
    rep = run(cap, Path(sys.argv[3]) if len(sys.argv) > 3 else None)
    print("sun:", rep["sun"])
    for l in rep["point_lights"][:12]:
        print(f"  light at {l['position']}, radius {l['radius']}, colour {l['colour']}, {l['draws']} draws, spread {l['spread']:.3f}")
    print(len(rep["point_lights"]), "point lights")
    for c in rep["checks"]:
        print("PASS" if c["pass"] else "FAIL", {k: v for k, v in c.items() if k != "pass"})
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(json.dumps(rep, indent=1))
    sys.exit(0 if all(c["pass"] for c in rep["checks"]) else 1)
