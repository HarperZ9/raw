"""What the frame's pixel shaders write, read from their own bytecode signatures.

usage: python shaders.py <capture dir> [<out json>]

For each kind of draw span (opaque set, blended set, water, shadow, post) this lists the pixel
shaders the draws used, which SV_Target outputs each one writes, and how many draws and
shaders leave a bound target unwritten. That is the evidence for the shader-patch routes in
docs/FRAME_BUFFERS.md: a patch can only add an output to a shader that has the data.

Only the DXBC container's signature chunks are read (ISGN, OSGN and their newer forms).
Original work, MIT.
"""
from __future__ import annotations

import collections
import csv
import json
import struct
import sys
from pathlib import Path

import roles


def signatures(code: bytes) -> dict:
    """{'in': [(semantic, index, register)], 'out': [...]} from a DXBC blob."""
    if code[:4] != b"DXBC":
        return {"in": [], "out": []}
    count, = struct.unpack_from("<I", code, 28)
    out = {"in": [], "out": []}
    for k in range(count):
        off, = struct.unpack_from("<I", code, 32 + 4 * k)
        tag = code[off:off + 4]
        size, = struct.unpack_from("<I", code, off + 4)
        body = code[off + 8:off + 8 + size]
        key = {b"ISGN": "in", b"ISG1": "in", b"OSGN": "out", b"OSG5": "out", b"OSG1": "out"}.get(tag)
        if not key:
            continue
        n, = struct.unpack_from("<I", body, 0)
        lead = {b"OSG5": 4, b"ISG1": 4, b"OSG1": 4}.get(tag, 0)      # a stream field first
        stride = 24 + lead + (4 if tag in (b"ISG1", b"OSG1") else 0)  # and a precision field last
        for i in range(n):
            e = 8 + i * stride + lead
            name_off, index, _sysval, _ctype, register = struct.unpack_from("<5I", body, e)
            end = body.index(b"\0", name_off)
            out[key].append((body[name_off:end].decode("ascii", "replace"), index, register))
    return out


def analyse(capture: Path) -> dict:
    m = json.loads((capture / "manifest.json").read_text())
    role = roles.assign(m)
    tex = {t["id"]: t for t in m["textures"]}
    kind_of = {}
    for iv in m["intervals"]:
        if not iv["draws"]:
            continue
        names = [role.get(r["tex"], "?") for r in iv["rt"]]
        if names[:4] == ["scene_hdr", "motion_vectors", "normals_masks", "snow_mask"]: k = "opaque set (colour, motion, normals, mask)"
        elif names == ["normals_masks", "motion_vectors"]: k = "normals and motion only"
        elif names[:1] == ["scene_hdr"] and "blend_colour" in names: k = "blended set (colour, mask, blend colour)"
        elif names[:1] == ["scene_hdr"] and "water_normals_mask" in names: k = "water (colour, water mask)"
        elif names[:1] == ["scene_hdr"] and len(names) == 1 and iv["ds"]["tex"] >= 0: k = "scene colour only, with depth"
        elif names[:2] == ["scene_hdr", "motion_vectors"]: k = "late draws on the prepass depth copy (colour, motion)"
        elif names == ["water_target"]: k = "water colour target"
        elif names and names[0] == "reflection_cube": k = "reflection cube faces"
        elif not names: k = "depth only (prepass, shadow maps)"
        elif names[0] == "backbuffer" and iv["ds"]["tex"] >= 0: k = "HUD"
        else: k = "full-screen passes"
        kind_of[iv["seq"]] = (k, len(iv["rt"]))
    sig_cache = {}
    def sig(h):
        if h not in sig_cache:
            p = capture / f"ps_{h}.dxbc"
            sig_cache[h] = signatures(p.read_bytes()) if p.exists() else None
        return sig_cache[h]
    kinds = collections.OrderedDict()
    with open(capture / "draws.csv", newline="") as f:
        for row in csv.DictReader(f):
            k, bound = kind_of.get(int(row["interval"]), ("?", 0))
            e = kinds.setdefault(k, {"draws": 0, "blended_draws": 0, "depth_writing_draws": 0, "bound_targets": bound,
                                     "shaders": collections.Counter(), "draws_by_outputs": collections.Counter()})
            e["draws"] += 1
            e["blended_draws"] += row["blend"] == "1"
            e["depth_writing_draws"] += row["depth"] == "1" and row["depthWrite"] == "1"
            e["shaders"][row["ps"]] += 1
            s = sig(row["ps"])
            targets = sorted({i for name, i, _ in s["out"] if name.upper() == "SV_TARGET"}) if s else None
            e["draws_by_outputs"]["no bytecode" if targets is None else ",".join(map(str, targets)) or "none"] += 1
    report = []
    for k, e in kinds.items():
        report.append({"span": k, "bound_targets": e["bound_targets"], "draws": e["draws"], "blended_draws": e["blended_draws"],
                       "depth_writing_draws": e["depth_writing_draws"], "pixel_shaders": len(e["shaders"]),
                       "draws_by_sv_target_outputs": dict(e["draws_by_outputs"])})
    return {"spans": report}


if __name__ == "__main__":
    r = analyse(Path(sys.argv[1]))
    for s in r["spans"]:
        print(f"{s['span']:55s} bound {s['bound_targets']}  draws {s['draws']:5d} (blended {s['blended_draws']}, depth-writing "
              f"{s['depth_writing_draws']})  shaders {s['pixel_shaders']:3d}  outputs {s['draws_by_sv_target_outputs']}")
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(json.dumps(r, indent=1))
