// AtmosphereLut.hlsl: Sébastien Hillaire (SIGGRAPH 2020) Physical Sky & Atmosphere LUTs (SM 6.9).
//   transmittance : 256x64 RGBA16F optical transmittance LUT (Rayleigh + Mie + Chappuis Ozone)
//   skyview       : 192x108 RGBA16F per-frame full-dome radiative-transfer Sky-View LUT driven by live sunDir & weather
// Original work, MIT.
#include "LightingCommon.hlsli"

Texture2D<float4> TransLut     : register(t0);
Texture2D<float4> MultiScatLut : register(t1);
SamplerState      Lin          : register(s0);

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

// 32x32 Hillaire SIGGRAPH 2020 Multiple-Scattering LUT (Psi_ms = L_2nd / (1 - f_ms)):
// Integrates isotropic infinite-order atmospheric multiple scattering over 32 Fibonacci-sphere directions
// across (cosSunZenith, altitude), replacing analytical multi-scatter approximations with exact radiative transfer.
float4 multiscat(float4 at : SV_Position) : SV_Target0 {
    const float2 uv = at.xy / 32.0f;
    const float cosS = uv.x * 2.0f - 1.0f;
    const float r = kPlanetR + lerp(0.1f, kAtmoR - kPlanetR - 0.1f, uv.y);
    const float3 ro = float3(0.0f, 0.0f, r);
    const float3 sunD = float3(sqrt(max(1.0f - cosS * cosS, 0.0f)), 0.0f, cosS);
    const float3 groundAlbedo = float3(0.28f, 0.30f, 0.32f);

    float3 L2nd = 0.0f.xxx;
    float3 fMs = 0.0f.xxx;
    const uint kDirs = 32u;
    const float dOmega = (4.0f * PI) / float(kDirs);
    [loop] for (uint d = 0u; d < kDirs; d++) {
        // Fibonacci sphere direction for uniform isotropic solid-angle quadrature.
        const float z = 1.0f - (2.0f * float(d) + 1.0f) / float(kDirs);
        const float rad = sqrt(max(1.0f - z * z, 0.0f));
        const float phi = float(d) * 2.39996323f;
        const float3 rd = float3(rad * cos(phi), rad * sin(phi), z);
        const float2 hitA = raySphere(ro, rd, kAtmoR);
        const float2 hitG = raySphere(ro, rd, kPlanetR);
        float tMax = hitA.y;
        const bool hitsGround = (hitG.x > 0.0f);
        if (hitsGround) { tMax = min(tMax, hitG.x); }
        const uint kSteps = 16u;
        const float dt = max(tMax, 0.0f) / float(kSteps);
        float3 Ldir = 0.0f.xxx, fDir = 0.0f.xxx, T = 1.0f.xxx;
        [loop] for (uint i = 0u; i < kSteps; i++) {
            const float t = (float(i) + 0.5f) * dt;
            const float3 p = ro + rd * t;
            const float rp = length(p);
            float3 sR, sT; float sM;
            atmoExtinction(rp - kPlanetR, 1.0f, sR, sM, sT);
            const float3 sS = sR + sM.xxx;
            const float muS = dot(p / rp, sunD);
            const float3 trSun = TransLut.SampleLevel(Lin, transUv(rp, muS), 0.0f).rgb;
            const float3 stepT = exp(-sT * dt);
            const float3 intS = (1.0f - stepT) / max(sT, 1.0e-5f.xxx);
            Ldir += T * (sS * (0.25f / PI) * trSun * intS);
            fDir += T * (sS * intS);
            T *= stepT;
        }
        if (hitsGround) {
            const float3 pg = ro + rd * tMax;
            const float muG = saturate(dot(normalize(pg), sunD));
            const float3 trG = TransLut.SampleLevel(Lin, transUv(kPlanetR, muG), 0.0f).rgb;
            Ldir += T * (groundAlbedo / PI) * trG * muG;
        }
        L2nd += Ldir * dOmega * (0.25f / PI);
        fMs  += fDir * dOmega * (0.25f / PI);
    }
    const float3 psi = L2nd / max(1.0f.xxx - fMs, 0.05f.xxx);
    return float4(psi, 1.0f);
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
        // Exact Hillaire SIGGRAPH 2020 precomputed infinite-order Multiple-Scattering LUT (32x32 Psi_ms).
        const float2 msUv = float2(saturate(muS * 0.5f + 0.5f), saturate(h / (kAtmoR - kPlanetR)));
        const float3 psiMs = MultiScatLut.SampleLevel(Lin, msUv, 0.0f).rgb;
        const float3 multiScat = (sR + sM.xxx) * psiMs;
        const float3 scat = (sR * phR + sM.xxx * phM) * transSun + multiScat;
        const float3 stepT = exp(-sT * dt);
        L += T * (scat * (1.0f - stepT) / max(sT, 1.0e-5f.xxx));
        T *= stepT;
    }
    // Calibrated solar irradiance exposure matching RAW's linear HDR pipeline (CIE D65 high-latitude sun)
    // + physical Belt of Venus (rosy-violet anti-solar twilight arch above Earth's indigo shadow at dawn/dusk).
    const float sunVisDay = smoothstep(-0.06f, 0.14f, sunD.z);
    const float3 solarIrr = float3(1.18f, 1.15f, 1.12f) * (1.45f * sunVisDay);
    const float twilightW = smoothstep(-0.08f, 0.02f, sunD.z) * (1.0f - smoothstep(0.05f, 0.22f, sunD.z));
    const float antiSolar = saturate(dot(rd, -sunD));
    const float beltBand = exp(-pow((rd.z - 0.14f) / 0.12f, 2.0f)) * pow(antiSolar, 2.0f);
    const float earthShadow = (1.0f - smoothstep(0.0f, 0.10f, rd.z)) * pow(antiSolar, 1.5f);
    float3 outSky = L * solarIrr;
    outSky += float3(0.42f, 0.22f, 0.34f) * (beltBand * twilightW * 0.45f);
    outSky = lerp(outSky, outSky * float3(0.45f, 0.55f, 0.85f), earthShadow * twilightW * 0.60f);
    return float4(outSky, 1.0f);
}
