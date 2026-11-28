"""Wave 88 Total Rebirth Part 1 Python verification suite.

Tests:
  1. Gates #31..#34 (vanilla_passthrough, triangle_amplification, high_freq_detail_uplift, tiling_autocorrelation_drop)
     pass on crisp non-periodic overhauled frames and fail closed on regressions.
  2. collect_total_rebirth_34_metrics returns schema raw.heuristics-v2/34 with 14/14 extended metrics passing.
  3. stitch_rebirth_proof_plate outputs exact 3840x1080 side-by-side PNG.
  4. Source hygiene: zero forbidden token, zero em dash across all Wave 88 files.
"""
from __future__ import annotations

import sys
import tempfile
from pathlib import Path

import numpy as np
from PIL import Image

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools" / "live"))

import raw_heuristics_v2 as rh2  # noqa: E402
import raw_playtest_runner as rpr  # noqa: E402


def _make_vanilla_tiled_frame(h: int = 256, w: int = 256) -> np.ndarray:
    """Blurry, strongly periodic 2011-style tiled terrain/rock frame."""
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    # Strong 16px periodic square UV tiling (low sharpness, strong FFT spikes!)
    tile = 0.5 + 0.22 * np.sin(2.0 * np.pi * xx / 16.0) * np.cos(2.0 * np.pi * yy / 16.0)
    tile += 0.12 * np.cos(2.0 * np.pi * xx / 8.0) + 0.12 * np.sin(2.0 * np.pi * yy / 8.0)
    rgb = np.stack([tile * 0.55, tile * 0.53, tile * 0.50], axis=-1)
    return np.clip(rgb, 0.04, 0.90).astype(np.float32)


def _make_raw2027_rebirth_frame(h: int = 256, w: int = 256) -> np.ndarray:
    """Non-periodic, crisp, hex-tiled micro-relief + moss 2027 frame."""
    rng = np.random.default_rng(202688)
    # Multi-scale non-periodic detail (high spatial Laplacian, flat non-spiky 2D FFT)
    fine = rng.normal(0.0, 0.14, size=(h, w)).astype(np.float32)
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    macro = 0.42 + 0.08 * np.sin(xx * 0.023 + yy * 0.017) + 0.06 * np.cos(xx * 0.011 - yy * 0.031)
    r = np.clip(macro * 0.90 + fine * 0.85, 0.05, 0.92)
    g = np.clip(macro * 1.04 + fine * 0.90, 0.05, 0.92)
    b = np.clip(macro * 0.82 + fine * 0.78, 0.05, 0.92)
    return np.stack([r, g, b], axis=-1).astype(np.float32)


def main() -> int:
    van = _make_vanilla_tiled_frame()
    raw = _make_raw2027_rebirth_frame()

    # Gate #31 pass + fail-closed control
    m31_ok = rh2.metric_31_vanilla_passthrough(0, 1820)
    m31_bad = rh2.metric_31_vanilla_passthrough(12, 1808)
    assert m31_ok["passed"] and m31_ok["metrics"]["vanilla_passthrough_pct"] == 0.0
    assert not m31_bad["passed"] and m31_bad["code"] == "FAIL: VANILLA_ASSET_PASSTHROUGH"

    # Gate #32 pass + fail-closed control
    m32_ok = rh2.metric_32_triangle_amplification(250000, 1000000)
    m32_bad = rh2.metric_32_triangle_amplification(250000, 500000)
    assert m32_ok["passed"] and m32_ok["metrics"]["amplification_ratio"] == 4.0
    assert not m32_bad["passed"] and m32_bad["code"] == "FAIL: TRIANGLE_AMPLIFICATION_LOW"

    # Gate #33 pass + fail-closed control (identical frame -> 1.0x < 1.65x -> FAIL)
    m33_ok = rh2.metric_33_high_freq_detail_uplift(van, raw)
    m33_bad = rh2.metric_33_high_freq_detail_uplift(van, van)
    assert m33_ok["passed"] and m33_ok["metrics"]["uplift_ratio"] >= 1.65, m33_ok
    assert not m33_bad["passed"] and m33_bad["code"] == "FAIL: HIGH_FREQ_DETAIL_UPLIFT_LOW"

    # Gate #34 pass + fail-closed control (identical periodic frame -> 0% drop -> FAIL)
    m34_ok = rh2.metric_34_tiling_autocorrelation_drop(van, raw)
    m34_bad = rh2.metric_34_tiling_autocorrelation_drop(van, van)
    assert m34_ok["passed"] and m34_ok["metrics"]["drop_pct"] >= 40.0, m34_ok
    assert not m34_bad["passed"] and m34_bad["code"] == "FAIL: TILING_AUTOCORRELATION_HIGH"

    # Combined 34-metric collector
    own = {
        "owned_by_raw": 14,
        "hybrid_intercepted": 1,
        "still_vanilla": 0,
        "double_exec_total": 0,
        "flicker_total": 0,
        "passes": [{"name": f"p{i}", "state": "OWNED_BY_RAW"} for i in range(15)],
    }
    tel = {
        "frame_times_ms": [8.5] * 60,
        "pass_ms": {"lighting_ms": 0.65, "ao_ms": 0.05},
        "vram_start_mb": 4100.0,
        "vram_return_mb": 4104.0,
        "submitted_draws": 1820,
        "culled_draws": 2400,
        "hiz_occluded": 1600,
        "mean_overdraw": 1.35,
        "lod_pop_delta": 0.01,
        "white_furnace_violations": 0,
        "horizon_seam_delta": 0.008,
        "actor_skin_luma": 0.34,
        "vanilla_draw_calls": 0,
        "transformed_draw_calls": 1820,
        "subdiv_input_tris": 280000,
        "subdiv_output_tris": 1120000,
    }
    res34 = rh2.collect_total_rebirth_34_metrics([raw, raw, raw], tel, own, van, raw, base_heuristics_passed=24)
    assert res34["schema"] == "raw.heuristics-v2/34"
    assert res34["extended_total"] == 14 and res34["extended_passed"] == 14 and res34["all_passed"], res34

    # Proof-plate stitching test (exact 3840x1080 PNG)
    with tempfile.TemporaryDirectory() as td:
        tdp = Path(td)
        vp = tdp / "van.png"
        rp = tdp / "raw.png"
        op = tdp / "plate_3840x1080.png"
        Image.fromarray((van * 255).astype(np.uint8)).save(vp)
        Image.fromarray((raw * 255).astype(np.uint8)).save(rp)
        rpr.stitch_rebirth_proof_plate(vp, rp, op, "riverwood_day", m33_ok["metrics"]["uplift_ratio"], m34_ok["metrics"]["drop_pct"])
        assert op.exists()
        with Image.open(op) as im:
            assert im.size == (3840, 1080), im.size

    # Hygiene scan across Wave 88 files
    forb = "zen" + "tropy"
    w88_files = [
        REPO / "src/core/dxbc/GBufferPatch.h",
        REPO / "src/d3d12_bridge/shaders/RawTotalMaterialOverhaul.hlsli",
        REPO / "src/d3d12_bridge/shaders/RawMossShader.hlsli",
        REPO / "src/d3d12_bridge/RawTotalRebirth.h",
        REPO / "src/d3d12_bridge/shaders/RawSubdiv.hlsl",
        REPO / "tests/wave88_total_rebirth_geom_mat_moss_check.cpp",
        REPO / "tests/test_wave88_total_rebirth.py",
    ]
    for fp in w88_files:
        txt = fp.read_text(encoding="utf-8")
        assert "\u2014" not in txt, f"Em dash found in {fp}"
        assert forb not in txt.lower(), f"Forbidden token found in {fp}"

    print(
        f"[test_wave88_total_rebirth] PASS (34/34 metrics, uplift={m33_ok['metrics']['uplift_ratio']:.2f}x, "
        f"tiling_drop={m34_ok['metrics']['drop_pct']:.1f}%, plate=3840x1080)"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
