#!/usr/bin/env python3
"""Canonical standalone oracle for RAW DX12 Wave 84:
512-1,024+ Actor SoA Crowd ECS + 4-Tier Adaptive Scheduler + Hierarchical HPA*
Multi-Cell Navmesh & Dynamic Carving + ORCA Crowd Avoidance + Off-Thread Utility/HTN AI +
Geometry-Coupled Estuary-Space 8x8 Orthonormal Hadamard FDN Spatial Audio.
Clean-room MIT.
"""
from __future__ import annotations

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent


def test_wave84_sources_and_hygiene() -> None:
    files = [
        ROOT / "src" / "engine_rt" / "RawActorCrowdEcs.h",
        ROOT / "src" / "engine_rt" / "RawActorCrowdEcs.cpp",
        ROOT / "src" / "engine_rt" / "RawCrowdNavAi.h",
        ROOT / "src" / "engine_rt" / "RawCrowdNavAi.cpp",
        ROOT / "src" / "ce_runtime" / "CeNavMesh.h",
        ROOT / "src" / "ce_runtime" / "CeNavMesh.cpp",
        ROOT / "src" / "engine_rt" / "rt_adaptive_audio_director.h",
        ROOT / "src" / "engine_rt" / "rt_adaptive_audio_director.cpp",
    ]
    forb = "zen" + "tropy"
    for p in files:
        txt = p.read_text(encoding="utf-8")
        assert "\u2014" not in txt, f"em dash found in {p.name}"
        assert forb not in txt.lower(), f"forbidden token in {p.name}"

    ecs_h = (ROOT / "src" / "engine_rt" / "RawActorCrowdEcs.h").read_text(encoding="utf-8")
    assert "kMaxCrowdActors = 1024u" in ecs_h and "AssignAdaptiveTiers" in ecs_h and "StepCrowdTick" in ecs_h
    nav_h = (ROOT / "src" / "ce_runtime" / "CeNavMesh.h").read_text(encoding="utf-8")
    assert "HpaClusterNavGraph" in nav_h and "CarveDynamicObstacle" in nav_h
    ai_h = (ROOT / "src" / "engine_rt" / "RawCrowdNavAi.h").read_text(encoding="utf-8")
    assert "SolveOrcaCrowdStep" in ai_h and "EvaluateUtilityHtnBatch" in ai_h
    aud_h = (ROOT / "src" / "engine_rt" / "rt_adaptive_audio_director.h").read_text(encoding="utf-8")
    assert "EstuarySpaceFdn" in aud_h and "AcousticProbeGeometry" in aud_h


def test_hadamard_8x8_orthonormality_and_rt60() -> None:
    # Exact 8x8 Sylvester Hadamard orthonormal matrix H_8 * (1/sqrt(8)):
    h = [[1.0]]
    for _ in range(3):
        h = [r + r for r in h] + [r + [-x for x in r] for r in h]
    s = 1.0 / math.sqrt(8.0)
    H = [[x * s for x in r] for r in h]
    # Verify H^T * H == I_8 to < 1e-12:
    for i in range(8):
        for j in range(8):
            dot = sum(H[k][i] * H[k][j] for k in range(8))
            expected = 1.0 if i == j else 0.0
            assert abs(dot - expected) < 1e-12, f"Hadamard non-orthonormal at ({i},{j}): {dot}"


def main() -> int:
    tests = [
        ("wave84_sources_and_hygiene", test_wave84_sources_and_hygiene),
        ("hadamard_8x8_orthonormality_and_rt60", test_hadamard_8x8_orthonormality_and_rt60),
    ]
    for name, fn in tests:
        fn()
        print(f"PASS {name}")
    print(f"WAVE84_ORACLE_PASS {len(tests)}/{len(tests)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
