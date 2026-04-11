//=============================================================================
//  SSGIRenderer.cpp — Screen-space global illumination via voxel cone tracing
//
//  Dispatch flow (per frame, PreENB stage):
//    1. Copy backbuffer for albedo read
//    2. Clear + dispatch Voxelize CS: depth+albedo → 128^3 voxel grid
//    3. Dispatch TraceGI CS: hemisphere rays → half-res raw GI
//    4. Dispatch Denoise CS: bilateral temporal filter (ping-pong)
//    5. Execute composite PS: bilateral upsample + additive blend
//=============================================================================

#include "SSGIRenderer.h"
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
//  Embedded HLSL — Pass 1: Voxelize CS
// ═══════════════════════════════════════════════════════════════════════════

static const char kVoxelizeCS[] = R"HLSL(
// Voxelize CS — project screen-space data into a 128^3 voxel grid.
// Each thread reads one screen pixel, determines its 3D voxel coordinate
// from depth, and atomically accumulates albedo + opacity into the grid.
//
// We use InterlockedAdd on a secondary atomic counter grid because HLSL
// typed UAV loads on float textures don't support atomics.  Instead we
// use two UAVs: one for the color accumulation (float4, written via
// average at the end) and a uint counter texture for weighting.

cbuffer VoxelizeCB : register(b0)
{
    uint2  ScreenDims;          // Full-res backbuffer size
    float  NearZ;               // Camera near plane
    float  FarZ;                // Camera far plane
    float  VoxelRange;          // World-space extent of the voxel grid
    float  pad0, pad1, pad2;
};

Texture2D<float>   DepthTex    : register(t0);   // Hi-Z mip 0 (full-res depth)
Texture2D<float4>  AlbedoTex   : register(t1);   // Backbuffer copy (scene color as albedo proxy)
Texture2D<float>   LinearDepth : register(t31);  // Pre-computed linearized depth

RWTexture3D<float4> VoxelGridSH   : register(u0);  // 128^3 — Y luminance SH2 (L0, L1x, L1y, L1z)
RWTexture3D<float4> VoxelGridCoCg : register(u1);  // 64^3  — chrominance + opacity (Co, Cg, opacity, 0)

// ── RGB → YCoCg conversion (lossless, invertible) ───────────────────────
float3 RGBtoYCoCg(float3 rgb)
{
    return float3(
         0.25 * rgb.r + 0.5 * rgb.g + 0.25 * rgb.b,   // Y  (luminance)
         0.5  * rgb.r                - 0.5  * rgb.b,    // Co (orange-blue)
        -0.25 * rgb.r + 0.5 * rgb.g - 0.25 * rgb.b     // Cg (green-purple)
    );
}

// ── SH2 projection: encode scalar luminance with direction ──────────────
// Band 0 (L0) + Band 1 (L1x, L1y, L1z) = 4 coefficients
float4 EncodeSH2(float value, float3 dir)
{
    return float4(
        0.282095 * value,
        0.488603 * dir.x * value,
        0.488603 * dir.y * value,
        0.488603 * dir.z * value
    );
}

// ── Reconstruct normal from depth gradients ─────────────────────────────
float3 ReconstructNormal(int2 coord)
{
    float3 posC = float3(float2(coord) / float2(ScreenDims), LinearDepth.Load(int3(coord, 0)));
    float3 posR = float3(float2(coord + int2(1, 0)) / float2(ScreenDims), LinearDepth.Load(int3(coord + int2(1, 0), 0)));
    float3 posU = float3(float2(coord + int2(0, 1)) / float2(ScreenDims), LinearDepth.Load(int3(coord + int2(0, 1), 0)));

    float3 ddx = posR - posC;
    float3 ddy = posU - posC;

    return normalize(cross(ddy, ddx));
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= ScreenDims.x || DTid.y >= ScreenDims.y)
        return;

    int2 coord = int2(DTid.xy);

    float rawDepth = DepthTex.Load(int3(coord, 0));

    // Skip sky pixels (reversed-Z: sky is near 0.0)
    if (rawDepth < 0.0001)
        return;

    float linearDepth = LinearDepth.Load(int3(coord, 0));

    // Skip pixels beyond voxel range
    if (linearDepth > VoxelRange)
        return;

    // Compute voxel coordinate
    // XY: screen-space UV -> [0, 128)
    // Z:  linear depth / voxelRange -> [0, 128)
    float3 voxelUV;
    voxelUV.x = (float(coord.x) + 0.5) / float(ScreenDims.x);
    voxelUV.y = (float(coord.y) + 0.5) / float(ScreenDims.y);
    voxelUV.z = saturate(linearDepth / VoxelRange);

    int3 voxelCoord = int3(voxelUV * 128.0);
    voxelCoord = clamp(voxelCoord, 0, 127);

    // Read albedo from backbuffer copy
    float4 albedo = AlbedoTex.Load(int3(coord, 0));

    // YCoCg SH2 encoding:
    // - Luminance (Y) projected into SH2 basis using surface normal
    //   → captures directional radiance distribution per voxel
    //   → reduces light leaking (back-facing voxels contribute less)
    // - Chrominance (Co, Cg) stored as scalars (low-frequency, no SH needed)
    float3 ycocg = RGBtoYCoCg(albedo.rgb);
    float3 normal = ReconstructNormal(coord);

    // Store: Y-SH2 at 128^3, CoCg at 64^3 (chroma subsampled — less spatial detail needed)
    // Last-write-wins is acceptable — temporal denoising smooths the result.
    VoxelGridSH[voxelCoord] = EncodeSH2(ycocg.x, normal);

    // CoCg at half voxel resolution: 2x2x2 Y voxels map to 1 CoCg voxel
    int3 cocgCoord = int3(voxelUV * 64.0);
    cocgCoord = clamp(cocgCoord, 0, 63);
    VoxelGridCoCg[cocgCoord] = float4(ycocg.y, ycocg.z, 1.0, 0.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 2: TraceGI CS (half-res)
// ═══════════════════════════════════════════════════════════════════════════

static const char kTraceGICS[] = R"HLSL(
// TraceGI CS — For each half-res pixel, shoot short-range hemisphere rays
// through the voxel grid and accumulate indirect radiance.

cbuffer TraceGICB : register(b0)
{
    uint2  HalfDims;        // Half-res output dimensions
    uint2  ScreenDims;      // Full-res backbuffer dimensions
    float  NearZ;
    float  FarZ;
    float  VoxelRange;
    int    RayCount;        // Number of hemisphere rays (1-8)
    int    MaxSteps;        // Max ray march steps per ray (4-64)
    float  GIIntensity;     // Output intensity multiplier
    uint   FrameIndex;      // For temporal rotation of ray directions
    float  FPDepthThreshold; // First-person depth mask (view-space units)
};

Texture2D<float>    DepthTex       : register(t0);   // Hi-Z mip 0
Texture2D<float4>   AlbedoTex      : register(t1);   // Backbuffer copy
Texture3D<float4>   VoxelGridSH    : register(t2);   // 128^3 — Y luminance SH2
Texture3D<float4>   VoxelGridCoCg  : register(t3);   // 64^3  — chrominance + opacity (chroma subsampled)
Texture2D<float4>   BlueNoise      : register(t30);  // 128x128 R2 quasi-random blue noise
Texture2D<float>    LinearDepth    : register(t31);  // Pre-computed linearized depth
SamplerState        LinearSamp     : register(s0);   // Trilinear for voxel reads

RWTexture2D<float4> GIOutput    : register(u0);   // Half-res raw GI (YCoCg)

// ── SH2 evaluation: reconstruct scalar from SH coefficients + direction ──
// Returns directional luminance for the given view direction.
// Back-facing voxels naturally contribute less — reduces light leaking.
float EvaluateSH2(float4 sh, float3 dir)
{
    return max(0.0,
        sh.x * 0.282095 +
        sh.y * 0.488603 * dir.x +
        sh.z * 0.488603 * dir.y +
        sh.w * 0.488603 * dir.z
    );
}

// ── YCoCg → RGB conversion ─────────────────────────────────────────────
float3 YCoCgToRGB(float3 ycocg)
{
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    return float3(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

// ── Reconstruct normal from depth ──────────────────────────────────────
float3 ReconstructNormal(int2 fullCoord)
{
    float3 posC = float3(float2(fullCoord) / float2(ScreenDims), LinearDepth.Load(int3(fullCoord, 0)));
    float3 posR = float3(float2(fullCoord + int2(1, 0)) / float2(ScreenDims), LinearDepth.Load(int3(fullCoord + int2(1, 0), 0)));
    float3 posU = float3(float2(fullCoord + int2(0, 1)) / float2(ScreenDims), LinearDepth.Load(int3(fullCoord + int2(0, 1), 0)));

    return normalize(cross(posU - posC, posR - posC));
}

// ── Blue noise temporal jitter ───────────────────────────────────────────
// R2 quasi-random blue noise provides spatially-uniform distribution that
// converges faster under temporal accumulation than hash-based white noise.
static const float GOLDEN_RATIO = 0.6180339887;
static const float PLASTIC_RECIP = 0.7548776662;  // 1/φ₂ — decorrelates channel 2

// ── Build hemisphere direction from index and normal ────────────────────
float3 HemisphereDir(int rayIdx, int totalRays, float3 normal, float2 noise)
{
    // Uniform hemisphere sampling with golden-ratio spiral
    float phi = 2.0 * 3.14159265 * (float(rayIdx) + noise.x) / float(totalRays);
    float cosTheta = sqrt(1.0 - (float(rayIdx) + noise.y) / float(totalRays));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    // Direction in tangent space
    float3 dir = float3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    // Build TBN from normal
    float3 up = abs(normal.z) < 0.999 ? float3(0, 0, 1) : float3(1, 0, 0);
    float3 tangent  = normalize(cross(up, normal));
    float3 binormal = cross(normal, tangent);

    // Transform to world-aligned hemisphere
    return normalize(tangent * dir.x + binormal * dir.y + normal * dir.z);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2 halfCoord = int2(DTid.xy);
    int2 fullCoord = halfCoord * 2;  // Corresponding full-res pixel

    float rawDepth = DepthTex.Load(int3(fullCoord, 0));

    // Skip sky
    if (rawDepth < 0.0001)
    {
        GIOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    float linearDepth = LinearDepth.Load(int3(fullCoord, 0));

    // Skip first-person geometry (hands/weapons) — no GI on close-range FP model
    if (linearDepth < FPDepthThreshold)
    {
        GIOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    // Skip pixels beyond voxel range
    if (linearDepth > VoxelRange)
    {
        GIOutput[halfCoord] = float4(0, 0, 0, 0);
        return;
    }

    float3 normal = ReconstructNormal(fullCoord);

    // Start position in voxel UV space [0,1]^3
    float3 startUV;
    startUV.x = (float(fullCoord.x) + 0.5) / float(ScreenDims.x);
    startUV.y = (float(fullCoord.y) + 0.5) / float(ScreenDims.y);
    startUV.z = saturate(linearDepth / VoxelRange);

    // Step size in UV space (1 voxel = 1/128)
    float stepSize = 1.0 / 128.0;

    // Blue noise temporal rotation — R2 quasi-random with golden ratio offset
    float4 bn = BlueNoise.Load(int3(int2(DTid.xy % 128), 0));
    float2 noise = float2(
        frac(bn.x + float(FrameIndex) * GOLDEN_RATIO),
        frac(bn.y + float(FrameIndex) * PLASTIC_RECIP)
    );

    float3 totalYCoCg = 0;

    for (int r = 0; r < RayCount; r++)
    {
        float3 dir = HemisphereDir(r, RayCount, normal, noise);

        // Convert direction to voxel UV step
        // XY are screen-space directions, Z is depth direction
        float3 uvDir = float3(dir.x, dir.y, dir.z) * stepSize;

        float3 pos = startUV + uvDir * 2.0;  // Start slightly offset from surface

        float3 rayYCoCg   = 0;
        float  rayOpacity = 0;

        // Negate ray direction for SH evaluation — we evaluate SH from the
        // viewpoint looking INTO the voxel, so use the incoming direction.
        float3 evalDir = -dir;

        for (int s = 0; s < MaxSteps; s++)
        {
            // Check bounds
            if (any(pos < 0.0) || any(pos > 1.0))
                break;

            // Sample SH2 luminance + CoCg from voxel grids
            float4 ySH   = VoxelGridSH.SampleLevel(LinearSamp, pos, 0);
            float4 cocgA = VoxelGridCoCg.SampleLevel(LinearSamp, pos, 0);

            float opacity = cocgA.z;
            if (opacity > 0.01)
            {
                // Evaluate directional luminance from SH2
                // Back-facing voxels contribute less — reduces light leaking
                float Y  = EvaluateSH2(ySH, evalDir);
                float Co = cocgA.x;
                float Cg = cocgA.y;

                // Front-to-back compositing in YCoCg space
                float alpha = opacity * (1.0 - rayOpacity);
                rayYCoCg  += float3(Y, Co, Cg) * alpha;
                rayOpacity += alpha;

                if (rayOpacity > 0.95)
                    break;
            }

            pos += uvDir;
        }

        totalYCoCg += rayYCoCg;
    }

    // Average over ray count and apply intensity
    totalYCoCg = totalYCoCg / float(max(RayCount, 1)) * GIIntensity;

    // Output in YCoCg — decoded to RGB at composite time
    GIOutput[halfCoord] = float4(totalYCoCg, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 3: Denoise CS (half-res, bilateral temporal)
// ═══════════════════════════════════════════════════════════════════════════

static const char kDenoiseCS[] = R"HLSL(
// Denoise CS — Bilateral temporal filter for GI.
// Reads current raw GI + previous frame denoised GI (ping-pong).
// Uses depth-aware edge stopping + temporal accumulation.

cbuffer DenoiseCB : register(b0)
{
    uint2  HalfDims;
    uint2  ScreenDims;
    float  NearZ;
    float  FarZ;
    float  TemporalAlpha;   // Blend factor toward history [0.05 .. 0.3]
    float  DepthThreshold;  // Depth-difference threshold for edge stopping
    uint   FrameIndex;
    float3 pad0;
};

Texture2D<float4>   CurrentGI  : register(t0);   // Raw GI (current frame)
Texture2D<float4>   HistoryGI  : register(t1);   // Previous denoised GI
Texture2D<float>    DepthTex   : register(t2);   // Full-res depth
Texture2D<float>    LinearDepth : register(t31); // Pre-computed linearized depth

RWTexture2D<float4> OutputGI   : register(u0);   // Denoised output

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= HalfDims.x || DTid.y >= HalfDims.y)
        return;

    int2 halfCoord = int2(DTid.xy);
    int2 fullCoord = halfCoord * 2;

    float3 current = CurrentGI.Load(int3(halfCoord, 0)).rgb;
    float3 history = HistoryGI.Load(int3(halfCoord, 0)).rgb;

    // First few frames: no valid history yet — output current directly
    if (FrameIndex < 3) { OutputGI[halfCoord] = float4(current, 1.0); return; }

    // ── Spatial bilateral filter (3x3 on half-res) ──────────────────────
    float centerDepth = LinearDepth.Load(int3(fullCoord, 0));

    float3 spatialSum = 0;
    float  spatialWeight = 0;

    [unroll]
    for (int dy = -1; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = -1; dx <= 1; dx++)
        {
            int2 sampleCoord = halfCoord + int2(dx, dy);
            sampleCoord = clamp(sampleCoord, 0, int2(HalfDims) - 1);

            float3 sampleGI = CurrentGI.Load(int3(sampleCoord, 0)).rgb;

            // Depth-based edge stopping
            int2 sampleFull = sampleCoord * 2;
            sampleFull = clamp(sampleFull, 0, int2(ScreenDims) - 1);
            float sampleDepth = LinearDepth.Load(int3(sampleFull, 0));
            float depthDiff = abs(sampleDepth - centerDepth) / max(centerDepth, 0.01);

            float w = exp(-depthDiff / max(DepthThreshold, 0.001));

            // Spatial kernel weight (simple box for now)
            spatialSum += sampleGI * w;
            spatialWeight += w;
        }
    }

    float3 filtered = spatialSum / max(spatialWeight, 0.001);

    // ── Temporal accumulation ───────────────────────────────────────────
    // YCoCg: .x IS luminance — direct comparison is more perceptually
    // accurate than weighted RGB dot product. Chrominance (.yz) changes
    // less per frame, so luminance drives rejection.
    float  luminanceDiff = abs(filtered.x - history.x);
    float  rejection = saturate(luminanceDiff * 5.0);

    float alpha = lerp(TemporalAlpha, 0.8, rejection);

    float3 result = lerp(history, filtered, alpha);

    // Clamp to prevent NaN/Inf propagation
    result = max(result, 0.0);
    result = min(result, 100.0);

    OutputGI[halfCoord] = float4(result, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  Embedded HLSL — Pass 4: Upsample + Composite PS (fullscreen)
// ═══════════════════════════════════════════════════════════════════════════

static const char kCompositePS[] = R"HLSL(
// Composite PS — Bilateral upsampling from half-res GI, additive blend.
// Additive blend is handled via D3D11 blend state (SrcAlpha One).

cbuffer CompositeCB : register(b0)
{
    uint2  ScreenDims;
    uint2  HalfDims;
    float  NearZ;
    float  FarZ;
    float  GIIntensity;
    float  pad0;
};

Texture2D<float4> GIBuffer   : register(t0);  // Half-res denoised GI
Texture2D<float>  DepthTex   : register(t1);  // Full-res depth
Texture2D<float>  LinearDepth : register(t31); // Pre-computed linearized depth
SamplerState      LinearSamp : register(s0);
SamplerState      PointSamp  : register(s1);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

// ── YCoCg → RGB decode ──────────────────────────────────────────────────
float3 YCoCgToRGB(float3 ycocg)
{
    float Y  = ycocg.x;
    float Co = ycocg.y;
    float Cg = ycocg.z;
    return float3(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;
    int2   fullCoord = int2(uv * float2(ScreenDims));

    // Full-res center depth
    float centerDepth = LinearDepth.Load(int3(fullCoord, 0));

    // Bilateral upsample: sample 4 nearest half-res texels, weight by depth
    // Upsampling in YCoCg space preserves luminance boundaries better
    float2 halfUV = uv * float2(HalfDims);
    int2   halfBase = int2(halfUV - 0.5);

    float3 result = 0;
    float  totalWeight = 0;

    [unroll]
    for (int dy = 0; dy <= 1; dy++)
    {
        [unroll]
        for (int dx = 0; dx <= 1; dx++)
        {
            int2 sampleCoord = halfBase + int2(dx, dy);
            sampleCoord = clamp(sampleCoord, 0, int2(HalfDims) - 1);

            float3 gi = GIBuffer.Load(int3(sampleCoord, 0)).rgb;

            // Depth weight from corresponding full-res pixel
            int2 sampleFull = sampleCoord * 2;
            sampleFull = clamp(sampleFull, 0, int2(ScreenDims) - 1);
            float sampleDepth = LinearDepth.Load(int3(sampleFull, 0));

            float depthDiff = abs(sampleDepth - centerDepth) / max(centerDepth, 0.01);
            float w = exp(-depthDiff * 50.0);

            // Bilinear weight
            float2 f = frac(halfUV - 0.5);
            float bilinW = (dx == 0 ? (1.0 - f.x) : f.x) * (dy == 0 ? (1.0 - f.y) : f.y);
            w *= bilinW;

            result += gi * w;
            totalWeight += w;
        }
    }

    result /= max(totalWeight, 0.001);

    // Decode YCoCg → RGB at final output
    result = YCoCgToRGB(result);
    result = max(result, 0.0);  // Clamp negative values from decode
    // NOTE: GIIntensity already applied in TraceGI pass — don't double-multiply

    // Output with alpha=1 for additive blend (blend state: Src * 1 + Dst * 1)
    return float4(result, 1.0);
}
)HLSL";


// ═══════════════════════════════════════════════════════════════════════════
//  CB structures — must match HLSL cbuffers exactly
// ═══════════════════════════════════════════════════════════════════════════

struct alignas(16) VoxelizeCBData
{
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    voxelRange;
    float    pad[3];
};
static_assert(sizeof(VoxelizeCBData) == 32, "VoxelizeCB must be 32 bytes");

struct alignas(16) TraceGICBData
{
    uint32_t halfW;
    uint32_t halfH;
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    voxelRange;
    int32_t  rayCount;
    int32_t  maxSteps;
    float    giIntensity;
    uint32_t frameIndex;
    float    fpDepthThreshold;  // first-person depth mask (view-space units)
};
static_assert(sizeof(TraceGICBData) == 48, "TraceGICB must be 48 bytes");

struct alignas(16) DenoiseCBData
{
    uint32_t halfW;
    uint32_t halfH;
    uint32_t screenW;
    uint32_t screenH;
    float    nearZ;
    float    farZ;
    float    temporalAlpha;
    float    depthThreshold;
    uint32_t frameIndex;
    float    pad[3];
};
static_assert(sizeof(DenoiseCBData) == 48, "DenoiseCB must be 48 bytes");

struct alignas(16) CompositeCBData
{
    uint32_t screenW;
    uint32_t screenH;
    uint32_t halfW;
    uint32_t halfH;
    float    nearZ;
    float    farZ;
    float    giIntensity;
    float    pad0;
};
static_assert(sizeof(CompositeCBData) == 32, "CompositeCB must be 32 bytes");


// ═══════════════════════════════════════════════════════════════════════════
//  Initialize
// ═══════════════════════════════════════════════════════════════════════════

bool SSGIRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    if (!cm.IsInitialized() || !rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("SSGIRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("SSGIRenderer: failed to get backbuffer (0x{:X})",
                         static_cast<uint32_t>(hr));
        return false;
    }

    D3D11_TEXTURE2D_DESC bbDesc;
    backTex->GetDesc(&bbDesc);
    backTex->Release();

    m_screenW = bbDesc.Width;
    m_screenH = bbDesc.Height;
    m_halfW   = (m_screenW + 1) / 2;
    m_halfH   = (m_screenH + 1) / 2;

    if (!CompileShaders()) return false;
    if (!CreateResources()) {
        ReleaseResources();
        return false;
    }

    // Register as PreENB pipeline pass (GI visible to ENB shaders same frame)
    m_pipelineHandle = pl.AddPass({
        .name     = "SSGI",
        .stage    = PipelineStage::PostGeometry,
        .priority = 20,  // After HiZ (priority 10), before other effects
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_firstFrame  = true;
    m_frameIndex  = 0;

    SKSE::log::info("SSGIRenderer: initialized ({}x{}, half={}x{}, Y-SH2={}^3, CoCg={}^3, rays={}, steps={}, t{})",
                    m_screenW, m_screenH, m_halfW, m_halfH, kVoxelRes, kCoCgVoxelRes,
                    m_rayCount, m_maxSteps, kSRVSlot);
    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Compile all shaders
// ═══════════════════════════════════════════════════════════════════════════

bool SSGIRenderer::CompileShaders()
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
                SKSE::log::error("SSGIRenderer: {} compile failed: {}", name,
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
            SKSE::log::error("SSGIRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("SSGIVoxelize", kVoxelizeCS, &m_voxelizeCS)) return false;
    if (!CompileCS("SSGITraceGI",  kTraceGICS,  &m_traceCS))    return false;
    if (!CompileCS("SSGIDenoise",  kDenoiseCS,  &m_denoiseCS))  return false;

    // Compile composite PS via RenderPassManager
    m_compositePass = RenderPassManager::Get().RegisterPass({
        .name     = "SSGIComposite",
        .psSource = kCompositePS,
    });
    if (!m_compositePass) {
        SKSE::log::error("SSGIRenderer: failed to register SSGIComposite pass");
        return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Create GPU resources
// ═══════════════════════════════════════════════════════════════════════════

bool SSGIRenderer::CreateResources()
{
    HRESULT hr;

    // ── 3D voxel grid: 128^3 R16G16B16A16_FLOAT ────────────────────────
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width     = kVoxelRes;
        desc.Height    = kVoxelRes;
        desc.Depth     = kVoxelRes;
        desc.MipLevels = 1;
        desc.Format    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture3D(&desc, nullptr, &m_voxelGrid);
        if (FAILED(hr)) {
            SKSE::log::error("SSGIRenderer: failed to create voxel grid (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MostDetailedMip = 0;
        srvDesc.Texture3D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_voxelGrid, &srvDesc, &m_voxelGridSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format               = desc.Format;
        uavDesc.ViewDimension        = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.MipSlice   = 0;
        uavDesc.Texture3D.FirstWSlice = 0;
        uavDesc.Texture3D.WSize       = kVoxelRes;
        hr = m_device->CreateUnorderedAccessView(m_voxelGrid, &uavDesc, &m_voxelGridUAV);
        if (FAILED(hr)) return false;

        // ── Second 3D grid: CoCg chrominance + opacity ──────────────────
        // Half resolution of Y-SH2 grid (64^3 vs 128^3).  Chroma needs less
        // spatial detail — trilinear sampling in the trace smooths it fine.
        desc.Width  = kCoCgVoxelRes;
        desc.Height = kCoCgVoxelRes;
        desc.Depth  = kCoCgVoxelRes;
        hr = m_device->CreateTexture3D(&desc, nullptr, &m_voxelGridCoCg);
        if (FAILED(hr)) {
            SKSE::log::error("SSGIRenderer: failed to create CoCg voxel grid (0x{:X})",
                             static_cast<uint32_t>(hr));
            return false;
        }
        hr = m_device->CreateShaderResourceView(m_voxelGridCoCg, &srvDesc, &m_voxelGridCoCgSRV);
        if (FAILED(hr)) return false;
        uavDesc.Texture3D.WSize = kCoCgVoxelRes;  // 64 (not 128)
        hr = m_device->CreateUnorderedAccessView(m_voxelGridCoCg, &uavDesc, &m_voxelGridCoCgUAV);
        if (FAILED(hr)) return false;
    }

    // ── Backbuffer copy for albedo ──────────────────────────────────────
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

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_backbufferCopy);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                 &m_backbufferCopySRV);
        if (FAILED(hr)) return false;
    }

    // ── Half-res raw GI buffer ──────────────────────────────────────────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_halfW;
        desc.Height     = m_halfH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_giRaw);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_giRaw, &srvDesc, &m_giRawSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = desc.Format;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(m_giRaw, &uavDesc, &m_giRawUAV);
        if (FAILED(hr)) return false;
    }

    // ── Ping-pong GI history (half-res, for temporal denoising) ─────────
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_halfW;
        desc.Height     = m_halfH;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = desc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = desc.Format;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;

        for (int i = 0; i < 2; ++i) {
            hr = m_device->CreateTexture2D(&desc, nullptr, &m_giHistory[i]);
            if (FAILED(hr)) return false;

            hr = m_device->CreateShaderResourceView(m_giHistory[i], &srvDesc,
                                                     &m_giHistorySRV[i]);
            if (FAILED(hr)) return false;

            hr = m_device->CreateUnorderedAccessView(m_giHistory[i], &uavDesc,
                                                      &m_giHistoryUAV[i]);
            if (FAILED(hr)) return false;
        }
    }

    // ── Constant buffers ────────────────────────────────────────────────
    {
        auto CreateCB = [&](UINT size, ID3D11Buffer** out) -> bool {
            D3D11_BUFFER_DESC desc = {};
            desc.ByteWidth      = (size + 15) & ~15u;
            desc.Usage           = D3D11_USAGE_DYNAMIC;
            desc.BindFlags       = D3D11_BIND_CONSTANT_BUFFER;
            desc.CPUAccessFlags  = D3D11_CPU_ACCESS_WRITE;
            return SUCCEEDED(m_device->CreateBuffer(&desc, nullptr, out));
        };

        if (!CreateCB(sizeof(VoxelizeCBData),  &m_voxelizeCB))  return false;
        if (!CreateCB(sizeof(TraceGICBData),   &m_traceCB))     return false;
        if (!CreateCB(sizeof(DenoiseCBData),   &m_denoiseCB))   return false;
        if (!CreateCB(sizeof(CompositeCBData), &m_compositeCB)) return false;
    }

    // ── Additive blend state ────────────────────────────────────────────
    {
        D3D11_BLEND_DESC desc = {};
        desc.RenderTarget[0].BlendEnable    = TRUE;
        desc.RenderTarget[0].SrcBlend       = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlend      = D3D11_BLEND_ONE;
        desc.RenderTarget[0].BlendOp        = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].SrcBlendAlpha  = D3D11_BLEND_ONE;
        desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
        desc.RenderTarget[0].BlendOpAlpha   = D3D11_BLEND_OP_ADD;
        desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

        hr = m_device->CreateBlendState(&desc, &m_additiveBlend);
        if (FAILED(hr)) return false;
    }

    // ── Samplers ────────────────────────────────────────────────────────
    {
        D3D11_SAMPLER_DESC desc = {};
        desc.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        desc.MaxAnisotropy  = 1;
        desc.ComparisonFunc = D3D11_COMPARISON_NEVER;
        desc.MaxLOD         = D3D11_FLOAT32_MAX;
        hr = m_device->CreateSamplerState(&desc, &m_linearSampler);
        if (FAILED(hr)) return false;

        desc.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
        hr = m_device->CreateSamplerState(&desc, &m_pointSampler);
        if (FAILED(hr)) return false;
    }

    return true;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Acquire depth SRV from currently-bound DSV
// ═══════════════════════════════════════════════════════════════════════════

bool SSGIRenderer::AcquireDepthSRV()
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
//  GetGISRV — returns the most recently denoised GI buffer SRV
// ═══════════════════════════════════════════════════════════════════════════

ID3D11ShaderResourceView* SSGIRenderer::GetGISRV() const
{
    if (!m_initialized) return nullptr;
    // After ExecutePass, pingPongIdx was swapped, so the "just written"
    // denoised GI is at (1 - current pingPongIdx).
    return m_giHistorySRV[1 - m_pingPongIdx];
}


// ═══════════════════════════════════════════════════════════════════════════
//  Per-frame execution (called by RenderPipeline at PreENB stage)
// ═══════════════════════════════════════════════════════════════════════════

void SSGIRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Gate: scene matrices must be valid (camera data verified non-garbage)
    if (!SceneMatrices::Get().IsValid()) return;

    auto& cm = ComputeManager::Get();

    // ── Acquire depth ───────────────────────────────────────────────────
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

    // ── Copy scene color for albedo ─────────────────────────────────────
    // During mid-frame dispatch, the backbuffer doesn't contain the scene —
    // the game renders to an internal RT exposed via ctx.gameSceneRTV.
    // Extract the texture from that RTV first; fall back to swapchain only
    // when gameSceneRTV is null (Present-time dispatch).
    {
        ID3D11Texture2D* sceneTex = nullptr;

        if (ctx.gameSceneRTV) {
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                res->QueryInterface(__uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&sceneTex));
                res->Release();
            }
        }

        if (!sceneTex) {
            // Fallback: swapchain backbuffer (Present-time path)
            auto* sc = ctx.swapChain ? ctx.swapChain : D3D11Hook::GetSwapChain();
            if (sc) {
                sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&sceneTex));
            }
        }

        if (!sceneTex) {
            if (m_depthSRV) { m_depthSRV->Release(); m_depthSRV = nullptr; }
            return;
        }

        // Ensure copy texture format matches source (game scene RT is HDR float,
        // but m_backbufferCopy was created as R8G8B8A8_UNORM at init time).
        D3D11_TEXTURE2D_DESC srcDesc;
        sceneTex->GetDesc(&srcDesc);
        D3D11_TEXTURE2D_DESC copyDesc;
        m_backbufferCopy->GetDesc(&copyDesc);
        if (srcDesc.Format != copyDesc.Format ||
            srcDesc.Width  != copyDesc.Width  ||
            srcDesc.Height != copyDesc.Height)
        {
            m_backbufferCopySRV->Release(); m_backbufferCopySRV = nullptr;
            m_backbufferCopy->Release();    m_backbufferCopy    = nullptr;

            D3D11_TEXTURE2D_DESC newDesc = srcDesc;
            newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            newDesc.Usage          = D3D11_USAGE_DEFAULT;
            newDesc.CPUAccessFlags = 0;
            newDesc.MiscFlags      = 0;
            newDesc.MipLevels      = 1;
            newDesc.ArraySize      = 1;
            newDesc.SampleDesc     = {1, 0};
            m_device->CreateTexture2D(&newDesc, nullptr, &m_backbufferCopy);

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format                    = newDesc.Format;
            srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MostDetailedMip = 0;
            srvDesc.Texture2D.MipLevels       = 1;
            m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc,
                                                &m_backbufferCopySRV);

            SKSE::log::info("SSGI: backbuffer copy recreated — {}x{} fmt={}",
                srcDesc.Width, srcDesc.Height, static_cast<uint32_t>(srcDesc.Format));
        }

        ctx.context->CopyResource(m_backbufferCopy, sceneTex);
        sceneTex->Release();
    }

    // Camera near/far from SceneData (reconstructed from CameraTracker)
    auto& scene = SceneMatrices::Get();
    const float nearZ = scene.NearClip();
    const float farZ  = scene.FarClip();

    // ── First frame: clear history buffers ──────────────────────────────
    if (m_firstFrame) {
        float clearColor[4] = {0, 0, 0, 0};
        ctx.context->ClearUnorderedAccessViewFloat(m_giHistoryUAV[0], clearColor);
        ctx.context->ClearUnorderedAccessViewFloat(m_giHistoryUAV[1], clearColor);
        m_firstFrame = false;
        m_pingPongIdx = 0;
    }

    cm.SaveCSState();

    // ── Bind pre-computed linear depth at t31 for all CS passes ─────────
    auto* linearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
    ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &linearDepthSRV);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: Voxelize — depth + albedo → 128^3 voxel grid
    // ═════════════════════════════════════════════════════════════════════
    {
        // Clear both voxel grids (Y-SH2 + CoCg)
        float clearVal[4] = {0, 0, 0, 0};
        ctx.context->ClearUnorderedAccessViewFloat(m_voxelGridUAV, clearVal);
        ctx.context->ClearUnorderedAccessViewFloat(m_voxelGridCoCgUAV, clearVal);

        // Update CB
        VoxelizeCBData cb;
        cb.screenW    = m_screenW;
        cb.screenH    = m_screenH;
        cb.nearZ      = nearZ;
        cb.farZ       = farZ;
        cb.voxelRange = m_voxelRange;
        cb.pad[0] = cb.pad[1] = cb.pad[2] = 0;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_voxelizeCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_voxelizeCB, 0);
        }

        ID3D11ShaderResourceView* srvs[] = { depthSRV, m_backbufferCopySRV };
        ctx.context->CSSetShaderResources(0, 2, srvs);

        // u0 = Y-SH2 grid, u1 = CoCg grid
        ID3D11UnorderedAccessView* uavs[] = { m_voxelGridUAV, m_voxelGridCoCgUAV };
        ctx.context->CSSetUnorderedAccessViews(0, 2, uavs, nullptr);

        ctx.context->CSSetConstantBuffers(0, 1, &m_voxelizeCB);

        ctx.context->CSSetShader(m_voxelizeCS, nullptr, 0);

        UINT groupsX = (m_screenW + 7) / 8;
        UINT groupsY = (m_screenH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Clear bindings
        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAVs[2] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 2, nullUAVs, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2: TraceGI — hemisphere rays through voxel grid → half-res GI
    // ═════════════════════════════════════════════════════════════════════
    {
        TraceGICBData cb;
        cb.halfW       = m_halfW;
        cb.halfH       = m_halfH;
        cb.screenW     = m_screenW;
        cb.screenH     = m_screenH;
        cb.nearZ       = nearZ;
        cb.farZ        = farZ;
        cb.voxelRange  = m_voxelRange;
        cb.rayCount    = m_rayCount;
        cb.maxSteps    = m_maxSteps;
        cb.giIntensity = m_giIntensity;
        cb.frameIndex  = m_frameIndex;
        cb.fpDepthThreshold = 16.0f;  // ~16 view-space units (between ENB 11.76, CS 18.0)

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_traceCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_traceCB, 0);
        }

        // t0=depth, t1=albedo, t2=Y-SH2 voxel grid, t3=CoCg voxel grid, t30=blue noise
        ID3D11ShaderResourceView* srvs[] = { depthSRV, m_backbufferCopySRV, m_voxelGridSRV, m_voxelGridCoCgSRV };
        ctx.context->CSSetShaderResources(0, 4, srvs);
        auto* bnSRV = SharedGPUResources::Get().GetBlueNoiseSRV();
        if (bnSRV) ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &bnSRV);

        ID3D11SamplerState* samplers[] = { m_linearSampler };
        ctx.context->CSSetSamplers(0, 1, samplers);

        ID3D11UnorderedAccessView* uavs[] = { m_giRawUAV };
        ctx.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

        ctx.context->CSSetConstantBuffers(0, 1, &m_traceCB);

        ctx.context->CSSetShader(m_traceCS, nullptr, 0);

        UINT groupsX = (m_halfW + 7) / 8;
        UINT groupsY = (m_halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[4] = {};
        ctx.context->CSSetShaderResources(0, 4, nullSRVs);
        { ID3D11ShaderResourceView* n = nullptr; ctx.context->CSSetShaderResources(SharedGPUResources::kBlueNoiseSlot, 1, &n); }
        ID3D11UnorderedAccessView* nullUAVs[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 3: Denoise — bilateral temporal filter (ping-pong)
    // ═════════════════════════════════════════════════════════════════════
    {
        int readIdx  = 1 - m_pingPongIdx;
        int writeIdx = m_pingPongIdx;

        DenoiseCBData cb;
        cb.halfW          = m_halfW;
        cb.halfH          = m_halfH;
        cb.screenW        = m_screenW;
        cb.screenH        = m_screenH;
        cb.nearZ          = nearZ;
        cb.farZ           = farZ;
        cb.temporalAlpha  = 0.2f;
        cb.depthThreshold = 0.05f;
        cb.frameIndex     = m_frameIndex;

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_denoiseCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_denoiseCB, 0);
        }

        // t0 = raw GI (current frame), t1 = previous denoised (history), t2 = depth
        ID3D11ShaderResourceView* srvs[] = { m_giRawSRV, m_giHistorySRV[readIdx], depthSRV };
        ctx.context->CSSetShaderResources(0, 3, srvs);

        // u0 = output denoised GI
        ID3D11UnorderedAccessView* uavs[] = { m_giHistoryUAV[writeIdx] };
        ctx.context->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);

        ctx.context->CSSetConstantBuffers(0, 1, &m_denoiseCB);

        ctx.context->CSSetShader(m_denoiseCS, nullptr, 0);

        UINT groupsX = (m_halfW + 7) / 8;
        UINT groupsY = (m_halfH + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRVs[3] = {};
        ctx.context->CSSetShaderResources(0, 3, nullSRVs);
        ID3D11UnorderedAccessView* nullUAVs[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAVs, nullptr);
    }

    // ── Unbind linear depth from CS t31 ─────────────────────────────────
    {
        ID3D11ShaderResourceView* nullSRV = nullptr;
        ctx.context->CSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
    }

    cm.RestoreCSState();

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 4: Upsample + Composite (fullscreen PS, additive blend)
    // ═════════════════════════════════════════════════════════════════════
    // During mid-frame dispatch, composite to the game's scene RT (not the
    // swapchain backbuffer, which doesn't hold the scene yet). Fall back to
    // swapchain only at Present-time.
    {
        ID3D11RenderTargetView* compositeRTV = nullptr;
        bool ownRTV = false;  // true when we created the RTV and must release it

        if (ctx.gameSceneRTV) {
            // Mid-frame: composite directly into the game's scene RT
            compositeRTV = ctx.gameSceneRTV;
            compositeRTV->AddRef();
            ownRTV = true;
        } else {
            // Present-time fallback: create an RTV from the swapchain backbuffer
            auto* sc = ctx.swapChain ? ctx.swapChain : D3D11Hook::GetSwapChain();
            if (sc) {
                ID3D11Texture2D* backTex = nullptr;
                if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                             reinterpret_cast<void**>(&backTex)))) {
                    D3D11_TEXTURE2D_DESC bbDesc;
                    backTex->GetDesc(&bbDesc);

                    D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
                    rtvDesc.Format        = bbDesc.Format;
                    rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
                    m_device->CreateRenderTargetView(backTex, &rtvDesc, &compositeRTV);
                    backTex->Release();
                    ownRTV = true;
                }
            }
        }

        if (compositeRTV) {
            // Update composite CB
            CompositeCBData cb;
            cb.screenW     = m_screenW;
            cb.screenH     = m_screenH;
            cb.halfW       = m_halfW;
            cb.halfH       = m_halfH;
            cb.nearZ       = nearZ;
            cb.farZ        = farZ;
            cb.giIntensity = m_giIntensity;
            cb.pad0        = 0;

            // Denoised GI SRV is from the write side of this frame's ping-pong
            int writeIdx = m_pingPongIdx;
            ID3D11ShaderResourceView* compositeSRVs[] = {
                m_giHistorySRV[writeIdx],  // t0: denoised half-res GI
                depthSRV                    // t1: full-res depth
            };
            ID3D11SamplerState* compositeSamplers[] = {
                m_linearSampler,  // s0
                m_pointSampler    // s1
            };

            // Bind linear depth at PS t31 for composite pixel shader
            auto* psLinearDepthSRV = SharedGPUResources::Get().GetLinearDepthSRV();
            ctx.context->PSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &psLinearDepthSRV);

            RenderPassManager::Get().Execute({
                .passID       = m_compositePass,
                .rtv          = compositeRTV,
                .srvs         = compositeSRVs,
                .srvCount     = 2,
                .samplers     = compositeSamplers,
                .samplerCount = 2,
                .cbData       = &cb,
                .cbSize       = sizeof(cb),
                .blendState   = m_additiveBlend,
            });

            // Unbind linear depth from PS t31
            {
                ID3D11ShaderResourceView* nullSRV = nullptr;
                ctx.context->PSSetShaderResources(SharedGPUResources::kLinearDepthSlot, 1, &nullSRV);
            }

            if (ownRTV)
                compositeRTV->Release();
        }
    }

    // ── Register GI SRV for injection at t26 ────────────────────────────
    {
        int writeIdx = m_pingPongIdx;
        auto& inj = SRVInjector::Get();
        if (inj.IsInitialized() && m_giHistorySRV[writeIdx]) {
            inj.RegisterSRV(kSRVSlot, m_giHistorySRV[writeIdx]);
        }
    }

    // ── Swap ping-pong index ────────────────────────────────────────────
    m_pingPongIdx = 1 - m_pingPongIdx;

    // ── Cleanup ─────────────────────────────────────────────────────────
    if (m_depthSRV) {
        m_depthSRV->Release();
        m_depthSRV = nullptr;
    }

    ++m_frameIndex;
}


// ═══════════════════════════════════════════════════════════════════════════
//  Shutdown + ReleaseResources
// ═══════════════════════════════════════════════════════════════════════════

void SSGIRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    // Voxelize (Y-SH2 + CoCg grids)
    SafeRelease(m_voxelGridUAV);
    SafeRelease(m_voxelGridSRV);
    SafeRelease(m_voxelGrid);
    SafeRelease(m_voxelGridCoCgUAV);
    SafeRelease(m_voxelGridCoCgSRV);
    SafeRelease(m_voxelGridCoCg);
    SafeRelease(m_voxelizeCB);
    SafeRelease(m_backbufferCopySRV);
    SafeRelease(m_backbufferCopy);

    // TraceGI
    SafeRelease(m_giRawUAV);
    SafeRelease(m_giRawSRV);
    SafeRelease(m_giRaw);
    SafeRelease(m_traceCB);

    // Denoise
    for (int i = 0; i < 2; ++i) {
        SafeRelease(m_giHistoryUAV[i]);
        SafeRelease(m_giHistorySRV[i]);
        SafeRelease(m_giHistory[i]);
    }
    SafeRelease(m_denoiseCB);

    // Composite
    SafeRelease(m_compositeCB);
    SafeRelease(m_additiveBlend);
    SafeRelease(m_linearSampler);
    SafeRelease(m_pointSampler);

    // Depth
    SafeRelease(m_depthSRV);

    // Compute shaders
    SafeRelease(m_voxelizeCS);
    SafeRelease(m_traceCS);
    SafeRelease(m_denoiseCS);
}

void SSGIRenderer::Shutdown()
{
    ReleaseResources();
    m_initialized = false;
    m_screenW = m_screenH = m_halfW = m_halfH = 0;
}

} // namespace SB
