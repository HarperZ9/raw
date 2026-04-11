//=============================================================================
//  SkylightingRenderer.cpp — Hemisphere-based sky visibility
//
//  Dispatch flow (per frame, PostGeometry stage):
//    0. Dispatch ProbeUpdate CS:  fill 128×128×64 SH2 voxel grid
//    1. Dispatch ProbeQuery CS:   per-pixel trilinear sample → raw sky
//    2. Dispatch Spatial CS:      5×5 bilateral filter → denoised sky visibility
//    3. Dispatch Temporal CS:     ping-pong accumulation → stable output
//    4. Register SRV at t29
//
//  Algorithm:
//    A camera-centered 3D voxel grid stores SH2 sky visibility coefficients.
//    Pass 0 fills this grid: each voxel marches upward through the depth buffer
//    in 6 hemisphere directions to determine which sky directions are visible.
//    Pass 1 reconstructs each pixel's world position and trilinearly samples
//    the probe grid for sky visibility — dramatically cheaper than per-pixel
//    ray marching while producing smooth, temporally stable results.
//
//  This replaces Community Shaders' "Skylighting" feature.
//=============================================================================

#include "SkylightingRenderer.h"
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
//  Embedded HLSL — Pass 0: ProbeUpdate CS (3D voxel grid)
// ═══════════════════════════════════════════════════════════════════════════

static const char kProbeUpdateCS[] = R"HLSL(
// ProbeUpdate CS — Fill 3D sky probe grid with SH2 sky visibility.
// Camera-centered grid: each voxel marches upward through depth buffer
// to determine which sky directions are visible from that world position.

cbuffer ProbeUpdateCB : register(b0)
{
    float4x4 ViewProj;
    float3   GridCenter;    // Camera pos snapped to voxel grid
    float    ProbeRangeXY;
    float    ProbeRangeZ;
    uint     ProbeResXY;
    uint     ProbeResZ;
    uint     FrameIndex;
    uint     ScreenW;
    uint     ScreenH;
    float    pad0, pad1;
};

Texture2D<float> LinearDepth : register(t31);
RWTexture3D<float4> ProbeGrid : register(u0);

// SH2 basis constants
static const float SH_C0 = 0.282095;
static const float SH_C1 = 0.488603;

// Hemisphere directions — 6 directions covering upper hemisphere
// Balanced set: up, 4 cardinal tilted, 1 more up
static const float3 kDirs[6] = {
    float3( 0.000,  0.000,  1.000),  // straight up
    float3( 0.707,  0.000,  0.707),  // NE tilted
    float3(-0.707,  0.000,  0.707),  // NW tilted
    float3( 0.000,  0.707,  0.707),  // forward tilted
    float3( 0.000, -0.707,  0.707),  // back tilted
    float3( 0.577,  0.577,  0.577),  // diagonal
};
static const int NUM_DIRS = 6;
static const int NUM_STEPS = 12;

float3 VoxelToWorld(uint3 voxelCoord)
{
    float3 uv = (float3(voxelCoord) + 0.5) / float3(ProbeResXY, ProbeResXY, ProbeResZ);
    return GridCenter + (uv - 0.5) * float3(ProbeRangeXY * 2.0, ProbeRangeXY * 2.0, ProbeRangeZ * 2.0);
}

[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ProbeResXY || DTid.y >= ProbeResXY || DTid.z >= ProbeResZ)
        return;

    float3 worldPos = VoxelToWorld(DTid);

    // Step size — march ~1 voxel per step in world space
    float voxelSize = ProbeRangeXY * 2.0 / float(ProbeResXY);
    float stepSize = voxelSize * 2.0;  // 2 voxels per step

    float4 sh2 = 0;

    for (int d = 0; d < NUM_DIRS; d++)
    {
        float3 dir = kDirs[d];
        bool hitOccluder = false;

        for (int s = 1; s <= NUM_STEPS; s++)
        {
            float3 samplePos = worldPos + dir * (float(s) * stepSize);

            // Project to screen space
            float4 clip = mul(ViewProj, float4(samplePos, 1.0));

            // Behind camera or at camera — skip
            if (clip.w <= 0.0)
                continue;

            float2 ndc = clip.xy / clip.w;
            float2 screenUV = ndc * float2(0.5, -0.5) + 0.5;

            // Off-screen — assume sky visible (conservative)
            if (any(screenUV < 0.0) || any(screenUV > 1.0))
                continue;

            // Read scene depth at projected screen position
            int2 screenCoord = int2(screenUV * float2(ScreenW, ScreenH));
            screenCoord = clamp(screenCoord, 0, int2(ScreenW - 1, ScreenH - 1));
            float sceneZ = LinearDepth.Load(int3(screenCoord, 0));

            // View-space depth of the sample point (clip.w = view-space Z)
            float sampleZ = clip.w;

            // If sample is behind scene geometry (with thickness bias) -> occluded
            if (sampleZ > sceneZ && (sampleZ - sceneZ) < stepSize * 4.0)
            {
                hitOccluder = true;
                break;
            }
        }

        if (!hitOccluder)
        {
            // Direction sees sky — project into SH2 basis
            sh2 += float4(
                SH_C0,
                SH_C1 * dir.x,
                SH_C1 * dir.y,
                SH_C1 * dir.z
            );
        }
    }

    // Normalize
    sh2 /= float(NUM_DIRS);

    ProbeGrid[DTid] = sh2;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 1: ProbeQuery CS (full-res per-pixel)
// ═══════════════════════════════════════════════════════════════════════════

static const char kProbeQueryCS[] = R"HLSL(
// ProbeQuery CS — Per-pixel sky visibility from volumetric probe grid.
// Samples SH2 coefficients from 3D probe, evaluates toward up direction.
// Much cheaper than per-pixel ray marching — single trilinear 3D sample.

cbuffer SkylightCB : register(b0)
{
    float4x4 InvViewProj;       // For world position reconstruction
    uint2    ScreenDims;
    float    NearZ;
    float    FarZ;
    float3   GridCenter;        // Probe grid center (snapped camera pos)
    float    ProbeRangeXY;
    float    ProbeRangeZ;
    float    Intensity;
    uint     FrameIndex;
    float    pad0;
};

Texture2D<float>  DepthTex    : register(t0);   // Raw depth for sky check
Texture3D<float4> ProbeGrid   : register(t1);   // 128x128x64 SH2 sky probe
Texture2D<float>  LinearDepth : register(t31);
SamplerState      TrilinearSamp : register(s0);

RWTexture2D<float> SkyOutput : register(u0);

static const float SH_C0 = 0.282095;
static const float SH_C1 = 0.488603;

float EvaluateSH2(float4 sh, float3 dir)
{
    return max(0.0,
        sh.x * SH_C0 +
        sh.y * SH_C1 * dir.x +
        sh.z * SH_C1 * dir.y +
        sh.w * SH_C1 * dir.z
    );
}

float3 WorldToProbeUV(float3 worldPos)
{
    float3 extent = float3(ProbeRangeXY * 2.0, ProbeRangeXY * 2.0, ProbeRangeZ * 2.0);
    return (worldPos - GridCenter) / extent + 0.5;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    // Sky check
    float rawDepth = DepthTex.Load(int3(coord, 0));
    if (rawDepth < 0.0001)
    {
        SkyOutput[coord] = 1.0;  // Sky is fully sky-visible
        return;
    }

    // Reconstruct world position from depth
    float2 uv = (float2(coord) + 0.5) / float2(ScreenDims);
    float4 ndc = float4(uv * 2.0 - 1.0, rawDepth, 1.0);
    ndc.y = -ndc.y;
    float4 worldH = mul(InvViewProj, ndc);
    float3 worldPos = worldH.xyz / worldH.w;

    // Sample probe grid
    float3 probeUV = WorldToProbeUV(worldPos);

    // Clamp to probe bounds — outside probe = assume full sky
    if (any(probeUV < 0.0) || any(probeUV > 1.0))
    {
        SkyOutput[coord] = 1.0;
        return;
    }

    float4 sh2 = ProbeGrid.SampleLevel(TrilinearSamp, probeUV, 0);

    // Evaluate SH2 toward up direction (Z-up in Skyrim)
    float3 skyDir = float3(0, 0, 1);
    float skyVis = EvaluateSH2(sh2, skyDir);
    skyVis = saturate(skyVis * Intensity);

    SkyOutput[coord] = skyVis;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — OLD Pass 1: Screen-space Skylighting CS (kept for reference)
// ═══════════════════════════════════════════════════════════════════════════

static const char kSkylightScreenSpaceCS[] = R"HLSL(
// Skylighting CS — Per-pixel upper-hemisphere visibility sampling.
//
// For each pixel, we test multiple directions in the upper hemisphere
// to see if they reach the sky (depth = 0 in reversed-Z) or are blocked
// by geometry.  The fraction of directions that escape gives the sky
// visibility factor.
//
// This is conceptually similar to GTAO but instead of measuring "how much
// of the hemisphere is occluded by nearby geometry", we measure "how much
// of the upper hemisphere is open to the sky".  The key difference:
//   - GTAO samples a short radius around the pixel (< 3m)
//   - Skylighting samples a long radius UPWARD (entire screen height)
//
// The result is multiplied by ENB's ambient color in the shader to produce
// physically-motivated ambient lighting with proper occlusion under arches,
// overhangs, interior doorways, tree canopies, etc.

cbuffer SkylightCB : register(b0)
{
    float4x4 ProjMatrix;           // Projection matrix (row-major)
    uint2    ScreenDims;           // Full-res dimensions
    float    NearZ;
    float    FarZ;
    float    SampleRadius;         // World-space max sample distance
    float    Intensity;            // Output multiplier
    int      NumDirections;        // Upper hemisphere directions (2-12)
    int      NumSteps;             // Steps per direction (2-16)
    uint     FrameIndex;           // For temporal jitter
    float3   pad0;
};

Texture2D<float> DepthTex : register(t0);       // Hi-Z mip 0
Texture2D<float4> BlueNoise : register(t30);   // 128x128 R2 quasi-random blue noise
Texture2D<float> LinearDepth : register(t31);  // Pre-computed linearized depth

RWTexture2D<float> SkyOutput : register(u0); // Full-res raw sky visibility

static const float PI = 3.14159265359;
static const float HALF_PI = 1.57079632679;

float3 UVToViewPos(float2 uv, float linearZ)
{
    float2 ndc = float2(uv.x * 2.0 - 1.0, (1.0 - uv.y) * 2.0 - 1.0);
    float3 viewPos;
    viewPos.x = ndc.x * linearZ / ProjMatrix[0][0];
    viewPos.y = ndc.y * linearZ / ProjMatrix[1][1];
    viewPos.z = linearZ;
    return viewPos;
}

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

    return normalize(cross(posU - posC, posR - posC));
}

// ── Blue noise temporal jitter (R2 quasi-random, Roberts 2018) ───────────
static const float GOLDEN_RATIO = 0.6180339887;

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);
    float2 texelSize = 1.0 / float2(ScreenDims);
    float2 uv = (float2(coord) + 0.5) * texelSize;

    float rawDepth = DepthTex.Load(int3(coord, 0));

    // Sky pixels always have full sky visibility
    if (rawDepth < 0.0001)
    {
        SkyOutput[coord] = 1.0;
        return;
    }

    float3 viewPos = UVToViewPos(uv, LinearDepth.Load(int3(coord, 0)));
    float3 viewNormal = ReconstructNormal(coord, texelSize);

    // Project sample radius from world-space to pixel-space.
    // ProjMatrix[0][0] maps world→NDC, multiply by half screen width for pixels.
    float screenRadius = SampleRadius * ProjMatrix[0][0] / viewPos.z
                         * float(ScreenDims.x) * 0.5;
    screenRadius = clamp(screenRadius, 4.0, float(ScreenDims.y) * 0.5);

    // Blue noise temporal rotation — spatially-uniform, faster convergence
    float4 bn = BlueNoise.Load(int3(int2(DTid.xy % 128), 0));
    float rotationOffset = frac(bn.x + float(FrameIndex) * GOLDEN_RATIO) * PI;

    float skyVisibility = 0.0;
    int validDirs = 0;

    for (int dir = 0; dir < NumDirections; dir++)
    {
        // Sample directions in the upper hemisphere (screen space: upward = -Y)
        // We distribute directions across 360 degrees but weight by the
        // upward component (view-space Y).  In screen space, "up" is -Y.
        float angle = (2.0 * PI / float(NumDirections)) * float(dir) + rotationOffset;
        float2 direction = float2(cos(angle), sin(angle));

        // Compute the view-space direction this screen direction corresponds to
        // Weight by how "upward" this direction is in view space
        // We want directions that go toward the sky (positive view-space Y in
        // right-handed, but Skyrim uses a specific convention)
        // Instead, we bias toward directions that move in the upper hemisphere
        // of the surface normal
        float3 sampleDirView = float3(direction.x, -direction.y, 0.0);
        float normalWeight = max(0.0, dot(normalize(sampleDirView), viewNormal));
        // At minimum, always contribute something for omnidirectional sampling
        normalWeight = max(normalWeight, 0.25);

        float stepSize = screenRadius / float(NumSteps);
        bool reachedSky = true;

        for (int step = 1; step <= NumSteps; step++)
        {
            float2 offset = direction * (float(step) * stepSize);
            int2 sampleCoord = coord + int2(offset);

            // Out of screen = assume sky
            if (sampleCoord.x < 0 || sampleCoord.x >= int(ScreenDims.x) ||
                sampleCoord.y < 0 || sampleCoord.y >= int(ScreenDims.y))
            {
                break;  // Escaped screen — counts as sky
            }

            float sampleDepthRaw = DepthTex.Load(int3(sampleCoord, 0));

            // Sky sample = direction reaches sky
            if (sampleDepthRaw < 0.0001)
                break;

            float2 sampleUV = (float2(sampleCoord) + 0.5) * texelSize;
            float3 samplePos = UVToViewPos(sampleUV, LinearDepth.Load(int3(sampleCoord, 0)));

            // Check if the sample is ABOVE us (closer to camera, blocking sky view)
            float3 toSample = samplePos - viewPos;
            float dist = length(toSample);

            // Elevation: positive dot with normal means sample is in our upper hemisphere
            float elevation = dot(normalize(toSample), viewNormal);

            // If geometry is above us and close, it blocks sky
            if (elevation > 0.1 && dist < SampleRadius * 2.0)
            {
                // This direction is blocked by nearby geometry
                reachedSky = false;
                break;
            }
        }

        if (reachedSky)
            skyVisibility += normalWeight;
        validDirs++;
    }

    // Normalize
    float maxContrib = float(validDirs) * 0.625;  // Expected avg of normalWeight
    float vis = (validDirs > 0) ? (skyVisibility / max(maxContrib, 0.01)) : 1.0;
    vis = saturate(vis * Intensity);

    SkyOutput[coord] = vis;
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: Spatial Denoise CS (5×5 bilateral)
// ═══════════════════════════════════════════════════════════════════════════

static const char kSpatialCS[] = R"HLSL(
cbuffer SpatialCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  DepthThreshold;
    float3 pad0;
};

Texture2D<float> SkyInput  : register(t0);
Texture2D<float> DepthTex  : register(t1);
Texture2D<float> LinearDepth : register(t31); // Pre-computed linearized depth

RWTexture2D<float> SkyOutput : register(u0);

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

    float centerDepth = LinearDepth.Load(int3(coord, 0));
    float centerSky   = SkyInput.Load(int3(coord, 0));

    float totalSky    = 0.0;
    float totalWeight = 0.0;

    for (int dy = -2; dy <= 2; dy++)
    {
        for (int dx = -2; dx <= 2; dx++)
        {
            int2 sampleCoord = coord + int2(dx, dy);
            sampleCoord = clamp(sampleCoord, 0, int2(ScreenDims) - 1);

            float sampleDepth = LinearDepth.Load(int3(sampleCoord, 0));
            float sampleSky   = SkyInput.Load(int3(sampleCoord, 0));

            float spatialW = GaussianWeight(dx) * GaussianWeight(dy);
            float depthDiff = abs(sampleDepth - centerDepth) / max(centerDepth, 0.01);
            float depthW = exp(-depthDiff / max(DepthThreshold, 0.001));

            float w = spatialW * depthW;
            totalSky += sampleSky * w;
            totalWeight += w;
        }
    }

    SkyOutput[coord] = totalSky / max(totalWeight, 0.0001);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 3: Temporal Accumulation CS (ping-pong)
// ═══════════════════════════════════════════════════════════════════════════

static const char kTemporalCS[] = R"HLSL(
cbuffer TemporalCB : register(b0)
{
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  TemporalAlpha;
    float  DepthRejectThreshold;
    uint   FrameIndex;
    float  pad0;
};

Texture2D<float> SkyCurrent  : register(t0);
Texture2D<float> SkyHistory  : register(t1);
Texture2D<float> DepthTex    : register(t2);
Texture2D<float> LinearDepth : register(t31); // Pre-computed linearized depth

RWTexture2D<float> SkyOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float current = SkyCurrent.Load(int3(coord, 0));
    float history = SkyHistory.Load(int3(coord, 0));

    // First few frames: no valid history yet — output current directly
    if (FrameIndex < 3) { SkyOutput[coord] = current; return; }

    // Adaptive blend: reject history when values change significantly
    float diff = abs(current - history);
    float reject = smoothstep(0.05, 0.3, diff);
    float alpha = lerp(TemporalAlpha, 0.5, reject);

    SkyOutput[coord] = lerp(history, current, alpha);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) ProbeUpdateCBData
{
    float    viewProj[16];       // 64 bytes
    float    gridCenter[3];      // +64  (12 bytes)
    float    probeRangeXY;       // +76  (4 bytes)
    float    probeRangeZ;        // +80  (4 bytes)
    uint32_t probeResXY;         // +84  (4 bytes)
    uint32_t probeResZ;          // +88  (4 bytes)
    uint32_t frameIndex;         // +92  (4 bytes)
    uint32_t screenW;            // +96  (4 bytes)
    uint32_t screenH;            // +100 (4 bytes)
    float    pad[2];             // +104 (8 bytes)
};
static_assert(sizeof(ProbeUpdateCBData) == 112, "ProbeUpdateCB must be 112 bytes");

struct alignas(16) SkylightCBData
{
    float    invViewProj[16];    // 64 bytes
    uint32_t screenW;            // +64
    uint32_t screenH;            // +68
    float    nearZ;              // +72
    float    farZ;               // +76
    float    gridCenter[3];      // +80  (12 bytes)
    float    probeRangeXY;       // +92  (4 bytes)
    float    probeRangeZ;        // +96  (4 bytes)
    float    intensity;          // +100 (4 bytes)
    uint32_t frameIndex;         // +104 (4 bytes)
    float    pad0;               // +108 (4 bytes)
};
static_assert(sizeof(SkylightCBData) == 112, "SkylightCB must be 112 bytes");

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

bool SkylightingRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                      IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm = ComputeManager::Get();
    auto& pl = RenderPipeline::Get();

    if (!cm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("SkylightingRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("SkylightingRenderer: failed to get backbuffer (0x{:X})",
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

    m_pipelineHandle = pl.AddPass({
        .name     = "Skylighting",
        .stage    = PipelineStage::PostGeometry,
        .priority = 17,  // After ContactShadows (16)
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_firstFrame  = true;
    m_frameIndex  = 0;

    SKSE::log::info("SkylightingRenderer: initialized ({}x{}, probe grid {}x{}x{}, range XY={:.0f} Z={:.0f}, t{})",
                    m_screenW, m_screenH, kProbeResXY, kProbeResXY, kProbeResZ,
                    m_probeRangeXY, m_probeRangeZ, kSRVSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool SkylightingRenderer::CompileShaders()
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
                SKSE::log::error("SkylightingRenderer: {} compile failed: {}", name,
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
            SKSE::log::error("SkylightingRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("SkyProbeUpdate",   kProbeUpdateCS, &m_probeUpdateCS)) return false;
    if (!CompileCS("SkyProbeQuery",   kProbeQueryCS,  &m_skylightCS))    return false;
    if (!CompileCS("SkylightSpatial", kSpatialCS,     &m_spatialCS))     return false;
    if (!CompileCS("SkylightTemporal",kTemporalCS,    &m_temporalCS))    return false;

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool SkylightingRenderer::CreateResources()
{
    HRESULT hr;

    auto CreateSkyTexture = [&](const char* name,
                                 ID3D11Texture2D** outTex,
                                 ID3D11ShaderResourceView** outSRV,
                                 ID3D11UnorderedAccessView** outUAV) -> bool
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_screenW;
        desc.Height     = m_screenH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, outTex);
        if (FAILED(hr)) {
            SKSE::log::error("SkylightingRenderer: failed to create {} texture (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(*outTex, &srvDesc, outSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R16_FLOAT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(*outTex, &uavDesc, outUAV);
        if (FAILED(hr)) return false;

        return true;
    };

    if (!CreateSkyTexture("skyRaw", &m_skyRaw, &m_skyRawSRV, &m_skyRawUAV))
        return false;
    if (!CreateSkyTexture("skySpatial", &m_skySpatial, &m_skySpatialSRV, &m_skySpatialUAV))
        return false;
    if (!CreateSkyTexture("skyHistory0", &m_skyHistory[0], &m_skyHistorySRV[0], &m_skyHistoryUAV[0]))
        return false;
    if (!CreateSkyTexture("skyHistory1", &m_skyHistory[1], &m_skyHistorySRV[1], &m_skyHistoryUAV[1]))
        return false;

    // ── Volumetric sky probe grid: 128×128×64 R16G16B16A16_FLOAT ─────
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width     = kProbeResXY;
        desc.Height    = kProbeResXY;
        desc.Depth     = kProbeResZ;
        desc.MipLevels = 1;
        desc.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture3D(&desc, nullptr, &m_probeGrid);
        if (FAILED(hr)) {
            SKSE::log::error("SkylightingRenderer: failed to create probe grid (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        // SRV
        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MostDetailedMip = 0;
        srvDesc.Texture3D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_probeGrid, &srvDesc, &m_probeGridSRV);
        if (FAILED(hr)) return false;

        // UAV
        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format                = desc.Format;
        uavDesc.ViewDimension         = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.MipSlice    = 0;
        uavDesc.Texture3D.FirstWSlice = 0;
        uavDesc.Texture3D.WSize       = kProbeResZ;
        hr = m_device->CreateUnorderedAccessView(m_probeGrid, &uavDesc, &m_probeGridUAV);
        if (FAILED(hr)) return false;
    }

    // Trilinear sampler for probe grid lookup
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        hr = m_device->CreateSamplerState(&sd, &m_linearSampler);
        if (FAILED(hr)) return false;
    }

    // Constant buffers
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;

        desc.ByteWidth = sizeof(ProbeUpdateCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_probeUpdateCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(SkylightCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_skylightCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(SpatialCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_spatialCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(TemporalCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_temporalCB);
        if (FAILED(hr)) return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV
// ═══════════════════════════════════════════════════════════════════════════

bool SkylightingRenderer::AcquireDepthSRV()
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
//  GetSkylightSRV
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* SkylightingRenderer::GetSkylightSRV() const
{
    if (!m_initialized) return nullptr;
    return m_skyHistorySRV[1 - m_pingPongIdx];
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution
// ═══════════════════════════════════════════════════════════════════════════

void SkylightingRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& scene = SceneMatrices::Get();
    if (!scene.IsValid()) return;

    auto& cm = ComputeManager::Get();

    // Prefer proxy-captured depth (always available during Present hook)
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    if (!depthSRV) {
        if (!AcquireDepthSRV()) return;
        depthSRV = m_depthSRV;
    }

    // HiZ is now built at PostGeometry:1 (before this pass),
    // so it's fresh during both mid-frame and Present-time dispatch.
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV())
            depthSRV = hiz.GetSRV();
    }

    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    // First frame: clear history
    if (m_firstFrame) {
        float clearColor[4] = {1.0f, 0.0f, 0.0f, 0.0f};
        // R16_FLOAT 1.0 = 0x3C00
        UINT clearVal[4] = {0x3C00, 0, 0, 0};
        ctx.context->ClearUnorderedAccessViewUint(m_skyHistoryUAV[0], clearVal);
        ctx.context->ClearUnorderedAccessViewUint(m_skyHistoryUAV[1], clearVal);
        m_firstFrame = false;
        m_pingPongIdx = 0;
    }

    cm.SaveCSState();

    // Bind pre-computed linear depth at t31 for all passes
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═══════════════════════════════════════════════════════════════════
    //  Pass 0: Probe grid update — fill 3D SH2 voxel grid
    // ═══════════════════════════════════════════════════════════════════
    float gridCenter[3];
    {
        const float* camPos = scene.CameraPos();
        float voxelSizeXY = m_probeRangeXY * 2.0f / kProbeResXY;
        float voxelSizeZ  = m_probeRangeZ  * 2.0f / kProbeResZ;
        gridCenter[0] = std::floor(camPos[0] / voxelSizeXY) * voxelSizeXY;
        gridCenter[1] = std::floor(camPos[1] / voxelSizeXY) * voxelSizeXY;
        gridCenter[2] = std::floor(camPos[2] / voxelSizeZ)  * voxelSizeZ;

        ProbeUpdateCBData cb = {};
        std::memcpy(cb.viewProj, scene.ViewProjMatrix(), sizeof(float) * 16);
        cb.gridCenter[0] = gridCenter[0];
        cb.gridCenter[1] = gridCenter[1];
        cb.gridCenter[2] = gridCenter[2];
        cb.probeRangeXY   = m_probeRangeXY;
        cb.probeRangeZ    = m_probeRangeZ;
        cb.probeResXY     = kProbeResXY;
        cb.probeResZ      = kProbeResZ;
        cb.frameIndex     = m_frameIndex;
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_probeUpdateCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_probeUpdateCB, 0);
        }

        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_probeGridUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_probeUpdateCB);
        ctx.context->CSSetShader(m_probeUpdateCS, nullptr, 0);

        UINT groupsX = (kProbeResXY + 3) / 4;
        UINT groupsY = (kProbeResXY + 3) / 4;
        UINT groupsZ = (kProbeResZ  + 3) / 4;
        ctx.context->Dispatch(groupsX, groupsY, groupsZ);

        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════
    //  Pass 1: Probe query — per-pixel trilinear sample from grid
    // ═══════════════════════════════════════════════════════════════════
    {
        SkylightCBData cb = {};
        std::memcpy(cb.invViewProj, scene.InvViewProjMatrix(), sizeof(float) * 16);
        cb.screenW       = m_screenW;
        cb.screenH       = m_screenH;
        cb.nearZ         = nearZ;
        cb.farZ          = farZ;
        cb.gridCenter[0] = gridCenter[0];
        cb.gridCenter[1] = gridCenter[1];
        cb.gridCenter[2] = gridCenter[2];
        cb.probeRangeXY  = m_probeRangeXY;
        cb.probeRangeZ   = m_probeRangeZ;
        cb.intensity     = m_intensity;
        cb.frameIndex    = m_frameIndex;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_skylightCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_skylightCB, 0);
        }

        ID3D11ShaderResourceView* srvs[] = { depthSRV, m_probeGridSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetSamplers(0, 1, &m_linearSampler);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_skyRawUAV, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_skylightCB);
        ctx.context->CSSetShader(m_skylightCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═══════════════════════════════════════════════════════════════════
    //  Pass 2: Spatial denoise
    // ═══════════════════════════════════════════════════════════════════
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

        ID3D11ShaderResourceView* srvs[] = { m_skyRawSRV, depthSRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_skySpatialUAV, nullptr);
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

    // ═══════════════════════════════════════════════════════════════════
    //  Pass 3: Temporal accumulation
    // ═══════════════════════════════════════════════════════════════════
    {
        int readIdx  = 1 - m_pingPongIdx;
        int writeIdx = m_pingPongIdx;

        TemporalCBData cb = {};
        cb.screenW              = m_screenW;
        cb.screenH              = m_screenH;
        cb.nearZ                = nearZ;
        cb.farZ                 = farZ;
        cb.temporalAlpha        = 0.15f;  // Faster convergence for stable skylighting
        cb.depthRejectThreshold = 0.1f;
        cb.frameIndex           = m_frameIndex;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_temporalCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_temporalCB, 0);
        }

        ID3D11ShaderResourceView* srvs[] = { m_skySpatialSRV, m_skyHistorySRV[readIdx], depthSRV };
        ctx.context->CSSetShaderResources(0, 3, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_skyHistoryUAV[writeIdx], nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_temporalCB);
        ctx.context->CSSetShader(m_temporalCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[3] = {};
        ctx.context->CSSetShaderResources(0, 3, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);

        m_pingPongIdx = readIdx;
    }

    // Unbind linear depth from t31
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // Register output SRV at t29
    SRVInjector::Get().RegisterSRV(kSRVSlot, GetSkylightSRV());

    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    m_frameIndex++;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown
// ═══════════════════════════════════════════════════════════════════════════

void SkylightingRenderer::Shutdown()
{
    if (!m_initialized) return;

    SRVInjector::Get().UnregisterSRV(kSRVSlot);

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("SkylightingRenderer: shut down");
}


void SkylightingRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    SafeRelease(m_probeUpdateCS);
    SafeRelease(m_probeGrid);
    SafeRelease(m_probeGridSRV);
    SafeRelease(m_probeGridUAV);
    SafeRelease(m_probeUpdateCB);
    SafeRelease(m_linearSampler);

    SafeRelease(m_skylightCS);
    SafeRelease(m_spatialCS);
    SafeRelease(m_temporalCS);

    SafeRelease(m_skyRaw);
    SafeRelease(m_skyRawSRV);
    SafeRelease(m_skyRawUAV);
    SafeRelease(m_skylightCB);

    SafeRelease(m_skySpatial);
    SafeRelease(m_skySpatialSRV);
    SafeRelease(m_skySpatialUAV);
    SafeRelease(m_spatialCB);

    SafeRelease(m_skyHistory[0]);
    SafeRelease(m_skyHistorySRV[0]);
    SafeRelease(m_skyHistoryUAV[0]);
    SafeRelease(m_skyHistory[1]);
    SafeRelease(m_skyHistorySRV[1]);
    SafeRelease(m_skyHistoryUAV[1]);
    SafeRelease(m_temporalCB);

    SafeRelease(m_depthSRV);
}

} // namespace SB
