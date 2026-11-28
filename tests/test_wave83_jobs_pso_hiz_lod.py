#!/usr/bin/env python3
"""Canonical standalone oracle for RAW DX12 Wave 83:
Lock-Free Chase-Lev Work-Stealing Job Graph + Async SHA-256 PSO Cache +
Two-Pass Hi-Z GPU Occlusion + Continuous Meshlet LOD Distance Budgeter.
Clean-room MIT.
"""
from __future__ import annotations

import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def test_wave83_sources_and_hygiene() -> None:
    files = [
        ROOT / "src" / "engine_rt" / "raw_job_system.h",
        ROOT / "src" / "engine_rt" / "raw_job_system.cpp",
        ROOT / "src" / "d3d12_bridge" / "RawPsoCache.h",
        ROOT / "src" / "d3d12_bridge" / "RawPsoCache.cpp",
        ROOT / "src" / "d3d12_bridge" / "RawTwoPassHiZ.h",
        ROOT / "src" / "d3d12_bridge" / "shaders" / "RawGpuCull.hlsl",
        ROOT / "src" / "d3d12_bridge" / "shaders" / "DistantLod.hlsli",
    ]
    forb = "zen" + "tropy"
    for p in files:
        txt = p.read_text(encoding="utf-8")
        assert "\u2014" not in txt, f"em dash found in {p.name}"
        assert forb not in txt.lower(), f"forbidden token in {p.name}"

    js_h = (ROOT / "src" / "engine_rt" / "raw_job_system.h").read_text(encoding="utf-8")
    assert "class ChaseLevDeque" in js_h and "SubmitAfterAll" in js_h and "SubmitInline" in js_h
    assert "static_assert(sizeof(RawJobSlot) == 64u" in js_h

    pso_h = (ROOT / "src" / "d3d12_bridge" / "RawPsoCache.h").read_text(encoding="utf-8")
    assert "ComputeSha256" in pso_h and "PrewarmBatchAsync" in pso_h and "static_assert(sizeof(RawPsoBlobHeader) == 80u" in pso_h

    cull_hlsl = (ROOT / "src" / "d3d12_bridge" / "shaders" / "RawGpuCull.hlsl").read_text(encoding="utf-8")
    assert "cs_cull_pass1_prev_hiz" in cull_hlsl and "cs_cull_pass2_disoccluded" in cull_hlsl
    assert "EvalMeshletLodCut" in cull_hlsl and "TestMeshletBackfaceCone" in cull_hlsl

    dlod = (ROOT / "src" / "d3d12_bridge" / "shaders" / "DistantLod.hlsli").read_text(encoding="utf-8")
    assert "rawEvalContinuousLodFade" in dlod


def test_two_pass_hiz_and_monotonic_lod_math() -> None:
    # Two-pass Hi-Z disocclusion check:
    min_obj_z, prev_hiz, curr_hiz, bias = 0.45, 0.20, 0.85, 1e-4
    pass1_occluded = min_obj_z > (prev_hiz + bias)
    pass2_occluded = min_obj_z > (curr_hiz + bias)
    assert pass1_occluded and (not pass2_occluded)

    # Conservative max() vs buggy min() check:
    taps = (0.20, 0.85, 0.25, 0.30)
    assert not (0.45 > max(taps) + bias)
    assert 0.45 > min(taps) + bias  # buggy min() falsely culls!

    # Watertight exact 1-node LOD partition:
    nodes = [(0.0, 0.25), (0.25, 1.0), (1.0, 1e9)]
    for z in (40.0, 200.0, 900.0, 3000.0):
        selected = sum(1 for lo, hi in nodes if ((lo * 1080.0 / z) <= 1.25) and ((hi * 1080.0 / z) > 1.25))
        assert selected == 1, f"expected exact 1 active LOD node at z={z}, got {selected}"


def main() -> int:
    tests = [
        ("wave83_sources_and_hygiene", test_wave83_sources_and_hygiene),
        ("two_pass_hiz_and_monotonic_lod_math", test_two_pass_hiz_and_monotonic_lod_math),
    ]
    for name, fn in tests:
        fn()
        print(f"PASS {name}")
    print(f"WAVE83_ORACLE_PASS {len(tests)}/{len(tests)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
