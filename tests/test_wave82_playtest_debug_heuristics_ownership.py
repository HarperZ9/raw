#!/usr/bin/env python3
"""Canonical standalone oracle & 4 planted-failure controls for RAW DX12 Wave 82:
  - 9 Canonical GPU Debug Views (RawDebugViews.hlsli / RawDebugViews.h)
  - 30-Metric Extended Heuristics Collector (raw_heuristics_v2.py)
  - Change-Targeted Full-World Playtest Runner (raw_playtest_runner.py)
  - 15-Pass Runtime Engine Ownership Ledger (engine_ownership_manifest.json / rt_ownership_ledger.h)
  - Planted Controls 82-A (VRAM leak), 82-B (Shimmer + Firefly), 82-C (Double-exec AO/Fog),
    82-D (Corrupted debug view NaN/zero-normal), plus Planted Uncovered-Subsystem diff guard!
Clean-room MIT.
"""
from __future__ import annotations

import json
import pathlib
import sys
import numpy as np

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "live"))

import raw_heuristics_v2 as rh2  # noqa: E402
import raw_playtest_runner as rpr  # noqa: E402


def _clean_burst(n: int = 6, h: int = 64, w: int = 64) -> list[np.ndarray]:
    base = np.full((h, w, 3), 0.32, dtype=np.float32)
    base[8:40, 8:40, :] = np.array([0.18, 0.36, 0.16], dtype=np.float32)  # calm pine canopy
    return [base.copy() for _ in range(n)]


def _clean_ownership_dump() -> dict:
    manifest = json.loads((ROOT / "data" / "ownership" / "engine_ownership_manifest.json").read_text(encoding="utf-8"))
    passes = manifest["passes"]
    return {
        "owned_by_raw": sum(1 for p in passes if p["state"] == "OWNED_BY_RAW"),
        "hybrid_intercepted": sum(1 for p in passes if p["state"] == "HYBRID_INTERCEPTED"),
        "still_vanilla": sum(1 for p in passes if p["state"] == "STILL_VANILLA"),
        "double_exec_total": 0,
        "flicker_total": 0,
        "passes": passes,
    }


def _clean_telemetry() -> dict:
    return {
        "frame_times_ms": [8.5, 8.8, 9.0, 8.6, 8.9, 8.7] * 10,
        "pass_ms": {
            "depth_ms": 0.40, "hiz_cull_ms": 0.029, "ao_ms": 0.054, "gi_ms": 0.114,
            "shadow_ms": 0.028, "cloud_ms": 0.084, "lighting_ms": 0.62, "post_ms": 0.097,
        },
        "vram_start_mb": 4100.0,
        "vram_return_mb": 4108.0,
        "submitted_draws": 1400,
        "culled_draws": 2100,
        "hiz_occluded": 1450,
        "mean_overdraw": 1.42,
        "lod_pop_delta": 0.014,
        "white_furnace_violations": 0,
        "horizon_seam_delta": 0.008,
        "actor_skin_luma": 0.34,
    }


def test_source_wiring_and_manifest() -> None:
    hlsli = (ROOT / "src" / "d3d12_bridge" / "shaders" / "RawDebugViews.hlsli").read_text(encoding="utf-8")
    hdr = (ROOT / "src" / "d3d12_bridge" / "RawDebugViews.h").read_text(encoding="utf-8")
    ledger = (ROOT / "src" / "engine_rt" / "rt_ownership_ledger.h").read_text(encoding="utf-8")
    lighting = (ROOT / "src" / "d3d12_bridge" / "shaders" / "Lighting.hlsl").read_text(encoding="utf-8")
    ctrl = (ROOT / "src" / "engine_rt" / "rt_control.cpp").read_text(encoding="utf-8")
    manifest = json.loads((ROOT / "data" / "ownership" / "engine_ownership_manifest.json").read_text(encoding="utf-8"))

    forb = "zen" + "tropy"
    for name, txt in (("RawDebugViews.hlsli", hlsli), ("RawDebugViews.h", hdr), ("rt_ownership_ledger.h", ledger)):
        assert "\u2014" not in txt, f"em dash in {name}"
        assert forb not in txt.lower(), f"forbidden token in {name}"

    assert '#include "RawDebugViews.hlsli"' in lighting and "rawEvalCanonicalDebugView(" in lighting
    assert '"debug_view"' in ctrl and '"ownership_dump"' in ctrl
    assert manifest["schema"] == "raw.engine-ownership-manifest/1" and len(manifest["passes"]) == 15


def test_clean_30_metrics_and_3title_rtrace() -> None:
    res = rh2.collect_extended_30_metrics(_clean_burst(), _clean_telemetry(), _clean_ownership_dump(), base_heuristics_passed=20)
    assert res["all_passed"] and res["total_metrics"] == 30 and res["extended_passed"] == 10, res

    # Full 12-subsystem coverage + deterministic 3-title replay hashes:
    cov = rpr.select_scenarios_for_subsystems(rpr.CORE_SUBSYSTEMS)
    assert cov["ok"] and len(cov["uncovered_subsystems"]) == 0
    for title in ("SkyrimSE", "Fallout4", "Starfield"):
        h1 = rpr.build_deterministic_rtrace(title, cov["selected_scenarios"], tick_hz=100).canonical_sha256()
        h2 = rpr.build_deterministic_rtrace(title, cov["selected_scenarios"], tick_hz=100).canonical_sha256()
        assert h1 == h2 and len(h1) == 64


def test_planted_controls_82a_to_82d_and_uncovered_subsystem() -> None:
    # 82-A: VRAM leak (+64 MB delta) -> FAIL: VRAM_LEAK_EXCEEDED
    t_leak = _clean_telemetry()
    t_leak["vram_return_mb"] = t_leak["vram_start_mb"] + 64.0
    r_a = rh2.collect_extended_30_metrics(_clean_burst(), t_leak, _clean_ownership_dump())
    assert (not r_a["all_passed"]) and ("FAIL: VRAM_LEAK_EXCEEDED" in r_a["failure_codes"]), r_a

    # 82-B: Foliage alternating shimmer + 12 isolated 10x firefly spikes -> FAIL: SHIMMER_OR_FIREFLY
    burst_bad = _clean_burst(n=6)
    for idx in range(len(burst_bad)):
        burst_bad[idx][8:40, 8:40, :] = np.array([0.08, 0.72 if idx % 2 == 0 else 0.14, 0.08], dtype=np.float32)
    for k in range(12):
        burst_bad[0][5 + k * 4, 48, :] = 8.5  # isolated 1-px fireflies
    r_b = rh2.collect_extended_30_metrics(burst_bad, _clean_telemetry(), _clean_ownership_dump())
    assert (not r_b["all_passed"]) and (r_b["failure_codes"].count("FAIL: SHIMMER_OR_FIREFLY") == 2), r_b

    # 82-C: Double-executed vanilla AO/fog -> FAIL: OWNERSHIP_DOUBLE_EXEC
    own_bad = _clean_ownership_dump()
    own_bad["double_exec_total"] = 2
    r_c = rh2.collect_extended_30_metrics(_clean_burst(), _clean_telemetry(), own_bad)
    assert (not r_c["all_passed"]) and ("FAIL: OWNERSHIP_DOUBLE_EXEC" in r_c["failure_codes"]), r_c

    # 82-D: Corrupted debug view NaN/zero-normal -> FAIL: DEBUG_VIEW_CORRUPT
    r_d = rh2.collect_extended_30_metrics(_clean_burst(), _clean_telemetry(), _clean_ownership_dump(), debug_view_corrupt=True)
    assert (not r_d["all_passed"]) and ("FAIL: DEBUG_VIEW_CORRUPT" in r_d["failure_codes"]), r_d

    # Planted Uncovered Subsystem in change-targeted coverage map -> FAIL: SUBSYSTEM_PLAYTEST_MISSING
    cov_bad = rpr.select_scenarios_for_subsystems(["culling", "unmapped_future_subsystem"])
    assert (not cov_bad["ok"]) and cov_bad["code"] == "FAIL: SUBSYSTEM_PLAYTEST_MISSING"


def main() -> int:
    tests = [
        ("source_wiring_and_manifest", test_source_wiring_and_manifest),
        ("clean_30_metrics_and_3title_rtrace", test_clean_30_metrics_and_3title_rtrace),
        ("planted_controls_82a_to_82d_and_uncovered_subsystem", test_planted_controls_82a_to_82d_and_uncovered_subsystem),
    ]
    for name, fn in tests:
        fn()
        print(f"PASS {name}")
    print(f"WAVE82_ORACLE_PASS {len(tests)}/{len(tests)}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
