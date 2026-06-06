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
