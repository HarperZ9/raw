"""Wave 73 Verification Suite: 10-Step Deja Anti-Drift, Froxel Albedo Conservation,
100 Hz Multi-Game Frame Pacing, Planted-Failure Controls, and Clean-Room Hygiene.

Original work, MIT.
"""

from __future__ import annotations

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _karis_fwd(luma: float) -> float:
    return luma / (1.0 + max(luma, 0.0))


def _karis_inv(y: float) -> float:
    return y / max(1.0 - y, 1.0e-5)


def simulate_10_step_karis(
    init_luma: float,
    alt_luma: float,
    alpha: float,
    use_deja_antidrift: bool,
    injected_step_bias: float = 0.0,
    spec_mv_gain: float = 1.0,
) -> tuple[float, float, float]:
    """Returns (max_rel_luma_drift, final_luma, expected_luma)."""
    hist_l = max(init_luma, 0.0)
    exact_l = hist_l
    max_rel_drift = 0.0
    for step in range(10):
        cur_l = alt_luma if (step % 2 == 0) else init_luma
        blend_k = _karis_fwd(hist_l) + (_karis_fwd(cur_l) - _karis_fwd(hist_l)) * alpha
        recon_l = _karis_inv(blend_k)
        if use_deja_antidrift and recon_l > 1.0e-4:
            lo = min(hist_l, cur_l)
            hi = max(hist_l, cur_l)
            target_l = min(max(hist_l + (cur_l - hist_l) * alpha, lo), hi)
            corr = min(max(target_l / recon_l, 0.90), 1.25)
            recon_l *= corr
        recon_l = recon_l * (1.0 + injected_step_bias) * spec_mv_gain
        exact_l = exact_l + (cur_l - exact_l) * alpha
        rel_drift = abs(recon_l - exact_l) / max(exact_l, 1.0e-4)
        max_rel_drift = max(max_rel_drift, rel_drift)
        hist_l = max(recon_l, 0.0)
    return max_rel_drift, hist_l, exact_l


def simulate_10_step_froxel_albedo(
    cur_rad: float,
    prev_rad: float,
    cur_ext: float,
    prev_ext: float,
    w_hist: float,
) -> tuple[float, float]:
    """Mirrors Wave 73 FroxelPass albedo-preserving blend; returns (max_albedo_overshoot, max_luma_overshoot)."""
    max_alb_over = 0.0
    max_lum_over = 0.0
    p_ext = prev_ext
    p_rgb = prev_rad * prev_ext
    for step in range(10):
        c_ext = cur_ext if (step % 2 == 0) else prev_ext
        c_rad = cur_rad if (step % 2 == 0) else prev_rad
        c_rgb = c_rad * c_ext
        b_ext = c_ext + (p_ext - c_ext) * w_hist
        p_rad_eff = p_rgb / p_ext if p_ext > 1.0e-6 else c_rad
        rad_min = min(c_rad, p_rad_eff)
        rad_max = max(c_rad, p_rad_eff)
        b_rad = min(max(c_rad + (p_rad_eff - c_rad) * w_hist, rad_min), rad_max)
        raw_rgb = c_rgb + (p_rgb - c_rgb) * w_hist
        alb_rgb = b_rad * b_ext
        safe_rgb = raw_rgb + (alb_rgb - raw_rgb) * 0.65
        lo_rgb = min(c_rgb, p_rgb)
        hi_rgb = max(c_rgb, p_rgb)
        if safe_rgb > 1.0e-6 and (safe_rgb < lo_rgb or safe_rgb > hi_rgb):
            safe_rgb *= min(max(safe_rgb, lo_rgb), hi_rgb) / safe_rgb
        eff_alb = safe_rgb / max(b_ext, 1.0e-6)
        max_alb_over = max(max_alb_over, max(0.0, eff_alb - max(cur_rad, prev_rad)))
        max_lum_over = max(max_lum_over, max(0.0, safe_rgb - hi_rgb), max(0.0, lo_rgb - safe_rgb))
        p_ext = b_ext
        p_rgb = safe_rgb
    return max_alb_over, max_lum_over


def test_shader_and_header_contracts() -> None:
    froxel = (ROOT / "src/d3d12_bridge/shaders/FroxelPass.hlsl").read_text(encoding="utf-8")
    rawlss = (ROOT / "src/d3d12_bridge/shaders/Rawlss5Pass.hlsl").read_text(encoding="utf-8")
    tsr = (ROOT / "src/d3d12_bridge/shaders/RawTsrUpscaleSim.hlsl").read_text(encoding="utf-8")
    pacing = (ROOT / "src/d3d12_bridge/RawFramePacing.h").read_text(encoding="utf-8")
    host = (ROOT / "src/d3d12_bridge/NativeDx12Host.h").read_text(encoding="utf-8")

    for tok in ("Wave 73:", "albBlendRgb", "radMin", "radMax", "minScatL", "maxScatL"):
        assert tok in froxel, f"Missing FroxelPass Wave 73 token: {tok}"
    for tok in ("Wave 73:", "maxChromaEnv", "blendedYConserv", "antiDriftGate", "rawDriftCorr"):
        assert tok in rawlss, f"Missing Rawlss5Pass Wave 73 token: {tok}"
    for tok in ("Wave 73:", "maxChromaEnv", "blendedYccEnv", "antiDriftGate", "driftCorr"):
        assert tok in tsr, f"Missing RawTsrUpscaleSim Wave 73 token: {tok}"
    for tok in (
        "kFixedSimHz",
        "ResolveFramePacingPolicy",
        "DeterministicFramePacer",
        "FramePacingTelemetry",
        "VerifyTenStepTemporalAntiDrift",
    ):
        assert tok in pacing, f"Missing RawFramePacing.h token: {tok}"
    for tok in ('#include "RawFramePacing.h"', "FramePacingPolicy pacingPolicy", "ResolveFramePacingPolicy(ep)"):
        assert tok in host, f"Missing NativeDx12Host.h Wave 73 token: {tok}"


def test_deja_10_step_energy_conservation_and_planted_controls() -> None:
    # 1. Prove naive Karis blending suffers Jensen darkening on moving high-contrast foliage
    naive_drift, naive_fin, exact_fin = simulate_10_step_karis(0.25, 2.25, 0.28, use_deja_antidrift=False)
    assert naive_drift > 0.15, f"Expected Jensen darkening >15% without guard, got {naive_drift:.4f}"
    assert naive_fin < exact_fin * 0.85

    # 2. Wave 73 Deja null-space guard slashes 10-step drift dramatically
    w73_drift, w73_fin, w73_exact = simulate_10_step_karis(0.35, 1.45, 0.28, use_deja_antidrift=True)
    assert w73_drift < 0.02, f"Expected Wave 73 drift <2%, got {w73_drift:.4f}"
    assert abs(w73_fin - w73_exact) / w73_exact < 0.02

    # 3. Steady-state exact fixed point (0.0 drift)
    ss_drift, _, _ = simulate_10_step_karis(0.85, 0.85, 0.25, use_deja_antidrift=True)
    assert ss_drift < 1.0e-9, f"Expected steady-state drift <1e-9, got {ss_drift}"

    # 4. Froxel albedo-preserving 10-step density transition has 0 albedo & luminance overshoot
    alb_over, lum_over = simulate_10_step_froxel_albedo(1.4, 0.4, 0.008, 0.001, 0.72)
    assert alb_over <= 1.0e-9 and lum_over <= 1.0e-9, f"Froxel overshoot detected: alb={alb_over}, lum={lum_over}"

    # 5. Planted-failure controls MUST trip cleanly:
    bias_drift, _, _ = simulate_10_step_karis(0.35, 1.45, 0.28, use_deja_antidrift=True, injected_step_bias=0.025)
    assert bias_drift > 0.08, f"Planted +2.5%/step bias must be caught (>8%), got {bias_drift:.4f}"

    spec_drift, _, _ = simulate_10_step_karis(0.50, 1.20, 0.30, use_deja_antidrift=True, spec_mv_gain=1.18)
    assert spec_drift > 0.20, f"Planted non-conservative specular MV gain must be caught (>20%), got {spec_drift:.4f}"


def test_clean_room_hygiene() -> None:
    strict_new_files = [
        ROOT / "src/d3d12_bridge/RawFramePacing.h",
        ROOT / "src/d3d12_bridge/NativeDx12Host.h",
        ROOT / "tests/wave73_antidrift_pacing_check.cpp",
        ROOT / "tests/test_wave73_antidrift_pacing.py",
    ]
    modified_shaders = [
        ROOT / "src/d3d12_bridge/shaders/FroxelPass.hlsl",
        ROOT / "src/d3d12_bridge/shaders/Rawlss5Pass.hlsl",
        ROOT / "src/d3d12_bridge/shaders/RawTsrUpscaleSim.hlsl",
    ]
    forbidden = "zen" + "tropy"
    for path in strict_new_files:
        text = path.read_text(encoding="utf-8")
        assert "\u2014" not in text, f"Em dash found in {path}"
        assert forbidden not in text.lower(), f"Forbidden token found in {path}"
    for path in modified_shaders:
        text = path.read_text(encoding="utf-8")
        assert forbidden not in text.lower(), f"Forbidden token found in {path}"
        for line in text.splitlines():
            if "Wave 73" in line:
                assert "\u2014" not in line, f"Em dash found in Wave 73 line of {path}"


def main() -> int:
    tests = [
        test_shader_and_header_contracts,
        test_deja_10_step_energy_conservation_and_planted_controls,
        test_clean_room_hygiene,
    ]
    for fn in tests:
        fn()
        print(f"  [PASS] {fn.__name__}")
    print(f"=== test_wave73_antidrift_pacing.py: {len(tests)}/{len(tests)} PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
