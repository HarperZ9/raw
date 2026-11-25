"""Verification suite for RAW Wave 64 20-Heuristic Full-Pipeline Visual & Harness Inspector.

Verifies ALL 20 heuristics (#1 .. #20):
  1. Clean PASS (zero false positives) on synthetic clean controls (final RGB + debug views
     1, 2, 16, 28, 29, 30, 31, 32, 33, 34 + clean water + clean telemetry bundle).
  2. Planted-failure detection on REAL user baseline screenshots (all 8 baseline detectors +
     crushed black tree trunks on 09.31.46.94.png when present on disk).
  3. Deterministic planted-failure detection on ALL 12 new Wave 64 heuristics (#9 .. #20).
  4. End-to-end `full_pipeline_audit.py` offline multi-scene audit, bit-exact `SkyrimCustom.ini`
     save/restore guard, `playtest_tour.py` offline pipeline, `boot_sentinel.state` cleanup,
     and `tools/live/kinds.py` handler registration.

Original work, MIT.
"""
from __future__ import annotations

import ast
import glob
import json
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
LIVE_DIR = REPO_ROOT / "tools" / "live"
sys.path.insert(0, str(LIVE_DIR))

import full_pipeline_audit as fpa  # noqa: E402
import playtest_tour as pt  # noqa: E402
import visual_heuristics as vh  # noqa: E402

BASELINE_DIR = Path(r"C:\Users\Zain\Videos\NVIDIA\The Elder Scrolls V Skyrim Special Edition")


def _find_baseline(tag: str) -> Path | None:
    if not BASELINE_DIR.is_dir():
        return None
    matches = sorted(glob.glob(str(BASELINE_DIR / f"*{tag}*.png")))
    return Path(matches[0]) if matches else None


def _make_clean_exterior(h: int = 360, w: int = 640) -> np.ndarray:
    img = np.zeros((h, w, 3), dtype=np.float32)
    yy = np.linspace(0.0, 1.0, h, dtype=np.float32)[:, None]
    xx = np.linspace(0.0, 1.0, w, dtype=np.float32)[None, :]
    sky_h = int(h * 0.45)
    img[:sky_h, :, 0] = 0.35 + 0.25 * yy[:sky_h]
    img[:sky_h, :, 1] = 0.46 + 0.22 * yy[:sky_h]
    img[:sky_h, :, 2] = 0.64 + 0.16 * yy[:sky_h]
    img[sky_h:, :, 0] = 0.12 + 0.03 * np.sin(xx * 6.0)
    img[sky_h:, :, 1] = 0.20 + 0.03 * np.cos(yy[sky_h:] * 8.0)
    img[sky_h:, :, 2] = 0.26 + 0.04 * np.sin(xx * 4.0 + yy[sky_h:] * 5.0)
    return np.clip(img, 0.0, 1.0)


def _make_clean_interior_cave(h: int = 360, w: int = 640) -> np.ndarray:
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    yn, xn = yy / h, xx / w
    # Rich multi-scale glacial veining micro-detail (high variance, zero flat triangles)
    vein = 0.05 * np.sin(xn * 67.0 + yn * 43.0) + 0.03 * np.cos(xn * 113.0 - yn * 89.0)
    img = np.zeros((h, w, 3), dtype=np.float32)
    img[:, :, 0] = 0.16 + 0.05 * np.sin(xn * 5.0) + vein * 0.7
    img[:, :, 1] = 0.22 + 0.05 * np.cos(yn * 4.0) + vein * 0.9
    img[:, :, 2] = 0.30 + 0.06 * np.sin((xn + yn) * 4.0) + vein
    d2 = (xn - 0.50) ** 2 + (yn - 0.68) ** 2
    glow = np.exp(-d2 / 0.04) * 0.35
    img[:, :, 0] += glow * 1.1
    img[:, :, 1] += glow * 0.65
    img[:, :, 2] += glow * 0.25
    return np.clip(img, 0.0, 1.0)


def _make_clean_actor_portrait(h: int = 360, w: int = 640) -> np.ndarray:
    img = np.full((h, w, 3), [0.24, 0.23, 0.22], dtype=np.float32)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    mask = ((xx - w * 0.5) / (w * 0.18)) ** 2 + ((yy - h * 0.6) / (h * 0.22)) ** 2 <= 1.0
    img[mask] = [0.34, 0.25, 0.19]
    return img


def _make_clean_debug_normals(h: int = 180, w: int = 320) -> np.ndarray:
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    nx = np.sin(xx * 0.04) * 0.6
    ny = np.cos(yy * 0.05) * 0.5
    nz = np.sqrt(np.clip(1.0 - nx * nx - ny * ny, 0.05, 1.0))
    n = np.stack([nx, ny, nz], axis=2)
    n /= np.linalg.norm(n, axis=2, keepdims=True)
    return np.clip(n * 0.5 + 0.5, 0.0, 1.0)


def _make_clean_debug_albedo(h: int = 180, w: int = 320) -> np.ndarray:
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    img = np.zeros((h, w, 3), dtype=np.float32)
    img[:, :, 0] = 0.22 + 0.20 * (xx / w)
    img[:, :, 1] = 0.28 + 0.18 * (yy / h)
    img[:, :, 2] = 0.20 + 0.15 * np.sin(xx * 0.03)
    return np.clip(img, 0.05, 0.85)


def _make_clean_debug_shadow(h: int = 180, w: int = 320) -> np.ndarray:
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    s = np.where((xx + yy) > (w * 0.7), 0.92, 0.22).astype(np.float32)
    return np.stack([s, s, s], axis=2)


def _make_clean_debug_skin(h: int = 180, w: int = 320) -> np.ndarray:
    img = np.zeros((h, w, 3), dtype=np.float32)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    mask = ((xx - w * 0.5) ** 2 + (yy - h * 0.5) ** 2) < (45.0 ** 2)
    grad = np.clip(0.55 - 0.004 * (xx - w * 0.5), 0.20, 0.85)
    img[mask, 0] = grad[mask]
    img[mask, 1] = grad[mask] * 0.78
    img[mask, 2] = grad[mask] * 0.65
    return img


def _make_clean_debug_depth(h: int = 180, w: int = 320) -> np.ndarray:
    yy = np.linspace(0.95, 0.08, h, dtype=np.float32)[:, None]
    d = np.repeat(yy, w, axis=1)
    return np.stack([d, d, d], axis=2)


def _make_clean_debug34_flora(h: int = 180, w: int = 320) -> np.ndarray:
    img = np.full((h, w, 3), [0.15, 0.55, 0.20], dtype=np.float32)  # healthy green foliage
    img[:, :80] = [0.60, 0.42, 0.18]  # healthy warm amber bark
    return img


def run_tests() -> int:
    failures = 0

    def check(cond: bool, msg: str) -> None:
        nonlocal failures
        tag = "PASS" if cond else "FAIL"
        print(f"  [{tag}] {msg}")
        if not cond:
            failures += 1

    print("=== RAW Wave 65: 25-Heuristic Full-Pipeline Visual, Temporal & Harness Suite ===")
    check(len(vh.ALL_HEURISTIC_NAMES) == 25, f"ALL_HEURISTIC_NAMES has exact count 25 (got {len(vh.ALL_HEURISTIC_NAMES)})")
    check(all(k in vh.DEBUG_VIEW_NAMES for k in (0, 28, 33, 34, 35, 36)) and len(vh.WATER_DEBUG_VIEW_NAMES) == 7,
          "DEBUG_VIEW_NAMES (0..36) & WATER_DEBUG_VIEW_NAMES (1..7) registered")

    # -------------------------------------------------------------------------
    # 1. Clean Controls MUST PASS ALL 20 Heuristics (Zero False Positives)
    # -------------------------------------------------------------------------
    clean_ext = _make_clean_exterior()
    clean_cave = _make_clean_interior_cave()
    clean_actor = _make_clean_actor_portrait()
    rep_ext = vh.audit_image(clean_ext, label="clean_exterior", include_extended_rgb=True)
    rep_cave = vh.audit_image(clean_cave, label="clean_cave", include_extended_rgb=True)
    rep_actor = vh.audit_image(clean_actor, label="clean_actor", include_extended_rgb=True)
    r_pan = vh.detect_camera_turn_shadow_flicker(clean_ext, np.roll(clean_ext, shift=2, axis=1))

    check(rep_ext["passed"], f"Clean exterior control passes all 12 RGB heuristics (defects={rep_ext['defects']})")
    check(rep_cave["passed"], f"Clean veined cave control passes all 12 RGB heuristics (defects={rep_cave['defects']})")
    check(rep_actor["passed"], f"Clean actor control passes all 12 RGB heuristics (defects={rep_actor['defects']})")
    check(not r_pan.detected, f"Smooth camera pan passes flicker guard (shift={r_pan.score:.4f})")

    # Clean DebugView controls (#9..#19)
    dbg33_clean = np.full((180, 320, 3), [0.12, 0.20, 0.30], dtype=np.float32)
    dbg34_clean = _make_clean_debug34_flora()
    dbg1_clean = _make_clean_debug_normals()
    dbg16_clean = _make_clean_debug_albedo()
    dbg2_clean = _make_clean_debug_shadow()
    dbg32_clean = _make_clean_debug_skin()
    dbg29_clean = _make_clean_debug_depth()

    check(not vh.detect_nan_inf_hdr_overflow(dbg33_clean).detected, "Heuristic #9 clean DebugView 33 passes (0 magenta px)")
    check(not vh.detect_crushed_black_tree_trunks(clean_ext, debug34_image=dbg34_clean).detected, "Heuristic #10 clean DebugView 34 bark passes")
    check(not vh.detect_bleached_white_pine_needles(clean_ext, debug34_image=dbg34_clean).detected, "Heuristic #11 clean DebugView 34 foliage passes")
    check(not vh.detect_water_neon_cyan_oversat(clean_ext).detected, "Heuristic #12 clean Nordic water passes cyan guard")
    check(not vh.detect_flat_ice_cave_triangles(clean_cave).detected, "Heuristic #13 veined glacial ice cave passes flatness guard")
    check(not vh.detect_normal_map_integrity(dbg1_clean).detected, "Heuristic #14 clean DebugView 1 normals passes")
    check(not vh.detect_albedo_range_pbr_sanity(dbg16_clean).detected, "Heuristic #15 clean DebugView 16 PBR albedo passes")
    check(not vh.detect_shadow_coverage_balance(dbg2_clean).detected, "Heuristic #16 balanced DebugView 2 shadows passes")
    check(not vh.detect_skin_sss_sanity(dbg32_clean).detected, "Heuristic #17 smooth DebugView 32 skin SSS passes")
    check(not vh.detect_exposure_and_tonemap_clipping(clean_ext).detected, "Heuristic #18 balanced exposure passes")
    check(not vh.detect_depth_and_stencil_integrity(dbg29_clean).detected, "Heuristic #19 smooth DebugView 29 depth passes")

    # DebugView 28 + 31 overlap: clean vs planted leak
    dbg_cls = np.zeros((240, 320, 3), dtype=np.float32)
    dbg_cls[80:180, 100:220] = [0.95, 0.50, 0.10]  # base==14 vivid orange actor class
    dbg_msk_clean = np.zeros((240, 320, 3), dtype=np.float32)
    dbg_msk_clean[10:60, 10:80] = [0.0, 0.85, 0.0]
    dbg_msk_leak = dbg_msk_clean.copy()
    dbg_msk_leak[100:140, 120:180] = [0.0, 0.85, 0.0]
    check(
        not vh.detect_actor_green_moss_chroma(clean_actor, debug_class_image=dbg_cls, debug_mask_image=dbg_msk_clean).detected
        and vh.detect_actor_green_moss_chroma(clean_actor, debug_class_image=dbg_cls, debug_mask_image=dbg_msk_leak).detected,
        "Heuristic #6 DebugView 28/31 orange base==14 actor-moss overlap guard verified",
    )

    # -------------------------------------------------------------------------
    # 2. Planted-Failure Detection on REAL User Baseline Screenshots (#1..#8, #10)
    # -------------------------------------------------------------------------
    p_orb1 = _find_baseline("09.36.08")
    p_orb2 = _find_baseline("09.36.11")
    p_red1 = _find_baseline("09.33.23")
    p_red2 = _find_baseline("09.33.34")
    p_scan1 = _find_baseline("09.31.34")
    p_scan2 = _find_baseline("09.34.56")
    p_zebra = _find_baseline("09.35.44")
    p_skycut = _find_baseline("09.37.13")
    p_moss = _find_baseline("09.33.46")
    p_outl = _find_baseline("09.32.32")
    p_flk_a = _find_baseline("09.37.21")
    p_flk_b = _find_baseline("09.38.46")
    p_bark = _find_baseline("09.31.46")

    if all(p is not None for p in (p_orb1, p_orb2, p_red1, p_red2, p_scan1, p_scan2, p_zebra, p_skycut, p_moss, p_outl, p_flk_a, p_flk_b, p_bark)):
        check(vh.detect_indoor_black_orbs(p_orb1).detected and vh.detect_indoor_black_orbs(p_orb2).detected,
              "Baseline #1 indoor black orbs flagged (09.36.08 & 09.36.11)")
        check(vh.detect_red_sky_fire_bleed(p_red1).detected and vh.detect_red_sky_fire_bleed(p_red2).detected,
              "Baseline #2 red sky fire bleed flagged (09.33.23 & 09.33.34)")
        check(vh.detect_water_scanline_holes(p_scan1).detected and vh.detect_water_scanline_holes(p_scan2).detected,
              "Baseline #3 water scanline holes flagged (09.31.34 & 09.34.56)")
        check(vh.detect_water_zebra_stripes(p_zebra).detected, "Baseline #4 water zebra stripes flagged (09.35.44)")
        check(vh.detect_sky_diagonal_cutout(p_skycut).detected, "Baseline #5 sky diagonal cutout flagged (09.37.13)")
        check(vh.detect_actor_green_moss_chroma(p_moss).detected, "Baseline #6 actor green moss flagged (09.33.46 Stump)")
        check(vh.detect_dark_motion_outline(p_outl).detected, "Baseline #7 dark motion outline flagged (09.32.32)")
        check(vh.detect_camera_turn_shadow_flicker(p_flk_a, p_flk_b).detected,
              "Baseline #8 camera-turn shadow flicker flagged (09.37.21 vs 09.38.46)")
        check(vh.detect_crushed_black_tree_trunks(p_bark).detected,
              "Baseline #10 crushed black tree trunks flagged (09.31.46)")
    else:
        print("  [INFO] Baseline screenshot folder absent; continuing with synthetic planted-defect proofs")

    # -------------------------------------------------------------------------
    # 3. Planted-Failure Detection on ALL 12 New Wave 64 Heuristics (#9 .. #20)
    # -------------------------------------------------------------------------
    # #9: Single magenta NaN pixel in DebugView 33 MUST FAIL!
    dbg33_nan = dbg33_clean.copy()
    dbg33_nan[90, 160] = [1.0, 0.0, 1.0]
    check(vh.detect_nan_inf_hdr_overflow(dbg33_nan).detected, "Planted #9 single NaN/HDR magenta pixel flagged")

    # #10: Red alarm on DebugView 34 crushed bark
    dbg34_red = dbg34_clean.copy()
    dbg34_red[40:90, 40:80] = [0.98, 0.02, 0.02]
    check(vh.detect_crushed_black_tree_trunks(clean_ext, debug34_image=dbg34_red).detected,
          "Planted #10 DebugView 34 red crushed-bark alarm flagged")

    # #11: Magenta alarm on DebugView 34 bleached pine needles
    dbg34_mag = dbg34_clean.copy()
    dbg34_mag[30:80, 120:170] = [0.98, 0.02, 0.98]
    check(vh.detect_bleached_white_pine_needles(clean_ext, debug34_image=dbg34_mag).detected,
          "Planted #11 DebugView 34 magenta bleached-foliage alarm flagged")

    # #12: Neon electric-cyan water oversaturation
    syn_cyan = clean_ext.copy()
    syn_cyan[170:330, :] = [0.05, 0.82, 0.96]
    check(vh.detect_water_neon_cyan_oversat(syn_cyan).detected, "Planted #12 electric-cyan water oversat flagged")

    # #13: Flat untextured angular blue ice cave triangles
    syn_flat_ice = np.full((240, 320, 3), [0.18, 0.34, 0.56], dtype=np.float32)
    syn_flat_ice[:, 160:] = [0.14, 0.28, 0.48]
    check(vh.detect_flat_ice_cave_triangles(syn_flat_ice).detected, "Planted #13 flat ice cave triangles flagged")

    # #14: Corrupt normal map (25% black NaN holes)
    dbg1_bad = dbg1_clean.copy()
    dbg1_bad[:90, :160] = 0.0
    check(vh.detect_normal_map_integrity(dbg1_bad).detected, "Planted #14 corrupt DebugView 1 normal holes flagged")

    # #15: Clipped pure-white PBR albedo (>25%)
    dbg16_bad = dbg16_clean.copy()
    dbg16_bad[:100, :] = 0.99
    check(vh.detect_albedo_range_pbr_sanity(dbg16_bad).detected, "Planted #15 blown white DebugView 16 albedo flagged")

    # #16: 100% flat white collapsed shadow map
    dbg2_flat = np.ones((180, 320, 3), dtype=np.float32)
    check(vh.detect_shadow_coverage_balance(dbg2_flat).detected, "Planted #16 collapsed 100% flat white shadow map flagged")

    # #17: Harsh binary terminator spike on DebugView 32 skin
    dbg32_bad = dbg32_clean.copy()
    yy, xx = np.mgrid[0:180, 0:320]
    stripes = ((xx % 2) == 0) & (((xx - 160) ** 2 + (yy - 90) ** 2) < 40 ** 2)
    dbg32_bad[stripes] = [0.95, 0.72, 0.60]
    dbg32_bad[~stripes & (((xx - 160) ** 2 + (yy - 90) ** 2) < 40 ** 2)] = [0.10, 0.08, 0.07]
    check(vh.detect_skin_sss_sanity(dbg32_bad).detected, "Planted #17 harsh DebugView 32 skin terminator spike flagged")

    # #18: Blown overexposed frame
    syn_blown = np.ones((180, 320, 3), dtype=np.float32)
    check(vh.detect_exposure_and_tonemap_clipping(syn_blown).detected, "Planted #18 blown exposure clipping flagged")

    # #19: Collapsed flat depth buffer
    dbg29_flat = np.full((180, 320, 3), 0.5, dtype=np.float32)
    check(vh.detect_depth_and_stencil_integrity(dbg29_flat).detected, "Planted #19 flat DebugView 29 depth buffer flagged")

    # -------------------------------------------------------------------------
    # 3B. Wave 64 Addendum: ALL 5 Temporal / Motion-Vector Sequence Heuristics (#21..#25)
    # -------------------------------------------------------------------------
    def _encode_mv35(h: int, w: int, dx_px: float, dy_px: float, div_map: np.ndarray | None = None) -> np.ndarray:
        out = np.zeros((h, w, 3), dtype=np.float32)
        out[:, :, 0] = np.clip((dx_px / float(w)) * 16.0 + 0.5, 0.0, 1.0)
        out[:, :, 1] = np.clip((dy_px / float(h)) * 16.0 + 0.5, 0.0, 1.0)
        if div_map is not None:
            out[:, :, 2] = np.clip(div_map, 0.0, 1.0)
        return out

    # Textured world frame shifting smoothly by (shift_x=+3, shift_y=+1) px per frame -> frame_t1(x,y) = frame_t(x-3, y-1)
    # So to warp frame_t -> frame_t1 via frame_t(x + dx, y + dy), true MV is dx=-3, dy=-1!
    th, tw = 180, 320
    yy_t, xx_t = np.mgrid[0:th, 0:tw].astype(np.float32)
    base_tex = np.stack([
        0.45 + 0.25 * np.sin(xx_t * 0.09 + yy_t * 0.04) + 0.15 * np.cos(xx_t * 0.21),
        0.42 + 0.22 * np.cos(yy_t * 0.11 - xx_t * 0.05) + 0.15 * np.sin(yy_t * 0.19),
        0.48 + 0.20 * np.sin((xx_t + yy_t) * 0.07),
    ], axis=2).astype(np.float32)
    base_tex = np.clip(base_tex, 0.05, 0.95)
    frame_t1_shift = np.roll(np.roll(base_tex, shift=3, axis=1), shift=1, axis=0)
    mv_true = _encode_mv35(th, tw, dx_px=-3.0, dy_px=-1.0)
    mv_inv = _encode_mv35(th, tw, dx_px=+3.0, dy_px=+1.0)

    # #21: Clean MV reprojection PASS vs inverted MV sign FAIL
    r21_ok = vh.analyze_motion_vector_reprojection(base_tex, frame_t1_shift, mv_true)
    r21_inv = vh.analyze_motion_vector_reprojection(base_tex, frame_t1_shift, mv_inv)
    check(not r21_ok.detected and r21_inv.detected and r21_inv.details["sign_inverted"],
          f"Heuristic #21 MV reprojection: clean_rmse={r21_ok.score:.4f} vs inverted_sign_detected={r21_inv.details['sign_inverted']}")

    # #22: Water temporal flow speed: calm 1.2 px/frame PASS vs frozen 0.0 px/frame FAIL vs frantic 8.0 px/frame FAIL
    w_base = np.zeros((180, 320, 3), dtype=np.float32)
    ripple = 0.08 * np.sin(xx_t * 0.18 + yy_t * 0.12) + 0.06 * np.cos(xx_t * 0.31 - yy_t * 0.22)
    w_base[:, :, 0] = np.clip(0.10 + ripple * 0.4, 0.02, 0.9)
    w_base[:, :, 1] = np.clip(0.24 + ripple * 0.8, 0.05, 0.9)
    w_base[:, :, 2] = np.clip(0.36 + ripple, 0.10, 0.9)
    water_calm = [np.roll(w_base, shift=int(round(k * 1.2)), axis=1) for k in range(5)]
    water_frozen = [w_base.copy() for _ in range(5)]
    water_frantic = [np.roll(w_base, shift=k * 8, axis=1) for k in range(5)]
    r22_ok = vh.analyze_water_temporal_flow_speed(water_calm)
    r22_zero = vh.analyze_water_temporal_flow_speed(water_frozen)
    r22_fast = vh.analyze_water_temporal_flow_speed(water_frantic)
    check(not r22_ok.detected and r22_zero.detected and r22_fast.detected,
          f"Heuristic #22 water flow speed: calm={r22_ok.score:.2f}px/f (PASS) vs frozen={r22_zero.score:.2f} & frantic={r22_fast.score:.2f} (FAIL)")

    # #23: Shadow temporal stability: stable burst PASS vs boiling/popping burst FAIL
    sh_stable = [dbg2_clean.copy() for _ in range(4)]
    sh_boil = [dbg2_clean.copy(), np.clip(dbg2_clean * 0.35, 0.0, 1.0), np.clip(dbg2_clean + 0.35, 0.0, 1.0), dbg2_clean.copy()]
    r23_ok = vh.analyze_shadow_temporal_stability(sh_stable)
    r23_bad = vh.analyze_shadow_temporal_stability(sh_boil)
    check(not r23_ok.detected and r23_bad.detected,
          f"Heuristic #23 shadow temporal stability: stable={r23_ok.score:.4f} vs boil/pop={r23_bad.score:.4f}")

    # #24: Temporal ghosting & dark motion outline along B > 0.15 divergence rim
    div_ring = np.zeros((th, tw), dtype=np.float32)
    div_ring[60:120, 140:143] = 0.85  # moving actor silhouette edge
    mv_actor = _encode_mv35(th, tw, dx_px=-3.0, dy_px=-1.0, div_map=div_ring)
    r24_ok = vh.analyze_temporal_ghosting_and_motion_outline([base_tex, frame_t1_shift], [mv_actor, mv_actor])
    ghost_bad = frame_t1_shift.copy()
    ghost_bad[60:120, 140:143] = 0.01  # planted dark cel-outline & ghosting residual on moving rim
    r24_bad = vh.analyze_temporal_ghosting_and_motion_outline([base_tex, ghost_bad], [mv_actor, mv_actor])
    check(not r24_ok.detected and r24_bad.detected,
          f"Heuristic #24 ghosting/outline on moving rim: clean={r24_ok.score:.4f} vs planted_rim={r24_bad.score:.4f}")

    # #25: World-lock anti-swimming: world-locked shift PASS vs screen-locked static texture during camera motion FAIL
    r25_ok = vh.analyze_world_lock_anti_swimming([base_tex, frame_t1_shift], [mv_true, mv_true])
    r25_swim = vh.analyze_world_lock_anti_swimming([base_tex, base_tex.copy()], [mv_true, mv_true])
    check(not r25_ok.detected and r25_swim.detected and r25_ok.details.get("motion_regime") == "moving_cam_sweep",
          f"Heuristic #25 world-lock anti-swimming: world_locked_ratio={r25_ok.score:.2f} vs screen_locked_ratio={r25_swim.score:.4f}")

    # Wave 65 calibration checks:
    # (a) Stationary MV bursts (< 0.25 px) in #21 & #25: clean PASS (no div0) vs corrupted frame FAIL
    mv_zero = _encode_mv35(th, tw, dx_px=0.0, dy_px=0.0)
    r21_stat_ok = vh.analyze_motion_vector_reprojection(base_tex, base_tex.copy(), mv_zero)
    r21_stat_bad = vh.analyze_motion_vector_reprojection(base_tex, np.zeros_like(base_tex), mv_zero)
    r25_stat_ok = vh.analyze_world_lock_anti_swimming([base_tex, base_tex.copy()], [mv_zero, mv_zero])
    r25_stat_bad = vh.analyze_world_lock_anti_swimming([base_tex, np.zeros_like(base_tex)], [mv_zero, mv_zero])
    check(
        (not r21_stat_ok.detected)
        and r21_stat_ok.details.get("motion_regime") == "stationary"
        and r21_stat_bad.detected
        and (not r25_stat_ok.detected)
        and r25_stat_ok.details.get("motion_regime") == "stationary"
        and r25_stat_bad.detected,
        "Wave 65 stationary vs moving MV burst handling in #21 & #25 PASS (no divide-by-zero)",
    )

    # (b) detect_indoor_black_orbs gated on is_interior (skips outdoor dark pillars at High Hrothgar)
    outdoor_pillar = np.full((180, 320, 3), 0.72, dtype=np.float32)
    outdoor_pillar[30:140, 100:210, :] = 0.004
    r1_ext_skip = vh.detect_indoor_black_orbs(outdoor_pillar, is_interior=False)
    r1_int_flag = vh.detect_indoor_black_orbs(outdoor_pillar, is_interior=True)
    check(not r1_ext_skip.detected and r1_ext_skip.details.get("skipped_exterior_scene") and r1_int_flag.detected,
          "Wave 65 detect_indoor_black_orbs is_interior gate PASS (skips exterior, flags interior)")

    # (c) detect_water_scanline_holes WaterView=7 magenta direct mode (> 0.0005 -> FAIL, clean -> PASS)
    wv7_clean = np.zeros((180, 320, 3), dtype=np.float32)
    wv7_clean[90:, :, 0] = 0.35  # flow
    wv7_clean[90:, :, 1] = 0.45  # depth
    wv7_clean[90:, :, 2] = 0.65  # shoreAlpha
    wv7_magenta = wv7_clean.copy()
    wv7_magenta[110:122, 100:130, :] = [1.0, 0.0, 1.0]  # > 0.0005 magenta continuity alarm
    r3_v7_ok = vh.detect_water_scanline_holes(clean_ext, water_flow_diag_image=wv7_clean)
    r3_v7_bad = vh.detect_water_scanline_holes(clean_ext, water_flow_diag_image=wv7_magenta)
    check(
        (not r3_v7_ok.detected)
        and r3_v7_ok.details.get("mode") == "water_flow_diag_view7"
        and r3_v7_bad.detected
        and r3_v7_bad.score > 0.0005,
        f"Wave 65 WaterView=7 magenta continuity gate PASS (clean={r3_v7_ok.score:.6f} vs planted={r3_v7_bad.score:.5f})",
    )

    # -------------------------------------------------------------------------
    # 4. Heuristic #20 + full_pipeline_audit.py + SkyrimCustomIniGuard + playtest_tour.py
    # -------------------------------------------------------------------------
    tmp_root = Path(tempfile.mkdtemp(prefix="raw_full_audit_w64_"))
    try:
        cap_dir = tmp_root / "captures"
        eng_out = cap_dir / "raw_engine_out"
        rep_out = tmp_root / "audit_out"
        sc_dir = cap_dir / "riverwood_day"
        sc_dir.mkdir(parents=True)
        eng_out.mkdir(parents=True)

        # Write clean multi-view scene bundle for riverwood_day
        def _save(arr: np.ndarray, p: Path) -> None:
            Image.fromarray((np.clip(arr, 0.0, 1.0) * 255).astype(np.uint8)).save(p)

        _save(clean_ext, sc_dir / "riverwood_day_improved.png")
        _save(dbg1_clean, sc_dir / "riverwood_day_view_normals.png")
        _save(dbg2_clean, sc_dir / "riverwood_day_view_sun_shadow.png")
        _save(dbg16_clean, sc_dir / "riverwood_day_view_albedo.png")
        _save(dbg_cls, sc_dir / "riverwood_day_view_full_classes.png")
        _save(dbg29_clean, sc_dir / "riverwood_day_view_depth_edge.png")
        _save(dbg_msk_clean, sc_dir / "riverwood_day_view_moss_mask.png")
        _save(dbg32_clean, sc_dir / "riverwood_day_view_skin_sss.png")
        _save(dbg33_clean, sc_dir / "riverwood_day_view_nan_hdr.png")
        _save(dbg34_clean, sc_dir / "riverwood_day_view_flora_trees.png")
        # Write 3-frame burst sequences (_f00.._f02) for temporal heuristics (#21..#25) in full_pipeline_audit
        for fi in range(3):
            sh_img = np.roll(np.roll(base_tex, shift=3 * fi, axis=1), shift=1 * fi, axis=0)
            sh_dbg2 = np.roll(np.roll(dbg2_clean, shift=3 * fi, axis=1), shift=1 * fi, axis=0)
            _save(sh_img, sc_dir / f"riverwood_day_audit_improved_f{fi:02d}.png")
            _save(mv_true, sc_dir / f"riverwood_day_audit_view_motion_outline_f{fi:02d}.png")
            _save(sh_dbg2, sc_dir / f"riverwood_day_audit_view_sun_shadow_f{fi:02d}.png")

        (eng_out / "boot_selfcheck.txt").write_text("Run #1 | Overall: PASS | Pass=12/12 Warn=0 Fail=0\n", encoding="utf-8")
        (eng_out / "bridge_live_telemetry.json").write_text(
            json.dumps({"d3d12DebugErrors": 0, "lightingMs": 1.12, "liveWorldCompositeMs": 0.04, "waterCausticsMs": 0.03}),
            encoding="utf-8",
        )
        (eng_out / "boot_sentinel.state").write_text("in_progress=1\nunclean_boot_count=0\n", encoding="utf-8")

        # Heuristic #20 clean PASS vs planted FAIL (missing/corrupt telemetry)
        r20_ok = vh.detect_live_world_compositor_activity(engine_out_dir=eng_out)
        r20_bad = vh.detect_live_world_compositor_activity(engine_out_dir=tmp_root / "missing_dir")
        check(not r20_ok.detected and r20_bad.detected,
              f"Heuristic #20 live telemetry audit: clean={r20_ok.severity} vs missing={r20_bad.severity}")

        # Full pipeline offline audit + boot_sentinel.state cleanup
        fp_rep = fpa.run_offline_audit(cap_dir, rep_out, profile="quick", engine_out_dir=eng_out)
        fp_md = (rep_out / "full_pipeline_report.md").read_text(encoding="utf-8")
        check(
            (rep_out / "full_pipeline_report.json").exists()
            and not (eng_out / "boot_sentinel.state").exists()
            and ("\u2014" not in fp_md)
            and fp_rep["summary"]["scenes_audited"] == 1
            and fp_rep["summary"]["clean_scenes"] == 1
            and fp_rep["scenes"][0]["heuristics_run"] >= 23,
            f"full_pipeline_audit.py offline multi-view+burst audit ({fp_rep['scenes'][0]['heuristics_run']} heuristics/scene), MD/JSON & sentinel cleanup PASS",
        )

        # SkyrimCustomIniGuard + bat raw_scene_init + cam_sweep + readiness parser proof (Wave 65)
        fake_ini = tmp_root / "SkyrimCustom.ini"
        orig_bytes = b"[General]\r\nsLanguage=ENGLISH\r\nbAlwaysActive=0\r\n"
        fake_ini.write_bytes(orig_bytes)
        bat_p = fpa.write_scene_init_bat(cap_dir, "fw c8221; set gamehour to 14")
        bat_txt = bat_p.read_text(encoding="utf-8")
        check(
            "set Survival_ModeEnabled to 0" in bat_txt
            and "stopquest ccQDRSSE001_SurvivalModeQuest" in bat_txt
            and "fw c8221" in bat_txt
            and "set gamehour to 14" in bat_txt,
            "Wave 65 write_scene_init_bat writes Survival suppress + weather/TOD lines",
        )
        with fpa.SkyrimCustomIniGuard(fake_ini) as guard:
            guard.write_scene_boot("WindhelmExterior01", fpa.SCENE_INIT_BAT_CMD)
            mid_txt = fake_ini.read_text(encoding="utf-8")
            check("sStartingCell=WindhelmExterior01" in mid_txt and "sStartingConsoleCommand=bat raw_scene_init" in mid_txt,
                  "SkyrimCustomIniGuard injects sStartingCell & bat raw_scene_init cleanly")
        check(fake_ini.read_bytes() == orig_bytes, "SkyrimCustomIniGuard restores SkyrimCustom.ini bit-exact on exit")

        sweep_p = fpa.emit_cam_sweep(cap_dir)
        sweep_txt = sweep_p.read_text(encoding="utf-8").strip()
        check(sweep_txt == "cam_sweep 0.008 0.0012 0.0 240",
              f"Wave 65 emit_cam_sweep writes exact command ({sweep_txt})")

        (cap_dir / "raw_harness_status.txt").write_text("presents=300 world=240 loads=1 cell=1000042101 active=0\n", encoding="utf-8")
        (eng_out / "control_results.jsonl").write_text(
            json.dumps({"cmd": "view", "cell_id": 65537, "menu_paused": 0, "interior": 0, "sweep_active": 0}) + "\n",
            encoding="utf-8",
        )
        st = fpa.parse_harness_status((cap_dir / "raw_harness_status.txt").read_text(encoding="utf-8"))
        check(st.get("world") == 240 and st.get("cell") != 1, "Wave 65 parse_harness_status parses world/cell readiness")

        # playtest_tour.py offline audit check
        pt_out = tmp_root / "tour_out"
        tour_rep = pt.run_offline_tour(sc_dir, pt_out, game_dir=cap_dir, engine_out_dir=eng_out, glob_pattern="*_improved.png")
        check(tour_rep["summary"]["frames_audited"] == 1 and tour_rep["summary"]["clean_frames"] == 1,
              "playtest_tour.py offline audit & report generation PASS")
    finally:
        shutil.rmtree(tmp_root, ignore_errors=True)

    # AST check on tools/live/kinds.py
    kinds_src = (LIVE_DIR / "kinds.py").read_text(encoding="utf-8")
    tree = ast.parse(kinds_src, filename="kinds.py")
    fn_names = {node.name for node in ast.walk(tree) if isinstance(node, ast.FunctionDef)}
    check({"debug_view", "visual_audit", "camera_sweep_flicker"}.issubset(fn_names),
          "tools/live/kinds.py registers debug_view, visual_audit, camera_sweep_flicker")

    print(f"=== RESULT: {'ALL PASS' if failures == 0 else 'FAIL'} (failures={failures}) ===")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(run_tests())
