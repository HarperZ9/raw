#!/usr/bin/env python3
"""Wave 91 verification suite: OPT-052 BuildLang-NTC610 SM 6.9/6.10 integration + 3 Wave 90 visual eye-audit fixes."""
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]


class TestWave91Ntc610AndVisualRefinements(unittest.TestCase):
    def test_ntc610_shader_spec_and_decode_wired(self):
        spec = (ROOT / "src/d3d12_bridge/shaders/RawNtc610ShaderSpec.hlsli").read_text(encoding="utf-8")
        dec = (ROOT / "src/d3d12_bridge/shaders/RawNeuralTexDecode.hlsli").read_text(encoding="utf-8")
        hdr = (ROOT / "src/d3d12_bridge/RawNeuralTextureCache.h").read_text(encoding="utf-8")
        self.assertIn("struct RawNtc610MaterialCB", spec)
        self.assertIn("struct RawNtc610PbrSample", spec)
        self.assertIn("kRawNtc610MaxValidQScale", spec)
        # Pure math-only include: zero unbound register declarations
        self.assertNotIn(": register(", spec)
        self.assertNotIn(": register(", dec)
        self.assertIn('#include "RawNtc610ShaderSpec.hlsli"', dec)
        self.assertIn("RAW_ENABLE_SM610_COOPVEC", dec)
        self.assertIn("rawNtc610DecodeInlineMlp", dec)
        self.assertIn("static_assert(sizeof(RawNtc610MaterialCB) == 256u", hdr)
        self.assertIn("EstimateNtc6104kBudget", hdr)
        self.assertIn("DecodeNtc610InlineMlp", hdr)

    def test_septimus_glacier_broad_facets_cut_mottling(self):
        overhaul = (ROOT / "src/d3d12_bridge/shaders/RawTotalMaterialOverhaul.hlsli").read_text(encoding="utf-8")
        self.assertIn("rawGlacierCarvedVoronoi", overhaul)
        self.assertIn("planeLight", overhaul)
        self.assertIn("seamRim", overhaul)
        # Medium-scale mottling cut >= 65% vs Wave 90 (2.65 -> <= 0.85, strictly silky 15:1 cleavage)
        self.assertIn("const float fineAmp  = isIceCavern ? 0.82f", overhaul)

    def test_near_mid_aerial_mist_cleared_below_60m(self):
        frox = (ROOT / "src/d3d12_bridge/shaders/FroxelPass.hlsl").read_text(encoding="utf-8")
        fog = (ROOT / "src/d3d12_bridge/shaders/LightFog.hlsl").read_text(encoding="utf-8")
        self.assertIn("nearMidClear60m", frox)
        self.assertIn("smoothstep(1800.0f, 5200.0f, rayDist)", frox)
        self.assertIn("smoothstep(1600.0f, 4800.0f, dist)", fog)

    def test_dry_granite_roughness_floor_present(self):
        lit = (ROOT / "src/d3d12_bridge/shaders/Lighting.hlsl").read_text(encoding="utf-8")
        self.assertIn("dryGraniteFloor", lit)
        self.assertIn("dryRockMinAlpha", lit)
        self.assertIn("pebbleFloor", lit)

    def test_zero_em_dashes_and_forbidden_tokens_in_wave91_files(self):
        bad_tok = "zen" + "tropy"
        targets = [
            "src/d3d12_bridge/shaders/RawNtc610ShaderSpec.hlsli",
            "src/d3d12_bridge/shaders/RawNeuralTexDecode.hlsli",
            "src/d3d12_bridge/RawNeuralTextureCache.h",
            "tests/wave91_ntc610_and_visual_audit_check.cpp",
            "tests/test_wave91_ntc610_and_visual_audit.py",
        ]
        for rel in targets:
            txt = (ROOT / rel).read_text(encoding="utf-8")
            self.assertEqual(txt.count("\u2014"), 0, f"em dash found in {rel}")
            self.assertEqual(txt.count("\u2013"), 0, f"en dash found in {rel}")
            self.assertEqual(txt.lower().count(bad_tok), 0, f"forbidden token in {rel}")


if __name__ == "__main__":
    unittest.main()
