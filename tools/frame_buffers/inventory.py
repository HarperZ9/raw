"""The buffer table and one image per buffer, from a FrameLedger capture.

usage: python inventory.py <capture dir> <out dir>

Writes <out>/buffers.json, <out>/buffers.md (the table of docs/FRAME_BUFFERS.md) and a PNG of
every captured buffer. The images show game content: keep the out dir outside the repository.

Original work, MIT.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

import rawtex as R
import roles

PER_FRAME = {"depth_main": "every frame, as R32F depth", "normals_masks": "every frame", "backbuffer": "every frame"}
MAX_SIDE = 1920


def _gray(x: np.ndarray) -> np.ndarray:
    return np.repeat((np.clip(np.nan_to_num(x), 0, 1) * 255 + 0.5).astype(np.uint8)[..., None], 3, -1)


def views(t: R.RawTex, image: int, role: str) -> list[tuple[str, np.ndarray]]:
    """Display images of one stored image: (suffix, uint8 RGB)."""
    c = R.channels(t, image)
    f = t.format
    if role == "normals_masks" or role == "half_normals":
        n = decode_normal(c[..., :2])
        return [("encoded", (c[..., :3] * 255 + 0.5).astype(np.uint8)), ("normal", ((n * 0.5 + 0.5) * 255 + 0.5).astype(np.uint8)),
                ("alpha", _gray(c[..., 3])), ("blue_x8", _gray(c[..., 2] * 8))]
    if f in (R.R24G8_TYPELESS, R.D24_UNORM_S8_UINT):
        d = R.depth24_exact(t, image)
        return [("depth", _gray(np.power(d, 600.0))), ("stencil", _gray(c[..., 1] / max(float(c[..., 1].max()), 1.0)))]
    if f in (R.R16_TYPELESS, R.D16_UNORM, R.R16_UNORM):
        return [("depth", _gray(np.power(c[..., 0], 4.0)))]
    if f == R.R16G16_FLOAT:
        m = float(np.abs(c).max()) or 1.0
        rg = c / m * 0.5 + 0.5
        return [("xy", np.dstack([_gray(rg[..., 0])[..., 0], _gray(rg[..., 1])[..., 0], np.full(c.shape[:2], 128, np.uint8)]))]
    if f == R.R8G8_UNORM:
        return [("r", _gray(c[..., 0])), ("g", _gray(c[..., 1]))]
    if c.shape[-1] == 1:
        x = c[..., 0]
        fin = x[np.isfinite(x)]
        lo, hi = (float(fin.min()), float(fin.max())) if fin.size else (0.0, 1.0)
        return [("value", _gray((x - lo) / ((hi - lo) or 1.0)))]
    if f in (R.R16G16B16A16_FLOAT, R.R16G16B16A16_TYPELESS, R.R32G32B32A32_FLOAT, R.R11G11B10_FLOAT):
        out = [("rgb", R.tonemap(c[..., :3]))]
        if c.shape[-1] == 4 and float(c[..., 3].max()) > float(c[..., 3].min()):
            out.append(("alpha", _gray(c[..., 3])))
        return out
    out = [("rgb", (np.clip(c[..., :3], 0, 1) * 255 + 0.5).astype(np.uint8))]
    if c.shape[-1] == 4 and float(c[..., 3].max()) > float(c[..., 3].min()):
        out.append(("alpha", _gray(c[..., 3])))
    return out


def decode_normal(enc: np.ndarray) -> np.ndarray:
    """The inverse of the game's write, n.xy / sqrt(8 - 8 n.z) + 0.5, in the game's view space."""
    e = enc.astype(np.float64) - 0.5
    s = np.minimum((e * e).sum(-1), 0.25)
    k = 4.0 * np.sqrt(1.0 - 4.0 * s)
    return np.stack([e[..., 0] * k, e[..., 1] * k, 8.0 * s - 1.0], -1)


def _save(img: np.ndarray, path: Path) -> None:
    im = Image.fromarray(img)
    im.thumbnail((MAX_SIDE, MAX_SIDE))
    im.save(path)


def build(capture: Path, out: Path) -> list[dict]:
    manifest = json.loads((capture / "manifest.json").read_text())
    role = roles.assign(manifest)
    out.mkdir(parents=True, exist_ok=True)
    rows = []
    for t in manifest["textures"]:
        if not t["written"]:
            continue
        r = role.get(t["id"], "unassigned")
        title, holds = roles.ROLE_TEXT[r]
        slices = t["array"] if t["dim"] == 2 else 1
        images = []
        path = capture / f"t{t['id']:02d}_final.rawtex"
        if t["dumped"] and path.exists():
            tex = R.read(path)
            picks = range(tex.array) if tex.dimension == 2 else [tex.depth // 2]
            for i in picks:
                for suffix, img in views(tex, i, r):
                    name = f"{t['id']:02d}_{r}" + (f"_{i}" if tex.images > 1 else "") + f"_{suffix}.png"
                    _save(img, out / name)
                    images.append(name)
        if r in PER_FRAME:
            shared = PER_FRAME[r]
        elif t["dim"] == 3:
            shared = "not yet (3D)"
        elif t["sharedEqual"] == slices and not t["sharedDifferent"] and not t["sharedFailed"]:
            shared = "on demand; D3D12 read equals D3D11" + (" (as R32F depth)" if t["format"] == R.R24G8_TYPELESS else "")
        elif t["sharedEqual"] or t["sharedDifferent"] or t["sharedFailed"]:
            shared = f"on demand; {t['sharedEqual']} of {slices} slices equal, {t['sharedDifferent']} differ, {t['sharedFailed']} failed"
        else:
            shared = "not checked in this capture"
        size = f"{t['w']}x{t['h']}" + (f"x{t['depth']}" if t["dim"] == 3 else "") + (f", {t['array']} layers" if t["array"] > 1 else "") + (f", {t['mips']} mips" if t["mips"] > 1 else "")
        how = []
        if t["rtIntervals"] or t["dsIntervals"]: how.append(f"{t['draws']} draws")
        if t["uavWrites"]: how.append(f"{t['uavWrites']} dispatches")
        if t["copiesIn"]: how.append("copy")
        rows.append({"id": t["id"], "role": r, "title": title, "holds": holds, "size": size,
                     "format": t["formatName"] or str(t["format"]), "written_by": ", ".join(how),
                     "first_write": roles._first_write(manifest, t["id"]), "captured": bool(t["dumped"]),
                     "shared": shared, "images": images})
    rows.sort(key=lambda x: (x["first_write"], x["id"]))
    (out / "buffers.json").write_text(json.dumps(rows, indent=1))
    lines = ["| # | Buffer | Size | Format | Written by | Captured | Shared with D3D12 | What it holds |", "|---|---|---|---|---|---|---|---|"]
    for n, x in enumerate(rows, 1):
        lines.append(f"| {n} | {x['title']} (t{x['id']}) | {x['size']} | {x['format']} | {x['written_by']} | "
                     f"{'yes' if x['captured'] else 'no'} | {x['shared']} | {x['holds']} |")
    (out / "buffers.md").write_text("\n".join(lines) + "\n")
    return rows


if __name__ == "__main__":
    rows = build(Path(sys.argv[1]), Path(sys.argv[2]))
    captured = sum(x["captured"] for x in rows)
    equal = sum("equals" in x["shared"] or "every frame" in x["shared"] for x in rows)
    print(f"{len(rows)} buffers written in the frame, {captured} captured, {equal} shared with D3D12 and verified")
