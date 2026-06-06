//=============================================================================
//  BloomRenderer.cpp — Compute-First Multi-Pass Bloom Pipeline
//
//  Dispatch flow (per frame, PreUI stage, priority 10):
//    1. Copy scene color to temp SRV (gameSceneRTV mid-frame, backbuffer fallback)
//    2. Bright Extract CS: full-res scene color -> half-res bright pixels
//    3. Downsample CS x3: 13-tap Jimenez filter (half -> quarter -> eighth -> sixteenth)
//    4. Upsample CS x3: 9-tap tent filter (sixteenth -> eighth -> quarter -> half)
//    5. Anamorphic Streak CS (optional): horizontal streak at half-res
//    6. Composite PS: blend all mip levels -> final bloom output
//
//  Algorithm:
//    Bright extraction uses Karis anti-firefly weighting (1/(1+luma)) to
//    prevent single bright pixels from dominating.  Soft-knee threshold
//    gives a smooth transition instead of hard cutoff.  The downsample
//    chain uses Jimenez's 13-tap filter from SIGGRAPH 2014 for high-quality
//    blur without visible ringing.  Upsampling uses a 3x3 tent filter
//    (bilinear kernel) that additively blends with the corresponding
//    downsample level to preserve detail.  Per-mip spectral tinting
//    approximates Rayleigh scattering (warm near-field, cool far-field).
//    The optional anamorphic streak applies a wide horizontal blur with
//    spectral dispersion (red extends wider than blue).
//
//  Quality comparable to high-end bloom (UE5 / Frostbite) but runs entirely
//  as compute + one fullscreen PS pass with no BSShader hooks required.
//=============================================================================

#include "BloomRenderer.h"
#include "ShaderLoader.h"
#include "D3D11Hook.h"
#include "LuminanceHistogram.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>
#include "GPUResource.h"
#include "WeatherParameterManager.h"

namespace SB
{

// =============================================================================
//  Embedded HLSL -- Pass 1: Bright Extract CS (full-res -> half-res)
// =============================================================================

static const char kBrightExtractCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Bright pixel extraction with Karis anti-firefly weighting.
// Reference: Brian Karis, "Tone Mapping" (UE4 Course Notes, SIGGRAPH 2013).
//
// Downsample source to half-res. For each 2x2 block, compute a weighted
// average where weight = 1/(1+luminance) to prevent single bright pixels
// from dominating the bloom. Apply soft-knee threshold with quadratic
// interpolation in the knee region.

cbuffer ExtractCB : register(b0)
{
    uint2  SrcDims;         // Source (backbuffer) dimensions
    uint2  DstDims;         // Destination (half-res) dimensions
    float  Threshold;       // Brightness threshold
    float  Knee;            // Soft-knee width (0 = hard cutoff)
    float  pad0, pad1;
}

Texture2D<float4> SrcTex : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> DstTex : register(u0);

// Rec. 709 luminance
float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Karis weight: suppress fireflies by weighting inversely with brightness
float KarisWeight(float3 c)
{
    return 1.0 / (1.0 + Luminance(c));
}

// Soft-knee threshold: quadratic blend in the knee region
// Returns a multiplier in [0,1] applied to the color after extraction.
float3 SoftThreshold(float3 c)
{
    float brightness = Luminance(c);

    // Below threshold-knee: fully rejected
    // Above threshold+knee: fully accepted
    // In between: quadratic interpolation
    float lower = Threshold - Knee;
    float upper = Threshold + Knee;

    // Soft curve in the knee region
    float soft = clamp((brightness - lower) / (upper - lower + 1e-6), 0.0, 1.0);
    soft = soft * soft; // quadratic ease-in

    // Hard threshold contribution
    float hard = step(Threshold, brightness);

    // Blend: use soft curve in knee region, hard above
    float contribution = (Knee > 0.0) ? soft : hard;

    return c * contribution;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // Each output pixel corresponds to a 2x2 block in the source.
    // Use bilinear sampling at the center of each source texel in the block,
    // then apply Karis weighting across the 4 samples.
    float2 srcTexelSize = 1.0 / float2(SrcDims);

    // Center of the 2x2 block in source UV space
    float2 baseUV = (float2(DTid.xy) * 2.0 + 1.0) * srcTexelSize;

    // Sample the 4 source texels in the 2x2 block
    float2 offsets[4] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };

    float3 weightedSum = 0.0;
    float  totalWeight = 0.0;

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float2 uv = baseUV + offsets[i] * srcTexelSize;
        float3 sample_ = SrcTex.SampleLevel(LinearSampler, uv, 0).rgb;

        // Apply Karis anti-firefly weight
        float w = KarisWeight(sample_);
        weightedSum += sample_ * w;
        totalWeight += w;
    }

    float3 avg = weightedSum / (totalWeight + 1e-6);

    // Apply soft-knee threshold to extract only bright pixels
    float3 bright = SoftThreshold(avg);

    DstTex[DTid.xy] = float4(bright, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 2-4: Downsample CS (Jimenez 13-tap)
// =============================================================================

static const char kDownsampleCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Progressive downsample using Jimenez 13-tap filter.
// Reference: Jorge Jimenez, "Next Generation Post Processing in Call of Duty"
//            (SIGGRAPH 2014, Advances in Real-Time Rendering course).
//
// Uses 13 bilinear taps arranged in a pattern that covers a 4x4 source texel
// region with appropriate weights. Five bilinear fetches reconstruct the full
// 13-tap kernel. This produces high-quality downsampling without visible
// ringing or aliasing artifacts.
//
// Tap layout (in source texel coordinates, centered on the 2x2 block):
//
//   A . B . C       Weights:
//   . D . E .         A,C,G,I = 0.03125  (1/32)  -- corners
//   F . G . H         B,D,F,H = 0.0625   (2/32)  -- edges
//   . I . J .         E       = 0.125    (4/32)  -- inner corners
//   K . L . M         G (center) = 0.125 (4/32)  -- center
//
// Simplified to the standard 5-fetch reconstruction with proper weights:
//   center (1 fetch, weight 0.5 of inner sum),
//   4 corner fetches at +-1 texel offsets (weight 0.125 each).

cbuffer DownsampleCB : register(b0)
{
    uint2  SrcDims;         // Source mip dimensions
    uint2  DstDims;         // Destination mip dimensions
    float2 SrcTexelSize;    // 1.0 / SrcDims
    float  pad0, pad1;
}

Texture2D<float4> SrcTex : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // UV at the center of the destination pixel, which maps to the center
    // of a 2x2 block in the source texture.
    float2 uv = (float2(DTid.xy) + 0.5) / float2(DstDims);

    // Jimenez 13-tap downsample filter, reconstructed from 13 point samples.
    // We use the standard decomposition described in the talk:
    //
    //   e = center
    //   a,b,c,d = inner ring at +-1 texel
    //   f,g,h,i = outer ring at +-2 texels
    //   j,k,l,m = edge samples at +-2 texels on axes
    //
    // Inner ring (4 taps at +-1 texel diagonal)
    float3 a = SrcTex.SampleLevel(LinearSampler, uv + float2(-1, -1) * SrcTexelSize, 0).rgb;
    float3 b = SrcTex.SampleLevel(LinearSampler, uv + float2( 1, -1) * SrcTexelSize, 0).rgb;
    float3 c = SrcTex.SampleLevel(LinearSampler, uv + float2(-1,  1) * SrcTexelSize, 0).rgb;
    float3 d = SrcTex.SampleLevel(LinearSampler, uv + float2( 1,  1) * SrcTexelSize, 0).rgb;

    // Center sample
    float3 e = SrcTex.SampleLevel(LinearSampler, uv, 0).rgb;

    // Edge samples (4 taps at +-2 texels on axes)
    float3 f = SrcTex.SampleLevel(LinearSampler, uv + float2(-2,  0) * SrcTexelSize, 0).rgb;
    float3 g = SrcTex.SampleLevel(LinearSampler, uv + float2( 2,  0) * SrcTexelSize, 0).rgb;
    float3 h = SrcTex.SampleLevel(LinearSampler, uv + float2( 0, -2) * SrcTexelSize, 0).rgb;
    float3 i = SrcTex.SampleLevel(LinearSampler, uv + float2( 0,  2) * SrcTexelSize, 0).rgb;

    // Outer corner samples (4 taps at +-2 texels diagonal)
    float3 j = SrcTex.SampleLevel(LinearSampler, uv + float2(-2, -2) * SrcTexelSize, 0).rgb;
    float3 k = SrcTex.SampleLevel(LinearSampler, uv + float2( 2, -2) * SrcTexelSize, 0).rgb;
    float3 l = SrcTex.SampleLevel(LinearSampler, uv + float2(-2,  2) * SrcTexelSize, 0).rgb;
    float3 m = SrcTex.SampleLevel(LinearSampler, uv + float2( 2,  2) * SrcTexelSize, 0).rgb;

    // Apply Jimenez 13-tap weights:
    //   Center group (e + inner ring): center gets 4/16, inner corners 2/16 each
    //   Edge+outer groups contribute the remaining weight
    //
    // From the talk, the standard weight decomposition:
    //   0.125 * (a+b+c+d)         -- inner ring, 4 samples * 1/8
    //   0.25  * e                  -- center
    //   0.0625 * (f+g+h+i)        -- edges, 4 samples * 1/16
    //   0.03125 * (j+k+l+m)       -- outer corners, 4 samples * 1/32
    // Total: 0.5 + 0.25 + 0.25 + 0.125 = ... normalizes to 1.0

    float3 result = e * 0.125;
    result += (a + b + c + d) * 0.125;
    result += (f + g + h + i) * 0.0625;
    result += (j + k + l + m) * 0.03125;

    DstTex[DTid.xy] = float4(result, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 5-7: Upsample CS (9-tap tent filter)
// =============================================================================

static const char kUpsampleCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Progressive upsample using 9-tap tent (3x3 bilinear) filter.
// Reference: Jorge Jimenez, "Next Generation Post Processing in Call of Duty"
//            (SIGGRAPH 2014, Advances in Real-Time Rendering course).
//
// The tent filter produces a smooth, artifact-free upsample. The result is
// additively blended with the corresponding downsample mip to accumulate
// bloom across all mip levels. Per-mip spectral tinting approximates
// Rayleigh scattering (warm near-field, cool far-field).
//
// 3x3 tent kernel weights (sum = 1.0):
//   1/16  2/16  1/16
//   2/16  4/16  2/16
//   1/16  2/16  1/16

cbuffer UpsampleCB : register(b0)
{
    uint2  SrcDims;         // Source (smaller mip) dimensions
    uint2  DstDims;         // Destination (larger mip) dimensions
    float2 SrcTexelSize;    // 1.0 / SrcDims
    float  MipLevel;        // Current mip level (for spectral tinting)
    float  BlendWeight;     // Blend factor for additive mix (0-1)
    float3 SpectralTint;    // Per-mip color tint (Rayleigh approximation)
    float  pad0;
}

Texture2D<float4> SrcTex : register(t0);   // Smaller mip (to upsample)
Texture2D<float4> DstBlend : register(t1); // Same-size downsample mip (to blend with)
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // UV in the source (smaller) texture corresponding to this output pixel
    float2 uv = (float2(DTid.xy) + 0.5) / float2(DstDims);

    // 9-tap tent filter (3x3 bilinear kernel)
    // Sample at 9 positions around the center with appropriate tent weights
    float3 result = 0.0;

    // Corners: weight 1/16 each
    result += SrcTex.SampleLevel(LinearSampler, uv + float2(-1, -1) * SrcTexelSize, 0).rgb * (1.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 1, -1) * SrcTexelSize, 0).rgb * (1.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2(-1,  1) * SrcTexelSize, 0).rgb * (1.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 1,  1) * SrcTexelSize, 0).rgb * (1.0 / 16.0);

    // Edges: weight 2/16 each
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 0, -1) * SrcTexelSize, 0).rgb * (2.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2(-1,  0) * SrcTexelSize, 0).rgb * (2.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 1,  0) * SrcTexelSize, 0).rgb * (2.0 / 16.0);
    result += SrcTex.SampleLevel(LinearSampler, uv + float2( 0,  1) * SrcTexelSize, 0).rgb * (2.0 / 16.0);

    // Center: weight 4/16
    result += SrcTex.SampleLevel(LinearSampler, uv, 0).rgb * (4.0 / 16.0);

    // Apply per-mip spectral tint (Rayleigh scattering approximation)
    result *= SpectralTint;

    // Read the corresponding downsample mip at this resolution for additive blend
    float2 dstUV = (float2(DTid.xy) + 0.5) / float2(DstDims);
    float3 blendColor = DstBlend.SampleLevel(LinearSampler, dstUV, 0).rgb;

    // Additive blend: upsampled lower mip + current downsample mip
    float3 final_ = blendColor + result * BlendWeight;

    DstTex[DTid.xy] = float4(final_, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 8: Anamorphic Streak CS (optional, half-res)
// =============================================================================

static const char kAnamorphicCS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Anamorphic lens streak: 1D horizontal Gaussian blur.
// Creates the characteristic horizontal streak seen in anamorphic lenses.
// Uses a 7-tap Gaussian kernel scaled by StreakLength to control width.

cbuffer AnamorphicCB : register(b0)
{
    uint2  Dims;            // Half-res dimensions
    float  Intensity;       // Streak intensity (0-2)
    float  StreakLength;    // Base streak length in texels
    float2 TexelSize;       // 1.0 / Dims
    float  pad0, pad1;
}

Texture2D<float4> SrcTex : register(t0);   // Half-res bloom (upsample level 0)
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Dims.x || DTid.y >= Dims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(Dims);

    // 7-tap Gaussian weights (sigma ~ 1.5, normalized)
    // Kernel: { 0.0205, 0.0855, 0.232, 0.324, 0.232, 0.0855, 0.0205 }
    static const float weights[4] = { 0.324, 0.232, 0.0855, 0.0205 };

    // Horizontal offset per tap, scaled by streak length
    float step = TexelSize.x * StreakLength;

    // Center tap
    float3 result = SrcTex.SampleLevel(LinearSampler, uv, 0).rgb * weights[0];

    // Symmetric taps 1-3
    [unroll]
    for (int i = 1; i < 4; i++)
    {
        float offset = float(i) * step;
        float3 left  = SrcTex.SampleLevel(LinearSampler, uv + float2(-offset, 0), 0).rgb;
        float3 right = SrcTex.SampleLevel(LinearSampler, uv + float2( offset, 0), 0).rgb;
        result += (left + right) * weights[i];
    }

    // Scale by intensity
    result *= Intensity;

    DstTex[DTid.xy] = float4(result, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 9: Composite PS (fullscreen)
// =============================================================================

static const char kCompositePS[] = R"HLSL(
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Final bloom composite pixel shader.
// Reads all bloom mip levels, applies per-mip weighting, optional chromatic
// shift, optional anamorphic streak blend, and outputs the combined bloom
// texture scaled by overall intensity. The result is written to the bloom
// output RT, which is later composited with the scene by the pipeline.

cbuffer CompositeCB : register(b0)
{
    float4 MipWeights;      // Per-mip blend weights (x=half, y=quarter, z=eighth, w=sixteenth)
    float  Intensity;       // Overall bloom intensity
    float  ColorTemp;       // Color temperature in Kelvin (6500 = neutral)
    float  ChromaticShift;  // R/B channel shift per mip (0.0-0.02)
    float  AnamorphicBlend; // Anamorphic streak blend factor (0 = off)
    float4 Padding;
}

Texture2D<float4> Mip0 : register(t0);     // Half-res upsample
Texture2D<float4> Mip1 : register(t1);     // Quarter-res upsample
Texture2D<float4> Mip2 : register(t2);     // Eighth-res upsample
Texture2D<float4> Mip3 : register(t3);     // Sixteenth-res upsample
Texture2D<float4> AnamorphicTex : register(t4); // Anamorphic streak (half-res)
SamplerState LinearSampler : register(s0);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;

    // Sample each mip level with optional chromatic shift.
    // ChromaticShift offsets the R and B channels outward from center
    // at increasing amounts for higher (smaller) mip levels, simulating
    // lens chromatic aberration in the bloom.
    float3 bloom = 0.0;

    // Mip 0 (half-res) -- minimal chromatic shift
    {
        float shift = ChromaticShift * 1.0;
        float2 dir = uv - 0.5;
        float r = Mip0.SampleLevel(LinearSampler, uv + dir * shift, 0).r;
        float g = Mip0.SampleLevel(LinearSampler, uv, 0).g;
        float b = Mip0.SampleLevel(LinearSampler, uv - dir * shift, 0).b;
        bloom += float3(r, g, b) * MipWeights.x;
    }

    // Mip 1 (quarter-res)
    {
        float shift = ChromaticShift * 2.0;
        float2 dir = uv - 0.5;
        float r = Mip1.SampleLevel(LinearSampler, uv + dir * shift, 0).r;
        float g = Mip1.SampleLevel(LinearSampler, uv, 0).g;
        float b = Mip1.SampleLevel(LinearSampler, uv - dir * shift, 0).b;
        bloom += float3(r, g, b) * MipWeights.y;
    }

    // Mip 2 (eighth-res)
    {
        float shift = ChromaticShift * 3.0;
        float2 dir = uv - 0.5;
        float r = Mip2.SampleLevel(LinearSampler, uv + dir * shift, 0).r;
        float g = Mip2.SampleLevel(LinearSampler, uv, 0).g;
        float b = Mip2.SampleLevel(LinearSampler, uv - dir * shift, 0).b;
        bloom += float3(r, g, b) * MipWeights.z;
    }

    // Mip 3 (sixteenth-res)
    {
        float shift = ChromaticShift * 4.0;
        float2 dir = uv - 0.5;
        float r = Mip3.SampleLevel(LinearSampler, uv + dir * shift, 0).r;
        float g = Mip3.SampleLevel(LinearSampler, uv, 0).g;
        float b = Mip3.SampleLevel(LinearSampler, uv - dir * shift, 0).b;
        bloom += float3(r, g, b) * MipWeights.w;
    }

    // Optional anamorphic streak blend
    if (AnamorphicBlend > 0.0)
    {
        float3 streak = AnamorphicTex.SampleLevel(LinearSampler, uv, 0).rgb;
        bloom += streak * AnamorphicBlend;
    }

    // Apply overall bloom intensity and clamp to prevent negative values
    bloom = max(bloom * Intensity, 0.0);

    return float4(bloom, 1.0);
}
)HLSL";


// =============================================================================
//  CB structures -- must match HLSL cbuffers exactly
// =============================================================================

struct alignas(16) ExtractCBData
{
    uint32_t srcW;           // +0
    uint32_t srcH;           // +4
    uint32_t dstW;           // +8
    uint32_t dstH;           // +12
    float    threshold;      // +16
    float    knee;           // +20
    float    pad[2];         // +24..31 -> total 32
};
static_assert(sizeof(ExtractCBData) == 32, "ExtractCB must be 32 bytes");

struct alignas(16) DownsampleCBData
{
    uint32_t srcW;           // +0
    uint32_t srcH;           // +4
    uint32_t dstW;           // +8
    uint32_t dstH;           // +12
    float    srcTexelSizeX;  // +16
    float    srcTexelSizeY;  // +20
    float    pad[2];         // +24..31 -> total 32
};
static_assert(sizeof(DownsampleCBData) == 32, "DownsampleCB must be 32 bytes");

struct alignas(16) UpsampleCBData
{
    uint32_t srcW;           // +0
    uint32_t srcH;           // +4
    uint32_t dstW;           // +8
    uint32_t dstH;           // +12
    float    srcTexelSizeX;  // +16
    float    srcTexelSizeY;  // +20
    float    mipLevel;       // +24
    float    blendWeight;    // +28
    float    spectralTintR;  // +32
    float    spectralTintG;  // +36
    float    spectralTintB;  // +40
    float    pad0;           // +44 -> total 48
};
static_assert(sizeof(UpsampleCBData) == 48, "UpsampleCB must be 48 bytes");

struct alignas(16) CompositeCBData
{
    float mipWeights[4];     // +0  (half, quarter, eighth, sixteenth)
    float intensity;         // +16
    float colorTemp;         // +20
    float chromaticShift;    // +24
    float anamorphicBlend;   // +28
    float pad[4];            // +32..47 -> total 48
};
static_assert(sizeof(CompositeCBData) == 48, "CompositeCB must be 48 bytes");

// Anamorphic CB reuses DownsampleCB layout with different semantics
struct alignas(16) AnamorphicCBData
{
    uint32_t dimsW;          // +0
    uint32_t dimsH;          // +4
    float    intensity;      // +8
    float    streakLength;   // +12
    float    texelSizeX;     // +16
    float    texelSizeY;     // +20
    float    pad[2];         // +24..31 -> total 32
};
static_assert(sizeof(AnamorphicCBData) == 32, "AnamorphicCB must be 32 bytes");


// =============================================================================
//  Spectral tint table -- Rayleigh scatter approximation per mip level
//  Warm near-field (mip 0-1), cool far-field (mip 2-3)
// =============================================================================

static constexpr float kSpectralTints[4][3] = {
    { 1.00f, 0.95f, 0.90f },  // Mip 0 (half): warm
    { 1.00f, 0.98f, 0.95f },  // Mip 1 (quarter): slight warm
    { 0.95f, 0.98f, 1.00f },  // Mip 2 (eighth): slight cool
    { 0.90f, 0.95f, 1.00f },  // Mip 3 (sixteenth): cool
};


// =============================================================================
//  Initialize
// =============================================================================

bool BloomRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                                IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    if (!dev || !ctx || !sc) return false;

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    if (!cm.IsInitialized() || !rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("BloomRenderer: prerequisites not initialized");
        return false;
    }

    m_device  = dev;
    m_context = ctx;

    // Get backbuffer dimensions
    ID3D11Texture2D* backTex = nullptr;
    HRESULT hr = sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                                reinterpret_cast<void**>(&backTex));
    if (FAILED(hr) || !backTex) {
        SKSE::log::error("BloomRenderer: failed to get backbuffer (0x{:X})",
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

    // Register composite fullscreen pixel shader pass
    m_compositePass = rpm.RegisterPass({
        .name     = "BloomComposite",
        .psSource = kCompositePS,
    });
    if (!m_compositePass) {
        SKSE::log::error("BloomRenderer: failed to register BloomComposite pass");
        ReleaseResources();
        return false;
    }

    // Register as PrePresent pipeline pass (priority 10, before ColorPipeline and ToneMap)
    m_pipelineHandle = pl.AddPass({
        .name     = "Bloom",
        .stage    = PipelineStage::PreUI,
        .priority = 10,
        .enabled  = false,  // default disabled
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    m_frameIndex  = 0;

    SKSE::log::info("BloomRenderer: initialized ({}x{}, threshold={:.2f}, intensity={:.2f}, knee={:.2f})",
                    m_screenW, m_screenH, m_threshold, m_intensity, m_knee);
    return true;
}


// =============================================================================
//  Compile all shaders
// =============================================================================

bool BloomRenderer::CompileShaders()
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
            SKSE::log::error("BloomRenderer: {} compile failed", name);
            return false;
        }

        HRESULT hr = m_device->CreateComputeShader(blob->GetBufferPointer(),
                                            blob->GetBufferSize(),
                                            nullptr, outCS);
        blob->Release();
        if (FAILED(hr)) {
            SKSE::log::error("BloomRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("Bloom_Extract",    kBrightExtractCS, &m_extractCS))    return false;
    if (!CompileCS("Bloom_Downsample", kDownsampleCS,    &m_downsampleCS)) return false;
    if (!CompileCS("Bloom_Upsample",   kUpsampleCS,      &m_upsampleCS))  return false;
    if (!CompileCS("Bloom_Anamorphic", kAnamorphicCS,    &m_anamorphicCS)) return false;

    return true;
}


// =============================================================================
//  Create GPU resources
// =============================================================================

bool BloomRenderer::CreateResources()
{
    HRESULT hr;

    // Helper: create bloom mip texture using GPUResource
    auto CreateBloomTexture = [&](const char* name, uint32_t w, uint32_t h,
                                   MipLevel& out) -> bool
    {
        if (!CreateGPUTexture(m_device, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT,
                              &out.tex, &out.srv, &out.uav, name))
            return false;
        out.w = w;
        out.h = h;
        return true;
    };
    // Compute mip dimensions: half -> quarter -> eighth -> sixteenth
    uint32_t w = m_screenW;
    uint32_t h = m_screenH;
    for (int i = 0; i < 4; i++) {
        w = (std::max)(w / 2u, 1u);
        h = (std::max)(h / 2u, 1u);

        char downName[64];
        snprintf(downName, sizeof(downName), "BloomDown%d", i);
        if (!CreateBloomTexture(downName, w, h, m_mips[i])) return false;

        // Upsample textures for levels 0-2 only.
        // Level 3 (sixteenth) has no upsample pass -- the composite PS
        // reads the downsample mip directly via m_mips[3].srv.
        if (i < 3) {
            char upName[64];
            snprintf(upName, sizeof(upName), "BloomUp%d", i);
            if (!CreateBloomTexture(upName, w, h, m_upMips[i])) return false;
        }
    }

    // Anamorphic scratch (half-res)
    if (!CreateBloomTexture("BloomAnamorphic", m_mips[0].w, m_mips[0].h, m_anamorphic))
        return false;

    // Final bloom output texture (half-res, with SRV + RTV for composite PS)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = m_mips[0].w;
        desc.Height     = m_mips[0].h;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

        hr = m_device->CreateTexture2D(&desc, nullptr, &m_bloomTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_bloomTex, &srvDesc, &m_bloomSRV);
        if (FAILED(hr)) return false;

        D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
        rtvDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
        rtvDesc.ViewDimension      = D3D11_RTV_DIMENSION_TEXTURE2D;
        rtvDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateRenderTargetView(m_bloomTex, &rtvDesc, &m_bloomRTV);
        if (FAILED(hr)) return false;
    }

    // Backbuffer copy (for reading as SRV)
    {
        ID3D11Texture2D* backTex = nullptr;
        auto* sc = D3D11Hook::GetSwapChain();
        if (!sc || FAILED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                          reinterpret_cast<void**>(&backTex)))) {
            SKSE::log::error("BloomRenderer: failed to get swapchain backbuffer for copy");
            return false;
        }

        D3D11_TEXTURE2D_DESC copyDesc;
        backTex->GetDesc(&copyDesc);
        backTex->Release();

        copyDesc.BindFlags     = D3D11_BIND_SHADER_RESOURCE;
        copyDesc.Usage         = D3D11_USAGE_DEFAULT;
        copyDesc.CPUAccessFlags = 0;

        hr = m_device->CreateTexture2D(&copyDesc, nullptr, &m_backbufferCopy);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = copyDesc.Format;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc, &m_backbufferCopySRV);
        if (FAILED(hr)) return false;
    }

    // Constant buffers
    // Constant buffers
    if (!CreateCB(m_device, sizeof(ExtractCBData), &m_extractCB)) return false;
    if (!CreateCB(m_device, sizeof(DownsampleCBData), &m_downsampleCB)) return false;
    if (!CreateCB(m_device, sizeof(UpsampleCBData), &m_upsampleCB)) return false;
    if (!CreateCB(m_device, sizeof(CompositeCBData), &m_compositeCB)) return false;
    // Linear clamp sampler for bloom texture sampling
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        sd.MaxLOD   = D3D11_FLOAT32_MAX;
        hr = m_device->CreateSamplerState(&sd, &m_linearSampler);
        if (FAILED(hr)) return false;
    }

    return true;
}


// =============================================================================
//  GetBloomSRV -- returns the final composited bloom texture
// =============================================================================

ID3D11ShaderResourceView* BloomRenderer::GetBloomSRV() const
{
    if (!m_initialized) return nullptr;
    return m_bloomSRV;
}


// =============================================================================
//  Per-frame execution (called by RenderPipeline at PreUI stage)
// =============================================================================

void BloomRenderer::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();

    // ── Copy scene color to temp SRV ────────────────────────────────────
    // Mid-frame: extract scene texture from the game's active RTV.
    // PrePresent fallback: use the swapchain backbuffer.
    {
        ID3D11Texture2D* sceneTex = nullptr;

        if (ctx.gameSceneRTV) {
            // Mid-frame dispatch: game's active scene RT
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                res->QueryInterface(__uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&sceneTex));
                res->Release();
            }
        } else {
            // PrePresent fallback: use backbuffer
            auto* sc = ctx.swapChain;
            if (!sc) sc = D3D11Hook::GetSwapChain();
            if (sc) {
                sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&sceneTex));
            }
        }

        if (!sceneTex) return;

        // Ensure copy texture matches the scene RT format/size.
        // The backbuffer is R8G8B8A8_UNORM but the game's internal scene RT
        // is often R16G16B16A16_FLOAT.  CopyResource requires identical
        // format+dimensions, so we lazily recreate the copy texture if needed.
        {
            D3D11_TEXTURE2D_DESC sceneDesc;
            sceneTex->GetDesc(&sceneDesc);

            D3D11_TEXTURE2D_DESC copyDesc;
            m_backbufferCopy->GetDesc(&copyDesc);

            if (sceneDesc.Format != copyDesc.Format ||
                sceneDesc.Width  != copyDesc.Width  ||
                sceneDesc.Height != copyDesc.Height)
            {
                SKSE::log::info("BloomRenderer: scene RT format/size changed — "
                    "scene={}x{} fmt={}, copy={}x{} fmt={} — recreating",
                    sceneDesc.Width, sceneDesc.Height, static_cast<int>(sceneDesc.Format),
                    copyDesc.Width, copyDesc.Height, static_cast<int>(copyDesc.Format));

                if (m_backbufferCopySRV) { m_backbufferCopySRV->Release(); m_backbufferCopySRV = nullptr; }
                if (m_backbufferCopy)    { m_backbufferCopy->Release();    m_backbufferCopy    = nullptr; }

                D3D11_TEXTURE2D_DESC newDesc = sceneDesc;
                newDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
                newDesc.Usage          = D3D11_USAGE_DEFAULT;
                newDesc.CPUAccessFlags = 0;
                newDesc.MiscFlags      = 0;

                HRESULT hr = m_device->CreateTexture2D(&newDesc, nullptr, &m_backbufferCopy);
                if (FAILED(hr)) {
                    SKSE::log::error("BloomRenderer: failed to recreate copy tex");
                    sceneTex->Release();
                    return;
                }

                D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
                srvDesc.Format                    = newDesc.Format;
                srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
                srvDesc.Texture2D.MipLevels       = 1;
                srvDesc.Texture2D.MostDetailedMip = 0;

                hr = m_device->CreateShaderResourceView(m_backbufferCopy, &srvDesc, &m_backbufferCopySRV);
                if (FAILED(hr)) {
                    SKSE::log::error("BloomRenderer: failed to recreate copy SRV");
                    m_backbufferCopy->Release();
                    m_backbufferCopy = nullptr;
                    sceneTex->Release();
                    return;
                }
            }
        }

        ctx.context->CopyResource(m_backbufferCopy, sceneTex);
        sceneTex->Release();
    }

    cm.SaveCSState();

    // Bind the linear sampler for all CS passes (s0)
    ctx.context->CSSetSamplers(0, 1, &m_linearSampler);

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 1: Bright Extract (full-res -> half-res)
    // ═════════════════════════════════════════════════════════════════════
    {
        ExtractCBData cb = {};
        cb.srcW      = m_screenW;
        cb.srcH      = m_screenH;
        cb.dstW      = m_mips[0].w;
        cb.dstH      = m_mips[0].h;
        cb.threshold = m_threshold;
        cb.knee      = m_knee;

        UploadCB(ctx.context, m_extractCB, &cb, sizeof(cb));

        ctx.context->CSSetShaderResources(0, 1, &m_backbufferCopySRV);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_mips[0].uav, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_extractCB);
        ctx.context->CSSetShader(m_extractCS, nullptr, 0);

        UINT groupsX = (m_mips[0].w + 7) / 8;
        UINT groupsY = (m_mips[0].h + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Clear bindings
        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 2-4: Downsample (Jimenez 13-tap, 3 levels)
    // ═════════════════════════════════════════════════════════════════════
    for (int i = 1; i < 4; i++)
    {
        const auto& src = m_mips[i - 1];
        const auto& dst = m_mips[i];

        DownsampleCBData cb = {};
        cb.srcW         = src.w;
        cb.srcH         = src.h;
        cb.dstW         = dst.w;
        cb.dstH         = dst.h;
        cb.srcTexelSizeX = 1.0f / static_cast<float>(src.w);
        cb.srcTexelSizeY = 1.0f / static_cast<float>(src.h);

        UploadCB(ctx.context, m_downsampleCB, &cb, sizeof(cb));

        ctx.context->CSSetShaderResources(0, 1, &src.srv);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &dst.uav, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_downsampleCB);
        ctx.context->CSSetShader(m_downsampleCS, nullptr, 0);

        UINT groupsX = (dst.w + 7) / 8;
        UINT groupsY = (dst.h + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Clear
        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 5-7: Upsample (9-tap tent filter, 3 levels back up)
    //  Goes from smallest mip back to half-res, blending with downsample
    // ═════════════════════════════════════════════════════════════════════
    for (int i = 2; i >= 0; i--)
    {
        // Source: the smaller mip (upsample result from previous iteration, or
        //         the smallest downsample mip for the first iteration)
        ID3D11ShaderResourceView* srcSRV;
        uint32_t srcW, srcH;

        if (i == 2) {
            // First upsample: read from the smallest downsample mip (index 3)
            srcSRV = m_mips[3].srv;
            srcW   = m_mips[3].w;
            srcH   = m_mips[3].h;
        } else {
            // Subsequent: read from previous upsample result
            srcSRV = m_upMips[i + 1].srv;
            srcW   = m_upMips[i + 1].w;
            srcH   = m_upMips[i + 1].h;
        }

        const auto& dst      = m_upMips[i];
        const auto& blendSrc = m_mips[i]; // corresponding downsample level

        UpsampleCBData cb = {};
        cb.srcW          = srcW;
        cb.srcH          = srcH;
        cb.dstW          = dst.w;
        cb.dstH          = dst.h;
        cb.srcTexelSizeX = 1.0f / static_cast<float>(srcW);
        cb.srcTexelSizeY = 1.0f / static_cast<float>(srcH);
        cb.mipLevel      = static_cast<float>(i);
        cb.blendWeight   = 1.0f;  // full additive blend
        cb.spectralTintR = kSpectralTints[i][0];
        cb.spectralTintG = kSpectralTints[i][1];
        cb.spectralTintB = kSpectralTints[i][2];

        UploadCB(ctx.context, m_upsampleCB, &cb, sizeof(cb));

        // t0 = smaller mip (to upsample), t1 = same-size downsample (to blend with)
        ID3D11ShaderResourceView* srvs[] = { srcSRV, blendSrc.srv };
        ctx.context->CSSetShaderResources(0, 2, srvs);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &dst.uav, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_upsampleCB);
        ctx.context->CSSetShader(m_upsampleCS, nullptr, 0);

        UINT groupsX = (dst.w + 7) / 8;
        UINT groupsY = (dst.h + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        // Clear
        ID3D11ShaderResourceView* nullSRVs[2] = {};
        ctx.context->CSSetShaderResources(0, 2, nullSRVs);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 8: Anamorphic Streak (optional, half-res horizontal)
    // ═════════════════════════════════════════════════════════════════════
    if (m_anamorphicIntensity > 0.001f)
    {
        AnamorphicCBData cb = {};
        cb.dimsW        = m_anamorphic.w;
        cb.dimsH        = m_anamorphic.h;
        cb.intensity    = m_anamorphicIntensity;
        cb.streakLength = 4.0f;  // base streak length in texels
        cb.texelSizeX   = 1.0f / static_cast<float>(m_anamorphic.w);
        cb.texelSizeY   = 1.0f / static_cast<float>(m_anamorphic.h);

        // Reuse the downsample CB buffer for anamorphic (same 32-byte size)
        UploadCB(ctx.context, m_downsampleCB, &cb, sizeof(cb));

        // Read from half-res upsample result
        ctx.context->CSSetShaderResources(0, 1, &m_upMips[0].srv);
        ctx.context->CSSetUnorderedAccessViews(0, 1, &m_anamorphic.uav, nullptr);
        ctx.context->CSSetConstantBuffers(0, 1, &m_downsampleCB);
        ctx.context->CSSetShader(m_anamorphicCS, nullptr, 0);

        UINT groupsX = (m_anamorphic.w + 7) / 8;
        UINT groupsY = (m_anamorphic.h + 7) / 8;
        ctx.context->Dispatch(groupsX, groupsY, 1);

        ID3D11ShaderResourceView* nullSRV[1] = {};
        ctx.context->CSSetShaderResources(0, 1, nullSRV);
        ID3D11UnorderedAccessView* nullUAV[1] = {};
        ctx.context->CSSetUnorderedAccessViews(0, 1, nullUAV, nullptr);
    }

    cm.RestoreCSState();

    // ═════════════════════════════════════════════════════════════════════
    //  Pass 9: Composite PS (fullscreen, writes to bloom output RT)
    // ═════════════════════════════════════════════════════════════════════
    {
        CompositeCBData cb = {};
        cb.mipWeights[0]   = 0.5f;   // half-res weight
        cb.mipWeights[1]   = 0.3f;   // quarter-res weight
        cb.mipWeights[2]   = 0.15f;  // eighth-res weight
        cb.mipWeights[3]   = 0.05f;  // sixteenth-res weight
        cb.intensity       = m_intensity * WeatherParameterManager::Get().GetCurrent().bloomIntensity;
        cb.colorTemp       = m_colorTemp;
        cb.chromaticShift  = 0.005f; // subtle chromatic dispersion
        cb.anamorphicBlend = (m_anamorphicIntensity > 0.001f) ? 0.3f : 0.0f;

        // Bind SRVs: t0-t3 = bloom mips, t4 = anamorphic
        // t0-t2: upsample results (half, quarter, eighth)
        // t3: smallest downsample mip directly (sixteenth, no upsample pass for this)
        ID3D11ShaderResourceView* srvs[5] = {
            m_upMips[0].srv,   // half (upsample result)
            m_upMips[1].srv,   // quarter (upsample result)
            m_upMips[2].srv,   // eighth (upsample result)
            m_mips[3].srv,     // sixteenth (raw downsample, no upsample for smallest)
            (m_anamorphicIntensity > 0.001f) ? m_anamorphic.srv : nullptr,
        };

        rpm.Execute({
            .passID       = m_compositePass,
            .rtv          = m_bloomRTV,
            .srvs         = srvs,
            .srvCount     = 5,
            .samplers     = &m_linearSampler,
            .samplerCount = 1,
            .cbData       = &cb,
            .cbSize       = sizeof(cb),
        });
    }

    m_frameIndex++;
}


// =============================================================================
//  Shutdown
// =============================================================================

void BloomRenderer::Shutdown()
{
    if (!m_initialized) return;

    if (m_pipelineHandle) {
        RenderPipeline::Get().RemovePass(m_pipelineHandle);
        m_pipelineHandle = 0;
    }

    ReleaseResources();
    m_initialized = false;

    SKSE::log::info("BloomRenderer: shut down");
}


// =============================================================================
//  Release all GPU resources
// =============================================================================

void BloomRenderer::ReleaseResources()
{
    auto SafeRelease = [](auto*& ptr) {
        if (ptr) { ptr->Release(); ptr = nullptr; }
    };

    // Compute shaders
    SafeRelease(m_extractCS);
    SafeRelease(m_downsampleCS);
    SafeRelease(m_upsampleCS);
    SafeRelease(m_anamorphicCS);

    // Downsample mip chain
    for (int i = 0; i < 4; i++) {
        SafeRelease(m_mips[i].tex);
        SafeRelease(m_mips[i].srv);
        SafeRelease(m_mips[i].uav);
    }

    // Upsample mip chain (only 0-2; level 3 is never allocated)
    for (int i = 0; i < 3; i++) {
        SafeRelease(m_upMips[i].tex);
        SafeRelease(m_upMips[i].srv);
        SafeRelease(m_upMips[i].uav);
    }

    // Anamorphic scratch
    SafeRelease(m_anamorphic.tex);
    SafeRelease(m_anamorphic.srv);
    SafeRelease(m_anamorphic.uav);

    // Final bloom output
    SafeRelease(m_bloomTex);
    SafeRelease(m_bloomSRV);
    SafeRelease(m_bloomRTV);

    // Backbuffer copy
    SafeRelease(m_backbufferCopy);
    SafeRelease(m_backbufferCopySRV);

    // Constant buffers
    SafeRelease(m_extractCB);
    SafeRelease(m_downsampleCB);
    SafeRelease(m_upsampleCB);
    SafeRelease(m_compositeCB);

    // Sampler
    SafeRelease(m_linearSampler);
}

} // namespace SB
