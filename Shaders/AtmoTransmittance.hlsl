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
