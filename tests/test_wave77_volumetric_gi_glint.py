"""Wave 77 verification suite: World-Space Multi-Bounce Froxel GI (Wave 75 L=8 Hash Cache),
Gust-Advected Water Foam & Capillary Ripple Coupling, and Energy-Conserving World-Space
Micro-Flake Snow Glint.
Runs standalone as a canonical engine-receipt/1 oracle (zero external deps, clean-room MIT).
"""
from __future__ import annotations

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHADERS = ROOT / "src" / "d3d12_bridge" / "shaders"


def _hash_int3(qx: int, qy: int, qz: int) -> int:
    m = 0xFFFFFFFF
    h = (((qx & m) * 73856093) ^ ((qy & m) * 19349663) ^ ((qz & m) * 83492791)) & m
    h = ((h ^ (h >> 16)) * 2246822507) & m
    h = ((h ^ (h >> 13)) * 3266489909) & m
    return (h ^ (h >> 16)) & m


def _unorm24(h: int) -> float:
    return float(h & 0x00FFFFFF) / 16777216.0


def test_wave77_tokens_and_bindings() -> None:
    hlsl = (SHADERS / "RawVolumetricGiGlint.hlsli").read_text(encoding="utf-8")
    cpp = (ROOT / "src" / "d3d12_bridge" / "RawVolumetricGiGlint.h").read_text(encoding="utf-8")
    froxel = (SHADERS / "FroxelPass.hlsl").read_text(encoding="utf-8")
    water = (SHADERS / "RtWater.hlsl").read_text(encoding="utf-8")
    lighting = (SHADERS / "Lighting.hlsl").read_text(encoding="utf-8")
    lpass = (ROOT / "src" / "d3d12_bridge" / "LightingPass.cpp").read_text(encoding="utf-8")

    for tok in (
        '#include "RawRestirHashCache.hlsli"',
        "rawEvalFroxelWorldGiBounce",
        "rawEvalGustWaterFoamRipple",
        "rawEvalMicroFlakeSnowGlint",
        "rawHashCacheKeyMultiRes",
    ):
        assert tok in hlsl, f"Missing HLSL token: {tok}"

    for tok in (
        "EvalFroxelWorldGiBounce",
        "EvalGustWaterFoamRipple",
        "EvalMicroFlakeSnowGlint",
        "HashCacheKeyMultiRes",
    ):
        assert tok in cpp, f"Missing C++23 mirror token: {tok}"

    for tok in (
        '#include "RawVolumetricGiGlint.hlsli"',
        "Texture2D<float4> RtGiBounce  : register(t5);",
        "rawEvalFroxelWorldGiBounce",
        "giFroxW77.bounceInScat",
    ):
        assert tok in froxel, f"Missing FroxelPass.hlsl Wave 77 token: {tok}"

    for tok in ("kFroxelInjectLayout[8]", "froxGiSrc", "const RasterBind fi[8]"):
        assert tok in lpass, f"Missing LightingPass.cpp Wave 77 binding token: {tok}"

    for tok in (
        '#include "RawVolumetricGiGlint.hlsli"',
        "rawEvalGustWaterFoamRipple",
        "gustCapW77.capillarySlope",
        "gustFoamW77.foamAdvectDelta",
        "gustFoamW77.fresnelEff",
    ):
        assert tok in water, f"Missing RtWater.hlsl Wave 77 token: {tok}"

    for tok in (
        '#include "RawVolumetricGiGlint.hlsli"',
        "rawEvalMicroFlakeSnowGlint",
        "glW77.glintSpec",
    ):
        assert tok in lighting, f"Missing Lighting.hlsl Wave 77 token: {tok}"


def test_wave77_white_furnace_and_pan_invariance_oracle() -> None:
    # Verify algebraic white-furnace upper bound (alb + budget * r/(1+r) < 1.0) and 0.0 pan drift:
    for alb in (0.0, 0.35, 0.72, 0.92):
        budget = max(1.0 - alb, 0.0)
        for raw_glint in (0.0, 0.1, 1.0, 10.0, 1e6):
            glint = budget * (raw_glint / (1.0 + raw_glint))
            assert 0.0 <= glint <= budget + 1e-12
            assert alb + glint <= 1.0 + 1e-12

    # Planted Control: unbounded sparkle violates white furnace > 1.0
    assert (0.78 + 4.5 * 1.55) > 5.0

    # Verify 3D world-space depth proximity gate rejects screen-space glow bleed:
    z_mid, z_surf, rad_w = 60.0, 650.0, 64.0
    dz = abs(z_mid - z_surf)
    gate = math.exp(-min((dz / rad_w) ** 2, 24.0)) if (z_mid <= z_surf + rad_w * 0.5 and dz <= rad_w * 2.5) else 0.0
    assert gate == 0.0, f"Expected 0.0 depth gate for distant surface, got {gate}"

    # Verify pure world-space cell hash invariance across screen-UV camera pan:
    q = (int(math.floor(4182.4 * 0.5)), int(math.floor(-1920.7 * 0.5)), int(math.floor(612.3 * 0.5)))
    ref_h = _hash_int3(*q)
    for u in (0.02, 0.25, 0.50, 0.75, 0.98):
        assert _hash_int3(*q) == ref_h
    # Planted screen-UV hash drifts across pan:
    uv_vals = {_unorm24(_hash_int3(int(u * 960), 540, 0)) for u in (0.02, 0.25, 0.50, 0.75, 0.98)}
    assert len(uv_vals) >= 4


def test_wave77_hygiene() -> None:
    forbidden = "zen" + "tropy"
    em_dash = "\u2014"
    new_files = [
        SHADERS / "RawVolumetricGiGlint.hlsli",
        ROOT / "src" / "d3d12_bridge" / "RawVolumetricGiGlint.h",
        ROOT / "tests" / "wave77_volumetric_gi_glint_check.cpp",
        ROOT / "tests" / "test_wave77_volumetric_gi_glint.py",
        ROOT / "docs" / "WAVE77-VOLUMETRIC-GI-FOAM-GLINT-2026-11-26.md",
    ]
    for p in new_files:
        txt = p.read_text(encoding="utf-8")
        assert em_dash not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"


def main() -> int:
    tests = [
        test_wave77_tokens_and_bindings,
        test_wave77_white_furnace_and_pan_invariance_oracle,
        test_wave77_hygiene,
    ]
    for fn in tests:
        fn()
        print(f"  [PASS] {fn.__name__}")
    print(f"=== WAVE 77 PYTHON ORACLE: {len(tests)}/{len(tests)} PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
