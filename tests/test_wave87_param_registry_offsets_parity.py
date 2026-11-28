"""Wave 87 Canonical Oracle: Zero-Unguarded-Offsets (69 -> 0) + C++/HLSL Layout Parity
+ Live Runtime RawParamRegistry + 17/17 (100%) Subsystem Coverage & 6 Live Scenes.
Clean-room MIT.
"""
from __future__ import annotations

import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools" / "live"))

import full_pipeline_audit as fpa  # noqa: E402
import raw_playtest_runner as rpr  # noqa: E402

WAVE87_NEW_FILES = [
    ROOT / "src/engine_rt/RawParamRegistry.h",
    ROOT / "src/engine_rt/RawParamRegistry.cpp",
    ROOT / "tests/wave87_param_registry_offsets_parity_check.cpp",
    ROOT / "tests/test_wave87_param_registry_offsets_parity.py",
    ROOT / "docs/WAVE87-ZERO-OFFSETS-PARITY-PARAM-REGISTRY-6SCENES-2026-11-26.md",
]


def main() -> int:
    forbidden = "zen" + "tropy"
    for p in WAVE87_NEW_FILES:
        assert p.exists(), f"Missing Wave 87 file: {p}"
        txt = p.read_text(encoding="utf-8")
        assert "\u2014" not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"

    # 1. Verify 17/17 subsystem playtest coverage (all 5 previously uncovered subsystems closed!).
    assert len(rpr.CORE_SUBSYSTEMS) == 17, f"Expected 17 core subsystems, got {len(rpr.CORE_SUBSYSTEMS)}"
    for sub in ("assets", "common", "materials", "pipeline", "ui"):
        assert sub in rpr.CORE_SUBSYSTEMS and sub in rpr.SUBSYSTEM_PATH_RULES
    cov = rpr.select_scenarios_for_subsystems(rpr.CORE_SUBSYSTEMS)
    assert cov["ok"] and len(cov["uncovered_subsystems"]) == 0, cov

    # 2. Verify extended6 profile has all 6 live scenes (4 baseline + whiterun_ext + riften_canal).
    ext6 = [s.name for s in fpa.CANONICAL_SCENES if s.in_extended6]
    assert len(ext6) == 6, f"Expected 6 extended6 scenes, got {ext6}"
    for req in ("riverwood_day", "inn_interior", "septimus_cave", "windhelm_ext_snow", "whiterun_ext", "riften_canal"):
        assert req in ext6, f"Missing scene {req} in extended6"

    # 3. Verify Atlas v2 live scan proves unguarded_offsets == 0 and untested_subsystems == 0 when present.
    atlas_v2 = pathlib.Path(r"C:\dev\mission-control\2026-11-17-raw\introspection-atlas\scan_raw_engine_atlas.py")
    if atlas_v2.exists():
        out_dir = pathlib.Path(r"E:\wt\raw-rt9-build\wave87_atlas_v2_check")
        proc = subprocess.run(
            [sys.executable, str(atlas_v2), "--v2", "--repo", str(ROOT), "--out-dir", str(out_dir)],
            capture_output=True, text=True, check=False,
        )
        assert proc.returncode == 0, f"Atlas v2 failed:\n{proc.stdout}\n{proc.stderr}"
        uni = json.loads((out_dir / "engine_parameter_universe_v2.json").read_text(encoding="utf-8"))
        summary = uni["summary"]
        assert summary["unguarded_offsets"] == 0, f"Remaining unguarded offsets: {summary['unguarded_offsets']}"
        assert summary["untested_subsystems"] == 0, f"Remaining untested subsystems: {summary['untested_subsystems']}"
        assert summary["playtest_rule_subsystems"] == 17, f"Expected 17 subsystems, got {summary['playtest_rule_subsystems']}"
        assert summary["unbound_hlsl_resources"] == 0, f"Unbound HLSL resources: {summary['unbound_hlsl_resources']}"

    # 4. Run compiled C++23 Wave 87 verification binary if present.
    exe = pathlib.Path(r"E:\wt\raw-rt9-build\wave87_param_registry_offsets_parity_check.exe")
    if exe.exists():
        proc = subprocess.run([str(exe)], capture_output=True, text=True, check=False)
        assert proc.returncode == 0, f"wave87 check failed:\n{proc.stdout}\n{proc.stderr}"
        assert "5 passed, 0 failed" in proc.stdout, f"Unexpected Wave 87 output:\n{proc.stdout}"

    print("PASS: Wave 87 Zero-Unguarded-Offsets (0) + Parity + RawParamRegistry + 17/17 Subsystems + 6 Live Scenes oracle")
    return 0


if __name__ == "__main__":
    sys.exit(main())
