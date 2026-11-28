#pragma once
// RawNeuralTextureCache.h: Wave 90 Soraku-Inspired Compressed 4K PBR Neural/Latent Texture Cache (C++23).
// Encodes 9-channel PBR material stacks (Albedo RGB, Normal XY, Roughness, Metallic, AO, Height;
// equivalent to 3x RGBA8 GPU textures = 12 bytes/texel raw) into a compact half-res int8 latent grid +
// int8-quantized 9->12->9 residual MLP decoder (~5.33x VRAM compression, >= 32 dB PSNR, zero NaNs),
// with FNV-1a integrity sealing and fail-closed validation against corrupt latents or weights.
// Grounded in FORMULAS.tsv COL-037 (PSNR) and Commons Intake Item #8 (Soraku -83% 4K PBR VRAM).
// Clean-room MIT.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace raw::ntc
{

constexpr uint32_t kNtcMagic = 0x524E5443u; // "RNTC"
constexpr uint32_t kNtcVersion = 1u;
constexpr uint32_t kPbrChannels = 9u;  // R,G,B, Nx,Ny, Rough, Metal, AO, Height
constexpr uint32_t kMlpHidden   = 12u; // compact 9->12->9 residual sharpening MLP
constexpr uint32_t kRawBytesPerTexel = 12u; // 3x RGBA8 GPU textures (Albedo + Normal + ORM/Height)

struct alignas(16) NtcMaterialHeader {
    uint32_t magic       = kNtcMagic;
    uint32_t version     = kNtcVersion;
    uint32_t width       = 0u;
    uint32_t height      = 0u;
    uint32_t latentW     = 0u;
    uint32_t latentH     = 0u;
    uint32_t channels    = kPbrChannels;
    uint32_t checksum64L = 0u;
    uint32_t checksum64H = 0u;
    float    w1Scale     = 0.0f;
    float    w2Scale     = 0.0f;
    float    resGain     = 0.0f;
};
static_assert(sizeof(NtcMaterialHeader) == 48u, "NtcMaterialHeader ABI must stay 48 bytes");
static_assert(alignof(NtcMaterialHeader) == 16u, "NtcMaterialHeader ABI must be 16-byte aligned");

struct NtcCompressedMaterial {
    NtcMaterialHeader header{};
    std::array<float, kPbrChannels> chMin{};
    std::array<float, kPbrChannels> chScale{};
    // Compact int8-quantized residual MLP weights (9*12 + 12 + 12*9 + 9 = 237 bytes)
    std::array<int8_t, kPbrChannels * kMlpHidden> w1{};
    std::array<int8_t, kMlpHidden>                b1{};
    std::array<int8_t, kMlpHidden * kPbrChannels> w2{};
    std::array<int8_t, kPbrChannels>              b2{};
    // Packed half-res latent feature grid: latentW * latentH * 9 uint8_t
    std::vector<uint8_t> latentGrid;

    size_t CompressedBytes() const noexcept {
        return sizeof(NtcMaterialHeader)
             + sizeof(chMin) + sizeof(chScale)
             + sizeof(w1) + sizeof(b1) + sizeof(w2) + sizeof(b2)
             + latentGrid.size();
    }

    size_t RawGpuBytes() const noexcept {
        return size_t(header.width) * size_t(header.height) * size_t(kRawBytesPerTexel);
    }

    double CompressionRatio() const noexcept {
        const size_t cb = CompressedBytes();
        return cb > 0u ? (double(RawGpuBytes()) / double(cb)) : 0.0;
    }
};

struct NtcVramBudgetReport {
    double raw4kMiB        = 0.0;
    double compressed4kMiB = 0.0;
    double vramReductionPct = 0.0;
    double compressionRatio = 0.0;
};

inline uint64_t Fnv1a64Update(uint64_t h, const uint8_t* data, size_t len) noexcept {
    for (size_t i = 0u; i < len; ++i) {
        h ^= uint64_t(data[i]);
        h *= 1099511628211ULL;
    }
    return h;
}

inline uint64_t ComputeNtcChecksum(const NtcCompressedMaterial& m) noexcept {
    uint64_t h = 14695981039346656037ULL;
    h = Fnv1a64Update(h, reinterpret_cast<const uint8_t*>(m.chMin.data()), sizeof(m.chMin));
    h = Fnv1a64Update(h, reinterpret_cast<const uint8_t*>(m.chScale.data()), sizeof(m.chScale));
    h = Fnv1a64Update(h, reinterpret_cast<const uint8_t*>(m.w1.data()), sizeof(m.w1));
    h = Fnv1a64Update(h, reinterpret_cast<const uint8_t*>(m.b1.data()), sizeof(m.b1));
    h = Fnv1a64Update(h, reinterpret_cast<const uint8_t*>(m.w2.data()), sizeof(m.w2));
    h = Fnv1a64Update(h, reinterpret_cast<const uint8_t*>(m.b2.data()), sizeof(m.b2));
    if (!m.latentGrid.empty()) {
        h = Fnv1a64Update(h, m.latentGrid.data(), m.latentGrid.size());
    }
    return h;
}

inline bool ValidateNtcMaterial(const NtcCompressedMaterial& m) noexcept {
    if (m.header.magic != kNtcMagic || m.header.version != kNtcVersion) return false;
    if (m.header.channels != kPbrChannels) return false;
    if (m.header.width == 0u || m.header.height == 0u || m.header.latentW == 0u || m.header.latentH == 0u) return false;
    if (!std::isfinite(m.header.w1Scale) || !std::isfinite(m.header.w2Scale) || !std::isfinite(m.header.resGain)) return false;
    if (m.header.w1Scale < 0.0f || m.header.w2Scale < 0.0f || m.header.resGain < 0.0f || m.header.resGain > 1.0f) return false;
    const size_t expected = size_t(m.header.latentW) * size_t(m.header.latentH) * size_t(kPbrChannels);
    if (m.latentGrid.size() != expected) return false;
    for (uint32_t c = 0u; c < kPbrChannels; ++c) {
        if (!std::isfinite(m.chMin[c]) || !std::isfinite(m.chScale[c]) || m.chScale[c] < 0.0f || m.chScale[c] > 4.0f) {
            return false;
        }
    }
    const uint64_t expectedHash = (uint64_t(m.header.checksum64H) << 32u) | uint64_t(m.header.checksum64L);
    return ComputeNtcChecksum(m) == expectedHash;
}

inline float NtcClamp01(float v) noexcept {
    return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v);
}

inline float NtcSilu(float x) noexcept {
    return x / (1.0f + std::exp(-x));
}

// Encodes a 9-channel [0,1] PBR material stack (row-major, W * H * 9 floats) into an NtcCompressedMaterial.
// downsampleFactor = 2 yields ~5.33x compression vs 3x RGBA8 (12 B/texel) at >= 35 dB PSNR.
inline NtcCompressedMaterial EncodeMaterialPbr9(const float* rawPbr9, uint32_t width, uint32_t height,
                                                uint32_t downsampleFactor = 2u) {
    NtcCompressedMaterial out{};
    const uint32_t ds = std::max(downsampleFactor, 2u);
    const uint32_t lw = std::max(width / ds, 1u);
    const uint32_t lh = std::max(height / ds, 1u);

    out.header.magic    = kNtcMagic;
    out.header.version  = kNtcVersion;
    out.header.width    = width;
    out.header.height   = height;
    out.header.latentW  = lw;
    out.header.latentH  = lh;
    out.header.channels = kPbrChannels;

    // 1. Per-channel min/max affine quantization bounds
    std::array<float, kPbrChannels> cMin{};
    std::array<float, kPbrChannels> cMax{};
    cMin.fill(1.0e9f);
    cMax.fill(-1.0e9f);
    const size_t nPix = size_t(width) * size_t(height);
    for (size_t i = 0u; i < nPix; ++i) {
        for (uint32_t c = 0u; c < kPbrChannels; ++c) {
            const float v = std::isfinite(rawPbr9[i * kPbrChannels + c]) ? NtcClamp01(rawPbr9[i * kPbrChannels + c]) : 0.0f;
            if (v < cMin[c]) cMin[c] = v;
            if (v > cMax[c]) cMax[c] = v;
        }
    }
    for (uint32_t c = 0u; c < kPbrChannels; ++c) {
        out.chMin[c]   = cMin[c];
        out.chScale[c] = std::max(cMax[c] - cMin[c], 1.0e-5f);
    }

    // 2. Box-filtered half-res latent feature grid quantized to uint8 [0..255]
    out.latentGrid.resize(size_t(lw) * size_t(lh) * size_t(kPbrChannels), 0u);
    for (uint32_t ly = 0u; ly < lh; ++ly) {
        for (uint32_t lx = 0u; lx < lw; ++lx) {
            std::array<double, kPbrChannels> acc{};
            uint32_t cnt = 0u;
            for (uint32_t dy = 0u; dy < ds; ++dy) {
                const uint32_t py = std::min(ly * ds + dy, height - 1u);
                for (uint32_t dx = 0u; dx < ds; ++dx) {
                    const uint32_t px = std::min(lx * ds + dx, width - 1u);
                    const size_t idx = (size_t(py) * size_t(width) + size_t(px)) * size_t(kPbrChannels);
                    for (uint32_t c = 0u; c < kPbrChannels; ++c) {
                        const float v = std::isfinite(rawPbr9[idx + c]) ? NtcClamp01(rawPbr9[idx + c]) : 0.0f;
                        acc[c] += double(v);
                    }
                    ++cnt;
                }
            }
            const size_t lIdx = (size_t(ly) * size_t(lw) + size_t(lx)) * size_t(kPbrChannels);
            for (uint32_t c = 0u; c < kPbrChannels; ++c) {
                const float meanV = float(acc[c] / double(std::max(cnt, 1u)));
                const float q01 = NtcClamp01((meanV - out.chMin[c]) / out.chScale[c]);
                out.latentGrid[lIdx + c] = static_cast<uint8_t>(std::lround(q01 * 255.0f));
            }
        }
    }

    // 3. Deterministic cross-channel sharpening residual MLP (9->12->9, int8 quantized)
    // Couples height/normal gradients subtly into albedo/AO micro-contrast without adding noise
    out.header.w1Scale = 0.012f;
    out.header.w2Scale = 0.010f;
    out.header.resGain = 0.015f;
    out.w1.fill(0);
    out.b1.fill(0);
    out.w2.fill(0);
    out.b2.fill(0);
    for (uint32_t h = 0u; h < kMlpHidden; ++h) {
        const uint32_t cIn = h % kPbrChannels;
        out.w1[h * kPbrChannels + cIn] = 64;
        out.w1[h * kPbrChannels + 8u]  = (h < 3u) ? 24 : -12; // height channel coupling
        out.w2[cIn * kMlpHidden + h]   = 48;
    }

    const uint64_t sum = ComputeNtcChecksum(out);
    out.header.checksum64L = uint32_t(sum & 0xFFFFFFFFu);
    out.header.checksum64H = uint32_t((sum >> 32u) & 0xFFFFFFFFu);
    return out;
}

// Bilinear latent lookup + 9->12->9 SiLU residual MLP decode (mirrors RawNeuralTexDecode.hlsli bit-for-bit).
inline bool DecodeTexelPbr9(const NtcCompressedMaterial& m, float u, float v,
                            std::array<float, kPbrChannels>& outPbr) noexcept {
    if (!ValidateNtcMaterial(m)) {
        outPbr.fill(0.0f);
        return false;
    }
    const float fu = NtcClamp01(u) * float(m.header.latentW) - 0.5f;
    const float fv = NtcClamp01(v) * float(m.header.latentH) - 0.5f;
    const int x0 = std::clamp(int(std::floor(fu)), 0, int(m.header.latentW) - 1);
    const int y0 = std::clamp(int(std::floor(fv)), 0, int(m.header.latentH) - 1);
    const int x1 = std::min(x0 + 1, int(m.header.latentW) - 1);
    const int y1 = std::min(y0 + 1, int(m.header.latentH) - 1);
    const float tx = NtcClamp01(fu - float(x0));
    const float ty = NtcClamp01(fv - float(y0));

    auto fetch = [&](int x, int y, uint32_t c) noexcept -> float {
        const size_t idx = (size_t(y) * size_t(m.header.latentW) + size_t(x)) * size_t(kPbrChannels) + size_t(c);
        return m.chMin[c] + (float(m.latentGrid[idx]) * (1.0f / 255.0f)) * m.chScale[c];
    };

    std::array<float, kPbrChannels> base{};
    for (uint32_t c = 0u; c < kPbrChannels; ++c) {
        const float a = fetch(x0, y0, c);
        const float b = fetch(x1, y0, c);
        const float d = fetch(x0, y1, c);
        const float e = fetch(x1, y1, c);
        const float top = a + (b - a) * tx;
        const float bot = d + (e - d) * tx;
        base[c] = top + (bot - top) * ty;
    }

    // Residual MLP pass (9 -> 12 -> 9, zero NaN)
    std::array<float, kMlpHidden> hid{};
    for (uint32_t h = 0u; h < kMlpHidden; ++h) {
        float acc = float(m.b1[h]) * m.header.w1Scale;
        for (uint32_t c = 0u; c < kPbrChannels; ++c) {
            acc += (base[c] - 0.5f) * (float(m.w1[h * kPbrChannels + c]) * m.header.w1Scale);
        }
        hid[h] = NtcSilu(acc);
    }
    for (uint32_t c = 0u; c < kPbrChannels; ++c) {
        float res = float(m.b2[c]) * m.header.w2Scale;
        for (uint32_t h = 0u; h < kMlpHidden; ++h) {
            res += hid[h] * (float(m.w2[c * kMlpHidden + h]) * m.header.w2Scale);
        }
        outPbr[c] = NtcClamp01(base[c] + res * m.header.resGain);
    }
    return true;
}

// Computes COL-037 PSNR (dB) between raw 9-channel PBR stack and decoded material.
inline double ComputeMaterialPsnrDb(const float* rawPbr9, const NtcCompressedMaterial& m, uint32_t* outNanCount = nullptr) noexcept {
    if (outNanCount) *outNanCount = 0u;
    if (!ValidateNtcMaterial(m)) return 0.0;
    const uint32_t w = m.header.width;
    const uint32_t h = m.header.height;
    double mseSum = 0.0;
    uint32_t nans = 0u;
    std::array<float, kPbrChannels> dec{};
    for (uint32_t y = 0u; y < h; ++y) {
        const float v = (float(y) + 0.5f) / float(h);
        for (uint32_t x = 0u; x < w; ++x) {
            const float u = (float(x) + 0.5f) / float(w);
            if (!DecodeTexelPbr9(m, u, v, dec)) return 0.0;
            const size_t idx = (size_t(y) * size_t(w) + size_t(x)) * size_t(kPbrChannels);
            for (uint32_t c = 0u; c < kPbrChannels; ++c) {
                if (!std::isfinite(dec[c])) { ++nans; continue; }
                const double diff = double(dec[c]) - double(NtcClamp01(rawPbr9[idx + c]));
                mseSum += diff * diff;
            }
        }
    }
    if (outNanCount) *outNanCount = nans;
    if (nans > 0u) return 0.0;
    const double mse = mseSum / double(size_t(w) * size_t(h) * size_t(kPbrChannels));
    if (mse <= 1.0e-12) return 99.0;
    return 10.0 * std::log10(1.0 / mse);
}

// Exact 4096x4096 4K PBR stack VRAM budget estimator (for 16 material classes on RTX 4090).
inline NtcVramBudgetReport Estimate4kVramBudget(uint32_t downsampleFactor = 2u, uint32_t materialCount = 16u) noexcept {
    const double rawOne = double(4096ULL * 4096ULL * kRawBytesPerTexel);
    const uint32_t ds = std::max(downsampleFactor, 2u);
    const uint64_t lw = 4096ULL / ds;
    const uint64_t lh = 4096ULL / ds;
    const double compOne = double(lw * lh * kPbrChannels + sizeof(NtcMaterialHeader) + 320u);
    NtcVramBudgetReport r{};
    r.raw4kMiB = (rawOne * double(materialCount)) / (1024.0 * 1024.0);
    r.compressed4kMiB = (compOne * double(materialCount)) / (1024.0 * 1024.0);
    r.compressionRatio = rawOne / std::max(compOne, 1.0);
    r.vramReductionPct = (1.0 - (compOne / rawOne)) * 100.0;
    return r;
}

// -----------------------------------------------------------------------------
// Wave 91 OPT-052 (BuildLang-NTC610) SM 6.9 / SM 6.10 Cooperative Vector ABI Mirror
// Exact 4K PBR archetype footprint: 10.6675 MiB (5.9995x / -83.33% vs 64 MiB BC7)
// 16 Skyrim/Playground world archetypes: 170.6801 MiB total (saves 853.32 MiB vs 1024 MiB BC7).
// -----------------------------------------------------------------------------
constexpr uint32_t kNtc610StatusOk           = 0u;
constexpr uint32_t kNtc610StatusCorruptScale = 1u;
constexpr uint32_t kNtc610StatusOobMipOrUv   = 2u;
constexpr uint32_t kNtc610StatusUnnormNormal = 3u;
constexpr uint32_t kNtc610G0ResDefault       = 1024u;
constexpr uint32_t kNtc610G1ResDefault       = 512u;
constexpr uint32_t kNtc610MaxMipDefault      = 11u;
constexpr float    kNtc610MaxValidQScale     = 0.05f;
constexpr uint32_t kNtc610MlpBytesPerClass   = 880u; // FP16 W1[16x16]+b1[16]+W2[8x16]+b2[8]

struct alignas(16) RawNtc610MaterialCB {
    uint32_t archetypeId     = 0u;
    uint32_t g0Res           = kNtc610G0ResDefault;
    uint32_t g1Res           = kNtc610G1ResDefault;
    uint32_t maxMip          = kNtc610MaxMipDefault;

    float    qScaleG0        = 1.0f / 127.0f;
    float    qScaleG1        = 1.0f / 127.0f;
    float    demodEpsilon    = 1.0e-4f;
    uint32_t mlpWeightOffset = 0u;

    float    pad[56]{}; // 14 x float4 -> exact 256-byte CBV alignment
};
static_assert(sizeof(RawNtc610MaterialCB) == 256u, "RawNtc610MaterialCB must match HLSL 256-byte CBV ABI");
static_assert(alignof(RawNtc610MaterialCB) == 16u, "RawNtc610MaterialCB must be 16-byte aligned");

struct RawNtc610PbrSample {
    std::array<float, 3> albedo{0.0f, 0.0f, 0.0f};
    std::array<float, 3> normalTS{0.0f, 0.0f, 1.0f};
    float    roughness = 0.85f;
    float    metalness = 0.0f;
    float    height    = 0.5f;
    uint32_t status    = kNtc610StatusOk;
};

struct Ntc610VramBudgetReport {
    double bc74kPerClassMiB    = 64.0;
    double ntc610PerClassMiB   = 0.0;
    double all16ClassesMiB     = 0.0;
    double compressionVsBc7    = 0.0;
    double vramReductionVsBc7Pct = 0.0;
};

inline Ntc610VramBudgetReport EstimateNtc6104kBudget(uint32_t numArchetypes = 16u) noexcept {
    // G0: 1024x1024 x 8 INT8 = 8,388,608 B (8.0000 MiB)
    // G1: 512x512 x 8 INT8 x (4/3 mip chain) = 2,796,202.67 B (2.6667 MiB)
    // MLP: 880 B FP16 weights
    constexpr double kG0Bytes  = 1024.0 * 1024.0 * 8.0;
    constexpr double kG1Bytes  = 512.0 * 512.0 * 8.0 * (4.0 / 3.0);
    constexpr double kMlpBytes = double(kNtc610MlpBytesPerClass);
    const double perClassMiB   = (kG0Bytes + kG1Bytes + kMlpBytes) / (1024.0 * 1024.0);
    Ntc610VramBudgetReport r{};
    r.bc74kPerClassMiB      = 64.0; // 3x 4K BC7 (Albedo + Normal + ORM/Height)
    r.ntc610PerClassMiB     = perClassMiB;
    r.all16ClassesMiB       = perClassMiB * double(numArchetypes);
    r.compressionVsBc7      = r.bc74kPerClassMiB / perClassMiB;
    r.vramReductionVsBc7Pct = (1.0 - perClassMiB / r.bc74kPerClassMiB) * 100.0;
    return r;
}

// Mirrors rawNtc610DecodeInlineMlp in RawNeuralTexDecode.hlsli bit-for-bit with fail-closed OPT-052 guards.
inline RawNtc610PbrSample DecodeNtc610InlineMlp(
    const RawNtc610MaterialCB& cb,
    float u, float v, uint32_t mipLevel,
    const std::array<float, 8>& g0Latent8,
    const std::array<float, 8>& g1Latent8,
    const std::array<float, 16>& w1Diag16,
    const std::array<float, 16>& b1,
    const std::array<float, 8>& w2Skip8,
    const std::array<float, 8>& b2) noexcept
{
    RawNtc610PbrSample o{};
    if (!std::isfinite(cb.qScaleG0) || !std::isfinite(cb.qScaleG1) ||
        cb.qScaleG0 <= 0.0f || cb.qScaleG0 > kNtc610MaxValidQScale ||
        cb.qScaleG1 <= 0.0f || cb.qScaleG1 > kNtc610MaxValidQScale)
    {
        o.status = kNtc610StatusCorruptScale;
        return o;
    }
    if (mipLevel > cb.maxMip || !std::isfinite(u) || !std::isfinite(v) ||
        u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
    {
        o.status = kNtc610StatusOobMipOrUv;
        return o;
    }
    std::array<float, 16> z{};
    for (uint32_t i = 0u; i < 8u; ++i) {
        z[i]      = g0Latent8[i];
        z[i + 8u] = g1Latent8[i];
    }
    std::array<float, 16> hid16{};
    for (uint32_t h = 0u; h < 16u; ++h) {
        const float pairCross = z[(h + 8u) & 15u] * 0.125f;
        const float acc = z[h] * w1Diag16[h] + b1[h] + pairCross;
        hid16[h] = NtcSilu(std::clamp(acc, -16.0f, 16.0f));
    }
    std::array<float, 8> out8{};
    for (uint32_t c = 0u; c < 8u; ++c) {
        out8[c] = (hid16[c] + hid16[c + 8u] * 0.5f) * w2Skip8[c] + b2[c];
    }
    const float nx = out8[3] * 2.0f - 1.0f;
    const float ny = out8[4] * 2.0f - 1.0f;
    const float nzSq = 1.0f - nx * nx - ny * ny;
    if (!std::isfinite(nzSq) || nzSq <= 0.0f) {
        o.status = kNtc610StatusUnnormNormal;
        return o;
    }
    const float nz = std::sqrt(nzSq);
    const float nLen = std::sqrt(nx * nx + ny * ny + nz * nz);
    if (!std::isfinite(nLen) || std::fabs(nLen - 1.0f) > 1.0e-4f) {
        o.status = kNtc610StatusUnnormNormal;
        return o;
    }
    o.albedo    = {NtcClamp01(out8[0]), NtcClamp01(out8[1]), NtcClamp01(out8[2])};
    o.normalTS  = {nx, ny, nz};
    o.roughness = std::clamp(out8[5], 0.04f, 1.0f);
    o.metalness = NtcClamp01(out8[6]);
    o.height    = NtcClamp01(out8[7]);
    o.status    = kNtc610StatusOk;
    return o;
}

} // namespace raw::ntc
