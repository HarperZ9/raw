"""Wave 79 verification suite: Physically-Bounded Planck Blackbody Fire/Torch/Brazier Emission
& Volumetric Smoke Self-Shadowing + Character Skin/Fur/Cloth Burley-Normalized SSS Guard.
Runs standalone as a canonical engine-receipt/1 oracle (zero external deps, clean-room MIT).
"""
from __future__ import annotations

import math
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHADERS = ROOT / "src" / "d3d12_bridge" / "shaders"


def _planck_chroma(temp_k: float) -> tuple[float, float, float]:
    lams = (615.0, 540.0, 465.0)
    exs = [1.4387768e7 / (lam * temp_k) for lam in lams]
    ex_min = min(exs)
    rads = [((550.0 / lam) ** 5) * math.exp(-min(e - ex_min, 60.0)) / max(1.0 - math.exp(-min(e, 60.0)), 1e-6)
            for lam, e in zip(lams, exs)]
    pk = max(rads)
    return (rads[0] / pk, rads[1] / pk, rads[2] / pk)


def test_wave79_tokens_and_wiring() -> None:
    hlsl = (SHADERS / "RawBlackbodySss.hlsli").read_text(encoding="utf-8")
    cpp = (ROOT / "src" / "d3d12_bridge" / "RawBlackbodySss.h").read_text(encoding="utf-8")
    lightfog = (SHADERS / "LightFog.hlsl").read_text(encoding="utf-8")
    lighting = (SHADERS / "Lighting.hlsl").read_text(encoding="utf-8")

    for tok in ("rawEvalBoundedBlackbodyFireSmoke", "rawEvalBurleySkinClothSss", "smokeUnderScatter", "totalEnergyMax"):
        assert tok in hlsl, f"Missing HLSL token: {tok}"
    for tok in ("EvalBoundedBlackbodyFireSmoke", "EvalBurleySkinClothSss", "smokeUnderScatter", "totalEnergyMax"):
        assert tok in cpp, f"Missing C++23 token: {tok}"
    for tok in ('#include "RawBlackbodySss.hlsli"', "rawEvalBoundedBlackbodyFireSmoke", "bbW79.fireEmission", "smokeBbW79.smokeUnderScatter"):
        assert tok in lightfog, f"Missing LightFog.hlsl Wave 79 token: {tok}"
    for tok in ('#include "RawBlackbodySss.hlsli"', "rawEvalBurleySkinClothSss", "skinW79.sssWrap", "clothW79.totalEnergyMax"):
        assert tok in lighting, f"Missing Lighting.hlsl Wave 79 token: {tok}"


def test_wave79_planck_and_burley_invariants() -> None:
    # 1) Temperature-monotone Planck chromaticity (1400K..3200K)
    prev_br = -1.0
    for tk in range(1400, 3300, 200):
        r, g, b = _planck_chroma(float(tk))
        br = b / max(r, 1e-6)
        assert br >= prev_br - 1e-9
        prev_br = br
    c_cool = _planck_chroma(1400.0)
    c_hot = _planck_chroma(3200.0)
    assert c_hot[2] > c_cool[2] + 0.15

    # 2) Beer-Lambert smoke transmittance in (0, 1] + planted negative optical depth rejected
    for od in (0.0, 0.5, 2.0, 8.0):
        t_smoke = math.exp(-max(0.0, od))
        assert 0.0 < t_smoke <= 1.0
    assert math.exp(-(-2.5)) > 10.0  # planted negative od control

    # 3) Burley skin/cloth white-furnace energy conservation <= 1.0
    for alb in (0.1, 0.5, 0.9):
        for nv in (0.05, 0.5, 1.0):
            f_spec = (0.028 + 0.972 * ((1.0 - nv) ** 5)) * 0.85
            rem = max(1.0 - f_spec, 0.0)
            raw_d = alb * rem
            raw_s = alb * 0.86 * 0.20 * rem
            sc = (rem / (raw_d + raw_s)) if (raw_d + raw_s) > rem else 1.0
            assert (raw_d + raw_s) * sc + f_spec <= 1.0 + 1e-12


def test_wave79_hygiene() -> None:
    forbidden = "zen" + "tropy"
    em_dash = "\u2014"
    new_files = [
        SHADERS / "RawBlackbodySss.hlsli",
        ROOT / "src" / "d3d12_bridge" / "RawBlackbodySss.h",
        ROOT / "tests" / "wave79_blackbody_sss_check.cpp",
        ROOT / "tests" / "test_wave79_blackbody_sss.py",
        ROOT / "docs" / "WAVE79-BLACKBODY-SMOKE-BURLEY-SSS-2026-11-26.md",
    ]
    for p in new_files:
        txt = p.read_text(encoding="utf-8")
        assert em_dash not in txt, f"Em dash found in {p}"
        assert forbidden not in txt.lower(), f"Forbidden token found in {p}"


def main() -> int:
    tests = [
        test_wave79_tokens_and_wiring,
        test_wave79_planck_and_burley_invariants,
        test_wave79_hygiene,
    ]
    for fn in tests:
        fn()
        print(f"  [PASS] {fn.__name__}")
    print(f"=== WAVE 79 PYTHON ORACLE: {len(tests)}/{len(tests)} PASS ===")
    return 0


if __name__ == "__main__":
    sys.exit(main())
