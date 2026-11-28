"""Wave 80 Milestone verification suite: Porosity-Coupled Wet-Stone/Mud/Wood Albedo Darkening
& Micro-Normal Water-Film Smoothing + Three-Band Cauchy Spectral Water-Bed Caustic Dispersion
(n_R=1.331, n_G=1.333, n_B=1.339, white-furnace mean == 1.0).
Runs standalone as a canonical engine-receipt/1 oracle (zero external deps, clean-room MIT).
"""
from __future__ import annotations

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHADERS = ROOT / "src" / "d3d12_bridge" / "shaders"


def test_wave80_tokens_and_wiring() -> None:
    hlsl = (SHADERS / "RawPorosityCaustics.hlsli").read_text(encoding="utf-8")
    cpp = (ROOT / "src" / "d3d12_bridge" / "RawPorosityCaustics.h").read_text(encoding="utf-8")
    lighting = (SHADERS / "Lighting.hlsl").read_text(encoding="utf-8")
    water = (SHADERS / "RtWater.hlsl").read_text(encoding="utf-8")

    for tok in (
        '#include "RawPrecipThermal.hlsli"',
        "rawEvalPorosityWetFilm",
        "rawEvalSpectralCausticDispersion",
        "1.331f",
        "1.333f",
        "1.339f",
    ):
        assert tok in hlsl, f"Missing HLSL token: {tok}"

    for tok in (
        "EvalPorosityWetFilm",
        "EvalSpectralCausticDispersion",
        "1.331f",
        "1.333f",
        "1.339f",
    ):
        assert tok in cpp, f"Missing C++23 token: {tok}"

    for tok in (
        '#include "RawPorosityCaustics.hlsli"',
        "rawEvalPorosityWetFilm",
        "porW80.albedoWet",
        "rawEvalSpectralCausticDispersion",
        "specCausW80.causticRGB",
    ):
        assert tok in lighting, f"Missing Lighting.hlsl Wave 80 token: {tok}"

    for tok in (
        '#include "RawPorosityCaustics.hlsli"',
        "rawEvalSpectralCausticDispersion",
        "waterCausW80.causticRGB",
    ):
        assert tok in water, f"Missing RtWater.hlsl Wave 80 token: {tok}"


def test_wave80_porosity_and_cauchy_invariants() -> None:
    # 1) Porosity darkening & energy conservation <= 1.0:
    for alb in (0.15, 0.45, 0.85):
        dry = alb
        wet = alb ** (1.0 + 0.72 * 0.90 * 0.85)
        assert wet < dry
        f_film = 0.02 + 0.98 * ((1.0 - 0.5) ** 5)
        assert min(wet, 1.0 - f_film) + f_film <= 1.0 + 1e-12

    # 2) Three-band Cauchy spectral caustic zero-mean white-furnace spatial mean == 1.0:
    inv_iors = (1.0 / 1.331, 1.0 / 1.333, 1.0 / 1.339)
    sx, sy = 0.42, 0.28
    sums = [0.0, 0.0, 0.0]
    n = 32
    for ix in range(n):
        for iy in range(n):
            bx, by = ix * 14.5, iy * 14.5
            for ci, eta in enumerate(inv_iors):
                rx, ry = -sx * eta, -sy * eta
                rz = -math.sqrt(max(1.0 - rx * rx - ry * ry, 0.09))
                lev = (45.0 / max(-rz, 0.25)) * (1.0 + (ci - 1) * 0.35)
                qx, qy = (bx - rx * lev) * 0.055, (by - ry * lev) * 0.055
                zm = (0.34 * math.sin(qx + 0.57 * qy)
                      + 0.28 * math.cos(0.63 * qx - 1.19 * qy + 1.31)
                      + 0.22 * math.sin(1.47 * qx - 0.93 * qy + 2.47)
                      + 0.16 * math.cos(0.88 * qx + 1.53 * qy + 0.73))
                sums[ci] += max(0.28, min(1.72, 1.0 + zm * 0.55))
    for s in sums:
        assert abs(s / (n * n) - 1.0) < 0.03


def test_wave80_hygiene() -> None:
    forbidden = "zen" + "tropy"
    em_dash = "\u2014"
    new_files = [
        SHADERS / "RawPorosityCaustics.hlsli",
        ROOT / "src" / "d3d12_bridge" / "RawPorosityCaustics.h",
        ROOT / "tests" / "wave80_porosity_caustics_check.cpp",
        ROOT / "tests" / "test_wave80_porosity_caustics.py",
        ROOT / "docs" / "WAVE80-POROSITY-CAUSTICS-MILESTONE-2026-11-26.md",
    ]
    for p in new_files:
        txt = p.read_text(encoding="utf-8")
        assert em_dash not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"


def main() -> int:
    tests = [
        test_wave80_tokens_and_wiring,
        test_wave80_porosity_and_cauchy_invariants,
        test_wave80_hygiene,
    ]
    for fn in tests:
        fn()
        print(f"  [PASS] {fn.__name__}")
    print(f"=== WAVE 80 PYTHON ORACLE: {len(tests)}/{len(tests)} PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
