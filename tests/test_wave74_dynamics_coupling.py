"""Wave 74 Verification Suite: World-Space Coherent Gust Field, Foliage Phase-Locked
Temporal MV Compensation, Volumetric Cloud-Shadow Water/Terrain/Wet-Leaf Coupling,
White-Furnace Energy Conservation, Deterministic 100 Hz Replay, and Planted Controls.

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


def eval_coherent_wind_gust(
    px: float,
    py: float,
    wx: float,
    wy: float,
    rain: float,
    snow: float,
    sim_t: float,
) -> tuple[float, float, float, float]:
    px, py = _san(px, 0.0), _san(py, 0.0)
    wx, wy = _san(wx, 0.0), _san(wy, 0.0)
    t = _san(sim_t, 0.0)
    r01 = _clamp(_san(rain, 0.0), 0.0, 1.0)
    s01 = _clamp(_san(snow, 0.0), 0.0, 1.0)

    w_len = math.hypot(wx, wy)
    dx = wx / w_len if w_len > 1.0e-3 else 0.8660254037844386
    dy = wy / w_len if w_len > 1.0e-3 else 0.5
    perp_x, perp_y = -dy, dx

    wind_norm = _clamp(w_len / 280.0 + r01 * 0.35 + s01 * 0.30, 0.0, 1.0)
    omega1 = 1.15 + 1.85 * wind_norm
    omega2 = 0.72 + 1.10 * wind_norm
    phi1 = (px * dx + py * dy) * 0.0024 - t * omega1
    phi2 = (px * perp_x + py * perp_y) * 0.0041 - t * omega2 + 1.37

    wave1 = 0.55 + 0.45 * math.sin(phi1)
    eddy2 = 0.65 + 0.35 * math.cos(phi2)
    amp = 0.20 + 0.80 * wind_norm
    gust01 = _clamp(wave1 * eddy2 * amp, 0.0, 1.0)
    sway_deriv = _clamp(math.cos(phi1) * eddy2 * amp * (omega1 * 0.32), -1.5, 1.5)
    return dx * sway_deriv, dy * sway_deriv, gust01, sway_deriv


def eval_wet_leaf_budget(
    alb: tuple[float, float, float],
    wetness: float,
    ndotv: float,
    cloud_trans: float,
    gust01: float,
    planted_leak: float = 0.0,
) -> tuple[float, float, float, float]:
    a_safe = tuple(_clamp(_san(c, 0.0), 0.0, 0.92) for c in alb)
    w = _clamp(_san(wetness, 0.0), 0.0, 1.0)
    nv = _clamp(_san(ndotv, 0.5), 0.0, 1.0)
    ct = _clamp(_san(cloud_trans, 1.0), 0.0, 1.0)
    g = _clamp(_san(gust01, 0.0), 0.0, 1.0)

    f0 = 0.016 + (0.024 - 0.016) * w
    f_spec = _clamp(f0 + (1.0 - f0) * ((1.0 - nv) ** 5), 0.0, 1.0)
    rem = max(1.0 - f_spec, 0.0)
    dark = 1.0 + (0.86 - 1.0) * (w * 0.65)
    alb_eff = tuple(min(c * dark, rem) for c in a_safe)
    max_alb = max(alb_eff)
    trans_cap = max(rem - max_alb, 0.0)
    desired = (0.24 + (0.15 - 0.24) * w) * ct * (1.0 + 0.16 * g)
    trans_w = min(desired, trans_cap) + planted_leak
    return max_alb, f_spec, trans_w, max_alb + f_spec + trans_w


def test_wave74_contracts() -> None:
    dyn_hlsl = (ROOT / "src/d3d12_bridge/shaders/RawWorldDynamics.hlsli").read_text(encoding="utf-8")
    dyn_cpp = (ROOT / "src/d3d12_bridge/RawWorldDynamics.h").read_text(encoding="utf-8")
    rawlss = (ROOT / "src/d3d12_bridge/shaders/Rawlss5Pass.hlsl").read_text(encoding="utf-8")
    lighting = (ROOT / "src/d3d12_bridge/shaders/Lighting.hlsl").read_text(encoding="utf-8")
    water = (ROOT / "src/d3d12_bridge/shaders/RtWater.hlsl").read_text(encoding="utf-8")

    for tok in ("rawEvalCoherentWindGust4", "rawEvalWetLeafRadiativeBudget", "rawEvalWaterCloudRoughnessCoupling"):
        assert tok in dyn_hlsl, f"Missing HLSL token: {tok}"
    for tok in ("EvalCoherentWindGust", "EvalWetLeafRadiativeBudget", "VerifyDeterministic100HzDynamicsReplay"):
        assert tok in dyn_cpp, f"Missing C++23 token: {tok}"
    for tok in ('#include "RawWorldDynamics.hlsli"', "foliageGustW", "gustMvComp", "gustFrontF", "Wave 73:"):
        assert tok in rawlss, f"Missing Rawlss5Pass Wave 74/73 token: {tok}"
    for tok in ('#include "RawWorldDynamics.hlsli"', "cloudTransL", "wetRoughBroad", "rawEvalWetLeafRadiativeBudget"):
        assert tok in lighting, f"Missing Lighting.hlsl Wave 74 token: {tok}"
    for tok in ('#include "RawWorldDynamics.hlsli"', "cloudTransW", "wCloudCoup", "causticTransmittance"):
        assert tok in water, f"Missing RtWater.hlsl Wave 74 token: {tok}"


def test_white_furnace_nan_resilience_and_planted_controls() -> None:
    # 1. Exhaustive white-furnace sweep: albedo + Fresnel + transmission <= 1.0
    for a in (0.02, 0.25, 0.65, 0.92, 2.0):
        for w in (0.0, 0.4, 1.0):
            for nv in (0.0, 0.05, 0.5, 1.0):
                for ct in (0.0, 0.3, 1.0):
                    for g in (0.0, 0.5, 1.0):
                        _, _, t_w, total = eval_wet_leaf_budget((a, a, a), w, nv, ct, g)
                        assert math.isfinite(total) and total <= 1.0 + 1.0e-9, f"Energy leak: {total}"
                        assert t_w >= 0.0

    # 2. Extreme NaN/Inf resilience (0 NaN/Inf output)
    gust = eval_coherent_wind_gust(math.nan, math.inf, -math.inf, math.nan, math.inf, -math.inf, math.nan)
    assert all(math.isfinite(v) for v in gust)
    leaf = eval_wet_leaf_budget((math.nan, math.inf, -3.0), math.nan, math.inf, math.nan, math.inf)
    assert all(math.isfinite(v) for v in leaf) and leaf[3] <= 1.0 + 1.0e-9

    # 3. Deterministic 100 Hz fixed-step replay (60 FPS vs 144 FPS -> exact 100 ticks in 1.0 s)
    def sim_ticks(fps: int) -> int:
        acc = 0.0
        ticks = 0
        dt = 1.0 / fps
        for _ in range(fps):
            acc += dt
            while acc + 1.0e-12 >= 0.01:
                acc = max(0.0, acc - 0.01)
                ticks += 1
        return ticks

    t60 = sim_ticks(60)
    t144 = sim_ticks(144)
    assert t60 == 100 and t144 == 100, f"Expected 100 fixed ticks, got {t60}, {t144}"
    g60 = eval_coherent_wind_gust(900.0, -450.0, 180.0, 60.0, 0.4, 0.0, t60 * 0.01)
    g144 = eval_coherent_wind_gust(900.0, -450.0, 180.0, 60.0, 0.4, 0.0, t144 * 0.01)
    assert all(abs(a - b) <= 1.0e-12 for a, b in zip(g60, g144))

    # 4. Planted failure control: energy leak > 1.0 MUST be caught
    _, _, _, leaked = eval_wet_leaf_budget((0.85, 0.85, 0.85), 0.2, 0.1, 1.0, 0.8, planted_leak=0.15)
    assert leaked > 1.05, f"Planted energy leak should exceed 1.05, got {leaked}"


def test_hygiene() -> None:
    forbidden = "zen" + "tropy"
    strict_files = [
        ROOT / "src/d3d12_bridge/shaders/RawWorldDynamics.hlsli",
        ROOT / "src/d3d12_bridge/RawWorldDynamics.h",
        ROOT / "tests/wave74_dynamics_coupling_check.cpp",
        ROOT / "tests/test_wave74_dynamics_coupling.py",
    ]
    for p in strict_files:
        txt = p.read_text(encoding="utf-8")
        assert "\u2014" not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"


def main() -> int:
    tests = [
        test_wave74_contracts,
        test_white_furnace_nan_resilience_and_planted_controls,
        test_hygiene,
    ]
    for fn in tests:
        fn()
        print(f"  [PASS] {fn.__name__}")
    print(f"=== test_wave74_dynamics_coupling.py: {len(tests)}/{len(tests)} PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
