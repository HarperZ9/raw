// SPDX-FileCopyrightText: 2026 Zain Dana Harper
// SPDX-License-Identifier: MIT
// RawFogShadowCache16km.h: C++23 exact mirror of CS-040 RawFogShadowCache16km.hlsli
// (16 km far-field volumetric fog shadow clipmap cache + Kiruna twilight dusk photometry).

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace raw::fsc16 {

inline constexpr float    kUnitsPerMeter    = 70.0f;
inline constexpr float    kRangeU           = 1120000.0f;  // 16 km at 70 u/m
inline constexpr float    kLevel0SideU      = 70000.0f;    // 1 km innermost clipmap side
inline constexpr uint32_t kLevels           = 6u;          // 1, 2, 4, 8, 16, 32 km sides
inline constexpr uint32_t kTexelsXY         = 256u;
inline constexpr uint32_t kSlices           = 32u;
inline constexpr uint32_t kBytesPerTexel    = 4u;          // RG16F
inline constexpr uint32_t kUpdateDivisor    = 8u;          // 8-frame round-robin slice update
inline constexpr uint32_t kProbeCount       = 4u;
inline constexpr float    kCascadeEndU      = 12000.0f;
inline constexpr float    kCascadeFadeU     = 3000.0f;
inline constexpr float    kProbeMinU        = 600.0f;
inline constexpr float    kProbeMaxU        = 140000.0f;
inline constexpr float    kVisFloor         = 0.14f;
inline constexpr float    kTwilightMaxRatio = 4.0f;
inline constexpr double   kMaxVramBudgetMiB = 512.0;

enum class Status : uint32_t {
    Ok                  = 0u,
    InvalidDistance     = 1u,
    InvalidClipmapLevel = 2u,
    InvalidZenithAngle  = 3u,
    InvalidProbeDepth   = 4u
};

struct ClipmapBudget {
    uint64_t totalTexels       = 0u;
    uint64_t totalBytes        = 0u;
    double   totalMiB          = 0.0;
    double   maxRadiusKm       = 0.0;
    uint32_t slicesPerFrame    = 0u;
    bool     withinBudget512MiB = false;
};

struct TwilightSample {
    Status               status             = Status::Ok;
    std::array<float, 3> tint               = {1.0f, 1.0f, 1.0f};
    float                gain               = 1.0f;
    float                weight             = 0.0f;
    float                zenithLumRel       = 1.0f;
    float                horizonZenithRatio = 1.0f;
};

inline float Clamp01(float x) noexcept {
    return std::clamp(x, 0.0f, 1.0f);
}

inline float Smoothstep(float e0, float e1, float x) noexcept {
    if (e1 <= e0) { return x >= e1 ? 1.0f : 0.0f; }
    const float t = Clamp01((x - e0) / (e1 - e0));
    return t * t * (3.0f - 2.0f * t);
}

inline ClipmapBudget ComputeClipmapBudget() noexcept {
    ClipmapBudget b{};
    const uint64_t perLevelTexels = uint64_t(kTexelsXY) * uint64_t(kTexelsXY) * uint64_t(kSlices);
    b.totalTexels       = perLevelTexels * uint64_t(kLevels);
    b.totalBytes        = b.totalTexels * uint64_t(kBytesPerTexel);
    b.totalMiB          = double(b.totalBytes) / (1024.0 * 1024.0);
    const float outerSideU = kLevel0SideU * float(1u << (kLevels - 1u));
    b.maxRadiusKm       = double(outerSideU * 0.5f) / double(kUnitsPerMeter * 1000.0f);
    b.slicesPerFrame    = (kLevels * kSlices) / kUpdateDivisor;
    b.withinBudget512MiB = (b.totalMiB <= kMaxVramBudgetMiB);
    return b;
}

inline Status ValidateDistance(float distU) noexcept {
    return (std::isfinite(distU) && distU >= 0.0f) ? Status::Ok : Status::InvalidDistance;
}

inline uint32_t LevelForDistance(float distU, Status* outStatus = nullptr) noexcept {
    if (ValidateDistance(distU) != Status::Ok) {
        if (outStatus) { *outStatus = Status::InvalidDistance; }
        return 0u;
    }
    if (outStatus) { *outStatus = Status::Ok; }
    const float ratio = distU / (kLevel0SideU * 0.5f);
    if (ratio <= 1.0f) { return 0u; }
    return std::min(uint32_t(std::ceil(std::log2(ratio))), kLevels - 1u);
}

inline float LevelSideU(uint32_t level, Status* outStatus = nullptr) noexcept {
    if (level >= kLevels) {
        if (outStatus) { *outStatus = Status::InvalidClipmapLevel; }
        return 0.0f;
    }
    if (outStatus) { *outStatus = Status::Ok; }
    return kLevel0SideU * float(1u << level);
}

inline float TexelSizeU(uint32_t level, Status* outStatus = nullptr) noexcept {
    Status st = Status::Ok;
    const float side = LevelSideU(level, &st);
    if (outStatus) { *outStatus = st; }
    return (st == Status::Ok) ? (side / float(kTexelsXY)) : 0.0f;
}

inline std::array<float, 2> SnapOrigin(float camX, float camY, uint32_t level, Status* outStatus = nullptr) noexcept {
    Status st = Status::Ok;
    const float t = TexelSizeU(level, &st);
    if (outStatus) { *outStatus = st; }
    if (st != Status::Ok || !std::isfinite(camX) || !std::isfinite(camY)) {
        if (outStatus && st == Status::Ok) { *outStatus = Status::InvalidDistance; }
        return {0.0f, 0.0f};
    }
    return {std::floor(camX / t) * t, std::floor(camY / t) * t};
}

inline bool SliceDueThisFrame(uint32_t slice, uint32_t frameIndex) noexcept {
    return (slice % kUpdateDivisor) == (frameIndex % kUpdateDivisor);
}

inline float ProbeStride(uint32_t i, float rayDistU) noexcept {
    const float safeD = std::isfinite(rayDistU) ? std::max(rayDistU, 0.0f) : 0.0f;
    const float mul = 0.04f * std::pow(3.0f, float(std::min(i, kProbeCount - 1u)));
    return std::clamp(safeD * mul, kProbeMinU, kProbeMaxU);
}

inline float FarFieldWeight(float distU) noexcept {
    const float d = std::isfinite(distU) ? std::max(distU, 0.0f) : 0.0f;
    return Smoothstep(kCascadeEndU - kCascadeFadeU, kCascadeEndU, d)
         * (1.0f - Smoothstep(kRangeU * 0.85f, kRangeU, d));
}

inline float ProbeOcclusion(float zHit, float zProbe, float uvX, float uvY, Status* outStatus = nullptr) noexcept {
    if (!std::isfinite(zHit) || !std::isfinite(zProbe) || zProbe <= 1.0f || zHit < 0.0f) {
        if (outStatus) { *outStatus = Status::InvalidProbeDepth; }
        return 1.0f;
    }
    if (outStatus) { *outStatus = Status::Ok; }
    const float minEdge = std::min(std::min(uvX, 1.0f - uvX), std::min(uvY, 1.0f - uvY));
    const float edgeV = Smoothstep(0.0f, 0.06f, minEdge);
    const float rel = (zProbe - zHit) / zProbe;
    const float hitW = Smoothstep(0.04f, 0.08f, rel) * (1.0f - Smoothstep(0.35f, 0.45f, rel));
    return 1.0f + (0.30f - 1.0f) * (edgeV * hitW);
}

inline float ResolveProbeVis(float occProduct) noexcept {
    return std::clamp(std::isfinite(occProduct) ? occProduct : 1.0f, kVisFloor, 1.0f);
}

inline float CombineVisibility(float cascadeVis, float farProbeVis, float distU) noexcept {
    const float cv = Clamp01(std::isfinite(cascadeVis) ? cascadeVis : 1.0f);
    const float fv = Clamp01(std::isfinite(farProbeVis) ? farProbeVis : 1.0f);
    const float d = std::isfinite(distU) ? std::max(distU, 0.0f) : 0.0f;
    const float cascadeW = 1.0f - Smoothstep(kCascadeEndU - kCascadeFadeU, kCascadeEndU, d);
    const float farV = 1.0f + (fv - 1.0f) * FarFieldWeight(d);
    return farV + (cv - farV) * cascadeW;
}

inline float KastenYoungAirmass(float zenithDeg, Status* outStatus = nullptr) noexcept {
    if (!std::isfinite(zenithDeg) || zenithDeg < 0.0f || zenithDeg >= 90.0f) {
        if (outStatus) { *outStatus = Status::InvalidZenithAngle; }
        return 0.0f;
    }
    if (outStatus) { *outStatus = Status::Ok; }
    const float z = std::clamp(zenithDeg, 0.0f, 89.9f);
    constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
    return 1.0f / (std::cos(z * kDegToRad) + 0.50572f * std::pow(96.07995f - z, -1.6364f));
}

inline TwilightSample EvalKirunaTwilight(float sunElevRad, float viewElevRad, float cosSunAz) noexcept {
    TwilightSample o{};
    if (!std::isfinite(sunElevRad) || !std::isfinite(viewElevRad) || !std::isfinite(cosSunAz)) {
        o.status = Status::InvalidZenithAngle;
        return o;
    }
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
    const float hDeg = std::clamp(sunElevRad * kRadToDeg, -18.0f, 90.0f);
    o.weight = Smoothstep(-12.0f, -6.0f, hDeg) * (1.0f - Smoothstep(2.0f, 8.0f, hDeg));
    o.zenithLumRel = std::pow(10.0f, 0.41f * std::max(std::min(hDeg, 0.0f), -12.0f));
    const float viewElevDeg = std::max(viewElevRad * kRadToDeg, 0.0f);
    const float zDeg = std::clamp(90.0f - viewElevDeg, 0.0f, 85.0f);
    const float X = KastenYoungAirmass(zDeg, nullptr);
    o.horizonZenithRatio = std::clamp(1.0f + 0.30f * (X - 1.0f), 1.0f, kTwilightMaxRatio);
    const float solar = Clamp01(cosSunAz * 0.5f + 0.5f);
    const float r = 1.03f + (1.07f - 1.03f) * solar;
    const float g = 0.97f + (0.98f - 0.97f) * solar;
    const float b = 1.03f + (0.86f - 1.03f) * solar;
    const float y = std::max(0.2126f * r + 0.7152f * g + 0.0722f * b, 1.0e-4f);
    o.tint = {r / y, g / y, b / y};
    const float tRat = Clamp01((o.horizonZenithRatio - 1.0f) / (kTwilightMaxRatio - 1.0f));
    o.gain = 0.90f + (1.06f - 0.90f) * tRat;
    o.status = Status::Ok;
    return o;
}

inline std::array<float, 3> TwilightFogMul(const TwilightSample& tw, float strength) noexcept {
    const float w = Clamp01(tw.weight * strength);
    return {
        1.0f + (tw.tint[0] * tw.gain - 1.0f) * w,
        1.0f + (tw.tint[1] * tw.gain - 1.0f) * w,
        1.0f + (tw.tint[2] * tw.gain - 1.0f) * w
    };
}

} // namespace raw::fsc16
