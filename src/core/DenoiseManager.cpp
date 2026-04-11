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

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 1: Joint Bilateral Filter CS (single-channel float)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kBilateralCS[] = R"HLSL(
// Joint Bilateral Filter — depth + normal edge-stopping Gaussian blur.
// Generic depth-aware spatial filter for single-channel noisy inputs
// (AO masks, shadow masks, sky visibility, etc.)
//
// Spatial kernel: Gaussian with sigma proportional to KernelRadius.
// Edge-stopping: exponential depth falloff + optional normal weighting.

cbuffer BilateralCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthSigma;      // Depth edge-stopping sensitivity (0.01-0.1)
    float  NormalSigma;     // Normal edge-stopping sensitivity (0.1-1.0)
    int    KernelRadius;    // 1=3x3, 2=5x5, 3=7x7
    float  pad0;
};

Texture2D<float>   NoisyInput  : register(t0);
Texture2D<float>   DepthTex    : register(t1);
Texture2D<float>   LinearDepth : register(t31);
RWTexture2D<float> Output      : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float centerDepth = LinearDepth.Load(int3(coord, 0));
    float centerVal   = NoisyInput.Load(int3(coord, 0));

    // Skip sky pixels (reversed-Z: sky near 0.0)
    if (DepthTex.Load(int3(coord, 0)) < 0.0001)
    {
        Output[coord] = centerVal;
        return;
    }

    float totalWeight = 0.0;
    float totalVal    = 0.0;

    float spatialSigmaSq = 2.0 * float(KernelRadius) * float(KernelRadius);

    for (int dy = -KernelRadius; dy <= KernelRadius; dy++)
    {
        for (int dx = -KernelRadius; dx <= KernelRadius; dx++)
        {
            int2 sampleCoord = coord + int2(dx, dy);
            sampleCoord = clamp(sampleCoord, 0, int2(ScreenDims) - 1);

            float sampleDepth = LinearDepth.Load(int3(sampleCoord, 0));
            float sampleVal   = NoisyInput.Load(int3(sampleCoord, 0));

            // Spatial weight — Gaussian falloff with distance
            float distSq   = float(dx * dx + dy * dy);
            float spatialW = exp(-distSq / max(spatialSigmaSq, 0.001));

            // Depth edge-stopping — suppress across depth discontinuities
            float depthDiff = abs(sampleDepth - centerDepth) / max(centerDepth, 0.01);
            float depthW    = exp(-depthDiff / max(DepthSigma, 0.0001));

            float w = spatialW * depthW;
            totalVal    += sampleVal * w;
            totalWeight += w;
        }
    }

    Output[coord] = totalVal / max(totalWeight, 1e-6);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 2: Joint Bilateral RGBA CS (R16G16B16A16_FLOAT)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kBilateralRGBACS[] = R"HLSL(
// Joint Bilateral Filter — RGBA variant for SSR, SSGI, and other float4 effects.
// Same edge-stopping logic as the scalar version, operating on all 4 channels.

cbuffer BilateralCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthSigma;
    float  NormalSigma;
    int    KernelRadius;
    float  pad0;
};

Texture2D<float4>   NoisyInput  : register(t0);
Texture2D<float>    DepthTex    : register(t1);
Texture2D<float>    LinearDepth : register(t31);
RWTexture2D<float4> Output      : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float centerDepth = LinearDepth.Load(int3(coord, 0));
    float4 centerVal  = NoisyInput.Load(int3(coord, 0));

    if (DepthTex.Load(int3(coord, 0)) < 0.0001)
    {
        Output[coord] = centerVal;
        return;
    }

    float  totalWeight = 0.0;
    float4 totalVal    = 0.0;

    float spatialSigmaSq = 2.0 * float(KernelRadius) * float(KernelRadius);

    for (int dy = -KernelRadius; dy <= KernelRadius; dy++)
    {
        for (int dx = -KernelRadius; dx <= KernelRadius; dx++)
        {
            int2 sampleCoord = coord + int2(dx, dy);
            sampleCoord = clamp(sampleCoord, 0, int2(ScreenDims) - 1);

            float sampleDepth = LinearDepth.Load(int3(sampleCoord, 0));
            float4 sampleVal  = NoisyInput.Load(int3(sampleCoord, 0));

            float distSq   = float(dx * dx + dy * dy);
            float spatialW = exp(-distSq / max(spatialSigmaSq, 0.001));

            float depthDiff = abs(sampleDepth - centerDepth) / max(centerDepth, 0.01);
            float depthW    = exp(-depthDiff / max(DepthSigma, 0.0001));

            float w = spatialW * depthW;
            totalVal    += sampleVal * w;
            totalWeight += w;
        }
    }

    Output[coord] = totalVal / max(totalWeight, 1e-6);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 3: A-Trous Wavelet CS (SVGF-style)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kATrousCS[] = R"HLSL(
// A-Trous Wavelet Filter — SVGF-style 5x5 B3-spline wavelet with
// exponentially increasing step size for multi-scale denoising.
//
// Dispatch 3 times with StepSize = 1, 2, 4 for a full 3-level filter.
// Each level doubles the effective filter radius without increasing
// the per-pixel sample count (always 5x5 = 25 taps).
//
// Edge-stopping: depth + luminance-based weighting.

cbuffer ATrousCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    int    StepSize;         // 1, 2, or 4 (for each a-trous level)
    float  DepthSigma;      // Depth edge-stopping sensitivity
    float  NormalSigma;     // Reserved for normal edge-stopping
    float  LuminanceSigma;  // Luminance edge-stopping sensitivity
};

Texture2D<float>   InputTex    : register(t0);
Texture2D<float>   DepthTex    : register(t1);
Texture2D<float>   LinearDepth : register(t31);
RWTexture2D<float> Output      : register(u0);

// 5-tap B3-spline kernel weights: { 1/16, 1/4, 3/8, 1/4, 1/16 }
// Indexed by absolute offset from center: [0]=3/8, [1]=1/4, [2]=1/16
static const float kKernel[3] = { 3.0 / 8.0, 1.0 / 4.0, 1.0 / 16.0 };

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float centerDepth = LinearDepth.Load(int3(coord, 0));
    float centerVal   = InputTex.Load(int3(coord, 0));

    if (DepthTex.Load(int3(coord, 0)) < 0.0001)
    {
        Output[coord] = centerVal;
        return;
    }

    float totalWeight = 0.0;
    float totalVal    = 0.0;

    for (int dy = -2; dy <= 2; dy++)
    {
        for (int dx = -2; dx <= 2; dx++)
        {
            int2 offset      = int2(dx, dy) * StepSize;
            int2 sampleCoord = coord + offset;
            sampleCoord = clamp(sampleCoord, 0, int2(ScreenDims) - 1);

            float sampleDepth = LinearDepth.Load(int3(sampleCoord, 0));
            float sampleVal   = InputTex.Load(int3(sampleCoord, 0));

            // B3-spline spatial weight (separable)
            float h = kKernel[abs(dx)] * kKernel[abs(dy)];

            // Depth edge-stopping
            float depthDiff = abs(sampleDepth - centerDepth) / max(centerDepth, 0.01);
            float depthW    = exp(-depthDiff / max(DepthSigma, 0.0001));

            // Luminance edge-stopping
            float lumDiff = abs(sampleVal - centerVal);
            float lumW    = exp(-lumDiff / max(LuminanceSigma, 0.0001));

            float w = h * depthW * lumW;
            totalVal    += sampleVal * w;
            totalWeight += w;
        }
    }

    Output[coord] = totalVal / max(totalWeight, 1e-6);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 4: Temporal Accumulation CS (single-channel float)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kTemporalCS[] = R"HLSL(
// Temporal Accumulation — Motion-aware temporal blending with
// reprojection and disocclusion detection for single-channel inputs.
//
// Reprojects each pixel to the previous frame via inverse view-projection,
// then checks for disocclusion (depth mismatch).  On disocclusion, the
// pixel snaps to the current frame value (alpha = 1).  Otherwise, it
// blends with history using the configured BlendAlpha.

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
};

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);

Texture2D<float> CurrentInput : register(t0);  // Current frame noisy input
Texture2D<float> HistoryInput : register(t1);  // Previous frame output
Texture2D<float> DepthTex     : register(t2);  // Current depth
Texture2D<float> PrevDepthTex : register(t3);  // Previous frame depth
Texture2D<float> LinearDepth  : register(t31); // Pre-computed linearized depth (current frame)
RWTexture2D<float> Output     : register(u0);

// Kept for previous-frame depth linearization (no pre-computed buffer for prev frame)
float LinearizeDepth(float z)
{
    return NearZ * FarZ / (NearZ + z * (FarZ - NearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2  coord = int2(DTid.xy);
    float2 uv   = (float2(DTid.xy) + 0.5) / float2(ScreenDims);

    float rawDepth = DepthTex.Load(int3(coord, 0));

    // Sky pass-through (reversed-Z: sky near 0.0)
    if (rawDepth < 0.0001)
    {
        Output[coord] = CurrentInput.Load(int3(coord, 0));
        return;
    }

    // ── Reproject to previous frame UV ──────────────────────────────────
    // NDC: x [-1,1] left-to-right, y [-1,1] bottom-to-top
    float4 clipPos = float4(uv * 2.0 - 1.0, rawDepth, 1.0);
    clipPos.y = -clipPos.y;  // Flip Y (UV top-left vs NDC bottom-left)

    float4 worldPos = mul(InvViewProj, clipPos);
    worldPos /= worldPos.w;

    float4 prevClip = mul(PrevViewProj, worldPos);
    float2 prevUV   = prevClip.xy / prevClip.w * 0.5 + 0.5;
    prevUV.y = 1.0 - prevUV.y;  // Flip Y back to UV space

    // ── Disocclusion detection ──────────────────────────────────────────
    bool outOfBounds = (prevUV.x < 0.0 || prevUV.x > 1.0 ||
                        prevUV.y < 0.0 || prevUV.y > 1.0);

    float prevRawDepth = PrevDepthTex.SampleLevel(LinearSampler, prevUV, 0);
    float curLinear    = LinearDepth.Load(int3(coord, 0));
    float prevLinear   = LinearizeDepth(prevRawDepth);

    bool disoccluded = outOfBounds ||
                       (abs(curLinear - prevLinear) > DepthRejectThreshold * curLinear);

    // ── Blend current + history ─────────────────────────────────────────
    float current = CurrentInput.Load(int3(coord, 0));
    float history = HistoryInput.SampleLevel(LinearSampler, prevUV, 0);

    float alpha = disoccluded ? 1.0 : BlendAlpha;
    Output[coord] = lerp(history, current, alpha);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Shader 5: Temporal Accumulation RGBA CS (float4)
// ═══════════════════════════════════════════════════════════════════════════════

static const char kTemporalRGBACS[] = R"HLSL(
// Temporal Accumulation RGBA — Same reprojection + disocclusion logic
// as the scalar variant, operating on R16G16B16A16_FLOAT textures.
// Used for SSR color, SSGI radiance, and other multi-channel effects.

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
};

SamplerState PointSampler  : register(s0);
SamplerState LinearSampler : register(s1);

Texture2D<float4> CurrentInput : register(t0);
Texture2D<float4> HistoryInput : register(t1);
Texture2D<float>  DepthTex     : register(t2);
Texture2D<float>  PrevDepthTex : register(t3);
Texture2D<float>  LinearDepth  : register(t31); // Pre-computed linearized depth (current frame)
RWTexture2D<float4> Output     : register(u0);

// Kept for previous-frame depth linearization (no pre-computed buffer for prev frame)
float LinearizeDepth(float z)
{
    return NearZ * FarZ / (NearZ + z * (FarZ - NearZ));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2   coord = int2(DTid.xy);
    float2 uv    = (float2(DTid.xy) + 0.5) / float2(ScreenDims);

    float rawDepth = DepthTex.Load(int3(coord, 0));

    if (rawDepth < 0.0001)
    {
        Output[coord] = CurrentInput.Load(int3(coord, 0));
        return;
    }

    // Reproject to previous frame
    float4 clipPos = float4(uv * 2.0 - 1.0, rawDepth, 1.0);
    clipPos.y = -clipPos.y;

    float4 worldPos = mul(InvViewProj, clipPos);
    worldPos /= worldPos.w;

    float4 prevClip = mul(PrevViewProj, worldPos);
    float2 prevUV   = prevClip.xy / prevClip.w * 0.5 + 0.5;
    prevUV.y = 1.0 - prevUV.y;

    // Disocclusion
    bool outOfBounds = (prevUV.x < 0.0 || prevUV.x > 1.0 ||
                        prevUV.y < 0.0 || prevUV.y > 1.0);

    float prevRawDepth = PrevDepthTex.SampleLevel(LinearSampler, prevUV, 0);
    float curLinear    = LinearDepth.Load(int3(coord, 0));
    float prevLinear   = LinearizeDepth(prevRawDepth);

    bool disoccluded = outOfBounds ||
                       (abs(curLinear - prevLinear) > DepthRejectThreshold * curLinear);

    // Blend
    float4 current = CurrentInput.Load(int3(coord, 0));
    float4 history = HistoryInput.SampleLevel(LinearSampler, prevUV, 0);

    float alpha = disoccluded ? 1.0 : BlendAlpha;
    Output[coord] = lerp(history, current, alpha);
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
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3DCompile(source, strlen(source), name,
                                nullptr, nullptr, "main", "cs_5_0",
                                flags, 0, &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("DenoiseManager: {} compile failed: {}", name,
                                 static_cast<const char*>(err->GetBufferPointer()));
                err->Release();
            }
            return false;
        }
        if (err) err->Release();

        hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
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

    if (!CompileCS("DenoiseBilateral",     kBilateralCS,     &m_bilateralCS))     return false;
    if (!CompileCS("DenoiseBilateralRGBA", kBilateralRGBACS, &m_bilateralRGBACS)) return false;
    if (!CompileCS("DenoiseATrous",        kATrousCS,        &m_atrousCS))        return false;
    if (!CompileCS("DenoiseTemporal",      kTemporalCS,      &m_temporalCS))      return false;
    if (!CompileCS("DenoiseTemporalRGBA",  kTemporalRGBACS,  &m_temporalRGBACS))  return false;

    SKSE::log::info("DenoiseManager: compiled 5/5 compute shaders");
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════════
//  Create shared resources (constant buffers + samplers)
// ═══════════════════════════════════════════════════════════════════════════════

bool DenoiseManager::CreateResources()
{
    HRESULT hr;

    // ── Constant buffers (DYNAMIC, CPU write) ─────────────────────────────
    auto CreateCB = [&](UINT sizeBytes, ID3D11Buffer** outCB) -> bool
    {
        // CB size must be 16-byte aligned
        sizeBytes = (sizeBytes + 15) & ~15u;

        D3D11_BUFFER_DESC desc = {};
        desc.ByteWidth      = sizeBytes;
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        hr = m_device->CreateBuffer(&desc, nullptr, outCB);
        return SUCCEEDED(hr) && *outCB != nullptr;
    };

    if (!CreateCB(sizeof(BilateralCBData), &m_bilateralCB)) return false;
    if (!CreateCB(sizeof(ATrousCBData),    &m_atrousCB))    return false;
    if (!CreateCB(sizeof(TemporalCBData),  &m_temporalCB))  return false;

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

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = m_context->Map(cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;

    std::memcpy(mapped.pData, data, size);
    m_context->Unmap(cb, 0);
    return true;
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
