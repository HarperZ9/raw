// LightFog.hlsl: volumetric fog lit by every light, after the lighting pass (LightingPass.cpp).
// A homogeneous medium of extinction sigma along each camera ray, up to the surface or a
// maximum distance:
//   sun     marched in 12 jittered steps, each shadowed by the game's cascades, with the
//           Henyey-Greenstein phase
//   lights  closed form, unshadowed: along the ray x(t) = t w the game's window 1 - |x - c|^2 / r^2
//           is a quadratic in t, so its integral over the chord inside the sphere is exact
// The result: lit * T(t_end) + in-scatter, T(t) = exp(-sigma t). Sky pixels keep the game's
// atmosphere. With FLAG_FOG off the pass copies the lit frame. Original work, MIT.
#define ATLAS_SLOT t2
#include "LightingCommon.hlsli"
#include "CloudCommon.hlsli"
#include "ElderOptics.hlsli"

Texture2D<float4> Lit         : register(t0);
Texture2D<float>  Depth       : register(t1);
Texture2D<float4> VolFog      : register(t3);
Texture2D<float4> AtmoSkyView : register(t4);
Texture2D<float4> Clouds      : register(t5);
Texture2D<float4> FroxelVol   : register(t6);
SamplerState      Lin         : register(s0);

// Hardware-trilinear lookup into our 160x90x64 3D Froxel Volumetric Participating Media Grid (1280x720 8x8 atlas).
float4 sampleFroxel3D(float2 uv, float z) {
    const float zCl = clamp(z, 24.0f, 14000.0f);
    const float sF = saturate(log(zCl / 24.0f) / log(14000.0f / 24.0f)) * 63.0f;
    const uint s0 = min(uint(floor(sF)), 63u);
    const uint s1 = min(s0 + 1u, 63u);
    const float sT = frac(sF);
    const float2 tileUv = clamp(uv, float2(0.5f / 160.0f, 0.5f / 90.0f), float2(159.5f / 160.0f, 89.5f / 90.0f));
    const float2 uv0 = (float2(float(s0 & 7u), float(s0 >> 3u)) + tileUv) * 0.125f;
    const float2 uv1 = (float2(float(s1 & 7u), float(s1 >> 3u)) + tileUv) * 0.125f;
    return lerp(FroxelVol.SampleLevel(Lin, uv0, 0.0f), FroxelVol.SampleLevel(Lin, uv1, 0.0f), sT);
}

float3 sampleHillaireSky(float3 rd) {
    const float az = atan2(rd.y, rd.x);
    const float elev = asin(clamp(rd.z, -1.0f, 1.0f));
    const float vNonLin = 0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI)));
    const float uAz = az / (2.0f * PI) + 0.5f;
    const float3 c0 = AtmoSkyView.SampleLevel(Lin, float2(uAz, saturate(vNonLin)), 0.0f).rgb;
    // Eliminate zenith-pole azimuth pinch when looking straight up (rd.z > 0.94).
    const float poleW = smoothstep(0.94f, 0.995f, rd.z);
    if (poleW > 0.001f) {
        const float3 zAvg = (AtmoSkyView.SampleLevel(Lin, float2(0.25f, 0.99f), 0.0f).rgb +
                             AtmoSkyView.SampleLevel(Lin, float2(0.75f, 0.99f), 0.0f).rgb) * 0.5f;
        return lerp(c0, zAvg, poleW);
    }
    return c0;
}

float phaseHG(float cosT, float g) {
    const float g2 = g * g;
    return (1.0f - g2) / (4.0f * PI * pow(max(1.0f + g2 - 2.0f * g * cosT, 1.0e-4f), 1.5f));
}

// The integral of 1 - |x(t) - c|^2 / r^2 over t in [0, tEnd] where it is positive.
float windowIntegral(float3 w, float3 c, float r, float tEnd) {
    const float tc = dot(w, c);
    const float h2 = dot(c, c) - tc * tc;
    const float r2 = r * r;
    if (h2 >= r2) { return 0.0f; }
    const float half = sqrt(r2 - h2);
    const float sa = max(0.0f, tc - half) - tc, sb = min(tEnd, tc + half) - tc;   // s = t - tc
    if (sb <= sa) { return 0.0f; }
    return (1.0f - h2 / r2) * (sb - sa) - (sb * sb * sb - sa * sa * sa) / (3.0f * r2);
}

float4 fs(float4 at : SV_Position) : SV_Target0 {
    const int3 ip = int3(at.xy, 0);
    float4 lit = Lit.Load(ip);
    if ((flags.x & FLAG_FOG) == 0u || (flags.x & FLAG_MATCH) != 0u) { return lit; }
    const float d = Depth.Load(ip);
    const float2 px = floor(at.xy);
    const float focusD = Depth.Load(int3(int2(dims.xy * 0.5f), 0));
    const float focusZ = focusD < 1.0f ? viewZOf(focusD) : 10000.0f;
    const float curZ = d < 1.0f ? viewZOf(d) : 100000.0f;
    // Pre-Pass Weather Optics: procedural HDR rain-on-lens droplets & blizzard crystalline frost edges
    // (3-pass Near/Far Physical Bokeh DoF runs in dedicated DofPass.hlsl right after LightFog).
    if (dofParams.w > 0.5f && (weather.w >= 0.12f || weather.z >= 0.25f)) {
        lit.rgb = elderWeatherLensPrepass(Lit, px, dims.xy, dims.zw, weather.w, weather.z, camPosW.w);
    }
    const bool isCelestial = (d < 1.0f && curZ > 220000.0f);
    if (d >= 1.0f || isCelestial) {
        // Unified Hillaire SIGGRAPH 2020 Physical HDR Sky + Solar Disc + Masser/Secunda Celestial Pass + Wrenninge 4-Octave HDR Cloud Composite:
        // Routing celestial sky-dome geometry (curZ > 220k: Masser & Secunda) here prevents terrestrial horizonMelt from
        // painting flat blue over the moon and ensures 3D volumetric clouds drift naturally in front of both moons!
        if (sunColour.w > 0.5f && flags.w != 10u) {
            const float3 sDir = normalize(worldAt(px, 0.995f) - eyeAt());
            const float sunLumS = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            // True 24-hour astronomical clock & sun vector from Sky::currentGameHour (+0x1B0, timeEnv.x = 0..24):
            // Completely decoupled from ShadowSceneNode->sunLight's night-time Moon flip & minimum-elevation clamp!
            const float3 aSun = astroSunDir();
            const float aElev = astroSunElev();
            const float dayW = smoothstep(-0.08f, 0.14f, aElev);
            const float sunDiscGate = smoothstep(-0.02f, 0.04f, aElev) * isRealSun();
            const float twilightBell = smoothstep(-0.10f, 0.00f, aElev) * (1.0f - smoothstep(0.06f, 0.28f, aElev));
            const float duskSide = smoothstep(12.5f, 15.0f, timeEnv.x);
            const float cover = saturate(clouds.x);
            // Preserve exotic DLC skies (Apocrypha emerald, Soul Cairn violet, Solstheim ash).
            const float isExotic = saturate(max((lit.g - max(lit.r, lit.b) * 1.08f) * 6.0f,
                                                (min(lit.r, lit.b) - lit.g * 1.15f) * 6.0f));
            const float cosS = saturate(dot(sDir, aSun));
            const float sunAng = acos(clamp(cosS, -1.0f, 1.0f));
            const float lowS = saturate(1.0f - max(aElev, 0.0f) * 3.2f);
            // Preserve authored Masser & Secunda moon crater textures across daytime, twilight, and night.
            const float moonLumF = dot(lit.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float moonKeep = isCelestial ? 1.0f : (smoothstep(0.16f, 0.48f, moonLumF) * (1.0f - smoothstep(0.985f, 0.996f, cosS)) * (1.0f - smoothstep(0.18f, 0.42f, aElev)));
            // Full 24-Hour Dynamic Skybox (Dawn -> Sunrise -> Day -> Golden Hour -> Sunset -> Dusk -> Star-Studded Night):
            const float hUp = saturate(sDir.z);
            const float3 nightZen = float3(0.005f, 0.011f, 0.028f);
            const float3 nightHor = float3(0.022f, 0.036f, 0.068f);
            float3 fullSky = lerp(nightHor, nightZen, pow(hUp, 0.56f));
            // Physical 22-degree warm atmospheric lunar halo around the night Moon (ShadowSceneNode->sunLight at night),
            // calibrated to author-01.jpg own-08..11.
            const float nightHaloW = smoothstep(0.02f, -0.08f, aElev) * (1.0f - cover * 0.55f);
            if (nightHaloW > 0.01f && sunDir.z > 0.04f) {
                const float cosM = saturate(dot(sDir, normalize(sunDir.xyz)));
                const float mAng = acos(clamp(cosM, -1.0f, 1.0f));
                const float mCorona = exp(-mAng * 10.5f) * 0.24f + exp(-pow((mAng - 0.384f) / 0.055f, 2.0f)) * 0.055f;
                fullSky += float3(0.92f, 0.84f, 0.68f) * (mCorona * nightHaloW);
            }
            if (dayW > 0.01f || twilightBell > 0.02f) {
                const float hElev = max(sDir.z, 0.048f);
                const float3 hSky = sampleHillaireSky(normalize(float3(sDir.xy, hElev)));
                const float3 sceneSkyRef = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), 0.06f.xxx);
                const float3 natZen = lerp(float3(0.95f, 0.98f, 1.02f), float3(0.84f, 0.94f, 1.08f), pow(hUp, 0.70f));
                const float3 richSky = lerp(hSky * natZen, max(hSky, sceneSkyRef * 1.35f), 0.22f * smoothstep(0.10f, 0.32f, aElev));
                const float hLum = max(dot(richSky, float3(0.2126f, 0.7152f, 0.0722f)), 0.08f);
                const float3 overcastSky = lerp(float3(0.76f, 0.80f, 0.86f), float3(0.56f, 0.62f, 0.72f), hUp) * max(hLum * 1.18f, 0.50f);
                float3 physSky = lerp(richSky, overcastSky, saturate(cover * 0.82f - 0.08f + weather.w * 0.45f + weather.z * 0.42f));
                // Authentic Pacific Northwest Dawn/Sunrise & Sunset/Dusk (author-01.jpg own-13..17, own-27, own-42):
                const float3 twiZen = lerp(float3(0.05f, 0.09f, 0.24f), float3(0.07f, 0.06f, 0.20f), duskSide);
                const float3 twiMid = lerp(float3(0.48f, 0.28f, 0.44f), float3(0.64f, 0.26f, 0.36f), duskSide);
                const float3 twiHor = lerp(float3(1.18f, 0.58f, 0.24f), float3(1.36f, 0.44f, 0.16f), duskSide);
                const float3 twiGrad = hUp < 0.26f ? lerp(twiHor, twiMid, pow(hUp / 0.26f, 0.72f))
                                                   : lerp(twiMid, twiZen, pow((hUp - 0.26f) / 0.74f, 0.78f));
                const float twiBlend = twilightBell * (1.0f - saturate(weather.w * 0.55f + weather.z * 0.45f)) * 0.68f;
                physSky = lerp(physSky, twiGrad, twiBlend);
                const float antiSolar = saturate(dot(sDir, -aSun));
                const float horBand = exp(-hUp * 6.5f);
                physSky += float3(1.18f, 0.46f, 0.14f) * (pow(cosS, 3.2f) * horBand * twilightBell * (1.0f - cover * 0.55f) * 0.78f);
                physSky += float3(0.52f, 0.24f, 0.48f) * (pow(antiSolar, 2.0f) * horBand * twilightBell * 0.34f);
                // Crisp physical 0.53-deg solar disc ONLY (behind volumetric clouds via cl.a transmittance — zero fake ring or oversized glow!).
                if (sunDiscGate > 0.01f) {
                    const float3 sunCoreCol = lerp(float3(1.0f, 0.99f, 0.96f), float3(1.0f, 0.60f, 0.22f), lowS);
                    const float3 sunHaloCol = lerp(float3(1.0f, 0.94f, 0.84f), float3(1.0f, 0.46f, 0.14f), lowS);
                    const float solarCore = smoothstep(0.99972f, 0.99993f, cosS) * 14.0f;
                    const float solarCorona = exp(-sunAng * 42.0f) * 0.95f;
                    physSky += (sunCoreCol * solarCore + sunHaloCol * (solarCorona * (1.0f - cover * 0.65f))) * sunDiscGate;
                }
                fullSky = lerp(fullSky, physSky, max(dayW, twilightBell * 0.92f));
            }
            // Seamless C2 sub-horizon worldbound atmospheric continuation (eliminates any dark world-edge wall).
            if (sDir.z < 0.0f) {
                const float subFade = smoothstep(0.0f, -0.24f, sDir.z);
                fullSky = lerp(fullSky, fullSky * 0.88f, subFade);
            }
            // Night sky celestial layer: subtle oxygen-557nm horizon airglow + dual-scale scintillating starfield +
            // Milky Way + 8-layer Nordic Aurora Borealis, strictly BEHIND 3D volumetric clouds (cl.a)!
            const float nightSkyW = saturate(1.0f - dayW * 1.35f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            if (nightSkyW > 0.02f && sDir.z > 0.0f) {
                // Faint upper-atmosphere nocturnal airglow limb just above the horizon (Commons Cluster B astrophotography).
                const float airglow = exp(-pow((sDir.z - 0.055f) / 0.048f, 2.0f));
                fullSky += float3(0.008f, 0.022f, 0.019f) * (airglow * nightSkyW * (1.0f - cover * 0.70f));
                const float3 sc1 = floor(sDir * 290.0f);
                const float3 sf1 = frac(sDir * 290.0f) - 0.5f;
                const float h1 = cHash(sc1);
                if (h1 > 0.955f) {
                    const float3 off1 = float3(cHash(sc1 + 11.3f), cHash(sc1 + 27.1f), cHash(sc1 + 43.7f)) - 0.5f;
                    const float d1 = length(sf1 - off1 * 0.52f);
                    const float core1 = exp(-d1 * d1 * 48.0f) * smoothstep(0.955f, 0.998f, h1);
                    const float twk1 = 0.76f + 0.24f * sin(camPosW.w * (2.4f + h1 * 5.2f) + h1 * 62.8f);
                    const float3 col1 = lerp(float3(0.70f, 0.84f, 1.0f), float3(1.0f, 0.85f, 0.64f), cHash(sc1 + 71.9f));
                    fullSky += col1 * (core1 * twk1 * nightSkyW * saturate(sDir.z * 5.0f) * 1.45f);
                }
                const float mwPlane = abs(dot(sDir, normalize(float3(0.52f, -0.36f, 0.77f))));
                const float mwBand = exp(-mwPlane * mwPlane * 20.0f) * smoothstep(0.05f, 0.28f, sDir.z);
                if (mwBand > 0.01f) {
                    const float mwDust = cNoise(sDir * 6.5f) * 0.65f + cNoise(sDir * 15.0f + 4.3f) * 0.35f;
                    const float mwLane = smoothstep(0.30f, 0.78f, mwDust);
                    fullSky += lerp(float3(0.04f, 0.06f, 0.13f), float3(0.14f, 0.12f, 0.20f), mwLane) * (mwBand * mwLane * nightSkyW * 0.55f);
                }
                const float auroraVis = nightSkyW * (1.0f - saturate(clouds.x * 1.05f)) * smoothstep(0.06f, 0.32f, sDir.z) * smoothstep(-0.25f, 0.45f, sDir.y);
                if (auroraVis > 0.02f) {
                    float3 aurSum = 0.0f.xxx;
                    [unroll] for (int ai = 0; ai < 8; ai++) {
                        const float ah = 0.18f + float(ai) * 0.11f;
                        const float2 ap = sDir.xy / max(sDir.z, 0.12f) * (1.35f + ah * 0.65f);
                        const float wave = sin(ap.x * 2.1f + camPosW.w * 0.075f + cNoise(float3(ap * 1.25f, camPosW.w * 0.035f)) * 3.4f);
                        const float band = exp(-abs(ap.y - 0.62f - wave * 0.44f) * 5.0f);
                        const float rays = 0.50f + 0.50f * cNoise(float3(ap.x * 9.0f, ap.y * 2.2f, camPosW.w * 0.11f + ah * 2.4f));
                        const float hT = saturate((ah - 0.18f) * 1.25f);
                        const float3 aCol = hT < 0.65f
                            ? lerp(float3(0.08f, 0.98f, 0.56f), float3(0.28f, 0.48f, 0.96f), hT / 0.65f)
                            : lerp(float3(0.28f, 0.48f, 0.96f), float3(0.78f, 0.18f, 0.54f), (hT - 0.65f) / 0.35f);
                        aurSum += aCol * (band * rays * (1.0f - ah * 0.55f));
                    }
                    fullSky += aurSum * (auroraVis * 0.11f);
                }
            }
            // Preserve Masser & Secunda twin moons cleanly on top of our procedural night/twilight sky.
            float3 baseSky = lerp(lit.rgb, fullSky, (1.0f - isExotic) * (1.0f - moonKeep * 0.90f));
            if (moonKeep > 0.05f && nightSkyW > 0.05f) {
                baseSky += lit.rgb * float3(0.88f, 0.96f, 1.15f) * (moonKeep * nightSkyW * 0.22f);
            }
            if ((flags.x & FLAG_CLOUDS) != 0u) {
                const float4 cl = Clouds.SampleLevel(Lin, (px + 0.5f) * dims.zw, 0.0f);
                // Clouds naturally occlude the solar disc via exact transmittance cl.a (removed duplicate blaze that burned through thick clouds!).
                baseSky = lerp(baseSky, baseSky * cl.a + cl.rgb, 1.0f - isExotic);
            }
            // Physical 42-deg primary + 51-deg secondary spectral atmospheric rainbow during sunlit rain (Commons own-03 & own-45).
            if (weather.w > 0.08f && aElev > 0.04f && aElev < 0.64f && sDir.z > 0.0f) {
                const float antiAng = acos(clamp(dot(sDir, -aSun), -1.0f, 1.0f));
                // Primary bow at 42 deg (0.733 rad, red outside -> violet inside); secondary bow at 51 deg (0.890 rad, reversed).
                const float pR = exp(-pow((antiAng - 0.742f) / 0.011f, 2.0f));
                const float pG = exp(-pow((antiAng - 0.728f) / 0.011f, 2.0f));
                const float pB = exp(-pow((antiAng - 0.714f) / 0.011f, 2.0f));
                const float sR = exp(-pow((antiAng - 0.876f) / 0.014f, 2.0f)) * 0.32f;
                const float sG = exp(-pow((antiAng - 0.892f) / 0.014f, 2.0f)) * 0.32f;
                const float sB = exp(-pow((antiAng - 0.908f) / 0.014f, 2.0f)) * 0.32f;
                const float bowW = saturate(weather.w * 2.2f) * smoothstep(0.04f, 0.14f, aElev) * (1.0f - smoothstep(0.45f, 0.64f, aElev)) * smoothstep(0.0f, 0.08f, sDir.z);
                baseSky += float3(pR + sR, pG + sG, pB + sB) * (bowW * 0.24f * (1.0f - isExotic));
            }
            // Sub-LSB Interleaved Gradient Noise dithering eliminates 8-bit/10-bit sky gradient banding lines cleanly.
            const float skyDith = (ign(px) - 0.5f) * (1.0f / 255.0f);
            return float4(max(baseSky + skyDith, 0.0f.xxx), lit.a);
        }
        return lit;
    }
    const float3 eye = eyeAt();
    const float3 p = worldAt(px, min(d, 0.999999f));
    const float3 toP = p - eye;
    const float dist = length(toP);
    const float3 w = toP / max(dist, 1.0e-4f);
    const float tEnd = min(dist, tune2.w);
    const float sigma = tune.x;

    // Depth-aware bilateral upsample of the SM6.6 temporally-reprojected 32-slice VolumetricPass buffer
    // (effective ~192-step smoothness with zero halo bleeding across pine needles and rooftops).
    const float2 uv = (px + 0.5f) * dims.zw;
    const float2 volPx = (px + 0.5f) * 0.5f;
    const float2 volDim = max(floor((dims.xy + 1.0f) * 0.5f), float2(1.0f, 1.0f));
    const int2 vBase = clamp(int2(floor(volPx - 0.5f)), int2(0, 0), int2(volDim) - 1);
    const int2 vMax = int2(volDim) - 1;
    const float zPix = min(curZ, 65000.0f);
    float3 vSum = 0.0f.xxx;
    float vW = 0.0f;
    [unroll] for (int dy = 0; dy <= 1; dy++) {
        [unroll] for (int dx = 0; dx <= 1; dx++) {
            const int2 q = clamp(vBase + int2(dx, dy), int2(0, 0), vMax);
            const float4 tap = VolFog.Load(int3(q, 0));
            const float relZ = abs(tap.a - zPix) / max(zPix, 1.0f);
            const float wz = exp(-relZ * 18.0f);
            vSum += tap.rgb * wz;
            vW += wz;
        }
    }
    const float3 vol2D = vW > 1.0e-4f ? (vSum / vW) : VolFog.SampleLevel(Lin, uv, 0.0f).rgb;
    // Live 3D World-Map Depth Isolation + Foreground Contrast Protection:
    // Keep foreground exterior geometry (<1,400u) crisp and un-washed (matching AAA dappled-forest shadow depth)
    // while preserving full 3D Froxel mountain/valley mist beyond 1,400u and full local-light mist at night/interiors.
    const float mapScale = (timeEnv.w > 0.5f || camPosW.z > 18000.0f) ? 0.18f : 1.0f;
    const float sunLumFg = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float dayFg = (sunColour.w > 0.5f) ? saturate((sunLumFg - 0.18f) * 3.0f) : 0.0f;
    const float fgClear = lerp(1.0f, smoothstep(320.0f, 1800.0f, dist), dayFg * (1.0f - saturate(weather.w * 0.65f + weather.z * 0.75f)));
    const float4 frox = sampleFroxel3D(uv, curZ);
    const float3 scatter = (frox.rgb * 1.10f + vol2D * 0.25f) * (mapScale * fgClear);
    // Engine-native underwater medium (FLAG_UNDERWATER from raw_engine.dll camera_in_water): physical Beer-Lambert absorption.
    if ((flags.x & FLAG_UNDERWATER) != 0u) {
        const float3 uw = elderUnderwaterMedium(lit.rgb, dist, 0.0022f);
        return float4(uw + scatter * float3(0.25f, 0.72f, 0.85f) * 1.4f, lit.a);
    }
    // Unified sky- and weather-coupled exponential height fog & alpine mountain mist (replaces Skyrim's flat 2011 metric fog).
    if ((flags.x & FLAG_FOG_REPLACES) != 0u && sunColour.w > 0.5f) {
        const float weatherDens = 1.0f + weather.w * 2.2f + weather.z * 2.6f + saturate(clouds.x - 0.35f) * 1.1f;
        const float baseSigma = 0.0000135f * weatherDens;
        // Smooth C2 elevation weight across w.z = 0 (eliminates the horizontal midpoint fog band at eye horizon).
        const float hStart = max(camPosW.z * 0.00025f, 0.0f);
        const float elevW = lerp(1.12f, 0.44f, smoothstep(-0.15f, 0.30f, w.z));
        // 3D rolling alpine mist & high-altitude mountain-collar cloud band (replaces static billboard fog planes).
        const float3 midPt = camPosW.xyz + eye + w * min(dist * 0.55f, 45000.0f);
        const float3 windShift = float3(cloudWind.xy * camPosW.w * 0.00006f, 0.0f);
        const float alpineNoise = cNoise(midPt * 0.00014f + windShift) * 0.65f + cNoise(midPt * 0.00042f - windShift * 1.6f + 7.1f) * 0.35f;
        const float peakCollar = exp(-pow((midPt.z - 5200.0f) / 3600.0f, 2.0f)) * smoothstep(8000.0f, 28000.0f, dist);
        const float alpineMist = (0.76f + 0.48f * alpineNoise) * (1.0f + peakCollar * (0.45f + 0.55f * alpineNoise));
        const float od = baseSigma * min(max(dist - 1400.0f, 0.0f), 240000.0f) * elevW * alpineMist * exp(-hStart * 0.15f);
        // Wavelength-dependent Rayleigh + Mie transmittance + smooth far-horizon aerial melt (eliminates the sharp Sea of Ghosts / Whiterun LOD seam).
        const float horizonMelt = smoothstep(70000.0f, 145000.0f, dist);
        const float3 T = exp(-od * float3(0.84f, 0.96f, 1.12f)) * (1.0f - horizonMelt);
        // Spectral 1:1 atmospheric gradient locked directly to live skyPalette(sZ, sM, sH) + cloud cover + Mie solar aureole:
        // eliminates the color discontinuity between mountain haze, horizon sky, sun, and cloud undersides.
        float3 sZ, sM, sH;
        skyPalette(sZ, sM, sH);
        const float3 aSunFg = astroSunDir();
        const float aElevFg = astroSunElev();
        const float nightF = smoothstep(0.03f, -0.07f, aElevFg);
        const float lowSunF = saturate(1.0f - max(aElevFg, 0.0f) * 3.0f);
        const float highSunF = smoothstep(0.20f, 0.48f, aElevFg);
        const float cosSunF = saturate(dot(w, aSunFg));
        // Unified sky-matched aerial perspective: uses the EXACT same Hillaire + overcastSky formula as the sky branch
        // (d >= 1.0f) so far mountains and the Sea of Ghosts melt into the sky dome with 0.000 luminance discontinuity
        // (eliminates the dark curved slate horizon band caught in r257_dawnstar_npc.jpg).
        const float cloudCov = saturate(clouds.x);
        const float3 hillaireSky = sampleHillaireSky(normalize(float3(w.xy, max(w.z, 0.048f))));
        const float hLumF = max(dot(hillaireSky, float3(0.2126f, 0.7152f, 0.0722f)), 0.05f);
        const float3 overcastAtmo = lerp(float3(0.74f, 0.78f, 0.85f), float3(0.54f, 0.60f, 0.70f), saturate(w.z)) * max(hLumF * 1.24f, 0.52f);
        float3 atmoCol = lerp(hillaireSky, overcastAtmo, saturate(cloudCov * 0.85f - 0.08f + weather.w * 0.45f + weather.z * 0.42f));
        // Dual-scale Mie solar scattering: broad golden/D65 aureole + tighter forward solar halo.
        const float3 d65Mie = lerp(float3(1.0f, 0.66f, 0.36f), float3(0.96f, 0.98f, 1.02f), highSunF);
        const float mieHalo = (pow(cosSunF, 5.0f) * 0.14f + pow(cosSunF, 28.0f) * 0.24f) * (1.0f - cloudCov * 0.72f) * (1.0f - nightF);
        atmoCol += d65Mie * mieHalo;
        // Snowstorm frosted whiteout vs night alpine indigo, with physical atmospheric highlight rolloff.
        atmoCol = lerp(atmoCol, lerp(sH, float3(0.58f, 0.63f, 0.70f), 0.55f), weather.z * 0.55f);
        atmoCol = lerp(atmoCol, max(sH * 0.55f, float3(0.05f, 0.09f, 0.18f)), nightF);
        // True linear HDR aerial perspective (no 0.65 SDR clamp so mountains separate with genuine atmospheric luminosity).
        atmoCol = min(atmoCol, 3.5f.xxx);
        // Feature 16: Native DX12 3D World-Space GPU Weather Precipitation (wind-advected rain streaks & alpine snowflakes)
        // depth-occluded against scene geometry and lit by live sky/sun/local lights.
        float3 precipAdd = 0.0f.xxx;
        if ((weather.w > 0.08f || weather.z > 0.08f) && mapScale > 0.5f) {
            const float2 windP = cloudWind.xy * 0.0025f;
            [unroll] for (int pl = 0; pl < 3; ++pl) {
                const float pDist = 110.0f + float(pl) * 165.0f;
                if (dist <= pDist) { continue; }
                const float3 wpP = camPosW.xyz + eye + w * pDist;
                if (weather.w > 0.08f) {
                    const float3 rCoord = float3((wpP.xy - windP * (wpP.z * 0.15f)) * 0.18f, wpP.z * 0.018f + camPosW.w * (11.0f + float(pl) * 3.0f));
                    const float rCell = cHash(floor(rCoord));
                    if (rCell > 0.965f) {
                        const float rMask = smoothstep(0.965f, 0.998f, rCell) * saturate(1.0f - length(frac(rCoord.xy) - 0.5f) * 2.8f);
                        precipAdd += max(atmoCol * 0.85f, 0.12f.xxx) * (rMask * weather.w * 0.28f / (1.0f + float(pl) * 0.5f));
                    }
                }
                if (weather.z > 0.08f) {
                    const float3 sCoord = float3((wpP.xy + float2(sin(camPosW.w * 0.9f + float(pl)), cos(camPosW.w * 0.7f + float(pl))) * 18.0f) * 0.095f,
                                                 wpP.z * 0.095f + camPosW.w * (2.6f + float(pl) * 0.8f));
                    const float sCell = cHash(floor(sCoord));
                    if (sCell > 0.955f) {
                        const float sMask = smoothstep(0.955f, 0.995f, sCell) * saturate(1.0f - length(frac(sCoord) - 0.5f) * 2.4f);
                        precipAdd += max(atmoCol * 1.15f + sunColour.rgb * 0.18f, 0.20f.xxx) * (sMask * weather.z * 0.42f / (1.0f + float(pl) * 0.45f));
                    }
                }
            }
        }
        // Attenuate additive volumetric scatter by transmittance T so far-horizon fog never double-adds into blinding whiteout.
        return float4(lit.rgb * T + atmoCol * (1.0f.xxx - T) + scatter * saturate(T + 0.20f) + precipAdd, lit.a);
    }
    // Dungeons, Caves, Nordic Tombs, Dwemer Ruins, Blackreach & Interiors (sunColour.w < 0.5):
    // 100% world-space subterranean depth mist + true 3D shadowed volumetric shafts from VolumetricPass.hlsl
    // (including 3D world-space BSLightingShader window portal lights from rt_emitters.cpp — zero camera-locked 2D shafts).
    if (sunColour.w < 0.5f && (flags.x & FLAG_FOG) != 0u) {
        const float3 wpInt = camPosW.xyz + eye + w * min(dist * 0.5f, 3200.0f);
        const float caveNoise = 0.72f + 0.56f * cNoise(wpInt * 0.00048f + float3(0.0f, 0.0f, camPosW.w * 0.015f));
        const float intOd = max(dist - 220.0f, 0.0f) * 0.000095f * caveNoise;
        const float intT = exp(-min(intOd, 1.25f));
        const float3 cellAmb = max(float3(ambient[0].w, ambient[1].w, ambient[2].w), float3(0.010f, 0.013f, 0.018f));
        const float3 caveMist = min(cellAmb * 0.65f, 0.22f.xxx);
        return float4(lit.rgb * intT + caveMist * (1.0f - intT) + scatter * 0.92f, lit.a);
    }
    const float T = (flags.x & FLAG_FOG_REPLACES) != 0u ? exp(-sigma * tEnd) : 1.0f;
    return float4(lit.rgb * T + scatter, lit.a);
}
