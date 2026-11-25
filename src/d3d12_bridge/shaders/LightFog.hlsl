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
// Uses shared froxelZToSlice() from LightingCommon.hlsli so kFroxNear/kFroxFar never diverge from FroxelPass.hlsl.
float4 sampleFroxel3D(float2 uv, float z) {
    const float sF = froxelZToSlice(z);
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
    // Clean 3D World Map isolation: ONLY bypass when paused MapMenu camera sits above 42,000u (never bypass High Hrothgar / mountains!).
    const bool isWorldMap = (timeEnv.w > 0.5f) && (camPosW.z > 42000.0f);
    if (isWorldMap) { return lit; }
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
    // Wave 33 Distant Mountain Aerial Perspective Fix (00.10.37.77.png Throat of the World cutout):
    // Tamriel's farthest DistantLOD peaks sit at 220k..340k units; only true sky-dome celestial geometry
    // (Masser/Secunda at far clip d > 0.99982 and curZ > 380,000u) should bypass atmospheric aerial perspective!
    const bool isCelestial = (d < 1.0f && d > 0.99982f && curZ > 380000.0f);
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
                // Preserve crisp Nordic Rayleigh sapphire sky on clear/partly-cloudy days; desaturate only under genuine heavy overcast!
                const float overcastW = saturate((cover - 0.55f) * 1.95f + weather.w * 0.52f + weather.z * 0.45f);
                const float3 overcastScatter = lerp(hSky, hLum.xxx * float3(0.88f, 0.94f, 1.04f), 0.72f);
                float3 physSky = lerp(hSky, overcastScatter, overcastW * 0.78f);
                float3 uSunS, uSkyZS, uSkyHS, uAmbUpS, uAmbDnS;
                evalWorldAtmosphere(uSunS, uSkyZS, uSkyHS, uAmbUpS, uAmbDnS);
                physSky = lerp(physSky, lerp(uSkyHS, uSkyZS, saturate(max(sDir.z, 0.0f) * 2.2f)), 0.28f) * 0.92f;
                // Luminous physical solar disc + smooth multi-scale Mie atmospheric corona (eliminates flat white paper sticker sun!):
                if (sunDiscGate > 0.01f) {
                    const float rDisc = saturate(sunAng / 0.0068f);
                    const float muLimb = sqrt(max(1.0f - rDisc * rDisc, 0.0f));
                    const float3 eddLimb = float3(1.0f - 0.28f * (1.0f - muLimb), 1.0f - 0.42f * (1.0f - muLimb), 1.0f - 0.58f * (1.0f - muLimb));
                    const float3 sunCoreCol = lerp(float3(1.05f, 1.00f, 0.92f), float3(1.15f, 0.62f, 0.24f), lowS) * eddLimb;
                    const float3 mieCol = lerp(float3(1.00f, 0.95f, 0.86f), float3(1.05f, 0.56f, 0.22f), lowS);
                    const float solarCore = smoothstep(0.99976f, 0.99996f, cosS) * 26.0f;
                    const float innerAureole = pow(cosS, 240.0f) * 1.65f + pow(cosS, 42.0f) * 0.42f;
                    const float wideMie = pow(cosS, 6.0f) * 0.16f * (1.0f - cover * 0.45f);
                    physSky += (sunCoreCol * solarCore + mieCol * (innerAureole + wideMie)) * sunDiscGate;
                }
                // Wave 29 Target-Frame Curved-Planet Belt of Venus (Antitwilight Arch) & Rising Earth Shadow Band:
                // Intersects each sky ray opposite the low sun against the curved planetary shadow tangent (R=6360 km),
                // painting the authentic deep slate-indigo Earth shadow below the rose-coral Belt of Venus at dawn/dusk!
                if (twilightBell > 0.03f && sDir.z > 0.0f && sDir.z < 0.42f) {
                    const float antiSunW = smoothstep(-0.15f, 0.85f, dot(normalize(float3(sDir.xy, 0.0f)), normalize(float3(-aSun.xy, 0.0f))));
                    // Earth shadow elevation rises opposite the sun as aElev drops below +0.06:
                    const float shadowElev = clamp(0.045f - aElev * 1.15f, 0.015f, 0.22f);
                    const float inEarthShadow = (1.0f - smoothstep(shadowElev - 0.035f, shadowElev + 0.045f, sDir.z)) * antiSunW;
                    const float venusArch = exp(-pow((sDir.z - (shadowElev + 0.048f)) / 0.052f, 2.0f)) * antiSunW;
                    const float clearTw = (1.0f - overcastW * 0.82f) * twilightBell;
                    physSky = lerp(physSky, physSky * float3(0.48f, 0.56f, 0.82f), inEarthShadow * clearTw * 0.62f);
                    physSky += float3(0.24f, 0.105f, 0.155f) * (venusArch * clearTw * 0.42f);
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
                // Wave 30 Van Rhijn Curved-Mesopause Airglow Layer (90 km O(1S) 557.7nm + OH Meinel, Kiruna AB1120):
                // Exact spherical shell path-length brightening V(theta) = 1 / sqrt(1 - (R/(R+90))^2 * cos^2(theta))
                // attenuated by lower-troposphere extinction near 0 deg, peaking naturally at 6..10 deg above horizon!
                const float vrRatio = 6360.0f / (6360.0f + 90.0f);
                const float cos2El = max(1.0f - sDir.z * sDir.z, 0.0f);
                const float vanRhijn = rsqrt(max(1.0f - vrRatio * vrRatio * cos2El, 0.025f));
                const float lowExt = exp(-0.085f / max(sDir.z, 0.032f));
                const float airglow = clamp(vanRhijn * lowExt * 0.34f, 0.0f, 1.35f);
                const float3 airCol = lerp(float3(0.012f, 0.024f, 0.018f), float3(0.005f, 0.015f, 0.017f), saturate(sDir.z * 3.0f));
                fullSky += airCol * (airglow * nightSkyW * (1.0f - cover * 0.68f));
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
                // Wave 28 Curved-Ionosphere Geomagnetic-Latitude Aurora (u/Hazmadd r/GraphicsProgramming physical geometry):
                // Intersects each pixel ray with a curved 3D spherical ionosphere shell (R=6360 km, curtain 105..285 km)
                // and places the auroral oval by the geomagnetic latitude of the 3D CURTAIN POINT each pixel hits (NOT the camera)!
                // Automatically curves along the northern horizon from Whiterun and sweeps overhead near Winterhold/Dawnstar!
                const float auroraVis = nightSkyW * (1.0f - saturate(clouds.x * 1.05f)) * smoothstep(0.02f, 0.16f, sDir.z);
                if (auroraVis > 0.02f) {
                    const float rEarthKm = 6360.0f;
                    const float camLatDeg = 60.5f + clamp(camPosW.y / 65000.0f, -2.5f, 3.5f);   // Whiterun ~60.5N -> Winterhold ~63.5N
                    float3 aurSum = 0.0f.xxx;
                    [unroll] for (int ai = 0; ai < 10; ai++) {
                        const float hT = float(ai) / 9.0f;
                        const float altKm = lerp(105.0f, 285.0f, hT);
                        const float rShell = rEarthKm + altKm;
                        const float bS = rEarthKm * sDir.z;
                        const float tKm = sqrt(max(bS * bS + rShell * rShell - rEarthKm * rEarthKm, 0.0f)) - bS;
                        // 3D hit point on the curved ionosphere shell in km (Y = North, X = East):
                        const float2 hitKm = sDir.xy * tKm;
                        const float curtainLat = camLatDeg + hitKm.y * (1.0f / 111.32f);
                        const float curtainLon = hitKm.x * (1.0f / 55.0f);
                        const float nWarp = cNoise(float3(curtainLon * 0.24f, curtainLat * 0.35f, camPosW.w * 0.024f));
                        const float ovalLat1 = 66.2f + sin(curtainLon * 0.42f + camPosW.w * 0.055f + nWarp * 3.4f) * 1.85f;
                        const float ovalLat2 = 68.8f + sin(curtainLon * 0.31f - camPosW.w * 0.042f + nWarp * 2.6f + 1.9f) * 1.45f;
                        const float band1 = exp(-abs(curtainLat - ovalLat1) * 1.45f);
                        const float band2 = exp(-abs(curtainLat - ovalLat2) * 1.15f) * 0.65f;
                        // Vertical geomagnetic field-line striations (aligned with 3D shell longitude, crisp lower edge at 105 km!):
                        const float rays = 0.38f + 0.62f * cNoise(float3(curtainLon * 3.2f, curtainLat * 0.65f, camPosW.w * 0.11f + hT * 2.2f));
                        const float vertEnv = smoothstep(0.0f, 0.12f, hT) * (1.0f - hT * 0.62f);
                        const float distFadeA = exp(-tKm * 0.00055f);
                        const float3 aCol = hT < 0.56f
                            ? lerp(float3(0.05f, 1.02f, 0.52f), float3(0.18f, 0.58f, 0.98f), hT / 0.56f)
                            : lerp(float3(0.18f, 0.58f, 0.98f), float3(0.86f, 0.14f, 0.48f), (hT - 0.56f) / 0.44f);
                        aurSum += aCol * ((band1 + band2) * rays * vertEnv * distFadeA);
                    }
                    fullSky += aurSum * (auroraVis * 0.145f);
                }
                // Natural shooting-star meteor streaks across clear dark northern skies (1 streak every ~9 sec):
                const float metBucket = floor(camPosW.w * 0.11f);
                const float metPhase = frac(camPosW.w * 0.11f);
                if (metPhase < 0.085f && cover < 0.45f) {
                    const float mRand = cHash(float3(metBucket, 17.3f, 3.1f));
                    if (mRand > 0.35f) {
                        const float3 mStart = normalize(float3(cHash(float3(metBucket, 1.1f, 2.2f)) * 2.0f - 1.0f,
                                                               cHash(float3(metBucket, 5.7f, 8.3f)) * 2.0f - 1.0f,
                                                               0.32f + 0.52f * cHash(float3(metBucket, 9.1f, 4.4f))));
                        const float3 mDir = normalize(cross(mStart, float3(0.2f, 0.3f, 0.9f)) - float3(0.0f, 0.0f, 0.45f));
                        const float mT = metPhase / 0.085f;
                        const float3 mHead = normalize(mStart + mDir * (mT * 0.22f));
                        const float3 mRel = sDir - mHead;
                        const float along = dot(mRel, -mDir);
                        const float perp = length(mRel + mDir * clamp(along, 0.0f, 0.065f));
                        const float tail = saturate(1.0f - along / 0.065f) * step(0.0f, along);
                        const float streak = exp(-perp * perp * 220000.0f) * tail * sin(mT * 3.14159265f);
                        fullSky += float3(0.92f, 0.96f, 1.0f) * (streak * nightSkyW * (1.0f - cover) * 2.4f);
                    }
                }
            }
            // Preserve Masser & Secunda twin moons cleanly on top of our procedural night/twilight sky.
            float3 baseSky = lerp(lit.rgb, fullSky, (1.0f - isExotic) * (1.0f - moonKeep * 0.96f));
            if (moonKeep > 0.04f && nightSkyW > 0.04f) {
                baseSky += lit.rgb * float3(1.06f, 0.98f, 0.88f) * (moonKeep * nightSkyW * 0.38f);
            }
            // Wave 29 Target-Frame Cirrus-Gated 22-Degree Hexagonal Ice-Crystal Halo & Sundogs (own-08..11):
            // Intersects each sky ray with the curved 10 km cirrus ice-crystal shell (R=6360+10 km) and modulates the
            // 6-band Snell minimum-deviation dispersion (21.7 deg red inside -> 22.5 deg violet outside) by actual cirrus crystal density!
            if ((1.0f - isExotic) > 0.2f && sDir.z > 0.04f) {
                const float coldAir = saturate(weather.z * 1.25f + smoothstep(2200.0f, 4400.0f, camPosW.z) * 0.65f + saturate(clouds.x - 0.18f) * 0.55f);
                if (coldAir > 0.15f) {
                    const float rEC = 6360.0f;
                    const float bC = rEC * sDir.z;
                    const float tCirKm = sqrt(max(bC * bC + (rEC + 10.0f) * (rEC + 10.0f) - rEC * rEC, 0.0f)) - bC;
                    const float2 cirHit = sDir.xy * tCirKm * 0.045f + cloudWind.xy * (camPosW.w * 0.00018f);
                    const float cirWisp = smoothstep(0.28f, 0.78f, cNoise(float3(cirHit, 3.7f)) * 0.65f + cNoise(float3(cirHit * 2.6f, 9.1f)) * 0.35f);
                    const float cirCrystal = lerp(0.30f, 1.0f, cirWisp);
                    if (nightSkyW > 0.20f && sunDir.w > 0.5f && sunDir.z > 0.10f) {
                        const float mAng = acos(clamp(dot(sDir, normalize(sunDir.xyz)), -1.0f, 1.0f));
                        // Forbidden dark inner sky inside 21.5 deg + crisp red-inner / blue-outer Snell dispersion:
                        const float halo22 = exp(-pow((mAng - 0.381f) / 0.020f, 2.0f)) * smoothstep(0.345f, 0.376f, mAng);
                        const float3 iceDisp = float3(
                            exp(-pow((mAng - 0.378f) / 0.015f, 2.0f)),
                            exp(-pow((mAng - 0.384f) / 0.017f, 2.0f)),
                            exp(-pow((mAng - 0.393f) / 0.019f, 2.0f)));
                        baseSky += (float3(0.95f, 0.86f, 0.72f) * halo22 * 0.45f + iceDisp * 0.55f) * (coldAir * cirCrystal * nightSkyW * (1.0f - cover * 0.65f) * 0.036f);
                    } else if (dayW > 0.20f && aElev > 0.04f && aElev < 0.34f && weather.z > 0.10f) {
                        const float sAng = acos(clamp(dot(sDir, aSun), -1.0f, 1.0f));
                        const float elevDiff = abs(sDir.z - aSun.z);
                        const float parhelia = exp(-pow((sAng - 0.381f) / 0.018f, 2.0f)) * exp(-pow(elevDiff / 0.042f, 2.0f));
                        const float3 dogDisp = float3(
                            exp(-pow((sAng - 0.378f) / 0.015f, 2.0f)),
                            exp(-pow((sAng - 0.384f) / 0.017f, 2.0f)),
                            exp(-pow((sAng - 0.393f) / 0.020f, 2.0f)));
                        baseSky += (float3(1.12f, 0.92f, 0.70f) * parhelia * 0.55f + dogDisp * 0.45f) * (coldAir * cirCrystal * dayW * (1.0f - cover * 0.55f) * 0.105f);
                    }
                }
            }
            if ((flags.x & FLAG_CLOUDS) != 0u) {
                const float2 skyUv = (px + 0.5f) * dims.zw;
                // Crisp hardware bilinear lookup (removes 5-tap blur that smeared 48-step cumulus detail into clay):
                const float4 cl = Clouds.SampleLevel(Lin, skyUv, 0.0f);
                // Let Masser & Secunda glow visibly through thin/moderate night clouds instead of disappearing!
                const float clTrans = lerp(cl.a, max(cl.a, 0.38f), moonKeep * nightSkyW);
                baseSky = lerp(baseSky, baseSky * clTrans + cl.rgb, 1.0f - isExotic);
                // Allow the solar disc to shine cleanly through thin/moderate cloud veils so the Sun stays perceptible!
                // Only show the crisp solar disc when clouds thin out (clTrans > 0.22); thick overcast hides the hard disc (14.06.14)!
                const float sunCloudVis = smoothstep(0.22f, 0.68f, clTrans);
                if (sunDiscGate > 0.01f && sunCloudVis > 0.005f) {
                    const float sunVeil = smoothstep(0.99980f, 0.99996f, cosS) * 14.0f + pow(cosS, 64.0f) * 0.16f;
                    const float3 sunVeilCol = lerp(float3(1.0f, 0.97f, 0.90f), float3(1.0f, 0.60f, 0.22f), lowS);
                    baseSky += sunVeilCol * (sunVeil * sunCloudVis * sunDiscGate * (1.0f - isExotic));
                }
                // Camera-reactive optical lens response (Zeiss/Cooke double-Gauss multi-element chromatic ghosts + anamorphic flare + veiling glare):
                // Reacts live to camera pan/pitch toward the Sun and automatically dims behind clouds or when looking away.
                if (sunDiscGate > 0.02f && (1.0f - isExotic) > 0.2f) {
                    const float4 sunScr = screenOf(eyeAt() + aSun * 50000.0f);
                    if (sunScr.w > 0.0f) {
                        const float2 sunUv = sunScr.xy * dims.zw;
                        if (all(sunUv > -0.25f) && all(sunUv < 1.25f)) {
                            const float edgeFadeS = smoothstep(-0.25f, 0.08f, min(min(sunUv.x, 1.0f - sunUv.x), min(sunUv.y, 1.0f - sunUv.y)));
                            // Check if the sun center pixel is unobstructed sky & read cloud transmittance at sun position:
                            const int2 sunPx = clamp(int2(sunScr.xy), int2(0, 0), int2(dims.xy) - 1);
                            const float sunClear = (Depth.Load(int3(sunPx, 0)) >= 0.9999f) ? 1.0f : 0.0f;
                            const float sunClT = Clouds.SampleLevel(Lin, saturate(sunUv), 0.0f).a;
                            const float flareVis = sunClear * pow(saturate(sunClT), 0.75f) * edgeFadeS * sunDiscGate;
                            if (flareVis > 0.01f) {
                                const float2 aspectS = float2(dims.x * dims.w, 1.0f);
                                const float2 curUv = (px + 0.5f) * dims.zw;
                                const float2 toSunUv = (curUv - sunUv) * aspectS;
                                // Subtle horizontal anamorphic streak + warm optical veiling glare + spectral 6-ray aperture diffraction (photos 16/23):
                                const float rSunLen = length(toSunUv);
                                const float anam = exp(-abs(toSunUv.y) * 48.0f) * exp(-abs(toSunUv.x) * 3.4f) * 0.24f;
                                const float veil = exp(-rSunLen * 2.5f) * 0.14f;
                                const float angS = atan2(toSunUv.y, toSunUv.x);
                                const float spoke6 = pow(abs(cos(angS * 3.0f)), 24.0f) * exp(-rSunLen * 6.5f);
                                const float3 prismCol = float3(
                                    0.65f + 0.35f * cos(rSunLen * 22.0f),
                                    0.65f + 0.35f * cos(rSunLen * 22.0f - 2.094f),
                                    0.65f + 0.35f * cos(rSunLen * 22.0f - 4.188f));
                                const float3 flareCol = lerp(float3(1.0f, 0.92f, 0.78f), float3(1.0f, 0.52f, 0.20f), lowS);
                                float3 lensAdd = flareCol * (anam + veil) + prismCol * (spoke6 * 0.28f);
                                // 3 chromatic multi-element optical ghosts mirrored across the optical center (0.5, 0.5):
                                const float2 axis = float2(0.5f, 0.5f) - sunUv;
                                static const float kGPos[3]  = { 0.55f, 1.15f, 1.68f };
                                static const float kGRad[3]  = { 0.045f, 0.075f, 0.032f };
                                static const float3 kGCol[3] = {
                                    float3(0.45f, 0.78f, 1.00f),
                                    float3(1.00f, 0.72f, 0.42f),
                                    float3(0.65f, 0.95f, 0.72f)
                                };
                                [unroll] for (int gi = 0; gi < 3; ++gi) {
                                    const float2 gCenter = sunUv + axis * kGPos[gi];
                                    const float gd = length((curUv - gCenter) * aspectS) / kGRad[gi];
                                    const float ring = exp(-pow(gd - 0.75f, 2.0f) * 9.0f) * 0.06f + exp(-gd * gd * 3.5f) * 0.03f;
                                    lensAdd += kGCol[gi] * ring;
                                }
                                baseSky += lensAdd * (flareVis * 0.65f);
                            }
                        }
                    }
                }
                // Physical 3D Crepuscular Cloud-Gap Light Rays (12-tap IGN-jittered radial transmittance march toward live sun):
                // smooth band-free sunrays fanning out of cumulus breaks with soft off-screen sun fade!
                // Removed screen-space 12-tap radial sun-spoke hack (21.03.52.63.png): true 3D volumetric godrays
                // belong exclusively in FroxelPass / VolumetricPass (zero screen-space radial spokes across overcast sky!).
            }
            // Wave 29 Target-Frame Rain-Shaft Primary (42 deg) + Secondary (51 deg) Rainbow + Negative-Space Alexander's Dark Band:
            // Between 42 deg (0.733 rad) and 51 deg (0.890 rad), Snell droplet optics forbids 1- or 2-bounce rays from emerging,
            // creating the real physical dark sky arch (Alexander's Dark Band) while brightening the supernumerary interior (<41 deg)!
            if (weather.w > 0.32f && clouds.x > 0.35f && aElev > 0.05f && aElev < 0.52f && sDir.z > 0.02f) {
                const float antiAng = acos(clamp(dot(sDir, -aSun), -1.0f, 1.0f));
                const float pR = exp(-pow((antiAng - 0.742f) / 0.011f, 2.0f));
                const float pG = exp(-pow((antiAng - 0.728f) / 0.011f, 2.0f));
                const float pB = exp(-pow((antiAng - 0.714f) / 0.011f, 2.0f));
                const float sR = exp(-pow((antiAng - 0.876f) / 0.014f, 2.0f)) * 0.28f;
                const float sG = exp(-pow((antiAng - 0.892f) / 0.014f, 2.0f)) * 0.28f;
                const float sB = exp(-pow((antiAng - 0.908f) / 0.014f, 2.0f)) * 0.28f;
                const float bowW = smoothstep(0.32f, 0.72f, weather.w) * smoothstep(0.35f, 0.65f, clouds.x)
                                 * smoothstep(0.05f, 0.14f, aElev) * (1.0f - smoothstep(0.36f, 0.52f, aElev))
                                 * smoothstep(0.02f, 0.10f, sDir.z) * (1.0f - isExotic);
                // Negative-space Alexander's Dark Band (42.5..50.5 deg) & supernumerary interior brightness (<41.2 deg):
                const float alexBand = smoothstep(0.735f, 0.768f, antiAng) * (1.0f - smoothstep(0.855f, 0.892f, antiAng));
                const float superInner = (1.0f - smoothstep(0.680f, 0.725f, antiAng)) * smoothstep(0.520f, 0.650f, antiAng);
                baseSky *= (1.0f - alexBand * bowW * 0.15f + superInner * bowW * 0.065f);
                baseSky += float3(pR + sR, pG + sG, pB + sB) * (bowW * 0.12f);
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
    // Live 3D World-Map Depth Isolation + True 3D Froxel Transmittance & In-Scatter Composition:
    // Within 24u..42,000u (kFroxFar), FroxelVol supplies genuine 3D Beer-Lambert transmittance (frox.a) and
    // dynamically lit 3D in-scattering (frox.rgb); VolFog adds fine crepuscular shafts without double-fogging.
    const float mapScale = (timeEnv.w > 0.5f && camPosW.z > 42000.0f) ? 0.18f : 1.0f;
    const float sunLumFg = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float dayFg = (sunColour.w > 0.5f) ? saturate((sunLumFg - 0.18f) * 3.0f) : 0.0f;
    const float fgClear = lerp(1.0f, smoothstep(260.0f, 1500.0f, dist), dayFg * (1.0f - saturate(weather.w * 0.65f + weather.z * 0.75f)));
    const float4 frox = sampleFroxel3D(uv, curZ);
    const float froxT = lerp(1.0f, saturate(frox.a), mapScale);
    const float3 froxScat = frox.rgb * mapScale;
    const float3 shaftScatter = vol2D * (0.14f * mapScale * fgClear);
    const float3 scatter = froxScat + shaftScatter;
    // Engine-native underwater medium: gate strictly on pixels below the live water plane so half-submerged cameras
    // NEVER tint above-water mountains or sky into harsh underwater cyan!
    if ((flags.x & FLAG_UNDERWATER) != 0u && (engineWorld.z < 0.5f || (p.z + camPosW.z) <= engineWorld.x + 10.0f)) {
        const float3 uw = elderUnderwaterMedium(lit.rgb, dist, 0.0016f);
        return float4(uw + scatter * float3(0.22f, 0.68f, 0.82f) * 1.15f, lit.a);
    }
    // Engine-level unified 3D froxel (0..42,000u) + Hillaire far-mountain aerial perspective (beyond kFroxFar=42,000u):
    if ((flags.x & FLAG_FOG_REPLACES) != 0u && sunColour.w > 0.5f) {
        const float weatherDens = 1.0f + weather.w * 2.1f + weather.z * 2.5f + saturate(clouds.x - 0.35f) * 1.05f;
        const float baseSigma = 0.0000125f * weatherDens;
        const float hStart = max(camPosW.z * 0.00025f, 0.0f);
        const float elevW = lerp(1.10f, 0.44f, smoothstep(-0.15f, 0.30f, w.z));
        // Far-field mountain & horizon continuation strictly BEYOND kFroxFar (zero 2D double-fogging inside the 3D froxel volume!):
        const float farSpan = min(max(dist - kFroxFar, 0.0f), 220000.0f);
        const float3 midPt = camPosW.xyz + eye + w * clamp(kFroxFar + farSpan * 0.50f, min(dist * 0.65f, kFroxFar), 120000.0f);
        const float3 windShift = float3(cloudWind.xy * camPosW.w * 0.00006f, 0.0f);
        const float alpineNoise = cNoise(midPt * 0.00014f + windShift) * 0.65f + cNoise(midPt * 0.00042f - windShift * 1.6f + 7.1f) * 0.35f;
        const float peakCollar = exp(-pow((midPt.z - 5200.0f) / 3600.0f, 2.0f)) * smoothstep(24000.0f, 52000.0f, dist);
        const float alpineMist = (0.76f + 0.48f * alpineNoise) * (1.0f + peakCollar * (0.42f + 0.50f * alpineNoise));
        const float odFar = baseSigma * farSpan * elevW * alpineMist * exp(-hStart * 0.15f);
        const float horizonMelt = smoothstep(75000.0f, 150000.0f, dist);
        // Wavelength-dependent far aerial transmittance composed multiplicatively with true 3D froxel transmittance froxT:
        float3 Tfar = exp(-odFar * float3(0.84f, 0.96f, 1.12f)) * (1.0f - horizonMelt);
        float3 T = froxT * Tfar;
        float3 sZ, sM, sH;
        skyPalette(sZ, sM, sH);
        const float3 aSunFg = astroSunDir();
        const float aElevFg = astroSunElev();
        const float nightF = smoothstep(0.03f, -0.07f, aElevFg);
        const float twilightF = smoothstep(-0.08f, 0.02f, aElevFg) * (1.0f - smoothstep(0.08f, 0.26f, aElevFg));
        const float highSunF = smoothstep(0.20f, 0.48f, aElevFg);
        const float cosSunF = saturate(dot(w, aSunFg));
        const float cloudCov = saturate(clouds.x);
        float3 uSunF, uSkyZF, uSkyHF, uAmbUpF, uAmbDnF;
        evalWorldAtmosphere(uSunF, uSkyZF, uSkyHF, uAmbUpF, uAmbDnF);
        const float3 hillaireSky = sampleHillaireSky(normalize(float3(w.xy, max(w.z, 0.008f))));
        const float hLumF = max(dot(hillaireSky, float3(0.2126f, 0.7152f, 0.0722f)), 0.04f);
        const float overcastWF = saturate((cloudCov - 0.55f) * 1.95f + weather.w * 0.52f + weather.z * 0.45f);
        const float3 overcastScatterF = lerp(hillaireSky, hLumF.xxx * float3(0.88f, 0.94f, 1.04f), 0.72f);
        float3 atmoCol = lerp(hillaireSky, overcastScatterF, overcastWF * 0.78f);
        atmoCol = lerp(atmoCol, lerp(uSkyHF, uSkyZF, saturate(max(w.z, 0.0f) * 2.2f)), 0.28f) * (0.92f * lerp(1.0f, 0.78f, twilightF * (1.0f - horizonMelt)));
        if ((flags.x & FLAG_CLOUDS) != 0u) {
            const float4 clF = Clouds.SampleLevel(Lin, uv, 0.0f);
            const float clMerge = smoothstep(28000.0f, 75000.0f, dist);
            atmoCol = lerp(atmoCol, atmoCol * clF.a + clF.rgb, clMerge);
        }
        // Dynamically lit far mountain aerial perspective: cloud-shadowed directional solar in-scattering vs shadowed ridges!
        const float fogCloudVis = (flags.x & FLAG_CLOUDS) != 0u ? cloudShadowN(midPt, 1u) : 1.0f;
        const float fogSunPhase = 0.18f + 0.82f * pow(cosSunF, 3.0f);
        const float3 d65Mie = lerp(float3(1.0f, 0.62f, 0.30f), float3(0.96f, 0.98f, 1.02f), highSunF);
        const float mieHalo = (pow(cosSunF, 5.0f) * 0.10f + pow(cosSunF, 28.0f) * 0.16f) * (1.0f - cloudCov * 0.72f) * (1.0f - nightF) * fogCloudVis;
        const float cloudMod = lerp(lerp(0.46f, 0.94f, fogCloudVis * (1.0f - nightF * 0.65f)), 1.0f, horizonMelt);
        atmoCol = atmoCol * cloudMod + lerp(d65Mie, uSunF, 0.60f) * (mieHalo + fogSunPhase * fogCloudVis * (1.0f - nightF) * (1.0f - horizonMelt) * 0.06f);
        const float3 ambSkyF = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), 0.03f.xxx);
        const float overcastDeckF = saturate(cloudCov * 0.85f + weather.w * 0.45f + weather.z * 0.55f);
        const float3 nightHorF = lerp(float3(0.028f, 0.044f, 0.078f), ambSkyF * float3(0.68f, 0.74f, 0.84f), overcastDeckF * 0.85f);
        atmoCol = lerp(atmoCol, nightHorF, nightF);
        // Wave 32 Dynamic Dry-Windstorm Particulate Haze (zero hardcoded blocks: activates ONLY during genuine dry
        // high-wind overcast storms, never during clear golden sunsets in Whiterun):
        const float windMagF = length(cloudWind.xy);
        const float ashW = saturate((clouds.x - 0.78f) * 4.5f) * saturate((windMagF - 260.0f) / 140.0f)
                         * (1.0f - saturate(weather.w * 6.0f + weather.z * 6.0f));
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
            // Attenuate blizzard sheet luminance by nightF so snowy night cliffs never glow bright grey-green against dark indigo sky (16.01.41/48)!
            const float3 daySheetCol = lerp(float3(0.24f, 0.29f, 0.36f), float3(0.62f, 0.68f, 0.76f), weather.z);
            const float3 sheetCol = lerp(daySheetCol, nightHorF * 1.05f, nightF);
            atmoCol = lerp(atmoCol, sheetCol, sheetDens * 0.48f);
            T *= (1.0f - sheetDens * 0.35f);
        }
        // Re-lock distant snowy/overcast twilight & night horizon color to exact sky-dome nightHorF so Winterhold cliffs never cut out!
        atmoCol = lerp(atmoCol, nightHorF, nightF * 0.85f);
        atmoCol = min(atmoCol, 3.5f.xxx);
        // GTA6-Surpassing 6-Layer Native DX12 3D World-Space Volumetric Precipitation (depth-occluded in 3D space):
        // Real Rain (wind-slanted specular streaks catching local torch/lantern light!), Real Snow (helical 3D vortex
        // snowflakes with foreground bokeh flakes & distant blizzard sheets), and Real Volcanic Ashstorms (swirling ash + embers)!
        float3 precipAdd = 0.0f.xxx;
        if ((weather.w > 0.08f || weather.z > 0.08f || ashW > 0.10f) && mapScale > 0.5f) {
            const float2 windP = cloudWind.xy * 0.0032f;
            const uint nPrecipLights = min(flags.y, 12u);
            [unroll] for (int pl = 0; pl < 6; ++pl) {
                const float pDist = 145.0f + float(pl) * 135.0f;
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
                        // Delicate Gaussian thin-filament rain streaks (kills giant white paper confetti rectangles in v1418_a.png!):
                        const float2 rF = frac(rCoord.xy) - 0.5f;
                        const float rThin = exp(-rF.x * rF.x * 260.0f) * exp(-rF.y * rF.y * 10.0f);
                        const float rMask = smoothstep(rGate, 0.998f, rCell) * rThin;
                        const float3 rLit = max(atmoCol * 0.95f + 0.06f, 0.12f.xxx) + localLightP * 0.72f;
                        precipAdd += rLit * (rMask * stormBoost * 0.24f / (1.0f + float(pl) * 0.40f));
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
        // True 3D World-Space Rising Blackbody Fire Embers around live hearths/campfires/braziers (strictly depth-occluded by dist!):
        if (mapScale > 0.5f && flags.y > 0u) {
            const uint nEmberL = min(flags.y, 12u);
            const float2 windE = normalize(cloudWind.xy + float2(1.0f, 0.35f)) * 18.0f;
            [loop] for (uint ei = 0u; ei < nEmberL; ++ei) {
                const float3 lcE = lightColS[ei].rgb;
                const float rE = lightPosR[ei].w;
                if (lcE.r <= lcE.b * 1.35f || lcE.r < 0.16f || rE < 110.0f) { continue; }
                const float3 lRel = lightPosR[ei].xyz;
                const float tClosest = dot(lRel - eye, w);
                if (tClosest <= 35.0f || tClosest >= dist || tClosest > 1600.0f) { continue; }
                const float3 ptRay = eye + w * tClosest;
                const float3 offL = ptRay - lRel;
                if (offL.z < -10.0f || offL.z > 155.0f || dot(offL.xy, offL.xy) > 6400.0f) { continue; }
                const float hNorm = saturate((offL.z + 10.0f) / 165.0f);
                const float3 wpE = camPosW.xyz + ptRay;
                const float3 eGrid = float3((wpE.xy - windE * hNorm + float2(sin(camPosW.w * 2.2f + wpE.z * 0.08f), cos(camPosW.w * 1.9f + wpE.z * 0.07f)) * 6.5f) * 0.14f,
                                            wpE.z * 0.09f - camPosW.w * 1.65f);
                const float eHash = cHash(floor(eGrid));
                if (eHash > 0.945f) {
                    const float dSpark = length(frac(eGrid) - 0.5f);
                    const float spark = smoothstep(0.945f, 0.998f, eHash) * exp(-dSpark * dSpark * 42.0f) * (1.0f - hNorm) * smoothstep(0.0f, 0.12f, hNorm);
                    // Blackbody cooling ramp: 2800K bright gold-white near flame -> 1500K deep crimson ember high up!
                    const float3 bbCol = lerp(float3(1.85f, 0.92f, 0.28f), float3(1.15f, 0.22f, 0.04f), hNorm);
                    precipAdd += bbCol * (spark * (1.0f - smoothstep(1000.0f, 1600.0f, tClosest)) * 0.85f);
                }
            }
        }
        // Wave 29 Negative-Space Depth-Parity Silhouette Diffraction & Crepuscular Dark-Shaft Cavity Contrast:
        // 1) Asymmetric silhouette edge diffraction: when a foreground occluder (curZ) borders a far bright sky/HDR void
        //    (zQ > curZ * 2.5), warm 620..670nm diffracted light wraps subtly OVER the dark foreground edge (emulsion
        //    halation + forward Mie edge diffraction). Dark foreground NEVER bleeds outward into bright sky!
        // 2) Transverse crepuscular cavity contrast: shadowed fog columns bordering bright sunlit shafts keep cool indigo depth.
        float3 silDiffAdd = 0.0f.xxx;
        if (mapScale > 0.5f && curZ < 6000.0f) {
            const int2 dMaxF = int2(dims.xy) - 1;
            const int2 ipF = int2(px);
            const float lFg = dot(lit.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            static const int2 kSil4[4] = { int2(2, 0), int2(-2, 0), int2(0, 2), int2(0, -2) };
            [unroll] for (int si = 0; si < 4; si++) {
                const int2 qS = clamp(ipF + kSil4[si], int2(0, 0), dMaxF);
                const float dS = Depth.Load(int3(qS, 0));
                const float zS = dS >= 1.0f ? 1.0e6f : viewZOf(dS);
                if (zS > curZ * 2.5f) {
                    const float3 bgCol = dS >= 1.0f ? atmoCol : max(Lit.Load(int3(qS, 0)).rgb, 0.0f.xxx);
                    const float lBg = dot(bgCol, float3(0.2126f, 0.7152f, 0.0722f));
                    if (lBg > max(lFg * 2.2f, 0.35f)) {
                        // Long-wavelength red-weighted edge diffraction wrap (Archive Cluster M emulsion halation):
                        silDiffAdd += min(bgCol, 2.5f.xxx) * float3(1.18f, 0.82f, 0.52f) * (saturate((lBg - lFg) * 0.28f) * 0.032f);
                    }
                }
            }
        }
        // Crepuscular dark-shaft cavity preservation: cool indigo-violet tint when local volumetric scatter dips beside sunlit air:
        const float shaftL = dot(shaftScatter, float3(0.2126f, 0.7152f, 0.0722f));
        const float shaftShear = abs(ddx(shaftL)) + abs(ddy(shaftL));
        const float3 cavityTint = lerp(1.0f.xxx, float3(0.84f, 0.90f, 1.12f), saturate(shaftShear * 14.0f) * (1.0f - saturate(shaftL * 8.0f)) * (1.0f - nightF));
        // Exact front-to-back Beer-Lambert composition:
        //   lit * (froxT * Tfar) + froxScat (0..42k 3D volume) + farInScat (42k..horizon, attenuated by froxT) + shafts + precip + silDiff
        const float3 farInScat = atmoCol * (1.0f.xxx - Tfar) * froxT;
        return float4(lit.rgb * T + (froxScat * cavityTint) * pdeMistBoost + farInScat + shaftScatter * saturate(T + 0.15f) * pdeMistBoost + precipAdd + silDiffAdd, lit.a);
    }
    // Dungeons, Caves, Nordic Tombs, Dwemer Ruins, Blackreach & Interiors (sunColour.w < 0.5):
    // Crisp, deep-contrast interior air (eliminates milky grey Dragonsreach haze & Septimus ice-cave whiteout) + rising hearth embers.
    if (sunColour.w < 0.5f && (flags.x & FLAG_FOG) != 0u) {
        const float3 wpInt = camPosW.xyz + eye + w * min(dist * 0.5f, 3200.0f);
        const float caveNoise = 0.78f + 0.44f * cNoise(wpInt * 0.00048f + float3(0.0f, 0.0f, camPosW.w * 0.015f));
        const float intOd = max(dist - 750.0f, 0.0f) * 0.000014f * caveNoise;
        const float intT = exp(-min(intOd, 0.18f));
        const float3 cellAmb = max(float3(ambient[0].w, ambient[1].w, ambient[2].w), float3(0.008f, 0.010f, 0.012f));
        const float ambL = dot(cellAmb, float3(0.2126f, 0.7152f, 0.0722f));
        const float3 caveMist = min(lerp(ambL.xxx, cellAmb, 0.45f) * 0.16f, 0.024f.xxx);
        float3 intEmberAdd = 0.0f;
        if (flags.y > 0u) {
            const uint nEmberI = min(flags.y, 16u);
            [loop] for (uint ei = 0u; ei < nEmberI; ++ei) {
                const float3 lcE = lightColS[ei].rgb;
                const float rE = lightPosR[ei].w;
                if (lcE.r <= lcE.b * 1.28f || lcE.r < 0.14f || rE < 95.0f) { continue; }
                const float3 lRel = lightPosR[ei].xyz;
                const float tClosest = dot(lRel - eye, w);
                if (tClosest <= 28.0f || tClosest >= dist || tClosest > 1400.0f) { continue; }
                const float3 ptRay = eye + w * tClosest;
                const float3 offL = ptRay - lRel;
                if (offL.z < -25.0f || offL.z > 165.0f || dot(offL.xy, offL.xy) > 7200.0f) { continue; }
                const float hNorm = saturate((offL.z + 25.0f) / 190.0f);
                const float3 wpE = camPosW.xyz + ptRay;
                const float3 eGrid = float3((wpE.xy + float2(sin(camPosW.w * 2.1f + wpE.z * 0.085f), cos(camPosW.w * 1.8f + wpE.z * 0.075f)) * 7.0f) * 0.145f,
                                            wpE.z * 0.092f - camPosW.w * 1.55f);
                const float eHash = cHash(floor(eGrid));
                if (eHash > 0.940f) {
                    const float dSpark = length(frac(eGrid) - 0.5f);
                    const float spark = smoothstep(0.940f, 0.998f, eHash) * exp(-dSpark * dSpark * 40.0f) * (1.0f - hNorm) * smoothstep(0.0f, 0.10f, hNorm);
                    const float3 bbCol = lerp(float3(2.10f, 0.96f, 0.26f), float3(1.25f, 0.20f, 0.03f), hNorm);
                    intEmberAdd += bbCol * (spark * (1.0f - smoothstep(900.0f, 1400.0f, tClosest)) * 0.95f);
                }
            }
        }
        // Unlock rich 3D Froxel volumetric torch/hearth cones in dungeons, ice caves & taverns (0.52x scatter)!
        return float4(lit.rgb * intT + caveMist * (1.0f - intT) + scatter * 0.52f + intEmberAdd, lit.a);
    }
    const float T = (flags.x & FLAG_FOG_REPLACES) != 0u ? exp(-sigma * tEnd) : 1.0f;
    return float4(lit.rgb * T + scatter, lit.a);
}
