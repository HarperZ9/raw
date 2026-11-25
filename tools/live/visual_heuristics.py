"""Automated quantitative visual defect heuristics for live RAW Skyrim SE screenshots.

Pure Python + Pillow + NumPy (zero OpenCV/SciPy dependencies). Calibrated against the
33 Wave 63 baseline screenshots (2026-11-25 09:31:34 .. 09:38:46) with strict separation
between real defects and clean synthetic/live control frames.

Original work, MIT.
"""
from __future__ import annotations

import argparse
import dataclasses
import json
import math
from pathlib import Path
from typing import Any, Sequence

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
}


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
        if rgb.max() > 1.5:
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


def detect_indoor_black_orbs(
    image: str | Path | Image.Image | np.ndarray,
    box_density_threshold: float = 0.92,
    min_surround_bright_frac: float = 0.35,
) -> HeuristicResult:
    """Detects large contiguous pitch-black soot/orb blotches occluding lit interiors/caves.

    Calibrated to flag baseline 09.36.08.92.png & 09.36.11.18.png (density=1.00 inside lit caves)
    while ignoring clean scenes, dark night shadows, and fade-to-black loading screens.
    """
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
            "max_black_box_density": round(max_box, 4),
            "box_size_px": int(k),
            "surround_bright_frac": round(bright_frac, 4),
            "total_pitch_black_frac": round(float(np.mean(lum < 0.015)), 4),
        },
    )


def detect_red_sky_fire_bleed(
    image: str | Path | Image.Image | np.ndarray,
    area_threshold: float = 0.025,
) -> HeuristicResult:
    """Detects anomalous high-red/low-blue fire halo blobs bleeding onto the upper sky region.

    Calibrated to flag baseline 09.33.23.72.png (0.052) and 09.33.34.46.png (0.185) while
    clean blue/overcast skies score ~0.000.
    """
    rgb = load_rgb_float(image)
    h, _, _ = rgb.shape
    y0, y1 = int(h * 0.06), int(h * 0.45)
    top = rgb[y0:y1, :, :]
    r, g, b = top[:, :, 0], top[:, :, 1], top[:, :, 2]

    red_blob = (r > 0.42) & ((r - b) > 0.22) & (r > g * 1.18)
    frac = float(np.mean(red_blob))
    detected = frac >= area_threshold
    return HeuristicResult(
        name="detect_red_sky_fire_bleed",
        detected=detected,
        score=frac,
        threshold=area_threshold,
        severity="fail" if detected else "pass",
        details={
            "red_sky_area_frac": round(frac, 5),
            "red_sky_pixels": int(red_blob.sum()),
            "upper_roi_pixels": int(red_blob.size),
        },
    )


def detect_water_scanline_holes(
    image: str | Path | Image.Image | np.ndarray,
    alt_threshold: float = 0.060,
    scan_threshold: float = 0.032,
) -> HeuristicResult:
    """Detects high-frequency horizontal row-alternating scanline banding inside water regions.

    Calibrated to flag baseline 09.31.34.90.png (alt=0.091, scan=0.041) and 09.34.56.50.png
    (alt=0.094, scan=0.077) while smooth water scores < 0.01.
    """
    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    lum = _luminance(rgb)

    y0, y1 = int(h * 0.35), int(h * 0.92)
    roi = rgb[y0:y1, :, :]
    lum_roi = lum[y0:y1, :]
    r, g, b = roi[:, :, 0], roi[:, :, 1], roi[:, :, 2]

    water_mask = (b > r * 1.12) & (b > 0.14) & (((b + g) * 0.5) > (r + 0.03))
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
            details={"water_pixels": w_cnt, "skipped": True},
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
    """Detects periodic narrow zebra-stripe spectral peaks in shallow water/shoreline bands.

    Calibrated to flag baseline 09.35.44.84.png (peakX=24.82, peakY=13.39, dy/dx=1.64) while
    clean isotropic water and smooth ice caves pass cleanly.
    """
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

    # Vertical & horizontal mid-frequency FFT concentration (stripe periods 6..40 px)
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
    """Detects razor-sharp straight diagonal or L-shaped luminance/chroma seams in the sky hemisphere.

    Uses a pure-NumPy Radon/Hough line accumulator across angles [-35..+35 deg] on sharp sky-to-sky
    chroma discontinuities (both sides bright > 0.28, strong blue-red jump). Calibrated to catch
    baseline 09.37.13.57.png (Windhelm Stables diagonal sky strip + L-cutout) with zero false
    positives on organic clouds or mountain silhouettes.
    """
    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    # Downsample moderately for fast, crisp Radon line accumulation (target width ~640)
    scale = 1
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

    # Sharp vertical chroma transition where BOTH sides are bright sky/cloud (excludes dark roofs/trees)
    both_bright = (lum[2:, :] > 0.30) & (lum[:-2, :] > 0.30)
    # Require at least one side to be distinctly sky-blue (blue_excess > 0.14)
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
            details={"sky_edge_pixels": int(ys.size), "best_angle_deg": 0.0, "max_collinear_votes": 0},
        )

    ys_f = ys.astype(np.float32)
    xs_f = xs.astype(np.float32)
    best_votes = 0
    best_angle = 0.0
    # Scan tilted and near-horizontal angles [-36 .. +36 deg] in 1.5-deg steps (quantize rho to 2px bins)
    for deg_idx in range(-24, 25):
        deg = deg_idx * 1.5
        rad = math.radians(deg)
        # Line normal perpendicular to slope angle `deg`: rho = y*cos(rad) - x*sin(rad)
        rho = np.round((ys_f * math.cos(rad) - xs_f * math.sin(rad)) * 0.5).astype(np.int32)
        # Count distinct X columns contributing to the strongest rho bin (prevents thick noisy blobs)
        vals, counts = np.unique(rho, return_counts=True)
        top_idx = int(np.argmax(counts))
        cand_votes = int(counts[top_idx])
        if cand_votes > best_votes:
            # Verify distinct X span of pixels in this bin
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
    """Detects anomalous moss-green chroma leakage on foreground actors/creatures.

    Supports two modes:
      1. Debug-layer overlap mode (when DebugView 28 class map + DebugView 31 moss-mask are supplied):
         flags ANY >0.2% overlap of Skinned Actor / Skin pixels with non-zero moss mask.
      2. Direct RGB foreground ROI mode: flags anomalous moss-green saturation patches in the
         central actor ROI (calibrated to flag baseline 09.33.46.31.png Stump the dog at 0.250).
    """
    if debug_class_image is not None and debug_mask_image is not None:
        cls_rgb = load_rgb_float(debug_class_image)
        msk_rgb = load_rgb_float(debug_mask_image)
        # DebugView 28 encodes Skinned Actor (base==14) in vivid cyan/magenta or skin in red:
        # treat high-magenta (r>0.7, b>0.7, g<0.3) or skin-red (r>0.7, g<0.35, b<0.35) as actor pixels.
        actor_mask = (
            ((cls_rgb[:, :, 0] > 0.70) & (cls_rgb[:, :, 2] > 0.70) & (cls_rgb[:, :, 1] < 0.35))
            | ((cls_rgb[:, :, 0] > 0.75) & (cls_rgb[:, :, 1] < 0.35) & (cls_rgb[:, :, 2] < 0.35))
        )
        # DebugView 31 encodes moss mask in green channel (g > 0.12)
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
) -> HeuristicResult:
    """Detects thin 1-4px dark cel-shading contour rims along high-contrast foreground silhouettes.

    Calibrated to flag baseline 09.32.32.11.png (0.00567) while clean smooth images score < 0.001.
    """
    rgb = load_rgb_float(image)
    h, w, _ = rgb.shape
    lum = _luminance(rgb)
    l = lum[int(h * 0.15):int(h * 0.85), int(w * 0.10):int(w * 0.90)]
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
    trough_frac = float(np.mean(dark_trough_x | dark_trough_y))
    detected = trough_frac >= trough_threshold
    return HeuristicResult(
        name="detect_dark_motion_outline",
        detected=detected,
        score=trough_frac,
        threshold=trough_threshold,
        severity="fail" if detected else "pass",
        details={
            "dark_trough_frac": round(trough_frac, 6),
            "dark_trough_pixels": int((dark_trough_x | dark_trough_y).sum()),
        },
    )


def detect_camera_turn_shadow_flicker(
    frame_a: str | Path | Image.Image | np.ndarray,
    frame_b: str | Path | Image.Image | np.ndarray,
    shift_frac_threshold: float = 0.18,
    p95_diff_threshold: float = 0.24,
) -> HeuristicResult:
    """Compares consecutive frames across a camera turn to measure abrupt shadow/luminance flicker.

    Calibrated to flag baseline 09.37.21.24.png vs 09.38.46.52.png (darkShiftFrac=0.280, p95=0.300)
    while smooth camera sweeps score < 0.06.
    """
    a = load_rgb_float(frame_a)
    b = load_rgb_float(frame_b)
    if a.shape != b.shape:
        h = min(a.shape[0], b.shape[0])
        w = min(a.shape[1], b.shape[1])
        a = a[:h, :w, :]
        b = b[:h, :w, :]

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


def audit_image(
    image_or_path: str | Path | Image.Image | np.ndarray,
    label: str = "",
    debug_class_image: str | Path | Image.Image | np.ndarray | None = None,
    debug_mask_image: str | Path | Image.Image | np.ndarray | None = None,
) -> dict[str, Any]:
    """Runs all 7 single-frame visual heuristics on one image and returns a JSON-serializable report."""
    rgb = load_rgb_float(image_or_path)
    h, w, _ = rgb.shape
    results = [
        detect_indoor_black_orbs(rgb),
        detect_red_sky_fire_bleed(rgb),
        detect_water_scanline_holes(rgb),
        detect_water_zebra_stripes(rgb),
        detect_sky_diagonal_cutout(rgb),
        detect_actor_green_moss_chroma(rgb, debug_class_image=debug_class_image, debug_mask_image=debug_mask_image),
        detect_dark_motion_outline(rgb),
    ]
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


def audit_directory(directory: str | Path, glob_pattern: str = "*.png") -> dict[str, Any]:
    """Audits every matching image in a directory plus consecutive-pair flicker telemetry."""
    d = Path(directory)
    files = sorted(d.glob(glob_pattern))
    reports = [audit_image(p, label=p.name) for p in files]
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
    ap = argparse.ArgumentParser(description="RAW Visual Heuristics Inspector (Wave 63+)")
    ap.add_argument("target", help="PNG/BMP file or directory of screenshots to audit")
    ap.add_argument("--pair", help="Optional second frame for camera_turn_shadow_flicker comparison")
    ap.add_argument("--glob", default="*.png", help="Filename glob when target is a directory")
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
        report = audit_directory(p, glob_pattern=args.glob)
    else:
        report = audit_image(p)

    text = json.dumps(report, indent=2)
    if args.json_out:
        Path(args.json_out).write_text(text + "\n", encoding="utf-8")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    raise SystemExit(_main())
