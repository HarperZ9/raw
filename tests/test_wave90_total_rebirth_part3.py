"""Wave 90 Project Total Rebirth Part 3 regression suite: hoarfrost twigs, sapphire glacier, HG translucency, NTC hygiene."""
from __future__ import annotations

import unittest
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent


class Wave90TotalRebirthPart3Tests(unittest.TestCase):
    def test_wave90_shaders_and_headers_present_and_wired(self) -> None:
        lighting = (REPO / "src" / "d3d12_bridge" / "shaders" / "Lighting.hlsl").read_text(encoding="utf-8")
        overhaul = (REPO / "src" / "d3d12_bridge" / "shaders" / "RawTotalMaterialOverhaul.hlsli").read_text(encoding="utf-8")
        comp = (REPO / "src" / "d3d12_bridge" / "shaders" / "RawLiveWorldComposite.hlsl").read_text(encoding="utf-8")
        ntc_h = (REPO / "src" / "d3d12_bridge" / "RawNeuralTextureCache.h").read_text(encoding="utf-8")
        ntc_hlsl = (REPO / "src" / "d3d12_bridge" / "shaders" / "RawNeuralTexDecode.hlsli").read_text(encoding="utf-8")

        # 1. Windhelm crisp white hoarfrost twig fix (no dark olive 0.22/0.34/0.19 clamp on winter twigs)
        self.assertIn("isFrostedWinterTwig", lighting)
        self.assertIn("hoarWhite", lighting)
        self.assertIn("hoarTarget", comp)

        # 2. Septimus Cave translucent sapphire glacier + Snell internal parallax caustics
        self.assertIn("refract(-viewDir, nPert, 1.0f / 1.31f)", overhaul)
        self.assertIn("causticInt", overhaul)
        self.assertIn("crystalGlint", overhaul)

        # 3. Part 3 Henyey-Greenstein (OPT-032) backlit leaf translucency + wind-gust grass micro-shadowing
        self.assertIn("cosBackHG", lighting)
        self.assertIn("gustShadow", comp)

        # 4. Soraku-inspired compressed 4K PBR neural texture cache + HLSL decoder
        self.assertIn("kNtcMagic", ntc_h)
        self.assertIn("ValidateNtcMaterial", ntc_h)
        self.assertIn("ComputeMaterialPsnrDb", ntc_h)
        self.assertIn("rawNtcDecodePbr9", ntc_hlsl)

    def test_wave90_cleanroom_and_typography_hygiene(self) -> None:
        forbidden = "zen" + "tropy"
        checked_paths = [
            REPO / "src" / "d3d12_bridge" / "RawNeuralTextureCache.h",
            REPO / "src" / "d3d12_bridge" / "shaders" / "RawNeuralTexDecode.hlsli",
            REPO / "src" / "d3d12_bridge" / "shaders" / "RawTotalMaterialOverhaul.hlsli",
            REPO / "src" / "d3d12_bridge" / "RawTotalRebirth.h",
            REPO / "tests" / "wave90_total_rebirth_part3_check.cpp",
            REPO / "tests" / "test_wave90_total_rebirth_part3.py",
        ]
        for p in checked_paths:
            txt = p.read_text(encoding="utf-8", errors="ignore")
            self.assertNotIn(forbidden, txt.lower(), f"Forbidden token in {p}")
            self.assertNotIn("\u2014", txt, f"Em dash found in {p}")


if __name__ == "__main__":
    unittest.main()
