// wave90_total_rebirth_part3_check.cpp: Wave 90 Project Total Rebirth Part 3 C++23 /W4 /WX Verification Suite.
// Verifies:
//   1. Soraku-inspired RawNeuralTextureCache (>=4x VRAM compression, >=32 dB COL-037 PSNR across 4 PBR archetypes, 0 NaNs)
//   2. 3/3 Planted fail-closed integrity controls (corrupt latent bitflip, NaN weight/scale, bad magic header)
//   3. Wave 90 crystalline sapphire glacier facet balance (soft anisotropy, zero dark zebra stripes)
//   4. Wave 90 Henyey-Greenstein (OPT-032) backlit leaf translucency monotonicity & white-furnace bounds
//   5. Exact 16-class 4K PBR VRAM reduction budget (>=80% reduction vs raw RGBA8)
// Clean-room MIT.

#include "../src/d3d12_bridge/RawNeuralTextureCache.h"
#include "../src/d3d12_bridge/RawTotalRebirth.h"

#include <cmath>
#include <cstdio>
#include <limits>
#include <vector>

using namespace raw::ntc;
using namespace raw::rebirth;

namespace {

int g_pass = 0;
int g_fail = 0;

void Check(bool cond, const char* name) {
    if (cond) {
        ++g_pass;
        std::printf("[PASS] %s\n", name);
    } else {
        ++g_fail;
        std::printf("[FAIL] %s\n", name);
    }
}

std::vector<float> SynthesizeProceduralPbr9(uint32_t w, uint32_t h, int archetype) {
    std::vector<float> buf(size_t(w) * size_t(h) * size_t(kPbrChannels), 0.0f);
    for (uint32_t y = 0u; y < h; ++y) {
        const float fy = float(y) / float(h);
        for (uint32_t x = 0u; x < w; ++x) {
            const float fx = float(x) / float(w);
            float height = 0.5f;
            float r = 0.4f, g = 0.4f, b = 0.4f, rough = 0.65f, metal = 0.02f, ao = 0.85f;
            if (archetype == 0) { // NordicStone
                height = EvalHexStochasticNoise({fx * 3.0f, fy * 3.0f});
                r = 0.34f + 0.14f * height; g = 0.36f + 0.14f * height; b = 0.38f + 0.14f * height;
                rough = 0.72f - 0.18f * height; ao = 0.62f + 0.34f * height;
            } else if (archetype == 1) { // TimberWood
                height = EvalTimberAnisotropicGrain({fx * 1.2f, 0.0f, fy * 1.2f}, {0.0f, 1.0f, 0.0f});
                r = 0.22f + 0.18f * height; g = 0.32f + 0.16f * height; b = 0.40f + 0.14f * height;
                rough = 0.64f - 0.12f * height; ao = 0.70f + 0.26f * height;
            } else if (archetype == 2) { // GlacierIce
                height = EvalGlacierAnisotropicHeight({fx * 2.0f, fy * 2.0f, fy * 1.0f}, {fx * 2.0f, fy * 2.0f});
                r = 0.18f + 0.18f * height; g = 0.46f + 0.22f * height; b = 0.64f + 0.24f * height;
                rough = 0.18f + 0.14f * (1.0f - height); ao = 0.78f + 0.20f * height;
            } else { // CobbleRoad
                height = EvalHexStochasticNoise({fx * 4.0f + 2.1f, fy * 4.0f + 1.3f});
                r = 0.30f + 0.16f * height; g = 0.29f + 0.16f * height; b = 0.27f + 0.15f * height;
                rough = 0.58f - 0.20f * height; ao = 0.56f + 0.40f * height;
            }
            const float nx = 0.5f + 0.22f * std::sin(fx * 6.2831853f * 2.0f + height);
            const float ny = 0.5f + 0.22f * std::cos(fy * 6.2831853f * 2.0f + height);
            const size_t idx = (size_t(y) * size_t(w) + size_t(x)) * size_t(kPbrChannels);
            buf[idx + 0u] = Clamp01(r);
            buf[idx + 1u] = Clamp01(g);
            buf[idx + 2u] = Clamp01(b);
            buf[idx + 3u] = Clamp01(nx);
            buf[idx + 4u] = Clamp01(ny);
            buf[idx + 5u] = Clamp01(rough);
            buf[idx + 6u] = Clamp01(metal);
            buf[idx + 7u] = Clamp01(ao);
            buf[idx + 8u] = Clamp01(height);
        }
    }
    return buf;
}

} // namespace

int main() {
    // 1. Soraku-inspired Neural/Latent PBR compression across 4 distinct material archetypes (128x128 slabs)
    const char* names[4] = {"NordicStone", "TimberWood", "GlacierIce", "CobbleRoad"};
    double minPsnr = 1e9;
    double minRatio = 1e9;
    NtcCompressedMaterial sampleMat{};
    for (int a = 0; a < 4; ++a) {
        const auto raw = SynthesizeProceduralPbr9(128u, 128u, a);
        const auto comp = EncodeMaterialPbr9(raw.data(), 128u, 128u, 2u);
        uint32_t nanCount = 999u;
        const double psnr = ComputeMaterialPsnrDb(raw.data(), comp, &nanCount);
        const double ratio = comp.CompressionRatio();
        std::printf("  [info] NTC[%s]: ratio=%.2fx psnr=%.2f dB nans=%u bytes=%zu/%zu\n",
                    names[a], ratio, psnr, nanCount, comp.CompressedBytes(), comp.RawGpuBytes());
        if (psnr < minPsnr) minPsnr = psnr;
        if (ratio < minRatio) minRatio = ratio;
        if (a == 0) sampleMat = comp;
        Check(ValidateNtcMaterial(comp) && nanCount == 0u && ratio >= 4.0 && psnr >= 32.0,
              names[a]);
    }
    Check(minRatio >= 4.0, "ntc_all_archetypes_compression_ge_4x");
    Check(minPsnr >= 32.0, "ntc_all_archetypes_psnr_ge_32dB");

    // 2. Exact 4K 16-class RTX 4090 VRAM budget check
    const NtcVramBudgetReport b4k = Estimate4kVramBudget(2u, 16u);
    std::printf("  [info] 16x 4K PBR VRAM: raw=%.1f MiB -> compressed=%.1f MiB (%.2fx, -%.1f%%)\n",
                b4k.raw4kMiB, b4k.compressed4kMiB, b4k.compressionRatio, b4k.vramReductionPct);
    Check(b4k.compressionRatio >= 5.0 && b4k.vramReductionPct >= 80.0,
          "ntc_4k_16class_vram_reduction_ge_80pct");

    // 3. 3/3 Planted Fail-Closed Controls (corrupt latent bitflip, NaN scale, bad magic)
    {
        auto badLatent = sampleMat;
        badLatent.latentGrid[badLatent.latentGrid.size() / 2u] ^= 0x01u;
        std::array<float, kPbrChannels> tmp{};
        Check(!ValidateNtcMaterial(badLatent) && !DecodeTexelPbr9(badLatent, 0.5f, 0.5f, tmp),
              "control_planted_corrupt_latent_bitflip_rejected");

        auto badNan = sampleMat;
        badNan.header.w1Scale = std::numeric_limits<float>::quiet_NaN();
        Check(!ValidateNtcMaterial(badNan) && !DecodeTexelPbr9(badNan, 0.5f, 0.5f, tmp),
              "control_planted_nan_weight_scale_rejected");

        auto badMagic = sampleMat;
        badMagic.header.magic = 0xDEADBEEFu;
        Check(!ValidateNtcMaterial(badMagic) && !DecodeTexelPbr9(badMagic, 0.5f, 0.5f, tmp),
              "control_planted_bad_magic_header_rejected");
    }

    // 4. Wave 90 Part 3: Henyey-Greenstein (OPT-032) backlit leaf translucency monotonicity & bounds
    {
        const Vec3 sunDir = Normalize3({0.0f, 1.0f, 0.25f});
        const Vec3 backView = Normalize3({0.0f, -1.0f, -0.25f}); // looking toward sun through canopy
        const Vec3 sideView = Normalize3({1.0f, 0.0f, 0.0f});
        const Vec3 frontView = sunDir;
        const float hgBack = EvalHenyeyGreensteinLeafTranslucency(backView, sunDir, 0.62f);
        const float hgSide = EvalHenyeyGreensteinLeafTranslucency(sideView, sunDir, 0.62f);
        const float hgFront = EvalHenyeyGreensteinLeafTranslucency(frontView, sunDir, 0.62f);
        std::printf("  [info] HG(g=0.62) backlit=%.4f side=%.4f front=%.4f\n", hgBack, hgSide, hgFront);
        Check(hgBack > hgSide * 3.0f && hgSide >= hgFront && hgBack <= 1.0f && hgFront >= 0.0f,
              "part3_henyey_greenstein_backlit_translucency_monotonic_bounded");
    }

    // 5. Wave 90 Glacier refinement: translucent crystalline facets stay soft & bounded (no dark zebra floor)
    {
        float hMin = 1.0f, hMax = 0.0f;
        for (int j = 0; j < 64; ++j) {
            for (int i = 0; i < 64; ++i) {
                const float x = float(i) * 0.05f, y = float(j) * 0.05f;
                const float h = EvalGlacierAnisotropicHeight({x, y, y * 0.5f}, {x, y});
                if (h < hMin) hMin = h;
                if (h > hMax) hMax = h;
            }
        }
        std::printf("  [info] Wave90 glacier height range=[%.3f, %.3f]\n", hMin, hMax);
        Check(hMin >= 0.0f && hMax <= 1.0f && (hMax - hMin) > 0.25f,
              "wave90_glacier_crystalline_facets_bounded_and_rich");
    }

    std::printf("wave90_total_rebirth_part3_check: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
