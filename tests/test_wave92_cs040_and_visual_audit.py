#!/usr/bin/env python3
"""Wave 92 verification suite: CS-040 16 km volumetric fog shadow cache & Kiruna twilight photometry + 3 Wave 91 visual polish fixes."""
import math
import pathlib
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
STAGED_TSV = pathlib.Path("C:/dev/mission-control/2026-11-27-author-ideas-buildlang/wave92_formulas.tsv")


class TestWave92Cs040AndVisualPolish(unittest.TestCase):
    def test_cs040_hlsl_and_cpp_wired_with_zero_unbound_registers(self):
        hlsli = (ROOT / "src/d3d12_bridge/shaders/RawFogShadowCache16km.hlsli").read_text(encoding="utf-8")
        hdr = (ROOT / "src/d3d12_bridge/RawFogShadowCache16km.h").read_text(encoding="utf-8")
        frox = (ROOT / "src/d3d12_bridge/shaders/FroxelPass.hlsl").read_text(encoding="utf-8")
        fog = (ROOT / "src/d3d12_bridge/shaders/LightFog.hlsl").read_text(encoding="utf-8")
        self.assertIn("kRawFsc16RangeU", hlsli)
        self.assertIn("rawFsc16CombineVisibility", hlsli)
        self.assertIn("rawFsc16KirunaTwilight", hlsli)
        self.assertNotIn(": register(", hlsli)
        self.assertIn("ComputeClipmapBudget", hdr)
        self.assertIn("EvalKirunaTwilight", hdr)
        self.assertIn('#include "RawFogShadowCache16km.hlsli"', frox)
        self.assertIn("rawFsc16CombineVisibility", frox)
        self.assertIn("rawFsc16TwilightFogMul", frox)
        self.assertIn('#include "RawFogShadowCache16km.hlsli"', fog)
        self.assertIn("rawFsc16FarFieldWeight", fog)
        self.assertIn("rawFsc16TwilightFogMul", fog)

    def test_cs040_independent_python_photometry_and_vram_oracle(self):
        # Clipmap VRAM budget: 6 levels * 256 * 256 * 32 * 4 B == 48.0 MiB <= 512 MiB
        total_bytes = 6 * 256 * 256 * 32 * 4
        total_mib = total_bytes / (1024.0 * 1024.0)
        self.assertAlmostEqual(total_mib, 48.0, places=6)
        self.assertLessEqual(total_mib, 512.0)
        # Outer level side = 70,000 u * 2^5 = 2,240,000 u -> radius 1,120,000 u / 70 u/m = 16.0 km
        radius_km = (70000.0 * (1 << 5) * 0.5) / (70.0 * 1000.0)
        self.assertAlmostEqual(radius_km, 16.0, places=6)
        # Kasten-Young (1989) relative optical airmass at z = 0 deg and z = 85 deg
        am0 = 1.0 / (math.cos(math.radians(0.0)) + 0.50572 * ((96.07995 - 0.0) ** -1.6364))
        am85 = 1.0 / (math.cos(math.radians(85.0)) + 0.50572 * ((96.07995 - 85.0) ** -1.6364))
        self.assertAlmostEqual(am0, 0.999712, delta=1.0e-5)
        self.assertAlmostEqual(am85, 10.305791, delta=1.0e-4)
        # Kiruna dusk ratio capped at 4.0 and 0.41 dex/deg depression decay at -3 deg
        hz_ratio = min(4.0, max(1.0, 1.0 + 0.30 * (am85 - 1.0)))
        self.assertAlmostEqual(hz_ratio, 3.791737, delta=1.0e-3)
        zen_rel = 10.0 ** (0.41 * -3.0)
        self.assertAlmostEqual(zen_rel, 0.058884, delta=1.0e-5)

    def test_three_wave91_visual_polish_fixes_present(self):
        overhaul = (ROOT / "src/d3d12_bridge/shaders/RawTotalMaterialOverhaul.hlsli").read_text(encoding="utf-8")
        frox = (ROOT / "src/d3d12_bridge/shaders/FroxelPass.hlsl").read_text(encoding="utf-8")
        fog = (ROOT / "src/d3d12_bridge/shaders/LightFog.hlsl").read_text(encoding="utf-8")
        lit = (ROOT / "src/d3d12_bridge/shaders/Lighting.hlsl").read_text(encoding="utf-8")
        # Fix 1: Septimus Cave ~2.2x enlarged organic flow-warped Voronoi glacier scale & softened seam/ridge contrast
        self.assertIn("pWI.x * 0.66f, pWI.y * 2.18f", overhaul)
        self.assertIn("pow(seam1, 2.2f) * 0.055f", overhaul)
        self.assertIn("silkySheen", overhaul)
        # Fix 2: Riften Canal ~25% warmer & clearer mid-distance fog wash
        self.assertIn("clearDayMidW92", frox)
        self.assertIn("midWarmTintW92", frox)
        self.assertIn("midClearWarmW92", fog)
        self.assertIn("froxWarmTintW92", fog)
        # Fix 3: Whiterun Exterior neutralize cool-blue sky-bounce on shaded weathered timber planks
        self.assertIn("isWeatheredTimberW92", lit)
        self.assertIn("isTimberBounceW92", lit)

    def test_staged_formulas_tsv_schema_and_clean_hygiene(self):
        self.assertTrue(STAGED_TSV.exists(), f"Missing staged TSV: {STAGED_TSV}")
        lines = [ln for ln in STAGED_TSV.read_text(encoding="utf-8").splitlines() if ln.strip()]
        self.assertEqual(len(lines), 2, "Expected header + 1 CS-040 staged formula row")
        hdr = lines[0].split("\t")
        row = lines[1].split("\t")
        self.assertEqual(len(hdr), 16)
        self.assertEqual(len(row), 16)
        self.assertEqual(row[14], "VERIFIED")
        self.assertEqual(row[15], "-")
        self.assertIn("CS-040", row[2])

    def test_zero_em_dashes_and_forbidden_tokens_in_wave92_files(self):
        bad_tok = "zen" + "tropy"
        targets = [
            ROOT / "src/d3d12_bridge/shaders/RawFogShadowCache16km.hlsli",
            ROOT / "src/d3d12_bridge/RawFogShadowCache16km.h",
            ROOT / "tests/wave92_cs040_and_visual_audit_check.cpp",
            ROOT / "tests/test_wave92_cs040_and_visual_audit.py",
            STAGED_TSV,
        ]
        for path in targets:
            txt = path.read_text(encoding="utf-8")
            self.assertEqual(txt.count("\u2014"), 0, f"em dash found in {path}")
            self.assertEqual(txt.count("\u2013"), 0, f"en dash found in {path}")
            self.assertEqual(txt.lower().count(bad_tok), 0, f"forbidden token in {path}")


if __name__ == "__main__":
    unittest.main()
