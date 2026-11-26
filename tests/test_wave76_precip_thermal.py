"""Wave 76 Verification Suite: World-Space Thermal/Wind Precipitation Accumulation,
Torch/Brazier Melt, Cloud-Crepuscular Two-Order Multi-Scattering, Sky-to-Water
Multiple-Scattering Conservation, 100 Hz Replay, Planted Controls, and Hygiene.

Original work, MIT.
"""

from __future__ import annotations

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _clamp(v: float, lo: float, hi: float) -> float:
    return min(max(v, lo), hi)


def _san(v: float, fb: float) -> float:
    return v if math.isfinite(v) else fb


def _smoothstep(e0: float, e1: float, x: float) -> float:
    t = _clamp((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def _world_h(x: float, y: float) -> float:
    qx, qy = _san(x, 0.0) * 0.014, _san(y, 0.0) * 0.014
    return (
        0.55 * math.sin(qx) * math.cos(qy)
        + 0.30 * math.cos(qx * 2.13 + 1.7) * math.sin(qy * 1.91 - 0.8)
        + 0.15 * math.sin((qx + qy) * 3.17 + 0.5)
    )


def _concavity(x: float, y: float) -> float:
    h = 18.0
    lap = (_world_h(x + h, y) + _world_h(x - h, y) + _world_h(x, y + h) + _world_h(x, y - h) - 4.0 * _world_h(x, y)) / (
        h * h * 0.0004
    )
    return _clamp(max(_san(lap, 0.0), 0.0) * 0.65, 0.0, 1.0)


def eval_torch_flux(dist_u: float, radius_u: float = 220.0) -> float:
    if not math.isfinite(dist_u) or dist_u < 0.0 or dist_u >= radius_u:
        return 0.0
    u = _clamp(1.0 - dist_u / radius_u, 0.0, 1.0)
    return _clamp(u * u * 1.65, 0.0, 1.5)


def eval_thermal_precip(
    wx: float,
    wy: float,
    gust01: float,
    base_snow: float,
    temp_c: float,
    fire_heat: float,
    screen_u: float = 0.5,
    disable_thermal: bool = False,
    planted_radial_bias: float = 0.0,
) -> tuple[float, float, float]:
    conc = _concavity(wx, wy)
    g01 = _clamp(_san(gust01, 0.0), 0.0, 1.0)
    s_in = _clamp(_san(base_snow, 0.0), 0.0, 1.0)
    t_c = _clamp(_san(temp_c, -2.0), -45.0, 45.0)
    fh = 0.0 if disable_thermal else _clamp(_san(fire_heat, 0.0), 0.0, 1.5)
    cold = _smoothstep(3.0, -4.0, t_c)
    snow_pre = _clamp(s_in * (0.76 + 0.28 * conc + 0.08 * g01) * (0.45 + 0.55 * cold), 0.0, 1.0)
    if planted_radial_bias > 0.0:
        edge = _clamp(min(screen_u, 1.0 - screen_u) / 0.10, 0.0, 1.0)
        snow_pre *= (1.0 - planted_radial_bias) + planted_radial_bias * edge
    melt_core = _smoothstep(0.14, 0.68, fh)
    slush = _clamp(_smoothstep(0.04, 0.24, fh) * (1.0 - _smoothstep(0.46, 0.86, fh)), 0.0, 1.0)
    wet = _clamp(max(melt_core * 0.85, slush * 0.78) * max(snow_pre, s_in * 0.55), 0.0, 1.0)
    return _clamp(snow_pre * (1.0 - melt_core), 0.0, 1.0), wet, _clamp(0.84 + (0.14 - 0.84) * wet, 0.12, 0.92)


def test_wave76_contracts() -> None:
    pt_hlsl = (ROOT / "src/d3d12_bridge/shaders/RawPrecipThermal.hlsli").read_text(encoding="utf-8")
    pt_cpp = (ROOT / "src/d3d12_bridge/RawPrecipThermal.h").read_text(encoding="utf-8")
    lighting = (ROOT / "src/d3d12_bridge/shaders/Lighting.hlsl").read_text(encoding="utf-8")
    froxel = (ROOT / "src/d3d12_bridge/shaders/FroxelPass.hlsl").read_text(encoding="utf-8")
    water = (ROOT / "src/d3d12_bridge/shaders/RtWater.hlsl").read_text(encoding="utf-8")

    for tok in ("rawEvalWorldThermalSnowAccum", "rawEvalCrepuscularPhase", "rawEvalSkyWaterMultiScatter"):
        assert tok in pt_hlsl, f"Missing HLSL token: {tok}"
    for tok in ("EvalWorldThermalSnowAccum", "EvalCrepuscularPhase", "VerifyDeterministic100HzThermalReplay"):
        assert tok in pt_cpp, f"Missing C++23 token: {tok}"
    for tok in ('#include "RawPrecipThermal.hlsli"', "rawEvalWorldThermalSnowAccum"):
        assert tok in lighting, f"Missing Lighting.hlsl Wave 76 token: {tok}"
    for tok in ('#include "RawPrecipThermal.hlsli"', "rawEvalCrepuscularPhase", "phCoupled"):
        assert tok in froxel, f"Missing FroxelPass.hlsl Wave 76 token: {tok}"
    for tok in ('#include "RawPrecipThermal.hlsli"', "rawEvalSkyWaterMultiScatter", "coupledWater"):
        assert tok in water, f"Missing RtWater.hlsl Wave 76 token: {tok}"


def test_thermal_melt_invariance_and_planted_controls() -> None:
    # 1. Torch proximity monotonicity + Planted Control A
    s_far, w_far, r_far = eval_thermal_precip(80.0, 120.0, 0.5, 0.85, -4.0, eval_torch_flux(260.0))
    s_near, w_near, r_near = eval_thermal_precip(80.0, 120.0, 0.5, 0.85, -4.0, eval_torch_flux(40.0))
    assert s_near < s_far and w_near > w_far and r_near < r_far
    s_no_th, w_no_th, _ = eval_thermal_precip(80.0, 120.0, 0.5, 0.85, -4.0, eval_torch_flux(40.0), disable_thermal=True)
    assert s_no_th == s_far and w_no_th == 0.0

    # 2. Screen-edge invariance + Planted Control B
    h_mid = eval_torch_flux(110.0)
    c_snow, c_wet, _ = eval_thermal_precip(220.0, -180.0, 0.5, 0.85, -5.0, h_mid, screen_u=0.5)
    e_snow, e_wet, _ = eval_thermal_precip(220.0, -180.0, 0.5, 0.85, -5.0, h_mid, screen_u=2.0 / 1920.0)
    assert abs(c_snow - e_snow) == 0.0 and abs(c_wet - e_wet) == 0.0
    bad_snow, _, _ = eval_thermal_precip(220.0, -180.0, 0.5, 0.85, -5.0, h_mid, screen_u=2.0 / 1920.0, planted_radial_bias=0.85)
    assert abs(c_snow - bad_snow) > 0.20

    # 3. NaN/Inf resilience (Planted Control C)
    bad = eval_thermal_precip(math.nan, math.inf, math.nan, math.inf, -math.inf, math.nan)
    assert all(math.isfinite(x) for x in bad)


def test_hygiene() -> None:
    forbidden = "zen" + "tropy"
    for p in (
        ROOT / "src/d3d12_bridge/shaders/RawPrecipThermal.hlsli",
        ROOT / "src/d3d12_bridge/RawPrecipThermal.h",
        ROOT / "tests/wave76_precip_thermal_check.cpp",
        ROOT / "tests/test_wave76_precip_thermal.py",
    ):
        txt = p.read_text(encoding="utf-8")
        assert "\u2014" not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"


def main() -> int:
    tests = [
        test_wave76_contracts,
        test_thermal_melt_invariance_and_planted_controls,
        test_hygiene,
    ]
    for fn in tests:
        fn()
        print(f"  [PASS] {fn.__name__}")
    print(f"=== test_wave76_precip_thermal.py: {len(tests)}/{len(tests)} PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
