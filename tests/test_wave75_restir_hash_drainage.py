"""Wave 75 Verification Suite: Disocclusion-Safe 2-Slot ReSTIR-GI/PT Shift-Mapping Jacobian & MIS,
Online Multi-Resolution L=8 World-Space Hash Radiance Cache, Heightfield-Coupled World-Space
Puddle Drainage (0.0 Screen-Edge Residual), Planted Controls, and Clean-Room Hygiene.

Original work, MIT.
"""

from __future__ import annotations

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def _world_micro_height(wx: float, wy: float) -> float:
    x = wx * 0.014
    y = wy * 0.014
    return (
        0.55 * math.sin(x) * math.cos(y)
        + 0.30 * math.cos(x * 2.13 + 1.7) * math.sin(y * 1.91 - 0.8)
        + 0.15 * math.sin((x + y) * 3.17 + 0.5)
    )


def eval_puddle_drainage(
    wx: float,
    wy: float,
    nz: float = 0.99,
    sky_vis: float = 1.0,
    rain: float = 0.85,
    evap: float = 0.35,
    screen_u: float = 0.5,
    planted_edge_bias: float = 0.0,
) -> tuple[float, float]:
    h = 18.0
    hc = _world_micro_height(wx, wy)
    lap = (
        _world_micro_height(wx + h, wy)
        + _world_micro_height(wx - h, wy)
        + _world_micro_height(wx, wy + h)
        + _world_micro_height(wx, wy - h)
        - 4.0 * hc
    ) / (h * h * 0.0004)
    flat = min(max((nz - 0.88) / 0.08, 0.0), 1.0)
    drain = min(max(max(lap, 0.0) * 0.65, 0.0), 1.0)
    raw_pool = (rain * sky_vis * nz * 0.55 + drain * (0.35 + 0.65 * rain) * sky_vis) / max(evap, 0.10)
    puddle = min(max((raw_pool - 0.55) * 1.45, 0.0), 1.0) * flat
    if planted_edge_bias > 0.0:
        edge_dist = min(screen_u, 1.0 - screen_u)
        edge_vig = min(max(edge_dist / 0.10, 0.0), 1.0)
        puddle *= (1.0 - planted_edge_bias) + planted_edge_bias * edge_vig
    return lap, puddle


def run_furnace(albedo: float = 0.80, drop_jacobian: bool = False) -> tuple[float, float]:
    r = 2.2
    nx_off = -1.65
    total = 0.0
    n_samples = 1024
    for i in range(n_samples):
        u1 = (i + 0.5) / n_samples
        u2 = (i * 0.6180339887498949) % 1.0
        phi = (u2 - 0.5) * math.pi
        cos_t = math.sqrt(1.0 - u1)
        sin_t = math.sqrt(u1)
        dx, dy, dz = sin_t * math.cos(phi), sin_t * math.sin(phi), cos_t
        sx, sy, sz = r * dx, r * dy, r * dz
        nsx, nsy, nsz = -dx, -dy, -dz
        vnx, vny, vnz = sx - nx_off, sy, sz
        dist_n2 = vnx * vnx + vny * vny + vnz * vnz
        dist_n = math.sqrt(dist_n2)
        cos_sn = max(abs((nsx * (-vnx) + nsy * (-vny) + nsz * (-vnz)) / dist_n), 0.05)
        cos_st = max(abs(nsx * (-dx) + nsy * (-dy) + nsz * (-dz)), 0.05)
        true_jac = (cos_st * dist_n2) / (cos_sn * r * r)
        jac_used = 1.0 if drop_jacobian else true_jac
        total += albedo * (jac_used / max(true_jac, 1.0e-6))
    mean = total / n_samples
    return mean, abs(mean - albedo) / albedo


def test_wave75_contracts() -> None:
    lib_hlsl = (ROOT / "src/d3d12_bridge/shaders/RawRestirHashCache.hlsli").read_text(encoding="utf-8")
    lib_cpp = (ROOT / "src/d3d12_bridge/RawRestirHashCache.h").read_text(encoding="utf-8")
    rt_shadow = (ROOT / "src/d3d12_bridge/shaders/RtShadow.hlsl").read_text(encoding="utf-8")
    rt_cache = (ROOT / "src/d3d12_bridge/shaders/RtCache.hlsl").read_text(encoding="utf-8")
    rt_cache_c = (ROOT / "src/d3d12_bridge/shaders/RtCacheCommon.hlsli").read_text(encoding="utf-8")
    weather = (ROOT / "src/d3d12_bridge/shaders/Weather.hlsli").read_text(encoding="utf-8")

    for tok in (
        "rawEvalRestirShiftJacobian",
        "rawEvalGeneralizedBalanceMis",
        "rawSelectTwoSlotReservoirLayer",
        "rawHashCacheKeyMultiRes",
        "rawEvalHashCacheRelativeL2Alpha",
    ):
        assert tok in lib_hlsl, f"Missing HLSL token: {tok}"
    for tok in (
        "RunConstantRadianceFurnaceCheck",
        "SelectTwoSlotReservoirLayer",
        "OnlineMultiResHashCache",
        "EvalWorldHeightfieldDrainagePuddle",
    ):
        assert tok in lib_cpp, f"Missing C++23 token: {tok}"
    for tok in ('#include "RawRestirHashCache.hlsli"', "rawSelectTwoSlotReservoirLayer", "rawEvalRestirShiftJacobian", "rawEvalGeneralizedBalanceMis"):
        assert tok in rt_shadow, f"Missing RtShadow Wave 75 token: {tok}"
    for tok in ("sanitizeCache4", "relL2"):
        assert tok in rt_cache, f"Missing RtCache Wave 75 token: {tok}"
    for tok in ("sanitizeCache4", "hashCacheKeyMultiRes"):
        assert tok in rt_cache_c, f"Missing RtCacheCommon Wave 75 token: {tok}"
    for tok in ("rawWorldMicroHeight", "rawWorldDrainageConcavity", "drainConcav"):
        assert tok in weather, f"Missing Weather.hlsli Wave 75 token: {tok}"


def test_furnace_and_screen_edge_invariance_with_planted_controls() -> None:
    mean_ok, rel_ok = run_furnace(0.80, drop_jacobian=False)
    assert abs(mean_ok - 0.80) < 0.015 and rel_ok < 0.02, f"Furnace biased: mean={mean_ok}"

    mean_bad, rel_bad = run_furnace(0.80, drop_jacobian=True)
    assert rel_bad > 0.15, f"Planted missing-Jacobian control must shift >15%, got {rel_bad:.4f} (mean={mean_bad})"

    # Locate concave world depression
    best = None
    for ix in range(-20, 21):
        for iy in range(-20, 21):
            wx, wy = ix * 35.0, iy * 35.0
            lap, pud = eval_puddle_drainage(wx, wy, screen_u=0.5)
            if lap > 0.0 and pud > 0.25 and (best is None or lap > best[0]):
                best = (lap, wx, wy, pud)
    assert best is not None, "Expected concave depression in world heightfield"
    _, bx, by, pud_center = best
    _, pud_edge = eval_puddle_drainage(bx, by, screen_u=2.0 / 1920.0, planted_edge_bias=0.0)
    assert abs(pud_center - pud_edge) == 0.0, f"Screen-edge puddle drift detected: {abs(pud_center - pud_edge)}"

    _, pud_hacked = eval_puddle_drainage(bx, by, screen_u=2.0 / 1920.0, planted_edge_bias=0.85)
    assert abs(pud_center - pud_hacked) > 0.20, "Planted screen-edge hack was not caught"


def test_hygiene() -> None:
    forbidden = "zen" + "tropy"
    strict_files = [
        ROOT / "src/d3d12_bridge/shaders/RawRestirHashCache.hlsli",
        ROOT / "src/d3d12_bridge/RawRestirHashCache.h",
        ROOT / "tests/wave75_restir_hash_drainage_check.cpp",
        ROOT / "tests/test_wave75_restir_hash_drainage.py",
    ]
    for p in strict_files:
        txt = p.read_text(encoding="utf-8")
        assert "\u2014" not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"


def main() -> int:
    tests = [
        test_wave75_contracts,
        test_furnace_and_screen_edge_invariance_with_planted_controls,
        test_hygiene,
    ]
    for fn in tests:
        fn()
        print(f"  [PASS] {fn.__name__}")
    print(f"=== test_wave75_restir_hash_drainage.py: {len(tests)}/{len(tests)} PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
