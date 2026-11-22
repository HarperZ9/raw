// AtmosphereLut.hlsl: Sébastien Hillaire (SIGGRAPH 2020) Physical Sky & Atmosphere LUTs (SM 6.9).
//   transmittance : 256x64 RGBA16F optical transmittance LUT (Rayleigh + Mie + Chappuis Ozone)
//   skyview       : 192x108 RGBA16F per-frame full-dome radiative-transfer Sky-View LUT driven by live sunDir & weather
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> TransLut : register(t0);
SamplerState      Lin      : register(s0);

// Physical planetary atmosphere constants (km): Earth/Nordic reference with Rayleigh, Mie aerosol & Ozone absorption.
static const float kPlanetR  = 6360.0f;
static const float kAtmoR    = 6460.0f;
static const float kRayH     = 8.0f;
static const float kMieH     = 1.2f;
static const float3 kBetaRay = float3(5.802e-3f, 13.558e-3f, 33.100e-3f);
static const float  kBetaMieS = 3.996e-3f;
static const float  kBetaMieE = 4.440e-3f;
static const float3 kBetaOzo = float3(0.650e-3f, 1.881e-3f, 0.085e-3f);

float2 raySphere(float3 ro, float3 rd, float rad) {
    const float b = dot(ro, rd);
    const float c = dot(ro, ro) - rad * rad;
    const float d = b * b - c;
    if (d < 0.0f) { return float2(-1.0f, -1.0f); }
    const float s = sqrt(d);
    return float2(-b - s, -b + s);
}

void atmoExtinction(float hKm, float turbidity, out float3 sigmaS_R, out float sigmaS_M, out float3 sigmaT) {
    const float dR = exp(-max(hKm, 0.0f) / kRayH);
    const float dM = exp(-max(hKm, 0.0f) / kMieH) * turbidity;
    const float dO = max(0.0f, 1.0f - abs(hKm - 25.0f) / 15.0f);
    sigmaS_R = kBetaRay * dR;
    sigmaS_M = kBetaMieS * dM;
    sigmaT = sigmaS_R + (kBetaMieE * dM).xxx + kBetaOzo * dO;
}

float2 transUv(float r, float mu) {
    const float H = sqrt(max(kAtmoR * kAtmoR - kPlanetR * kPlanetR, 1.0f));
    const float rho = sqrt(max(r * r - kPlanetR * kPlanetR, 0.0f));
    const float d = max(0.0f, -r * mu + sqrt(max(r * r * (mu * mu - 1.0f) + kAtmoR * kAtmoR, 0.0f)));
    const float dMin = kAtmoR - r;
    const float dMax = rho + H;
    return float2(saturate((d - dMin) / max(dMax - dMin, 1.0e-4f)), saturate(rho / H));
}

// 256x64 Transmittance LUT: integrates optical depth along ray (r, mu) to top of atmosphere.
float4 transmittance(float4 at : SV_Position) : SV_Target0 {
    const float2 uv = at.xy / float2(256.0f, 64.0f);
    const float H = sqrt(kAtmoR * kAtmoR - kPlanetR * kPlanetR);
    const float rho = H * uv.y;
    const float r = sqrt(rho * rho + kPlanetR * kPlanetR);
    const float dMin = kAtmoR - r;
    const float dMax = rho + H;
    const float d = dMin + uv.x * (dMax - dMin);
    const float mu = d < 1.0e-4f ? 1.0f : clamp((H * H - rho * rho - d * d) / (2.0f * r * d), -1.0f, 1.0f);
    const float3 ro = float3(0.0f, 0.0f, r);
    const float3 rd = float3(sqrt(max(1.0f - mu * mu, 0.0f)), 0.0f, mu);
    const uint kSteps = 32u;
    const float dt = d / float(kSteps);
    float3 od = 0.0f.xxx;
    [loop] for (uint i = 0u; i < kSteps; i++) {
        const float t = (float(i) + 0.5f) * dt;
        const float h = length(ro + rd * t) - kPlanetR;
        float3 sR, sT; float sM;
        atmoExtinction(h, 1.0f, sR, sM, sT);
        od += sT * dt;
    }
    return float4(exp(-od), 1.0f);
}

// 192x108 Sky-View LUT (non-linear elevation parameterization around horizon for crisp horizon gradients).
float4 skyview(float4 at : SV_Position) : SV_Target0 {
    const float2 uv = at.xy / float2(192.0f, 108.0f);
    const float az = (uv.x - 0.5f) * 2.0f * PI;
    // Hillaire non-linear elevation mapping concentrates texels near the horizon (v = 0.5).
    const float vAdj = uv.y < 0.5f ? -pow(1.0f - 2.0f * uv.y, 2.0f) : pow(2.0f * uv.y - 1.0f, 2.0f);
    const float elev = vAdj * (0.5f * PI);
    const float cosE = cos(elev), sinE = sin(elev);
    const float3 rd = float3(cosE * cos(az), cosE * sin(az), sinE);

    const float altKm = max(camPosW.z * 0.0000143f, 0.0f) + 0.25f;
    const float3 ro = float3(0.0f, 0.0f, kPlanetR + altKm);
    const float2 hitAtmo = raySphere(ro, rd, kAtmoR);
    const float2 hitGround = raySphere(ro, rd, kPlanetR);
    float tMax = hitAtmo.y;
    if (hitGround.x > 0.0f) { tMax = min(tMax, hitGround.x); }
    tMax = max(tMax, 0.0f);

    const float3 sunD = normalize(sunDir.xyz);
    const float cosT = dot(rd, sunD);
    const float phR = (3.0f / (16.0f * PI)) * (1.0f + cosT * cosT);
    const float g = 0.78f;
    const float phM = (1.0f - g * g) / (4.0f * PI * pow(max(1.0f + g * g - 2.0f * g * cosT, 1.0e-4f), 1.5f));
    const float turbidity = 1.0f + saturate(clouds.x) * 1.4f + weather.w * 1.8f + weather.z * 1.6f;

    const uint kSteps = 24u;
    float3 L = 0.0f.xxx;
    float3 T = 1.0f.xxx;
    float tPrev = 0.0f;
    [loop] for (uint i = 0u; i < kSteps; i++) {
        const float u1 = (float(i) + 1.0f) / float(kSteps);
        const float tNext = tMax * (u1 * u1);   // quadratic clustering near horizon
        const float dt = max(tNext - tPrev, 1.0e-4f);
        const float t = tPrev + 0.5f * dt;
        tPrev = tNext;
        const float3 p = ro + rd * t;
        const float r = length(p);
        const float h = r - kPlanetR;
        float3 sR, sT; float sM;
        atmoExtinction(h, turbidity, sR, sM, sT);
        const float muS = dot(p / r, sunD);
        const float3 transSun = TransLut.SampleLevel(Lin, transUv(r, muS), 0.0f).rgb;
        // Analytical multi-scattering approximation (Hillaire 2020 isotropic ground-sky bounce).
        const float3 multiScat = (sR + sM.xxx) * (0.11f * saturate(sunD.z * 2.5f + 0.25f));
        const float3 scat = (sR * phR + sM.xxx * phM) * transSun + multiScat;
        const float3 stepT = exp(-sT * dt);
        L += T * (scat * (1.0f - stepT) / max(sT, 1.0e-5f.xxx));
        T *= stepT;
    }
    // Calibrated solar irradiance exposure matching RAW's linear HDR pipeline (CIE D65 high-latitude sun).
    const float sunVisDay = smoothstep(-0.06f, 0.14f, sunD.z);
    const float3 solarIrr = float3(1.18f, 1.15f, 1.12f) * (1.45f * sunVisDay);
    return float4(L * solarIrr, 1.0f);
}
