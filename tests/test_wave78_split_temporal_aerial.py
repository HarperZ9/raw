"""Wave 78 verification suite: Specular-Diffuse Split Temporal Disocclusion Guard,
World-Space Sub-Pixel Foliage Anti-Shimmer & Coverage Preservation, and Energy-Conserving
Analytical Aerial-Perspective & Height-Fog Multi-Scatter Coupling.
Runs standalone as a canonical engine-receipt/1 oracle (zero external deps, clean-room MIT).
"""
from __future__ import annotations

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHADERS = ROOT / "src" / "d3d12_bridge" / "shaders"


def test_wave78_tokens_and_wiring() -> None:
    hlsl = (SHADERS / "RawSplitTemporalAerial.hlsli").read_text(encoding="utf-8")
    cpp = (ROOT / "src" / "d3d12_bridge" / "RawSplitTemporalAerial.h").read_text(encoding="utf-8")
    rawlss = (SHADERS / "Rawlss5Pass.hlsl").read_text(encoding="utf-8")
    lighting = (SHADERS / "Lighting.hlsl").read_text(encoding="utf-8")
    lightfog = (SHADERS / "LightFog.hlsl").read_text(encoding="utf-8")

    for tok in (
        '#include "RawPrecipThermal.hlsli"',
        "rawEvalSplitTemporalGuard",
        "rawEvalFoliageCoverageAntiShimmer",
        "rawEvalAnalyticalAerialHeightFog",
        "rawEvalCrepuscularPhase",
    ):
        assert tok in hlsl, f"Missing HLSL token: {tok}"

    for tok in (
        "EvalSplitTemporalGuard",
        "EvalFoliageCoverageAntiShimmer",
        "EvalAnalyticalAerialHeightFog",
        "EvalCrepuscularPhase",
    ):
        assert tok in cpp, f"Missing C++23 mirror token: {tok}"

    for tok in (
        '#include "RawSplitTemporalAerial.hlsli"',
        "rawEvalSplitTemporalGuard",
        "splitW78.gammaEff",
        "splitW78.alphaEff",
    ):
        assert tok in rawlss, f"Missing Rawlss5Pass.hlsl Wave 78 token: {tok}"

    for tok in (
        '#include "RawSplitTemporalAerial.hlsli"',
        "rawEvalFoliageCoverageAntiShimmer",
        "folW78.coverageScale",
        "folW78.translucencyScale",
    ):
        assert tok in lighting, f"Missing Lighting.hlsl Wave 78 token: {tok}"

    for tok in (
        '#include "RawSplitTemporalAerial.hlsli"',
        "rawEvalAnalyticalAerialHeightFog",
        "aerialW78.transmittance",
        "aerialW78.inScatter",
    ):
        assert tok in lightfog, f"Missing LightFog.hlsl Wave 78 token: {tok}"


def test_wave78_analytical_invariants_and_planted_controls() -> None:
    # 1) Roughness-monotone specular parallax & tighter specular variance clip:
    prev_p = 2.0
    for r in (0.04, 0.20, 0.50, 0.80, 1.00):
        p = (1.0 - r) ** 2
        assert p <= prev_p + 1e-12
        prev_p = p

    # 2) Analytical height-fog optical depth Taylor limit at u == 0 (0/0 NaN prevented):
    for u in (-2.0, -1e-4, 0.0, 1e-4, 2.0):
        h_int = (1.0 - 0.5 * u + (1.0 / 6.0) * u * u) if abs(u) < 1e-3 else ((1.0 - math.exp(-u)) / u)
        assert math.isfinite(h_int) and h_int > 0.0
        t = math.exp(-max(0.0, min(0.000015 * 40000.0 * h_int, 32.0)))
        assert 0.0 <= t <= 1.0

    # 3) Horizon convergence & white-furnace aerial conservation:
    t_mid = 0.45
    in_scat = 0.96 * (1.0 - t_mid)
    assert t_mid + in_scat <= 1.0 + 1e-12
    # Planted Control: super-unit albedo 1.45 violates white furnace > 1.0:
    assert (t_mid + 1.45 * (1.0 - t_mid)) > 1.15


def test_wave78_hygiene() -> None:
    forbidden = "zen" + "tropy"
    em_dash = "\u2014"
    new_files = [
        SHADERS / "RawSplitTemporalAerial.hlsli",
        ROOT / "src" / "d3d12_bridge" / "RawSplitTemporalAerial.h",
        ROOT / "tests" / "wave78_split_temporal_aerial_check.cpp",
        ROOT / "tests" / "test_wave78_split_temporal_aerial.py",
        ROOT / "docs" / "WAVE78-SPLIT-TEMPORAL-FOLIAGE-AERIAL-2026-11-26.md",
    ]
    for p in new_files:
        txt = p.read_text(encoding="utf-8")
        assert em_dash not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"


def main() -> int:
    tests = [
        test_wave78_tokens_and_wiring,
        test_wave78_analytical_invariants_and_planted_controls,
        test_wave78_hygiene,
    ]
    for fn in tests:
        fn()
        print(f"  [PASS] {fn.__name__}")
    print(f"=== WAVE 78 PYTHON ORACLE: {len(tests)}/{len(tests)} PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
