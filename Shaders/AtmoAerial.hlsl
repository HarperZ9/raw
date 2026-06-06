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
