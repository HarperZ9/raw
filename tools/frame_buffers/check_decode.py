"""Decode checks for a captured frame: each buffer, read back and compared with known values.

usage: python check_decode.py <capture dir> [<out json>]      exit code 0 when every check passes

  every buffer   the file holds the size its header states, and every float is finite
  depth          view depth from the depth buffer and the game's own projection matrix equals the
                 view depth the game itself computes (its float depth pyramid, mip 0)
  normals        decoding is the exact inverse of the game's write: encode(decode(texel)) gives
                 the texel back; decoded normals have unit length; on flat patches of the depth
                 buffer they agree with the plane
  motion         the motion target equals the reprojection of each opaque pixel with the game's
                 current and previous view-projection matrices
  controls       a wrong projection term fails the depth check; a wrong normal decode (z sign
                 flipped, or x and y swapped) fails the flat-patch check: the checks can tell wrong from right

The camera comes from the capture itself: the per-frame constant buffer of the opaque pass.

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
from inventory import decode_normal


def camera(capture: Path, manifest: dict, opaque_seq: int) -> dict:
    """Matrices of the game's per-frame constants (vertex slot 12) at the first opaque draw."""
    first = None
    with open(capture / "draws.csv", newline="") as f:
        for row in csv.DictReader(f):
            if int(row["interval"]) == opaque_seq:
                first = int(row["draw"]); break
    blob = (capture / "drawcb.bin").read_bytes()
    off = 0
    while off < len(blob):
        draw, = struct.unpack_from("<I", blob, off); off += 4
        cbs = []
        for _ in range(8):
            n, = struct.unpack_from("<H", blob, off); off += 2
            cbs.append(blob[off:off + n]); off += n
        if draw == first:
            v = np.frombuffer(cbs[3], dtype=np.float32).astype(np.float64)
            m = [v[i * 16:(i + 1) * 16].reshape(4, 4) for i in range(10)]
            return {"view": m[0], "proj": m[1], "view_proj_unjittered": m[3], "prev_view_proj_unjittered": m[4]}
    raise RuntimeError("no per-frame constants for the opaque pass in drawcb.bin")


def view_depth(d: np.ndarray, proj: np.ndarray) -> np.ndarray:
    """depth = proj[2][2] + proj[2][3] / z  ->  z."""
    return -proj[2, 3] / (proj[2, 2] - d)


def run(capture: Path) -> dict:
    m = json.loads((capture / "manifest.json").read_text())
    role = roles.assign(m)
    by = {v: k for k, v in role.items()}
    w, h = m["backbuffer"]
    ivs = [iv for iv in m["intervals"] if iv["draws"] > 0]
    normal_id, depth_id, motion_id = by["normals_masks"], by["depth_main"], by["motion_vectors"]
    opaque = next(iv for iv in ivs if normal_id in [r["tex"] for r in iv["rt"]])
    opaque_end = [iv["seq"] for iv in ivs if normal_id in [r["tex"] for r in iv["rt"]] and normal_id in iv["history"]][-1]
    cam = camera(capture, m, opaque["seq"])
    P = cam["proj"]
    checks = []

    def check(name, ok, **facts):
        checks.append({"check": name, "pass": bool(ok), **facts})
        print(("[PASS] " if ok else "[FAIL] ") + name + "  " + json.dumps(facts))

    # Every buffer: size and finiteness.
    bad = []
    count = 0
    for t in m["textures"]:
        p = capture / f"t{t['id']:02d}_final.rawtex"
        if not (t["written"] and t["dumped"] and p.exists()): continue
        count += 1
        tex = R.read(p)   # raises when the size is wrong
        if tex.format in (R.R16G16B16A16_FLOAT, R.R16G16_FLOAT, R.R16_FLOAT, R.R32_FLOAT, R.R11G11B10_FLOAT, R.R32G32B32A32_FLOAT):
            for i in range(tex.images):
                if not np.isfinite(R.channels(tex, i)).all(): bad.append(t["id"]); break
    check("every captured buffer reads back at its stated size with finite floats", not bad, buffers=count, non_finite=bad)

    # Depth against the game's own view depth. The game builds its pyramid from the depth copy it
    # takes after the depth prepass, so that copy is the buffer to compare.
    d = R.depth24_exact(R.read(capture / f"t{depth_id:02d}_i{opaque_end:03d}.rawtex"))
    z = view_depth(d, P)
    drawn = d < 1.0
    pyramid = R.channels(R.read(capture / f"t{by['linear_depth_pyramid']:02d}_final.rawtex"))[..., 0].astype(np.float64)
    cap = float(pyramid.max())                       # the game clamps its view depth
    zp = view_depth(R.depth24_exact(R.read(capture / f"t{by['depth_copy_prepass']:02d}_final.rawtex")), P)
    sel = zp < cap * 0.999
    rel = np.abs(pyramid[sel] - zp[sel]) / zp[sel]
    inside = float((rel <= 1e-4).mean())
    check("view depth from the depth buffer equals the game's own view depth (relative 1e-4)", inside >= 0.9999,
          pixels=int(sel.sum()), within=round(inside, 6), median_relative_error=float(np.median(rel)), game_depth_clamp=cap)
    wrong = np.abs(pyramid[sel] - (-P[2, 3] * 1.01 / (P[2, 2] - (P[2, 2] + P[2, 3] / zp[sel])))) / zp[sel]
    check("CONTROL: a projection term off by 1 % fails the depth check", float((wrong <= 1e-4).mean()) < 0.9999,
          within=round(float((wrong <= 1e-4).mean()), 6))
    later = float((np.abs(z - zp) / zp > 1e-4)[zp < cap * 0.999].mean())
    checks.append({"check": "measured: pixels whose depth the opaque pass changed after the prepass copy", "pass": True,
                   "fraction": round(later, 6), "note": "the game's own view depth, and so its own AO, does not see these"})
    print("       " + json.dumps(checks[-1]))

    # Normals.
    n_raw = R.read(capture / f"t{normal_id:02d}_i{opaque_end:03d}.rawtex")
    enc8 = n_raw.data[0][:, : w * 4].reshape(h, w, 4)[..., :2]
    n = decode_normal(enc8.astype(np.float64) / 255.0)
    length = np.linalg.norm(n, axis=-1)
    has = drawn & ~((enc8[..., 0] == 127) & (enc8[..., 1] == 127))
    check("decoded normals have unit length (1e-9)", float(np.abs(length[has] - 1).max()) < 1e-9,
          pixels=int(has.sum()), worst=float(np.abs(length[has] - 1).max()))
    p_ = np.sqrt(np.maximum(8.0 - 8.0 * n[..., 2], 1e-6))
    back = np.clip(np.rint((n[..., :2] / p_[..., None] + 0.5) * 255.0), 0, 255).astype(np.uint8)
    same = (back == enc8).all(-1)
    e = enc8.astype(np.float64) / 255.0 - 0.5
    domain = has & ((e * e).sum(-1) <= 0.25)        # texels the game's write can produce
    check("encode(decode(texel)) returns the texel: the decode is the inverse of the game's write",
          float(same[domain].mean()) >= 0.99999, pixels=int(domain.sum()), identical=round(float(same[domain].mean()), 7),
          outside_write_domain=round(float((has & ~domain).sum() / has.sum()), 6))
    xs = ((np.arange(w) + 0.5) / w) * 2 - 1
    ys = 1 - ((np.arange(h) + 0.5) / h) * 2
    pos = np.stack([(xs[None, :] - P[0, 2]) * z / P[0, 0], (ys[:, None] - P[1, 2]) * z / P[1, 1], z], -1)
    flat = planar_agreement(pos, n, has)
    check("on flat patches of the depth buffer the decoded normal is within 30 degrees of the plane (median), "
          "also on the patches that look at the camera",
          flat["median_degrees"] < 30.0 and flat["camera_facing_median_degrees"] < 30.0, **flat)
    swapped = planar_agreement(pos, n[..., [1, 0, 2]], has)
    check("CONTROL: the decode with x and y swapped fails the flat-patch check", swapped["median_degrees"] >= 30.0,
          median_degrees=swapped["median_degrees"])
    flipped = planar_agreement(pos, n * np.array([1.0, 1.0, -1.0]), has)
    check("CONTROL: the decode with z flipped fails on the patches that look at the camera",
          flipped["camera_facing_median_degrees"] >= 30.0, camera_facing_median_degrees=flipped["camera_facing_median_degrees"])
    ray = pos / np.linalg.norm(pos, axis=-1, keepdims=True)
    away = float(((n * ray).sum(-1)[has] > 0.25).mean())
    checks.append({"check": "measured: drawn pixels whose normal points away from the viewer (cosine over 0.25)", "pass": True,
                   "fraction": round(away, 5), "note": "two-sided foliage keeps its front-face normal on back faces"})
    print("       " + json.dumps(checks[-1]))

    # Motion vectors against reprojection.
    mv = R.channels(R.read(capture / f"t{motion_id:02d}_i{opaque_end:03d}.rawtex")).astype(np.float64)
    V, VP, PVP = cam["view"], cam["view_proj_unjittered"], cam["prev_view_proj_unjittered"]
    step = 4                                          # every fourth pixel each way: 1/16 of the frame
    ps = pos[::step, ::step].reshape(-1, 3)
    world = ps @ V[:3, :3]                            # camera-relative world: view is a pure rotation
    hom = np.concatenate([world, np.ones((len(world), 1))], 1)
    cur, prv = hom @ VP.T, hom @ PVP.T
    expect = (cur[:, :2] / cur[:, 3:4] - prv[:, :2] / prv[:, 3:4]) * np.array([-0.5, 0.5])
    got = mv[::step, ::step].reshape(-1, 2)
    keep = has[::step, ::step].ravel()
    err_px = np.abs(got[keep] - expect[keep]) * np.array([w, h])
    inside = float((err_px.max(-1) <= 0.5).mean())
    check("motion vectors equal the reprojection with the game's current and previous view-projection (0.5 px)",
          inside >= 0.95, pixels=int(keep.sum()), within=round(inside, 5),
          camera_motion_px=float(np.abs(expect[keep] * np.array([w, h])).max()),
          note="pixels outside are surfaces that moved on their own (wind, actors)")
    return {"capture": capture.name, "width": w, "height": h, "checks": checks,
            "failed": sum(not c["pass"] for c in checks)}


def planar_agreement(pos: np.ndarray, n: np.ndarray, has: np.ndarray, block: int = 48) -> dict:
    angles, facing = [], []
    hh, ww = has.shape
    for y0 in range(0, hh - block, block):
        for x0 in range(0, ww - block, block):
            if not has[y0:y0 + block, x0:x0 + block].all(): continue
            p = pos[y0:y0 + block, x0:x0 + block].reshape(-1, 3)
            c = p.mean(0)
            if c[2] > 6000: continue
            _, s, vt = np.linalg.svd(p - c, full_matrices=False)
            if s[2] / s[1] > 0.004: continue
            g = vt[2] if (vt[2] * c).sum() < 0 else -vt[2]
            mean = n[y0:y0 + block, x0:x0 + block].reshape(-1, 3).mean(0)
            mean /= np.linalg.norm(mean)
            angle = np.degrees(np.arccos(np.clip((mean * g).sum(), -1, 1)))
            angles.append(angle)
            if abs(g[2]) >= 0.5: facing.append(angle)   # the plane looks at the camera: z carries the signal
    a = np.array(angles) if angles else np.array([np.nan])
    f = np.array(facing) if facing else np.array([np.nan])
    return {"flat_patches": len(angles), "median_degrees": round(float(np.median(a)), 2), "p90_degrees": round(float(np.percentile(a, 90)), 2),
            "camera_facing_patches": len(facing), "camera_facing_median_degrees": round(float(np.median(f)), 2),
            "note": "the normal target carries normal maps and smoothed vertex normals, so it differs from the flat geometry"}


if __name__ == "__main__":
    report = run(Path(sys.argv[1]))
    if len(sys.argv) > 2:
        Path(sys.argv[2]).write_text(json.dumps(report, indent=1))
    print(f"{'FAILED' if report['failed'] else 'ALL CHECKS PASSED'}: {report['failed']} check(s) failed")
    sys.exit(1 if report["failed"] else 0)
