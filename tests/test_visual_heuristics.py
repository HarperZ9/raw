"""Verification suite for RAW Wave 63 Visual Heuristics & Playtest Tour Harness.

Verifies:
  1. Planted-failure detection on REAL user baseline screenshots (all 8 detectors on the
     exact 2026-11-25 09:31..09:38 Skyrim SE PNGs when present on disk).
  2. Deterministic synthetic planted-defect proofs (CI-safe fallback & precision verification).
  3. Clean PASS (zero false positives) on synthetic artifact-free control images (sky, water,
     cave interior, actor fur/skin, smooth camera sweep, and DebugView 28/31 overlap).
  4. Offline playtest_tour.py pipeline, playtest_report.{json,md} generation, boot_sentinel.state
     cleanup, and tools/live/kinds.py handler registration.

Original work, MIT.
"""
from __future__ import annotations

import ast
import glob
import json
import os
import shutil
import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parent.parent
LIVE_DIR = REPO_ROOT / "tools" / "live"
sys.path.insert(0, str(LIVE_DIR))

import playtest_tour as pt  # noqa: E402
import visual_heuristics as vh  # noqa: E402

BASELINE_DIR = Path(r"C:\Users\Zain\Videos\NVIDIA\The Elder Scrolls V Skyrim Special Edition")


def _find_baseline(tag: str) -> Path | None:
    if not BASELINE_DIR.is_dir():
        return None
    matches = sorted(glob.glob(str(BASELINE_DIR / f"*{tag}*.png")))
    return Path(matches[0]) if matches else None


def _make_clean_exterior(h: int = 360, w: int = 640) -> np.ndarray:
    """Clean Nordic exterior: smooth sky gradient, soft clouds, calm dark-teal river, neutral stone."""
    img = np.zeros((h, w, 3), dtype=np.float32)
    yy = np.linspace(0.0, 1.0, h, dtype=np.float32)[:, None]
    xx = np.linspace(0.0, 1.0, w, dtype=np.float32)[None, :]
    # Upper 45%: smooth blue-slate daylight sky
    sky_h = int(h * 0.45)
    img[:sky_h, :, 0] = 0.35 + 0.25 * yy[:sky_h]
    img[:sky_h, :, 1] = 0.46 + 0.22 * yy[:sky_h]
    img[:sky_h, :, 2] = 0.64 + 0.16 * yy[:sky_h]
    # Lower 55%: calm deep Nordic emerald-slate water with gentle broad swell (no scanlines/stripes)
    img[sky_h:, :, 0] = 0.12 + 0.03 * np.sin(xx * 6.0)
    img[sky_h:, :, 1] = 0.20 + 0.03 * np.cos(yy[sky_h:] * 8.0)
    img[sky_h:, :, 2] = 0.26 + 0.04 * np.sin(xx * 4.0 + yy[sky_h:] * 5.0)
    return np.clip(img, 0.0, 1.0)


def _make_clean_interior_cave(h: int = 360, w: int = 640) -> np.ndarray:
    """Clean lit glacial cave interior with warm hearth glow and zero black soot orbs."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    yn, xn = yy / h, xx / w
    img = np.zeros((h, w, 3), dtype=np.float32)
    img[:, :, 0] = 0.16 + 0.05 * np.sin(xn * 5.0)
    img[:, :, 1] = 0.22 + 0.05 * np.cos(yn * 4.0)
    img[:, :, 2] = 0.30 + 0.06 * np.sin((xn + yn) * 4.0)
    # Warm hearth light pool near lower center
    d2 = (xn - 0.50) ** 2 + (yn - 0.68) ** 2
    glow = np.exp(-d2 / 0.04) * 0.35
    img[:, :, 0] += glow * 1.1
    img[:, :, 1] += glow * 0.65
    img[:, :, 2] += glow * 0.25
    return np.clip(img, 0.0, 1.0)


def _make_clean_actor_portrait(h: int = 360, w: int = 640) -> np.ndarray:
    """Clean foreground warm-brown dog/actor silhouette against muted stone (zero green moss)."""
    img = np.full((h, w, 3), [0.24, 0.23, 0.22], dtype=np.float32)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    mask = ((xx - w * 0.5) / (w * 0.18)) ** 2 + ((yy - h * 0.6) / (h * 0.22)) ** 2 <= 1.0
    img[mask] = [0.34, 0.25, 0.19]  # warm brown fur/leather
    return img


def run_tests() -> int:
    failures = 0

    def check(cond: bool, msg: str) -> None:
        nonlocal failures
        tag = "PASS" if cond else "FAIL"
        print(f"  [{tag}] {msg}")
        if not cond:
            failures += 1

    print("=== RAW Wave 63: Visual Heuristics & Playtest Harness Suite ===")

    # -------------------------------------------------------------------------
    # 1. Clean Synthetic Controls MUST PASS ALL 8 Heuristics (Zero False Positives)
    # -------------------------------------------------------------------------
    clean_ext = _make_clean_exterior()
    clean_cave = _make_clean_interior_cave()
    clean_actor = _make_clean_actor_portrait()
    rep_ext = vh.audit_image(clean_ext, label="clean_exterior")
    rep_cave = vh.audit_image(clean_cave, label="clean_cave")
    rep_actor = vh.audit_image(clean_actor, label="clean_actor")

    # Smooth 2px camera pan pair -> zero shadow flicker
    pan_b = np.roll(clean_ext, shift=2, axis=1)
    r_pan = vh.detect_camera_turn_shadow_flicker(clean_ext, pan_b)

    check(rep_ext["passed"], f"Clean exterior control passes all 7 single-frame heuristics (defects={rep_ext['defects']})")
    check(rep_cave["passed"], f"Clean cave control passes all 7 single-frame heuristics (defects={rep_cave['defects']})")
    check(rep_actor["passed"], f"Clean actor control passes all 7 single-frame heuristics (defects={rep_actor['defects']})")
    check(not r_pan.detected, f"Smooth camera pan passes flicker guard (shift={r_pan.score:.4f})")

    # DebugView 28 + 31 overlap: clean actor (no moss overlap) vs planted moss leak
    dbg_cls = np.zeros((240, 320, 3), dtype=np.float32)
    dbg_cls[80:180, 100:220] = [0.95, 0.10, 0.95]  # base==14 actor class (magenta)
    dbg_msk_clean = np.zeros((240, 320, 3), dtype=np.float32)
    dbg_msk_clean[10:60, 10:80] = [0.0, 0.85, 0.0]  # moss only on distant rock
    dbg_msk_leak = dbg_msk_clean.copy()
    dbg_msk_leak[100:140, 120:180] = [0.0, 0.85, 0.0]  # planted moss leak onto actor
    r_dbg_clean = vh.detect_actor_green_moss_chroma(clean_actor, debug_class_image=dbg_cls, debug_mask_image=dbg_msk_clean)
    r_dbg_leak = vh.detect_actor_green_moss_chroma(clean_actor, debug_class_image=dbg_cls, debug_mask_image=dbg_msk_leak)
    check(not r_dbg_clean.detected and r_dbg_leak.detected,
          f"DebugView 28/31 actor-moss overlap guard: clean={r_dbg_clean.score:.4f} vs leak={r_dbg_leak.score:.4f}")

    # -------------------------------------------------------------------------
    # 2. Planted-Failure Detection on REAL User Baseline Screenshots (All 8 Detectors!)
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

    if all(p is not None for p in (p_orb1, p_orb2, p_red1, p_red2, p_scan1, p_scan2, p_zebra, p_skycut, p_moss, p_outl, p_flk_a, p_flk_b)):
        r_orb1 = vh.detect_indoor_black_orbs(p_orb1)
        r_orb2 = vh.detect_indoor_black_orbs(p_orb2)
        check(r_orb1.detected and r_orb2.detected,
              f"Real baseline indoor black orbs flagged: 09.36.08={r_orb1.score:.3f}, 09.36.11={r_orb2.score:.3f}")

        r_red1 = vh.detect_red_sky_fire_bleed(p_red1)
        r_red2 = vh.detect_red_sky_fire_bleed(p_red2)
        check(r_red1.detected and r_red2.detected,
              f"Real baseline red sky fire bleed flagged: 09.33.23={r_red1.score:.4f}, 09.33.34={r_red2.score:.4f}")

        r_scan1 = vh.detect_water_scanline_holes(p_scan1)
        r_scan2 = vh.detect_water_scanline_holes(p_scan2)
        check(r_scan1.detected and r_scan2.detected,
              f"Real baseline water scanline holes flagged: 09.31.34={r_scan1.score:.4f}, 09.34.56={r_scan2.score:.4f}")

        r_zebra = vh.detect_water_zebra_stripes(p_zebra)
        check(r_zebra.detected,
              f"Real baseline water zebra stripes flagged: 09.35.44 peak={r_zebra.score:.2f} (aniso={r_zebra.details['grad_anisotropy_dy_dx']})")

        r_skycut = vh.detect_sky_diagonal_cutout(p_skycut)
        check(r_skycut.detected,
              f"Real baseline sky diagonal cutout flagged: 09.37.13 ratio={r_skycut.score:.3f} angle={r_skycut.details['best_angle_deg']}deg")

        r_moss = vh.detect_actor_green_moss_chroma(p_moss)
        check(r_moss.detected,
              f"Real baseline actor green moss flagged: 09.33.46 (Stump) frac={r_moss.score:.4f}")

        r_outl = vh.detect_dark_motion_outline(p_outl)
        check(r_outl.detected,
              f"Real baseline dark motion outline flagged: 09.32.32 frac={r_outl.score:.5f}")

        r_flk = vh.detect_camera_turn_shadow_flicker(p_flk_a, p_flk_b)
        check(r_flk.detected,
              f"Real baseline camera-turn shadow flicker flagged: 09.37.21 vs 09.38.46 shift={r_flk.score:.3f} p95={r_flk.details['p95_abs_diff']:.3f}")
    else:
        print("  [INFO] Real baseline screenshot folder not present; running synthetic planted-defect proofs")

    # -------------------------------------------------------------------------
    # 3. Synthetic Planted-Defect Proofs (Deterministic on Any Host)
    # -------------------------------------------------------------------------
    # 3a. Planted indoor black orb inside clean cave
    syn_orb = clean_cave.copy()
    syn_orb[80:250, 200:400] = 0.0
    check(vh.detect_indoor_black_orbs(syn_orb).detected, "Synthetic planted indoor black orb flagged")

    # 3b. Planted red sky fire bleed in upper sky
    syn_red = clean_ext.copy()
    syn_red[40:120, 160:460] = [0.85, 0.22, 0.08]
    check(vh.detect_red_sky_fire_bleed(syn_red).detected, "Synthetic planted red sky fire bleed flagged")

    # 3c. Planted sky diagonal cutout
    syn_cut = clean_ext.copy()
    yy, xx = np.mgrid[0:360, 0:640]
    above_diag = (yy < (150 - 0.18 * xx)) & (yy > 30) & (yy < 200)
    below_diag = (yy >= (150 - 0.18 * xx)) & (yy < 200)
    syn_cut[above_diag] = [0.68, 0.70, 0.72]  # cloud grey
    syn_cut[below_diag] = [0.22, 0.38, 0.78]  # razor-sharp blue sky strip
    check(vh.detect_sky_diagonal_cutout(syn_cut).detected, "Synthetic planted sky diagonal cutout flagged")

    # -------------------------------------------------------------------------
    # 4. Offline playtest_tour.py Pipeline + boot_sentinel.state Cleanup + kinds.py Check
    # -------------------------------------------------------------------------
    tmp_root = Path(tempfile.mkdtemp(prefix="raw_playtest_tour_w63_"))
    try:
        shots_dir = tmp_root / "shots"
        eng_out = tmp_root / "raw_engine_out"
        rep_out = tmp_root / "report"
        shots_dir.mkdir()
        eng_out.mkdir()

        Image.fromarray((clean_ext * 255).astype(np.uint8)).save(shots_dir / "01_clean.png")
        Image.fromarray((syn_orb * 255).astype(np.uint8)).save(shots_dir / "02_orb_defect.png")
        (eng_out / "boot_selfcheck.txt").write_text("Run #1 | Overall: PASS | Pass=12/12\n", encoding="utf-8")
        (eng_out / "boot_sentinel.state").write_text("in_progress=1\nunclean_boot_count=0\n", encoding="utf-8")

        tour_rep = pt.run_offline_tour(shots_dir, rep_out, game_dir=tmp_root, engine_out_dir=eng_out)
        json_exists = (rep_out / "playtest_report.json").exists()
        md_exists = (rep_out / "playtest_report.md").exists()
        sentinel_removed = not (eng_out / "boot_sentinel.state").exists()
        md_text = (rep_out / "playtest_report.md").read_text(encoding="utf-8") if md_exists else ""
        no_em_dash = ("\u2014" not in md_text)

        check(
            json_exists and md_exists and sentinel_removed and no_em_dash
            and tour_rep["summary"]["frames_audited"] == 2
            and tour_rep["summary"]["clean_frames"] == 1
            and tour_rep["summary"]["flagged_frames"] == 1
            and tour_rep["boot_selfcheck"]["passed"],
            f"playtest_tour.py offline audit, JSON+MD report & boot_sentinel.state cleanup verified",
        )
    finally:
        shutil.rmtree(tmp_root, ignore_errors=True)

    # AST syntax & handler check on tools/live/kinds.py (no live game session needed)
    kinds_src = (LIVE_DIR / "kinds.py").read_text(encoding="utf-8")
    tree = ast.parse(kinds_src, filename="kinds.py")
    fn_names = {node.name for node in ast.walk(tree) if isinstance(node, ast.FunctionDef)}
    has_handlers = {"debug_view", "visual_audit", "camera_sweep_flicker"}.issubset(fn_names)
    check(has_handlers and '"debug_view"' in kinds_src and '"visual_audit"' in kinds_src and '"camera_sweep_flicker"' in kinds_src,
          "tools/live/kinds.py parses cleanly & registers debug_view, visual_audit, camera_sweep_flicker")

    print(f"=== RESULT: {'ALL PASS' if failures == 0 else 'FAIL'} (failures={failures}) ===")
    return 0 if failures == 0 else 1


if __name__ == "__main__":
    raise SystemExit(run_tests())
