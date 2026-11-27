"""Wave 89 Project Total Rebirth Part 2 regression suite: Gates #35..#37, fail-closed visual controls, and hygiene."""
from __future__ import annotations

import sys
import unittest
from pathlib import Path

import numpy as np

REPO = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(REPO / "tools" / "live"))
import raw_heuristics_v2 as rh2  # noqa: E402


def _make_directional_surface(h: int, w: int, horizontal: bool = True, base_rgb: tuple[float, float, float] = (0.38, 0.48, 0.60)) -> np.ndarray:
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    if horizontal:
        striation = 0.08 * np.sin(yy * 0.45 + 0.3 * np.sin(xx * 0.04)) + 0.05 * np.sin(yy * 1.15)
    else:
        striation = 0.08 * np.sin(xx * 0.48 + 0.25 * np.sin(yy * 0.04)) + 0.05 * np.sin(xx * 1.25)
    img = np.empty((h, w, 3), dtype=np.float32)
    for c in range(3):
        img[..., c] = np.clip(base_rgb[c] + striation, 0.02, 0.96)
    return img


def _make_leopard_spot_surface(h: int, w: int, base_rgb: tuple[float, float, float] = (0.38, 0.48, 0.60)) -> np.ndarray:
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    # Isotropic circular blotch grid (X and Y frequencies strictly equal)
    spots = 0.18 * (np.sin(xx * 0.28) * np.sin(yy * 0.28))
    img = np.empty((h, w, 3), dtype=np.float32)
    for c in range(3):
        img[..., c] = np.clip(base_rgb[c] + spots, 0.02, 0.96)
    return img


class Wave89TotalRebirthPart2Tests(unittest.TestCase):
    def test_gate35_warm_horse_pass_and_planted_green_horse_fail(self) -> None:
        h, w = 128, 160
        van = np.full((h, w, 3), [0.46, 0.30, 0.18], dtype=np.float32)
        raw_clean = np.full((h, w, 3), [0.49, 0.31, 0.18], dtype=np.float32)
        res_pass = rh2.metric_35_animal_no_green_bleed(van, raw_clean)
        self.assertTrue(res_pass["passed"], res_pass)

        # Planted Defect A control: horse turned moss-green (G > R)
        raw_green_horse = np.full((h, w, 3), [0.24, 0.44, 0.16], dtype=np.float32)
        res_fail = rh2.metric_35_animal_no_green_bleed(van, raw_green_horse)
        self.assertFalse(res_fail["passed"], res_fail)
        self.assertEqual(res_fail["code"], "FAIL: ANIMAL_ACTOR_GREEN_BLEED")

    def test_gate36_frosted_winter_twigs_pass_and_planted_spring_green_fail(self) -> None:
        h, w = 128, 160
        van = np.full((h, w, 3), [0.44, 0.45, 0.48], dtype=np.float32)
        raw_frosted = np.full((h, w, 3), [0.45, 0.46, 0.50], dtype=np.float32)
        res_pass = rh2.metric_36_snow_foliage_no_spring_green(van, raw_frosted)
        self.assertTrue(res_pass["passed"], res_pass)

        # Planted Defect B control: winter twigs shifted bright spring-green
        raw_spring_green = np.full((h, w, 3), [0.28, 0.52, 0.24], dtype=np.float32)
        res_fail = rh2.metric_36_snow_foliage_no_spring_green(van, raw_spring_green)
        self.assertFalse(res_fail["passed"], res_fail)
        self.assertEqual(res_fail["code"], "FAIL: SNOW_FOLIAGE_SPRING_GREEN")

    def test_gate37_anisotropic_ice_wood_pass_and_planted_leopard_spots_fail(self) -> None:
        ice_clean = _make_directional_surface(160, 200, horizontal=True, base_rgb=(0.28, 0.48, 0.62))
        wood_clean = _make_directional_surface(160, 200, horizontal=False, base_rgb=(0.24, 0.36, 0.46))
        res_pass = rh2.metric_37_ice_wood_anisotropic_no_blotch(ice_clean, wood_clean)
        self.assertTrue(res_pass["passed"], res_pass)

        # Planted Defect C control: isotropic leopard-spot glacier ice
        ice_blotch = _make_leopard_spot_surface(160, 200, base_rgb=(0.28, 0.48, 0.62))
        res_fail = rh2.metric_37_ice_wood_anisotropic_no_blotch(ice_blotch, wood_clean)
        self.assertFalse(res_fail["passed"], res_fail)
        self.assertEqual(res_fail["code"], "FAIL: ICE_WOOD_ISOTROPIC_BLOTCH")

    def test_collect_total_rebirth_37_metrics_schema(self) -> None:
        h, w = 160, 200
        yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
        # Periodic tiled vanilla ground vs non-periodic high-detail RAW ground
        van_ref = np.stack([
            0.35 + 0.08 * np.sin(xx * 0.4) * np.sin(yy * 0.4),
            0.34 + 0.08 * np.sin(xx * 0.4) * np.sin(yy * 0.4),
            0.32 + 0.08 * np.sin(xx * 0.4) * np.sin(yy * 0.4),
        ], axis=-1).astype(np.float32)
        rng = np.random.default_rng(89)
        hi_detail = rng.uniform(-0.08, 0.08, size=(h, w)).astype(np.float32)
        raw_ref = np.clip(0.35 + np.stack([hi_detail, hi_detail, hi_detail], axis=-1), 0.05, 0.92)
        frames = [raw_ref.copy() for _ in range(4)]
        telemetry = {
            "frame_times_ms": [8.5] * 60,
            "pass_ms": {"depth_ms": 0.4, "ao_ms": 0.05, "gi_ms": 0.10, "shadow_ms": 0.03, "lighting_ms": 0.60, "post_ms": 0.09},
            "vram_start_mb": 4000.0,
            "vram_return_mb": 4004.0,
            "submitted_draws": 1500,
            "culled_draws": 2200,
            "hiz_occluded": 1400,
            "mean_overdraw": 1.35,
            "lod_pop_delta": 0.01,
            "vanilla_draw_calls": 0,
            "transformed_draw_calls": 1820,
            "subdiv_input_tris": 250000,
            "subdiv_output_tris": 1000000,
        }
        import json
        manifest = json.loads((REPO / "data" / "ownership" / "engine_ownership_manifest.json").read_text(encoding="utf-8"))
        passes = manifest.get("passes", [])
        ownership = {
            "owned_by_raw": sum(1 for p in passes if p.get("state") == "OWNED_BY_RAW"),
            "hybrid_intercepted": sum(1 for p in passes if p.get("state") == "HYBRID_INTERCEPTED"),
            "still_vanilla": sum(1 for p in passes if p.get("state") == "STILL_VANILLA"),
            "double_exec_total": 0,
            "flicker_total": 0,
            "passes": passes,
        }
        ice_clean = _make_directional_surface(h, w, horizontal=True)
        wood_clean = _make_directional_surface(h, w, horizontal=False)
        res = rh2.collect_total_rebirth_37_metrics(
            frames, telemetry, ownership,
            vanilla_ref_rgb=van_ref,
            raw_ref_rgb=raw_ref,
            animal_vanilla_rgb=np.full((h, w, 3), [0.46, 0.30, 0.18], dtype=np.float32),
            animal_raw_rgb=np.full((h, w, 3), [0.48, 0.31, 0.18], dtype=np.float32),
            snow_vanilla_rgb=np.full((h, w, 3), [0.44, 0.45, 0.48], dtype=np.float32),
            snow_raw_rgb=np.full((h, w, 3), [0.45, 0.46, 0.50], dtype=np.float32),
            ice_raw_rgb=ice_clean,
            wood_raw_rgb=wood_clean,
        )
        self.assertEqual(res["schema"], "raw.heuristics-v2/37")
        self.assertEqual(res["total_metrics"], 37)
        self.assertEqual(res["extended_total"], 17)
        self.assertTrue(res["all_passed"], res["failure_codes"])

    def test_wave89_cleanroom_and_typography_hygiene(self) -> None:
        forbidden = "zen" + "tropy"
        checked_paths = [
            REPO / "src" / "d3d11_proxy" / "GBufferVariants.cpp",
            REPO / "src" / "d3d12_bridge" / "shaders" / "RawTotalMaterialOverhaul.hlsli",
            REPO / "src" / "d3d12_bridge" / "shaders" / "RawMossShader.hlsli",
            REPO / "src" / "d3d12_bridge" / "shaders" / "RawWeathering.hlsli",
            REPO / "src" / "d3d12_bridge" / "RawTotalRebirth.h",
            REPO / "tests" / "wave89_total_rebirth_part2_check.cpp",
            REPO / "tests" / "test_wave89_total_rebirth_part2.py",
        ]
        for p in checked_paths:
            txt = p.read_text(encoding="utf-8", errors="ignore")
            self.assertNotIn(forbidden, txt.lower(), f"Forbidden token in {p}")
            self.assertNotIn("\u2014", txt, f"Em dash found in {p}")


if __name__ == "__main__":
    unittest.main()
