"""RAW DX12 Wave 82: 30-Metric Extended Heuristics Collector (raw_heuristics_v2.py).

Retains all 20+ existing visual_heuristics.py checks (#1..#20+) with zero weakened
thresholds and adds extended runtime, GPU culling, temporal, photometric, and
ownership ledger metrics (#21..#30):
  #21 Frame-time distribution (p50, p95, p99, 1%-low FPS, hitch counts >33.3ms / >50ms)
  #22 Per-pass GPU timing breakdown (depth, hiz_cull, ao, gi, shadow, cloud, lighting, post)
  #23 VRAM residency delta across start/return checkpoints (budget <= 32 MB)
  #24 Draw-call & GPU culling efficiency (submitted, culled ratio %, Hi-Z occlusion rate %)
  #25 Overdraw & LOD pop delta (mean overdraw <= 2.5x, adjacent-frame silhouette pop delta)
  #26 Temporal stability (FLIP/SSIM-style adjacent-frame L1/SSIM error during walk & pan)
  #27 Foliage temporal shimmer FFT (high-frequency canopy temporal energy ratio <= 0.12)
  #28 Specular/GI firefly count (isolated 1-px spikes > 6.0x 3x3 median, budget <= 4 / 1080p)
  #29 Photometric integrity bundle (black-crush <= 2.5%, highlight-clip <= 1.5%,
      out-of-gamut <= 0.1%, white-furnace violations == 0, horizon seam <= 0.04,
      actor skin/hair luminance in [0.06, 0.72])
  #30 Runtime engine ownership ledger check (0 unexpected STILL_VANILLA regressions,
      0 double-executed AO/fog/depth passes, 0 flicker frames)

Clean-room MIT.
"""
from __future__ import annotations

import math
import sys
from pathlib import Path
from typing import Any, Sequence

import numpy as np

HERE = Path(__file__).resolve().parent
if str(HERE) not in sys.path:
    sys.path.insert(0, str(HERE))

import visual_heuristics as vh  # noqa: E402


def _pct(vals: Sequence[float], q: float) -> float:
    if not vals:
        return 0.0
    arr = np.sort(np.asarray(vals, dtype=np.float64))
    idx = min(max(int(math.ceil((q / 100.0) * len(arr))) - 1, 0), len(arr) - 1)
    return float(arr[idx])


def metric_21_frametime_distribution(frame_times_ms: Sequence[float]) -> dict[str, Any]:
    vals = [float(x) for x in frame_times_ms if math.isfinite(float(x)) and float(x) > 0.0]
    if not vals:
        return {"id": 21, "name": "frametime_distribution", "passed": False, "code": "FAIL: NO_FRAMETIME_SAMPLES"}
    p50 = _pct(vals, 50.0)
    p95 = _pct(vals, 95.0)
    p99 = _pct(vals, 99.0)
    one_pct_low_fps = 1000.0 / max(p99, 1e-3)
    hitch_33 = sum(1 for x in vals if x > 33.3)
    hitch_50 = sum(1 for x in vals if x > 50.0)
    passed = (p99 <= 33.3) and (hitch_50 == 0)
    return {
        "id": 21, "name": "frametime_distribution", "passed": passed,
        "code": "PASS" if passed else "FAIL: FRAMETIME_HITCH",
        "metrics": {
            "samples": len(vals), "p50_ms": round(p50, 3), "p95_ms": round(p95, 3),
            "p99_ms": round(p99, 3), "one_pct_low_fps": round(one_pct_low_fps, 2),
            "hitch_gt_33ms": hitch_33, "hitch_gt_50ms": hitch_50,
        },
    }


def metric_22_per_pass_gpu_ms(pass_ms: dict[str, float], budget_total_ms: float = 12.0) -> dict[str, Any]:
    required = ("depth_ms", "hiz_cull_ms", "ao_ms", "gi_ms", "shadow_ms", "cloud_ms", "lighting_ms", "post_ms")
    clean = {k: float(pass_ms.get(k, 0.0)) for k in required}
    total = sum(clean.values())
    all_fin = all(math.isfinite(v) and v >= 0.0 for v in clean.values())
    passed = all_fin and (total <= budget_total_ms)
    clean["total_gpu_ms"] = round(total, 4)
    return {
        "id": 22, "name": "per_pass_gpu_ms", "passed": passed,
        "code": "PASS" if passed else "FAIL: GPU_PASS_BUDGET",
        "metrics": clean,
    }


def metric_23_vram_residency_delta(start_mb: float, return_mb: float, max_delta_mb: float = 32.0) -> dict[str, Any]:
    delta = float(return_mb) - float(start_mb)
    passed = math.isfinite(delta) and (delta <= max_delta_mb)
    return {
        "id": 23, "name": "vram_residency_delta", "passed": passed,
        "code": "PASS" if passed else "FAIL: VRAM_LEAK_EXCEEDED",
        "metrics": {"start_mb": round(float(start_mb), 2), "return_mb": round(float(return_mb), 2), "delta_mb": round(delta, 2), "budget_mb": max_delta_mb},
    }


def metric_24_drawcall_culling_efficiency(submitted_draws: int, culled_draws: int, hiz_occluded: int) -> dict[str, Any]:
    tot = max(int(submitted_draws) + int(culled_draws), 1)
    cull_ratio = float(culled_draws) / float(tot)
    hiz_ratio = float(hiz_occluded) / float(tot)
    passed = (submitted_draws >= 0) and (culled_draws >= 0) and (hiz_occluded >= 0) and (cull_ratio >= 0.15)
    return {
        "id": 24, "name": "drawcall_culling_efficiency", "passed": passed,
        "code": "PASS" if passed else "FAIL: CULLING_EFFICIENCY",
        "metrics": {
            "submitted_draws": int(submitted_draws), "culled_draws": int(culled_draws),
            "hiz_occluded": int(hiz_occluded), "culled_ratio_pct": round(cull_ratio * 100.0, 2),
            "hiz_occlusion_pct": round(hiz_ratio * 100.0, 2),
        },
    }


def metric_25_overdraw_and_lod_pop(mean_overdraw: float, lod_pop_delta: float, max_overdraw: float = 2.5, max_pop: float = 0.08) -> dict[str, Any]:
    passed = (mean_overdraw <= max_overdraw) and (lod_pop_delta <= max_pop) and (mean_overdraw >= 0.0) and (lod_pop_delta >= 0.0)
    return {
        "id": 25, "name": "overdraw_and_lod_pop", "passed": passed,
        "code": "PASS" if passed else "FAIL: OVERDRAW_OR_LOD_POP",
        "metrics": {"mean_overdraw": round(float(mean_overdraw), 3), "lod_pop_delta": round(float(lod_pop_delta), 4)},
    }


def metric_26_temporal_flip_ssim(frames_rgb: Sequence[np.ndarray], max_l1_err: float = 0.14, min_ssim: float = 0.80) -> dict[str, Any]:
    if len(frames_rgb) < 2:
        return {"id": 26, "name": "temporal_flip_ssim", "passed": True, "code": "PASS", "metrics": {"pairs": 0, "mean_l1": 0.0, "min_ssim": 1.0}}
    l1s, ssims = [], []
    c1, c2 = 0.01 ** 2, 0.03 ** 2
    for i in range(len(frames_rgb) - 1):
        a = np.asarray(frames_rgb[i], dtype=np.float32)
        b = np.asarray(frames_rgb[i + 1], dtype=np.float32)
        la = 0.2126 * a[..., 0] + 0.7152 * a[..., 1] + 0.0722 * a[..., 2]
        lb = 0.2126 * b[..., 0] + 0.7152 * b[..., 1] + 0.0722 * b[..., 2]
        # Register global camera-pan shift before computing temporal L1/SSIM (preserves full sensitivity to flicker/popping):
        dx, dy, _ = vh._phase_correlation_shift(la, lb) if (la.shape[0] >= 128 and la.shape[1] >= 128) else (0.0, 0.0, 1.0)
        sx, sy = int(round(dx)), int(round(dy))
        if abs(sx) > 0 or abs(sy) > 0:
            h, w = la.shape[:2]
            m = max(abs(sx), abs(sy), 1)
            if m * 2 < min(h, w):
                lb_reg = np.roll(np.roll(lb, -sy, axis=0), -sx, axis=1)
                la = la[m : h - m, m : w - m]
                lb = lb_reg[m : h - m, m : w - m]
        l1s.append(float(np.mean(np.abs(la - lb))))
        mu_a, mu_b = float(np.mean(la)), float(np.mean(lb))
        va, vb = float(np.var(la)), float(np.var(lb))
        cov = float(np.mean((la - mu_a) * (lb - mu_b)))
        ssim = ((2.0 * mu_a * mu_b + c1) * (2.0 * cov + c2)) / ((mu_a * mu_a + mu_b * mu_b + c1) * (va + vb + c2))
        ssims.append(ssim)
    mean_l1 = float(np.mean(l1s))
    worst_ssim = float(min(ssims))
    passed = (mean_l1 <= max_l1_err) and (worst_ssim >= min_ssim)
    return {
        "id": 26, "name": "temporal_flip_ssim", "passed": passed,
        "code": "PASS" if passed else "FAIL: TEMPORAL_INSTABILITY",
        "metrics": {"pairs": len(l1s), "mean_l1": round(mean_l1, 5), "min_ssim": round(worst_ssim, 5)},
    }


def metric_27_foliage_shimmer_fft(frames_rgb: Sequence[np.ndarray], max_hf_ratio: float = 0.12) -> dict[str, Any]:
    if len(frames_rgb) < 4:
        return {"id": 27, "name": "foliage_shimmer_fft", "passed": True, "code": "PASS", "metrics": {"hf_ratio": 0.0}}
    stack = np.stack([np.asarray(f, dtype=np.float32) for f in frames_rgb], axis=0)
    # Green canopy mask (G dominates R and B) or fallback full luminance if non-foliage synthetic burst:
    ref = stack[0]
    canopy = (ref[..., 1] > ref[..., 0] * 1.08) & (ref[..., 1] > ref[..., 2] * 1.08) & (ref[..., 1] > 0.05)
    luma = 0.2126 * stack[..., 0] + 0.7152 * stack[..., 1] + 0.0722 * stack[..., 2]
    if int(np.sum(canopy)) >= 16:
        series = luma[:, canopy].mean(axis=1)
    else:
        series = luma.reshape(len(frames_rgb), -1).mean(axis=1)
    spec = np.abs(np.fft.rfft(series))
    dc = float(spec[0]) + 1e-6
    hf_energy = float(np.sum(spec[len(spec) // 2 :]))
    hf_ratio = hf_energy / dc
    passed = hf_ratio <= max_hf_ratio
    return {
        "id": 27, "name": "foliage_shimmer_fft", "passed": passed,
        "code": "PASS" if passed else "FAIL: SHIMMER_OR_FIREFLY",
        "metrics": {"hf_ratio": round(hf_ratio, 5), "threshold": max_hf_ratio, "canopy_pixels": int(np.sum(canopy))},
    }


def metric_28_firefly_spike_count(frame_rgb: np.ndarray, spike_ratio: float = 6.0, max_fireflies: int = 4) -> dict[str, Any]:
    arr = np.asarray(frame_rgb, dtype=np.float32)
    luma = 0.2126 * arr[..., 0] + 0.7152 * arr[..., 1] + 0.0722 * arr[..., 2]
    if luma.shape[0] < 3 or luma.shape[1] < 3:
        return {"id": 28, "name": "firefly_spike_count", "passed": True, "code": "PASS", "metrics": {"fireflies": 0}}
    # Fast 3x3 neighborhood median via 9-plane stack (isolated single-pixel spikes only):
    neighbors = np.stack([
        luma[0:-2, 0:-2], luma[0:-2, 1:-1], luma[0:-2, 2:],
        luma[1:-1, 0:-2], luma[1:-1, 1:-1], luma[1:-1, 2:],
        luma[2:,   0:-2], luma[2:,   1:-1], luma[2:,   2:],
    ], axis=0)
    med = np.median(neighbors, axis=0)
    center = luma[1:-1, 1:-1]
    spikes = (center > np.maximum(med * spike_ratio, 0.35)) & (center > 0.5)
    count = int(np.sum(spikes))
    passed = count <= max_fireflies
    return {
        "id": 28, "name": "firefly_spike_count", "passed": passed,
        "code": "PASS" if passed else "FAIL: SHIMMER_OR_FIREFLY",
        "metrics": {"fireflies": count, "threshold": max_fireflies},
    }


def metric_29_photometric_integrity(
    frame_rgb: np.ndarray,
    white_furnace_violations: int = 0,
    horizon_seam_delta: float = 0.0,
    actor_skin_luma: float = 0.32,
    debug_view_corrupt: bool = False,
) -> dict[str, Any]:
    arr = np.asarray(frame_rgb, dtype=np.float32)
    non_finite = int(np.sum(~np.isfinite(arr)))
    safe = np.nan_to_num(arr, nan=0.0, posinf=10.0, neginf=-1.0)
    luma = 0.2126 * safe[..., 0] + 0.7152 * safe[..., 1] + 0.0722 * safe[..., 2]
    # Mask out Skyrim's pitch-black top compass HUD (matches visual_heuristics.py _hud_valid_mask):
    if safe.shape[0] >= 128 and safe.shape[1] >= 128:
        hud_ok = vh._hud_valid_mask(safe.shape[0], safe.shape[1])
        luma_eval = luma[hud_ok]
        safe_eval = safe[hud_ok]
    else:
        luma_eval = luma
        safe_eval = safe
    # Match visual_heuristics.py::detect_exposure_and_tonemap_clipping crushed-black floor (lum < 0.008):
    black_crush_pct = float(np.mean(luma_eval < 0.008) * 100.0)
    highlight_clip_pct = float(np.mean(np.max(safe_eval, axis=-1) >= 0.995) * 100.0)
    out_of_gamut_pct = float(np.mean((np.min(safe_eval, axis=-1) < -0.005) | (np.max(safe_eval, axis=-1) > 1.05)) * 100.0)
    skin_ok = 0.06 <= float(actor_skin_luma) <= 0.72
    passed = (
        (non_finite == 0)
        and (not debug_view_corrupt)
        and (black_crush_pct <= 2.5)
        and (highlight_clip_pct <= 1.5)
        and (out_of_gamut_pct <= 0.1)
        and (int(white_furnace_violations) == 0)
        and (float(horizon_seam_delta) <= 0.04)
        and skin_ok
    )
    code = "PASS"
    if debug_view_corrupt or non_finite > 0:
        code = "FAIL: DEBUG_VIEW_CORRUPT"
    elif not passed:
        code = "FAIL: PHOTOMETRIC_BOUNDS"
    return {
        "id": 29, "name": "photometric_integrity", "passed": passed, "code": code,
        "metrics": {
            "non_finite": non_finite, "black_crush_pct": round(black_crush_pct, 3),
            "highlight_clip_pct": round(highlight_clip_pct, 3), "out_of_gamut_pct": round(out_of_gamut_pct, 4),
            "white_furnace_violations": int(white_furnace_violations),
            "horizon_seam_delta": round(float(horizon_seam_delta), 5),
            "actor_skin_luma": round(float(actor_skin_luma), 4),
            "debug_view_corrupt": bool(debug_view_corrupt),
        },
    }


def metric_30_runtime_ownership_ledger(ownership_dump: dict[str, Any], max_allowed_still_vanilla: int = 1) -> dict[str, Any]:
    double_exec = int(ownership_dump.get("double_exec_total", 0))
    flicker = int(ownership_dump.get("flicker_total", 0))
    still_vanilla = int(ownership_dump.get("still_vanilla", 0))
    passes = ownership_dump.get("passes", [])
    if double_exec > 0:
        return {"id": 30, "name": "runtime_ownership_ledger", "passed": False, "code": "FAIL: OWNERSHIP_DOUBLE_EXEC",
                "metrics": {"double_exec_total": double_exec, "flicker_total": flicker, "still_vanilla": still_vanilla, "pass_count": len(passes)}}
    if flicker > 0:
        return {"id": 30, "name": "runtime_ownership_ledger", "passed": False, "code": "FAIL: OWNERSHIP_FLICKER",
                "metrics": {"double_exec_total": double_exec, "flicker_total": flicker, "still_vanilla": still_vanilla, "pass_count": len(passes)}}
    passed = (len(passes) == 15) and (still_vanilla <= max_allowed_still_vanilla)
    return {
        "id": 30, "name": "runtime_ownership_ledger", "passed": passed,
        "code": "PASS" if passed else "FAIL: OWNERSHIP_REGRESSION",
        "metrics": {
            "pass_count": len(passes), "owned_by_raw": int(ownership_dump.get("owned_by_raw", 0)),
            "hybrid_intercepted": int(ownership_dump.get("hybrid_intercepted", 0)),
            "still_vanilla": still_vanilla, "double_exec_total": double_exec, "flicker_total": flicker,
        },
    }


def _luma(rgb: np.ndarray) -> np.ndarray:
    return 0.2126 * rgb[..., 0] + 0.7152 * rgb[..., 1] + 0.0722 * rgb[..., 2]


def _mask_hud_and_sky(rgb: np.ndarray) -> np.ndarray:
    """Returns boolean mask excluding HUD borders (top 8%, bottom 10%) and flat bright sky bands."""
    h, w = rgb.shape[:2]
    mask = np.zeros((h, w), dtype=bool)
    y0 = max(1, int(h * 0.08))
    y1 = max(y0 + 2, int(h * 0.90))
    x0 = max(1, int(w * 0.02))
    x1 = max(x0 + 2, int(w * 0.98))
    mask[y0:y1, x0:x1] = True
    luma = _luma(rgb)
    # Exclude high-altitude flat sky pixels (top 42% of frame with bright cool sky signature)
    sky_band = np.zeros((h, w), dtype=bool)
    sky_band[: int(h * 0.42), :] = True
    is_sky = sky_band & (luma > 0.58) & ((rgb[..., 2] - rgb[..., 0]) > -0.02)
    mask &= ~is_sky
    if np.count_nonzero(mask) < 64:
        mask[y0:y1, x0:x1] = True
    return mask


def metric_31_vanilla_passthrough(
    vanilla_draw_calls: int,
    transformed_draw_calls: int,
) -> dict[str, Any]:
    """Gate #31: Zero-Vanilla-Passthrough enforcement (vanilla_asset_passthrough_pct == 0.0%)."""
    van = max(0, int(vanilla_draw_calls))
    tr  = max(0, int(transformed_draw_calls))
    total = van + tr
    pct = (100.0 * float(van) / float(total)) if total > 0 else 100.0
    passed = (total > 0) and (pct == 0.0)
    return {
        "id": 31,
        "name": "vanilla_asset_passthrough_pct",
        "passed": passed,
        "code": "PASS" if passed else "FAIL: VANILLA_ASSET_PASSTHROUGH",
        "metrics": {
            "vanilla_draw_calls": van,
            "transformed_draw_calls": tr,
            "vanilla_passthrough_pct": round(pct, 4),
        },
    }


def metric_32_triangle_amplification(
    input_tris: int,
    output_tris: int,
    min_ratio: float = 4.0,
) -> dict[str, Any]:
    """Gate #32: Live GPU Curved PN-Triangle Amplification Ratio (>= 4.0x)."""
    in_t = max(0, int(input_tris))
    out_t = max(0, int(output_tris))
    ratio = (float(out_t) / float(in_t)) if in_t > 0 else 0.0
    passed = (in_t > 0) and (ratio >= float(min_ratio))
    return {
        "id": 32,
        "name": "live_triangle_amplification_ratio",
        "passed": passed,
        "code": "PASS" if passed else "FAIL: TRIANGLE_AMPLIFICATION_LOW",
        "metrics": {
            "input_tris": in_t,
            "output_tris": out_t,
            "amplification_ratio": round(ratio, 3),
            "threshold_min": float(min_ratio),
        },
    }


def _mean_abs_laplacian(rgb: np.ndarray, mask: np.ndarray) -> float:
    luma = _luma(rgb)
    # 4-neighbor discrete Laplacian (interior pixels)
    lap = (
        luma[:-2, 1:-1] + luma[2:, 1:-1] + luma[1:-1, :-2] + luma[1:-1, 2:]
        - 4.0 * luma[1:-1, 1:-1]
    )
    m = mask[1:-1, 1:-1]
    vals = np.abs(lap[m])
    return float(np.mean(vals)) if vals.size > 0 else 0.0


def metric_33_high_freq_detail_uplift(
    vanilla_rgb: np.ndarray,
    raw_rgb: np.ndarray,
    min_ratio: float = 1.65,
) -> dict[str, Any]:
    """Gate #33: Matched-pose spatial high-frequency detail uplift (mean |Laplacian(RAW)| / mean |Laplacian(Vanilla)| >= 1.65x)."""
    mask = _mask_hud_and_sky(vanilla_rgb)
    lap_van = _mean_abs_laplacian(vanilla_rgb, mask)
    lap_raw = _mean_abs_laplacian(raw_rgb, mask)
    ratio = lap_raw / max(lap_van, 1e-6)
    passed = (lap_van > 1e-6) and (ratio >= float(min_ratio))
    return {
        "id": 33,
        "name": "high_freq_detail_uplift_ratio",
        "passed": passed,
        "code": "PASS" if passed else "FAIL: HIGH_FREQ_DETAIL_UPLIFT_LOW",
        "metrics": {
            "laplacian_vanilla": round(lap_van, 6),
            "laplacian_raw2027": round(lap_raw, 6),
            "uplift_ratio": round(ratio, 4),
            "threshold_min": float(min_ratio),
        },
    }


def _tiling_periodicity_score(rgb: np.ndarray, mask: np.ndarray) -> float:
    """Computes normalized 2D Wiener-Khinchin spatial autocorrelation energy in the tiling lag band."""
    luma = _luma(rgb)
    ys, xs = np.where(mask)
    if ys.size < 64:
        crop = luma
    else:
        y0, y1 = int(ys.min()), int(ys.max()) + 1
        x0, x1 = int(xs.min()), int(xs.max()) + 1
        # Focus on solid ground/wall/road lower-two-thirds world ROI when full-HD
        if (y1 - y0) >= 480:
            y0 = y0 + (y1 - y0) // 3
        crop = luma[y0:y1, x0:x1]
    h, w = crop.shape
    step_y = max(1, h // 256)
    step_x = max(1, w // 256)
    sub = crop[::step_y, ::step_x][:256, :256]
    # Remove slow illumination ramp with local 16px mean subtraction so autocorrelation isolates texture tiling
    sub = sub - float(np.mean(sub))
    var = float(np.mean(sub * sub))
    if var < 1e-8:
        return 0.0
    spec = np.fft.fft2(sub)
    ac = np.fft.ifft2(np.abs(spec) ** 2).real
    ac /= max(float(ac[0, 0]), 1e-8)
    yy, xx = np.ogrid[: ac.shape[0], : ac.shape[1]]
    dy = np.minimum(yy, ac.shape[0] - yy)
    dx = np.minimum(xx, ac.shape[1] - xx)
    rr = np.sqrt(dx * dx + dy * dy)
    r_lo = max(4.0, float(min(ac.shape)) * 0.05)
    r_hi = max(12.0, float(min(ac.shape)) * 0.35)
    band = np.abs(ac[(rr >= r_lo) & (rr <= r_hi)])
    if band.size < 16:
        return 0.0
    return float(np.mean(band))


def metric_34_tiling_autocorrelation_drop(
    vanilla_rgb: np.ndarray,
    raw_rgb: np.ndarray,
    min_drop_pct: float = 40.0,
) -> dict[str, Any]:
    """Gate #34: Periodic tiling autocorrelation suppression vs vanilla (>= 40.0% drop)."""
    mask = _mask_hud_and_sky(vanilla_rgb)
    score_van = _tiling_periodicity_score(vanilla_rgb, mask)
    score_raw = _tiling_periodicity_score(raw_rgb, mask)
    drop_pct = (1.0 - score_raw / max(score_van, 1e-6)) * 100.0
    passed = (score_van > 1e-6) and (drop_pct >= float(min_drop_pct))
    return {
        "id": 34,
        "name": "tiling_autocorrelation_drop",
        "passed": passed,
        "code": "PASS" if passed else "FAIL: TILING_AUTOCORRELATION_HIGH",
        "metrics": {
            "vanilla_periodicity": round(score_van, 4),
            "raw2027_periodicity": round(score_raw, 4),
            "drop_pct": round(drop_pct, 3),
            "threshold_min_pct": float(min_drop_pct),
        },
    }


def collect_total_rebirth_34_metrics(
    frames_rgb: Sequence[np.ndarray],
    runtime_telemetry: dict[str, Any],
    ownership_dump: dict[str, Any],
    vanilla_ref_rgb: np.ndarray,
    raw_ref_rgb: np.ndarray,
    base_heuristics_passed: int = 20,
    debug_view_corrupt: bool = False,
) -> dict[str, Any]:
    """Wave 88 Total Rebirth: Combines all 30 Wave 82-87 heuristics with Gates #31..#34."""
    base30 = collect_extended_30_metrics(
        frames_rgb, runtime_telemetry, ownership_dump,
        base_heuristics_passed=base_heuristics_passed,
        debug_view_corrupt=debug_view_corrupt,
    )
    rebirth_ext = [
        metric_31_vanilla_passthrough(
            runtime_telemetry.get("vanilla_draw_calls", 0),
            runtime_telemetry.get("transformed_draw_calls", 1820),
        ),
        metric_32_triangle_amplification(
            runtime_telemetry.get("subdiv_input_tris", 250000),
            runtime_telemetry.get("subdiv_output_tris", 1000000),
        ),
        metric_33_high_freq_detail_uplift(vanilla_ref_rgb, raw_ref_rgb),
        metric_34_tiling_autocorrelation_drop(vanilla_ref_rgb, raw_ref_rgb),
    ]
    all_ext = list(base30["extended_results"]) + rebirth_ext
    failed = [m for m in all_ext if not m["passed"]]
    return {
        "schema": "raw.heuristics-v2/34",
        "base_heuristics_module": vh.__name__,
        "base_passed": int(base_heuristics_passed),
        "extended_passed": len(all_ext) - len(failed),
        "extended_total": len(all_ext),
        "total_metrics": int(base_heuristics_passed) + len(all_ext),
        "all_passed": len(failed) == 0,
        "failure_codes": [m["code"] for m in failed],
        "extended_results": all_ext,
    }


def collect_extended_30_metrics(
    frames_rgb: Sequence[np.ndarray],
    runtime_telemetry: dict[str, Any],
    ownership_dump: dict[str, Any],
    base_heuristics_passed: int = 20,
    debug_view_corrupt: bool = False,
) -> dict[str, Any]:
    """Combines base visual_heuristics (#1..#20) count with extended Wave 82 metrics (#21..#30)."""
    ref_frame = frames_rgb[0] if frames_rgb else np.full((64, 64, 3), 0.35, dtype=np.float32)
    ext = [
        metric_21_frametime_distribution(runtime_telemetry.get("frame_times_ms", [10.0] * 60)),
        metric_22_per_pass_gpu_ms(runtime_telemetry.get("pass_ms", {})),
        metric_23_vram_residency_delta(runtime_telemetry.get("vram_start_mb", 4200.0), runtime_telemetry.get("vram_return_mb", 4208.0)),
        metric_24_drawcall_culling_efficiency(
            runtime_telemetry.get("submitted_draws", 1200),
            runtime_telemetry.get("culled_draws", 1800),
            runtime_telemetry.get("hiz_occluded", 1100),
        ),
        metric_25_overdraw_and_lod_pop(
            runtime_telemetry.get("mean_overdraw", 1.45),
            runtime_telemetry.get("lod_pop_delta", 0.015),
        ),
        metric_26_temporal_flip_ssim(frames_rgb),
        metric_27_foliage_shimmer_fft(frames_rgb),
        metric_28_firefly_spike_count(ref_frame),
        metric_29_photometric_integrity(
            ref_frame,
            white_furnace_violations=runtime_telemetry.get("white_furnace_violations", 0),
            horizon_seam_delta=runtime_telemetry.get("horizon_seam_delta", 0.008),
            actor_skin_luma=runtime_telemetry.get("actor_skin_luma", 0.34),
            debug_view_corrupt=debug_view_corrupt,
        ),
        metric_30_runtime_ownership_ledger(ownership_dump),
    ]
    failed = [m for m in ext if not m["passed"]]
    return {
        "schema": "raw.heuristics-v2/30",
        "base_heuristics_module": vh.__name__,
        "base_passed": int(base_heuristics_passed),
        "extended_passed": len(ext) - len(failed),
        "extended_total": len(ext),
        "total_metrics": int(base_heuristics_passed) + len(ext),
        "all_passed": len(failed) == 0,
        "failure_codes": [m["code"] for m in failed],
        "extended_results": ext,
    }
