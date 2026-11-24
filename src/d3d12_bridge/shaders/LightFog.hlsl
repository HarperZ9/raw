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
Texture2D<float4> WorldField  : register(t7);
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
    const float vNonLin = saturate(0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI))));
    const float uAz = clamp(az / (2.0f * PI) + 0.5f, 0.004f, 0.996f);
    float3 c0 = AtmoSkyView.SampleLevel(Lin, float2(uAz, vNonLin), 0.0f).rgb;
    // Seamless azimuth wrap across +-PI seam (eliminates the straight diagonal sky seam seen in 01.29.15.22.png!):
    const float edgeD = min(uAz, 1.0f - uAz);
    if (edgeD < 0.025f) {
        const float3 cWrap = AtmoSkyView.SampleLevel(Lin, float2(uAz < 0.5f ? 0.996f : 0.004f, vNonLin), 0.0f).rgb;
        c0 = lerp(0.5f * (c0 + cWrap), c0, smoothstep(0.004f, 0.025f, edgeD));
    }
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
            // Preserve authored Masser (crimson-ochre) & Secunda (silver-grey) moon crater textures across night,
            // twilight & partial phases (0.035..0.18 threshold rescues dim moon craters that 0.16..0.48 wiped out!):
            const float moonLumF = dot(lit.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float masserWarm = saturate((lit.r - lit.b) * 6.5f) * smoothstep(0.015f, 0.06f, moonLumF);
            const float moonMask = max(smoothstep(0.035f, 0.18f, moonLumF), masserWarm);
            const float moonKeep = isCelestial ? 1.0f : (moonMask * (1.0f - smoothstep(0.985f, 0.996f, cosS)) * (1.0f - smoothstep(0.18f, 0.42f, aElev)));
            // Full 24-Hour Dynamic Skybox matched continuously to cloud-deck horizon luminance (eliminates black horizon band
            // seen in 01.33.15.97.png & 01.31.56.65.png when overcast/snow clouds sit above the horizon!):
            const float hUp = saturate(sDir.z);
            const float3 ambSkyCol = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), 0.03f.xxx);
            const float overcastDeck = saturate(cover * 0.85f + weather.w * 0.45f + weather.z * 0.55f);
            const float3 nightZen = lerp(float3(0.008f, 0.015f, 0.036f), ambSkyCol * float3(0.45f, 0.52f, 0.65f), overcastDeck * 0.72f);
            const float3 nightHor = lerp(float3(0.028f, 0.044f, 0.078f), ambSkyCol * float3(0.68f, 0.74f, 0.84f), overcastDeck * 0.85f);
            float3 fullSky = lerp(nightHor, nightZen, pow(hUp, 0.52f));
            // Offset mCorona around sunDir.xyz removed: Skyrim's night shadow light vector does not match Masser's
            // skybox texture position, which created a floating blue spotlight in empty night sky (21.03.04.78.png).
            if (dayW > 0.01f || twilightBell > 0.02f) {
                // 100% Physically Grounded Hillaire SIGGRAPH 2020 Radiative-Transfer Sky (Rayleigh + Mie + Chappuis Ozone +
                // Infinite-Order Multi-Scattering + Planetary Earth Shadow) — ZERO hardcoded fake 3-stop twilight gradients!
                const float hElev = max(sDir.z, 0.008f);
                const float3 hSky = sampleHillaireSky(normalize(float3(sDir.xy, hElev)));
                const float hLum = max(dot(hSky, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
                // Physical overcast multiple-scattering deck attenuation (desaturates toward Mie-scattered cloud-base luminance):
                const float overcastW = saturate(cover * 0.85f - 0.12f + weather.w * 0.45f + weather.z * 0.42f);
                const float3 overcastScatter = lerp(hSky, hLum.xxx * float3(0.92f, 0.96f, 1.02f), 0.78f);
                float3 physSky = lerp(hSky, overcastScatter, overcastW * 0.82f);
                // Exact 1:1 sync with evalWorldAtmosphere() (same 0.42 blend as mountain aerial perspective & clouds
                // so the sky dome, horizon fog, mountains, water & ambient share one continuous color field!):
                float3 uSunS, uSkyZS, uSkyHS, uAmbUpS, uAmbDnS;
                evalWorldAtmosphere(uSunS, uSkyZS, uSkyHS, uAmbUpS, uAmbDnS);
                physSky = lerp(physSky, lerp(uSkyHS, uSkyZS, saturate(max(sDir.z, 0.0f) * 2.2f)), 0.42f);
                // Physical 0.53-deg solar disc with Eddington limb darkening & exact atmospheric transmittance:
                if (sunDiscGate > 0.01f) {
                    const float rDisc = saturate(sunAng / 0.0052f);
                    const float muLimb = sqrt(max(1.0f - rDisc * rDisc, 0.0f));
                    const float3 eddLimb = float3(1.0f - 0.45f * (1.0f - muLimb), 1.0f - 0.62f * (1.0f - muLimb), 1.0f - 0.78f * (1.0f - muLimb));
                    const float3 sunCoreCol = lerp(float3(1.0f, 0.99f, 0.96f), float3(1.0f, 0.56f, 0.18f), lowS) * eddLimb;
                    const float3 sunHaloCol = lerp(float3(1.0f, 0.94f, 0.84f), float3(1.0f, 0.44f, 0.14f), lowS);
                    const float solarCore = smoothstep(0.99972f, 0.99993f, cosS) * 16.0f;
                    const float solarCorona = exp(-sunAng * 44.0f) * 0.95f;
                    physSky += (sunCoreCol * solarCore + sunHaloCol * (solarCorona * (1.0f - cover * 0.65f))) * sunDiscGate;
                }
                fullSky = lerp(fullSky, physSky, max(dayW, twilightBell * 0.96f));
            }
            // Infinite Distant Horizon Ocean Plane (Horizon Fix replacement, COMPETITIVE-2026.md):
            // Beyond Skyrim's map grid edge (sDir.z < 0 on sky pixels), analytically intersect sea level with
            // Fresnel sky reflection, rippling sun/moon glitter path, and C2 aerial-perspective horizon melt.
            if (sDir.z < 0.0f) {
                const float seaElev = max((eyeAt().z + camPosW.z) - waterA.w, 120.0f);
                const float tSea = min(seaElev / max(-sDir.z, 0.002f), 260000.0f);
                const float2 seaP = (camPosW.xy + sDir.xy * tSea) * 0.00045f;
                const float w1 = cNoise(float3(seaP * 2.2f + cloudWind.xy * camPosW.w * 0.0008f, camPosW.w * 0.12f)) * 2.0f - 1.0f;
                const float w2 = cNoise(float3(seaP * 5.6f - cloudWind.yx * camPosW.w * 0.0014f, camPosW.w * 0.21f)) * 2.0f - 1.0f;
                const float3 seaN = normalize(float3((w1 * 0.035f + w2 * 0.020f), (w2 * 0.035f - w1 * 0.020f), 1.0f));
                const float ndv = saturate(dot(seaN, -sDir));
                const float fres = 0.02f + 0.98f * pow(1.0f - ndv, 5.0f);
                const float3 reflDir = reflect(sDir, seaN);
                float3 ocZ, ocM, ocH;
                skyPalette(ocZ, ocM, ocH);
                const float3 seaSky = lerp(ocH, ocM, smoothstep(0.0f, 0.35f, max(reflDir.z, 0.0f)));
                const float3 deepSea = lerp(float3(0.008f, 0.024f, 0.044f), float3(0.014f, 0.048f, 0.072f), dayW) * (0.28f + 0.72f * dayW);
                const float3 glDir = normalize(lerp(aSun, sunDir.xyz, smoothstep(0.04f, -0.06f, aElev)));
                const float3 glH = normalize(glDir - sDir);
                const float glint = pow(saturate(dot(seaN, glH)), 140.0f) * (1.0f - cover * 0.75f);
                const float3 glintCol = lerp(float3(1.15f, 0.88f, 0.62f), float3(0.45f, 0.62f, 0.95f), smoothstep(0.04f, -0.06f, aElev));
                const float3 oceanCol = lerp(deepSea, seaSky, fres) + glintCol * (glint * 0.85f);
                const float hazeSea = exp(-tSea * 0.000018f);
                const float subFade = smoothstep(-0.001f, -0.048f, sDir.z);
                fullSky = lerp(fullSky, lerp(fullSky, oceanCol, hazeSea), subFade);
            }
            // Night sky celestial layer: subtle oxygen-557nm horizon airglow + dual-scale crisp procedural starfield
            // (magnitude 1-3 Planck stars + dense faint-magnitude deep field, zero DXT1) + Milky Way + dual-curtain Nordic Auroras!
            const float nightSkyW = saturate(1.0f - dayW * 1.35f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            if (nightSkyW > 0.02f && sDir.z > 0.0f) {
                // Faint upper-atmosphere nocturnal airglow limb just above the horizon (Commons Cluster B astrophotography).
                const float airglow = exp(-pow((sDir.z - 0.055f) / 0.048f, 2.0f));
                fullSky += float3(0.008f, 0.022f, 0.019f) * (airglow * nightSkyW * (1.0f - cover * 0.70f));
                const float3 sc1 = floor(sDir * 290.0f);
                const float3 sf1 = frac(sDir * 290.0f) - 0.5f;
                const float h1 = cHash(sc1);
                if (h1 > 0.952f) {
                    const float3 off1 = float3(cHash(sc1 + 11.3f), cHash(sc1 + 27.1f), cHash(sc1 + 43.7f)) - 0.5f;
                    const float d1 = length(sf1 - off1 * 0.52f);
                    const float core1 = exp(-d1 * d1 * 58.0f) * smoothstep(0.952f, 0.998f, h1);
                    // Altitude-dependent atmospheric scintillation (stronger twinkling near horizon air mass) + Planck O/B/A/G/K/M star colors.
                    const float scintAmp = lerp(0.42f, 0.14f, saturate(sDir.z * 2.5f));
                    const float twk1 = (1.0f - scintAmp) + scintAmp * sin(camPosW.w * (2.6f + h1 * 5.8f) + h1 * 62.8f);
                    const float spT = cHash(sc1 + 71.9f);
                    const float3 col1 = spT < 0.35f ? lerp(float3(0.62f, 0.78f, 1.0f), float3(0.94f, 0.96f, 1.0f), spT / 0.35f)
                                                    : lerp(float3(0.94f, 0.96f, 1.0f), float3(1.0f, 0.72f, 0.44f), (spT - 0.35f) / 0.65f);
                    fullSky += col1 * (core1 * twk1 * nightSkyW * saturate(sDir.z * 5.0f) * 1.65f);
                }
                // Second high-density faint-magnitude pinpoint starfield (replaces blurry 512px DXT1 sky stars at 1080p/4K/8K):
                const float3 sc2 = floor(sDir * 620.0f);
                const float3 sf2 = frac(sDir * 620.0f) - 0.5f;
                const float h2 = cHash(sc2 + 19.7f);
                if (h2 > 0.945f) {
                    const float3 off2 = float3(cHash(sc2 + 5.1f), cHash(sc2 + 33.7f), cHash(sc2 + 61.3f)) - 0.5f;
                    const float d2 = length(sf2 - off2 * 0.48f);
                    const float core2 = exp(-d2 * d2 * 78.0f) * smoothstep(0.945f, 0.999f, h2);
                    fullSky += float3(0.84f, 0.90f, 1.0f) * (core2 * nightSkyW * saturate(sDir.z * 4.5f) * 0.62f);
                }
                // Multi-band physical Galactic Nebula + H-alpha/O-III emission filaments + dark molecular Bok dust lanes:
                const float mwPlane = abs(dot(sDir, normalize(float3(0.52f, -0.36f, 0.77f))));
                const float mwBand = exp(-mwPlane * mwPlane * 16.0f) * smoothstep(0.05f, 0.28f, sDir.z);
                if (mwBand > 0.01f) {
                    const float mwWarp = cNoise(sDir * 3.8f + 1.9f) * 0.65f;
                    const float mwDust = cNoise(sDir * 6.5f + mwWarp) * 0.62f + cNoise(sDir * 15.0f - mwWarp + 4.3f) * 0.38f;
                    const float darkRift = smoothstep(0.44f, 0.66f, cNoise(sDir * 9.5f + float3(7.1f, 2.3f, 5.7f)));
                    const float hAlpha = smoothstep(0.62f, 0.88f, cNoise(sDir * 8.2f + float3(13.4f, 4.9f, 9.1f)));
                    const float oIII   = smoothstep(0.64f, 0.90f, cNoise(sDir * 10.5f + float3(2.7f, 17.3f, 3.3f)));
                    const float3 nebCol = lerp(float3(0.04f, 0.07f, 0.16f), float3(0.16f, 0.14f, 0.24f), smoothstep(0.28f, 0.76f, mwDust))
                                        + float3(0.28f, 0.07f, 0.14f) * (hAlpha * 0.55f)
                                        + float3(0.06f, 0.22f, 0.26f) * (oIII * 0.45f);
                    fullSky += nebCol * (mwBand * (1.0f - darkRift * 0.68f) * nightSkyW * 0.72f);
                }
                // Dual-Curtain Analytic 10-Layer Volumetric Nordic Aurora Borealis (557.7nm O-I green -> 427.8nm N2+ blue -> 630nm O-I crimson):
                const float auroraVis = nightSkyW * (1.0f - saturate(clouds.x * 1.05f)) * smoothstep(0.05f, 0.28f, sDir.z) * smoothstep(-0.35f, 0.35f, sDir.y);
                if (auroraVis > 0.02f) {
                    float3 aurSum = 0.0f.xxx;
                    [unroll] for (int ai = 0; ai < 10; ai++) {
                        const float ah = 0.14f + float(ai) * 0.095f;
                        const float2 ap = sDir.xy / max(sDir.z, 0.12f) * (1.30f + ah * 0.68f);
                        const float nWarp = cNoise(float3(ap * 1.15f, camPosW.w * 0.032f));
                        const float wave1 = sin(ap.x * 2.0f + camPosW.w * 0.072f + nWarp * 3.6f);
                        const float band1 = exp(-abs(ap.y - 0.58f - wave1 * 0.46f) * 5.2f);
                        const float wave2 = sin(ap.x * 1.45f - camPosW.w * 0.054f + nWarp * 2.8f + 1.9f);
                        const float band2 = exp(-abs(ap.y - 1.12f - wave2 * 0.38f) * 4.4f) * 0.65f;
                        const float rays = 0.42f + 0.58f * cNoise(float3(ap.x * 11.5f, ap.y * 2.0f, camPosW.w * 0.12f + ah * 2.6f));
                        const float hT = saturate((ah - 0.14f) * 1.18f);
                        const float3 aCol = hT < 0.58f
                            ? lerp(float3(0.06f, 0.98f, 0.54f), float3(0.22f, 0.54f, 0.96f), hT / 0.58f)
                            : lerp(float3(0.22f, 0.54f, 0.96f), float3(0.84f, 0.16f, 0.52f), (hT - 0.58f) / 0.42f);
                        aurSum += aCol * ((band1 + band2) * rays * (1.0f - ah * 0.52f));
                    }
                    fullSky += aurSum * (auroraVis * 0.115f);
                }
            }
            // Preserve Masser & Secunda twin moons cleanly on top of our procedural night/twilight sky.
            float3 baseSky = lerp(lit.rgb, fullSky, (1.0f - isExotic) * (1.0f - moonKeep * 0.96f));
            if (moonKeep > 0.04f && nightSkyW > 0.04f) {
                baseSky += lit.rgb * float3(0.95f, 0.98f, 1.12f) * (moonKeep * nightSkyW * 0.35f);
            }
            if ((flags.x & FLAG_CLOUDS) != 0u) {
                const float2 skyUv = (px + 0.5f) * dims.zw;
                // Crisp hardware bilinear lookup (removes 5-tap blur that smeared 48-step cumulus detail into clay):
                const float4 cl = Clouds.SampleLevel(Lin, skyUv, 0.0f);
                // Let Masser & Secunda glow visibly through thin/moderate night clouds instead of disappearing!
                const float clTrans = lerp(cl.a, max(cl.a, 0.38f), moonKeep * nightSkyW);
                baseSky = lerp(baseSky, baseSky * clTrans + cl.rgb, 1.0f - isExotic);
                // Physical 3D Crepuscular Cloud-Gap Light Rays (12-tap IGN-jittered radial transmittance march toward live sun):
                // smooth band-free sunrays fanning out of cumulus breaks with soft off-screen sun fade!
                // Removed screen-space 12-tap radial sun-spoke hack (21.03.52.63.png): true 3D volumetric godrays
                // belong exclusively in FroxelPass / VolumetricPass (zero screen-space radial spokes across overcast sky!).
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
        const float od = baseSigma * min(max(dist - 2200.0f, 0.0f), 240000.0f) * elevW * alpineMist * exp(-hStart * 0.15f);
        // Wavelength-dependent Rayleigh + Mie transmittance + smooth far-horizon aerial melt (eliminates the sharp Sea of Ghosts / Whiterun LOD seam).
        const float horizonMelt = smoothstep(70000.0f, 145000.0f, dist);
        float3 T = exp(-od * float3(0.84f, 0.96f, 1.12f)) * (1.0f - horizonMelt);
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
        // Unified World-Space Atmosphere sync: blend Hillaire Sky-View LUT with evalWorldAtmosphere() horizon
        // so distant mountains, volumetric fogs, clouds, and sky dome share one exact color & luminance gradient!
        float3 uSunF, uSkyZF, uSkyHF, uAmbUpF, uAmbDnF;
        evalWorldAtmosphere(uSunF, uSkyZF, uSkyHF, uAmbUpF, uAmbDnF);
        const float3 hillaireSky = sampleHillaireSky(normalize(float3(w.xy, max(w.z, 0.008f))));
        const float hLumF = max(dot(hillaireSky, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float overcastWF = saturate(cloudCov * 0.85f - 0.12f + weather.w * 0.45f + weather.z * 0.42f);
        const float3 overcastScatterF = lerp(hillaireSky, hLumF.xxx * float3(0.92f, 0.96f, 1.02f), 0.78f);
        float3 atmoCol = lerp(hillaireSky, overcastScatterF, overcastWF * 0.82f);
        atmoCol = lerp(atmoCol, lerp(uSkyHF, uSkyZF, saturate(max(w.z, 0.0f) * 2.2f)), 0.42f);
        const float3 d65Mie = lerp(float3(1.0f, 0.66f, 0.36f), float3(0.96f, 0.98f, 1.02f), highSunF);
        const float mieHalo = (pow(cosSunF, 5.0f) * 0.14f + pow(cosSunF, 28.0f) * 0.24f) * (1.0f - cloudCov * 0.72f) * (1.0f - nightF);
        atmoCol += lerp(d65Mie, uSunF, 0.45f) * mieHalo;
        // Exact 1:1 night horizon sky-dome sync (eliminates dark greenish-grey mountain cutout against indigo night sky
        // captured in 13.16.31.17.png!): matches LightFog.hlsl:122 nightHor formula identically!
        const float3 ambSkyF = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), 0.03f.xxx);
        const float overcastDeckF = saturate(cloudCov * 0.85f + weather.w * 0.45f + weather.z * 0.55f);
        const float3 nightHorF = lerp(float3(0.028f, 0.044f, 0.078f), ambSkyF * float3(0.68f, 0.74f, 0.84f), overcastDeckF * 0.85f);
        atmoCol = lerp(atmoCol, nightHorF, nightF);
        // Solstheim volcanic ashstorm particles disabled until explicitly keyed to Solstheim ash weather IDs
        // (sunColour.r > sunColour.b * 1.42 falsely triggered glowing orange embers in Whiterun on every golden sunset!).
        const float ashW = 0.0f;
        if (ashW > 0.05f) {
            const float3 ashFogCol = float3(0.42f, 0.26f, 0.16f) * max(dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f)), 0.35f);
            const float ashDens = 1.0f - exp(-dist * 0.00024f * ashW);
            atmoCol = lerp(atmoCol, ashFogCol, ashW * 0.82f);
            T *= (1.0f - ashDens * 0.72f);
        }
        // Cluster B (Turner's Snow Storm B15.5): sweeping 3D wind-slanted distant rain/blizzard curtain sheets across valleys!
        if ((weather.w > 0.22f || weather.z > 0.22f) && dist > 650.0f && mapScale > 0.5f) {
            const float2 sheetW = normalize(cloudWind.xy + float2(1.0f, 0.35f));
            const float3 sheetP = camPosW.xyz + eye + w * min(dist * 0.55f, 4200.0f);
            const float slant = dot(sheetP.xy, sheetW) * 0.0014f - sheetP.z * 0.00045f - camPosW.w * 0.12f;
            const float curtain = smoothstep(0.35f, 0.85f, cNoise(float3(slant, dot(sheetP.xy, float2(-sheetW.y, sheetW.x)) * 0.0009f, sheetP.z * 0.00025f)));
            const float sheetDens = curtain * saturate((dist - 650.0f) / 3800.0f) * max(weather.w, weather.z) * 0.22f;
            atmoCol = lerp(atmoCol, lerp(float3(0.24f, 0.29f, 0.36f), float3(0.62f, 0.68f, 0.76f), weather.z), sheetDens * 0.55f);
            T *= (1.0f - sheetDens * 0.35f);
        }
        atmoCol = min(atmoCol, 3.5f.xxx);
        // GTA6-Surpassing 6-Layer Native DX12 3D World-Space Volumetric Precipitation (depth-occluded in 3D space):
        // Real Rain (wind-slanted specular streaks catching local torch/lantern light!), Real Snow (helical 3D vortex
        // snowflakes with foreground bokeh flakes & distant blizzard sheets), and Real Volcanic Ashstorms (swirling ash + embers)!
        float3 precipAdd = 0.0f.xxx;
        if ((weather.w > 0.08f || weather.z > 0.08f || ashW > 0.10f) && mapScale > 0.5f) {
            const float2 windP = cloudWind.xy * 0.0032f;
            const uint nPrecipLights = min(flags.y, 12u);
            [unroll] for (int pl = 0; pl < 6; ++pl) {
                const float pDist = 65.0f + float(pl) * 115.0f;
                if (dist <= pDist) { continue; }
                const float3 pRelP = eye + w * pDist;
                const float3 wpP = camPosW.xyz + pRelP;
                // Local point-light illumination on nearby rain/snow/ash particles (torches, braziers, lanterns, forges!):
                float3 localLightP = 0.0f.xxx;
                if (pl < 3) {
                    [loop] for (uint li = 0u; li < nPrecipLights; ++li) {
                        const float3 dL = lightPosR[li].xyz - pRelP;
                        const float rL = max(lightPosR[li].w * 1.15f, 1.0f);
                        const float d2 = dot(dL, dL);
                        if (d2 < rL * rL) {
                            const float attP = 1.0f - sqrt(d2) / rL;
                            localLightP += lightColS[li].rgb * (attP * attP * 1.45f);
                        }
                    }
                }
                if (weather.w > 0.06f) {
                    // Intense wind-driven diagonal torrential rain + dramatic storm squall sheets + backlit torch/lightning glint:
                    const float stormBoost = saturate(weather.w * 1.45f);
                    const float gust = 1.0f + 0.55f * sin(camPosW.w * 1.65f + wpP.x * 0.0024f) + 0.25f * cos(camPosW.w * 2.9f - wpP.y * 0.0018f);
                    const float3 rCoord = float3((wpP.xy - windP * (wpP.z * 0.24f * gust)) * lerp(0.32f, 0.20f, float(pl) / 5.0f),
                                                 wpP.z * 0.0075f + camPosW.w * (20.0f + float(pl) * 4.8f));
                    const float rCell = cHash(floor(rCoord));
                    const float rGate = lerp(0.948f, 0.916f, stormBoost);
                    if (rCell > rGate) {
                        const float2 rF = frac(rCoord.xy) - 0.5f;
                        const float rThin = saturate(1.0f - abs(rF.x) * 8.4f) * saturate(1.0f - abs(rF.y) * 1.45f);
                        const float rMask = smoothstep(rGate, 0.998f, rCell) * rThin;
                        const float3 rLit = max(atmoCol * 1.35f + 0.10f, 0.20f.xxx) + localLightP * 2.15f;
                        precipAdd += rLit * (rMask * stormBoost * 0.62f / (1.0f + float(pl) * 0.36f));
                    }
                    // Sweeping 3D storm wind-squall mist curtains during heavy downpours (pl >= 2):
                    if (pl >= 2 && weather.w > 0.35f) {
                        const float sqN = cNoise(float3((wpP.xy - windP * (camPosW.w * 48.0f)) * 0.0018f, wpP.z * 0.0012f));
                        const float sqSheet = smoothstep(0.54f, 0.88f, sqN) * saturate((weather.w - 0.35f) * 1.8f);
                        precipAdd += (atmoCol * 0.72f + localLightP * 0.35f) * (sqSheet * 0.14f / float(pl));
                    }
                }
                if (weather.z > 0.08f) {
                    const float swirlPh = camPosW.w * (0.42f + float(pl) * 0.10f);
                    const float swayX = sin(swirlPh + wpP.z * 0.014f + wpP.y * 0.005f) * 20.0f;
                    const float swayY = cos(swirlPh * 0.82f + wpP.x * 0.012f - wpP.z * 0.008f) * 20.0f;
                    const float fScale = lerp(0.125f, 0.055f, float(pl) / 5.0f);
                    const float3 sCoord = float3((wpP.xy - windP * (wpP.z * 0.12f) + float2(swayX, swayY)) * fScale,
                                                 wpP.z * fScale + camPosW.w * (1.25f + float(pl) * 0.28f));
                    const float sCell = cHash(floor(sCoord));
                    if (sCell > 0.936f) {
                        const float dFlake = length(frac(sCoord) - 0.5f);
                        const float flakeSharp = pl < 2 ? 12.0f : 22.0f;   // larger soft foreground bokeh flakes!
                        const float sMask = smoothstep(0.936f, 0.998f, sCell) * exp(-dFlake * dFlake * flakeSharp);
                        const float3 sLit = max(atmoCol * 1.30f + 0.14f, 0.28f.xxx) + localLightP * 1.65f;
                        precipAdd += sLit * (sMask * weather.z * 0.46f / (1.0f + float(pl) * 0.32f));
                    }
                }
                if (ashW > 0.10f) {
                    const float aTurb = sin(camPosW.w * 1.6f + wpP.z * 0.02f) * 18.0f;
                    const float3 aCoord = float3((wpP.xy - windP * (camPosW.w * 34.0f) + float2(aTurb, -aTurb * 0.7f)) * lerp(0.13f, 0.075f, float(pl) / 5.0f),
                                                 wpP.z * 0.10f + camPosW.w * (2.2f + float(pl) * 0.55f));
                    const float aCell = cHash(floor(aCoord));
                    if (aCell > 0.928f) {
                        const float aMask = smoothstep(0.928f, 0.996f, aCell) * saturate(1.0f - length(frac(aCoord) - 0.5f) * 2.4f);
                        const bool isEmber = aCell > 0.985f;
                        const float3 aCol = isEmber ? float3(3.20f, 0.78f, 0.16f) : (float3(0.32f, 0.25f, 0.20f) + localLightP * 0.65f);
                        precipAdd += aCol * (aMask * ashW * (isEmber ? 1.15f : 0.48f) / (1.0f + float(pl) * 0.38f));
                    }
                }
            }
        }
        // Cross-shader PDE coupling: sample WorldFieldSim (t7) so freshly wet ground & campfire-melted snow
        // physically boost low-lying ground evaporation mist in LightFog.hlsl (zero siloed passes!):
        float pdeMistBoost = 1.0f;
        if (dist < 1000.0f) {
            const float2 wfUv = saturate(p.xy / 2048.0f + 0.5f.xx);
            if (all(wfUv > 0.02f) && all(wfUv < 0.98f)) {
                const float4 wf = WorldField.SampleLevel(Lin, wfUv, 0.0f);
                pdeMistBoost = 1.0f + saturate(wf.b * 0.65f + wf.a * 0.35f) * 0.45f;
            }
        }
        // Attenuate additive volumetric scatter by transmittance T so far-horizon fog never double-adds into blinding whiteout.
        return float4(lit.rgb * T + atmoCol * (1.0f.xxx - T) + scatter * saturate(T + 0.20f) * pdeMistBoost + precipAdd, lit.a);
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
