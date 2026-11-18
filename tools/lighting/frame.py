"""Reads a lighting dump ([Lighting] Dump=1): the shared textures of one frame and the decoded
SceneLighting, for the offline model check (model.py). Original work, MIT."""
from __future__ import annotations

import struct
from pathlib import Path

import numpy as np

FORMATS = {"color_in": ("f2", 4), "color_out": ("f2", 4), "depth": ("f4", 1), "normal": ("u1", 4), "motion": ("f2", 2),
           "albedo": ("u1", 4), "specular": ("u1", 4), "shadow_mask": ("u1", 4), "shadow_atlas": ("f4", 1)}


def texture(base: Path, name: str) -> np.ndarray:
    raw = (base.parent / f"{base.name}_{name}.bin").read_bytes()
    _, h, row = struct.unpack_from("<3I", raw)
    kind, ch = FORMATS[name]
    px = np.frombuffer(raw, dtype=np.uint8, offset=12).reshape(h, row)
    item = np.dtype(kind).itemsize * ch
    w = row // item
    a = px[:, : w * item].copy().view(kind).reshape(h, w, ch).astype(np.float32)
    return a / 255.0 if kind == "u1" else a


def lighting(base: Path) -> dict:
    b = (base.parent / f"{base.name}_lighting.bin").read_bytes()
    f = lambda off, n: np.frombuffer(b, dtype="<f4", count=n, offset=off).astype(np.float64)
    out = {"valid": b[0], "has_sun": b[1], "sun_dir": f(4, 3), "sun_colour": f(16, 3), "ambient": f(28, 12).reshape(3, 4),
           "knee": f(76, 1)[0], "spec_scale": f(80, 1)[0], "has_cascades": b[84], "cascades": struct.unpack_from("<I", b, 88)[0],
           "cascade": f(92, 24).reshape(2, 3, 4), "bias": f(188, 2), "split": f(196, 1)[0], "clip_to_world": f(204, 16).reshape(4, 4),
           "world_to_clip": f(268, 16).reshape(4, 4), "world_to_view": f(332, 16).reshape(4, 4), "view_to_world": f(396, 16).reshape(4, 4),
           "res_scale": f(460, 2)}
    n = struct.unpack_from("<I", b, 468)[0]
    lights = []
    for i in range(n):
        o = 472 + i * 36
        p, r, c = f(o, 3), f(o + 12, 1)[0], f(o + 16, 3)
        ch, draws = struct.unpack_from("<iI", b, o + 28)
        lights.append({"pos": p, "radius": r, "colour": c, "channel": ch, "draws": draws})
    out["lights"] = lights
    out["counters"] = struct.unpack_from(f"<{(len(b) - 5080) // 4}I", b, 5080)   # draws, sightings, over the limit[, copies]
    return out
