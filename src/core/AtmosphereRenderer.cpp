//=============================================================================
//  AtmosphereRenderer.cpp — Physically-based atmosphere + sky rendering
//
//  Three precomputed LUTs:
//    1. Transmittance (256x64): optical depth from any altitude/angle
//    2. Multi-scattering (32x32): 2nd+ order scattering contribution
//    3. Aerial perspective (32x32x32): in-scattering for distant objects
//
//  Sky evaluation runs as a PostGeometry pipeline pass — writes to a managed RT
//  that can be composited over the game sky or used for sky replacement.
//=============================================================================

#include "AtmosphereRenderer.h"
#include "SRVInjector.h"

#include <SKSE/SKSE.h>
#include <dxgi.h>
#include <cstring>
#include <cmath>

namespace SB
{

// ── Atmosphere constants CB ───────────────────────────────────────────────
struct AtmosphereCB
{
    float earthRadius;       // 6360 km
    float atmosphereRadius;  // 6460 km
    float pad0, pad1;

    float rayleighScaleH;    // 8.0 km
    float mieScaleH;         // 1.2 km
    float mieG;              // 0.8 (anisotropy)
    float sunIntensity;      // 20.0

    float rayleighR, rayleighG, rayleighB;
    float ozoneScale;

    float sunZenithCos;
    float sunAzimuth;
    float lutWidth, lutHeight;
};

// ── Celestial constant buffer (sun disk, moon, stars) ─────────────────────
struct CelestialCB
{
    float sunZenithCos, sunAzimuth;
    float moonZenithCos, moonAzimuth;
    float moonPhase;
    float starIntensity;
    float sunDiskIntensity;
    float sunIntensity;
    uint32_t screenW, screenH;
    float pad[2];
};

// ── Transmittance LUT compute shader ──────────────────────────────────────
// Maps (altitude, zenith angle) → optical depth through the atmosphere
static const char kTransmittanceCS[] = R"HLSL(
// Transmittance LUT — 256x64 optical depth integration
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Reference: Bruneton & Neyret 2008, "Precomputed Atmospheric Scattering"
//            Hillaire 2020, "A Scalable and Production Ready Sky and
//            Atmosphere Rendering Technique"
//
// Maps (u=cos_zenith, v=altitude) -> transmittance through the atmosphere.
// Integrates Rayleigh + Mie + Ozone extinction along a ray from altitude h
// at zenith angle theta to the top of the atmosphere.
//
// UV parameterization:
//   u -> cos(zenith angle) mapped from [-1, 1]
//   v -> altitude mapped from [0, AtmoRadius - EarthRadius]

cbuffer AtmoCB : register(b0)
{
    float EarthRadius;
    float AtmoRadius;
    float pad0, pad1;
    float RayleighScaleH;
    float MieScaleH;
    float MieG;
    float SunIntensity;
    float RayleighR, RayleighG, RayleighB;
    float OzoneScale;
    float SunZenithCos;
    float SunAzimuth;
    float LutWidth, LutHeight;
}

RWTexture2D<float4> TransmittanceLUT : register(u0);

static const int   NUM_SAMPLES = 40;
static const float PI = 3.14159265358979;

// Ozone absorption cross-section (simplified, per meter)
static const float3 kOzoneAbsorption = float3(0.650e-6, 1.881e-6, 0.085e-6);

// Ray-sphere intersection: returns distance to nearest hit or -1
// Origin at (0, originY, 0), direction = (sinTheta, cosTheta, 0)
float RaySphereIntersect(float originY, float cosTheta, float sphereRadius)
{
    // Quadratic: t^2 + 2*originY*cosTheta*t + (originY^2 - R^2) = 0
    float b = 2.0 * originY * cosTheta;
    float c = originY * originY - sphereRadius * sphereRadius;
    float disc = b * b - 4.0 * c;
    if (disc < 0.0) return -1.0;
    return (-b + sqrt(disc)) * 0.5;
}

// Density at altitude h (meters above sea level)
float RayleighDensity(float h) { return exp(-h / RayleighScaleH); }
float MieDensity(float h)      { return exp(-h / MieScaleH); }
float OzoneDensity(float h)
{
    // Simplified ozone layer profile: peak at 25km, ~15km width
    float center = 25000.0;
    float width  = 15000.0;
    return max(0.0, 1.0 - abs(h - center) / width);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= (uint)LutWidth || DTid.y >= (uint)LutHeight)
        return;

    // Map pixel to (cosZenith, altitude)
    float u = (float(DTid.x) + 0.5) / LutWidth;
    float v = (float(DTid.y) + 0.5) / LutHeight;

    // Parameterize altitude: v -> h in [0, AtmoRadius - EarthRadius]
    float maxAltitude = AtmoRadius - EarthRadius;
    float h = v * maxAltitude;
    float r = EarthRadius + h;  // distance from planet center

    // Parameterize zenith angle: u -> cos(theta) in [-1, 1]
    // Use non-linear mapping for better precision near horizon
    float cosTheta = 2.0 * u - 1.0;

    // Find ray length to atmosphere boundary
    float tMax = RaySphereIntersect(r, cosTheta, AtmoRadius);
    if (tMax < 0.0)
    {
        TransmittanceLUT[DTid.xy] = float4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    // Check for ground intersection
    float tGround = RaySphereIntersect(r, cosTheta, EarthRadius);
    if (tGround > 0.0)
        tMax = min(tMax, tGround);

    // Integrate optical depth along ray
    float3 opticalDepth = 0.0;
    float dt = tMax / float(NUM_SAMPLES);

    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

    for (int i = 0; i < NUM_SAMPLES; i++)
    {
        float t = (float(i) + 0.5) * dt;

        // Position along ray (2D: x = sinTheta*t, y = r + cosTheta*t)
        float px = sinTheta * t;
        float py = r + cosTheta * t;
        float sampleR = sqrt(px * px + py * py);
        float sampleH = sampleR - EarthRadius;

        if (sampleH < 0.0) break;  // hit ground

        float rho_r = RayleighDensity(sampleH);
        float rho_m = MieDensity(sampleH);
        float rho_o = OzoneDensity(sampleH);

        // Extinction coefficients
        float3 rayleighExt = float3(RayleighR, RayleighG, RayleighB) * rho_r;
        float  mieExt      = 1.11e-5 * rho_m;  // Mie extinction ~= 1.1 * scattering
        float3 ozoneExt    = kOzoneAbsorption * OzoneScale * rho_o;

        opticalDepth += (rayleighExt + float3(mieExt, mieExt, mieExt) + ozoneExt) * dt;
    }

    float3 transmittance = exp(-opticalDepth);
    TransmittanceLUT[DTid.xy] = float4(transmittance, 1.0);
}
)HLSL";

// ── Multi-scattering LUT compute shader ───────────────────────────────────
// Approximates 2nd+ order scattering contribution (Hillaire 2020)
static const char kMultiScatterCS[] = R"HLSL(
// Multi-scattering LUT — 32x32 second-order scattering approximation
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Reference: Hillaire 2020, "A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique" (Section 5.3)
//
// Approximates the contribution of 2nd and higher order scattering using
// the isotropic assumption: after the first scatter, the phase function
// averages to uniform (1/4pi). For each (sun_zenith, altitude) pair,
// integrate over a sphere of directions to compute how much light
// arrives from all directions after being scattered once.
//
// UV parameterization:
//   u -> cos(sun zenith) in [-1, 1]
//   v -> altitude in [0, AtmoRadius - EarthRadius]

cbuffer AtmoCB : register(b0)
{
    float EarthRadius;
    float AtmoRadius;
    float pad0, pad1;
    float RayleighScaleH;
    float MieScaleH;
    float MieG;
    float SunIntensity;
    float RayleighR, RayleighG, RayleighB;
    float OzoneScale;
    float SunZenithCos;
    float SunAzimuth;
    float LutWidth, LutHeight;
}

Texture2D<float4> TransmittanceLUT : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> MultiScatterLUT : register(u0);

static const float PI = 3.14159265358979;
static const int SAMPLE_COUNT = 64;       // directions on the sphere
static const int STEP_COUNT   = 20;       // integration steps per ray
static const float3 kOzoneAbsorption = float3(0.650e-6, 1.881e-6, 0.085e-6);

float RayleighDensity(float h) { return exp(-h / RayleighScaleH); }
float MieDensity(float h)      { return exp(-h / MieScaleH); }
float OzoneDensity(float h)
{
    float center = 25000.0;
    float width  = 15000.0;
    return max(0.0, 1.0 - abs(h - center) / width);
}

float RaySphereIntersect(float originY, float cosTheta, float sphereRadius)
{
    float b = 2.0 * originY * cosTheta;
    float c = originY * originY - sphereRadius * sphereRadius;
    float disc = b * b - 4.0 * c;
    if (disc < 0.0) return -1.0;
    return (-b + sqrt(disc)) * 0.5;
}

// Sample transmittance LUT
float3 SampleTransmittance(float altitude, float cosZenith)
{
    float maxAlt = AtmoRadius - EarthRadius;
    float v = saturate(altitude / maxAlt);
    float u = cosZenith * 0.5 + 0.5;
    return TransmittanceLUT.SampleLevel(LinearSampler, float2(u, v), 0).rgb;
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= 32 || DTid.y >= 32)
        return;

    float u = (float(DTid.x) + 0.5) / 32.0;
    float v = (float(DTid.y) + 0.5) / 32.0;

    float sunCosZenith = 2.0 * u - 1.0;
    float maxAltitude  = AtmoRadius - EarthRadius;
    float h = v * maxAltitude;
    float r = EarthRadius + h;

    // Integrate over hemisphere of directions using uniform sphere sampling
    // We compute two quantities:
    //   L_2nd: total in-scattered luminance from 2nd-order scattering
    //   fms:   fraction of light that gets multi-scattered (for infinite series)
    float3 L_2nd = 0.0;
    float3 fms   = 0.0;

    for (int i = 0; i < SAMPLE_COUNT; i++)
    {
        // Uniform sphere sampling (Fibonacci spiral)
        float phi = 2.0 * PI * float(i) * 0.6180339887;  // golden ratio
        float cosTheta = 1.0 - 2.0 * (float(i) + 0.5) / float(SAMPLE_COUNT);
        float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));

        // Ray from current position in this direction
        float tMax = RaySphereIntersect(r, cosTheta, AtmoRadius);
        if (tMax < 0.0) continue;

        // Check ground intersection
        float tGround = RaySphereIntersect(r, cosTheta, EarthRadius);
        bool  hitGround = (tGround > 0.0);
        if (hitGround) tMax = min(tMax, tGround);

        float dt = tMax / float(STEP_COUNT);

        // Integrate scattering along this ray
        float3 throughput = 1.0;
        float3 inScatter  = 0.0;

        for (int s = 0; s < STEP_COUNT; s++)
        {
            float t = (float(s) + 0.5) * dt;

            float px = sinTheta * t;
            float py = r + cosTheta * t;
            float sR = sqrt(px * px + py * py);
            float sH = sR - EarthRadius;
            if (sH < 0.0) break;

            float rho_r = RayleighDensity(sH);
            float rho_m = MieDensity(sH);
            float rho_o = OzoneDensity(sH);

            float3 rayleighScat = float3(RayleighR, RayleighG, RayleighB) * rho_r;
            float  mieScat      = 1e-5 * rho_m;
            float3 scattering   = rayleighScat + float3(mieScat, mieScat, mieScat);

            float3 rayleighExt  = float3(RayleighR, RayleighG, RayleighB) * rho_r;
            float  mieExt       = 1.11e-5 * rho_m;
            float3 ozoneExt     = kOzoneAbsorption * OzoneScale * rho_o;
            float3 extinction   = rayleighExt + float3(mieExt, mieExt, mieExt) + ozoneExt;

            float3 sampleTransmittance = exp(-extinction * dt);

            // Transmittance from sample point to sun
            float cosSunAtSample = (py * sunCosZenith) / sR;  // approximate
            float3 sunTrans = SampleTransmittance(sH, cosSunAtSample);

            // Isotropic phase for multi-scattering: 1/(4*PI)
            float3 inscatterSample = scattering * sunTrans * (1.0 / (4.0 * PI));

            // Integrate with throughput (energy-conserving trapezoidal)
            float3 intScatter = (inscatterSample - inscatterSample * sampleTransmittance) /
                                max(extinction, 1e-10);
            inScatter  += throughput * intScatter;
            fms        += throughput * (scattering - scattering * sampleTransmittance) /
                          max(extinction, 1e-10);
            throughput *= sampleTransmittance;
        }

        // Ground albedo contribution
        if (hitGround)
        {
            float3 groundTrans = SampleTransmittance(0, sunCosZenith);
            float groundAlbedo = 0.3;
            inScatter += throughput * groundAlbedo * groundTrans * max(sunCosZenith, 0.0) / PI;
        }

        // Solid angle weight: uniform sphere = 4*PI / SAMPLE_COUNT
        float weight = 4.0 * PI / float(SAMPLE_COUNT);
        L_2nd += inScatter * weight;
        // fms already accumulated per step
    }

    // Scale fms by sphere weight
    fms *= (4.0 * PI / float(SAMPLE_COUNT));

    // Infinite series: L_ms = L_2nd / (1 - fms)
    // Clamp fms to prevent divergence
    float3 multiScatter = L_2nd / max(1.0 - fms, 0.001);

    MultiScatterLUT[DTid.xy] = float4(multiScatter, 1.0);
}
)HLSL";

// ── Aerial perspective compute shader ─────────────────────────────────────
// 3D LUT: (x=screen U, y=screen V, z=distance) → inscattering + extinction
static const char kAerialCS[] = R"HLSL(
// Aerial perspective LUT — 32x32x32 view-dependent inscattering
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Reference: Hillaire 2020, "A Scalable and Production Ready Sky and
// Atmosphere Rendering Technique" (Section 5.4)
//
// 3D LUT parameterized by:
//   x -> view direction azimuth (0..2PI, relative to sun)
//   y -> view direction elevation (-PI/2..PI/2)
//   z -> distance from camera (0..MaxAerialDist, non-linear)
//
// Each voxel stores accumulated inscattering (rgb) and transmittance (a)
// along the view ray from the camera up to the given distance.

cbuffer AtmoCB : register(b0)
{
    float EarthRadius;
    float AtmoRadius;
    float pad0, pad1;
    float RayleighScaleH;
    float MieScaleH;
    float MieG;
    float SunIntensity;
    float RayleighR, RayleighG, RayleighB;
    float OzoneScale;
    float SunZenithCos;
    float SunAzimuth;
    float LutWidth, LutHeight;
}

Texture2D<float4> TransmittanceLUT : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture3D<float4> AerialLUT : register(u0);

static const float PI = 3.14159265358979;
static const float MAX_AERIAL_DIST = 32000.0;  // meters
static const float CAMERA_ALTITUDE = 2.0;       // meters above ground (Skyrim player height)
static const int   STEP_COUNT = 16;
static const float3 kOzoneAbsorption = float3(0.650e-6, 1.881e-6, 0.085e-6);

float RayleighDensity(float h) { return exp(-h / RayleighScaleH); }
float MieDensity(float h)      { return exp(-h / MieScaleH); }
float OzoneDensity(float h)
{
    float center = 25000.0;
    float width  = 15000.0;
    return max(0.0, 1.0 - abs(h - center) / width);
}

// Henyey-Greenstein phase function
float HGPhase(float cosTheta, float g)
{
    float g2 = g * g;
    float denom = 1.0 + g2 - 2.0 * g * cosTheta;
    return (1.0 - g2) / (4.0 * PI * pow(max(denom, 1e-6), 1.5));
}

// Rayleigh phase function
float RayleighPhase(float cosTheta)
{
    return (3.0 / (16.0 * PI)) * (1.0 + cosTheta * cosTheta);
}

float3 SampleTransmittance(float altitude, float cosZenith)
{
    float maxAlt = AtmoRadius - EarthRadius;
    float v = saturate(altitude / maxAlt);
    float u = cosZenith * 0.5 + 0.5;
    return TransmittanceLUT.SampleLevel(LinearSampler, float2(u, v), 0).rgb;
}

[numthreads(4, 4, 4)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid >= 32))
        return;

    // Parameterize the 3D LUT
    float u = (float(DTid.x) + 0.5) / 32.0;  // azimuth relative to sun: [0, 2*PI]
    float v = (float(DTid.y) + 0.5) / 32.0;  // elevation: [-PI/2, PI/2]
    float w = (float(DTid.z) + 0.5) / 32.0;  // distance: [0, MAX_AERIAL_DIST] (squared mapping)

    float azimuth   = u * 2.0 * PI;
    float elevation = (v - 0.5) * PI;
    float distance  = w * w * MAX_AERIAL_DIST;  // Quadratic mapping concentrates near camera

    // View direction in local coords (Y=up, sun in XY plane)
    float cosElev = cos(elevation);
    float sinElev = sin(elevation);
    float3 viewDir = float3(cosElev * cos(azimuth), sinElev, cosElev * sin(azimuth));

    // Camera position: at ground level + small altitude
    float camR = EarthRadius + CAMERA_ALTITUDE;

    // Sun direction (in same coords, sun is in the XY plane at given zenith)
    float sunSinZ = sqrt(max(0.0, 1.0 - SunZenithCos * SunZenithCos));
    float3 sunDir = float3(sunSinZ, SunZenithCos, 0.0);

    // Cos angle between view and sun (for phase functions)
    float cosViewSun = dot(viewDir, sunDir);

    // March along the view ray, accumulating inscatter and transmittance
    float3 inscatter    = 0.0;
    float3 throughput   = 1.0;
    float  dt = distance / float(STEP_COUNT);

    for (int i = 0; i < STEP_COUNT; i++)
    {
        float t = (float(i) + 0.5) * dt;

        // Sample position: camera at (0, camR, 0) + viewDir * t
        float3 pos = float3(0.0, camR, 0.0) + viewDir * t;
        float sampleR = length(pos);
        float sampleH = sampleR - EarthRadius;

        if (sampleH < 0.0) break;   // hit ground
        if (sampleH > AtmoRadius - EarthRadius) continue;  // above atmosphere

        float rho_r = RayleighDensity(sampleH);
        float rho_m = MieDensity(sampleH);

        float3 rayleighScat = float3(RayleighR, RayleighG, RayleighB) * rho_r;
        float  mieScat      = 1e-5 * rho_m;

        float3 rayleighExt  = rayleighScat;
        float  mieExt       = 1.11e-5 * rho_m;
        float  rho_o        = OzoneDensity(sampleH);
        float3 ozoneExt     = kOzoneAbsorption * OzoneScale * rho_o;
        float3 extinction   = rayleighExt + float3(mieExt, mieExt, mieExt) + ozoneExt;

        float3 stepTransmittance = exp(-extinction * dt);

        // Sun transmittance from sample point to top of atmosphere
        float cosSunZenithAtSample = pos.y / max(sampleR, 1e-6);  // approximate: sun angle at sample
        float3 sunTrans = SampleTransmittance(sampleH, cosSunZenithAtSample);

        // In-scattering: Rayleigh + Mie phase-weighted
        float3 scatter = rayleighScat * RayleighPhase(cosViewSun) +
                         float3(mieScat, mieScat, mieScat) * HGPhase(cosViewSun, MieG);
        float3 inScatterSample = scatter * sunTrans * SunIntensity;

        // Integrate (energy-conserving)
        float3 intScatter = (inScatterSample - inScatterSample * stepTransmittance) /
                            max(extinction, 1e-10);
        inscatter  += throughput * intScatter;
        throughput *= stepTransmittance;
    }

    // Store: RGB = accumulated inscattering, A = average transmittance
    float avgTransmittance = dot(throughput, 1.0 / 3.0);
    AerialLUT[DTid] = float4(inscatter, avgTransmittance);
}
)HLSL";

// ── Celestial body compute shader (sun disk, moon, stars) ─────────────────
static const char kCelestialCS[] = R"HLSL(
// Celestial body rendering — sun disc, moon, stars
// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Renders celestial bodies onto a full-resolution texture:
//   - Sun disc with Hestroffer limb darkening
//   - Moon disc with phase-dependent illumination
//   - Procedural star field (hash-based)
//
// All bodies are attenuated by atmospheric transmittance from the
// precomputed LUT (observer assumed at ground level).
//
// Reference: Hestroffer & Magnan 1998 (limb darkening coefficient)

cbuffer CelestialCB : register(b0)
{
    float SunZenithCos, SunAzimuth;
    float MoonZenithCos, MoonAzimuth;
    float MoonPhase;
    float StarIntensity;
    float SunDiskIntensity;
    float SunLuminance;
    uint2 ScreenDims;
    float2 pad;
}

Texture2D<float4> TransmittanceLUT : register(t0);
SamplerState LinearSampler : register(s0);
RWTexture2D<float4> CelestialOutput : register(u0);

static const float PI           = 3.14159265358979;
static const float SUN_ANGULAR  = 0.00935;   // sun angular radius in radians (~0.535 deg)
static const float MOON_ANGULAR = 0.00907;   // moon angular radius (~0.52 deg)
static const float EARTH_RADIUS = 6360000.0;
static const float ATMO_RADIUS  = 6460000.0;

// Sample transmittance LUT at ground level for a given zenith angle
float3 SampleTransmittance(float cosZenith)
{
    float maxAlt = ATMO_RADIUS - EARTH_RADIUS;
    float v = 0.0;  // ground level
    float u = cosZenith * 0.5 + 0.5;
    return TransmittanceLUT.SampleLevel(LinearSampler, float2(u, v), 0).rgb;
}

// Simple hash for star placement
float Hash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float Hash2(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (any(DTid.xy >= ScreenDims))
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(ScreenDims);
    float4 result = float4(0.0, 0.0, 0.0, 0.0);

    // Map pixel UV to a view direction on the sky hemisphere
    // Assume equirectangular-ish mapping for sky dome
    float azimuth   = uv.x * 2.0 * PI;
    float elevation = (1.0 - uv.y) * PI * 0.5;  // 0 at horizon, PI/2 at zenith

    float cosElev = cos(elevation);
    float sinElev = sin(elevation);
    float3 viewDir = float3(cosElev * cos(azimuth), sinElev, cosElev * sin(azimuth));

    // ── Sun direction ────────────────────────────────────────────────
    float sunSinZ = sqrt(max(0.0, 1.0 - SunZenithCos * SunZenithCos));
    float3 sunDir = float3(sunSinZ * cos(SunAzimuth), SunZenithCos, sunSinZ * sin(SunAzimuth));

    // ── Moon direction ───────────────────────────────────────────────
    float moonSinZ = sqrt(max(0.0, 1.0 - MoonZenithCos * MoonZenithCos));
    float3 moonDir = float3(moonSinZ * cos(MoonAzimuth), MoonZenithCos, moonSinZ * sin(MoonAzimuth));

    // ── Sun disc ─────────────────────────────────────────────────────
    float cosSunAngle = dot(viewDir, sunDir);
    float sunAngle = acos(clamp(cosSunAngle, -1.0, 1.0));

    if (sunAngle < SUN_ANGULAR)
    {
        // Hestroffer limb darkening: I(r) = I0 * (1 - r^2)^alpha
        // where r = angular_distance / angular_radius, alpha ~ 0.3
        float r = sunAngle / SUN_ANGULAR;
        float mu = sqrt(max(0.0, 1.0 - r * r));
        float limbDarkening = pow(mu, 0.3);  // Hestroffer alpha = 0.3

        // Atmospheric transmittance toward sun
        float3 sunTransmittance = SampleTransmittance(SunZenithCos);

        // Sun color: warm blackbody ~5778K, attenuated by atmosphere
        float3 sunColor = float3(1.0, 0.95, 0.9) * SunDiskIntensity * limbDarkening;
        result.rgb += sunColor * sunTransmittance;
    }

    // ── Moon disc ────────────────────────────────────────────────────
    float cosMoonAngle = dot(viewDir, moonDir);
    float moonAngle = acos(clamp(cosMoonAngle, -1.0, 1.0));

    if (moonAngle < MOON_ANGULAR && MoonZenithCos > -0.1)
    {
        float r = moonAngle / MOON_ANGULAR;

        // Phase illumination: simple Lambertian model
        // MoonPhase: 0=new, 0.5=full, 1=new again
        float phase = abs(2.0 * MoonPhase - 1.0);  // 0=new moon, 1=full moon

        // Disc coordinates for phase terminator
        float2 discUV = float2(0.0, 0.0);
        if (moonAngle > 0.0)
        {
            // Project view dir onto disc plane (approximate)
            float3 toView = normalize(viewDir - moonDir * cosMoonAngle);
            discUV = float2(dot(toView, float3(1,0,0)), dot(toView, float3(0,0,1))) * r;
        }

        // Phase mask: illuminate based on moon phase
        float phaseMask = smoothstep(-0.1, 0.1, discUV.x * (2.0 * phase - 1.0) + phase);
        phaseMask = lerp(phaseMask, 1.0, phase);  // full moon = fully lit

        // Soft edge
        float edge = 1.0 - smoothstep(0.9, 1.0, r);

        // Atmospheric transmittance toward moon
        float3 moonTransmittance = SampleTransmittance(MoonZenithCos);

        // Moon albedo ~ 0.12, illuminated by sun
        float moonLuminance = 0.12 * SunLuminance * 0.01;  // much dimmer than sun
        float3 moonColor = float3(0.9, 0.92, 1.0) * moonLuminance * phaseMask * edge;
        result.rgb += moonColor * moonTransmittance;
    }

    // ── Stars ────────────────────────────────────────────────────────
    // Only visible when sun is below/near horizon
    float starVisibility = saturate(-SunZenithCos * 5.0 + 0.5);

    if (starVisibility > 0.0 && StarIntensity > 0.0)
    {
        // Grid-based star placement: divide sky into cells
        float2 starUV = float2(azimuth / (2.0 * PI), elevation / (PI * 0.5));
        float2 cellSize = float2(0.005, 0.01);  // ~200x100 cells
        float2 cell = floor(starUV / cellSize);

        // Random star in each cell
        float starRand = Hash(cell);
        float starRand2 = Hash2(cell);

        if (starRand > 0.97)  // ~3% of cells have stars
        {
            // Star position within cell
            float2 starPos = (cell + float2(Hash(cell + 1.0), Hash(cell + 2.0))) * cellSize;
            float2 diff = starUV - starPos;
            float dist = length(diff / cellSize);

            if (dist < 0.3)
            {
                // Star brightness variation
                float brightness = pow(starRand2, 3.0) * StarIntensity;

                // Star color: temperature variation
                float temp = starRand2;
                float3 starColor;
                if (temp < 0.3)
                    starColor = float3(1.0, 0.7, 0.5);   // cool red
                else if (temp < 0.7)
                    starColor = float3(1.0, 1.0, 0.95);  // white
                else
                    starColor = float3(0.7, 0.8, 1.0);   // hot blue

                // Point-like falloff
                float falloff = exp(-dist * dist * 50.0);

                // Atmospheric transmittance
                float3 trans = SampleTransmittance(max(sinElev, 0.0));

                result.rgb += starColor * brightness * falloff * starVisibility * trans;
            }
        }
    }

    // Alpha: 0 where no celestial body was rendered (for compositing)
    result.a = saturate(dot(result.rgb, float3(0.299, 0.587, 0.114)));

    CelestialOutput[DTid.xy] = result;
}
)HLSL";

// ── Initialize ────────────────────────────────────────────────────────────

bool AtmosphereRenderer::Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx, IDXGISwapChain* sc)
{
    if (m_initialized) return true;
    m_device = dev;
    m_context = ctx;

    // Get screen dimensions from swap chain
    DXGI_SWAP_CHAIN_DESC scDesc;
    if (sc && SUCCEEDED(sc->GetDesc(&scDesc))) {
        m_screenW = scDesc.BufferDesc.Width;
        m_screenH = scDesc.BufferDesc.Height;
    } else {
        m_screenW = 1920;
        m_screenH = 1080;
    }

    auto& cm = ComputeManager::Get();
    if (!cm.IsInitialized()) return false;

    // Compile compute shaders
    m_transmittanceCS = cm.CompileShader("AtmoTransmittance", kTransmittanceCS);
    m_scatteringCS    = cm.CompileShader("AtmoMultiScatter", kMultiScatterCS);
    m_aerialCS        = cm.CompileShader("AtmoAerial", kAerialCS);

    if (!m_transmittanceCS || !m_scatteringCS || !m_aerialCS) {
        SKSE::log::error("AtmosphereRenderer: failed to compile compute shaders");
        return false;
    }

    HRESULT hr;

    // Create Transmittance LUT (256x64)
    {
        auto res = cm.CreateTexture2D(256, 64, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      true, true, 1, false, "TransmittanceLUT");
        if (!res.Valid()) return false;
        m_transmittanceTex = res.texture;
        m_transmittanceSRV = res.srv;
        m_transmittanceUAV = res.uav;
    }

    // Create Multi-scattering LUT (32x32)
    {
        auto res = cm.CreateTexture2D(32, 32, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      true, true, 1, false, "MultiScatterLUT");
        if (!res.Valid()) return false;
        m_scatteringTex = res.texture;
        m_scatteringSRV = res.srv;
        m_scatteringUAV = res.uav;
    }

    // Create Aerial perspective LUT (32x32x32 Texture3D)
    {
        D3D11_TEXTURE3D_DESC desc = {};
        desc.Width = desc.Height = desc.Depth = 32;
        desc.MipLevels = 1;
        desc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS;
        hr = dev->CreateTexture3D(&desc, nullptr, &m_aerialTex);
        if (FAILED(hr)) return false;

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = desc.Format;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        srvDesc.Texture3D.MipLevels = 1;
        hr = dev->CreateShaderResourceView(m_aerialTex, &srvDesc, &m_aerialSRV);
        if (FAILED(hr)) return false;

        D3D11_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
        uavDesc.Format = desc.Format;
        uavDesc.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D;
        uavDesc.Texture3D.WSize = 32;
        hr = dev->CreateUnorderedAccessView(m_aerialTex, &uavDesc, &m_aerialUAV);
        if (FAILED(hr)) return false;
    }

    // Constants CB
    m_atmoCB = cm.CreateConstantBuffer(sizeof(AtmosphereCB));
    if (!m_atmoCB) return false;

    // Linear sampler for LUT reads
    {
        D3D11_SAMPLER_DESC sd = {};
        sd.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
        sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
        hr = dev->CreateSamplerState(&sd, &m_linearSampler);
        if (FAILED(hr)) return false;
    }

    // Register SRVs for injection
    SRVInjector::Get().RegisterSRV(kTransmittanceLUTSlot, m_transmittanceSRV);
    SRVInjector::Get().RegisterSRV(kScatteringLUTSlot, m_scatteringSRV);

    // ── Celestial body resources ──────────────────────────────────────────
    // Compile celestial compute shader
    m_celestialCS = cm.CompileShader("CelestialBodies", kCelestialCS);
    if (!m_celestialCS) {
        SKSE::log::error("AtmosphereRenderer: failed to compile celestial CS");
        return false;
    }

    // Create celestial render target (full-res R16G16B16A16_FLOAT with SRV + UAV)
    {
        auto res = cm.CreateTexture2D(m_screenW, m_screenH, DXGI_FORMAT_R16G16B16A16_FLOAT,
                                      true, true, 1, false, "CelestialRT");
        if (!res.Valid()) {
            SKSE::log::error("AtmosphereRenderer: failed to create celestial RT ({}x{})", m_screenW, m_screenH);
            return false;
        }
        m_celestialTex = res.texture;
        m_celestialSRV = res.srv;
        m_celestialUAV = res.uav;
    }

    // Create celestial constant buffer
    m_celestialCB = cm.CreateConstantBuffer(sizeof(CelestialCB));
    if (!m_celestialCB) {
        SKSE::log::error("AtmosphereRenderer: failed to create celestial CB");
        return false;
    }

    // Register celestial SRV for injection at t25
    SRVInjector::Get().RegisterSRV(kCelestialSRVSlot, m_celestialSRV);

    // Initial LUT computation (sun at noon)
    UpdateLUTs(1.0f, 0.0f);

    m_initialized = true;
    SKSE::log::info("AtmosphereRenderer: initialized (transmittance 256x64, scatter 32x32, aerial 32^3, celestial {}x{})",
                    m_screenW, m_screenH);
    return true;
}

void AtmosphereRenderer::Shutdown()
{
    auto SafeRelease = [](auto*& p) { if (p) { p->Release(); p = nullptr; } };
    SafeRelease(m_transmittanceTex);
    SafeRelease(m_transmittanceSRV);
    SafeRelease(m_transmittanceUAV);
    SafeRelease(m_scatteringTex);
    SafeRelease(m_scatteringSRV);
    SafeRelease(m_scatteringUAV);
    SafeRelease(m_aerialTex);
    SafeRelease(m_aerialSRV);
    SafeRelease(m_aerialUAV);
    SafeRelease(m_atmoCB);
    SafeRelease(m_linearSampler);
    SafeRelease(m_celestialTex);
    SafeRelease(m_celestialSRV);
    SafeRelease(m_celestialUAV);
    SafeRelease(m_celestialCB);
    m_initialized = false;
}

// ── Update LUTs when sun position changes ─────────────────────────────────

void AtmosphereRenderer::UpdateLUTs(float sunZenithCos, float sunAzimuth)
{
    if (!m_initialized) return;

    // Skip recomputation if sun hasn't moved significantly
    float delta = std::abs(sunZenithCos - m_lastSunZenith);
    if (delta < 0.01f && m_lastSunZenith > -900.0f) return;
    m_lastSunZenith = sunZenithCos;

    auto& cm = ComputeManager::Get();

    // Update CB with Earth atmosphere parameters
    AtmosphereCB cb;
    cb.earthRadius      = 6360000.0f;   // meters
    cb.atmosphereRadius = 6460000.0f;
    cb.rayleighScaleH   = 8000.0f;
    cb.mieScaleH        = 1200.0f;
    cb.mieG             = 0.8f;
    cb.sunIntensity     = 20.0f;
    // Rayleigh scattering coefficients at sea level (per meter)
    cb.rayleighR        = 5.802e-6f;
    cb.rayleighG        = 13.558e-6f;
    cb.rayleighB        = 33.1e-6f;
    cb.ozoneScale       = 1.0f;
    cb.sunZenithCos     = sunZenithCos;
    cb.sunAzimuth       = sunAzimuth;
    cb.lutWidth         = 256.0f;
    cb.lutHeight        = 64.0f;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (SUCCEEDED(m_context->Map(m_atmoCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
        std::memcpy(mapped.pData, &cb, sizeof(cb));
        m_context->Unmap(m_atmoCB, 0);
    }

    cm.SaveCSState();

    // Pass 1: Transmittance LUT
    {
        cm.CSSetCBs(0, 1, &m_atmoCB);
        ID3D11UnorderedAccessView* uavs[] = { m_transmittanceUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_transmittanceCS, 256 / 8, 64 / 8, 1);
        cm.CSClearUAVs(0, 1);
    }

    // Pass 2: Multi-scattering LUT (reads transmittance)
    {
        cm.CSSetCBs(0, 1, &m_atmoCB);
        ID3D11ShaderResourceView* srvs[] = { m_transmittanceSRV };
        cm.CSSetSRVs(0, 1, srvs);
        ID3D11SamplerState* samplers[] = { m_linearSampler };
        cm.CSSetSamplers(0, 1, samplers);
        ID3D11UnorderedAccessView* uavs[] = { m_scatteringUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_scatteringCS, 32 / 8, 32 / 8, 1);
        cm.CSClearSRVs(0, 1);
        cm.CSClearUAVs(0, 1);
    }

    // Pass 3: Aerial perspective LUT (reads transmittance)
    {
        cm.CSSetCBs(0, 1, &m_atmoCB);
        ID3D11ShaderResourceView* srvs[] = { m_transmittanceSRV };
        cm.CSSetSRVs(0, 1, srvs);
        ID3D11SamplerState* samplers[] = { m_linearSampler };
        cm.CSSetSamplers(0, 1, samplers);
        ID3D11UnorderedAccessView* uavs[] = { m_aerialUAV };
        cm.CSSetUAVs(0, 1, uavs);
        cm.Dispatch(m_aerialCS, 32 / 4, 32 / 4, 32 / 4);
        cm.CSClearSRVs(0, 1);
        cm.CSClearUAVs(0, 1);
    }

    cm.RestoreCSState();

    // Render celestial bodies after LUTs are updated (they sample transmittance)
    RenderCelestials(sunZenithCos, sunAzimuth);
}

// ── Render celestial bodies (sun disk, moon, stars) ───────────────────────

void AtmosphereRenderer::RenderCelestials(float sunZenithCos, float sunAzimuth)
{
    if (!m_initialized || !m_celestialCS || !m_celestialCB || !m_celestialUAV)
        return;

    auto& cm = ComputeManager::Get();

    // Fill celestial constant buffer
    CelestialCB cb;
    cb.sunZenithCos    = sunZenithCos;
    cb.sunAzimuth      = sunAzimuth;
    cb.moonZenithCos   = m_moonZenithCos;
    cb.moonAzimuth     = m_moonAzimuth;
    cb.moonPhase       = m_moonPhase;
    cb.starIntensity   = m_starIntensity;
    cb.sunDiskIntensity = m_sunDiskIntensity;
    cb.sunIntensity    = 20.0f;  // Match atmosphere sun luminance
    cb.screenW         = m_screenW;
    cb.screenH         = m_screenH;
    cb.pad[0] = cb.pad[1] = 0.0f;

    D3D11_MAPPED_SUBRESOURCE mapped;
    if (FAILED(m_context->Map(m_celestialCB, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped)))
        return;
    std::memcpy(mapped.pData, &cb, sizeof(cb));
    m_context->Unmap(m_celestialCB, 0);

    cm.SaveCSState();

    // Bind resources: CB at b0, transmittance LUT at t0 + sampler at s0, output UAV at u0
    cm.CSSetCBs(0, 1, &m_celestialCB);
    ID3D11ShaderResourceView* srvs[] = { m_transmittanceSRV };
    cm.CSSetSRVs(0, 1, srvs);
    ID3D11SamplerState* samplers[] = { m_linearSampler };
    cm.CSSetSamplers(0, 1, samplers);
    ID3D11UnorderedAccessView* uavs[] = { m_celestialUAV };
    cm.CSSetUAVs(0, 1, uavs);

    // Dispatch: 8x8 thread groups over full screen resolution
    uint32_t groupsX = (m_screenW + 7) / 8;
    uint32_t groupsY = (m_screenH + 7) / 8;
    cm.Dispatch(m_celestialCS, groupsX, groupsY, 1);

    cm.CSClearSRVs(0, 1);
    cm.CSClearUAVs(0, 1);

    cm.RestoreCSState();
}

void AtmosphereRenderer::ExecuteSkyPass(PassContext& ctx)
{
    // TODO: Replace game sky shader with physically-based evaluation
    // using the precomputed LUTs. For now, LUTs are available to any
    // shader that samples t23/t24.
}

} // namespace SB
