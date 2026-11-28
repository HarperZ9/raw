// SPDX-FileCopyrightText: 2026 Zain Dana Harper
// SPDX-License-Identifier: MIT
// Wave 91 Verification: OPT-052 BuildLang-NTC610 SM 6.9/6.10 ABI & Inline MLP + Wave 91 Carved Glacier Heightfield.

#include "d3d12_bridge/RawNeuralTextureCache.h"
#include "d3d12_bridge/RawTotalRebirth.h"

#include <array>
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
    using namespace raw::ntc;

    // 1. Exact 256-byte CBV ABI alignment check
    Check(sizeof(RawNtc610MaterialCB) == 256u && alignof(RawNtc610MaterialCB) == 16u,
          "ntc610_material_cb_exact_256b_abi");

    // 2. OPT-052 4K PBR VRAM budget check (-83.33% vs 64 MiB BC7, 170.68 MiB for 16 world archetypes)
    const Ntc610VramBudgetReport b = EstimateNtc6104kBudget(16u);
    std::printf("  [info] NTC610 4K PBR: %.4f MiB/class (%.4fx vs 64 MiB BC7, -%.2f%% VRAM); 16 classes = %.4f MiB\n",
                b.ntc610PerClassMiB, b.compressionVsBc7, b.vramReductionVsBc7Pct, b.all16ClassesMiB);
    Check(b.ntc610PerClassMiB > 10.60 && b.ntc610PerClassMiB < 10.75,
          "ntc610_per_class_4k_footprint_10_67MiB");
    Check(b.compressionVsBc7 >= 5.99 && b.vramReductionVsBc7Pct >= 83.30,
          "ntc610_vram_reduction_ge_83_3pct_vs_bc7");
    Check(b.all16ClassesMiB <= 176.0,
          "ntc610_16_world_archetypes_le_176MiB");

    // 3. Valid inline 16->16->8 MLP decode check
    RawNtc610MaterialCB cb{};
    cb.archetypeId = 3u;
    const std::array<float, 8> g0{0.42f, 0.56f, 0.68f, 0.52f, 0.49f, 0.78f, 0.05f, 0.61f};
    const std::array<float, 8> g1{0.38f, 0.50f, 0.64f, 0.51f, 0.50f, 0.74f, 0.04f, 0.58f};
    std::array<float, 16> w1{};
    std::array<float, 16> b1{};
    for (size_t i = 0; i < 16; ++i) { w1[i] = 0.85f; b1[i] = 0.05f; }
    const std::array<float, 8> w2{0.55f, 0.55f, 0.55f, 0.25f, 0.25f, 0.50f, 0.20f, 0.50f};
    const std::array<float, 8> b2{0.18f, 0.20f, 0.22f, 0.42f, 0.43f, 0.45f, 0.02f, 0.25f};

    const RawNtc610PbrSample sOk = DecodeNtc610InlineMlp(cb, 0.35f, 0.65f, 2u, g0, g1, w1, b1, w2, b2);
    const float nLen = std::sqrt(sOk.normalTS[0] * sOk.normalTS[0] +
                                 sOk.normalTS[1] * sOk.normalTS[1] +
                                 sOk.normalTS[2] * sOk.normalTS[2]);
    std::printf("  [info] NTC610 valid decode: status=%u alb=(%.3f,%.3f,%.3f) |N|=%.6f rough=%.3f\n",
                sOk.status, sOk.albedo[0], sOk.albedo[1], sOk.albedo[2], nLen, sOk.roughness);
    Check(sOk.status == kNtc610StatusOk && std::fabs(nLen - 1.0f) <= 1.0e-4f &&
          sOk.roughness >= 0.04f && sOk.roughness <= 1.0f,
          "ntc610_valid_inline_mlp_decode_ok_unit_normal");

    // 4. Planted Control 1: Corrupt quantizer scale (> 0.05) rejected fail-closed
    RawNtc610MaterialCB cbBadScale = cb;
    cbBadScale.qScaleG0 = 0.125f;
    const RawNtc610PbrSample sBadScale = DecodeNtc610InlineMlp(cbBadScale, 0.5f, 0.5f, 0u, g0, g1, w1, b1, w2, b2);
    Check(sBadScale.status == kNtc610StatusCorruptScale,
          "control_1_planted_corrupt_qscale_rejected");

    // 5. Planted Control 2: OOB mip (> 11) and OOB UV (> 1.0) rejected fail-closed
    const RawNtc610PbrSample sBadMip = DecodeNtc610InlineMlp(cb, 0.5f, 0.5f, 15u, g0, g1, w1, b1, w2, b2);
    const RawNtc610PbrSample sBadUv  = DecodeNtc610InlineMlp(cb, 1.25f, 0.5f, 0u, g0, g1, w1, b1, w2, b2);
    Check(sBadMip.status == kNtc610StatusOobMipOrUv && sBadUv.status == kNtc610StatusOobMipOrUv,
          "control_2_planted_oob_mip_and_uv_rejected");

    // 6. Planted Control 3: Unnormalized tangent-space normal (nx^2 + ny^2 >= 1.0) rejected fail-closed
    std::array<float, 8> b2BadNorm = b2;
    b2BadNorm[3] = 1.45f; // drives nx > 0.9
    b2BadNorm[4] = 1.45f; // drives ny > 0.9 -> nx^2 + ny^2 > 1.0
    const RawNtc610PbrSample sBadNorm = DecodeNtc610InlineMlp(cb, 0.5f, 0.5f, 0u, g0, g1, w1, b1, w2, b2BadNorm);
    Check(sBadNorm.status == kNtc610StatusUnnormNormal,
          "control_3_planted_unnorm_normal_rejected");

    // 7. Planted Control 4: NaN quantizer scale rejected fail-closed
    RawNtc610MaterialCB cbNan = cb;
    cbNan.qScaleG1 = std::numeric_limits<float>::quiet_NaN();
    const RawNtc610PbrSample sNan = DecodeNtc610InlineMlp(cbNan, 0.5f, 0.5f, 0u, g0, g1, w1, b1, w2, b2);
    Check(sNan.status == kNtc610StatusCorruptScale,
          "control_4_planted_nan_qscale_rejected");

    // 8. Wave 91 Carved Sapphire Glacier heightfield bounded & rich across crystal facets & hairline seams
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
    std::printf("  [info] Wave91 carved glacier facet height range=[%.3f, %.3f]\n", hMin, hMax);
    Check(hMin >= 0.0f && hMax <= 1.0f && (hMax - hMin) >= 0.30f,
          "wave91_carved_glacier_facets_bounded_and_distinct");

    std::printf("wave91_ntc610_and_visual_audit_check: %d passed, %d failed\n", gPassed, gFailed);
    return gFailed == 0 ? 0 : 1;
}
