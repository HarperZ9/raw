//=============================================================================
//  GTAORenderer.cpp — VB-SSGI (Visibility Bitmask Screen-Space GI)
//
//  Upgraded from GTAO (Jimenez 2016) to visibility bitmask approach
//  (Activision 2019, "Practical Real-Time Strategies for Accurate Indirect
//  Occlusion"). Computes both ambient occlusion and short-range indirect
//  bounce lighting via 32-bit visibility bitmask encoding.
//
//  Dispatch flow (per frame, PreENB stage):
//    1. Copy backbuffer → m_sceneColorTex (bounce light source)
//    2. Dispatch VB-SSGI CS:  visibility bitmask AO + bounce gather → raw float4
//    3. Dispatch Spatial CS:  5x5 bilateral filter → denoised float4
//    4. Dispatch Temporal CS:  ping-pong history accumulation → final float4
//    5. Register SRV at t20 (.rgb = bounce, .a = AO)
//=============================================================================

#include "GTAORenderer.h"
#include "HiZPyramid.h"
#include "SRVInjector.h"
#include "SharedGPUResources.h"
#include "D3D11Hook.h"
#include "SceneData.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

namespace SB
{

// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 1: VB-SSGI CS (full-res)
//  Visibility bitmask AO + indirect bounce light gathering
// ═══════════════════════════════════════════════════════════════════════════

static const char kGTAOCS[] = R"HLSL(
// VB-SSGI CS — Visibility Bitmask Screen-Space Global Illumination
// Based on "Practical Real-Time Strategies for Accurate Indirect Occlusion"
// (Activision / Jimenez et al. 2019).
//
// Replaces horizon-angle tracking with a 32-bit bitmask encoding the
// visibility of each elevation slice over the hemisphere. Enables:
//   - More accurate multi-bounce AO
//   - Short-range indirect bounce by gathering scene color from visible samples

cbuffer GTAOCB : register(b0)
{
    float4x4 ProjMatrix;           // Projection matrix (row-major)
    uint2    ScreenDims;           // Full-res dimensions
    float    NearZ;
    float    FarZ;
    float    AORadius;             // World-space sample radius
    float    AOIntensity;          // Output multiplier
    int      NumDirections;        // Horizon search directions (2-8)
    int      NumSteps;             // Steps per direction (2-16)
    uint     FrameIndex;           // For temporal jitter rotation
    float    BounceIntensity;      // Bounce light multiplier
    uint     BounceEnabled;        // Enable bounce color gathering
    float    FPDepthThreshold;    // View-space Z below which pixels are first-person (skip)
};

Texture2D<float> DepthTex : register(t0);        // Hi-Z mip 0 (or raw depth)
Texture2D<float4> SceneColorTex : register(t1);  // Scene color for bounce gathering
Texture2D<float4> BlueNoise : register(t30);     // 128x128 R2 quasi-random blue noise
Texture2D<float> LinearDepth : register(t31);    // Pre-computed linearized depth

RWTexture2D<float4> Output : register(u0);       // (bounce.rgb, ao.a)

// ── Constants ────────────────────────────────────────────────────────────
static const float PI = 3.14159265359;
static const float HALF_PI = 1.57079632679;

// ── RGB → YCoCg conversion (lossless, invertible) ───────────────────────
// Y  = luminance, Co = orange-blue chrominance, Cg = green-purple chrominance
// Output .rgb stores (Y, Co, Cg) — better for luminance-aware denoising
float3 RGBtoYCoCg(float3 rgb)
{
    return float3(
         0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,
         0.5  * rgb.r                - 0.5  * rgb.b,
        -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b
    );
}

// ── Visibility bitmask constants ─────────────────────────────────────────
static const uint BITMASK_BITS = 32;

// ── Screen UV -> view-space position ─────────────────────────────────────
float3 UVToViewPos(float2 uv, float linearZ)
{
    // Unproject using inverse of projection
    // NDC = uv * 2 - 1 (with Y flipped for DX)
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float3 viewPos;
    viewPos.x = ndc.x * linearZ / ProjMatrix[0][0];
    viewPos.y = ndc.y * linearZ / ProjMatrix[1][1];
    viewPos.z = linearZ;
    return viewPos;
}

// ── Reconstruct view-space normal from depth gradients ───────────────────
float3 ReconstructNormal(int2 coord, float2 texelSize)
{
    float dC = LinearDepth.Load(int3(coord, 0));
    float dR = LinearDepth.Load(int3(coord + int2(1, 0), 0));
    float dU = LinearDepth.Load(int3(coord + int2(0, -1), 0));

    float2 uv  = (float2(coord) + 0.5) * texelSize;
    float2 uvR = (float2(coord) + float2(1.5, 0.5)) * texelSize;
    float2 uvU = (float2(coord) + float2(0.5, -0.5)) * texelSize;

    float3 posC = UVToViewPos(uv,  dC);
    float3 posR = UVToViewPos(uvR, dR);
    float3 posU = UVToViewPos(uvU, dU);

    float3 ddx = posR - posC;
    float3 ddy = posU - posC;

    return normalize(cross(ddy, ddx));
}

// ── Blue noise temporal jitter ───────────────────────────────────────────
// R2 quasi-random blue noise (128x128, 4 channels) replaces hash-based noise.
// Golden ratio temporal offset: frac(bn + frame * φ) produces non-repeating
// quasi-random sequence with excellent spatial distribution (Roberts 2018).
static const float GOLDEN_RATIO = 0.6180339887;

float BlueNoiseAt(uint2 coord, uint frame, uint channel)
{
    float4 bn = BlueNoise.Load(int3(int2(coord % 128), 0));
    float val = (channel == 0) ? bn.x : (channel == 1) ? bn.y :
                (channel == 2) ? bn.z : bn.w;
    return frac(val + float(frame) * GOLDEN_RATIO);
}

// ── Map elevation angle to bitmask bit index ─────────────────────────────
uint elevationToBit(float elevAngle)
{
    // Map elevation [0, PI/2] to bit index [0, 31]
    float normalized = saturate(elevAngle / HALF_PI);
    return min(uint(normalized * float(BITMASK_BITS)), BITMASK_BITS - 1);
}

// ── Compute AO from visibility bitmask ───────────────────────────────────
float bitmaskAO(uint visibilityMask)
{
    // Count visible bits — more visible = less occluded
    // AO = 1 - (visible_bits / total_bits)
    uint popcount = countbits(visibilityMask);
    return 1.0 - float(popcount) / float(BITMASK_BITS);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);
    float2 texelSize = 1.0 / float2(ScreenDims);
    float2 uv = (float2(coord) + 0.5) * texelSize;

    float rawDepth = DepthTex.Load(int3(coord, 0));

    // Skip sky (reversed-Z: sky near 0.0)
    if (rawDepth < 0.0001)
    {
        Output[coord] = float4(0, 0, 0, 1);
        return;
    }

    // Skip first-person geometry (hands/weapons) — prevents close-range AO artifacts.
    // Threshold ~16 view-space units (between ENB's 11.76 and CS's 18.0).
    float linearZ = LinearDepth.Load(int3(coord, 0));
    {
        if (linearZ < FPDepthThreshold)
        {
            Output[coord] = float4(0, 0, 0, 1);  // no occlusion, no bounce
            return;
        }
    }

    float3 viewPos = UVToViewPos(uv, linearZ);
    float3 viewNormal = ReconstructNormal(coord, texelSize);

    // Project AO radius from world-space to pixel-space.
    // ProjMatrix[0][0] maps world→NDC (range [-1,1]), multiply by
    // half screen width to convert NDC→pixels.
    float screenRadius = AORadius * ProjMatrix[0][0] / viewPos.z
                         * float(ScreenDims.x) * 0.5;
    // Clamp to avoid excessive steps for close geometry
    screenRadius = clamp(screenRadius, 2.0, 256.0);

    // Temporal rotation offset — blue noise provides spatially-uniform
    // jitter pattern that converges faster under temporal accumulation
    float rotationOffset = BlueNoiseAt(DTid.xy, FrameIndex, 0) * PI;

    float totalAO = 0.0;
    float3 totalBounce = float3(0, 0, 0);
    float bounceWeight = 0.0;

    for (int dir = 0; dir < NumDirections; dir++)
    {
        // Angle for this direction (evenly distributed + jittered)
        float angle = (PI / float(NumDirections)) * (float(dir) + 0.5) + rotationOffset;
        float2 direction = float2(cos(angle), sin(angle));

        // Step size in pixels
        float stepSize = screenRadius / float(NumSteps);

        // Visibility bitmask for this direction — all visible initially
        uint visibilityMask = 0xFFFFFFFF;

        for (int step = 1; step <= NumSteps; step++)
        {
            // Sample position
            float2 offset = direction * (float(step) * stepSize);
            int2 sampleCoord = coord + int2(offset);

            // Clamp to screen
            sampleCoord = clamp(sampleCoord, 0, int2(ScreenDims) - 1);

            float sampleRawDepth = DepthTex.Load(int3(sampleCoord, 0));
            if (sampleRawDepth < 0.0001)
                continue;  // Skip sky samples

            float sampleLinZ = LinearDepth.Load(int3(sampleCoord, 0));
            float2 sampleUV = (float2(sampleCoord) + 0.5) * texelSize;
            float3 samplePos = UVToViewPos(sampleUV, sampleLinZ);

            // Vector from current pixel to sample
            float3 horizonVec = samplePos - viewPos;
            float horizonDist = length(horizonVec);

            // Skip samples outside world-space radius
            if (horizonDist > AORadius * 2.0)
                continue;

            float3 horizonDir = normalize(horizonVec);
            float elevation = asin(saturate(dot(horizonDir, viewNormal)));

            // Distance falloff
            float falloff = saturate(1.0 - horizonDist / (AORadius * 2.0));

            // Mark occluded: all bits from 0 to elevation are blocked
            uint occludedBit = elevationToBit(elevation);
            uint occlusionMask = (1u << (occludedBit + 1)) - 1;

            // Apply falloff: only mark occlusion for nearby samples
            if (falloff > 0.3)
                visibilityMask &= ~occlusionMask;

            // Gather bounce color from visible samples (in YCoCg space)
            if (BounceEnabled && falloff > 0.2)
            {
                float3 sampleColor = SceneColorTex.Load(int3(sampleCoord, 0)).rgb;
                float3 sampleYCoCg = RGBtoYCoCg(sampleColor);
                float NdotH = saturate(dot(horizonDir, viewNormal));
                float bounceW = NdotH * falloff * falloff;
                totalBounce += sampleYCoCg * bounceW;
                bounceWeight += bounceW;
            }
        }

        // Per-direction AO from bitmask
        totalAO += bitmaskAO(visibilityMask);
    }

    totalAO /= float(NumDirections);
    float ao = saturate(1.0 - totalAO * AOIntensity);

    // Normalize bounce
    float3 bounce = float3(0, 0, 0);
    if (bounceWeight > 0.001)
        bounce = totalBounce / bounceWeight * BounceIntensity;

    Output[coord] = float4(bounce, ao);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: Spatial Denoise CS (full-res, 5x5 bilateral)
//  Updated for float4 (bounce.rgb, ao.a) input/output
// ═══════════════════════════════════════════════════════════════════════════

static const char kSpatialCS[] = R"HLSL(
// Spatial denoise — 5x5 bilateral filter with depth-gradient edge stopping.
// Preserves edges at depth discontinuities while smoothing noise from the
// limited-direction visibility bitmask search.
// Operates on float4: .rgb = bounce light, .a = ambient occlusion.
// Edge stopping is driven by the AO channel (.a) and depth.

cbuffer SpatialCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthThreshold;     // Edge-stopping depth sensitivity
    float3 pad0;
};

Texture2D<float4> AOInput  : register(t0);   // (bounce.rgb, ao.a)
Texture2D<float>  DepthTex : register(t1);   // Depth buffer
Texture2D<float> LinearDepth : register(t31); // Pre-computed linearized depth

RWTexture2D<float4> AOOutput : register(u0);  // Spatially filtered (bounce.rgb, ao.a)

// Gaussian kernel weights for 5x5
static const float kGaussianWeights[3] = { 0.375, 0.25, 0.0625 };

float GaussianWeight(int offset)
{
    return kGaussianWeights[abs(offset)];
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float  centerDepth = LinearDepth.Load(int3(coord, 0));
    float4 centerVal   = AOInput.Load(int3(coord, 0));

    float4 totalVal    = float4(0, 0, 0, 0);
    float  totalWeight = 0.0;

    for (int dy = -2; dy <= 2; dy++)
    {
        for (int dx = -2; dx <= 2; dx++)
        {
            int2 sampleCoord = coord + int2(dx, dy);
            sampleCoord = clamp(sampleCoord, 0, int2(ScreenDims) - 1);

            float  sampleDepth = LinearDepth.Load(int3(sampleCoord, 0));
            float4 sampleVal   = AOInput.Load(int3(sampleCoord, 0));

            // Spatial weight (separable Gaussian)
            float spatialW = GaussianWeight(dx) * GaussianWeight(dy);

            // Depth edge stopping
            float depthDiff = abs(sampleDepth - centerDepth) / max(centerDepth, 0.01);
            float depthW = exp(-depthDiff / max(DepthThreshold, 0.001));

            // AO similarity weight — reject samples with very different AO
            float aoDiff = abs(sampleVal.a - centerVal.a);
            float aoW = exp(-aoDiff * 10.0);

            float w = spatialW * depthW * aoW;
            totalVal += sampleVal * w;
            totalWeight += w;
        }
    }

    AOOutput[coord] = totalVal / max(totalWeight, 0.0001);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 3: Temporal Accumulation CS (full-res, ping-pong)
//  Updated for float4 (bounce.rgb, ao.a) input/output
// ═══════════════════════════════════════════════════════════════════════════

static const char kTemporalCS[] = R"HLSL(
// Temporal accumulation — exponential blend with depth-aware history rejection.
// Uses ping-pong buffers: reads from previous frame, writes to current frame.
// Operates on float4: .rgb = bounce light, .a = ambient occlusion.
// History rejection is driven by the AO channel (.a), blend applies to all channels.

cbuffer TemporalCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  TemporalAlpha;       // Blend weight for new frame (0.05-0.2)
    float  DepthRejectThreshold; // Depth difference threshold for history rejection
    uint   FrameIndex;          // For first-frame detection
    float  pad0;
};

Texture2D<float4> AOCurrent  : register(t0);  // Spatially denoised (this frame)
Texture2D<float4> AOHistory  : register(t1);  // Previous frame's accumulated
Texture2D<float>  DepthTex   : register(t2);  // Current depth
Texture2D<float> LinearDepth : register(t31); // Pre-computed linearized depth

RWTexture2D<float4> AOOutput : register(u0);  // Output accumulated (bounce.rgb, ao.a)

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float4 current = AOCurrent.Load(int3(coord, 0));
    float4 history = AOHistory.Load(int3(coord, 0));

    // First few frames: skip history entirely (prevents black ghosting from
    // zeroed/uninitialized history buffers)
    if (FrameIndex < 3)
    {
        AOOutput[coord] = current;
        return;
    }

    // History rejection based on AO channel difference
    float aoDiff = abs(current.a - history.a);
    float reject = smoothstep(0.05, 0.3, aoDiff);

    float bounceDiff = abs(current.x - history.x);
    float bounceReject = smoothstep(0.05, 0.3, bounceDiff);
    reject = max(reject, bounceReject * 0.5);

    // Adaptive alpha: more history weight when stable, less when changing
    float alpha = lerp(TemporalAlpha, 0.5, reject);

    // Exponential blend — all 4 channels
    float4 result = lerp(history, current, alpha);

    AOOutput[coord] = result;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures — must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) GTAOCBData
{
    float    projMatrix[16];     // 64 bytes — row-major 4x4
    uint32_t screenW;            // +64
    uint32_t screenH;            // +68
    float    nearZ;              // +72
    float    farZ;               // +76
    float    aoRadius;           // +80
    float    aoIntensity;        // +84
    int32_t  numDirections;      // +88
    int32_t  numSteps;           // +92
    uint32_t frameIndex;         // +96
    float    bounceIntensity;    // +100
    uint32_t bounceEnabled;      // +104
    float    fpDepthThreshold;   // +108 — first-person depth mask (view-space units)
};
static_assert(sizeof(GTAOCBData) == 112, "GTAOCB must be 112 bytes");

struct alignas(16) SpatialCBData
{
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    depthThreshold;
    float    pad[3];
};
static_assert(sizeof(SpatialCBData) == 32, "SpatialCB must be 32 bytes");

struct alignas(16) TemporalCBData
{
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    temporalAlpha;
    float    depthRejectThreshold;
    uint32_t frameIndex;
    float    pad;
};
static_assert(sizeof(TemporalCBData) == 32, "TemporalCB must be 32 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool GTAORenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm = ComputeManager::Get();
    auto& pl = RenderPipeline::Get();

    if (!cm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("GTAORenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("GTAORenderer: failed to get backbuffer (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc;
    backTex->GetDesc(&bbDesc);
    backTex->Release();

    m_screenW = bbDesc.Width;
    m_screenH = bbDesc.Height;

    if (!CompileShaders()) return false;
    if (!CreateResources()) {
        ReleaseResources();
        return false;
    }

    // Register as PostGeometry pipeline pass — dispatched mid-frame by
    // PhaseDispatcher when the game transitions out of GeometryMain phase.
    // Live depth + SceneMatrices available via UpdateFromNiCamera().
    m_pipelineHandle = pl.AddPass({
        .name     = "VB-SSGI",
        .stage    = PipelineStage::PostGeometry,
        .priority = 15,  // After HiZ (10), before SSGI (20)
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_firstFrame  = true;
    m_frameIndex  = 0;

    SKSE::log::info("GTAORenderer: VB-SSGI initialized ({}x{}, dirs={}, steps={}, radius={:.1f}, bounce={}, t{})",
                    m_screenW, m_screenH, m_numDirections, m_numSteps, m_aoRadius, m_bounceEnabled, kSRVSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool GTAORenderer::CompileShaders()
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
                SKSE::log::error("GTAORenderer: {} compile failed: {}", name,
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
            SKSE::log::error("GTAORenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("VBSSGIMain",    kGTAOCS,     &m_gtaoCS))     return false;
    if (!CompileCS("VBSSGISpatial", kSpatialCS,  &m_spatialCS))  return false;
    if (!CompileCS("VBSSGITemporal",kTemporalCS, &m_temporalCS)) return false;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool GTAORenderer::CreateResources()
{
    HRESULT hr;

    // Helper: create full-res R16G16B16A16_FLOAT texture + SRV + UAV
    auto CreateFloat4Texture = [&](const char* name,
                               ID3D11Texture2D** outTex,
                               ID3D11ShaderResourceView** outSRV,
                               ID3D11UnorderedAccessView** outUAV) -> bool
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_screenW;
        desc.Height     = m_screenH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, outTex);
        if (FAILED(hr)) {
            SKSE::log::error("GTAORenderer: failed to create {} texture (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(*outTex, &srvDesc, outSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(*outTex, &uavDesc, outUAV);
        if (FAILED(hr)) return false;

        return true;
    };

    // Pass 1 output: raw VB-SSGI (bounce.rgb, ao.a)
    if (!CreateFloat4Texture("aoRaw", &m_aoRaw, &m_aoRawSRV, &m_aoRawUAV))
        return false;

    // Pass 2 output: spatially denoised
    if (!CreateFloat4Texture("aoSpatial", &m_aoSpatial, &m_aoSpatialSRV, &m_aoSpatialUAV))
        return false;

    // Pass 3: ping-pong history buffers
    if (!CreateFloat4Texture("aoHistory0", &m_aoHistory[0], &m_aoHistorySRV[0], &m_aoHistoryUAV[0]))
        return false;
    if (!CreateFloat4Texture("aoHistory1", &m_aoHistory[1], &m_aoHistorySRV[1], &m_aoHistoryUAV[1]))
        return false;

    // Scene color copy texture (full-res, matches backbuffer format)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_screenW;
        desc.Height     = m_screenH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R8G8B8A8_UNORM;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_sceneColorTex);
        if (FAILED(hr)) {
            SKSE::log::error("GTAORenderer: failed to create scene color texture (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R8G8B8A8_UNORM;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_sceneColorTex, &srvDesc, &m_sceneColorSRV);
        if (FAILED(hr)) {
            SKSE::log::error("GTAORenderer: failed to create scene color SRV (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
    }

    // Constant buffers
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;

        desc.ByteWidth = sizeof(GTAOCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_gtaoCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(SpatialCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_spatialCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(TemporalCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_temporalCB);
        if (FAILED(hr)) return false;
    }

    // Point sampler (for Load-style reads via sampler when needed)
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter        = D3D11_FILTER_MIN_MAG_MIP_POINT;
        desc.AddressU      = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV      = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW      = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy = 1;
        desc.ComparisonFunc= D3D11_COMPARISON_NEVER;
        desc.MaxLOD        = D3D11_FLOAT32_MAX;
        hr = m_device->CreateSamplerState(&desc, &m_pointSampler);
        if (FAILED(hr)) return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV from currently-bound DSV
// ═══════════════════════════════════════════════════════════════════════════

bool GTAORenderer::AcquireDepthSRV()
{
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    ID3D11DepthStencilView* dsv = nullptr;
    m_context->OMGetRenderTargets(0, nullptr, &dsv);
    if (!dsv) return false;

    ID3D11Resource* res = nullptr;
    dsv->GetResource(&res);
    dsv->Release();
    if (!res) return false;

    ID3D11Texture2D* depthTex = nullptr;
    HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                     reinterpret_cast<void**>(&depthTex));
    res->Release();
    if (FAILED(hr) || !depthTex) return false;

    D3D11_TEXTURE2D_DESC desc;
    depthTex->GetDesc(&desc);

    if (!(desc.BindFlags & D3D11_BIND_SHADER_RESOURCE)) {
        depthTex->Release();
        return false;
    }

    DXGI_FORMAT srvFormat;
    switch (desc.Format) {
        case DXGI_FORMAT_R32_TYPELESS:      srvFormat = DXGI_FORMAT_R32_FLOAT;              break;
        case DXGI_FORMAT_R24G8_TYPELESS:    srvFormat = DXGI_FORMAT_R24_UNORM_X8_TYPELESS;  break;
        case DXGI_FORMAT_R32G8X24_TYPELESS: srvFormat = DXGI_FORMAT_R32_FLOAT_X8X24_TYPELESS; break;
        case DXGI_FORMAT_R16_TYPELESS:      srvFormat = DXGI_FORMAT_R16_UNORM;              break;
        case DXGI_FORMAT_R32_FLOAT:         srvFormat = DXGI_FORMAT_R32_FLOAT;              break;
        case DXGI_FORMAT_R16_UNORM:         srvFormat = DXGI_FORMAT_R16_UNORM;              break;
        default:
            depthTex->Release();
            return false;
    }

    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format                    = srvFormat;
    srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MostDetailedMip = 0;
    srvDesc.Texture2D.MipLevels       = 1;

    hr = m_device->CreateShaderResourceView(depthTex, &srvDesc, &m_depthSRV);
    depthTex->Release();

    return SUCCEEDED(hr) && m_depthSRV != nullptr;
}


// ═══════════════════════════════════════════════════════════════════════════
//  GetOutputSRV — returns the most recently accumulated output SRV
//  (.rgb = bounce light, .a = ambient occlusion)
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* GTAORenderer::GetOutputSRV() const
{
    if (!m_initialized) return nullptr;
    // After ExecutePass, pingPongIdx was swapped, so the "just written"
    // accumulated output is at (1 - current pingPongIdx).
    return m_aoHistorySRV[1 - m_pingPongIdx];
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution (called by RenderPipeline at PreENB stage)
// ═══════════════════════════════════════════════════════════════════════════

void GTAORenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Gate: scene matrices must be valid (camera data verified non-garbage)
    auto& scene = SceneMatrices::Get();
    if (!scene.IsValid()) {
        static int s_logCount = 0;
        if (s_logCount++ < 5) SKSE::log::warn("VB-SSGI: SceneMatrices invalid, skipping");
        return;
    }

    auto& cm = ComputeManager::Get();

    // ── Acquire depth ───────────────────────────────────────────────────
    // Prefer proxy-captured depth (always available during Present hook)
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    if (!depthSRV) {
        if (!AcquireDepthSRV()) {
            static int s_logCount2 = 0;
            if (s_logCount2++ < 5) SKSE::log::warn("VB-SSGI: depth acquisition failed (proxy={}, OM=fail)", (void*)D3D11Hook::GetGameDepthSRV());
            return;
        }
        depthSRV = m_depthSRV;
    }
    {
        static int s_logCount3 = 0;
        if (s_logCount3++ < 10) {
            SKSE::log::info("VB-SSGI: ExecutePass — rawDepthSRV={}, near={:.1f}, far={:.1f}, gameSceneDSV={}",
                (void*)depthSRV, scene.NearClip(), scene.FarClip(), (void*)ctx.gameSceneDSV);
        }
    }

    // HiZ is now built at PostGeometry:1 (before this pass at :15),
    // so it's fresh during both mid-frame and Present-time dispatch.
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV()) {
            depthSRV = hiz.GetSRV();
        }
    }

    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    // ── Copy scene color texture (for bounce gathering) ─────────────────
    // During mid-frame dispatch the backbuffer does NOT contain the scene;
    // the game renders to an internal RT exposed via ctx.gameSceneRTV.
    // Extract the underlying texture from that RTV first, falling back to
    // the legacy backbuffer path only when gameSceneRTV is null (PrePresent).
    if (m_bounceEnabled && m_sceneColorTex) {
        ID3D11Texture2D* sceneSrc   = nullptr;
        bool             needRelease = false;

        // --- Mid-frame path: read the game's current render target -------
        if (ctx.gameSceneRTV) {
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                HRESULT hr = res->QueryInterface(__uuidof(ID3D11Texture2D),
                                                  reinterpret_cast<void**>(&sceneSrc));
                res->Release();
                if (FAILED(hr)) sceneSrc = nullptr;
                // sceneSrc obtained via QI — must be released by us
                needRelease = (sceneSrc != nullptr);
            }
        }

        // --- Fallback: pre-UI snapshot or swap chain backbuffer ----------
        if (!sceneSrc) {
            sceneSrc = D3D11Hook::GetPreUISceneTex();
            if (sceneSrc) {
                needRelease = false;  // owned by D3D11Hook, do not release
            } else {
                auto* sc = D3D11Hook::GetSwapChain();
                if (sc) {
                    sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                  reinterpret_cast<void**>(&sceneSrc));
                    needRelease = (sceneSrc != nullptr);
                }
            }
        }

        if (sceneSrc) {
            D3D11_TEXTURE2D_DESC srcDesc;
            sceneSrc->GetDesc(&srcDesc);

            // Ensure copy texture format matches source (game scene RT is HDR float,
            // but m_sceneColorTex was created as R8G8B8A8_UNORM at init time).
            D3D11_TEXTURE2D_DESC copyDesc;
            m_sceneColorTex->GetDesc(&copyDesc);
            if (srcDesc.Format != copyDesc.Format ||
                srcDesc.Width  != copyDesc.Width  ||
                srcDesc.Height != copyDesc.Height)
            {
                m_sceneColorSRV->Release(); m_sceneColorSRV = nullptr;
                m_sceneColorTex->Release(); m_sceneColorTex = nullptr;

                D3D11_TEXTURE2D_DESC newDesc = srcDesc;
                newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
                newDesc.Usage          = D3D11_USAGE_DEFAULT;
                newDesc.CPUAccessFlags = 0;
                newDesc.MiscFlags      = 0;
                newDesc.MipLevels      = 1;
                newDesc.ArraySize      = 1;
                newDesc.SampleDesc     = {1, 0};
                m_device->CreateTexture2D(&newDesc, nullptr, &m_sceneColorTex);

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                    = newDesc.Format;
                srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MostDetailedMip = 0;
                srvDesc.Texture2D.MipLevels       = 1;
                m_device->CreateShaderResourceView(m_sceneColorTex, &srvDesc, &m_sceneColorSRV);

                SKSE::log::info("VB-SSGI: scene color texture recreated — {}x{} fmt={}",
                    srcDesc.Width, srcDesc.Height, static_cast<uint32_t>(srcDesc.Format));
            }

            ctx.context->CopyResource(m_sceneColorTex, sceneSrc);
            if (needRelease) {
                sceneSrc->Release();
            }
        }
    }

    // First frame: clear history with float4(0,0,0,1) — no bounce, full visibility
    if (m_firstFrame) {
        float clearVal[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        ctx.context->ClearUnorderedAccessViewFloat(m_aoHistoryUAV[0], clearVal);
        ctx.context->ClearUnorderedAccessViewFloat(m_aoHistoryUAV[1], clearVal);
        m_firstFrame = false;
        m_pingPongIdx = 0;
    }

    cm.SaveCSState();

    // Bind pre-computed linear depth at CS t31
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: VB-SSGI — visibility bitmask AO + bounce gather
    // ═════════════════════════════════════════════════════════════════════
    {
        GTAOCBData cb = {};
        std::memcpy(cb.projMatrix, scene.ProjMatrix(), sizeof(float) * 16);
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.aoRadius       = m_aoRadius;
        cb.aoIntensity    = m_aoIntensity;
        cb.numDirections  = m_numDirections;
        cb.numSteps       = m_numSteps;
        cb.frameIndex     = m_frameIndex;
        cb.bounceIntensity = m_bounceIntensity;
        cb.bounceEnabled  = m_bounceEnabled ? 1u : 0u;
        cb.fpDepthThreshold = 16.0f;  // ~16 view-space units (between ENB 11.76, CS 18.0)

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_gtaoCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_gtaoCB, 0);
        }

        // t0 = depth, t1 = scene color, t30 = blue noise
        ID3D11ShaderResourceView* srvs[] = { depthSRV, m_sceneColorSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        auto* bnSRV = SharedGPUResources::Get().GetBlueNoiseSRV();
        if (bnSRV) ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &bnSRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_aoRawUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_gtaoCB);
        ctx.context->CSSetShader(m_gtaoCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Clear bindings
        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        { ID3D11ShaderResourceView* n = nullptr; ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &n); }
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2: Spatial denoise — 5x5 bilateral filter on float4
    // ═════════════════════════════════════════════════════════════════════
    {
        SpatialCBData cb = {};
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.depthThreshold = 0.05f;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_spatialCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_spatialCB, 0);
        }

        // t0 = raw VB-SSGI output, t1 = depth
        ID3D11ShaderResourceView* srvs[] = { m_aoRawSRV, depthSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_aoSpatialUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_spatialCB);
        ctx.context->CSSetShader(m_spatialCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 3: Temporal accumulation (ping-pong) on float4
    // ═════════════════════════════════════════════════════════════════════
    {
        int readIdx  = 1 - m_pingPongIdx;
        int writeIdx = m_pingPongIdx;

        TemporalCBData cb = {};
        cb.screenW              = m_screenW;
        cb.screenH              = m_screenH;
        cb.nearZ                = nearZ;
        cb.farZ                 = farZ;
        cb.temporalAlpha        = 0.2f;   // was 0.1 — faster convergence, less ghosting
        cb.depthRejectThreshold = 0.1f;
        cb.frameIndex           = m_frameIndex;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_temporalCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_temporalCB, 0);
        }

        // t0 = spatially denoised (current), t1 = history (previous), t2 = depth
        ID3D11ShaderResourceView* srvs[] = { m_aoSpatialSRV, m_aoHistorySRV[readIdx], depthSRV };
        ctx.context->CSSetShaderResources(0, 3, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_aoHistoryUAV[writeIdx], nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_temporalCB);
        ctx.context->CSSetShader(m_temporalCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[3] = {};
        ctx.context->CSSetShaderResources(0, 3, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

        // Swap ping-pong
        m_pingPongIdx = readIdx;
    }

    // Unbind linear depth
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // Register output SRV at t20
    SRVInjector::Get().RegisterSRV(kSRVSlot, GetOutputSRV());

    // Release per-frame depth SRV
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    m_frameIndex++;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void GTAORenderer::Shutdown()
{
    if (!m_initialized) return;

    SRVInjector::Get().UnregisterSRV(kSRVSlot);

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("GTAORenderer: VB-SSGI shut down");
}


void GTAORenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_gtaoCS);
    SafeRelease(m_spatialCS);
    SafeRelease(m_temporalCS);

    SafeRelease(m_aoRaw);
    SafeRelease(m_aoRawSRV);
    SafeRelease(m_aoRawUAV);
    SafeRelease(m_gtaoCB);

    SafeRelease(m_aoSpatial);
    SafeRelease(m_aoSpatialSRV);
    SafeRelease(m_aoSpatialUAV);
    SafeRelease(m_spatialCB);

    SafeRelease(m_aoHistory[0]);
    SafeRelease(m_aoHistorySRV[0]);
    SafeRelease(m_aoHistoryUAV[0]);
    SafeRelease(m_aoHistory[1]);
    SafeRelease(m_aoHistorySRV[1]);
    SafeRelease(m_aoHistoryUAV[1]);
    SafeRelease(m_temporalCB);

    SafeRelease(m_sceneColorTex);
    SafeRelease(m_sceneColorSRV);

    SafeRelease(m_pointSampler);
    SafeRelease(m_depthSRV);
}

} // namespace SB
