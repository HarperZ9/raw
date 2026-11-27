// SPDX-FileCopyrightText: 2026 Zain Dana Harper
// SPDX-License-Identifier: MIT
// Wave 92 Verification: CS-040 16 km Volumetric Fog Shadow Cache & Kiruna Twilight Dusk Photometry
// + Wave 92 2.2x Enlarged Carved Glacier Heightfield.

#include "d3d12_bridge/RawFogShadowCache16km.h"
#include "d3d12_bridge/RawTotalRebirth.h"

#include <cmath>
#include <cstdio>
#include <limits>

namespace {
int gPassed = 0;
int gFailed = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++gPassed;
        std::printf("[PASS] %s\n", name);
    } else {
        ++gFailed;
        std::printf("[FAIL] %s\n", name);
    }
}
} // namespace

int main() {
    using namespace raw::fsc16;

    // 1. 16 km Clipmap VRAM budget (6 levels x 256 x 256 x 32 RG16F = 48.00 MiB <= 512 MiB)
    const ClipmapBudget budget = ComputeClipmapBudget();
    std::printf("  [info] CS-040 16 km clipmap: %llu texels, %.2f MiB (budget <= %.0f MiB), radius=%.1f km, %u slices/frame\n",
                static_cast<unsigned long long>(budget.totalTexels), budget.totalMiB, kMaxVramBudgetMiB,
                budget.maxRadiusKm, budget.slicesPerFrame);
    Check(std::fabs(budget.totalMiB - 48.0) <= 1.0e-4 && budget.withinBudget512MiB &&
          std::fabs(budget.maxRadiusKm - 16.0) <= 1.0e-4 && budget.slicesPerFrame == 24u,
          "cs040_clipmap_48MiB_vram_and_16km_radius");

    // 2. Clipmap level selection, zero-shimmer texel origin snapping, and 8-frame round-robin slice scheduling
    Status st0 = Status::Ok, st5 = Status::Ok, stSnap = Status::Ok;
    const uint32_t lvlNear = LevelForDistance(10000.0f, &st0);
    const uint32_t lvlFar  = LevelForDistance(1050000.0f, &st5);
    const float t0 = TexelSizeU(0u);
    const auto sA = SnapOrigin(1234.5f, -5678.9f, 0u, &stSnap);
    const auto sB = SnapOrigin(1234.5f + t0 * 0.25f, -5678.9f + t0 * 0.10f, 0u, &stSnap);
    uint32_t coveredSlices = 0u;
    for (uint32_t s = 0u; s < kSlices; ++s) {
        uint32_t hits = 0u;
        for (uint32_t f = 0u; f < kUpdateDivisor; ++f) {
            if (SliceDueThisFrame(s, f)) { ++hits; }
        }
        if (hits == 1u) { ++coveredSlices; }
    }
    Check(st0 == Status::Ok && st5 == Status::Ok && lvlNear == 0u && lvlFar == 5u &&
          stSnap == Status::Ok && std::fabs(sA[0] - sB[0]) < 1.0e-4f && std::fabs(sA[1] - sB[1]) < 1.0e-4f &&
          coveredSlices == kSlices,
          "cs040_clipmap_levels_texel_snap_and_round_robin");

    // 3. Geometric sun-ray probe strides & cascade-to-far-field visibility crossfade
    const float p0 = ProbeStride(0u, 30000.0f);
    const float p1 = ProbeStride(1u, 30000.0f);
    const float p2 = ProbeStride(2u, 30000.0f);
    const float p3 = ProbeStride(3u, 30000.0f);
    const float vNear = CombineVisibility(0.25f, 0.95f, 6000.0f);
    const float vFar  = CombineVisibility(0.25f, 0.42f, 24000.0f);
    const float vEdge = CombineVisibility(0.25f, 0.42f, kRangeU + 50000.0f);
    Check(p0 < p1 && p1 < p2 && p2 < p3 && p0 >= kProbeMinU && p3 <= kProbeMaxU &&
          std::fabs(vNear - 0.25f) <= 1.0e-5f && std::fabs(vFar - 0.42f) <= 1.0e-5f &&
          std::fabs(vEdge - 1.0f) <= 1.0e-5f,
          "cs040_probe_strides_and_cascade_far_crossfade");

    // 4. Kiruna twilight dusk photometry: Kasten-Young airmass, 0.41 dex/deg decay, horizon/zenith ratio, Y == 1 tint
    Status stAm0 = Status::Ok, stAm85 = Status::Ok;
    const float amZen = KastenYoungAirmass(0.0f, &stAm0);
    const float amHor = KastenYoungAirmass(85.0f, &stAm85);
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    const TwilightSample twDusk = EvalKirunaTwilight(-3.0f * kDegToRad, 5.0f * kDegToRad, 0.90f);
    const TwilightSample twNoon = EvalKirunaTwilight(45.0f * kDegToRad, 5.0f * kDegToRad, 0.90f);
    const float tintY = 0.2126f * twDusk.tint[0] + 0.7152f * twDusk.tint[1] + 0.0722f * twDusk.tint[2];
    const float expectedRel = std::pow(10.0f, 0.41f * -3.0f);
    const auto noonMul = TwilightFogMul(twNoon, 0.55f);
    std::printf("  [info] Kiruna twilight: X(0)=%.6f X(85)=%.6f duskW=%.3f zenRel=%.6f hzRatio=%.3f tintY=%.6f\n",
                amZen, amHor, twDusk.weight, twDusk.zenithLumRel, twDusk.horizonZenithRatio, tintY);
    Check(stAm0 == Status::Ok && stAm85 == Status::Ok &&
          std::fabs(amZen - 0.999712f) < 1.0e-3f && std::fabs(amHor - 10.305791f) < 2.0e-2f &&
          twDusk.status == Status::Ok && twDusk.weight > 0.99f &&
          std::fabs(twDusk.zenithLumRel - expectedRel) < 1.0e-5f &&
          twDusk.horizonZenithRatio > 3.5f && twDusk.horizonZenithRatio <= kTwilightMaxRatio &&
          std::fabs(tintY - 1.0f) < 1.0e-5f && twNoon.weight == 0.0f &&
          std::fabs(noonMul[0] - 1.0f) < 1.0e-6f && std::fabs(noonMul[2] - 1.0f) < 1.0e-6f,
          "cs040_kiruna_twilight_photometry_and_noon_invariance");

    // 5. Planted Control 1: Negative and NaN distances rejected fail-closed
    Status stNeg = Status::Ok, stNan = Status::Ok;
    (void)LevelForDistance(-500.0f, &stNeg);
    (void)LevelForDistance(std::numeric_limits<float>::quiet_NaN(), &stNan);
    Check(stNeg == Status::InvalidDistance && stNan == Status::InvalidDistance,
          "control_1_planted_negative_and_nan_distance_rejected");

    // 6. Planted Control 2: OOB clipmap level (>= 6) rejected fail-closed
    Status stLvl = Status::Ok;
    const float badSide = LevelSideU(kLevels, &stLvl);
    Check(stLvl == Status::InvalidClipmapLevel && badSide == 0.0f,
          "control_2_planted_oob_clipmap_level_rejected");

    // 7. Planted Control 3: Invalid Kasten-Young zenith angle (>= 90 deg or negative) rejected fail-closed
    Status stZ90 = Status::Ok, stZNeg = Status::Ok;
    (void)KastenYoungAirmass(90.0f, &stZ90);
    (void)KastenYoungAirmass(-2.0f, &stZNeg);
    Check(stZ90 == Status::InvalidZenithAngle && stZNeg == Status::InvalidZenithAngle,
          "control_3_planted_invalid_zenith_angle_rejected");

    // 8. Planted Control 4: Thin foreground prop (zHit=500u vs zProbe=40000u) must NEVER shadow far mountain fog,
    //    while real mountain ridge (zHit=32000u vs zProbe=40000u) occludes cleanly and invalid probe depth fails closed.
    Status stProp = Status::Ok, stRidge = Status::Ok, stBadZ = Status::Ok;
    const float occProp  = ProbeOcclusion(500.0f, 40000.0f, 0.5f, 0.5f, &stProp);
    const float occRidge = ProbeOcclusion(32000.0f, 40000.0f, 0.5f, 0.5f, &stRidge);
    const float occBadZ  = ProbeOcclusion(-10.0f, 0.5f, 0.5f, 0.5f, &stBadZ);
    Check(stProp == Status::Ok && std::fabs(occProp - 1.0f) < 1.0e-5f &&
          stRidge == Status::Ok && occRidge < 0.35f &&
          stBadZ == Status::InvalidProbeDepth && std::fabs(occBadZ - 1.0f) < 1.0e-5f,
          "control_4_planted_thin_prop_no_false_shadow_and_bad_depth_rejected");

    // 9. Wave 92 2.2x enlarged carved sapphire glacier heightfield bounded & distinct
    float hMin = 1.0f, hMax = 0.0f;
    for (int iy = 0; iy < 32; ++iy) {
        for (int ix = 0; ix < 32; ++ix) {
            const float x = float(ix) * 0.14f;
            const float y = float(iy) * 0.14f;
            const float h = raw::rebirth::EvalGlacierAnisotropicHeight({x, y, 0.3f}, {x, y});
            if (h < hMin) hMin = h;
            if (h > hMax) hMax = h;
        }
    }
    std::printf("  [info] Wave92 2.2x glacier facet height range=[%.3f, %.3f]\n", hMin, hMax);
    Check(hMin >= 0.0f && hMax <= 1.0f && (hMax - hMin) >= 0.30f,
          "wave92_enlarged_glacier_facets_bounded_and_distinct");

    std::printf("wave92_cs040_and_visual_audit_check: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
