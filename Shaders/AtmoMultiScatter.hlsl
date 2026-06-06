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
