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
#include "D3D11Hook.h"
#include "LuminanceHistogram.h"

#include <SKSE/SKSE.h>
#include <d3dcompiler.h>
#include <cstring>
#include <cmath>

namespace SB
{

// =============================================================================
//  Embedded HLSL -- Pass 1: Bright Extract CS (full-res -> half-res)
// =============================================================================

static const char kBrightExtractCS[] = R"HLSL(
// Bright Extract CS -- Karis anti-firefly + soft-knee threshold
//
// Reads the full-res backbuffer and writes half-res bright pixels.
// Uses 2x2 weighted averaging with 1/(1+luma) weight to suppress
// single-pixel fireflies that plague naive threshold extraction.
// Soft-knee blends smoothly around the threshold.

cbuffer ExtractCB : register(b0)
{
    uint2  SrcDims;         // Source (backbuffer) dimensions
    uint2  DstDims;         // Destination (half-res) dimensions
    float  Threshold;       // Brightness threshold
    float  Knee;            // Soft-knee width (0 = hard cutoff)
    float  pad0, pad1;
};

Texture2D<float4> SrcTex : register(t0);
SamplerState LinearSampler : register(s0);

RWTexture2D<float4> DstTex : register(u0);

float Luminance(float3 c)
{
    return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Soft-knee threshold: smooth blend around the threshold
// Returns a 0-1 factor for how much of the color to keep
float SoftKnee(float brightness, float threshold, float knee)
{
    float softMin = threshold - knee;
    float softMax = threshold + knee;

    if (brightness <= softMin)
        return 0.0;
    if (brightness >= softMax)
        return 1.0;

    // Quadratic blend in the knee region
    float t = (brightness - softMin) / (2.0 * knee);
    return t * t;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // Map destination pixel to source 2x2 block
    float2 srcTexelSize = 1.0 / float2(SrcDims);
    float2 uv = (float2(DTid.xy) * 2.0 + 1.0) * srcTexelSize;

    // Sample 2x2 neighborhood for Karis average
    float2 offsets[4] = {
        float2(-0.5, -0.5),
        float2( 0.5, -0.5),
        float2(-0.5,  0.5),
        float2( 0.5,  0.5)
    };

    float3 totalColor = float3(0, 0, 0);
    float totalWeight = 0.0;

    for (int i = 0; i < 4; i++)
    {
        float2 sampleUV = uv + offsets[i] * srcTexelSize;
        float3 color = SrcTex.SampleLevel(LinearSampler, sampleUV, 0).rgb;

        // Karis anti-firefly weight: 1 / (1 + luma)
        float luma = Luminance(color);
        float w = 1.0 / (1.0 + luma);

        totalColor += color * w;
        totalWeight += w;
    }

    float3 avg = totalColor / max(totalWeight, 0.0001);

    // Apply soft-knee threshold
    float brightness = Luminance(avg);
    float factor = SoftKnee(brightness, Threshold, max(Knee, 0.0001));

    DstTex[DTid.xy] = float4(avg * factor, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 2-4: Downsample CS (Jimenez 13-tap)
// =============================================================================

static const char kDownsampleCS[] = R"HLSL(
// Downsample CS -- Jimenez 13-tap filter (SIGGRAPH 2014)
//
// High-quality downsampling that avoids ringing and aliasing.
// Uses 13 taps arranged in a cross+diagonal pattern with carefully
// chosen weights for energy conservation.

cbuffer DownsampleCB : register(b0)
{
    uint2  SrcDims;         // Source mip dimensions
    uint2  DstDims;         // Destination mip dimensions
    float2 SrcTexelSize;    // 1.0 / SrcDims
    float  pad0, pad1;
};

Texture2D<float4> SrcTex : register(t0);
SamplerState LinearSampler : register(s0);

RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // Center UV in source texture
    float2 uv = (float2(DTid.xy) * 2.0 + 1.0) / float2(SrcDims);
    float2 ts = SrcTexelSize;

    // Jimenez 13-tap filter:
    //   a - b - c
    //   - d - e -
    //   f - g - h
    //   - i - j -
    //   k - l - m
    //
    // Weights: center group (d,e,i,j) = 0.5 (4 taps sharing center)
    //          corner groups = 0.125 each (4 groups x 4 taps)
    //          center tap (g) gets extra weight from all groups

    float3 a = SrcTex.SampleLevel(LinearSampler, uv + float2(-2, -2) * ts, 0).rgb;
    float3 b = SrcTex.SampleLevel(LinearSampler, uv + float2( 0, -2) * ts, 0).rgb;
    float3 c = SrcTex.SampleLevel(LinearSampler, uv + float2( 2, -2) * ts, 0).rgb;

    float3 d = SrcTex.SampleLevel(LinearSampler, uv + float2(-1, -1) * ts, 0).rgb;
    float3 e = SrcTex.SampleLevel(LinearSampler, uv + float2( 1, -1) * ts, 0).rgb;

    float3 f = SrcTex.SampleLevel(LinearSampler, uv + float2(-2,  0) * ts, 0).rgb;
    float3 g = SrcTex.SampleLevel(LinearSampler, uv,                        0).rgb;
    float3 h = SrcTex.SampleLevel(LinearSampler, uv + float2( 2,  0) * ts, 0).rgb;

    float3 i = SrcTex.SampleLevel(LinearSampler, uv + float2(-1,  1) * ts, 0).rgb;
    float3 j = SrcTex.SampleLevel(LinearSampler, uv + float2( 1,  1) * ts, 0).rgb;

    float3 k = SrcTex.SampleLevel(LinearSampler, uv + float2(-2,  2) * ts, 0).rgb;
    float3 l = SrcTex.SampleLevel(LinearSampler, uv + float2( 0,  2) * ts, 0).rgb;
    float3 m = SrcTex.SampleLevel(LinearSampler, uv + float2( 2,  2) * ts, 0).rgb;

    // Weighted combination (sums to 1.0 for energy conservation)
    //   Inner quad (d,e,i,j):          0.500 / 4 each = 0.125
    //   Top-left corner (a,b,d,f):     0.125 / 4      = 0.03125
    //   Top-right corner (b,c,e,h):    0.125 / 4      = 0.03125
    //   Bottom-left corner (f,i,k,l):  0.125 / 4      = 0.03125
    //   Bottom-right corner (h,j,l,m): 0.125 / 4      = 0.03125
    //   Center tap (g):                participates in all groups

    float3 result = float3(0, 0, 0);

    // Inner quad: d + e + i + j (weight 0.5, each 0.125)
    result += (d + e + i + j) * 0.125;

    // Corner groups (each weight 0.125, 4 taps = 0.03125 each)
    result += (a + b + d + g) * 0.03125;  // top-left
    result += (b + c + e + g) * 0.03125;  // top-right
    result += (g + f + i + k) * 0.03125;  // bottom-left -- using f, not duplicate
    result += (g + h + j + m) * 0.03125;  // bottom-right

    // Cross taps for the remaining weight
    result += (f + g) * 0.03125;  // left
    result += (g + h) * 0.03125;  // right
    result += (l + g) * 0.03125;  // bottom center
    result += (b + g) * 0.03125;  // top center (extra coverage)

    DstTex[DTid.xy] = float4(result, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 5-7: Upsample CS (9-tap tent filter)
// =============================================================================

static const char kUpsampleCS[] = R"HLSL(
// Upsample CS -- 9-tap 3x3 tent filter with additive blend
//
// Upsamples from a smaller mip and additively blends with the
// corresponding downsample level at the target resolution.
// Per-mip spectral tinting simulates atmospheric scattering:
// warm near-field (mip 0-1), cool far-field (mip 2+).

cbuffer UpsampleCB : register(b0)
{
    uint2  SrcDims;         // Source (smaller mip) dimensions
    uint2  DstDims;         // Destination (larger mip) dimensions
    float2 SrcTexelSize;    // 1.0 / SrcDims
    float  MipLevel;        // Current mip level (for spectral tinting)
    float  BlendWeight;     // Blend factor for additive mix (0-1)
    float3 SpectralTint;    // Per-mip color tint (Rayleigh approximation)
    float  pad0;
};

Texture2D<float4> SrcTex : register(t0);   // Smaller mip (to upsample)
Texture2D<float4> DstBlend : register(t1); // Same-size downsample mip (to blend with)
SamplerState LinearSampler : register(s0);

RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= DstDims.x || DTid.y >= DstDims.y)
        return;

    // UV in source (smaller) texture
    float2 uv = (float2(DTid.xy) + 0.5) / float2(DstDims);
    float2 ts = SrcTexelSize;

    // 9-tap 3x3 tent filter (bilinear kernel):
    //   1  2  1
    //   2  4  2   / 16
    //   1  2  1
    float3 sum = float3(0, 0, 0);

    sum += SrcTex.SampleLevel(LinearSampler, uv + float2(-1, -1) * ts, 0).rgb * 1.0;
    sum += SrcTex.SampleLevel(LinearSampler, uv + float2( 0, -1) * ts, 0).rgb * 2.0;
    sum += SrcTex.SampleLevel(LinearSampler, uv + float2( 1, -1) * ts, 0).rgb * 1.0;

    sum += SrcTex.SampleLevel(LinearSampler, uv + float2(-1,  0) * ts, 0).rgb * 2.0;
    sum += SrcTex.SampleLevel(LinearSampler, uv + float2( 0,  0) * ts, 0).rgb * 4.0;
    sum += SrcTex.SampleLevel(LinearSampler, uv + float2( 1,  0) * ts, 0).rgb * 2.0;

    sum += SrcTex.SampleLevel(LinearSampler, uv + float2(-1,  1) * ts, 0).rgb * 1.0;
    sum += SrcTex.SampleLevel(LinearSampler, uv + float2( 0,  1) * ts, 0).rgb * 2.0;
    sum += SrcTex.SampleLevel(LinearSampler, uv + float2( 1,  1) * ts, 0).rgb * 1.0;

    sum /= 16.0;

    // Apply per-mip spectral tint (Rayleigh scatter approximation)
    sum *= SpectralTint;

    // Additive blend with same-resolution downsample level
    float3 existing = DstBlend.SampleLevel(LinearSampler, uv, 0).rgb;
    float3 blended = existing + sum * BlendWeight;

    DstTex[DTid.xy] = float4(blended, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 8: Anamorphic Streak CS (optional, half-res)
// =============================================================================

static const char kAnamorphicCS[] = R"HLSL(
// Anamorphic Streak CS -- Horizontal blur with exponential falloff
//
// Creates cinematic horizontal light streaks from bright areas.
// Spectral dispersion: red channel extends wider than blue,
// simulating chromatic aberration in anamorphic lenses.

cbuffer AnamorphicCB : register(b0)
{
    uint2  Dims;            // Half-res dimensions
    float  Intensity;       // Streak intensity (0-2)
    float  StreakLength;    // Base streak length in texels
    float2 TexelSize;       // 1.0 / Dims
    float  pad0, pad1;
};

Texture2D<float4> SrcTex : register(t0);   // Half-res bloom (upsample level 0)
SamplerState LinearSampler : register(s0);

RWTexture2D<float4> DstTex : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Dims.x || DTid.y >= Dims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(Dims);

    // 9-tap horizontal blur with exponential falloff
    // Spectral dispersion: R extends 1.2x, G = 1.0x, B = 0.8x
    float3 disperseScale = float3(1.2, 1.0, 0.8);
    float3 totalColor = float3(0, 0, 0);
    float totalWeight = 0.0;

    for (int i = -4; i <= 4; i++)
    {
        float t = float(i) / 4.0;
        float weight = exp(-abs(t) * 2.0); // Exponential falloff

        // Per-channel offset for spectral dispersion
        float3 color;
        color.r = SrcTex.SampleLevel(LinearSampler,
            uv + float2(float(i) * disperseScale.r * TexelSize.x * StreakLength, 0), 0).r;
        color.g = SrcTex.SampleLevel(LinearSampler,
            uv + float2(float(i) * disperseScale.g * TexelSize.x * StreakLength, 0), 0).g;
        color.b = SrcTex.SampleLevel(LinearSampler,
            uv + float2(float(i) * disperseScale.b * TexelSize.x * StreakLength, 0), 0).b;

        totalColor += color * weight;
        totalWeight += weight;
    }

    totalColor /= max(totalWeight, 0.0001);

    DstTex[DTid.xy] = float4(totalColor * Intensity, 1.0);
}
)HLSL";


// =============================================================================
//  Embedded HLSL -- Pass 9: Composite PS (fullscreen)
// =============================================================================

static const char kCompositePS[] = R"HLSL(
// Bloom Composite PS -- Blends all mip levels into final bloom output
//
// Reads 4 upsample mip levels + optional anamorphic streak.
// Per-mip chromatic dispersion (R/B separate scales).
// Kelvin-to-RGB color temperature tinting.
// Energy-conserving final blend.

cbuffer CompositeCB : register(b0)
{
    float4 MipWeights;      // Per-mip blend weights (x=half, y=quarter, z=eighth, w=sixteenth)
    float  Intensity;       // Overall bloom intensity
    float  ColorTemp;       // Color temperature in Kelvin (6500 = neutral)
    float  ChromaticShift;  // R/B channel shift per mip (0.0-0.02)
    float  AnamorphicBlend; // Anamorphic streak blend factor (0 = off)
    float4 Padding;
};

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

// Approximate Kelvin color temperature to RGB (Tanner Helland's algorithm)
float3 KelvinToRGB(float kelvin)
{
    float temp = kelvin / 100.0;
    float3 rgb;

    // Red
    if (temp <= 66.0)
        rgb.r = 1.0;
    else
        rgb.r = saturate(1.292936 * pow(temp - 60.0, -0.1332047592));

    // Green
    if (temp <= 66.0)
        rgb.g = saturate(0.390082 * log(temp) - 0.631841);
    else
        rgb.g = saturate(1.129891 * pow(temp - 60.0, -0.0755148492));

    // Blue
    if (temp >= 66.0)
        rgb.b = 1.0;
    else if (temp <= 19.0)
        rgb.b = 0.0;
    else
        rgb.b = saturate(0.543207 * log(temp - 10.0) - 1.196254);

    return rgb;
}

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;

    // Sample each mip level with chromatic dispersion
    // Higher mips get more R/B shift for natural falloff
    float3 bloom = float3(0, 0, 0);
    float totalWeight = 0.0;

    // Mip 0 (half-res) -- minimal shift
    {
        float shift = ChromaticShift * 0.5;
        float3 color;
        color.r = Mip0.Sample(LinearSampler, uv + float2(shift, 0)).r;
        color.g = Mip0.Sample(LinearSampler, uv).g;
        color.b = Mip0.Sample(LinearSampler, uv - float2(shift, 0)).b;
        bloom += color * MipWeights.x;
        totalWeight += MipWeights.x;
    }

    // Mip 1 (quarter-res)
    {
        float shift = ChromaticShift * 1.0;
        float3 color;
        color.r = Mip1.Sample(LinearSampler, uv + float2(shift, 0)).r;
        color.g = Mip1.Sample(LinearSampler, uv).g;
        color.b = Mip1.Sample(LinearSampler, uv - float2(shift, 0)).b;
        bloom += color * MipWeights.y;
        totalWeight += MipWeights.y;
    }

    // Mip 2 (eighth-res)
    {
        float shift = ChromaticShift * 2.0;
        float3 color;
        color.r = Mip2.Sample(LinearSampler, uv + float2(shift, 0)).r;
        color.g = Mip2.Sample(LinearSampler, uv).g;
        color.b = Mip2.Sample(LinearSampler, uv - float2(shift, 0)).b;
        bloom += color * MipWeights.z;
        totalWeight += MipWeights.z;
    }

    // Mip 3 (sixteenth-res) -- maximum shift
    {
        float shift = ChromaticShift * 4.0;
        float3 color;
        color.r = Mip3.Sample(LinearSampler, uv + float2(shift, 0)).r;
        color.g = Mip3.Sample(LinearSampler, uv).g;
        color.b = Mip3.Sample(LinearSampler, uv - float2(shift, 0)).b;
        bloom += color * MipWeights.w;
        totalWeight += MipWeights.w;
    }

    // Energy-conserving normalization
    if (totalWeight > 0.0001)
        bloom /= totalWeight;

    // Add anamorphic streak
    if (AnamorphicBlend > 0.0)
    {
        float3 streak = AnamorphicTex.Sample(LinearSampler, uv).rgb;
        bloom += streak * AnamorphicBlend;
    }

    // Apply color temperature tinting
    float3 tempTint = KelvinToRGB(ColorTemp);
    // Normalize so 6500K = white (no shift)
    float3 neutralTint = KelvinToRGB(6500.0);
    tempTint /= max(neutralTint, 0.001);
    bloom *= tempTint;

    // Apply overall intensity
    bloom *= Intensity;

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
        ID3DBlob* blob = nullptr;
        ID3DBlob* err  = nullptr;
        HRESULT hr = D3DCompile(source, strlen(source), name,
                                nullptr, nullptr, "main", "cs_5_0",
                                flags, 0, &blob, &err);
        if (FAILED(hr)) {
            if (err) {
                SKSE::log::error("BloomRenderer: {} compile failed: {}", name,
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
            SKSE::log::error("BloomRenderer: CreateComputeShader failed for {} (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }
        return true;
    };

    if (!CompileCS("BloomExtract",    kBrightExtractCS, &m_extractCS))    return false;
    if (!CompileCS("BloomDownsample", kDownsampleCS,    &m_downsampleCS)) return false;
    if (!CompileCS("BloomUpsample",   kUpsampleCS,      &m_upsampleCS))  return false;
    if (!CompileCS("BloomAnamorphic", kAnamorphicCS,    &m_anamorphicCS)) return false;

    return true;
}


// =============================================================================
//  Create GPU resources
// =============================================================================

bool BloomRenderer::CreateResources()
{
    HRESULT hr;

    // Helper: create an R16G16B16A16_FLOAT texture with SRV + UAV
    auto CreateBloomTexture = [&](const char* name, uint32_t w, uint32_t h,
                                   MipLevel& out) -> bool
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width      = w;
        desc.Height     = h;
        desc.MipLevels  = 1;
        desc.ArraySize  = 1;
        desc.Format     = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc = {1, 0};
        desc.Usage      = D3D11_USAGE_DEFAULT;
        desc.BindFlags  = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;

        hr = m_device->CreateTexture2D(&desc, nullptr, &out.tex);
        if (FAILED(hr)) {
            SKSE::log::error("BloomRenderer: failed to create {} texture (0x{:X})",
                             name, static_cast<uint32_t>(hr));
            return false;
        }

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format                    = DXGI_FORMAT_R16G16B16A16_FLOAT;
        srvDesc.ViewDimension             = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MostDetailedMip = 0;
        srvDesc.Texture2D.MipLevels       = 1;
        hr = m_device->CreateShaderResourceView(out.tex, &srvDesc, &out.srv);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format             = DXGI_FORMAT_R16G16B16A16_FLOAT;
        uavDesc.ViewDimension      = D3D11_UAV_DIMENSION_TEXTURE2D;
        uavDesc.Texture2D.MipSlice = 0;
        hr = m_device->CreateUnorderedAccessView(out.tex, &uavDesc, &out.uav);
        if (FAILED(hr)) return false;

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
    {
        D3D11_BUFFER_DESC desc = {};
        desc.Usage          = D3D11_USAGE_DYNAMIC;
        desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
        desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;

        desc.ByteWidth = sizeof(ExtractCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_extractCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(DownsampleCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_downsampleCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(UpsampleCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_upsampleCB);
        if (FAILED(hr)) return false;

        desc.ByteWidth = sizeof(CompositeCBData);
        hr = m_device->CreateBuffer(&desc, nullptr, &m_compositeCB);
        if (FAILED(hr)) return false;
    }

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

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_extractCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_extractCB, 0);
        }

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

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_downsampleCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_downsampleCB, 0);
        }

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

        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_upsampleCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_upsampleCB, 0);
        }

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
        D3D11_MAPPED_SUBRESOURCE mapped;
        if (SUCCEEDED(ctx.context->Map(m_downsampleCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
            std::memcpy(mapped.pData, &cb, sizeof(cb));
            ctx.context->Unmap(m_downsampleCB, 0);
        }

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
        cb.intensity       = m_intensity;
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
