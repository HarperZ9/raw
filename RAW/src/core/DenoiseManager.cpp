//=============================================================================
//  DenoiseManager.cpp — Shared denoising compute shader library
//
//  Provides 5 pre-compiled compute shaders for edge-preserving denoising:
//    1. Joint Bilateral (float)    — depth+normal edge-stopping Gaussian
//    2. Joint Bilateral (float4)   — same for RGBA textures
//    3. A-Trous Wavelet            — SVGF-style multi-level wavelet filter
//    4. Temporal Accumulation       — reprojection + disocclusion (float)
//    5. Temporal Accumulation       — same for RGBA textures
//
//  This is a utility library — not a pipeline pass.  Other renderers call
//  DispatchBilateral / DispatchATrous / DispatchTemporal within their own
//  SaveCSState / RestoreCSState blocks.
//
//  Current-frame depth linearization uses the pre-computed LinearDepth
//  texture at t31 (from SharedGPUResources).  Temporal shaders retain
//  LinearizeDepth() only for previous-frame depth (no pre-computed buffer).
//  Skyrim uses reversed-Z depth (z=1 near, z=0 far).
//=============================================================================

#include "DenoiseManager.h"
#include "SharedGPUResources.h"
#include "ShaderLoader.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include "GPUResource.h"

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 1: Joint Bilateral Filter CS (single-channel float)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kBilateralCS[] = R"HLSL(
// Joint bilateral filter — single-channel float (depth edge-stopping)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// 5x5 bilateral Gaussian with depth-based edge stopping.
// Uses pre-linearized depth from t31 (SharedGPUResources) for
// the current frame. Gaussian spatial weights are hard-coded
// for a 5x5 kernel (sigma_s ~ 1.5). The depth weight uses an
// exponential falloff: w_d = exp(-|z_c - z_n| / (DepthSigma * z_c)).

cbuffer BilateralCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthSigma;      // Depth edge-stopping sensitivity (0.01-0.1)
    float  NormalSigma;     // Normal edge-stopping sensitivity (0.1-1.0)
    int    KernelRadius;    // 1=3x3, 2=5x5, 3=7x7
    float  pad0;
}

Texture2D<float>   NoisyInput  : register(t0);
Texture2D<float>   DepthTex    : register(t1);
Texture2D<float>   LinearDepth : register(t31);
RWTexture2D<float> Output      : register(u0);

// Spatial Gaussian weight for offset (dx, dy) with sigma ~ 1.5
float GaussianWeight(int dx, int dy)
{
    static const float kSigmaS = 1.5;
    static const float kInvTwoSigma2 = 1.0 / (2.0 * kSigmaS * kSigmaS);
    return exp(-(float)(dx * dx + dy * dy) * kInvTwoSigma2);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= ScreenDims))
        return;

    int2 center = int2(DTid.xy);

    float centerValue = NoisyInput.Load(int3(center, 0));
    float centerDepth = LinearDepth.Load(int3(center, 0));

    // Sky pixels: pass through unfiltered
    if (centerDepth <= 0.0)
    {
        Output[DTid.xy] = centerValue;
        return;
    }

    float weightSum = 0.0;
    float valueSum  = 0.0;

    int radius = clamp(KernelRadius, 1, 3);

    [loop]
    for (int dy = -radius; dy <= radius; dy++)
    {
        [loop]
        for (int dx = -radius; dx <= radius; dx++)
        {
            int2 samplePos = center + int2(dx, dy);

            // Clamp to screen bounds
            samplePos = clamp(samplePos, int2(0, 0), int2(ScreenDims) - int2(1, 1));

            float sampleValue = NoisyInput.Load(int3(samplePos, 0));
            float sampleDepth = LinearDepth.Load(int3(samplePos, 0));

            // Spatial weight (Gaussian)
            float wSpatial = GaussianWeight(dx, dy);

            // Depth edge-stopping weight
            float depthDiff = abs(centerDepth - sampleDepth) / max(centerDepth * DepthSigma, 1e-6);
            float wDepth = exp(-depthDiff);

            float w = wSpatial * wDepth;
            valueSum  += sampleValue * w;
            weightSum += w;
        }
    }

    Output[DTid.xy] = (weightSum > 1e-6) ? (valueSum / weightSum) : centerValue;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 2: Joint Bilateral RGBA CS (R16G16B16A16_FLOAT)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kBilateralRGBACS[] = R"HLSL(
// Joint bilateral filter — RGBA float4 (depth edge-stopping)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Same algorithm as the single-channel variant but operates on
// R16G16B16A16_FLOAT data. Depth edge stopping prevents color
// bleeding across geometric discontinuities.

cbuffer BilateralCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthSigma;
    float  NormalSigma;
    int    KernelRadius;
    float  pad0;
}

Texture2D<float4>   NoisyInput  : register(t0);
Texture2D<float>    DepthTex    : register(t1);
Texture2D<float>    LinearDepth : register(t31);
RWTexture2D<float4> Output      : register(u0);

float GaussianWeight(int dx, int dy)
{
    static const float kSigmaS = 1.5;
    static const float kInvTwoSigma2 = 1.0 / (2.0 * kSigmaS * kSigmaS);
    return exp(-(float)(dx * dx + dy * dy) * kInvTwoSigma2);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= ScreenDims))
        return;

    int2 center = int2(DTid.xy);

    float4 centerValue = NoisyInput.Load(int3(center, 0));
    float  centerDepth = LinearDepth.Load(int3(center, 0));

    // Sky pixels: pass through unfiltered
    if (centerDepth <= 0.0)
    {
        Output[DTid.xy] = centerValue;
        return;
    }

    float  weightSum = 0.0;
    float4 valueSum  = 0.0;

    int radius = clamp(KernelRadius, 1, 3);

    [loop]
    for (int dy = -radius; dy <= radius; dy++)
    {
        [loop]
        for (int dx = -radius; dx <= radius; dx++)
        {
            int2 samplePos = clamp(center + int2(dx, dy),
                                   int2(0, 0), int2(ScreenDims) - int2(1, 1));

            float4 sampleValue = NoisyInput.Load(int3(samplePos, 0));
            float  sampleDepth = LinearDepth.Load(int3(samplePos, 0));

            float wSpatial = GaussianWeight(dx, dy);

            float depthDiff = abs(centerDepth - sampleDepth) / max(centerDepth * DepthSigma, 1e-6);
            float wDepth = exp(-depthDiff);

            float w = wSpatial * wDepth;
            valueSum  += sampleValue * w;
            weightSum += w;
        }
    }

    Output[DTid.xy] = (weightSum > 1e-6) ? (valueSum / weightSum) : centerValue;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 3: A-Trous Wavelet CS (SVGF-style)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kATrousCS[] = R"HLSL(
// A-trous wavelet filter with depth + luminance edge stopping
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Reference: Dammertz et al. 2010, "Edge-Avoiding A-Trous Wavelet
// Transform for fast Global Illumination Filtering"
//
// Uses a 5x5 B3-spline kernel decimated by StepSize. The filter
// is applied in a single pass at the current decimation level;
// the caller (DispatchATrousFull) ping-pongs across levels 0..N
// with StepSize = 1, 2, 4, 8, 16.
//
// Edge-stopping functions:
//   - Depth:     w_z = exp(-|z_c - z_n| / (sigma_z * z_c))
//   - Luminance: w_l = exp(-|l_c - l_n| / sigma_l)  [single-channel]

cbuffer ATrousCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    int    StepSize;         // 1, 2, or 4 (for each a-trous level)
    float  DepthSigma;      // Depth edge-stopping sensitivity
    float  NormalSigma;     // Reserved for normal edge-stopping
    float  LuminanceSigma;  // Luminance edge-stopping sensitivity
}

// B3-spline 1D kernel weights: [1/16, 1/4, 3/8, 1/4, 1/16]
// 2D weight = h[|dx|] * h[|dy|] where index maps: 0->3/8, 1->1/4, 2->1/16
static const float kKernel[3] = { 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };

Texture2D<float>   InputTex    : register(t0);
Texture2D<float>   DepthTex    : register(t1);
Texture2D<float>   LinearDepth : register(t31);
RWTexture2D<float> Output      : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= ScreenDims))
        return;

    int2 center = int2(DTid.xy);

    float centerValue = InputTex.Load(int3(center, 0));
    float centerDepth = LinearDepth.Load(int3(center, 0));

    // Sky pixels: pass through
    if (centerDepth <= 0.0)
    {
        Output[DTid.xy] = centerValue;
        return;
    }

    float weightSum = 0.0;
    float valueSum  = 0.0;

    // 5-tap 2D separable kernel with a-trous stride
    [unroll]
    for (int dy = -2; dy <= 2; dy++)
    {
        [unroll]
        for (int dx = -2; dx <= 2; dx++)
        {
            int2 offset = int2(dx, dy) * StepSize;
            int2 samplePos = center + offset;

            // Clamp to screen
            samplePos = clamp(samplePos, int2(0, 0), int2(ScreenDims) - int2(1, 1));

            float sampleValue = InputTex.Load(int3(samplePos, 0));
            float sampleDepth = LinearDepth.Load(int3(samplePos, 0));

            // B3-spline spatial weight (separable: w = h[|dx|] * h[|dy|])
            float wSpatial = kKernel[abs(dx)] * kKernel[abs(dy)];

            // Depth edge-stopping
            float depthDiff = abs(centerDepth - sampleDepth) / max(centerDepth * DepthSigma, 1e-6);
            float wDepth = exp(-depthDiff);

            // Luminance edge-stopping
            float lumaDiff = abs(centerValue - sampleValue);
            float wLuma = exp(-lumaDiff / max(LuminanceSigma, 1e-6));

            float w = wSpatial * wDepth * wLuma;
            valueSum  += sampleValue * w;
            weightSum += w;
        }
    }

    Output[DTid.xy] = (weightSum > 1e-6) ? (valueSum / weightSum) : centerValue;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 4: Temporal Accumulation CS (single-channel float)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kTemporalCS[] = R"HLSL(
// Temporal accumulation — single-channel float
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Reprojects current pixel to previous frame via InvViewProj + PrevViewProj.
// On successful reprojection, blends current value with history using
// BlendAlpha. On disocclusion (depth mismatch or off-screen), resets to
// current-frame value. Uses neighborhood clamping (3x3 min/max) to
// suppress ghosting from stale history.
//
// Previous-frame depth uses raw reversed-Z (near=1, far=0) and is
// linearized inline since no pre-computed buffer exists for it.

cbuffer TemporalCB : register(b0)
{
    uint2    ScreenDims;
    float    NearZ;
    float    FarZ;
    float4x4 PrevViewProj;   // Previous frame's view-projection matrix
    float4x4 InvViewProj;    // Current frame's inverse view-projection
    float    BlendAlpha;      // Base blend factor (e.g. 0.05 for AO, 0.1 for SSR)
    float    DepthRejectThreshold;  // Relative depth difference for disocclusion
    float    pad0;
    float    pad1;
}

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);
Texture2D<float> CurrentInput : register(t0);  // Current frame noisy input
Texture2D<float> HistoryInput : register(t1);  // Previous frame output
Texture2D<float> DepthTex     : register(t2);  // Current depth (reversed-Z)
Texture2D<float> PrevDepthTex : register(t3);  // Previous frame depth (reversed-Z)
Texture2D<float> LinearDepth  : register(t31); // Pre-computed linearized depth (current frame)
RWTexture2D<float> Output     : register(u0);

// Linearize reversed-Z depth: viewZ = N*F / (N + z*(F-N))
float LinearizeDepth(float z)
{
    return (NearZ * FarZ) / (NearZ + z * (FarZ - NearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= ScreenDims))
        return;

    int2   pixelCoord = int2(DTid.xy);
    float2 screenSize = float2(ScreenDims);
    float2 uv = (float2(pixelCoord) + 0.5) / screenSize;

    float currentValue = CurrentInput.Load(int3(pixelCoord, 0));
    float currentLinZ  = LinearDepth.Load(int3(pixelCoord, 0));

    // Sky pixels: just output current value, no accumulation
    if (currentLinZ <= 0.0)
    {
        Output[DTid.xy] = currentValue;
        return;
    }

    // ── Reproject to previous frame ──────────────────────────────────
    // Reconstruct clip-space position from UV + reversed-Z depth
    float rawDepth = DepthTex.Load(int3(pixelCoord, 0));
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, rawDepth, 1.0);

    // Unproject to world space
    float4 worldPos = mul(clipPos, InvViewProj);
    worldPos /= worldPos.w;

    // Project to previous frame clip space
    float4 prevClip = mul(worldPos, PrevViewProj);
    float2 prevNDC  = prevClip.xy / prevClip.w;
    float2 prevUV   = float2(prevNDC.x * 0.5 + 0.5, 0.5 - prevNDC.y * 0.5);

    // ── Disocclusion detection ───────────────────────────────────────
    bool valid = true;

    // Off-screen rejection
    if (any(prevUV < 0.0) || any(prevUV > 1.0))
        valid = false;

    float historyValue = currentValue;  // fallback

    if (valid)
    {
        // Sample previous-frame depth at reprojected location
        int2 prevPixel = int2(prevUV * screenSize);
        prevPixel = clamp(prevPixel, int2(0, 0), int2(ScreenDims) - int2(1, 1));
        float prevRawZ = PrevDepthTex.Load(int3(prevPixel, 0));
        float prevLinZ = LinearizeDepth(prevRawZ);

        // Depth-based disocclusion: relative difference check
        float depthRatio = abs(currentLinZ - prevLinZ) / max(currentLinZ, 1e-6);
        if (depthRatio > DepthRejectThreshold)
            valid = false;
    }

    if (valid)
    {
        // Bilinear sample history at reprojected UV
        historyValue = HistoryInput.SampleLevel(LinearSampler, prevUV, 0);

        // ── Neighborhood clamping (3x3) ──────────────────────────────
        // Prevents ghosting by clamping history to the local value range
        float minVal = currentValue;
        float maxVal = currentValue;

        [unroll]
        for (int dy = -1; dy <= 1; dy++)
        {
            [unroll]
            for (int dx = -1; dx <= 1; dx++)
            {
                int2 np = clamp(pixelCoord + int2(dx, dy),
                                int2(0, 0), int2(ScreenDims) - int2(1, 1));
                float nv = CurrentInput.Load(int3(np, 0));
                minVal = min(minVal, nv);
                maxVal = max(maxVal, nv);
            }
        }

        historyValue = clamp(historyValue, minVal, maxVal);

        // Blend: small alpha = slow accumulation (more temporal stability)
        Output[DTid.xy] = lerp(historyValue, currentValue, BlendAlpha);
    }
    else
    {
        // Disoccluded: reset to current frame
        Output[DTid.xy] = currentValue;
    }
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 5: Temporal Accumulation RGBA CS (float4)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kTemporalRGBACS[] = R"HLSL(
// Temporal accumulation — RGBA float4
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Same reprojection + disocclusion + neighborhood-clamping logic as
// the single-channel variant, but operates on R16G16B16A16_FLOAT data.
// Neighborhood clamping is per-channel (AABB clamp in RGBA space).
// Previous-frame depth is linearized inline (no pre-computed buffer).

cbuffer TemporalCB : register(b0)
{
    uint2    ScreenDims;
    float    NearZ;
    float    FarZ;
    float4x4 PrevViewProj;
    float4x4 InvViewProj;
    float    BlendAlpha;
    float    DepthRejectThreshold;
    float    pad0;
    float    pad1;
}

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);
Texture2D<float4> CurrentInput : register(t0);
Texture2D<float4> HistoryInput : register(t1);
Texture2D<float>  DepthTex     : register(t2);
Texture2D<float>  PrevDepthTex : register(t3);
Texture2D<float>  LinearDepth  : register(t31); // Pre-computed linearized depth (current frame)
RWTexture2D<float4> Output     : register(u0);

float LinearizeDepth(float z)
{
    return (NearZ * FarZ) / (NearZ + z * (FarZ - NearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= ScreenDims))
        return;

    int2   pixelCoord = int2(DTid.xy);
    float2 screenSize = float2(ScreenDims);
    float2 uv = (float2(pixelCoord) + 0.5) / screenSize;

    float4 currentValue = CurrentInput.Load(int3(pixelCoord, 0));
    float  currentLinZ  = LinearDepth.Load(int3(pixelCoord, 0));

    // Sky pixels: pass through
    if (currentLinZ <= 0.0)
    {
        Output[DTid.xy] = currentValue;
        return;
    }

    // ── Reproject to previous frame ──────────────────────────────────
    float rawDepth = DepthTex.Load(int3(pixelCoord, 0));
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float4 clipPos = float4(ndc, rawDepth, 1.0);

    float4 worldPos = mul(clipPos, InvViewProj);
    worldPos /= worldPos.w;

    float4 prevClip = mul(worldPos, PrevViewProj);
    float2 prevNDC  = prevClip.xy / prevClip.w;
    float2 prevUV   = float2(prevNDC.x * 0.5 + 0.5, 0.5 - prevNDC.y * 0.5);

    // ── Disocclusion detection ───────────────────────────────────────
    bool valid = true;

    if (any(prevUV < 0.0) || any(prevUV > 1.0))
        valid = false;

    float4 historyValue = currentValue;

    if (valid)
    {
        int2 prevPixel = clamp(int2(prevUV * screenSize),
                               int2(0, 0), int2(ScreenDims) - int2(1, 1));
        float prevRawZ = PrevDepthTex.Load(int3(prevPixel, 0));
        float prevLinZ = LinearizeDepth(prevRawZ);

        float depthRatio = abs(currentLinZ - prevLinZ) / max(currentLinZ, 1e-6);
        if (depthRatio > DepthRejectThreshold)
            valid = false;
    }

    if (valid)
    {
        historyValue = HistoryInput.SampleLevel(LinearSampler, prevUV, 0);

        // ── Neighborhood clamping (3x3, per-channel AABB) ────────────
        float4 minVal = currentValue;
        float4 maxVal = currentValue;

        [unroll]
        for (int dy = -1; dy <= 1; dy++)
        {
            [unroll]
            for (int dx = -1; dx <= 1; dx++)
            {
                int2 np = clamp(pixelCoord + int2(dx, dy),
                                int2(0, 0), int2(ScreenDims) - int2(1, 1));
                float4 nv = CurrentInput.Load(int3(np, 0));
                minVal = min(minVal, nv);
                maxVal = max(maxVal, nv);
            }
        }

        historyValue = clamp(historyValue, minVal, maxVal);

        Output[DTid.xy] = lerp(historyValue, currentValue, BlendAlpha);
    }
    else
    {
        Output[DTid.xy] = currentValue;
    }
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════════
//  CB structures — must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════════

struct alignas(16) BilateralCBData
{
    uint32_t screenW;       // +0
    uint32_t screenH;       // +4
    float    nearZ;         // +8
    float    farZ;          // +12
    float    depthSigma;    // +16
    float    normalSigma;   // +20
    int32_t  kernelRadius;  // +24
    float    pad0;          // +28  -> total 32 (2 * 16)
};
static_assert(sizeof(BilateralCBData) == 32, "BilateralCB must be 32 bytes");

struct alignas(16) ATrousCBData
{
    uint32_t screenW;           // +0
    uint32_t screenH;           // +4
    float    nearZ;             // +8
    float    farZ;              // +12
    int32_t  stepSize;          // +16
    float    depthSigma;        // +20
    float    normalSigma;       // +24
    float    luminanceSigma;    // +28  -> total 32 (2 * 16)
};
static_assert(sizeof(ATrousCBData) == 32, "ATrousCB must be 32 bytes");

struct alignas(16) TemporalCBData
{
    uint32_t screenW;               // +0
    uint32_t screenH;               // +4
    float    nearZ;                 // +8
    float    farZ;                  // +12
    float    prevViewProj[16];      // +16  (64 bytes)
    float    invViewProj[16];       // +80  (64 bytes)
    float    blendAlpha;            // +144
    float    depthRejectThreshold;  // +148
    float    pad0;                  // +152
    float    pad1;                  // +156  -> total 160 (10 * 16)
};
static_assert(sizeof(TemporalCBData) == 160, "TemporalCB must be 160 bytes");


// ═══════════════════════════════════════════════════════════════════════════════
//  Singleton — defined inline in DenoiseManager.h
// ═══════════════════════════════════════════════════════════════════════════════


// ═══════════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════════

bool DenoiseManager::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx)
{
    if (m_initialized) return true;
    if (!dev || !ctx) return false;

    m_device  = dev;
    m_context = ctx;

    if (!CompileShaders()) {
        SKSE::log::error("DenoiseManager: shader compilation failed");
        ReleaseResources();
        return false;
    }

    if (!CreateResources()) {
        SKSE::log::error("DenoiseManager: resource creation failed");
        ReleaseResources();
        return false;
    }

    m_initialized = true;
    SKSE::log::info("DenoiseManager: initialized (5 compute shaders, 3 CBs, 2 samplers)");
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════════
//  Compile all 5 shaders
// ═══════════════════════════════════════════════════════════════════════════════

bool DenoiseManager::CompileShaders()
{
    UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
#ifdef _DEBUG
    flags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#endif

    auto CompileCS = [&](const char* name, const char* source,
                         ID3D11ComputeShader** outCS) -> bool
    {
        ID3DBlob* blob = ShaderLoader::Compile(name, source, "main", "cs_5_0", flags);
        if (!blob) {
            SKSE::log::error("DenoiseManager: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("DenoiseManager: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("Denoise_Bilateral",     kBilateralCS,     &m_bilateralCS))     return false;
    if (!CompileCS("Denoise_BilateralRGBA", kBilateralRGBACS, &m_bilateralRGBACS)) return false;
    if (!CompileCS("Denoise_ATrous",        kATrousCS,        &m_atrousCS))        return false;
    if (!CompileCS("Denoise_Temporal",      kTemporalCS,      &m_temporalCS))      return false;
    if (!CompileCS("Denoise_TemporalRGBA",  kTemporalRGBACS,  &m_temporalRGBACS))  return false;

    SKSE::log::info("DenoiseManager: compiled 5/5 compute shaders");
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════════
//  Create shared resources (constant buffers + samplers)
// ═══════════════════════════════════════════════════════════════════════════════

bool DenoiseManager::CreateResources()
{
    HRESULT hr;

    // ── Constant buffers ─────────────────────────────────────────────────────
    if (!CreateCB(m_device, sizeof(BilateralCBData), &m_bilateralCB)) return false;
    if (!CreateCB(m_device, sizeof(ATrousCBData),    &m_atrousCB))    return false;
    if (!CreateCB(m_device, sizeof(TemporalCBData),  &m_temporalCB))  return false;

    // ── Point clamp sampler ───────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_POINT;
        desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MaxLOD         = D3D11_FLOAT32_MAX;

        hr = m_device->CreateSamplerState(&desc, &m_pointSampler);
        if (FAILED(hr)) {
            SKSE::log::error("DenoiseManager: failed to create point sampler (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
    }

    // ── Linear clamp sampler ──────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter         = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW       = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MaxLOD         = D3D11_FLOAT32_MAX;

        hr = m_device->CreateSamplerState(&desc, &m_linearSampler);
        if (FAILED(hr)) {
            SKSE::log::error("DenoiseManager: failed to create linear sampler (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════════
//  SetMatrices — called each frame before temporal dispatches
// ═══════════════════════════════════════════════════════════════════════════════

void DenoiseManager::SetMatrices(const float* prevViewProj, const float* invViewProj)
{
    if (prevViewProj) std::memcpy(m_prevViewProj, prevViewProj, sizeof(float) * 16);
    if (invViewProj)  std::memcpy(m_invViewProj,  invViewProj,  sizeof(float) * 16);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  UpdateCB — Map/WRITE_DISCARD helper
// ═══════════════════════════════════════════════════════════════════════════════

bool DenoiseManager::UpdateCB(ID3D11Buffer* cb, const void* data, uint32_t size)
{
    if (!cb || !data || !m_context) return false;
    return UploadCB(m_context, cb, data, size);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  DispatchBilateral — single-channel float
// ═══════════════════════════════════════════════════════════════════════════════

void DenoiseManager::DispatchBilateral(
    ID3D11ShaderResourceView*  noisyInput,
    ID3D11ShaderResourceView*  depthSRV,
    ID3D11UnorderedAccessView* output,
    uint32_t width, uint32_t height,
    float depthSigma, float normalSigma, int kernelRadius)
{
    if (!m_initialized || !noisyInput || !depthSRV || !output) return;

    // Update constant buffer
    BilateralCBData cb = {};
    cb.screenW      = width;
    cb.screenH      = height;
    cb.nearZ        = m_nearZ;
    cb.farZ         = m_farZ;
    cb.depthSigma   = depthSigma;
    cb.normalSigma  = normalSigma;
    cb.kernelRadius = kernelRadius;
    if (!UpdateCB(m_bilateralCB, &cb, sizeof(cb))) return;

    // Bind resources
    ID3D11ShaderResourceView* srvs[] = { noisyInput, depthSRV };
    m_context->CSSetShaderResources(0, 2, srvs);
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);
    m_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
    m_context->CSSetConstantBuffers(0, 1, &m_bilateralCB);
    m_context->CSSetShader(m_bilateralCS, nullptr, 0);

    // Dispatch
    UINT groupsX = (width  + 7) / 8;
    UINT groupsY = (height + 7) / 8;
    m_context->Dispatch(groupsX, groupsY, 1);

    // Clear bindings (prevent SRV/UAV conflicts)
    ID3D11ShaderResourceView*  nullSRVs[2] = {};
    ID3D11UnorderedAccessView* nullUAV[1]  = {};
    m_context->CSSetShaderResources(0, 2, nullSRVs);
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }
    m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  DispatchBilateralRGBA — R16G16B16A16_FLOAT
// ═══════════════════════════════════════════════════════════════════════════════

void DenoiseManager::DispatchBilateralRGBA(
    ID3D11ShaderResourceView*  noisyInput,
    ID3D11ShaderResourceView*  depthSRV,
    ID3D11UnorderedAccessView* output,
    uint32_t width, uint32_t height,
    float depthSigma, int kernelRadius)
{
    if (!m_initialized || !noisyInput || !depthSRV || !output) return;

    // Reuse the same CB layout — normalSigma unused in RGBA variant
    BilateralCBData cb = {};
    cb.screenW      = width;
    cb.screenH      = height;
    cb.nearZ        = m_nearZ;
    cb.farZ         = m_farZ;
    cb.depthSigma   = depthSigma;
    cb.normalSigma  = 0.0f;
    cb.kernelRadius = kernelRadius;
    if (!UpdateCB(m_bilateralCB, &cb, sizeof(cb))) return;

    ID3D11ShaderResourceView* srvs[] = { noisyInput, depthSRV };
    m_context->CSSetShaderResources(0, 2, srvs);
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);
    m_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
    m_context->CSSetConstantBuffers(0, 1, &m_bilateralCB);
    m_context->CSSetShader(m_bilateralRGBACS, nullptr, 0);

    UINT groupsX = (width  + 7) / 8;
    UINT groupsY = (height + 7) / 8;
    m_context->Dispatch(groupsX, groupsY, 1);

    ID3D11ShaderResourceView*  nullSRVs[2] = {};
    ID3D11UnorderedAccessView* nullUAV[1]  = {};
    m_context->CSSetShaderResources(0, 2, nullSRVs);
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }
    m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  DispatchATrous — single pass at given step size
// ═══════════════════════════════════════════════════════════════════════════════

void DenoiseManager::DispatchATrous(
    ID3D11ShaderResourceView*  input,
    ID3D11ShaderResourceView*  depthSRV,
    ID3D11UnorderedAccessView* output,
    uint32_t width, uint32_t height,
    int stepSize, float depthSigma, float luminanceSigma)
{
    if (!m_initialized || !input || !depthSRV || !output) return;

    ATrousCBData cb = {};
    cb.screenW         = width;
    cb.screenH         = height;
    cb.nearZ           = m_nearZ;
    cb.farZ            = m_farZ;
    cb.stepSize        = stepSize;
    cb.depthSigma      = depthSigma;
    cb.normalSigma     = 0.0f;
    cb.luminanceSigma  = luminanceSigma;
    if (!UpdateCB(m_atrousCB, &cb, sizeof(cb))) return;

    ID3D11ShaderResourceView* srvs[] = { input, depthSRV };
    m_context->CSSetShaderResources(0, 2, srvs);
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);
    m_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
    m_context->CSSetConstantBuffers(0, 1, &m_atrousCB);
    m_context->CSSetShader(m_atrousCS, nullptr, 0);

    UINT groupsX = (width  + 7) / 8;
    UINT groupsY = (height + 7) / 8;
    m_context->Dispatch(groupsX, groupsY, 1);

    ID3D11ShaderResourceView*  nullSRVs[2] = {};
    ID3D11UnorderedAccessView* nullUAV[1]  = {};
    m_context->CSSetShaderResources(0, 2, nullSRVs);
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }
    m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  DispatchATrousFull — 3-level a-trous with ping-pong
// ═══════════════════════════════════════════════════════════════════════════════

void DenoiseManager::DispatchATrousFull(
    ID3D11ShaderResourceView*  input,
    ID3D11ShaderResourceView*  depthSRV,
    ID3D11UnorderedAccessView* pingUAV,
    ID3D11ShaderResourceView*  pingSRV,
    ID3D11UnorderedAccessView* pongUAV,
    ID3D11ShaderResourceView*  pongSRV,
    uint32_t width, uint32_t height)
{
    if (!m_initialized) return;

    // Level 0: input -> pingUAV (step=1)
    DispatchATrous(input,   depthSRV, pingUAV, width, height, 1);

    // Level 1: pingSRV -> pongUAV (step=2)
    DispatchATrous(pingSRV, depthSRV, pongUAV, width, height, 2);

    // Level 2: pongSRV -> pingUAV (step=4)
    DispatchATrous(pongSRV, depthSRV, pingUAV, width, height, 4);

    // Final result is in pingUAV (read via pingSRV)
}


// ═══════════════════════════════════════════════════════════════════════════════
//  DispatchTemporal — single-channel float
// ═══════════════════════════════════════════════════════════════════════════════

void DenoiseManager::DispatchTemporal(
    ID3D11ShaderResourceView*  current,
    ID3D11ShaderResourceView*  history,
    ID3D11ShaderResourceView*  depthSRV,
    ID3D11ShaderResourceView*  prevDepthSRV,
    ID3D11UnorderedAccessView* output,
    uint32_t width, uint32_t height,
    float blendAlpha, float depthRejectThreshold)
{
    if (!m_initialized || !current || !history || !depthSRV || !prevDepthSRV || !output)
        return;

    // Update constant buffer with current matrices + parameters
    TemporalCBData cb = {};
    cb.screenW              = width;
    cb.screenH              = height;
    cb.nearZ                = m_nearZ;
    cb.farZ                 = m_farZ;
    std::memcpy(cb.prevViewProj, m_prevViewProj, sizeof(float) * 16);
    std::memcpy(cb.invViewProj,  m_invViewProj,  sizeof(float) * 16);
    cb.blendAlpha           = blendAlpha;
    cb.depthRejectThreshold = depthRejectThreshold;
    if (!UpdateCB(m_temporalCB, &cb, sizeof(cb))) return;

    // Bind samplers (s0 = point, s1 = linear)
    ID3D11SamplerState* samplers[] = { m_pointSampler, m_linearSampler };
    m_context->CSSetSamplers(0, 2, samplers);

    // Bind SRVs: t0=current, t1=history, t2=depth, t3=prevDepth, t31=linearDepth
    ID3D11ShaderResourceView* srvs[] = { current, history, depthSRV, prevDepthSRV };
    m_context->CSSetShaderResources(0, 4, srvs);
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);
    m_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
    m_context->CSSetConstantBuffers(0, 1, &m_temporalCB);
    m_context->CSSetShader(m_temporalCS, nullptr, 0);

    UINT groupsX = (width  + 7) / 8;
    UINT groupsY = (height + 7) / 8;
    m_context->Dispatch(groupsX, groupsY, 1);

    // Clear bindings
    ID3D11ShaderResourceView*  nullSRVs[4] = {};
    ID3D11UnorderedAccessView* nullUAV[1]  = {};
    m_context->CSSetShaderResources(0, 4, nullSRVs);
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }
    m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  DispatchTemporalRGBA — R16G16B16A16_FLOAT
// ═══════════════════════════════════════════════════════════════════════════════

void DenoiseManager::DispatchTemporalRGBA(
    ID3D11ShaderResourceView*  current,
    ID3D11ShaderResourceView*  history,
    ID3D11ShaderResourceView*  depthSRV,
    ID3D11ShaderResourceView*  prevDepthSRV,
    ID3D11UnorderedAccessView* output,
    uint32_t width, uint32_t height,
    float blendAlpha, float depthRejectThreshold)
{
    if (!m_initialized || !current || !history || !depthSRV || !prevDepthSRV || !output)
        return;

    TemporalCBData cb = {};
    cb.screenW              = width;
    cb.screenH              = height;
    cb.nearZ                = m_nearZ;
    cb.farZ                 = m_farZ;
    std::memcpy(cb.prevViewProj, m_prevViewProj, sizeof(float) * 16);
    std::memcpy(cb.invViewProj,  m_invViewProj,  sizeof(float) * 16);
    cb.blendAlpha           = blendAlpha;
    cb.depthRejectThreshold = depthRejectThreshold;
    if (!UpdateCB(m_temporalCB, &cb, sizeof(cb))) return;

    ID3D11SamplerState* samplers[] = { m_pointSampler, m_linearSampler };
    m_context->CSSetSamplers(0, 2, samplers);

    ID3D11ShaderResourceView* srvs[] = { current, history, depthSRV, prevDepthSRV };
    m_context->CSSetShaderResources(0, 4, srvs);
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);
    m_context->CSSetUnorderedAccessViews(0, 1, &output, nullptr);
    m_context->CSSetConstantBuffers(0, 1, &m_temporalCB);
    m_context->CSSetShader(m_temporalRGBACS, nullptr, 0);

    UINT groupsX = (width  + 7) / 8;
    UINT groupsY = (height + 7) / 8;
    m_context->Dispatch(groupsX, groupsY, 1);

    ID3D11ShaderResourceView*  nullSRVs[4] = {};
    ID3D11UnorderedAccessView* nullUAV[1]  = {};
    m_context->CSSetShaderResources(0, 4, nullSRVs);
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        m_context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }
    m_context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
}


// ═══════════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════════

void DenoiseManager::Shutdown()
{
    if (!m_initialized) return;

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("DenoiseManager: shut down");
}


void DenoiseManager::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_bilateralCS);
    SafeRelease(m_bilateralRGBACS);
    SafeRelease(m_atrousCS);
    SafeRelease(m_temporalCS);
    SafeRelease(m_temporalRGBACS);

    SafeRelease(m_bilateralCB);
    SafeRelease(m_atrousCB);
    SafeRelease(m_temporalCB);

    SafeRelease(m_pointSampler);
    SafeRelease(m_linearSampler);

    m_device  = nullptr;
    m_context = nullptr;
}

} // namespace SB
