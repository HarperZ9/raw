"""Automated 20-heuristic full-pipeline visual & telemetry inspector for live RAW Skyrim SE renders.

Pure Python + Pillow + NumPy (zero OpenCV/SciPy dependencies). Covers BOTH final `improved`
renders AND all custom pipeline debug layers (`DebugView` 0..36 and `WaterView` 1..7), plus
live runtime telemetry (`bridge_live_telemetry.json`, `boot_selfcheck.txt`, `harness_results.jsonl`).

Original work, MIT.
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import math
from pathlib import Path
from typing import Any, Mapping, Sequence

import numpy as np
from PIL import Image


DEBUG_VIEW_NAMES: dict[int, str] = {
    0: "FinalLit",
    1: "WorldNormals",
    2: "SunShadow",
    3: "AmbientLight",
    4: "SkyVisibility",
    5: "RtBounceGI",
    6: "EnvMapFinal",
    7: "RtPrimaryRefl",
    8: "WeatherWetPuddleSnow",
    9: "SplitScreenCompare",
    10: "SkyOverlayMask",
    11: "CloudShadow",
    12: "MaterialClasses",
    13: "DirectPlusAmbient",
    14: "UntracedRestVis",
    15: "GBufferAlbedoRaw",
    16: "GBufferBaseAlbedo",
    17: "TonemapRatio",
    18: "EnvReflection",
    19: "CharacterLight",
    20: "LuminanceHeat",
    21: "ExposureHeat",
    22: "WaterDepthMask",
    23: "WaterNormalMask",
    24: "ShadowCascadeId",
    25: "MotionVectorMag",
    26: "RoughnessMap",
    27: "SpecularEnergy",
    28: "FullClassPlusActor14",
    29: "LinearDepthSilhouette",
    30: "ShadowStabilityAndRtSlots",
    31: "MossSnowGlacierMask",
    32: "ActorSkinSssContactAo",
    33: "NanInfHdrOverflowHeatmap",
    34: "flora_trees",
    35: "motion_outline",
    36: "fire_froxel",
}

WATER_DEBUG_VIEW_NAMES: dict[int, str] = {
    1: "water_refl",
    2: "water_caustics",
    3: "water_fresnel",
    4: "water_depth",
    5: "water_normals",
    6: "water_body",
    7: "water_flow_diag",
}

ALL_HEURISTIC_NAMES: tuple[str, ...] = (
    "detect_indoor_black_orbs",
    "detect_red_sky_fire_bleed",
    "detect_water_scanline_holes",
    "detect_water_zebra_stripes",
    "detect_sky_diagonal_cutout",
    "detect_actor_green_moss_chroma",
    "detect_dark_motion_outline",
    "detect_camera_turn_shadow_flicker",
    "detect_nan_inf_hdr_overflow",
    "detect_crushed_black_tree_trunks",
    "detect_bleached_white_pine_needles",
    "detect_water_neon_cyan_oversat",
    "detect_flat_ice_cave_triangles",
    "detect_normal_map_integrity",
    "detect_albedo_range_pbr_sanity",
    "detect_shadow_coverage_balance",
    "detect_skin_sss_sanity",
    "detect_exposure_and_tonemap_clipping",
    "detect_depth_and_stencil_integrity",
    "detect_live_world_compositor_activity",
    "analyze_motion_vector_reprojection",
    "analyze_water_temporal_flow_speed",
    "analyze_shadow_temporal_stability",
    "analyze_temporal_ghosting_and_motion_outline",
    "analyze_world_lock_anti_swimming",
)


@dataclasses.dataclass
class HeuristicResult:
    name: str
    detected: bool
    score: float
    threshold: float
    severity: str  # "pass" | "warn" | "fail"
    details: dict[str, Any]

    def to_dict(self) -> dict[str, Any]:
        return {
            "name": self.name,
            "detected": bool(self.detected),
            "score": round(float(self.score), 6),
            "threshold": round(float(self.threshold), 6),
            "severity": self.severity,
            "details": self.details,
        }


def load_rgb_float(image_or_path: str | Path | Image.Image | np.ndarray) -> np.ndarray:
    """Loads an image into a contiguous float32 HxWx3 RGB array in [0.0, 1.0]."""
    if isinstance(image_or_path, np.ndarray):
        arr = image_or_path
        if arr.ndim != 3 or arr.shape[2] < 3:
            raise ValueError(f"Expected HxWx3+ RGB array, got shape {arr.shape}")
        rgb = arr[:, :, :3].astype(np.float32, copy=False)
        if rgb.size > 0 and float(rgb.max()) > 1.5:
            rgb = rgb / 255.0
        return np.clip(rgb, 0.0, 1.0)
    if isinstance(image_or_path, Image.Image):
        im = image_or_path.convert("RGB")
        return np.asarray(im, dtype=np.float32) / 255.0
    p = Path(image_or_path)
    with Image.open(p) as im:
        return np.asarray(im.convert("RGB"), dtype=np.float32) / 255.0


def _luminance(rgb: np.ndarray) -> np.ndarray:
    return 0.2126 * rgb[:, :, 0] + 0.7152 * rgb[:, :, 1] + 0.0722 * rgb[:, :, 2]


def _saturation(rgb: np.ndarray) -> np.ndarray:
    cmax = np.max(rgb, axis=2)
    cmin = np.min(rgb, axis=2)
    return (cmax - cmin) / np.maximum(cmax, 1e-5)


def _match_shape(a: np.ndarray, b: np.ndarray) -> tuple[np.ndarray, np.ndarray]:
    if a.shape[:2] == b.shape[:2]:
        return a, b
    h = min(a.shape[0], b.shape[0])
    w = min(a.shape[1], b.shape[1])
    return a[:h, :w, :], b[:h, :w, :]


def _hud_valid_mask(h: int, w: int) -> np.ndarray:
    """Boolean mask excluding static Skyrim HUD pixels:
    - Top compass bar (`y < 0.11 * h`)
    - Center 18x18 crosshair box (`cy-9..cy+9, cx-9..cx+9`) on frames >= 64px.
    """
    mask = np.ones((h, w), dtype=bool)
    top_cut = int(h * 0.11)
    if top_cut > 0:
        mask[:top_cut, :] = False
    half = min(9, max(2, min(h, w) // 20)) if min(h, w) >= 64 else 0
    if half > 0:
        cy, cx = h // 2, w // 2
        mask[max(0, cy - half) : min(h, cy + half), max(0, cx - half) : min(w, cx + half)] = False
    return mask


# -----------------------------------------------------------------------------
# Heuristics 1..8: Baseline-Proven Live Defect Detectors
# -----------------------------------------------------------------------------

def detect_indoor_black_orbs(
    image: str | Path | Image.Image | np.ndarray,
    box_density_threshold: float = 0.92,
    min_surround_bright_frac: float = 0.35,
    is_interior: bool | None = None,
) -> HeuristicResult:
    """#1: Detects large contiguous pitch-black soot/orb blotches occluding lit interiors/caves.

    When `is_interior=False` (known outdoor scene such as High Hrothgar or Windhelm), returns PASS
    so dark architectural pillars or cliff shadows in bright exterior snow never false-flag.
    When `is_interior` is True or None, runs the full interior soot-orb check (100% baseline coverage).
    """
    if is_interior is False:
        return HeuristicResult(
            name="detect_indoor_black_orbs",
            detected=False,
            score=0.0,
            threshold=box_density_threshold,
            severity="pass",
            details={"is_interior": False, "skipped_exterior_scene": True},
        )

    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    lum = _luminance(rgb)

    bright_frac = float(np.mean(lum > 0.12))
    cy0, cy1 = int(h * 0.10), int(h * 0.86)
    cx0, cx1 = int(w * 0.10), int(w * 0.90)
    sub_dark = (lum[cy0:cy1, cx0:cx1] < 0.015).astype(np.float32)

    k = max(24, min(128, min(sub_dark.shape[0], sub_dark.shape[1]) // 4))
    if sub_dark.shape[0] <= k or sub_dark.shape[1] <= k:
        max_box = float(sub_dark.mean()) if sub_dark.size else 0.0
    else:
        ii = np.pad(sub_dark, ((1, 0), (1, 0))).cumsum(axis=0).cumsum(axis=1)
        box = (ii[k:, k:] - ii[:-k, k:] - ii[k:, :-k] + ii[:-k, :-k]) / float(k * k)
        max_box = float(box.max())

    detected = (max_box >= box_density_threshold) and (bright_frac >= min_surround_bright_frac)
    return HeuristicResult(
        name="detect_indoor_black_orbs",
        detected=detected,
        score=max_box if bright_frac >= min_surround_bright_frac else 0.0,
        threshold=box_density_threshold,
        severity="fail" if detected else "pass",
        details={
            "is_interior": True if is_interior is True else "unspecified",
            "max_black_box_density": round(max_box, 4),
            "box_size_px": int(k),
            "surround_bright_frac": round(bright_frac, 4),
            "total_pitch_black_frac": round(float(np.mean(lum < 0.015)), 4),
        },
    )


def detect_red_sky_fire_bleed(
    image: str | Path | Image.Image | np.ndarray,
    area_threshold: float = 0.025,
    debug29_image: str | Path | Image.Image | np.ndarray | None = None,
) -> HeuristicResult:
    """#2: Detects anomalous high-red/low-blue fire halo blobs bleeding onto the upper sky region.

    When `debug29_image` (`view_depth_edge`) is provided, uses its exact far-sky mask
    (`Lighting.hlsl` sky encoding: low R/G, blue-dominant navy) to filter out warm torch-lit
    stone walls (e.g. Windhelm palace walls) while catching any red bleed on true sky pixels or
    extreme saturated fiery crimson (`R > 0.65 && R-G > 0.25 && R-B > 0.35`).
    """
    rgb = load_rgb_float(image)
    h, _, _ = rgb.shape
    y0, y1 = int(h * 0.06), int(h * 0.45)
    top = rgb[y0:y1, :, :]
    r, g, b = top[:, :, 0], top[:, :, 1], top[:, :, 2]

    fiery_crimson = (r > 0.65) & ((r - g) > 0.25) & ((r - b) > 0.35)
    red_halo = (r > 0.42) & ((r - b) > 0.22) & (r > g * 1.18)

    sky_mask_used = False
    sky_frac = 1.0
    if debug29_image is not None:
        d29 = load_rgb_float(debug29_image)
        rgb_m, d29_m = _match_shape(rgb, d29)
        hm = rgb_m.shape[0]
        y0m, y1m = int(hm * 0.06), int(hm * 0.45)
        top = rgb_m[y0m:y1m, :, :]
        r, g, b = top[:, :, 0], top[:, :, 1], top[:, :, 2]
        fiery_crimson = (r > 0.65) & ((r - g) > 0.25) & ((r - b) > 0.35)
        red_halo = (r > 0.42) & ((r - b) > 0.22) & (r > g * 1.18)
        d29_top = d29_m[y0m:y1m, :, :]
        # Lighting.hlsl flags.w==29 sky encoding: linear (0.02, 0.04, 0.10) -> sRGB (~0.15, 0.22, 0.35)
        sky_mask = (
            (d29_top[:, :, 0] < 0.22)
            & (d29_top[:, :, 1] < 0.30)
            & (d29_top[:, :, 2] < 0.48)
            & (d29_top[:, :, 2] > d29_top[:, :, 0] + 0.04)
        )
        sky_mask_used = True
        sky_frac = float(np.mean(sky_mask))
        red_blob = (sky_mask & red_halo) | fiery_crimson
    else:
        red_blob = red_halo | fiery_crimson

    frac = float(np.mean(red_blob))
    detected = frac >= area_threshold
    return HeuristicResult(
        name="detect_red_sky_fire_bleed",
        detected=detected,
        score=frac,
        threshold=area_threshold,
        severity="fail" if detected else "pass",
        details={
            "sky_mask_used": sky_mask_used,
            "upper_sky_frac": round(sky_frac, 4),
            "red_sky_area_frac": round(frac, 5),
            "red_sky_pixels": int(red_blob.sum()),
            "upper_roi_pixels": int(red_blob.size),
        },
    )


def detect_water_scanline_holes(
    image: str | Path | Image.Image | np.ndarray,
    alt_threshold: float = 0.060,
    scan_threshold: float = 0.032,
    water_depth_image: str | Path | Image.Image | np.ndarray | None = None,
    water_flow_diag_image: str | Path | Image.Image | np.ndarray | None = None,
) -> HeuristicResult:
    """#3: Detects high-frequency horizontal row-alternating scanline banding inside water regions.

    Accepts optional `water_depth_image` (`WaterView=4`) to mask water pixels precisely (suppressing
    stone/bush false alarms) and optional `water_flow_diag_image` (`WaterView=7`) to catch direct
    magenta shallow-depth continuity alarms.
    """
    if water_flow_diag_image is not None:
        wf = load_rgb_float(water_flow_diag_image)
        mag = (wf[:, :, 0] > 0.78) & (wf[:, :, 2] > 0.78) & (wf[:, :, 1] < 0.22)
        mag_frac = float(np.mean(mag))
        detected_v7 = mag_frac > 0.0005
        return HeuristicResult(
            name="detect_water_scanline_holes",
            detected=detected_v7,
            score=mag_frac,
            threshold=0.0005,
            severity="fail" if detected_v7 else "pass",
            details={
                "mode": "water_flow_diag_view7",
                "magenta_continuity_alarm_frac": round(mag_frac, 6),
                "magenta_pixels": int(mag.sum()),
            },
        )

    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    lum = _luminance(rgb)

    y0, y1 = int(h * 0.35), int(h * 0.92)
    roi = rgb[y0:y1, :, :]
    lum_roi = lum[y0:y1, :]

    if water_depth_image is not None:
        wd = load_rgb_float(water_depth_image)
        rgb, wd = _match_shape(rgb, wd)
        h, w, _ = rgb.shape
        y0, y1 = int(h * 0.35), int(h * 0.92)
        lum_roi = _luminance(rgb)[y0:y1, :]
        wd_lum = _luminance(wd[y0:y1, :, :])
        water_mask = wd_lum > 0.04
        mode = "water_depth_masked"
    else:
        r, g, b = roi[:, :, 0], roi[:, :, 1], roi[:, :, 2]
        water_mask = (b > r * 1.12) & (b > 0.14) & (((b + g) * 0.5) > (r + 0.03))
        mode = "rgb_water_heuristic"

    w_m = water_mask[1:, 1:]
    w_cnt = int(w_m.sum())
    min_water_px = max(400, int(0.015 * h * w))

    if w_cnt < min_water_px:
        return HeuristicResult(
            name="detect_water_scanline_holes",
            detected=False,
            score=0.0,
            threshold=alt_threshold,
            severity="pass",
            details={"mode": mode, "water_pixels": w_cnt, "skipped": True},
        )

    dy = np.abs(lum_roi[1:, :] - lum_roi[:-1, :])
    dx = np.abs(lum_roi[:, 1:] - lum_roi[:, :-1])
    scan_px = w_m & (dy[:, 1:] > 0.07) & (dx[1:, :] < 0.04)
    scan_ratio = float(scan_px.sum()) / float(w_cnt)

    d1 = lum_roi[1:, :] - lum_roi[:-1, :]
    alt_px = (d1[1:, :] * d1[:-1, :] < -0.0015) & water_mask[1:-1, :]
    alt_ratio = float(alt_px.sum()) / float(w_cnt)

    detected = (alt_ratio >= alt_threshold) and (scan_ratio >= scan_threshold)
    return HeuristicResult(
        name="detect_water_scanline_holes",
        detected=detected,
        score=alt_ratio if scan_ratio >= scan_threshold else min(alt_ratio, scan_ratio),
        threshold=alt_threshold,
        severity="fail" if detected else "pass",
        details={
            "mode": mode,
            "water_pixels": w_cnt,
            "row_alternating_ratio": round(alt_ratio, 5),
            "scanline_step_ratio": round(scan_ratio, 5),
        },
    )


def detect_water_zebra_stripes(
    image: str | Path | Image.Image | np.ndarray,
    spectral_peak_threshold: float = 12.0,
    min_anisotropy: float = 1.25,
    min_dy: float = 0.006,
) -> HeuristicResult:
    """#4: Detects periodic narrow zebra-stripe spectral peaks in shallow water/shoreline bands."""
    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    lum = _luminance(rgb)

    y0, y1 = int(h * 0.40), int(h * 0.90)
    x0, x1 = int(w * 0.10), int(w * 0.90)
    roi_rgb = rgb[y0:y1, x0:x1, :]
    roi = lum[y0:y1, x0:x1]

    r, g, b = roi_rgb[:, :, 0], roi_rgb[:, :, 1], roi_rgb[:, :, 2]
    water_frac = float(np.mean((b > r * 1.10) & (b > 0.14) & (((b + g) * 0.5) > (r + 0.025))))
    if water_frac < 0.25 or roi.shape[0] < 48 or roi.shape[1] < 48:
        return HeuristicResult(
            name="detect_water_zebra_stripes",
            detected=False,
            score=0.0,
            threshold=spectral_peak_threshold,
            severity="pass",
            details={"water_frac": round(water_frac, 4), "skipped": True},
        )

    roi_zm_y = roi - roi.mean(axis=0, keepdims=True)
    spec_y = (np.abs(np.fft.rfft(roi_zm_y, axis=0)) ** 2).mean(axis=1)
    ky_lo = max(3, roi.shape[0] // 40)
    ky_hi = min(len(spec_y) - 1, max(ky_lo + 4, roi.shape[0] // 6))
    band_y = spec_y[ky_lo:ky_hi]
    peak_y = float(band_y.max() / (float(np.median(band_y)) + 1e-8)) if band_y.size else 0.0

    roi_zm_x = roi - roi.mean(axis=1, keepdims=True)
    spec_x = (np.abs(np.fft.rfft(roi_zm_x, axis=1)) ** 2).mean(axis=0)
    kx_lo = max(3, roi.shape[1] // 40)
    kx_hi = min(len(spec_x) - 1, max(kx_lo + 4, roi.shape[1] // 6))
    band_x = spec_x[kx_lo:kx_hi]
    peak_x = float(band_x.max() / (float(np.median(band_x)) + 1e-8)) if band_x.size else 0.0

    dy = float(np.abs(roi[1:, :] - roi[:-1, :]).mean())
    dx = float(np.abs(roi[:, 1:] - roi[:, :-1]).mean())
    aniso = dy / (dx + 1e-6)
    peak_max = max(peak_y, peak_x)

    detected = (peak_max >= spectral_peak_threshold) and (dy >= min_dy) and (aniso >= min_anisotropy)
    return HeuristicResult(
        name="detect_water_zebra_stripes",
        detected=detected,
        score=peak_max if (dy >= min_dy and aniso >= min_anisotropy) else 0.0,
        threshold=spectral_peak_threshold,
        severity="fail" if detected else "pass",
        details={
            "water_frac": round(water_frac, 4),
            "fft_peak_y": round(peak_y, 2),
            "fft_peak_x": round(peak_x, 2),
            "grad_anisotropy_dy_dx": round(aniso, 3),
            "mean_abs_dy": round(dy, 5),
        },
    )


def detect_sky_diagonal_cutout(
    image: str | Path | Image.Image | np.ndarray,
    collinear_ratio_threshold: float = 0.28,
) -> HeuristicResult:
    """#5: Detects razor-sharp straight diagonal or L-shaped luminance/chroma seams in the sky hemisphere."""
    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    if w > 800:
        scale = max(1, w // 640)
        rgb_s = rgb[::scale, ::scale, :]
    else:
        rgb_s = rgb

    hs, ws, _ = rgb_s.shape
    y0, y1 = int(hs * 0.08), int(hs * 0.62)
    top = rgb_s[y0:y1, :, :]
    lum = _luminance(top)
    blue_excess = top[:, :, 2] - top[:, :, 0]

    both_bright = (lum[2:, :] > 0.30) & (lum[:-2, :] > 0.30)
    has_blue_side = (blue_excess[2:, :] > 0.14) | (blue_excess[:-2, :] > 0.14)
    chroma_step = np.abs(blue_excess[2:, :] - blue_excess[:-2, :]) > 0.085
    edge_map = both_bright & has_blue_side & chroma_step

    ys, xs = np.nonzero(edge_map)
    if ys.size < 40:
        return HeuristicResult(
            name="detect_sky_diagonal_cutout",
            detected=False,
            score=0.0,
            threshold=collinear_ratio_threshold,
            severity="pass",
            details={"sky_edge_pixels": int(ys.size), "best_angle_deg": 0.0, "max_collinear_x_cols": 0},
        )

    ys_f = ys.astype(np.float32)
    xs_f = xs.astype(np.float32)
    best_votes = 0
    best_angle = 0.0
    for deg_idx in range(-24, 25):
        deg = deg_idx * 1.5
        rad = math.radians(deg)
        rho = np.round((ys_f * math.cos(rad) - xs_f * math.sin(rad)) * 0.5).astype(np.int32)
        vals, counts = np.unique(rho, return_counts=True)
        top_idx = int(np.argmax(counts))
        if int(counts[top_idx]) > best_votes:
            bin_xs = xs[rho == vals[top_idx]]
            unique_x = int(np.unique(bin_xs).size)
            if unique_x > best_votes:
                best_votes = unique_x
                best_angle = deg

    collinear_ratio = float(best_votes) / float(max(1, ws))
    detected = collinear_ratio >= collinear_ratio_threshold
    return HeuristicResult(
        name="detect_sky_diagonal_cutout",
        detected=detected,
        score=collinear_ratio,
        threshold=collinear_ratio_threshold,
        severity="fail" if detected else "pass",
        details={
            "sky_edge_pixels": int(ys.size),
            "max_collinear_x_cols": int(best_votes),
            "scaled_width": int(ws),
            "collinear_width_ratio": round(collinear_ratio, 4),
            "best_angle_deg": round(best_angle, 2),
        },
    )


def detect_actor_green_moss_chroma(
    image: str | Path | Image.Image | np.ndarray,
    moss_frac_threshold: float = 0.16,
    debug_class_image: str | Path | Image.Image | np.ndarray | None = None,
    debug_mask_image: str | Path | Image.Image | np.ndarray | None = None,
) -> HeuristicResult:
    """#6: Detects anomalous moss-green chroma leakage on foreground actors/creatures."""
    if debug_class_image is not None and debug_mask_image is not None:
        cls_rgb, msk_rgb = _match_shape(load_rgb_float(debug_class_image), load_rgb_float(debug_mask_image))
        # DebugView 28 encodes base==14 actor in vivid orange/magenta, skin in red:
        actor_mask = (
            ((cls_rgb[:, :, 0] > 0.70) & (cls_rgb[:, :, 2] > 0.70) & (cls_rgb[:, :, 1] < 0.35))
            | ((cls_rgb[:, :, 0] > 0.80) & (cls_rgb[:, :, 1] > 0.35) & (cls_rgb[:, :, 1] < 0.65) & (cls_rgb[:, :, 2] < 0.22))
            | ((cls_rgb[:, :, 0] > 0.75) & (cls_rgb[:, :, 1] < 0.35) & (cls_rgb[:, :, 2] < 0.35))
        )
        moss_mask = msk_rgb[:, :, 1] > 0.12
        actor_px = int(actor_mask.sum())
        overlap_px = int((actor_mask & moss_mask).sum())
        overlap_ratio = float(overlap_px) / float(max(1, actor_px))
        thr = 0.002
        detected = (actor_px >= 64) and (overlap_ratio > thr)
        return HeuristicResult(
            name="detect_actor_green_moss_chroma",
            detected=detected,
            score=overlap_ratio,
            threshold=thr,
            severity="fail" if detected else "pass",
            details={
                "mode": "debug_view_28_31_overlap",
                "actor_pixels": actor_px,
                "moss_overlap_pixels": overlap_px,
                "overlap_ratio": round(overlap_ratio, 5),
            },
        )

    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    roi = rgb[int(h * 0.35):int(h * 0.88), int(w * 0.25):int(w * 0.75), :]
    r, g, b = roi[:, :, 0], roi[:, :, 1], roi[:, :, 2]
    moss_px = (g > 0.12) & (g > r * 1.12) & (g > b * 1.28) & ((g - b) > 0.05)
    frac = float(np.mean(moss_px))
    detected = frac >= moss_frac_threshold
    return HeuristicResult(
        name="detect_actor_green_moss_chroma",
        detected=detected,
        score=frac,
        threshold=moss_frac_threshold,
        severity="fail" if detected else "pass",
        details={
            "mode": "rgb_foreground_roi",
            "moss_chroma_frac": round(frac, 5),
            "moss_pixels": int(moss_px.sum()),
        },
    )


def detect_dark_motion_outline(
    image: str | Path | Image.Image | np.ndarray,
    trough_threshold: float = 0.0032,
    debug28_image: str | Path | Image.Image | np.ndarray | None = None,
    debug29_image: str | Path | Image.Image | np.ndarray | None = None,
) -> HeuristicResult:
    """#7: Detects thin 1-4px dark cel-shading contour rims along high-contrast foreground silhouettes.

    Accepts optional `debug28_image` / `debug29_image` masks to restrict checks to real silhouette
    boundaries (suppressing stone-mortar / bush false positives) while keeping 100% RGB fallback.
    """
    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    lum = _luminance(rgb)
    y0, y1 = int(h * 0.15), int(h * 0.85)
    x0, x1 = int(w * 0.10), int(w * 0.90)
    l = lum[y0:y1, x0:x1]
    if l.shape[0] < 16 or l.shape[1] < 16:
        return HeuristicResult(
            name="detect_dark_motion_outline",
            detected=False,
            score=0.0,
            threshold=trough_threshold,
            severity="pass",
            details={"skipped": True},
        )

    c = l[3:-3, 3:-3]
    dark_trough_x = (c < 0.08) & ((l[3:-3, :-6] - c) > 0.12) & ((l[3:-3, 6:] - c) > 0.12)
    dark_trough_y = (c < 0.08) & ((l[:-6, 3:-3] - c) > 0.12) & ((l[6:, 3:-3] - c) > 0.12)
    trough = dark_trough_x | dark_trough_y

    mode = "rgb_trough"
    if debug29_image is not None:
        _, d29 = _match_shape(rgb, load_rgb_float(debug29_image))
        d_sub = _luminance(d29[y0:y1, x0:x1])
        # High-contrast silhouette jumps in DebugView 29
        edge_x = np.abs(d_sub[3:-3, 6:] - d_sub[3:-3, :-6]) > 0.06
        edge_y = np.abs(d_sub[6:, 3:-3] - d_sub[:-6, 3:-3]) > 0.06
        sil_mask = edge_x | edge_y
        sil_cnt = int(sil_mask.sum())
        if sil_cnt >= 64:
            trough = trough & sil_mask
            mode = "debug29_silhouette_masked"
    elif debug28_image is not None:
        _, d28 = _match_shape(rgb, load_rgb_float(debug28_image))
        c_sub = d28[y0:y1, x0:x1]
        # Restrict near class boundaries so static mortar lines inside a single class are ignored
        cls_edge_x = np.max(np.abs(c_sub[3:-3, 6:] - c_sub[3:-3, :-6]), axis=2) > 0.15
        cls_edge_y = np.max(np.abs(c_sub[6:, 3:-3] - c_sub[:-6, 3:-3]), axis=2) > 0.15
        bnd = cls_edge_x | cls_edge_y
        if int(bnd.sum()) >= 64:
            trough = trough & bnd
            mode = "debug28_class_boundary_masked"

    trough_frac = float(np.mean(trough))
    detected = trough_frac >= trough_threshold
    return HeuristicResult(
        name="detect_dark_motion_outline",
        detected=detected,
        score=trough_frac,
        threshold=trough_threshold,
        severity="fail" if detected else "pass",
        details={
            "mode": mode,
            "dark_trough_frac": round(trough_frac, 6),
            "dark_trough_pixels": int(trough.sum()),
        },
    )


def detect_camera_turn_shadow_flicker(
    frame_a: str | Path | Image.Image | np.ndarray,
    frame_b: str | Path | Image.Image | np.ndarray,
    shift_frac_threshold: float = 0.18,
    p95_diff_threshold: float = 0.24,
) -> HeuristicResult:
    """#8: Compares consecutive frames across a camera turn to measure abrupt shadow/luminance flicker."""
    a, b = _match_shape(load_rgb_float(frame_a), load_rgb_float(frame_b))
    la = _luminance(a)
    lb = _luminance(b)
    diff = np.abs(la - lb)
    mean_diff = float(diff.mean())
    p95_diff = float(np.percentile(diff, 95))
    shift_frac = float(np.mean(diff > 0.15))
    mean_lum_jump = abs(float(la.mean()) - float(lb.mean()))

    detected = (shift_frac >= shift_frac_threshold) or (p95_diff >= p95_diff_threshold)
    return HeuristicResult(
        name="detect_camera_turn_shadow_flicker",
        detected=detected,
        score=shift_frac,
        threshold=shift_frac_threshold,
        severity="fail" if detected else "pass",
        details={
            "mean_abs_diff": round(mean_diff, 5),
            "p95_abs_diff": round(p95_diff, 5),
            "abrupt_shift_frac_gt_015": round(shift_frac, 5),
            "global_mean_lum_jump": round(mean_lum_jump, 5),
        },
    )


# -----------------------------------------------------------------------------
# Heuristics 9..20: Wave 64 Full-Pipeline Debug Layer & Telemetry Auditors
# -----------------------------------------------------------------------------

def detect_nan_inf_hdr_overflow(
    debug33_image: str | Path | Image.Image | np.ndarray,
    max_allowed_pixels: int = 0,
) -> HeuristicResult:
    """#9: Inspects DebugView 33 (NanInfHdrOverflowHeatmap) -- ZERO magenta alarm pixels allowed!"""
    rgb = load_rgb_float(debug33_image)
    r, g, b = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]
    magenta_alarm = (r > 0.85) & (b > 0.85) & (g < 0.15)
    alarm_px = int(magenta_alarm.sum())
    alarm_frac = float(alarm_px) / float(max(1, magenta_alarm.size))
    detected = alarm_px > max_allowed_pixels
    return HeuristicResult(
        name="detect_nan_inf_hdr_overflow",
        detected=detected,
        score=float(alarm_px),
        threshold=float(max_allowed_pixels),
        severity="fail" if detected else "pass",
        details={
            "magenta_alarm_pixels": alarm_px,
            "magenta_alarm_frac": round(alarm_frac, 6),
        },
    )


def detect_crushed_black_tree_trunks(
    image: str | Path | Image.Image | np.ndarray,
    debug34_image: str | Path | Image.Image | np.ndarray | None = None,
    debug34_red_threshold: float = 0.002,
    rgb_crush_threshold: float = 0.035,
) -> HeuristicResult:
    """#10: Detects crushed pitch-black (#050505) tree trunks via DebugView 34 red alarm or RGB bark scan."""
    if debug34_image is not None:
        d34 = load_rgb_float(debug34_image)
        red_alarm = (d34[:, :, 0] > 0.82) & (d34[:, :, 1] < 0.18) & (d34[:, :, 2] < 0.18)
        frac = float(np.mean(red_alarm))
        detected = frac >= debug34_red_threshold
        return HeuristicResult(
            name="detect_crushed_black_tree_trunks",
            detected=detected,
            score=frac,
            threshold=debug34_red_threshold,
            severity="fail" if detected else "pass",
            details={"mode": "debug34_red_alarm", "crushed_bark_alarm_frac": round(frac, 5), "alarm_pixels": int(red_alarm.sum())},
        )

    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    lum = _luminance(rgb)
    bright_frac = float(np.mean(lum > 0.14))
    if bright_frac < 0.40:
        return HeuristicResult(
            name="detect_crushed_black_tree_trunks",
            detected=False,
            score=0.0,
            threshold=rgb_crush_threshold,
            severity="pass",
            details={"mode": "rgb_vertical_bark", "skipped_dark_scene": True, "bright_frac": round(bright_frac, 4)},
        )

    roi = lum[int(h * 0.12):int(h * 0.82), int(w * 0.05):int(w * 0.95)]
    crushed = roi < 0.020
    # Require vertical coherence (tree trunk columns rather than isolated speckles): >= 16 consecutive dark rows
    win = max(8, roi.shape[0] // 24)
    cs = np.pad(crushed.astype(np.int32), ((1, 0), (0, 0))).cumsum(axis=0)
    vert_runs = (cs[win:, :] - cs[:-win, :]) >= int(win * 0.90)
    frac = float(np.mean(vert_runs)) if vert_runs.size else 0.0
    detected = frac >= rgb_crush_threshold
    return HeuristicResult(
        name="detect_crushed_black_tree_trunks",
        detected=detected,
        score=frac,
        threshold=rgb_crush_threshold,
        severity="fail" if detected else "pass",
        details={
            "mode": "rgb_vertical_bark",
            "crushed_vertical_frac": round(frac, 5),
            "bright_frac": round(bright_frac, 4),
        },
    )


def detect_bleached_white_pine_needles(
    image: str | Path | Image.Image | np.ndarray,
    debug34_image: str | Path | Image.Image | np.ndarray | None = None,
    debug34_magenta_threshold: float = 0.002,
    rgb_bleach_threshold: float = 0.055,
) -> HeuristicResult:
    """#11: Detects chalky bleached pale grey-white pine foliage via DebugView 34 magenta alarm or RGB canopy check."""
    if debug34_image is not None:
        d34 = load_rgb_float(debug34_image)
        mag_alarm = (d34[:, :, 0] > 0.82) & (d34[:, :, 2] > 0.82) & (d34[:, :, 1] < 0.18)
        frac = float(np.mean(mag_alarm))
        detected = frac >= debug34_magenta_threshold
        return HeuristicResult(
            name="detect_bleached_white_pine_needles",
            detected=detected,
            score=frac,
            threshold=debug34_magenta_threshold,
            severity="fail" if detected else "pass",
            details={"mode": "debug34_magenta_alarm", "bleached_foliage_frac": round(frac, 5), "alarm_pixels": int(mag_alarm.sum())},
        )

    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    roi = rgb[int(h * 0.12):int(h * 0.65), int(w * 0.05):int(w * 0.95), :]
    lum = _luminance(roi)
    sat = _saturation(roi)
    # High-frequency textured canopy (excludes smooth sky/clouds!) that is chalky pale grey-white
    grad = np.abs(lum[1:, 1:] - lum[:-1, 1:]) + np.abs(lum[1:, 1:] - lum[1:, :-1])
    chalk_px = (sat[1:, 1:] < 0.10) & (lum[1:, 1:] > 0.46) & (lum[1:, 1:] < 0.88) & (grad > 0.06)
    frac = float(np.mean(chalk_px))
    detected = frac >= rgb_bleach_threshold
    return HeuristicResult(
        name="detect_bleached_white_pine_needles",
        detected=detected,
        score=frac,
        threshold=rgb_bleach_threshold,
        severity="fail" if detected else "pass",
        details={"mode": "rgb_textured_chalk", "bleached_canopy_frac": round(frac, 5)},
    )


def detect_water_neon_cyan_oversat(
    image: str | Path | Image.Image | np.ndarray,
    water_body_image: str | Path | Image.Image | np.ndarray | None = None,
    oversat_ratio_threshold: float = 0.12,
) -> HeuristicResult:
    """#12: Detects unnatural cartoon electric-cyan oversaturation in water pixels or WaterView 6 (`water_body`)."""
    src = load_rgb_float(water_body_image if water_body_image is not None else image)
    h, w, _ = src.shape
    roi = src if water_body_image is not None else src[int(h * 0.35):int(h * 0.92), :, :]
    r, g, b = roi[:, :, 0], roi[:, :, 1], roi[:, :, 2]
    sat = _saturation(roi)

    if water_body_image is not None:
        water_mask = _luminance(roi) > 0.03
        mode = "water_body_view6"
    else:
        water_mask = (b > r * 1.12) & (b > 0.14) & (((b + g) * 0.5) > (r + 0.03))
        mode = "rgb_water_mask"

    w_cnt = int(water_mask.sum())
    if w_cnt < max(200, int(0.01 * h * w)):
        return HeuristicResult(
            name="detect_water_neon_cyan_oversat",
            detected=False,
            score=0.0,
            threshold=oversat_ratio_threshold,
            severity="pass",
            details={"mode": mode, "water_pixels": w_cnt, "skipped": True},
        )

    neon_cyan = water_mask & (b > 0.52) & (g > 0.45) & ((b - r) > 0.34) & ((g - r) > 0.24) & (sat > 0.62)
    ratio = float(neon_cyan.sum()) / float(w_cnt)
    detected = ratio >= oversat_ratio_threshold
    return HeuristicResult(
        name="detect_water_neon_cyan_oversat",
        detected=detected,
        score=ratio,
        threshold=oversat_ratio_threshold,
        severity="fail" if detected else "pass",
        details={"mode": mode, "water_pixels": w_cnt, "neon_cyan_ratio": round(ratio, 5)},
    )


def detect_flat_ice_cave_triangles(
    image: str | Path | Image.Image | np.ndarray,
    flat_block_threshold: float = 0.72,
    min_facet_edge_frac: float = 0.0008,
) -> HeuristicResult:
    """#13: Checks icy/glacial regions for flat untextured low-variance polygonal triangles.

    Excludes bright open upper sky so clear exterior skies never false-positive, and requires
    sharp polygonal facet edges separating flat uniform low-variance blue ice blocks.
    """
    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    lum = _luminance(rgb)
    r, g, b = rgb[:, :, 0], rgb[:, :, 1], rgb[:, :, 2]

    # Skip open bright daylight sky exteriors (ice caves & glacial walls have vertical/tilted facets, not open sky)
    top_h = max(1, int(h * 0.38))
    open_sky_frac = float(np.mean((lum[:top_h, :] > 0.42) & (b[:top_h, :] > r[:top_h, :] + 0.08)))
    if open_sky_frac > 0.30:
        return HeuristicResult(
            name="detect_flat_ice_cave_triangles",
            detected=False,
            score=0.0,
            threshold=flat_block_threshold,
            severity="pass",
            details={"open_sky_frac": round(open_sky_frac, 4), "skipped_open_sky": True},
        )

    ice_mask = (b > r + 0.04) & (b > 0.16) & (lum > 0.08) & (lum < 0.75) & (g > r * 1.02)
    ice_frac = float(np.mean(ice_mask))
    if ice_frac < 0.22 or h < 32 or w < 32:
        return HeuristicResult(
            name="detect_flat_ice_cave_triangles",
            detected=False,
            score=0.0,
            threshold=flat_block_threshold,
            severity="pass",
            details={"ice_frac": round(ice_frac, 4), "skipped": True},
        )

    # Sharp vertical/tilted polygonal facet boundary edges inside the ice mask (ignores horizontal horizon lines)
    gx = np.abs(lum[:, 1:] - lum[:, :-1])
    ice_inner = ice_mask[:, 1:] & ice_mask[:, :-1]
    ice_inner_cnt = int(ice_inner.sum())
    facet_edges = ice_inner & (gx > 0.035)
    facet_edge_frac = float(facet_edges.sum()) / float(max(1, ice_inner_cnt))

    # Partition into 8x8 blocks and measure micro-detail luminance std inside ice-dominant blocks
    bh, bw = h // 8, w // 8
    lum_b = lum[: bh * 8, : bw * 8].reshape(bh, 8, bw, 8).transpose(0, 2, 1, 3)
    ice_b = ice_mask[: bh * 8, : bw * 8].reshape(bh, 8, bw, 8).transpose(0, 2, 1, 3)
    ice_block = ice_b.mean(axis=(2, 3)) > 0.75
    ice_block_cnt = int(ice_block.sum())
    if ice_block_cnt < 16:
        return HeuristicResult(
            name="detect_flat_ice_cave_triangles",
            detected=False,
            score=0.0,
            threshold=flat_block_threshold,
            severity="pass",
            details={"ice_blocks": ice_block_cnt, "skipped": True},
        )

    stds = lum_b.std(axis=(2, 3))
    flat_ice = ice_block & (stds < 0.0055)
    flat_ratio = float(flat_ice.sum()) / float(ice_block_cnt)
    detected = (flat_ratio >= flat_block_threshold) and (facet_edge_frac >= min_facet_edge_frac)
    return HeuristicResult(
        name="detect_flat_ice_cave_triangles",
        detected=detected,
        score=flat_ratio if facet_edge_frac >= min_facet_edge_frac else 0.0,
        threshold=flat_block_threshold,
        severity="fail" if detected else "pass",
        details={
            "ice_frac": round(ice_frac, 4),
            "ice_blocks": ice_block_cnt,
            "flat_ice_block_ratio": round(flat_ratio, 4),
            "facet_edge_frac": round(facet_edge_frac, 5),
            "median_ice_block_std": round(float(np.median(stds[ice_block])), 5),
        },
    )


def detect_normal_map_integrity(
    debug1_image: str | Path | Image.Image | np.ndarray,
    max_bad_len_frac: float = 0.12,
    max_zero_hole_frac: float = 0.08,
) -> HeuristicResult:
    """#14: Audits DebugView 1 (`WorldNormals`, n*0.5+0.5) for unit-length norms, zero NaN holes & diversity.

    Masks out static Skyrim HUD pixels (top 11% compass bar & 18px center crosshair box) so UI overlay
    elements never corrupt world-normal unit-length checks.
    """
    rgb = load_rgb_float(debug1_image)
    h, w, _ = rgb.shape
    hud_ok = _hud_valid_mask(h, w)
    zero_holes = hud_ok & (rgb[:, :, 0] < 0.01) & (rgb[:, :, 1] < 0.01) & (rgb[:, :, 2] < 0.01)
    hud_cnt = max(1, int(hud_ok.sum()))
    zero_frac = float(zero_holes.sum()) / float(hud_cnt)
    valid = hud_ok & (~zero_holes)
    if int(valid.sum()) < 64:
        return HeuristicResult(
            name="detect_normal_map_integrity",
            detected=True,
            score=1.0,
            threshold=max_zero_hole_frac,
            severity="fail",
            details={"zero_hole_frac": round(zero_frac, 5), "reason": "all_black_normal_buffer"},
        )

    n = rgb[valid] * 2.0 - 1.0
    lengths = np.linalg.norm(n, axis=1)
    bad_len_frac = float(np.mean(np.abs(lengths - 1.0) > 0.28))
    div_std = float(np.mean(n.std(axis=0)))
    degenerate = div_std < 0.03

    detected = (bad_len_frac > max_bad_len_frac) or (zero_frac > max_zero_hole_frac) or degenerate
    score = max(bad_len_frac, zero_frac, 1.0 if degenerate else 0.0)
    return HeuristicResult(
        name="detect_normal_map_integrity",
        detected=detected,
        score=score,
        threshold=max_bad_len_frac,
        severity="fail" if detected else "pass",
        details={
            "hud_masked": True,
            "zero_hole_frac": round(zero_frac, 5),
            "bad_unit_len_frac": round(bad_len_frac, 5),
            "mean_norm_length": round(float(lengths.mean()), 4),
            "normal_diversity_std": round(div_std, 4),
        },
    )


def detect_albedo_range_pbr_sanity(
    debug16_image: str | Path | Image.Image | np.ndarray,
    max_black_frac: float = 0.20,
    max_white_frac: float = 0.14,
) -> HeuristicResult:
    """#15: Audits DebugView 16 (`GBufferBaseAlbedo`) for physically bounded PBR albedo in [0.02..0.92]."""
    rgb = load_rgb_float(debug16_image)
    lum = _luminance(rgb)
    too_black = float(np.mean(lum < 0.015))
    too_white = float(np.mean(lum > 0.94))
    mean_lum = float(lum.mean())
    out_of_mean = (mean_lum < 0.03) or (mean_lum > 0.80)

    detected = (too_black > max_black_frac) or (too_white > max_white_frac) or out_of_mean
    score = max(too_black, too_white)
    return HeuristicResult(
        name="detect_albedo_range_pbr_sanity",
        detected=detected,
        score=score,
        threshold=max_black_frac,
        severity="fail" if detected else "pass",
        details={
            "albedo_mean_lum": round(mean_lum, 4),
            "below_0015_frac": round(too_black, 4),
            "above_094_frac": round(too_white, 4),
        },
    )


def detect_shadow_coverage_balance(
    debug_shadow_image: str | Path | Image.Image | np.ndarray,
) -> HeuristicResult:
    """#16: Audits DebugView 2 (`SunShadow`) or 30 (`ShadowStability`) for neither 100% black nor 100% flat white."""
    rgb = load_rgb_float(debug_shadow_image)
    lum = _luminance(rgb)
    mean_l = float(lum.mean())
    std_l = float(lum.std())

    all_black = mean_l < 0.02
    all_flat_white = (mean_l > 0.992) and (std_l < 0.015)
    detected = all_black or all_flat_white
    score = 1.0 if detected else abs(mean_l - 0.5)
    return HeuristicResult(
        name="detect_shadow_coverage_balance",
        detected=detected,
        score=score,
        threshold=0.99,
        severity="fail" if detected else "pass",
        details={
            "shadow_mean_lum": round(mean_l, 4),
            "shadow_std_lum": round(std_l, 4),
            "all_black": all_black,
            "all_flat_white": all_flat_white,
        },
    )


def detect_skin_sss_sanity(
    debug32_image: str | Path | Image.Image | np.ndarray,
    max_spike_frac: float = 0.20,
) -> HeuristicResult:
    """#17: Audits DebugView 32 (`ActorSkinSssContactAo`) for smooth skin wrap with zero harsh terminator spikes."""
    rgb = load_rgb_float(debug32_image)
    lum = _luminance(rgb)
    mag_alarm = (rgb[:, :, 0] > 0.85) & (rgb[:, :, 2] > 0.85) & (rgb[:, :, 1] < 0.15)
    if int(mag_alarm.sum()) > 0:
        return HeuristicResult(
            name="detect_skin_sss_sanity",
            detected=True,
            score=1.0,
            threshold=max_spike_frac,
            severity="fail",
            details={"magenta_nan_pixels": int(mag_alarm.sum())},
        )

    skin_mask = lum > 0.05
    skin_cnt = int(skin_mask.sum())
    if skin_cnt < 64:
        return HeuristicResult(
            name="detect_skin_sss_sanity",
            detected=False,
            score=0.0,
            threshold=max_spike_frac,
            severity="pass",
            details={"skin_pixels": skin_cnt, "skipped_no_actors": True},
        )

    inner = skin_mask[1:, 1:] & skin_mask[:-1, 1:] & skin_mask[1:, :-1]
    inner_cnt = int(inner.sum())
    if inner_cnt < 32:
        return HeuristicResult(
            name="detect_skin_sss_sanity",
            detected=False,
            score=0.0,
            threshold=max_spike_frac,
            severity="pass",
            details={"skin_pixels": skin_cnt, "skipped_small_roi": True},
        )

    grad = np.maximum(np.abs(lum[1:, 1:] - lum[:-1, 1:]), np.abs(lum[1:, 1:] - lum[1:, :-1]))
    harsh = inner & (grad > 0.25)
    spike_frac = float(harsh.sum()) / float(inner_cnt)
    detected = spike_frac >= max_spike_frac
    return HeuristicResult(
        name="detect_skin_sss_sanity",
        detected=detected,
        score=spike_frac,
        threshold=max_spike_frac,
        severity="fail" if detected else "pass",
        details={
            "skin_pixels": skin_cnt,
            "harsh_terminator_frac": round(spike_frac, 5),
            "p99_skin_grad": round(float(np.percentile(grad[inner], 99)), 4),
        },
    )


def detect_exposure_and_tonemap_clipping(
    image: str | Path | Image.Image | np.ndarray,
    max_crushed_black_frac: float = 0.38,
    max_blown_white_frac: float = 0.18,
) -> HeuristicResult:
    """#18: Measures crushed-black % and blown-highlight % on final `improved` tonemapped output."""
    rgb = load_rgb_float(image)
    lum = _luminance(rgb)
    crushed = float(np.mean(lum < 0.008))
    blown = float(np.mean((rgb[:, :, 0] > 0.99) & (rgb[:, :, 1] > 0.99) & (rgb[:, :, 2] > 0.99)))
    mean_l = float(lum.mean())
    extreme_mean = (mean_l < 0.025) or (mean_l > 0.88)

    detected = (crushed > max_crushed_black_frac) or (blown > max_blown_white_frac) or extreme_mean
    score = max(crushed, blown)
    return HeuristicResult(
        name="detect_exposure_and_tonemap_clipping",
        detected=detected,
        score=score,
        threshold=max_blown_white_frac,
        severity="fail" if detected else "pass",
        details={
            "mean_lum": round(mean_l, 4),
            "crushed_black_frac": round(crushed, 4),
            "blown_white_frac": round(blown, 4),
        },
    )


def detect_depth_and_stencil_integrity(
    debug29_image: str | Path | Image.Image | np.ndarray,
    min_dynamic_range: float | None = None,
    max_zero_frac: float = 0.85,
    is_interior: bool | None = None,
) -> HeuristicResult:
    """#19: Audits DebugView 29 (`LinearDepthSilhouette`) for smooth depth range & clean sky separation.

    Uses `0.025` min dynamic range for enclosed interiors (`is_interior=True`) and `0.08` for exteriors.
    """
    eff_min_range = (
        float(min_dynamic_range)
        if min_dynamic_range is not None
        else (0.025 if is_interior is True else 0.08)
    )
    rgb = load_rgb_float(debug29_image)
    lum = _luminance(rgb)
    p05 = float(np.percentile(lum, 5))
    p95 = float(np.percentile(lum, 95))
    dyn_range = p95 - p05
    zero_frac = float(np.mean(lum < 0.005))

    detected = (dyn_range < eff_min_range) or (zero_frac > max_zero_frac)
    return HeuristicResult(
        name="detect_depth_and_stencil_integrity",
        detected=detected,
        score=dyn_range,
        threshold=eff_min_range,
        severity="fail" if detected else "pass",
        details={
            "is_interior": bool(is_interior) if is_interior is not None else False,
            "depth_p05": round(p05, 4),
            "depth_p95": round(p95, 4),
            "depth_dynamic_range": round(dyn_range, 4),
            "zero_depth_frac": round(zero_frac, 4),
        },
    )


def detect_live_world_compositor_activity(
    engine_out_dir: str | Path | None = None,
    bridge_telemetry_path: str | Path | None = None,
    boot_selfcheck_path: str | Path | None = None,
    harness_results_path: str | Path | None = None,
    min_boot_pass: int = 11,
) -> HeuristicResult:
    """#20: Audits bridge_live_telemetry.json, boot_selfcheck.txt & harness_results.jsonl health."""
    issues: list[str] = []
    details: dict[str, Any] = {}

    eout = Path(engine_out_dir) if engine_out_dir else None
    boot_p = Path(boot_selfcheck_path) if boot_selfcheck_path else (eout / "boot_selfcheck.txt" if eout else None)
    bridge_p = Path(bridge_telemetry_path) if bridge_telemetry_path else (eout / "bridge_live_telemetry.json" if eout else None)
    harness_p = Path(harness_results_path) if harness_results_path else None

    if boot_p is not None and boot_p.exists():
        txt = boot_p.read_text(encoding="utf-8", errors="replace")
        pass_ok = ("Overall: PASS" in txt) or ("Pass=12/12" in txt) or (f"Pass={min_boot_pass}/12" in txt)
        if not pass_ok:
            # Parse explicit Pass=N/12 if present
            idx = txt.find("Pass=")
            parsed_n = -1
            if idx != -1:
                tail = txt[idx + 5: idx + 12].split("/", 1)[0]
                if tail.isdigit():
                    parsed_n = int(tail)
            if parsed_n < min_boot_pass:
                issues.append(f"boot_selfcheck below {min_boot_pass}/12 (parsed={parsed_n})")
        details["boot_selfcheck_present"] = True
    else:
        issues.append("boot_selfcheck.txt missing")
        details["boot_selfcheck_present"] = False

    if bridge_p is not None and bridge_p.exists():
        try:
            data = json.loads(bridge_p.read_text(encoding="utf-8", errors="replace"))
            dbg_errs = int(data.get("d3d12DebugErrors", 0))
            if dbg_errs > 0:
                issues.append(f"d3d12DebugErrors={dbg_errs}")
            # Count active/positive GPU timing fields (*Ms)
            ms_keys = [k for k, v in data.items() if k.endswith("Ms") and isinstance(v, (int, float)) and math.isfinite(float(v)) and float(v) >= 0.0]
            bad_ms = [k for k, v in data.items() if k.endswith("Ms") and (not isinstance(v, (int, float)) or not math.isfinite(float(v)) or float(v) < 0.0)]
            if bad_ms:
                issues.append(f"invalid_gpu_timings={bad_ms}")
            if not ms_keys:
                issues.append("no_gpu_solver_timings_in_telemetry")
            details["bridge_telemetry_present"] = True
            details["gpu_timing_fields"] = len(ms_keys)
            details["d3d12_debug_errors"] = dbg_errs
        except Exception as exc:
            issues.append(f"bridge_telemetry_parse_error: {exc}")
    else:
        issues.append("bridge_live_telemetry.json missing")
        details["bridge_telemetry_present"] = False

    if harness_p is not None:
        if harness_p.exists():
            rows = [ln for ln in harness_p.read_text(encoding="utf-8", errors="replace").splitlines() if ln.strip().startswith("{")]
            details["harness_rows"] = len(rows)
            if not rows:
                issues.append("harness_results.jsonl has 0 rows")
        else:
            issues.append("harness_results.jsonl missing")

    detected = len(issues) > 0
    details["issues"] = issues
    return HeuristicResult(
        name="detect_live_world_compositor_activity",
        detected=detected,
        score=float(len(issues)),
        threshold=0.0,
        severity="fail" if detected else "pass",
        details=details,
    )


# -----------------------------------------------------------------------------
# Heuristics 21..25: Wave 64 Addendum Temporal & Motion-Vector Sequence Analyzers
# -----------------------------------------------------------------------------

def decode_debug35_motion_vectors(
    mv_debug35: str | Path | Image.Image | np.ndarray,
) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Decodes signed GPU motion vectors (dx_px, dy_px) & divergence B from DebugView 35.

    Contract from Rawlss5Pass.hlsl (flags.w == 35u):
      R = saturate(mv.x * 16.0 + 0.5) -> mv_x = (R - 0.5) / 16.0 (UV)
      G = saturate(mv.y * 16.0 + 0.5) -> mv_y = (G - 0.5) / 16.0 (UV)
      B = saturate(mvDivPx * 0.25)    -> moving silhouette divergence in [0..1]
    """
    rgb = load_rgb_float(mv_debug35)
    h, w, _ = rgb.shape
    mv_x_uv = (rgb[:, :, 0] - 0.5) / 16.0
    mv_y_uv = (rgb[:, :, 1] - 0.5) / 16.0
    dx_px = mv_x_uv * float(w)
    dy_px = mv_y_uv * float(h)
    mv_div = rgb[:, :, 2]
    return dx_px, dy_px, mv_div


def _warp_bilinear_rgb(rgb: np.ndarray, dx_px: np.ndarray, dy_px: np.ndarray) -> np.ndarray:
    """Pure-NumPy bilinear gather: out[y, x] = rgb[y + dy_px[y, x], x + dx_px[y, x]]."""
    h, w, _ = rgb.shape
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    sx = np.clip(xx + dx_px, 0.0, float(w - 1))
    sy = np.clip(yy + dy_px, 0.0, float(h - 1))

    x0 = np.floor(sx).astype(np.int32)
    y0 = np.floor(sy).astype(np.int32)
    x1 = np.minimum(x0 + 1, w - 1)
    y1 = np.minimum(y0 + 1, h - 1)
    fx = (sx - x0)[:, :, None]
    fy = (sy - y0)[:, :, None]

    c00 = rgb[y0, x0]
    c10 = rgb[y0, x1]
    c01 = rgb[y1, x0]
    c11 = rgb[y1, x1]
    top = c00 * (1.0 - fx) + c10 * fx
    bot = c01 * (1.0 - fx) + c11 * fx
    return top * (1.0 - fy) + bot * fy


def _phase_correlation_shift(a: np.ndarray, b: np.ndarray) -> tuple[float, float, float]:
    """Computes sub-pixel 2D translation (dx, dy) from `a` to `b` via windowed FFT phase correlation."""
    h, w = a.shape
    if h < 16 or w < 16:
        return 0.0, 0.0, 0.0
    win_y = np.hanning(h).astype(np.float32)[:, None]
    win_x = np.hanning(w).astype(np.float32)[None, :]
    win = win_y * win_x
    fa = np.fft.fft2((a - float(a.mean())) * win)
    fb = np.fft.fft2((b - float(b.mean())) * win)
    cps = fb * np.conj(fa)
    mag = np.abs(cps)
    mag_max = float(mag.max())
    if mag_max < 1e-8:
        return 0.0, 0.0, 0.0
    eps = max(1e-6, mag_max * 1e-3)
    corr = np.real(np.fft.ifft2(cps / (mag + eps)))
    peak_idx = int(np.argmax(corr))
    py, px = divmod(peak_idx, w)
    peak_val = float(corr[py, px])

    # Parabolic 3-point sub-pixel refinement
    def _sub(v_m: float, v_0: float, v_p: float) -> float:
        denom = 2.0 * (v_m - 2.0 * v_0 + v_p)
        if abs(denom) < 1e-8:
            return 0.0
        return float(np.clip((v_m - v_p) / denom, -0.5, 0.5))

    sy = _sub(float(corr[(py - 1) % h, px]), peak_val, float(corr[(py + 1) % h, px]))
    sx = _sub(float(corr[py, (px - 1) % w]), peak_val, float(corr[py, (px + 1) % w]))

    dy = (py + sy) if py <= h // 2 else (py + sy - h)
    dx = (px + sx) if px <= w // 2 else (px + sx - w)
    return float(dx), float(dy), peak_val


def analyze_motion_vector_reprojection(
    frame_t: str | Path | Image.Image | np.ndarray,
    frame_t1: str | Path | Image.Image | np.ndarray,
    mv_debug35_t1: str | Path | Image.Image | np.ndarray,
    max_reproj_rmse: float = 0.14,
    burst_stride: int = 1,
) -> HeuristicResult:
    """#21: Decodes signed GPU MVs from DebugView 35 (scaled by `burst_stride`), masks static HUD regions,
    warps frame_t -> frame_t1 testing +mv/-mv/0, and flags sign inversion or severe reprojection residuals."""
    stride_f = float(max(1, int(burst_stride)))
    a, b = _match_shape(load_rgb_float(frame_t), load_rgb_float(frame_t1))
    mv_rgb = load_rgb_float(mv_debug35_t1)
    a, mv_rgb = _match_shape(a, mv_rgb)
    b, mv_rgb = _match_shape(b, mv_rgb)
    dx_1f, dy_1f, _ = decode_debug35_motion_vectors(mv_rgb)
    dx_px = dx_1f * stride_f
    dy_px = dy_1f * stride_f

    h, w, _ = a.shape
    hud_ok = _hud_valid_mask(h, w)
    mag_px = np.hypot(dx_px, dy_px)
    mean_mv_px = float(mag_px[hud_ok].mean()) if int(hud_ok.sum()) > 0 else float(mag_px.mean())
    max_mv_px = float(np.percentile(mag_px[hud_ok], 95)) if int(hud_ok.sum()) > 0 else float(np.percentile(mag_px, 95))
    margin = int(min(max(4, math.ceil(max_mv_px) + 2), min(h, w) // 5))

    sl_y = slice(margin, h - margin)
    sl_x = slice(margin, w - margin)
    valid_crop = hud_ok[sl_y, sl_x]
    if int(valid_crop.sum()) < 16:
        valid_crop = np.ones((h - 2 * margin, w - 2 * margin), dtype=bool)
    ref = b[sl_y, sl_x, :]
    rmse_zero = float(np.sqrt(np.mean((ref[valid_crop] - a[sl_y, sl_x, :][valid_crop]) ** 2)))

    # Distinguish stationary bursts (mean_mv_px < 0.25) from moving cam_sweep bursts (>= 0.25)
    if mean_mv_px < 0.25:
        detected_static = rmse_zero > max_reproj_rmse
        return HeuristicResult(
            name="analyze_motion_vector_reprojection",
            detected=detected_static,
            score=rmse_zero,
            threshold=max_reproj_rmse,
            severity="fail" if detected_static else "pass",
            details={
                "motion_regime": "stationary",
                "burst_stride": int(stride_f),
                "hud_masked": True,
                "rmse_plus_mv": round(rmse_zero, 5),
                "rmse_minus_mv": round(rmse_zero, 5),
                "rmse_zero_mv": round(rmse_zero, 5),
                "static_temporal_rmse": round(rmse_zero, 5),
                "mean_mv_px": round(mean_mv_px, 3),
                "p95_mv_px": round(max_mv_px, 3),
                "sign_inverted": False,
                "best_sign": "stationary",
            },
        )

    warp_plus = _warp_bilinear_rgb(a, dx_px, dy_px)
    warp_minus = _warp_bilinear_rgb(a, -dx_px, -dy_px)
    rmse_plus = float(np.sqrt(np.mean((ref[valid_crop] - warp_plus[sl_y, sl_x, :][valid_crop]) ** 2)))
    rmse_minus = float(np.sqrt(np.mean((ref[valid_crop] - warp_minus[sl_y, sl_x, :][valid_crop]) ** 2)))

    sign_inverted = (rmse_minus + 0.015) < rmse_plus
    no_improvement = (mean_mv_px >= 0.50) and (rmse_plus > rmse_zero * 1.05) and (rmse_plus > 0.03)
    high_residual = min(rmse_plus, rmse_minus) > max_reproj_rmse

    best_sign = "+mv" if rmse_plus <= min(rmse_minus, rmse_zero) else ("-mv" if rmse_minus <= rmse_zero else "zero")
    detected = sign_inverted or no_improvement or high_residual
    return HeuristicResult(
        name="analyze_motion_vector_reprojection",
        detected=detected,
        score=rmse_plus,
        threshold=max_reproj_rmse,
        severity="fail" if detected else "pass",
        details={
            "motion_regime": "moving_cam_sweep",
            "burst_stride": int(stride_f),
            "hud_masked": True,
            "rmse_plus_mv": round(rmse_plus, 5),
            "rmse_minus_mv": round(rmse_minus, 5),
            "rmse_zero_mv": round(rmse_zero, 5),
            "mean_mv_px": round(mean_mv_px, 3),
            "p95_mv_px": round(max_mv_px, 3),
            "sign_inverted": sign_inverted,
            "best_sign": best_sign,
        },
    )


def analyze_water_temporal_flow_speed(
    seq_frames: Sequence[str | Path | Image.Image | np.ndarray],
    water_mask: str | Path | Image.Image | np.ndarray | None = None,
    min_speed_px: float = 0.15,
    max_speed_px: float = 4.50,
    is_interior: bool | None = None,
    water_flow_diag_image: str | Path | Image.Image | np.ndarray | None = None,
    mv_seq_frames: Sequence[str | Path | Image.Image | np.ndarray] | None = None,
    burst_stride: int = 1,
) -> HeuristicResult:
    """#22: Computes multi-frame FFT phase-correlation water flow speed across a burst sequence.

    Cleanly skips enclosed interiors (`is_interior=True`) and dry scenes where `water_flow_diag`
    (`WaterView=7`) reports `< 0.5%` active water pixels. When `mv_seq_frames` is provided, subtracts
    per-pixel camera motion (`burst_stride * mv`) before measuring water surface flow in px/frame.
    """
    if is_interior is True:
        return HeuristicResult(
            name="analyze_water_temporal_flow_speed",
            detected=False,
            score=0.0,
            threshold=max_speed_px,
            severity="pass",
            details={"is_interior": True, "skipped_interior_scene": True},
        )

    if len(seq_frames) < 2:
        return HeuristicResult(
            name="analyze_water_temporal_flow_speed",
            detected=False,
            score=0.0,
            threshold=max_speed_px,
            severity="pass",
            details={"skipped_short_sequence": True, "frames": len(seq_frames)},
        )

    stride_f = float(max(1, int(burst_stride)))
    imgs = [load_rgb_float(f) for f in seq_frames]
    h, w, _ = imgs[0].shape
    y0, y1 = int(h * 0.40), int(h * 0.90)
    x0, x1 = int(w * 0.10), int(w * 0.90)

    if water_flow_diag_image is not None:
        wf = load_rgb_float(water_flow_diag_image)
        _, wf = _match_shape(imgs[0], wf)
        # WaterView=7 dims non-water pixels cleanly; active water pixels carry R=flow, G=depth, B=shore
        active_v7 = (wf.max(axis=2) > 0.22) & (_luminance(wf) > 0.08)
        active_v7_frac = float(np.mean(active_v7))
        if active_v7_frac < 0.005:
            return HeuristicResult(
                name="analyze_water_temporal_flow_speed",
                detected=False,
                score=0.0,
                threshold=max_speed_px,
                severity="pass",
                details={
                    "water_view7_active_frac": round(active_v7_frac, 5),
                    "skipped_no_active_water_view7": True,
                },
            )

    if water_mask is not None:
        wm_arr = load_rgb_float(water_mask)
        _, wm_arr = _match_shape(imgs[0], wm_arr)
        wmask = _luminance(wm_arr[y0:y1, x0:x1, :]) > 0.04
    elif water_flow_diag_image is not None:
        wf = load_rgb_float(water_flow_diag_image)
        _, wf = _match_shape(imgs[0], wf)
        wf_roi = wf[y0:y1, x0:x1, :]
        wmask = (wf_roi.max(axis=2) > 0.22) & (_luminance(wf_roi) > 0.08)
    else:
        r0, g0, b0 = imgs[0][y0:y1, x0:x1, 0], imgs[0][y0:y1, x0:x1, 1], imgs[0][y0:y1, x0:x1, 2]
        wmask = (b0 > r0 * 1.10) & (b0 > 0.14) & (((b0 + g0) * 0.5) > (r0 + 0.025))

    water_frac = float(np.mean(wmask))
    if water_frac < 0.15:
        return HeuristicResult(
            name="analyze_water_temporal_flow_speed",
            detected=False,
            score=0.0,
            threshold=max_speed_px,
            severity="pass",
            details={"water_frac": round(water_frac, 4), "skipped_no_water": True},
        )

    # Locate the densest contiguous water sub-window so static shoreline mask edges
    # cannot bias the FFT phase-correlation peak toward (0, 0).
    rh, rw = wmask.shape
    ph = max(16, min(48, rh // 2))
    pw = max(24, min(96, rw // 2))
    ii = np.pad(wmask.astype(np.int32), ((1, 0), (1, 0)), mode="constant").cumsum(axis=0).cumsum(axis=1)
    win_sums = ii[ph:, pw:] - ii[:-ph, pw:] - ii[ph:, :-pw] + ii[:-ph, :-pw]
    best_idx = int(np.argmax(win_sums))
    by, bx = divmod(best_idx, win_sums.shape[1])
    patch_density = float(win_sums[by, bx]) / float(ph * pw)
    if patch_density < 0.65:
        return HeuristicResult(
            name="analyze_water_temporal_flow_speed",
            detected=False,
            score=0.0,
            threshold=max_speed_px,
            severity="pass",
            details={
                "water_frac": round(water_frac, 4),
                "max_water_patch_density": round(patch_density, 4),
                "skipped_no_contiguous_water": True,
            },
        )

    mvs = [load_rgb_float(m) for m in mv_seq_frames] if mv_seq_frames else None
    camera_compensated = False
    speeds: list[float] = []
    dxs: list[float] = []
    dys: list[float] = []
    py0, py1 = y0 + by, y0 + by + ph
    px0, px1 = x0 + bx, x0 + bx + pw
    for k in range(len(imgs) - 1):
        img_a, img_b = _match_shape(imgs[k], imgs[k + 1])
        if mvs is not None and (k + 1) < len(mvs):
            mv_k = mvs[k + 1]
            img_a, mv_k = _match_shape(img_a, mv_k)
            img_b, mv_k = _match_shape(img_b, mv_k)
            dx_cam, dy_cam, _ = decode_debug35_motion_vectors(mv_k)
            img_a_comp = _warp_bilinear_rgb(img_a, dx_cam * stride_f, dy_cam * stride_f)
            camera_compensated = True
        else:
            img_a_comp = img_a
        la = _luminance(img_a_comp[py0:py1, px0:px1, :])
        lb = _luminance(img_b[py0:py1, px0:px1, :])
        dx, dy, _ = _phase_correlation_shift(la, lb)
        dx_per_frame = dx / stride_f
        dy_per_frame = dy / stride_f
        dxs.append(dx_per_frame)
        dys.append(dy_per_frame)
        speeds.append(math.hypot(dx_per_frame, dy_per_frame))

    mean_spd = float(np.mean(speeds))
    max_spd = float(np.max(speeds))
    too_slow = mean_spd < min_speed_px
    too_fast = (mean_spd > max_speed_px) or (max_spd > max_speed_px * 1.35)
    detected = too_slow or too_fast
    return HeuristicResult(
        name="analyze_water_temporal_flow_speed",
        detected=detected,
        score=mean_spd,
        threshold=max_speed_px,
        severity="fail" if detected else "pass",
        details={
            "pairs_analyzed": len(speeds),
            "burst_stride": int(stride_f),
            "camera_motion_subtracted": camera_compensated,
            "water_frac": round(water_frac, 4),
            "mean_speed_px_per_frame": round(mean_spd, 4),
            "max_speed_px_per_frame": round(max_spd, 4),
            "mean_dx_px": round(float(np.mean(dxs)), 4),
            "mean_dy_px": round(float(np.mean(dys)), 4),
            "too_slow_static": too_slow,
            "too_fast_jittery": too_fast,
        },
    )


def analyze_shadow_temporal_stability(
    shadow_seq_frames: Sequence[str | Path | Image.Image | np.ndarray],
    mv_seq_frames: Sequence[str | Path | Image.Image | np.ndarray] | None = None,
    max_boil_std: float = 0.055,
    max_global_jump: float = 0.14,
    max_flicker_frac: float = 0.08,
    burst_stride: int = 1,
) -> HeuristicResult:
    """#23: Measures motion-compensated shadow-edge boiling & cascade-pop variance across burst frames."""
    if len(shadow_seq_frames) < 2:
        return HeuristicResult(
            name="analyze_shadow_temporal_stability",
            detected=False,
            score=0.0,
            threshold=max_boil_std,
            severity="pass",
            details={"skipped_short_sequence": True, "frames": len(shadow_seq_frames)},
        )

    stride_f = float(max(1, int(burst_stride)))
    imgs = [load_rgb_float(f) for f in shadow_seq_frames]
    mvs = [load_rgb_float(m) for m in mv_seq_frames] if mv_seq_frames else None

    pair_stds: list[float] = []
    global_jumps: list[float] = []
    flicker_fracs: list[float] = []

    for k in range(len(imgs) - 1):
        a, b = _match_shape(imgs[k], imgs[k + 1])
        max_mv_k = 0.0
        if mvs is not None and (k + 1) < len(mvs):
            mv_k = mvs[k + 1]
            a, mv_k = _match_shape(a, mv_k)
            b, mv_k = _match_shape(b, mv_k)
            dx_1f, dy_1f, _ = decode_debug35_motion_vectors(mv_k)
            dx = dx_1f * stride_f
            dy = dy_1f * stride_f
            max_mv_k = float(np.percentile(np.hypot(dx, dy), 95))
            a_aligned = _warp_bilinear_rgb(a, dx, dy)
        else:
            a_aligned = a
        la = _luminance(a_aligned)
        lb = _luminance(b)
        diff = np.abs(lb - la)
        h, w = diff.shape
        hud_ok = _hud_valid_mask(h, w)
        m = int(min(max(2, min(h, w) // 16, math.ceil(max_mv_k) + 2), min(h, w) // 5))
        crop_hud = hud_ok[m : h - m, m : w - m]
        inner_2d = diff[m : h - m, m : w - m]
        inner = inner_2d[crop_hud] if int(crop_hud.sum()) >= 16 else inner_2d.ravel()
        la_valid = la[m : h - m, m : w - m][crop_hud] if int(crop_hud.sum()) >= 16 else la.ravel()
        lb_valid = lb[m : h - m, m : w - m][crop_hud] if int(crop_hud.sum()) >= 16 else lb.ravel()
        pair_stds.append(float(inner.std()))
        global_jumps.append(abs(float(lb_valid.mean()) - float(la_valid.mean())))
        flicker_fracs.append(float(np.mean(inner > 0.15)))

    boil_std = float(np.mean(pair_stds))
    max_jump = float(np.max(global_jumps))
    max_flicker = float(np.max(flicker_fracs))
    detected = (boil_std > max_boil_std) or (max_jump > max_global_jump) or (max_flicker > max_flicker_frac)
    return HeuristicResult(
        name="analyze_shadow_temporal_stability",
        detected=detected,
        score=max(boil_std, max_jump, max_flicker),
        threshold=max_boil_std,
        severity="fail" if detected else "pass",
        details={
            "pairs_analyzed": len(pair_stds),
            "burst_stride": int(stride_f),
            "hud_masked": True,
            "motion_compensated": mvs is not None,
            "mean_pair_boil_std": round(boil_std, 5),
            "max_global_shadow_jump": round(max_jump, 5),
            "max_pixel_flicker_frac": round(max_flicker, 5),
        },
    )


def analyze_temporal_ghosting_and_motion_outline(
    seq_frames: Sequence[str | Path | Image.Image | np.ndarray],
    mv_seq_frames: Sequence[str | Path | Image.Image | np.ndarray],
    max_outline_frac: float = 0.14,
    max_ghost_rmse: float = 0.12,
    burst_stride: int = 1,
) -> HeuristicResult:
    """#24: Measures trailing ghosting streaks & dark cel-shading silhouette rims specifically along
    high-velocity / high-divergence (`B > 0.15` in DebugView 35) moving actor edges."""
    n = min(len(seq_frames), len(mv_seq_frames))
    if n < 2:
        return HeuristicResult(
            name="analyze_temporal_ghosting_and_motion_outline",
            detected=False,
            score=0.0,
            threshold=max_ghost_rmse,
            severity="pass",
            details={"skipped_short_sequence": True, "frames": n},
        )

    stride_f = float(max(1, int(burst_stride)))
    outline_fracs: list[float] = []
    ghost_rmses: list[float] = []
    active_edge_px_total = 0

    for k in range(n - 1):
        a, b = _match_shape(load_rgb_float(seq_frames[k]), load_rgb_float(seq_frames[k + 1]))
        mv_b = load_rgb_float(mv_seq_frames[k + 1])
        a, mv_b = _match_shape(a, mv_b)
        b, mv_b = _match_shape(b, mv_b)
        dx_1f, dy_1f, mv_div = decode_debug35_motion_vectors(mv_b)
        dx = dx_1f * stride_f
        dy = dy_1f * stride_f

        h, w, _ = b.shape
        hud_ok = _hud_valid_mask(h, w)
        lb = _luminance(b)
        # Moving silhouette pixels: high divergence B > 0.15 excluding static HUD overlay
        moving_edge = (mv_div > 0.15) & hud_ok
        edge_inner = moving_edge[3:-3, 3:-3]
        edge_cnt = int(edge_inner.sum())
        active_edge_px_total += edge_cnt

        if edge_cnt >= 16:
            c = lb[3:-3, 3:-3]
            dark_rim = (
                edge_inner
                & (c < 0.07)
                & (
                    ((lb[3:-3, :-6] - c) > 0.12) & ((lb[3:-3, 6:] - c) > 0.12)
                    | ((lb[:-6, 3:-3] - c) > 0.12) & ((lb[6:, 3:-3] - c) > 0.12)
                )
            )
            outline_fracs.append(float(dark_rim.sum()) / float(edge_cnt))

            warped_a = _warp_bilinear_rgb(a, dx, dy)
            err = np.sqrt(np.mean((b[3:-3, 3:-3] - warped_a[3:-3, 3:-3]) ** 2, axis=2))
            ghost_rmses.append(float(np.mean(err[edge_inner])))

    if not outline_fracs:
        return HeuristicResult(
            name="analyze_temporal_ghosting_and_motion_outline",
            detected=False,
            score=0.0,
            threshold=max_ghost_rmse,
            severity="pass",
            details={
                "burst_stride": int(stride_f),
                "hud_masked": True,
                "active_moving_edge_pixels": active_edge_px_total,
                "skipped_static_scene": True,
            },
        )

    max_outline = float(np.max(outline_fracs))
    max_ghost = float(np.max(ghost_rmses))
    detected = (max_outline > max_outline_frac) or (max_ghost > max_ghost_rmse)
    return HeuristicResult(
        name="analyze_temporal_ghosting_and_motion_outline",
        detected=detected,
        score=max(max_outline, max_ghost),
        threshold=max_ghost_rmse,
        severity="fail" if detected else "pass",
        details={
            "burst_stride": int(stride_f),
            "hud_masked": True,
            "active_moving_edge_pixels": active_edge_px_total,
            "max_dark_rim_frac": round(max_outline, 5),
            "max_edge_ghost_rmse": round(max_ghost, 5),
        },
    )


def analyze_world_lock_anti_swimming(
    seq_frames: Sequence[str | Path | Image.Image | np.ndarray],
    mv_seq_frames: Sequence[str | Path | Image.Image | np.ndarray],
    max_screen_lock_ratio: float = 0.75,
    burst_stride: int = 1,
) -> HeuristicResult:
    """#25: Verifies world-space surfaces track true GPU motion vectors (scaled by `burst_stride`)
    instead of staying screen-locked (camera-swimming) during camera motion."""
    n = min(len(seq_frames), len(mv_seq_frames))
    if n < 2:
        return HeuristicResult(
            name="analyze_world_lock_anti_swimming",
            detected=False,
            score=0.0,
            threshold=max_screen_lock_ratio,
            severity="pass",
            details={"skipped_short_sequence": True, "frames": n},
        )

    stride_f = float(max(1, int(burst_stride)))
    ratios: list[float] = []
    world_rmses: list[float] = []
    screen_rmses: list[float] = []
    stationary_rmses: list[float] = []

    for k in range(n - 1):
        a, b = _match_shape(load_rgb_float(seq_frames[k]), load_rgb_float(seq_frames[k + 1]))
        mv_b = load_rgb_float(mv_seq_frames[k + 1])
        a, mv_b = _match_shape(a, mv_b)
        b, mv_b = _match_shape(b, mv_b)
        dx_1f, dy_1f, _ = decode_debug35_motion_vectors(mv_b)
        dx = dx_1f * stride_f
        dy = dy_1f * stride_f
        h, w, _ = a.shape
        hud_ok = _hud_valid_mask(h, w)
        mag = np.hypot(dx, dy)
        mean_mv = float(mag[hud_ok].mean()) if int(hud_ok.sum()) > 0 else float(mag.mean())

        p95_mv = float(np.percentile(mag[hud_ok], 95)) if int(hud_ok.sum()) > 0 else float(np.percentile(mag, 95))
        margin = int(min(max(4, math.ceil(p95_mv) + 2), min(h, w) // 5))
        sl_y = slice(margin, h - margin)
        sl_x = slice(margin, w - margin)
        valid_crop = hud_ok[sl_y, sl_x]
        if int(valid_crop.sum()) < 16:
            valid_crop = np.ones((h - 2 * margin, w - 2 * margin), dtype=bool)
        rmse_screen = float(np.sqrt(np.mean((b[sl_y, sl_x][valid_crop] - a[sl_y, sl_x][valid_crop]) ** 2)))

        if mean_mv < 0.25:
            stationary_rmses.append(rmse_screen)
            continue

        warped = _warp_bilinear_rgb(a, dx, dy)
        rmse_world = float(np.sqrt(np.mean((b[sl_y, sl_x][valid_crop] - warped[sl_y, sl_x][valid_crop]) ** 2)))
        world_rmses.append(rmse_world)
        screen_rmses.append(rmse_screen)
        # If image stays glued to screen coordinates during motion, rmse_screen << rmse_world!
        ratio = rmse_screen / max(rmse_world, 1e-6)
        ratios.append(ratio)

    if not ratios:
        max_static = float(np.max(stationary_rmses)) if stationary_rmses else 0.0
        detected_static = max_static > 0.14
        return HeuristicResult(
            name="analyze_world_lock_anti_swimming",
            detected=detected_static,
            score=max_static,
            threshold=0.14,
            severity="fail" if detected_static else "pass",
            details={
                "motion_regime": "stationary",
                "burst_stride": int(stride_f),
                "hud_masked": True,
                "stationary_pairs_analyzed": len(stationary_rmses),
                "max_static_temporal_rmse": round(max_static, 5),
                "skipped_no_camera_motion": True,
            },
        )

    min_ratio = float(np.min(ratios))
    max_world_rmse = float(np.max(world_rmses))
    detected = (min_ratio < max_screen_lock_ratio) and (max_world_rmse > 0.025)
    return HeuristicResult(
        name="analyze_world_lock_anti_swimming",
        detected=detected,
        score=min_ratio,
        threshold=max_screen_lock_ratio,
        severity="fail" if detected else "pass",
        details={
            "motion_regime": "moving_cam_sweep",
            "burst_stride": int(stride_f),
            "hud_masked": True,
            "moving_pairs_analyzed": len(ratios),
            "min_screen_to_world_rmse_ratio": round(min_ratio, 4),
            "max_world_locked_rmse": round(max_world_rmse, 5),
            "mean_screen_locked_rmse": round(float(np.mean(screen_rmses)), 5),
        },
    )


# -----------------------------------------------------------------------------
# Single-Image, Scene-Bundle & Directory Audit Orchestrators
# -----------------------------------------------------------------------------

def audit_image(
    image_or_path: str | Path | Image.Image | np.ndarray,
    label: str = "",
    debug_class_image: str | Path | Image.Image | np.ndarray | None = None,
    debug_mask_image: str | Path | Image.Image | np.ndarray | None = None,
    debug29_image: str | Path | Image.Image | np.ndarray | None = None,
    debug34_image: str | Path | Image.Image | np.ndarray | None = None,
    water_depth_image: str | Path | Image.Image | np.ndarray | None = None,
    water_flow_diag_image: str | Path | Image.Image | np.ndarray | None = None,
    include_extended_rgb: bool = False,
) -> dict[str, Any]:
    """Runs core single-frame heuristics (7 baseline + optional extended RGB checks) on one image."""
    rgb = load_rgb_float(image_or_path)
    h, w, _ = rgb.shape
    results = [
        detect_indoor_black_orbs(rgb),
        detect_red_sky_fire_bleed(rgb),
        detect_water_scanline_holes(rgb, water_depth_image=water_depth_image, water_flow_diag_image=water_flow_diag_image),
        detect_water_zebra_stripes(rgb),
        detect_sky_diagonal_cutout(rgb),
        detect_actor_green_moss_chroma(rgb, debug_class_image=debug_class_image, debug_mask_image=debug_mask_image),
        detect_dark_motion_outline(rgb, debug28_image=debug_class_image, debug29_image=debug29_image),
    ]
    if include_extended_rgb:
        results.extend([
            detect_crushed_black_tree_trunks(rgb, debug34_image=debug34_image),
            detect_bleached_white_pine_needles(rgb, debug34_image=debug34_image),
            detect_water_neon_cyan_oversat(rgb),
            detect_flat_ice_cave_triangles(rgb),
            detect_exposure_and_tonemap_clipping(rgb),
        ])
    failed = [r.name for r in results if r.detected]
    return {
        "image": label or (str(image_or_path) if isinstance(image_or_path, (str, Path)) else "<memory>"),
        "width": int(w),
        "height": int(h),
        "passed": len(failed) == 0,
        "defect_count": len(failed),
        "defects": failed,
        "heuristics": {r.name: r.to_dict() for r in results},
    }


def _infer_is_interior(scene_name: str, is_interior: bool | None) -> bool | None:
    if is_interior is not None:
        return bool(is_interior)
    low = scene_name.lower()
    if any(tok in low for tok in ("inn", "cave", "interior", "dungeon", "outpost", "barrow", "tomb", "mine", "ruin")):
        return True
    if any(tok in low for tok in ("ext", "day", "sunset", "rain", "night", "river", "hrothgar", "whiterun", "windhelm")):
        return False
    return None


def audit_scene_bundle(
    step_images: Mapping[str, str | Path | Image.Image | np.ndarray],
    scene_name: str = "scene",
    telemetry: Mapping[str, str | Path | None] | None = None,
    burst_sequences: Mapping[str, Sequence[str | Path | Image.Image | np.ndarray]] | None = None,
    is_interior: bool | None = None,
    burst_stride: int = 3,
) -> dict[str, Any]:
    """Runs ALL applicable heuristics (up to 25!) across a scene's stills, debug layers, telemetry & bursts."""
    results: list[HeuristicResult] = []
    resolved_interior = _infer_is_interior(scene_name, is_interior)
    stride_n = max(1, int(burst_stride))
    imp = step_images.get("improved")
    v1 = step_images.get("view_normals")
    v2 = step_images.get("view_sun_shadow")
    v16 = step_images.get("view_albedo")
    v28 = step_images.get("view_full_classes")
    v29 = step_images.get("view_depth_edge")
    v30 = step_images.get("view_shadow_stab")
    v31 = step_images.get("view_moss_mask")
    v32 = step_images.get("view_skin_sss")
    v33 = step_images.get("view_nan_hdr")
    v34 = step_images.get("view_flora_trees")
    w_depth = step_images.get("water_depth")
    w_body = step_images.get("water_body")
    w_flow = step_images.get("water_flow_diag")

    if imp is not None:
        rgb = load_rgb_float(imp)
        results.extend([
            detect_indoor_black_orbs(rgb, is_interior=resolved_interior),
            detect_red_sky_fire_bleed(rgb, debug29_image=v29),
            detect_water_scanline_holes(rgb, water_depth_image=w_depth, water_flow_diag_image=w_flow),
            detect_water_zebra_stripes(rgb),
            detect_sky_diagonal_cutout(rgb),
            detect_actor_green_moss_chroma(rgb, debug_class_image=v28, debug_mask_image=v31),
            detect_dark_motion_outline(rgb, debug28_image=v28, debug29_image=v29),
            detect_crushed_black_tree_trunks(rgb, debug34_image=v34),
            detect_bleached_white_pine_needles(rgb, debug34_image=v34),
            detect_water_neon_cyan_oversat(rgb, water_body_image=w_body),
            detect_flat_ice_cave_triangles(rgb),
            detect_exposure_and_tonemap_clipping(rgb),
        ])
    if v33 is not None:
        results.append(detect_nan_inf_hdr_overflow(v33))
    if v1 is not None:
        results.append(detect_normal_map_integrity(v1))
    if v16 is not None:
        results.append(detect_albedo_range_pbr_sanity(v16))
    if v2 is not None or v30 is not None:
        results.append(detect_shadow_coverage_balance(v2 if v2 is not None else v30))
    if v32 is not None:
        results.append(detect_skin_sss_sanity(v32))
    if v29 is not None:
        results.append(detect_depth_and_stencil_integrity(v29, is_interior=resolved_interior))
    if telemetry is not None:
        results.append(
            detect_live_world_compositor_activity(
                engine_out_dir=telemetry.get("engine_out_dir"),
                bridge_telemetry_path=telemetry.get("bridge_telemetry_path"),
                boot_selfcheck_path=telemetry.get("boot_selfcheck_path"),
                harness_results_path=telemetry.get("harness_results_path"),
            )
        )

    if burst_sequences:
        imp_seq = burst_sequences.get("improved")
        mv_seq = burst_sequences.get("view_motion_outline")
        sh_seq = burst_sequences.get("view_sun_shadow")
        if imp_seq and len(imp_seq) >= 2:
            results.append(
                analyze_water_temporal_flow_speed(
                    imp_seq,
                    water_mask=w_depth,
                    is_interior=resolved_interior,
                    water_flow_diag_image=w_flow,
                    mv_seq_frames=mv_seq,
                    burst_stride=stride_n,
                )
            )
            if mv_seq and len(mv_seq) >= 2:
                results.append(
                    analyze_motion_vector_reprojection(
                        imp_seq[0], imp_seq[1], mv_seq[1], burst_stride=stride_n
                    )
                )
                results.append(
                    analyze_temporal_ghosting_and_motion_outline(
                        imp_seq, mv_seq, burst_stride=stride_n
                    )
                )
                results.append(
                    analyze_world_lock_anti_swimming(
                        imp_seq, mv_seq, burst_stride=stride_n
                    )
                )
        if sh_seq and len(sh_seq) >= 2:
            results.append(
                analyze_shadow_temporal_stability(
                    sh_seq, mv_seq_frames=mv_seq, burst_stride=stride_n
                )
            )

    failed = [r.name for r in results if r.detected]
    return {
        "scene": scene_name,
        "is_interior": bool(resolved_interior) if resolved_interior is not None else False,
        "burst_stride": stride_n,
        "steps_present": sorted(step_images.keys()),
        "burst_steps_present": sorted(k for k, v in (burst_sequences or {}).items() if len(v) >= 2),
        "heuristics_run": len(results),
        "passed": len(failed) == 0,
        "defect_count": len(failed),
        "defects": failed,
        "heuristics": {r.name: r.to_dict() for r in results},
    }


def audit_directory(directory: str | Path, glob_pattern: str = "*.png", include_extended_rgb: bool = False) -> dict[str, Any]:
    """Audits every matching image in a directory."""
    d = Path(directory)
    files = sorted(d.glob(glob_pattern))
    reports = [audit_image(p, label=p.name, include_extended_rgb=include_extended_rgb) for p in files]
    defect_histogram: dict[str, int] = {}
    for rep in reports:
        for name in rep["defects"]:
            defect_histogram[name] = defect_histogram.get(name, 0) + 1
    return {
        "directory": str(d),
        "image_count": len(reports),
        "clean_images": sum(1 for r in reports if r["passed"]),
        "flagged_images": sum(1 for r in reports if not r["passed"]),
        "defect_histogram": defect_histogram,
        "images": reports,
    }


def _main(argv: Sequence[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description="RAW 25-Heuristic Full-Pipeline Visual & Temporal Inspector (Wave 65)")
    ap.add_argument("target", help="PNG/BMP file or directory of screenshots to audit")
    ap.add_argument("--pair", help="Optional second frame for camera_turn_shadow_flicker comparison")
    ap.add_argument("--glob", default="*.png", help="Filename glob when target is a directory")
    ap.add_argument("--extended", action="store_true", help="Include extended RGB heuristics (#10..#13, #18)")
    ap.add_argument("--json", dest="json_out", help="Optional path to write JSON report")
    args = ap.parse_args(argv)

    p = Path(args.target)
    if args.pair:
        flicker = detect_camera_turn_shadow_flicker(p, Path(args.pair))
        report: dict[str, Any] = {
            "frame_a": str(p),
            "frame_b": str(args.pair),
            "flicker": flicker.to_dict(),
        }
    elif p.is_dir():
        report = audit_directory(p, glob_pattern=args.glob, include_extended_rgb=args.extended)
    else:
        report = audit_image(p, include_extended_rgb=args.extended)

    text = json.dumps(report, indent=2)
    if args.json_out:
        Path(args.json_out).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
