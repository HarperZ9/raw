//=============================================================================
//  VolumetricClouds.cpp --- GPU volumetric cloud raymarching
//
//  Three-pass pipeline:
//    1. NoiseGen CS (init-time): shape 128^3 + detail 32^3 Worley/Perlin
//    2. CloudRaymarch CS (per-frame, quarter-res): density + inscattering
//    3. CloudComposite PS (fullscreen): bilateral upsample + blend
//=============================================================================

#include "VolumetricClouds.h"
#include "SRVInjector.h"
#include "D3D11Hook.h"
#include "SceneData.h"
#include "HiZPyramid.h"
#include "ComputeManager.h"
#include "RenderPassManager.h"

#include <SKSE/SKSE.h>
#include <cstring>
#include <cmath>
#include "GPUResource.h"

namespace SB
{

// ============================================================================
//  HLSL: Shape noise generation (128^3, 8x8x1 thread groups)
//  Perlin-Worley FBM for base cloud shape
// ============================================================================
static const char kShapeNoiseCS[] = R"HLSL(
// Procedural 3D Perlin-Worley shape noise for volumetric clouds (128^3)
// Reference: Schneider & Vos 2015, "The Real-time Volumetric Cloudscapes
//            of Horizon: Zero Dawn"
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

RWTexture3D<float> ShapeNoise : register(u0);

// ---- Hash / gradient helpers ------------------------------------------------
// Based on Inigo Quilez's integer hash (no sin, GPU-friendly)
uint hash3(uint3 v)
{
    v = v * uint3(1597334677u, 3812015801u, 2798796415u);
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v.x ^ v.y ^ v.z;
}

float hashToFloat(uint h)
{
    return float(h & 0x7FFFFFFFu) / float(0x7FFFFFFF);
}

// ---- 3D value noise ---------------------------------------------------------
// Trilinear interpolation of hashed lattice values.
float valueNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = p - i;
    // Smoothstep (Hermite) interpolation weights
    float3 u = f * f * (3.0 - 2.0 * f);

    int3 ip = int3(i);

    // Eight corner hashes
    float c000 = hashToFloat(hash3(uint3(ip + int3(0,0,0))));
    float c100 = hashToFloat(hash3(uint3(ip + int3(1,0,0))));
    float c010 = hashToFloat(hash3(uint3(ip + int3(0,1,0))));
    float c110 = hashToFloat(hash3(uint3(ip + int3(1,1,0))));
    float c001 = hashToFloat(hash3(uint3(ip + int3(0,0,1))));
    float c101 = hashToFloat(hash3(uint3(ip + int3(1,0,1))));
    float c011 = hashToFloat(hash3(uint3(ip + int3(0,1,1))));
    float c111 = hashToFloat(hash3(uint3(ip + int3(1,1,1))));

    // Trilinear blend
    float x00 = lerp(c000, c100, u.x);
    float x10 = lerp(c010, c110, u.x);
    float x01 = lerp(c001, c101, u.x);
    float x11 = lerp(c011, c111, u.x);
    float y0  = lerp(x00,  x10,  u.y);
    float y1  = lerp(x01,  x11,  u.y);
    return lerp(y0, y1, u.z);
}

// ---- 3D Worley noise --------------------------------------------------------
// Single-octave cellular noise: returns distance to nearest random feature point
// in a 3x3x3 neighbourhood search.
float worleyNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = p - i;

    float minDist = 1.0;

    [unroll]
    for (int z = -1; z <= 1; z++)
    [unroll]
    for (int y = -1; y <= 1; y++)
    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        int3 offset = int3(x, y, z);
        // Deterministic random feature point inside each cell
        uint h = hash3(uint3(int3(i) + offset));
        float3 fp = float3(offset) + float3(
            hashToFloat(h),
            hashToFloat(h * 2654435761u),
            hashToFloat(h * 2246822519u)
        ) - f;
        float d = dot(fp, fp);
        minDist = min(minDist, d);
    }

    return sqrt(minDist);
}

// ---- FBM (value noise, 4 octaves) ------------------------------------------
float valueNoiseFBM(float3 p)
{
    float v = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    [unroll]
    for (int i = 0; i < 4; i++)
    {
        v += amp * valueNoise(p * freq);
        freq *= 2.0;
        amp  *= 0.5;
    }
    return v;
}

// ---- Main -------------------------------------------------------------------
// Dispatch: (128/8, 128/8, 1) = (16, 16, 1) thread groups.
// Each thread writes one column of 128 Z slices.
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    [loop]
    for (uint z = 0; z < 128; z++)
    {
        float3 uvw = float3(DTid.xy, z) / 128.0;
        float3 p = uvw * 8.0; // 8 noise periods across the volume

        // Perlin-like base: 4-octave value noise FBM
        float perlin = valueNoiseFBM(p);

        // Worley erosion: invert so ridges become valleys
        float worley = 1.0 - worleyNoise(p * 2.0);

        // Perlin-Worley blend (Schneider & Vos eq. 1):
        // Remap Perlin using Worley to add billowy structure
        float shape = saturate(lerp(perlin, worley, 0.3));

        ShapeNoise[uint3(DTid.xy, z)] = shape;
    }
}
)HLSL";

// ============================================================================
//  HLSL: Detail noise generation (32^3, 8x8x1 thread groups)
//  High-frequency Worley for erosion
// ============================================================================
static const char kDetailNoiseCS[] = R"HLSL(
// Procedural 3D detail noise for volumetric cloud edge erosion (32^3)
// Reference: Schneider & Vos 2015, "The Real-time Volumetric Cloudscapes
//            of Horizon: Zero Dawn"
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

RWTexture3D<float> DetailNoise : register(u0);

// ---- Hash helpers (same as shape noise) -------------------------------------
uint hash3(uint3 v)
{
    v = v * uint3(1597334677u, 3812015801u, 2798796415u);
    v.x += v.y * v.z;
    v.y += v.z * v.x;
    v.z += v.x * v.y;
    return v.x ^ v.y ^ v.z;
}

float hashToFloat(uint h)
{
    return float(h & 0x7FFFFFFFu) / float(0x7FFFFFFF);
}

// ---- 3D Worley noise --------------------------------------------------------
float worleyNoise(float3 p)
{
    float3 i = floor(p);
    float3 f = p - i;

    float minDist = 1.0;

    [unroll]
    for (int z = -1; z <= 1; z++)
    [unroll]
    for (int y = -1; y <= 1; y++)
    [unroll]
    for (int x = -1; x <= 1; x++)
    {
        int3 offset = int3(x, y, z);
        uint h = hash3(uint3(int3(i) + offset));
        float3 fp = float3(offset) + float3(
            hashToFloat(h),
            hashToFloat(h * 2654435761u),
            hashToFloat(h * 2246822519u)
        ) - f;
        float d = dot(fp, fp);
        minDist = min(minDist, d);
    }

    return sqrt(minDist);
}

// ---- FBM (Worley, 3 octaves) -----------------------------------------------
// Inverted Worley layered at increasing frequencies for curly, high-frequency
// detail suitable for eroding cloud edges.
float worleyFBM(float3 p)
{
    float v = 0.0;
    float amp = 0.5;
    float freq = 1.0;
    [unroll]
    for (int i = 0; i < 3; i++)
    {
        v += amp * (1.0 - worleyNoise(p * freq));
        freq *= 2.0;
        amp  *= 0.5;
    }
    return v;
}

// ---- Main -------------------------------------------------------------------
// Dispatch: (32/8, 32/8, 1) = (4, 4, 1) thread groups.
// Each thread writes one column of 32 Z slices.
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    [loop]
    for (uint z = 0; z < 32; z++)
    {
        float3 uvw = float3(DTid.xy, z) / 32.0;
        float3 p = uvw * 4.0; // 4 noise periods across the volume

        float detail = worleyFBM(p);

        DetailNoise[uint3(DTid.xy, z)] = saturate(detail);
    }
}
)HLSL";

// ============================================================================
//  HLSL: Cloud raymarch compute shader (quarter-res, 8x8 thread groups)
//  Marches rays through 1500m-4000m cloud layer, samples noise for density,
//  computes inscatter (Henyey-Greenstein dual-lobe) + Beer's law extinction.
//  Temporal reprojection reuses previous frame where possible.
// ============================================================================
static const char kCloudRaymarchCS[] = R"HLSL(
// Per-pixel volumetric cloud ray marching (quarter-resolution)
// References: Schneider & Vos 2015, "The Real-time Volumetric Cloudscapes
//             of Horizon: Zero Dawn"
//             Hillaire 2016, "Physically Based Sky, Atmosphere and Cloud
//             Rendering"
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer CloudCB : register(b0)
{
    float3 SunDirection;
    float  Time;
    float3 SunColor;
    float  CloudBase;       // 1500m
    float3 CameraPos;
    float  CloudTop;        // 4000m
    float3 WindOffset;
    float  Coverage;        // [0..1]
    float2 ScreenSize;      // quarter-res dimensions
    float  Density;
    float  FrameIndex;
    row_major float4x4 PrevViewProj;  // for temporal reprojection
    row_major float4x4 InvViewProj;   // current frame inverse VP
    // Fog parameters
    float  FogDensity;
    float  FogHeight;
    float  FogFalloff;
    float  FogAnisotropy;
    uint   FogEnabled;
    float3 fogPad;
}

static const float PI = 3.14159265;
static const int   PRIMARY_STEPS   = 48;
static const int   LIGHT_STEPS     = 6;
static const float EXTINCTION_COEF = 0.04;  // per-unit extinction
static const float AMBIENT_MIN     = 0.15;  // ambient sky contribution floor

Texture3D<float>  ShapeNoise  : register(t0);
Texture3D<float>  DetailNoise : register(t1);
Texture2D<float4> PrevCloud   : register(t2);  // previous frame cloud result
Texture2D<float>  DepthBuffer : register(t3);   // full-res depth for composite
SamplerState TrilinearSampler : register(s0);
RWTexture2D<float4> CloudOut : register(u0);

// =============================================================================
//  Henyey-Greenstein phase function
//  HG(cosTheta, g) = (1 - g^2) / (4*PI * (1 + g^2 - 2*g*cosTheta)^1.5)
// =============================================================================
float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(max(denom, 1e-6), 1.5));
}

// Dual-lobe phase: blend forward (silver lining) and back scattering
float dualLobePhase(float cosTheta)
{
    float forward  = HenyeyGreenstein(cosTheta, 0.6);
    float backward = HenyeyGreenstein(cosTheta, -0.3);
    return lerp(backward, forward, 0.7);
}

// =============================================================================
//  Ray-slab intersection: horizontal cloud layer at [CloudBase, CloudTop]
//  Returns (tMin, tMax) along the ray. Negative = behind camera.
// =============================================================================
float2 intersectCloudLayer(float3 origin, float3 dir)
{
    // Avoid division by zero for perfectly horizontal rays
    float invDirZ = abs(dir.z) > 1e-6 ? 1.0 / dir.z : 1e6 * sign(dir.z + 1e-12);
    float t0 = (CloudBase - origin.z) * invDirZ;
    float t1 = (CloudTop  - origin.z) * invDirZ;

    float tMin = min(t0, t1);
    float tMax = max(t0, t1);
    return float2(max(tMin, 0.0), tMax);
}

// =============================================================================
//  Height fraction within cloud layer [0, 1]
// =============================================================================
float heightFraction(float altitude)
{
    return saturate((altitude - CloudBase) / max(CloudTop - CloudBase, 1.0));
}

// =============================================================================
//  Height-dependent density gradient (Schneider & Vos, stratus-like profile)
//  Dense near base, thinning toward top.
// =============================================================================
float heightGradient(float hf)
{
    // Ramp up from base, broad plateau, fade at top
    float bottom = saturate(hf * 5.0);          // quick ramp in lowest 20%
    float top    = saturate((1.0 - hf) * 3.0);  // fade in top 33%
    return bottom * top;
}

// =============================================================================
//  Sample cloud density at a world-space position
// =============================================================================
float sampleCloudDensity(float3 worldPos, bool doDetail)
{
    float hf = heightFraction(worldPos.z);

    // Wind-displaced sampling coordinates
    // Shape noise tiles at 128 units; scale world position for reasonable tiling
    float3 shapeUVW = (worldPos + WindOffset) * 0.0003; // ~3333 world-unit period
    float shape = ShapeNoise.SampleLevel(TrilinearSampler, shapeUVW, 0).r;

    // Height-weighted shape
    float hg = heightGradient(hf);
    shape *= hg;

    // Apply coverage: remap so that Coverage=0 means no cloud, Coverage=1 means solid
    // Remap: density = saturate( (shape - (1 - Coverage)) / Coverage )
    float coverageThreshold = 1.0 - Coverage;
    float baseDensity = saturate((shape - coverageThreshold) / max(Coverage, 0.01));

    if (baseDensity < 0.01)
        return 0.0;

    // Detail erosion (only when marching primary ray, skip for light steps)
    if (doDetail)
    {
        float3 detailUVW = (worldPos + WindOffset * 1.5) * 0.002; // higher frequency
        float detail = DetailNoise.SampleLevel(TrilinearSampler, detailUVW, 0).r;

        // Erode edges: subtract detail weighted by inverse height (more erosion at top)
        float detailWeight = 0.35 * lerp(1.0, 0.5, hf);
        baseDensity = saturate(baseDensity - detail * detailWeight);
    }

    return baseDensity * Density;
}

// =============================================================================
//  Light marching: estimate optical depth toward sun for in-scatter
//  (Schneider & Vos: 6 steps toward light with exponentially increasing step)
// =============================================================================
float lightMarch(float3 pos)
{
    float cloudHeight = CloudTop - CloudBase;
    float stepSize = cloudHeight / float(LIGHT_STEPS);
    float opticalDepth = 0.0;

    [loop]
    for (int i = 0; i < LIGHT_STEPS; i++)
    {
        pos += SunDirection * stepSize;

        // Exit if we leave the cloud layer
        float alt = pos.z;
        if (alt < CloudBase || alt > CloudTop)
            break;

        float d = sampleCloudDensity(pos, false);
        opticalDepth += d * stepSize * EXTINCTION_COEF;
    }

    return opticalDepth;
}

// =============================================================================
//  Reconstruct world-space ray direction from pixel UV + InvViewProj
// =============================================================================
float3 reconstructRay(float2 uv)
{
    // UV -> NDC (D3D11: Y is flipped)
    float2 ndc = float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);

    // Unproject far plane point through inverse view-projection
    float4 farClip = mul(float4(ndc, 1.0, 1.0), InvViewProj);
    float3 farWorld = farClip.xyz / farClip.w;

    return normalize(farWorld - CameraPos);
}

// =============================================================================
//  Main: quarter-resolution ray march
// =============================================================================
[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= uint(ScreenSize.x) || DTid.y >= uint(ScreenSize.y))
    {
        return;
    }

    float2 uv = (float2(DTid.xy) + 0.5) / ScreenSize;

    // ---- Sky pixel rejection ------------------------------------------------
    // Depth is reversed-Z from HiZ: sky ~ 0. At PostGeometry, sky geometry
    // hasn't rendered yet so depth remains cleared. Skip to prevent black sky.
    // Sample from the full-res depth buffer (nearest pixel at quarter-res UV).
    uint2 fullResCoord = uint2(uv * ScreenSize * 4.0); // approximate full-res pixel
    float rawDepth = DepthBuffer.Load(int3(fullResCoord, 0)).r;
    if (rawDepth < 0.00001)
    {
        // Sky pixel: output fully transparent (no cloud)
        CloudOut[DTid.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ---- Ray setup ----------------------------------------------------------
    float3 rayDir = reconstructRay(uv);
    float2 tRange = intersectCloudLayer(CameraPos, rayDir);
    float tMin = tRange.x;
    float tMax = tRange.y;

    // No intersection with cloud layer
    if (tMax <= tMin || tMax <= 0.0)
    {
        CloudOut[DTid.xy] = float4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    // ---- Primary ray march --------------------------------------------------
    float stepSize = (tMax - tMin) / float(PRIMARY_STEPS);
    float cosTheta = dot(rayDir, SunDirection);
    float phase    = dualLobePhase(cosTheta);

    float  transmittance = 1.0;
    float3 inscatter     = float3(0.0, 0.0, 0.0);
    float  t = tMin;

    // Jitter start position to reduce banding (temporal noise)
    {
        uint seed = DTid.x + DTid.y * 1920u + uint(FrameIndex) * 73856093u;
        float jitter = frac(float(seed) * 0.00000000023283064365386963); // 1/2^32-ish
        t += stepSize * jitter;
    }

    [loop]
    for (int i = 0; i < PRIMARY_STEPS; i++)
    {
        float3 samplePos = CameraPos + rayDir * t;

        float density = sampleCloudDensity(samplePos, true);

        if (density > 0.001)
        {
            float extinction = density * stepSize * EXTINCTION_COEF;

            // Beer-Lambert attenuation for this step
            float stepTransmittance = exp(-extinction);

            // Light energy at this sample: march toward sun
            float lightOptDepth = lightMarch(samplePos);
            float lightTransmittance = exp(-lightOptDepth);

            // Ambient: height-dependent sky light contribution
            float hf = heightFraction(samplePos.z);
            float ambient = AMBIENT_MIN + 0.15 * hf;

            // In-scattered light at this sample (energy-conserving integration)
            float3 lightEnergy = SunColor * (lightTransmittance * phase + ambient);
            float3 stepScatter = lightEnergy * density * EXTINCTION_COEF;

            // Energy-conserving integration (Frostbite / Hillaire 2016):
            // integral of scatter * exp(-ext * s) ds over step
            // = scatter/ext * (1 - exp(-ext * step))
            float3 scatterIntegral = stepScatter * (1.0 - stepTransmittance)
                                     / max(density * EXTINCTION_COEF, 1e-6);

            inscatter     += transmittance * scatterIntegral;
            transmittance *= stepTransmittance;

            // Early exit when ray is effectively opaque
            if (transmittance < 0.01)
            {
                transmittance = 0.0;
                break;
            }
        }

        t += stepSize;
    }

    // ---- Output: float4(inscatter.rgb, transmittance) -----------------------
    CloudOut[DTid.xy] = float4(inscatter, transmittance);
}
)HLSL";

// ============================================================================
//  HLSL: Cloud composite pixel shader (fullscreen)
//  Bilateral upsample from quarter-res + blend using transmittance
// ============================================================================
static const char kCloudCompositePS[] = R"HLSL(
// Bilateral upsample + composite for volumetric clouds
// Reference: Schneider & Vos 2015, "The Real-time Volumetric Cloudscapes
//            of Horizon: Zero Dawn" (upsampling section)
// Copyright (c) 2026 Zain D. Harper. All rights reserved.

cbuffer CompositeCB : register(b0)
{
    float2 QuarterSize;    // quarter-res dimensions
    float2 FullSize;       // full-res dimensions
    float  DepthThreshold; // bilateral depth threshold
    float  Pad0, Pad1, Pad2;
}

Texture2D<float4> CloudTex   : register(t0);  // quarter-res cloud (inscatter.rgb, transmittance.a)
Texture2D<float>  DepthTex   : register(t1);  // full-res depth
Texture2D<float4> SceneColor : register(t2);  // current backbuffer
SamplerState PointSampler   : register(s0);
SamplerState LinearSampler  : register(s1);

struct VSOut
{
    float4 pos : SV_Position;
    float2 uv  : TEXCOORD0;
};

float4 main(VSOut input) : SV_Target
{
    float2 uv = input.uv;

    // ---- Fetch scene color --------------------------------------------------
    float4 scene = SceneColor.SampleLevel(PointSampler, uv, 0);

    // ---- Sky pixel guard (reversed-Z: sky < 0.0001) -------------------------
    // At PostGeometry, sky hasn't rendered yet. If we composite clouds onto sky
    // pixels we'll get a black sky. Pass through scene color unchanged.
    float fullDepth = DepthTex.SampleLevel(PointSampler, uv, 0).r;
    if (fullDepth < 0.0001)
    {
        return scene;
    }

    // ---- Bilateral upsample from quarter-res --------------------------------
    // Sample the 4 nearest quarter-res texels and weight by depth similarity
    // to the full-res pixel.  This preserves sharp edges at depth discontinuities
    // (e.g., mountains against sky) while smoothly interpolating in flat regions.

    float2 quarterUV = uv * FullSize / QuarterSize;  // texel coordinate in quarter-res
    float2 texelCenter = floor(quarterUV - 0.5) + 0.5;

    // Bilinear interpolation weights
    float2 frac_ = quarterUV - texelCenter;

    // Four nearest quarter-res texel centers
    float2 uv00 = (texelCenter + float2(0.0, 0.0)) / QuarterSize;
    float2 uv10 = (texelCenter + float2(1.0, 0.0)) / QuarterSize;
    float2 uv01 = (texelCenter + float2(0.0, 1.0)) / QuarterSize;
    float2 uv11 = (texelCenter + float2(1.0, 1.0)) / QuarterSize;

    // Sample cloud at four neighbours
    float4 c00 = CloudTex.SampleLevel(PointSampler, uv00, 0);
    float4 c10 = CloudTex.SampleLevel(PointSampler, uv10, 0);
    float4 c01 = CloudTex.SampleLevel(PointSampler, uv01, 0);
    float4 c11 = CloudTex.SampleLevel(PointSampler, uv11, 0);

    // Sample depth at four quarter-res positions (map back to full-res UV)
    float d00 = DepthTex.SampleLevel(PointSampler, uv00, 0).r;
    float d10 = DepthTex.SampleLevel(PointSampler, uv10, 0).r;
    float d01 = DepthTex.SampleLevel(PointSampler, uv01, 0).r;
    float d11 = DepthTex.SampleLevel(PointSampler, uv11, 0).r;

    // Bilateral weights: bilinear weight * depth similarity
    float w00 = (1.0 - frac_.x) * (1.0 - frac_.y);
    float w10 =        frac_.x  * (1.0 - frac_.y);
    float w01 = (1.0 - frac_.x) *        frac_.y;
    float w11 =        frac_.x  *        frac_.y;

    // Depth-based bilateral weighting: exponential falloff with depth difference
    w00 *= exp(-abs(fullDepth - d00) / max(DepthThreshold, 1e-6));
    w10 *= exp(-abs(fullDepth - d10) / max(DepthThreshold, 1e-6));
    w01 *= exp(-abs(fullDepth - d01) / max(DepthThreshold, 1e-6));
    w11 *= exp(-abs(fullDepth - d11) / max(DepthThreshold, 1e-6));

    float totalWeight = w00 + w10 + w01 + w11;

    float4 cloud;
    if (totalWeight > 1e-6)
    {
        cloud = (c00 * w00 + c10 * w10 + c01 * w01 + c11 * w11) / totalWeight;
    }
    else
    {
        // Fallback: simple bilinear
        cloud = CloudTex.SampleLevel(LinearSampler, uv, 0);
    }

    // ---- Composite: inscatter + scene * transmittance -----------------------
    float3 inscatter    = cloud.rgb;
    float  transmittance = cloud.a;

    float3 result = inscatter + scene.rgb * transmittance;
    return float4(result, scene.a);
}
)HLSL";

// ============================================================================
//  CB data structures (must match HLSL layouts)
// ============================================================================

struct CloudCBData
{
    float sunDirection[3];
    float time;
    float sunColor[3];
    float cloudBase;
    float cameraPos[3];
    float cloudTop;
    float windOffset[3];
    float coverage;
    float screenSize[2];
    float density;
    float frameIndex;
    float prevViewProj[16];
    float invViewProj[16];
    // Fog parameters
    float fogDensity;       // Base fog density
    float fogHeight;        // Fog layer height
    float fogFalloff;       // Exponential falloff rate
    float fogAnisotropy;    // Scattering anisotropy
    uint32_t fogEnabled;    // 0 or 1
    float fogPad[3];        // Alignment padding
};

struct CompositeCBData
{
    float quarterSize[2];
    float fullSize[2];
    float depthThreshold;
    float pad[3];
};

// ============================================================================
//  Initialize
// ============================================================================

bool VolumetricClouds::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;

    m_device  = dev;
    m_context = ctx;

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();
    auto& pl  = RenderPipeline::Get();

    if (!cm.IsInitialized() || !rpm.IsInitialized() || !pl.IsInitialized()) {
        SKSE::log::error("VolumetricClouds: prerequisites not initialized");
        return false;
    }

    // Compile noise generation compute shaders
    m_shapeNoiseCS = cm.CompileShader("CloudShapeNoise", kShapeNoiseCS);
    if (!m_shapeNoiseCS) {
        SKSE::log::error("VolumetricClouds: failed to compile ShapeNoise CS");
        return false;
    }

    m_detailNoiseCS = cm.CompileShader("CloudDetailNoise", kDetailNoiseCS);
    if (!m_detailNoiseCS) {
        SKSE::log::error("VolumetricClouds: failed to compile DetailNoise CS");
        return false;
    }

    // Compile cloud raymarch compute shader
    m_raymarchCS = cm.CompileShader("CloudRaymarch", kCloudRaymarchCS);
    if (!m_raymarchCS) {
        SKSE::log::error("VolumetricClouds: failed to compile CloudRaymarch CS");
        return false;
    }

    // Register composite fullscreen pass
    m_compositePass = rpm.RegisterPass({
        .name = "CloudComposite",
        .psSource = kCloudCompositePS,
    });
    if (!m_compositePass) {
        SKSE::log::error("VolumetricClouds: failed to register CloudComposite pass");
        return false;
    }

    // Create cloud raymarch constant buffer
    m_cloudCB = cm.CreateConstantBuffer(sizeof(CloudCBData));
    if (!m_cloudCB) return false;

    // Create composite constant buffer
    if (!CreateCB(dev, sizeof(CompositeCBData), &m_compositeCB)) return false;
    // Create trilinear wrap sampler for 3D noise reads
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressV = D3D11_TEXTURE_ADDRESS_WRAP;
        sd.AddressW = D3D11_TEXTURE_ADDRESS_WRAP;
        if (FAILED(dev->CreateSamplerState(&sd, &m_trilinearSampler)))
            return false;
    }

    // Defer noise generation to first ExecutePass — dispatching compute shaders
    // during kDataLoaded init corrupts D3D11 state and breaks Scaleform UI.
    // m_noiseGenerated = false already, so first ExecutePass will generate them.

    // Create quarter-res cloud render targets
    {
        ID3D11Texture2D* backTex = nullptr;
        if (SUCCEEDED(sc->GetBuffer(0, __uuidof(ID3D11Texture2D), reinterpret_cast<void**>(&backTex)))) {
            D3D11_TEXTURE2D_DESC bbDesc;
            backTex->GetDesc(&bbDesc);
            backTex->Release();

            if (!CreateCloudTextures(bbDesc.Width, bbDesc.Height))
                return false;
        } else {
            return false;
        }
    }

    // Register SRV at t27 for shader passes
    SRVInjector::Get().RegisterSRV(kCloudSRVSlot, m_cloudSRV);

    // Register as PostGeometry pipeline pass
    // Register at PostGeometry with priority 80 (after core effects at 15-25,
    // before SceneCompositor at 90).  PostSky is ideal but currently never
    // fires because no sky shader hashes are registered for phase detection.
    // TODO: re-enable PostSky once sky shader hash discovery is implemented.
    m_pipelineHandle = pl.AddPass({
        .name     = "VolumetricClouds",
        .stage    = PipelineStage::PostGeometry,
        .priority = 80,
        .execute  = [this](PassContext& ctx) { ExecutePass(ctx); },
    });

    m_initialized = true;
    SKSE::log::info("VolumetricClouds: initialized (quarter-res {}x{}, shape 128^3, detail 32^3)",
        m_quarterW, m_quarterH);
    return true;
}

// ============================================================================
//  Shutdown
// ============================================================================

void VolumetricClouds::Shutdown()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };

    SafeRelease(m_shapeNoiseTex);
    SafeRelease(m_shapeNoiseSRV);
    SafeRelease(m_shapeNoiseUAV);
    SafeRelease(m_detailNoiseTex);
    SafeRelease(m_detailNoiseSRV);
    SafeRelease(m_detailNoiseUAV);
    SafeRelease(m_cloudTex);
    SafeRelease(m_cloudSRV);
    SafeRelease(m_cloudUAV);
    SafeRelease(m_cloudHistTex);
    SafeRelease(m_cloudHistSRV);
    SafeRelease(m_cloudCB);
    SafeRelease(m_compositeCB);
    SafeRelease(m_trilinearSampler);

    m_noiseGenerated = false;
    m_initialized = false;
}

// ============================================================================
//  Wind accumulation (called from main frame loop with WeatherTracker data)
// ============================================================================

void VolumetricClouds::AccumulateWind(float dx, float dy, float dz)
{
    m_windOffsetX += dx;
    m_windOffsetY += dy;
    m_windOffsetZ += dz;

    // Wrap to prevent float precision loss after hours of play.
    // Period is large enough to be visually seamless (noise tiles at ~3333 world units).
    constexpr float kWrapPeriod = 100000.0f;
    m_windOffsetX = std::fmod(m_windOffsetX, kWrapPeriod);
    m_windOffsetY = std::fmod(m_windOffsetY, kWrapPeriod);
    m_windOffsetZ = std::fmod(m_windOffsetZ, kWrapPeriod);
}

// ============================================================================
//  Generate noise textures (one-time at init)
// ============================================================================

bool VolumetricClouds::GenerateNoiseTextures()
{
    if (m_noiseGenerated) return true;

    auto& cm = ComputeManager::Get();
    HRESULT hr;

    // ---- Shape noise: 128^3 R8_UNORM ----
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width     = 128;
        desc.Height    = 128;
        desc.Depth     = 128;
        desc.MipLevels = 1;
        desc.Format    = DXGI_FORMAT_R8_UNORM;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        hr = m_device->CreateTexture3D(&desc, nullptr, &m_shapeNoiseTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = 1;
        hr = m_device->CreateShaderResourceView(m_shapeNoiseTex, &srvDesc, &m_shapeNoiseSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = 128;
        hr = m_device->CreateUnorderedAccessView(m_shapeNoiseTex, &uavDesc, &m_shapeNoiseUAV);
        if (FAILED(hr)) return false;
    }

    // ---- Detail noise: 32^3 R8_UNORM ----
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width     = 32;
        desc.Height    = 32;
        desc.Depth     = 32;
        desc.MipLevels = 1;
        desc.Format    = DXGI_FORMAT_R8_UNORM;
        desc.Usage     = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        hr = m_device->CreateTexture3D(&desc, nullptr, &m_detailNoiseTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = 1;
        hr = m_device->CreateShaderResourceView(m_detailNoiseTex, &srvDesc, &m_detailNoiseSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = 32;
        hr = m_device->CreateUnorderedAccessView(m_detailNoiseTex, &uavDesc, &m_detailNoiseUAV);
        if (FAILED(hr)) return false;
    }

    // ---- Dispatch noise generation ----
    cm.SaveCSState();

    // Shape noise: 128/8 = 16 groups in X and Y, Z is iterated in-shader
    {
        ID3D11UnorderedAccessView* uavs[] = { m_shapeNoiseUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_shapeNoiseCS, 128 / 8, 128 / 8, 1);
        cm.CSClearUAVs(0, 1);
    }

    // Detail noise: 32/8 = 4 groups in X and Y, Z is iterated in-shader
    {
        ID3D11UnorderedAccessView* uavs[] = { m_detailNoiseUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_detailNoiseCS, 32 / 8, 32 / 8, 1);
        cm.CSClearUAVs(0, 1);
    }

    cm.RestoreCSState();

    m_noiseGenerated = true;
    SKSE::log::info("VolumetricClouds: noise textures generated (shape 128^3, detail 32^3)");
    return true;
}

// ============================================================================
//  Create quarter-res cloud textures + history for temporal reprojection
// ============================================================================

bool VolumetricClouds::CreateCloudTextures(uint32_t screenW, uint32_t screenH)
{
    m_quarterW = screenW / 4;
    m_quarterH = screenH / 4;
    if (m_quarterW == 0) m_quarterW = 1;
    if (m_quarterH == 0) m_quarterH = 1;

    auto& cm = ComputeManager::Get();
    HRESULT hr;

    // Current frame cloud output
    {
        auto res = cm.CreateTexture2D(m_quarterW, m_quarterH,
                                       DXGI_FORMAT_R16G16B16A16_FLOAT,
                                       true, true, 1, false, "CloudOutput");
        if (!res.Valid()) return false;
        m_cloudTex = res.texture;
        m_cloudSRV = res.srv;
        m_cloudUAV = res.uav;
    }

    // Previous frame cloud history (for temporal reprojection, SRV only)
    {
        D3D11_TEXTURE2D_DESC desc = {};
        desc.Width            = m_quarterW;
        desc.Height           = m_quarterH;
        desc.MipLevels        = 1;
        desc.ArraySize        = 1;
        desc.Format           = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.SampleDesc.Count = 1;
        desc.Usage            = D3D11_USAGE_DEFAULT;
        desc.BindFlags        = D3D11_BIND_SHADER_RESOURCE;
        hr = m_device->CreateTexture2D(&desc, nullptr, &m_cloudHistTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = 1;
        hr = m_device->CreateShaderResourceView(m_cloudHistTex, &srvDesc, &m_cloudHistSRV);
        if (FAILED(hr)) return false;
    }

    return true;
}

// ============================================================================
//  Per-frame execution (called from RenderPipeline at PostGeometry stage)
// ============================================================================

void VolumetricClouds::ExecutePass(PassContext& ctx)
{
    if (!m_initialized || !m_enabled) return;

    // Lazy noise generation: only when explicitly enabled
    if (!m_noiseGenerated) {
        if (!GenerateNoiseTextures()) {
            SKSE::log::error("VolumetricClouds: noise generation failed — disabling");
            m_enabled = false;
            return;
        }
    }

    auto& cm  = ComputeManager::Get();
    auto& rpm = RenderPassManager::Get();

    m_frameIndex++;

    // Acquire depth SRV — HiZ is now built at PostGeometry:1 (before this pass),
    // so it's fresh during mid-frame dispatch. Use it when available.
    ID3D11ShaderResourceView* depthSRV = D3D11Hook::GetGameDepthSRV();
    {
        auto& hiz = HiZPyramid::Get();
        if (hiz.IsInitialized() && hiz.GetSRV()) {
            depthSRV = hiz.GetSRV();
        }
    }

    // ---- Pass 2: Cloud raymarch (quarter-res compute) ----
    {
        // Prepare CB data
        auto& scene = SceneMatrices::Get();
        CloudCBData cb = {};
        cb.sunDirection[0] = scene.SunDirection()[0];
        cb.sunDirection[1] = scene.SunDirection()[1];
        cb.sunDirection[2] = scene.SunDirection()[2];
        cb.time            = static_cast<float>(m_frameIndex) * 0.016f;
        cb.sunColor[0]     = scene.SunColor()[0];
        cb.sunColor[1]     = scene.SunColor()[1];
        cb.sunColor[2]     = scene.SunColor()[2];
        cb.cloudBase       = m_cloudBase;
        cb.cameraPos[0]    = scene.CameraPosX();
        cb.cameraPos[1]    = scene.CameraPosY();
        cb.cameraPos[2]    = scene.CameraPosZ();
        cb.cloudTop        = m_cloudTop;
        cb.windOffset[0]   = m_windOffsetX;
        cb.windOffset[1]   = m_windOffsetY;
        cb.windOffset[2]   = m_windOffsetZ;
        cb.coverage        = m_coverage;
        cb.screenSize[0]   = static_cast<float>(m_quarterW);
        cb.screenSize[1]   = static_cast<float>(m_quarterH);
        cb.density         = m_density;
        cb.frameIndex      = static_cast<float>(m_frameIndex);

        std::memcpy(cb.prevViewProj, scene.PrevViewProjMatrix(), 16 * sizeof(float));
        std::memcpy(cb.invViewProj,  scene.InvViewProjMatrix(),  16 * sizeof(float));

        cb.fogDensity    = m_fogDensity;
        cb.fogHeight     = m_fogHeight;
        cb.fogFalloff    = m_fogFalloff;
        cb.fogAnisotropy = m_fogAnisotropy;
        cb.fogEnabled    = m_fogEnabled ? 1u : 0u;
        cb.fogPad[0] = cb.fogPad[1] = cb.fogPad[2] = 0.0f;

        UploadCB(ctx.context, m_cloudCB, &cb, sizeof(cb));

        cm.SaveCSState();

        // Bind resources
        ID3D11ShaderResourceView* srvs[] = {
            m_shapeNoiseSRV,   // t0
            m_detailNoiseSRV,  // t1
            m_cloudHistSRV,    // t2 (previous frame)
            depthSRV,          // t3 (depth — acquired above)
        };
        cm.CSSetSRVs(0, 4, srvs);

        ID3D11SamplerState* samplers[] = { m_trilinearSampler };
        cm.CSSetSamplers(0, 1, samplers);

        cm.CSSetCBs(0, 1, &m_cloudCB);

        ID3D11UnorderedAccessView* uavs[] = { m_cloudUAV };
        cm.CSSetUAVs(0, 1, uavs);

        // Dispatch at quarter resolution
        UINT groupsX = (m_quarterW + 7) / 8;
        UINT groupsY = (m_quarterH + 7) / 8;
        cm.Dispatch(m_raymarchCS, groupsX, groupsY, 1);

        cm.CSClearSRVs(0, 4);
        cm.CSClearUAVs(0, 1);
        cm.RestoreCSState();

        // Copy current cloud output to history for next frame's temporal reprojection
        ctx.context->CopyResource(m_cloudHistTex, m_cloudTex);
    }

    // ---- Pass 3: Cloud composite (fullscreen PS) ----
    {
        // Acquire scene texture: mid-frame uses game's active RTV, Present uses swapchain
        ID3D11Texture2D* sceneTex = nullptr;
        bool ownSceneTex = false;
        ID3D11RenderTargetView* sceneRTV = nullptr;
        bool ownRTV = false;

        if (ctx.gameSceneRTV) {
            // Mid-frame path: extract texture from game's active RTV
            ID3D11Resource* res = nullptr;
            ctx.gameSceneRTV->GetResource(&res);
            if (res) {
                res->QueryInterface(__uuidof(ID3D11Texture2D),
                                    reinterpret_cast<void**>(&sceneTex));
                res->Release();
            }
            ownSceneTex = true;
            sceneRTV = ctx.gameSceneRTV;
            sceneRTV->AddRef();
            ownRTV = true;
        } else {
            // Present-time path: use swapchain backbuffer
            auto* sc = ctx.swapChain;
            if (!sc) sc = D3D11Hook::GetSwapChain();
            if (sc) {
                sc->GetBuffer(0, __uuidof(ID3D11Texture2D),
                              reinterpret_cast<void**>(&sceneTex));
                ownSceneTex = true;
            }
        }

        if (!sceneTex) return;

        D3D11_TEXTURE2D_DESC texDesc;
        sceneTex->GetDesc(&texDesc);

        // Guard: only composite onto full-color scene RTs (same as SceneCompositor)
        {
            bool validFmt = (texDesc.Format == DXGI_FORMAT_R16G16B16A16_FLOAT ||
                             texDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM ||
                             texDesc.Format == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ||
                             texDesc.Format == DXGI_FORMAT_R11G11B10_FLOAT ||
                             texDesc.Format == DXGI_FORMAT_R10G10B10A2_UNORM);
            if (!validFmt) {
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }
        }

        // Create temporary scene copy (read as SRV while writing to RTV)
        ID3D11Texture2D* sceneCopy = nullptr;
        ID3D11ShaderResourceView* sceneCopySRV = nullptr;
        {
            D3D11_TEXTURE2D_DESC copyDesc = texDesc;
            copyDesc.BindFlags      = D3D11_BIND_SHADER_RESOURCE;
            copyDesc.Usage          = D3D11_USAGE_DEFAULT;
            copyDesc.CPUAccessFlags = 0;
            copyDesc.MiscFlags      = 0;
            if (FAILED(m_device->CreateTexture2D(&copyDesc, nullptr, &sceneCopy))) {
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }
            ctx.context->CopyResource(sceneCopy, sceneTex);

            D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
            srvDesc.Format = texDesc.Format;
            srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
            srvDesc.Texture2D.MipLevels = 1;
            if (FAILED(m_device->CreateShaderResourceView(sceneCopy, &srvDesc, &sceneCopySRV))) {
                sceneCopy->Release();
                if (ownSceneTex) sceneTex->Release();
                if (ownRTV) sceneRTV->Release();
                return;
            }
        }

        // Create scene RTV if we don't already have one from mid-frame
        if (!sceneRTV) {
            D3D11_RENDER_TARGET_VIEW_DESC rtvDesc = {};
            rtvDesc.Format = texDesc.Format;
            rtvDesc.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
            m_device->CreateRenderTargetView(sceneTex, &rtvDesc, &sceneRTV);
            ownRTV = true;
        }

        if (ownSceneTex) sceneTex->Release();

        if (sceneRTV) {
            // Update composite CB
            CompositeCBData compCB = {};
            compCB.quarterSize[0]  = static_cast<float>(m_quarterW);
            compCB.quarterSize[1]  = static_cast<float>(m_quarterH);
            compCB.fullSize[0]     = static_cast<float>(ctx.screenW);
            compCB.fullSize[1]     = static_cast<float>(ctx.screenH);
            compCB.depthThreshold  = 0.001f;

            UploadCB(ctx.context, m_compositeCB, &compCB, sizeof(compCB));

            // SRVs for composite pass: cloud + depth + scene
            ID3D11ShaderResourceView* srvs[] = {
                m_cloudSRV,    // t0: quarter-res cloud
                depthSRV,      // t1: depth (acquired at top of ExecutePass)
                sceneCopySRV,  // t2: scene color
            };

            // Samplers: point + linear
            ID3D11SamplerState* pointSampler = nullptr;
            {
                D3D11_SAMPLER_DESC sd = {};
                sd.Filter   = D3D11_FILTER_MIN_MAG_MIP_POINT;
                sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                m_device->CreateSamplerState(&sd, &pointSampler);
            }
            ID3D11SamplerState* samplers[] = { pointSampler, m_trilinearSampler };

            rpm.Execute({
                .passID       = m_compositePass,
                .rtv          = sceneRTV,
                .srvs         = srvs,
                .srvCount     = 3,
                .samplers     = samplers,
                .samplerCount = 2,
                .cbData       = &compCB,
                .cbSize       = sizeof(compCB),
            });

            if (pointSampler) pointSampler->Release();
        }

        if (ownRTV && sceneRTV) sceneRTV->Release();
        if (sceneCopySRV) sceneCopySRV->Release();
        if (sceneCopy) sceneCopy->Release();
    }
}

} // namespace SB
