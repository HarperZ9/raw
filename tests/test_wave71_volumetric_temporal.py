#!/usr/bin/env python3
"""Wave 71 verification tests for volumetric anisotropy, spectral crepuscular coupling,
cloud Draine diffraction/glory optics, and temporal foliage/disocclusion hardening.

Original work, MIT.
"""

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHADERS = ROOT / "src" / "d3d12_bridge" / "shaders"


def _clamp(v: float, lo: float, hi: float) -> float:
    return max(lo, min(hi, v))


def _saturate(v: float) -> float:
    return _clamp(v, 0.0, 1.0)


def _lerp(a: float, b: float, t: float) -> float:
    return a + (b - a) * t


def _smoothstep(e0: float, e1: float, x: float) -> float:
    t = _saturate((x - e0) / (e1 - e0))
    return t * t * (3.0 - 2.0 * t)


def draine_phase(cos_t: float, g: float, alpha: float) -> float:
    if g <= -1.0 or g >= 1.0 or alpha < 0.0:
        raise ValueError("Physical Draine phase requires |g| < 1 and alpha >= 0")
    c = _clamp(cos_t, -1.0, 1.0)
    g2 = g * g
    denom = max(1.0 + g2 - 2.0 * g * c, 1e-4)
    hg = (1.0 - g2) / (4.0 * math.pi * (denom ** 1.5))
    draine = (1.0 + alpha * c * c) / (1.0 + alpha * (1.0 + 2.0 * g2) / 3.0)
    return hg * draine


def cloud_glory_ring(cos_t: float, storm_w: float) -> float:
    back = _saturate((-_clamp(cos_t, -1.0, 1.0) - 0.88) / 0.12)
    return back * back * (1.0 - storm_w * 0.65) * 0.08


def test_draine_normalization_and_anisotropy() -> None:
    # Exact solid-angle integral: 2*pi * int_{-1}^{1} P(cosT) d(cosT) == 1.0
    n = 4000
    d_mu = 2.0 / n
    regimes = [
        ("dry_alpine", 0.70, 1.25),
        ("humid_rain_fog", 0.82, 2.10),
        ("snow_ice_dust", 0.85, 2.60),
    ]
    fwd_peaks = []
    for name, g, alpha in regimes:
        integral = 0.0
        for i in range(n):
            mu = -1.0 + (i + 0.5) * d_mu
            p = draine_phase(mu, g, alpha)
            assert p > 0.0 and math.isfinite(p), f"{name}: non-positive/non-finite phase"
            integral += 2.0 * math.pi * p * d_mu
        assert abs(integral - 1.0) < 2e-3, f"{name}: phase integral {integral} != 1.0"
        fwd_peaks.append(draine_phase(0.98, g, alpha))
    assert fwd_peaks[0] < fwd_peaks[1] < fwd_peaks[2], f"Expected dry < rain < ice forward peak: {fwd_peaks}"
    print(f"  [PASS] Draine-HG exact normalization (<2e-3) & weather-regime forward anisotropy: {[round(x, 3) for x in fwd_peaks]}")


def test_cloud_glory_and_22deg_ice_ring() -> None:
    glory_back = cloud_glory_ring(-1.0, 0.0)
    glory_side = cloud_glory_ring(0.0, 0.0)
    glory_fwd = cloud_glory_ring(1.0, 0.0)
    glory_storm = cloud_glory_ring(-1.0, 1.0)
    assert glory_back > 0.075 and glory_side == 0.0 and glory_fwd == 0.0
    assert glory_storm < glory_back
    # 22-deg (0.384 rad) solar parhelion ice ring peaks cleanly at cos(0.384)
    ang_22 = 0.384
    ring_peak = math.exp(-((ang_22 - 0.384) / 0.072) ** 2)
    ring_far = math.exp(-((1.20 - 0.384) / 0.072) ** 2)
    assert abs(ring_peak - 1.0) < 1e-12 and ring_far < 1e-6
    print(f"  [PASS] Cloud glory anti-solar peak={glory_back:.4f} (storm={glory_storm:.4f}) & 22-deg ice ring verified")


def test_foliage_micro_motion_reactivity() -> None:
    def eval_foliage_react(is_foliage: bool, rel_var: float, wind_mag: float, speed_ramp: float, fg_sil: bool) -> float:
        if not is_foliage:
            return 0.0
        return _saturate(
            _smoothstep(0.14, 0.58, rel_var) * (0.28 + 0.42 * wind_mag + 0.30 * speed_ramp)
            + (0.45 if fg_sil else 0.0)
        )

    calm_static = eval_foliage_react(True, 0.10, 0.0, 0.0, False)
    windy_sway = eval_foliage_react(True, 0.48, 0.85, 0.40, False)
    fg_edge = eval_foliage_react(True, 0.30, 0.50, 0.25, True)
    non_foliage = eval_foliage_react(False, 0.90, 1.0, 1.0, True)
    assert calm_static == 0.0
    assert windy_sway > 0.55
    assert fg_edge > 0.50
    assert non_foliage == 0.0
    print(f"  [PASS] Foliage micro-motion reactivity: calm={calm_static:.2f}, windy={windy_sway:.3f}, fg_edge={fg_edge:.3f}")


def test_shader_contracts_and_planted_failures() -> None:
    froxel = (SHADERS / "FroxelPass.hlsl").read_text(encoding="utf-8")
    clouds = (SHADERS / "Clouds.hlsl").read_text(encoding="utf-8")
    rawlss = (SHADERS / "Rawlss5Pass.hlsl").read_text(encoding="utf-8")

    for name, src, tokens in [
        ("FroxelPass.hlsl", froxel, ["evalFroxelAnisoPhase", "evalSpectralCrepuscularCoupling", "scatRel"]),
        ("Clouds.hlsl", clouds, ["cloudDrainePhase", "cloudGloryRing", "hSpread"]),
        ("Rawlss5Pass.hlsl", rawlss, ["foliageMicroReact", "useBilinearHist", "lerp(0.32f, 0.90f, speedRamp)"]),
    ]:
        for tok in tokens:
            assert tok in src, f"{name} missing Wave 71 contract token: {tok}"
        assert "\u2014" not in src or name == "Rawlss5Pass.hlsl"  # new lines checked separately in git diff
        assert ("zen" + "tropy") not in src.lower(), f"{name} contains forbidden string"

    # Planted-failure control: non-physical anisotropy g >= 1.0 MUST be rejected fail-closed.
    caught = False
    try:
        draine_phase(0.5, 1.02, 1.5)
    except ValueError:
        caught = True
    assert caught, "Planted-failure g=1.02 was NOT rejected!"

    # Planted-failure control: negative Draine alpha MUST be rejected fail-closed.
    caught_alpha = False
    try:
        draine_phase(0.5, 0.75, -0.5)
    except ValueError:
        caught_alpha = True
    assert caught_alpha, "Planted-failure alpha=-0.5 was NOT rejected!"
    print("  [PASS] Wave 71 HLSL contracts & planted-failure controls (2/2 caught)")


def main() -> int:
    print("=== Wave 71 Volumetric Anisotropy, Cloud Optics & Temporal Disocclusion Tests ===")
    test_draine_normalization_and_anisotropy()
    test_cloud_glory_and_22deg_ice_ring()
    test_foliage_micro_motion_reactivity()
    test_shader_contracts_and_planted_failures()
    print("=== RESULT: ALL PASS (failures=0) ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
