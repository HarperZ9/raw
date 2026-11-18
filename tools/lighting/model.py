"""The match model of shaders/Lighting.hlsl in numpy, run on a lighting dump against the game's
own lit buffer, term by term. It shows which input or term carries the difference, with
controls that must make the match worse.

usage: python model.py <dump base, e.g. E:/cap/rw4/riverwood_day> [stride]

Original work, MIT.
"""
from __future__ import annotations

import sys
from pathlib import Path

import numpy as np

from frame import lighting, texture


def lab(c: np.ndarray) -> np.ndarray:
    v = np.clip(c, 0, None)
    v = v / (1 + v)
    m = np.array([[0.4124, 0.3576, 0.1805], [0.2126, 0.7152, 0.0722], [0.0193, 0.1192, 0.9505]])
    xyz = v @ m.T / np.array([0.95047, 1.0, 1.08883])
    f = np.where(xyz > 0.008856, np.cbrt(xyz), 7.787 * xyz + 16 / 116)
    return np.stack([116 * f[..., 1] - 16, 500 * (f[..., 0] - f[..., 1]), 200 * (f[..., 1] - f[..., 2])], -1)


def score(ours: np.ndarray, game: np.ndarray) -> str:
    rel = np.abs(ours - game).sum() / np.abs(game).sum()
    de = np.linalg.norm(lab(ours) - lab(game), axis=-1)
    return f"rel {rel:.4f}  dE {de.mean():6.3f}  dE<2 {np.mean(de < 2):.3f}  dE<5 {np.mean(de < 5):.3f}"


class Scene:
    def __init__(self, base: Path, stride: int):
        s = slice(None, None, stride)
        self.L = lighting(base)
        self.game = texture(base, "color_in")[s, s, :3]
        self.ours_gpu = texture(base, "color_out")[s, s, :3]
        self.depth = texture(base, "depth")[s, s, 0]
        self.normal = texture(base, "normal")[s, s]
        self.albedo = texture(base, "albedo")[s, s, :3]
        self.spec = texture(base, "specular")[s, s]
        self.mask = texture(base, "shadow_mask")[s, s]
        self.atlas = texture(base, "shadow_atlas")[..., 0]
        h, w = self.depth.shape
        self.full = (w * stride, h * stride)
        ys, xs = np.mgrid[0:h, 0:w]
        self.px = np.stack([xs * stride, ys * stride], -1).astype(np.float64)
        cls = np.round(self.spec[..., 2] * 255).astype(int)
        self.cls = cls & 127
        self.sel = (self.cls > 0) & (self.cls < 7) & (cls & 128 == 0) & (self.depth < 1)

    def world(self) -> np.ndarray:
        W, H = self.full
        uv = (self.px + 0.5) / np.array([W, H])
        sx, sy = self.L["res_scale"]
        c = np.stack([uv[..., 0] * sx * 2 - 1, (1 - uv[..., 1] * sy) * 2 - 1, self.depth, np.ones_like(self.depth)], -1)
        p = c @ self.L["clip_to_world"].T
        return p[..., :3] / p[..., 3:4]

    def normals(self) -> np.ndarray:
        e = self.normal[..., :2] - 0.5
        s = np.minimum((e * e).sum(-1), 0.25)
        nv = np.concatenate([e * (4 * np.sqrt(1 - 4 * s))[..., None], (8 * s - 1)[..., None]], -1)
        n = nv @ self.L["view_to_world"][:3, :3].T
        return n / np.maximum(np.linalg.norm(n, axis=-1, keepdims=True), 1e-6)

    def cascade_shadow(self, p: np.ndarray) -> np.ndarray:
        L = self.L
        side = self.atlas.shape[0]
        k = ((L["cascades"] > 1) & (self.depth > L["split"])).astype(int)
        q = np.concatenate([p, np.ones_like(p[..., :1])], -1)
        lit = np.ones(p.shape[:2])
        for c in (0, 1):
            m = k == c
            sc = q[m] @ L["cascade"][c].T
            inside = (sc[:, 0] > 0) & (sc[:, 0] < 1) & (sc[:, 1] > 0) & (sc[:, 1] < 1)
            tx = np.clip((sc[:, 0] * side).astype(int), 0, side - 1) + c * side
            ty = np.clip((sc[:, 1] * side).astype(int), 0, side - 1)
            v = np.where(inside, (sc[:, 2] - L["bias"][c]) <= self.atlas[ty, tx], True)
            lit[m] = v
        return lit


def shade(sc: Scene, sun_mask: str = "cascades", spec_on: bool = True, normals: np.ndarray | None = None,
          ambient_on: bool = True, lights_on: bool = True, soft_k: float = 0.15, parallax_lobe: bool = False) -> np.ndarray:
    L = sc.L
    p = sc.world()
    n = sc.normals() if normals is None else normals
    v = -p / np.linalg.norm(p, axis=-1, keepdims=True)
    shin = np.exp2(sc.spec[..., 1] * 13)
    diff = np.zeros_like(p)
    spec = np.zeros_like(p)

    soft = np.where(sc.cls == 5, soft_k, 0.0)[..., None]
    ss = lambda x: x * x * (3 - 2 * x)

    def add(l, c, full):
        x = (n * l).sum(-1)
        nl = np.clip(x, 0, 1)
        diff[...] += soft * np.clip(ss(np.clip((x + 1) * 0.5, 0, 1)) - ss(nl), 0, 1)[..., None] * full
        h = v + l
        h /= np.maximum(np.linalg.norm(h, axis=-1, keepdims=True), 1e-9)
        nh = np.clip((n * h).sum(-1), 0, 1)
        diff[...] += nl[..., None] * c
        spec[...] += (nh ** shin)[..., None] * c

    sh = sc.cascade_shadow(p) if sun_mask == "cascades" else sc.mask[..., 0] if sun_mask == "mask" else np.ones(p.shape[:2])
    add(np.broadcast_to(L["sun_dir"], p.shape), L["sun_colour"] * sh[..., None], L["sun_colour"])
    if lights_on:
        for li in L["lights"]:
            d = li["pos"] - p
            dist = np.linalg.norm(d, axis=-1)
            att = 1 - np.clip(dist / li["radius"], 0, 1) ** 2
            full = li["colour"] * att[..., None]
            c = full * sc.mask[..., li["channel"]][..., None] if 0 <= li["channel"] <= 3 else full
            add(d / np.maximum(dist[..., None], 1e-4), c, full)
    amb = np.stack([(n * L["ambient"][k, :3]).sum(-1) + L["ambient"][k, 3] for k in range(3)], -1) if ambient_on else 0
    knee = lambda x: np.minimum(x, (1 - L["knee"]) * x + 1)
    lit = knee(sc.albedo * (diff + amb))
    if spec_on:
        lobe = (sc.spec[..., 1:2] > 0.002) & (sc.cls[..., None] != 4) & ((sc.cls[..., None] != 6) | parallax_lobe)
        lit = lit + spec * (sc.spec[..., :1] * L["spec_scale"]) * lobe
    return knee(lit)


def main() -> None:
    base = Path(sys.argv[1])
    sc = Scene(base, int(sys.argv[2]) if len(sys.argv) > 2 else 2)
    g = sc.game[sc.sel]
    print(f"pixels {sc.sel.sum()}, sun {sc.L['sun_dir'].round(3)} colour {sc.L['sun_colour'].round(3)}, lights {len(sc.L['lights'])}")
    print("GPU output             ", score(sc.ours_gpu[sc.sel], g))
    full = shade(sc)
    print("model, cascades        ", score(full[sc.sel], g))
    print("model, game sun mask   ", score(shade(sc, "mask")[sc.sel], g))
    print("model, no sun shadow   ", score(shade(sc, "none")[sc.sel], g))
    print("model, no specular     ", score(shade(sc, spec_on=False)[sc.sel], g))
    print("model, no point lights ", score(shade(sc, lights_on=False)[sc.sel], g))
    print("control: no ambient    ", score(shade(sc, ambient_on=False)[sc.sel], g))
    print("control: normals flipped", score(shade(sc, normals=-sc.normals())[sc.sel], g))
    ratio = (g.sum(-1) + 1e-4) / (full[sc.sel].sum(-1) + 1e-4)
    print("game / model luminance: median %.3f, quartiles %.3f %.3f" % tuple(np.percentile(ratio, [50, 25, 75])))
    n = sc.normals()[sc.sel]
    print("mean world normal", n.mean(0).round(3), " (ground-heavy frames lean to +z)")


if __name__ == "__main__":
    main()
