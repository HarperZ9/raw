// AtmosphereLut.hlsl: First-Principles Physical Planetary Atmosphere LUTs (Hillaire SIGGRAPH 2020, SM 6.6 / 6.10).
//   transmittance : 256x64 RGBA16F optical transmittance LUT (Rayleigh + Mie + Chappuis Ozone 600nm absorption band)
//   multiscat     : 32x32  RGBA16F infinite-order multiple-scattering LUT (64-dir Fibonacci quadrature, Psi_ms)
//   skyview       : 192x108 RGBA16F per-frame radiative-transfer Sky-View LUT coupling live weather humidity/turbidity,
//                   Chappuis ozone twilight (indigo zenith, rose Belt of Venus, blue-slate Earth Shadow), and
//                   seamless RawCosmosSky (galaxy, nebulae, 3D curtain auroras) above the atmospheric extinction curve.
// Original work, MIT.
#include "LightingCommon.hlsli"
#include "RawCosmosSky.hlsli"

Texture2D<float4> TransLut     : register(t0);
Texture2D<float4> MultiScatLut : register(t1);
SamplerState      Lin          : register(s0);

// Physical planetary atmosphere constants (km): Nordic/Earth reference with Rayleigh, Mie aerosol & Chappuis Ozone.
static const float  kPlanetR   = 6360.0f;
static const float  kAtmoR     = 6460.0f;
static const float  kRayH      = 8.0f;
static const float  kMieH      = 1.2f;
static const float3 kBetaRay   = float3(5.802e-3f, 13.558e-3f, 33.100e-3f);
static const float  kBetaMieS  = 3.996e-3f;
static const float  kBetaMieE  = 4.440e-3f;
// Chappuis Ozone Absorption Band (peak ~575..605 nm orange-red/yellow-green, minimal ~460 nm blue):
// Long grazing twilight paths through the 10..42 km stratospheric ozone layer absorb orange-red/green while
// transmitting deep blue-indigo -> authentic indigo twilight zenith, rose-pink Belt of Venus & slate Earth Shadow!
static const float3 kBetaOzo   = float3(1.180e-3f, 2.780e-3f, 0.112e-3f);

float2 raySphere(float3 ro, float3 rd, float rad) {
    const float b = dot(ro, rd);
    const float c = dot(ro, ro) - rad * rad;
    const float d = b * b - c;
    if (d < 0.0f) { return float2(-1.0f, -1.0f); }
    const float s = sqrt(d);
    return float2(-b - s, -b + s);
}

// Smooth stratospheric Chapman-like ozone profile peaking at 25 km (10..42 km stratospheric layer)
// + altitude-dependent Rayleigh & humidity/turbidity-coupled Mie aerosol extinction.
void atmoExtinction(float hKm, float turbidity, float mieScaleH, out float3 sigmaS_R, out float sigmaS_M, out float3 sigmaT) {
    const float hCl = max(hKm, 0.0f);
    const float dR = exp(-hCl / kRayH);
    const float dM = exp(-hCl / max(mieScaleH, 0.8f)) * turbidity;
    // Asymmetric stratospheric ozone layer: sharper lower tropopause cutoff (~10 km), gentler upper tail (~42 km):
    const float oLo = saturate((hCl - 10.0f) / 15.0f);
    const float oHi = saturate((42.0f - hCl) / 17.0f);
    const float dO  = (hCl < 25.0f) ? (oLo * oLo * (3.0f - 2.0f * oLo)) : (oHi * oHi * (3.0f - 2.0f * oHi));
    sigmaS_R = kBetaRay * dR;
    sigmaS_M = kBetaMieS * dM;
    sigmaT   = sigmaS_R + (kBetaMieE * dM).xxx + kBetaOzo * dO;
}

float2 transUv(float r, float mu) {
    const float H = sqrt(max(kAtmoR * kAtmoR - kPlanetR * kPlanetR, 1.0f));
    const float rho = sqrt(max(r * r - kPlanetR * kPlanetR, 0.0f));
    const float d = max(0.0f, -r * mu + sqrt(max(r * r * (mu * mu - 1.0f) + kAtmoR * kAtmoR, 0.0f)));
    const float dMin = kAtmoR - r;
    const float dMax = rho + H;
    return float2(saturate((d - dMin) / max(dMax - dMin, 1.0e-4f)), saturate(rho / H));
}

// 256x64 Transmittance LUT: 40-step exact optical depth integration along ray (r, mu) to top of atmosphere.
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
    const uint kSteps = 40u;
    const float dt = d / float(kSteps);
    float3 od = 0.0f.xxx;
    [loop] for (uint i = 0u; i < kSteps; i++) {
        const float t = (float(i) + 0.5f) * dt;
        const float h = length(ro + rd * t) - kPlanetR;
        float3 sR, sT; float sM;
        atmoExtinction(h, 1.0f, kMieH, sR, sM, sT);
        od += sT * dt;
    }
    return float4(exp(-od), 1.0f);
}

// 32x32 Hillaire SIGGRAPH 2020 Multiple-Scattering LUT (Psi_ms = L_2nd / (1 - f_ms)):
// Integrates infinite-order isotropic atmospheric multiple scattering over 64 Fibonacci-sphere directions
// (runs once on frame 0 -> 0.00 ms per frame) with exact Rayleigh + Mie + Chappuis Ozone extinction.
float4 multiscat(float4 at : SV_Position) : SV_Target0 {
    const float2 uv = at.xy / 32.0f;
    const float cosS = uv.x * 2.0f - 1.0f;
    const float r = kPlanetR + lerp(0.1f, kAtmoR - kPlanetR - 0.1f, uv.y);
    const float3 ro = float3(0.0f, 0.0f, r);
    const float3 sunD = float3(sqrt(max(1.0f - cosS * cosS, 0.0f)), 0.0f, cosS);
    const float3 groundAlbedo = float3(0.26f, 0.29f, 0.32f);

    float3 L2nd = 0.0f.xxx;
    float3 fMs  = 0.0f.xxx;
    const uint kDirs = 64u;
    const float dOmega = (4.0f * PI) / float(kDirs);
    [loop] for (uint d = 0u; d < kDirs; d++) {
        const float z = 1.0f - (2.0f * float(d) + 1.0f) / float(kDirs);
        const float rad = sqrt(max(1.0f - z * z, 0.0f));
        const float phi = float(d) * 2.39996323f;
        const float3 rd = float3(rad * cos(phi), rad * sin(phi), z);
        const float2 hitA = raySphere(ro, rd, kAtmoR);
        const float2 hitG = raySphere(ro, rd, kPlanetR);
        float tMax = hitA.y;
        const bool hitsGround = (hitG.x > 0.0f);
        if (hitsGround) { tMax = min(tMax, hitG.x); }
        const uint kSteps = 20u;
        const float dt = max(tMax, 0.0f) / float(kSteps);
        float3 Ldir = 0.0f.xxx, fDir = 0.0f.xxx, T = 1.0f.xxx;
        [loop] for (uint i = 0u; i < kSteps; i++) {
            const float t = (float(i) + 0.5f) * dt;
            const float3 p = ro + rd * t;
            const float rp = length(p);
            float3 sR, sT; float sM;
            atmoExtinction(rp - kPlanetR, 1.0f, kMieH, sR, sM, sT);
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

// 192x108 Sky-View LUT (non-linear elevation parameterization around horizon for crisp twilight & horizon gradients).
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
    const bool hitsGround = (hitGround.x > 0.0f);
    float tMax = hitAtmo.y;
    if (hitsGround) { tMax = min(tMax, hitGround.x); }
    tMax = max(tMax, 0.0f);

    const float3 sunD = astroSunDir();
    const float cosT = dot(rd, sunD);

    // Couple real humidity & aerosol turbidity from live weather (wetness, puddles, snow, rain, cloud deck) + altitude:
    // Clear alpine air -> low turbidity (~0.70, crisp cobalt sky, sharp forward Mie peak);
    // Humid/stormy air -> higher turbidity (~2.8..3.5), thicker aerosol boundary layer & broader droplet Mie phase!
    const float alpineClean = saturate((altKm - 0.35f) * 0.35f);
    const float humidity = saturate(weather.x * 0.48f + weather.y * 0.22f + weather.w * 0.85f + weather.z * 0.62f
                                  + saturate(clouds.x - 0.38f) * 0.72f);
    const float turbidity = lerp(0.74f - alpineClean * 0.10f, 3.35f, humidity);
    const float mieScaleH = lerp(1.12f, 1.88f, humidity);

    // Rayleigh phase + humidity-coupled tri-lobe Cornette-Shanks / Henyey-Greenstein Mie aerosol phase:
    const float phR = (3.0f / (16.0f * PI)) * (1.0f + cosT * cosT);
    const float g1 = lerp(0.84f, 0.72f, humidity);
    const float g2 = lerp(0.42f, 0.28f, humidity);
    const float g3 = -0.22f;
    const float phM1 = (1.0f - g1 * g1) / (4.0f * PI * pow(max(1.0f + g1 * g1 - 2.0f * g1 * cosT, 1.0e-4f), 1.5f));
    const float phM2 = (1.0f - g2 * g2) / (4.0f * PI * pow(max(1.0f + g2 * g2 - 2.0f * g2 * cosT, 1.0e-4f), 1.5f));
    const float phM3 = (1.0f - g3 * g3) / (4.0f * PI * pow(max(1.0f + g3 * g3 - 2.0f * g3 * cosT, 1.0e-4f), 1.5f));
    const float phM  = phM1 * lerp(0.74f, 0.56f, humidity)
                     + phM2 * lerp(0.24f, 0.34f, humidity)
                     + phM3 * lerp(0.02f, 0.10f, humidity);

    // 40-step quadratic-clustered full-dome radiative transfer (Rayleigh + Mie + Chappuis Ozone + Multi-Scattering):
    const uint kSteps = 40u;
    float3 L = 0.0f.xxx;
    float3 T = 1.0f.xxx;
    float tPrev = 0.0f;
    [loop] for (uint i = 0u; i < kSteps; i++) {
        const float u1 = (float(i) + 1.0f) / float(kSteps);
        const float tNext = tMax * (u1 * u1); // quadratic clustering near horizon
        const float dt = max(tNext - tPrev, 1.0e-4f);
        const float t = tPrev + 0.5f * dt;
        tPrev = tNext;
        const float3 p = ro + rd * t;
        const float r = length(p);
        const float h = r - kPlanetR;
        float3 sR, sT; float sM;
        atmoExtinction(h, turbidity, mieScaleH, sR, sM, sT);
        const float muS = dot(p / r, sunD);
        // Smooth planetary shadow cutoff across the limb so Earth's shadow rises naturally at twilight:
        const float2 pEarthHit = raySphere(p, sunD, kPlanetR - 0.5f);
        const float planetVis = (pEarthHit.x > 0.0f) ? 0.0f : 1.0f;
        const float3 transSun = TransLut.SampleLevel(Lin, transUv(r, muS), 0.0f).rgb * planetVis;
        const float2 msUv = float2(saturate(muS * 0.5f + 0.5f), saturate(h / (kAtmoR - kPlanetR)));
        const float3 psiMs = MultiScatLut.SampleLevel(Lin, msUv, 0.0f).rgb;
        const float3 multiScat = (sR + sM.xxx) * psiMs;
        const float3 scat = (sR * phR + sM.xxx * phM) * transSun + multiScat;
        const float3 stepT = exp(-sT * dt);
        L += T * (scat * (1.0f - stepT) / max(sT, 1.0e-5f.xxx));
        T *= stepT;
    }

    // Physical linear solar irradiance driven by astronomical sun elevation (sunD.z):
    // Smoothly traverses astronomical night -> nautical/civil twilight -> crisp Nordic alpine day!
    const float sunVisDay = smoothstep(-0.11f, 0.14f, sunD.z);
    const float3 solarIrr = float3(0.96f, 1.00f, 1.07f) * (9.4f * sunVisDay);
    float3 outSky = L * solarIrr;

    // Clear alpine cobalt zenith refinement (recedes smoothly with humidity/overcast):
    const float clearAlpine = (1.0f - humidity * 0.72f) * smoothstep(0.06f, 0.26f, sunD.z);
    outSky += float3(0.028f, 0.120f, 0.345f) * (clearAlpine * smoothstep(0.04f, 0.65f, rd.z));

    // Physical Chappuis-Ozone Twilight Architecture (strictly gated to real sunset/sunrise: -0.13 < sunD.z < +0.14):
    //   1) Solar horizon ember/amber forward scatter glow
    //   2) Deep Chappuis indigo zenith tint
    //   3) Rose-pink Belt of Venus antitwilight arch opposite the sun (above rising Earth Shadow)
    //   4) Blue-slate Earth Shadow band rising from the anti-solar horizon below Belt of Venus!
    const float twilightW = smoothstep(-0.13f, -0.01f, sunD.z) * (1.0f - smoothstep(0.04f, 0.14f, sunD.z));
    if (twilightW > 0.005f) {
        const float clearTw = twilightW * (1.0f - saturate(clouds.x - 0.45f) * 1.15f);
        const float2 rdHor = normalize(rd.xy + float2(1.0e-5f, 0.0f));
        const float2 sunHor = normalize(sunD.xy + float2(1.0e-5f, 0.0f));
        const float solarAz = saturate(dot(rdHor, sunHor));
        const float antiAz  = saturate(dot(rdHor, -sunHor));

        // Earth shadow rises in the anti-solar sky as the sun sinks below +0.05 rad:
        const float shadowElev = clamp(0.036f - sunD.z * 1.18f, 0.010f, 0.26f);
        const float earthShadow = (1.0f - smoothstep(shadowElev - 0.032f, shadowElev + 0.046f, rd.z))
                                * pow(antiAz, 1.45f) * smoothstep(-0.02f, 0.015f, rd.z);
        const float beltOfVenus = exp(-pow((rd.z - (shadowElev + 0.048f)) / 0.058f, 2.0f))
                                * pow(antiAz, 1.65f) * smoothstep(0.0f, 0.03f, rd.z);
        const float solarHorGlow = exp(-max(rd.z, 0.0f) * lerp(7.4f, 5.2f, humidity))
                                 * pow(solarAz, 2.3f) * smoothstep(-0.02f, 0.01f, rd.z);
        const float ozoneZenith = smoothstep(0.28f, 0.88f, rd.z) * smoothstep(-0.13f, -0.02f, sunD.z);

        outSky += float3(1.12f, 0.42f, 0.13f) * (solarHorGlow * clearTw * 0.78f);
        outSky += float3(0.48f, 0.185f, 0.34f) * (beltOfVenus * clearTw * 0.54f);
        outSky = lerp(outSky, outSky * float3(0.36f, 0.46f, 0.82f) + float3(0.012f, 0.020f, 0.048f), earthShadow * clearTw * 0.65f);
        outSky = lerp(outSky, outSky * float3(0.76f, 0.82f, 1.18f) + float3(0.008f, 0.014f, 0.042f), ozoneZenith * clearTw * 0.42f);
    }

    // Subtle deep-night Rayleigh moonlight/starlight scattering floor so night sky never collapses to black void:
    const float nightFloorW = smoothstep(0.02f, -0.10f, sunD.z);
    if (nightFloorW > 0.005f) {
        const float hUp = saturate(rd.z);
        const float3 nightBase = lerp(float3(0.018f, 0.029f, 0.058f), float3(0.006f, 0.012f, 0.030f), pow(hUp, 0.55f));
        outSky += nightBase * nightFloorW;
    }

    // Composite RawCosmosSky (Galactic Plane, Dark Dust Rifts, H-II Nebulae, 3D Volumetric Curtain Auroras,
    // Van Rhijn Airglow & 22-deg Ice Halo) seamlessly ABOVE our exact raymarched atmospheric transmittance T!
    // (Pinpoint stars are disabled in this 192x108 LUT to prevent sub-pixel texel aliasing; full-res LightFog
    //  and sharp EnvCubePass evaluate pinpoint O/B/A/F/G/K/M stars & meteors cleanly at native angular resolution.)
    if (!hitsGround && rd.z > -0.015f) {
        const float simTime = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
        const float gameHour = timeEnv.x;
        const float3 cosmosRaw = evalCosmosAndAuroraEx(
            rd, sunD, gameHour, simTime, clouds.x, weather.z, camPosW.y,
            false,  // no aliased single-pixel stars inside 192x108 LUT
            false   // apply exact raymarched top-of-atmosphere transmittance T below!
        );
        const float horSoft = smoothstep(-0.008f, 0.045f, rd.z);
        outSky += cosmosRaw * T * horSoft;
    }

    // Gentle HDR highlight shoulder preserves deep twilight indigo, rose Belt of Venus & vivid aurora greens:
    outSky = outSky / (1.0f + outSky * 0.36f);
    return float4(max(outSky, 0.0f.xxx), 1.0f);
}
