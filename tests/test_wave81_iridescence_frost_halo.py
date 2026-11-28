#!/usr/bin/env python3
"""Canonical standalone oracle for RAW DX12 Wave 81:
Physical Airy Thin-Film Frost/Oil/Water Iridescence + Anisotropic Glaze-Ice &
Frost-Crystal Phase Transition (-4 C .. +2 C) + Atmospheric 42-deg Rain-Mist Bow
& 22-deg Ice-Crystal Halo Spectral Scattering.
Clean-room MIT.
"""
import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHADERS = ROOT / "src" / "d3d12_bridge" / "shaders"
BRIDGE = ROOT / "src" / "d3d12_bridge"


def smoothstep(e0: float, e1: float, x: float) -> float:
    if abs(e1 - e0) <= 1e-7:
        return 1.0 if x >= e1 else 0.0
    t = max(0.0, min(1.0, (x - e0) / (e1 - e0)))
    return t * t * (3.0 - 2.0 * t)


def airy_reflectance(cos_theta: float, d_nm: float, n_f: float, n_b: float) -> tuple[float, float, float]:
    c = max(0.0, min(1.0, cos_theta))
    sin2_i = max(1.0 - c * c, 0.0)
    cos_t = math.sqrt(max(1.0 - sin2_i / (n_f * n_f), 0.04))
    opd = 2.0 * n_f * d_nm * cos_t
    r12n = (1.0 - n_f) / (1.0 + n_f)
    r23n = (n_f - n_b) / (n_f + n_b)
    R12 = max(0.0, min(1.0, r12n * r12n + (1.0 - r12n * r12n) * ((1.0 - c) ** 5)))
    R23 = max(0.0, min(0.995, r23n * r23n))
    phase_flip = math.pi if (r12n * r23n < 0.0) else 0.0
    two_root = 2.0 * math.sqrt(max(R12 * R23, 0.0))
    out = []
    for lam in (650.0, 532.0, 450.0):
        delta = (2.0 * math.pi * opd) / lam + phase_flip
        cd = math.cos(delta)
        num = max(R12 + R23 + two_root * cd, 0.0)
        den = max(1.0 + R12 * R23 + two_root * cd, 1e-5)
        out.append(max(0.0, min(1.0, num / den)))
    return (out[0], out[1], out[2])


def test_source_and_wiring() -> None:
    hlsli = (SHADERS / "RawIridescenceFrostHalo.hlsli").read_text(encoding="utf-8")
    hdr = (BRIDGE / "RawIridescenceFrostHalo.h").read_text(encoding="utf-8")
    lighting = (SHADERS / "Lighting.hlsl").read_text(encoding="utf-8")
    water = (SHADERS / "RtWater.hlsl").read_text(encoding="utf-8")
    fog = (SHADERS / "LightFog.hlsl").read_text(encoding="utf-8")

    forb = "zen" + "tropy"
    for name, text in (("RawIridescenceFrostHalo.hlsli", hlsli), ("RawIridescenceFrostHalo.h", hdr)):
        assert "\u2014" not in text, f"em dash found in {name}"
        assert forb not in text.lower(), f"forbidden token in {name}"

    assert '#include "RawPrecipThermal.hlsli"' in hlsli
    assert "rawPtLaplacianConcavity" in hlsli
    assert "SV_Position" not in hlsli and "screenUv" not in hlsli
    for sym in ("rawEvalThinFilmIridescence", "rawEvalFrostGlazeCoupling", "rawEvalAtmoSpectralHalo"):
        assert sym in hlsli, f"missing {sym} in hlsli"
    for sym in ("EvalThinFilmIridescence", "EvalFrostGlazeCoupling", "EvalAtmoSpectralHalo"):
        assert sym in hdr, f"missing {sym} in header"

    assert '#include "RawIridescenceFrostHalo.hlsli"' in lighting
    assert "rawEvalFrostGlazeCoupling(" in lighting and "rawEvalThinFilmIridescence(" in lighting
    assert '#include "RawIridescenceFrostHalo.hlsli"' in water
    assert "rawEvalThinFilmIridescence(" in water
    assert '#include "RawIridescenceFrostHalo.hlsli"' in fog
    assert "rawEvalAtmoSpectralHalo(" in fog


def test_airy_physics_bounds() -> None:
    max_spread = 0.0
    for d in (80.0, 160.0, 240.0, 360.0, 480.0, 620.0, 780.0, 900.0):
        for c in (0.05, 0.25, 0.5, 0.75, 1.0):
            for n_f in (1.31, 1.333, 1.46):
                r, g, b = airy_reflectance(c, d, n_f, 1.52)
                assert 0.0 <= r <= 1.0 and 0.0 <= g <= 1.0 and 0.0 <= b <= 1.0
                max_spread = max(max_spread, max(r, g, b) - min(r, g, b))
    assert max_spread > 0.015, f"expected chromatic interference spread, got {max_spread}"


def test_phase_and_halo_gates() -> None:
    # Freezing phase transition monotonicity across +2 C .. -4 C:
    f_warm = 1.0 - smoothstep(-4.0, 2.0, 3.0)
    f_mid = 1.0 - smoothstep(-4.0, 2.0, -1.0)
    f_cold = 1.0 - smoothstep(-4.0, 2.0, -5.0)
    assert f_warm == 0.0 and 0.45 < f_mid < 0.55 and f_cold == 1.0

    # Dry-day rainbow gate must be strictly zero:
    assert smoothstep(0.05, 0.35, 0.0) == 0.0
    assert smoothstep(0.05, 0.35, 0.04) == 0.0
    assert smoothstep(0.05, 0.35, 0.40) == 1.0

    # 22-deg ice halo forbidden inner zone (< 20 deg -> 0.0):
    assert smoothstep(0.352, 0.374, math.radians(18.0)) == 0.0
    assert smoothstep(0.352, 0.374, math.radians(22.0)) == 1.0


def main() -> int:
    tests = [
        ("source_and_wiring", test_source_and_wiring),
        ("airy_physics_bounds", test_airy_physics_bounds),
        ("phase_and_halo_gates", test_phase_and_halo_gates),
    ]
    for name, fn in tests:
        fn()
        print(f"PASS {name}")
    print(f"WAVE81_ORACLE_PASS {len(tests)}/{len(tests)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
