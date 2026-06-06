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
    if (rawDepth < 0.0001)
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
