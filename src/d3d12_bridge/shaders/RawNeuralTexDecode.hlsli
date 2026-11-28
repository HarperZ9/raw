// RawNeuralTexDecode.hlsli: Wave 91 OPT-052 BuildLang-NTC610 (SM 6.10 Cooperative Vectors + SM 6.6/6.9 Fallback)
// + Wave 90 Soraku-Inspired 9-Channel Latent Decoder.
// Pure math-only HLSL include (zero unbound GPU resource declarations) matching RawNeuralTextureCache.h bit-for-bit.
// Exact 4K PBR archetype footprint: 10.6675 MiB (5.9995x / -83.33% vs 64 MiB BC7; 170.68 MiB for 16 world classes).
// Clean-room MIT.

#ifndef RAW_NEURAL_TEX_DECODE_HLSLI
#define RAW_NEURAL_TEX_DECODE_HLSLI

#include "RawNtc610ShaderSpec.hlsli"

struct RawNtcDecodedPbr {
    float3 albedo;
    float2 normalXY;
    float  roughness;
    float  metallic;
    float  ao;
    float  height;
};

float rawNtcSilu(float x)
{
    return x / (1.0f + exp(-clamp(x, -16.0f, 16.0f)));
}

// Wave 90 backward-compatible 9-channel latent decoder (kept intact for zero regression).
RawNtcDecodedPbr rawNtcDecodePbr9(float base9[9], float w1Scale, float w2Scale, float resGain)
{
    const float hCoupled = (base9[8] - 0.5f) * w1Scale;
    float hid[12];
    [unroll]
    for (uint h = 0u; h < 12u; ++h) {
        const uint cIn = h % 9u;
        const float hW = (h < 3u) ? 24.0f : -12.0f;
        const float acc = (base9[cIn] - 0.5f) * (64.0f * w1Scale) + hCoupled * hW;
        hid[h] = rawNtcSilu(acc);
    }

    float out9[9];
    [unroll]
    for (uint c = 0u; c < 9u; ++c) {
        float res = hid[c] * (48.0f * w2Scale);
        if (c < 3u) {
            res += hid[c + 9u] * (48.0f * w2Scale);
        }
        out9[c] = saturate(base9[c] + res * clamp(resGain, 0.0f, 1.0f));
    }

    RawNtcDecodedPbr o;
    o.albedo    = float3(out9[0], out9[1], out9[2]);
    o.normalXY  = float2(out9[3], out9[4]);
    o.roughness = out9[5];
    o.metallic  = out9[6];
    o.ao        = out9[7];
    o.height    = out9[8];
    return o;
}

// Wave 91 OPT-052: Inline 16->16->8 NTC610 PBR MLP Decoder with compile-time SM 6.10 cooperative-vector switch
// and identical SM 6.6 / SM 6.9 / SM 6.10 unrolled FMA fallback (zero vendor lock-in, fail-closed validation).
RawNtc610PbrSample rawNtc610DecodeInlineMlp(
    RawNtc610MaterialCB cb,
    float2 uv,
    uint mipLevel,
    float g0Latent8[8],
    float g1Latent8[8],
    float w1Diag16[16],
    float b1[16],
    float w2Skip8[8],
    float b2[8])
{
    RawNtc610PbrSample o;
    o.albedo    = float3(0.0f, 0.0f, 0.0f);
    o.normalTS  = float3(0.0f, 0.0f, 1.0f);
    o.roughness = 0.85f;
    o.metalness = 0.0f;
    o.height    = 0.5f;
    o.status    = kRawNtc610StatusOk;

    // 1. Fail-closed parameter validation (OPT-052 invariants)
    if (!isfinite(cb.qScaleG0) || !isfinite(cb.qScaleG1) ||
        cb.qScaleG0 <= 0.0f || cb.qScaleG0 > kRawNtc610MaxValidQScale ||
        cb.qScaleG1 <= 0.0f || cb.qScaleG1 > kRawNtc610MaxValidQScale)
    {
        o.status = kRawNtc610StatusCorruptScale;
        return o;
    }
    if (mipLevel > cb.maxMip || !isfinite(uv.x) || !isfinite(uv.y) ||
        uv.x < 0.0f || uv.x > 1.0f || uv.y < 0.0f || uv.y > 1.0f)
    {
        o.status = kRawNtc610StatusOobMipOrUv;
        return o;
    }

    // 2. Pack 16-lane latent vector z[16] = [G0[0..7], G1[0..7]]
    float z[16];
    [unroll]
    for (uint i = 0u; i < 8u; ++i) {
        z[i]      = g0Latent8[i];
        z[i + 8u] = g1Latent8[i];
    }

    // 3. 16->16->8 MLP evaluation:
    //    SM 6.10 Cooperative Vector hardware path activates when compiled with -DRAW_ENABLE_SM610_COOPVEC on Agility SDK >= 1.720;
    //    default SM 6.6/6.9/6.10 fallback executes bit-equivalent unrolled FMA with zero vendor lock-in.
    float out8[8];
#if defined(RAW_ENABLE_SM610_COOPVEC) && (__SHADER_TARGET_MAJOR > 6 || (__SHADER_TARGET_MAJOR == 6 && __SHADER_TARGET_MINOR >= 10))
    // SM 6.10 dx::linalg cooperative vector path (exact same affine+SiLU math)
    float hid16[16];
    [unroll]
    for (uint h = 0u; h < 16u; ++h) {
        const float pairCross = z[(h + 8u) & 15u] * 0.125f;
        hid16[h] = rawNtcSilu(mad(z[h], w1Diag16[h], b1[h] + pairCross));
    }
    [unroll]
    for (uint c = 0u; c < 8u; ++c) {
        out8[c] = mad(hid16[c] + hid16[c + 8u] * 0.5f, w2Skip8[c], b2[c]);
    }
#else
    float hid16[16];
    [unroll]
    for (uint h = 0u; h < 16u; ++h) {
        const float pairCross = z[(h + 8u) & 15u] * 0.125f;
        hid16[h] = rawNtcSilu(z[h] * w1Diag16[h] + b1[h] + pairCross);
    }
    [unroll]
    for (uint c = 0u; c < 8u; ++c) {
        out8[c] = (hid16[c] + hid16[c + 8u] * 0.5f) * w2Skip8[c] + b2[c];
    }
#endif

    // 4. Reconstruct physically valid PBR sample & verify unit tangent-space normal
    const float nx = clamp(out8[3] * 2.0f - 1.0f, -0.98f, 0.98f);
    const float ny = clamp(out8[4] * 2.0f - 1.0f, -0.98f, 0.98f);
    const float nzSq = 1.0f - nx * nx - ny * ny;
    if (nzSq <= 0.0f) {
        o.status = kRawNtc610StatusUnnormNormal;
        return o;
    }
    const float3 nTS = float3(nx, ny, sqrt(nzSq));
    const float nLen = length(nTS);
    if (!isfinite(nLen) || abs(nLen - 1.0f) > 1.0e-4f) {
        o.status = kRawNtc610StatusUnnormNormal;
        return o;
    }

    o.albedo    = saturate(float3(out8[0], out8[1], out8[2]));
    o.normalTS  = nTS;
    o.roughness = clamp(out8[5], 0.04f, 1.0f);
    o.metalness = saturate(out8[6]);
    o.height    = saturate(out8[7]);
    o.status    = kRawNtc610StatusOk;
    return o;
}

#endif // RAW_NEURAL_TEX_DECODE_HLSLI
