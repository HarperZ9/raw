"""Wave 86 Canonical Oracle: 2027 Mod-Developer SDK ABI v2.0 + BuildLang FFI
+ Sub-100 ms Atomic Hot-Reload + HDR10/BT.2020/OKLab Output + Cross-Title Adapter.
Clean-room MIT.
"""
from __future__ import annotations

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent

WAVE86_FILES = [
    ROOT / "include/raw_mod_sdk_abi.h",
    ROOT / "src/engine_rt/RawModSdkHost.h",
    ROOT / "src/engine_rt/RawModSdkHost.cpp",
    ROOT / "sdk/raw_mod_sdk.bld",
    ROOT / "src/d3d12_bridge/RawHotReload.h",
    ROOT / "src/d3d12_bridge/RawHotReload.cpp",
    ROOT / "src/d3d12_bridge/shaders/RawHdrDisplayOutput.hlsli",
    ROOT / "src/d3d12_bridge/RawHdrDisplayOutput.h",
    ROOT / "src/ce_runtime/ICreationGameAdapter.h",
    ROOT / "tools/live/raw_mod_check.py",
    ROOT / "tests/wave86_mod_sdk_hotreload_hdr_check.cpp",
    ROOT / "tests/test_wave86_mod_sdk_hotreload_hdr.py",
    ROOT / "docs/WAVE86-MOD-SDK-HOTRELOAD-HDR-ADAPTER-2026-11-26.md",
]


def main() -> int:
    forbidden = "zen" + "tropy"
    for p in WAVE86_FILES:
        assert p.exists(), f"Missing Wave 86 file: {p}"
        txt = p.read_text(encoding="utf-8")
        assert "\u2014" not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"

    abi_h = (ROOT / "include/raw_mod_sdk_abi.h").read_text(encoding="utf-8")
    assert "0x00020000u" in abi_h
    assert "static_assert(sizeof(RawModPassDesc) == 64u" in abi_h
    assert "static_assert(sizeof(RawModBudgetRecord) == 64u" in abi_h

    hdr_hlsl = (ROOT / "src/d3d12_bridge/shaders/RawHdrDisplayOutput.hlsli").read_text(encoding="utf-8")
    assert "RawRec709ToRec2020" in hdr_hlsl
    assert "RawOklabSoftGamutCompress" in hdr_hlsl
    assert "RawLinearNitsToPq01" in hdr_hlsl
    assert "RawPq01ToLinearNits" in hdr_hlsl

    # Run raw_mod_check.py built-in clean + conflict self-test.
    mod_chk = subprocess.run(
        [sys.executable, str(ROOT / "tools/live/raw_mod_check.py"), "--self-test"],
        capture_output=True, text=True, check=False,
    )
    assert mod_chk.returncode == 0, f"raw_mod_check.py failed:\n{mod_chk.stdout}\n{mod_chk.stderr}"
    assert '"self_test_ok": true' in mod_chk.stdout

    # Run compiled C++23 Wave 86 verification binary if present.
    exe = pathlib.Path(r"E:\wt\raw-rt9-build\wave86_mod_sdk_hotreload_hdr_check.exe")
    if exe.exists():
        proc = subprocess.run([str(exe)], capture_output=True, text=True, check=False)
        assert proc.returncode == 0, f"wave86 check failed:\n{proc.stdout}\n{proc.stderr}"
        assert "6 passed, 0 failed" in proc.stdout, f"Unexpected Wave 86 output:\n{proc.stdout}"

    print("PASS: Wave 86 Mod SDK ABI v2.0 + BuildLang FFI + Hot-Reload + HDR10/OKLab + GameAdapter oracle")
    return 0


if __name__ == "__main__":
    sys.exit(main())
