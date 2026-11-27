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
#include "RawProceduralNoise.hlsli"
#include "RawWeatherAtmo.hlsli"
#include "RawCosmosSky.hlsli"
#include "RawSpells3D.hlsli"
#include "RawSplitTemporalAerial.hlsli"
#include "RawBlackbodySss.hlsli"
#include "RawIridescenceFrostHalo.hlsli"

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
    const float rdLen2 = dot(rd, rd);
    const float3 d = (isfinite(rdLen2) && rdLen2 > 1.0e-8f) ? (rd * rsqrt(rdLen2)) : float3(0.0f, 0.0f, 1.0f);
    const float az = atan2(d.y, d.x);
    const float elev = asin(clamp(d.z, -1.0f, 1.0f));
    const float vNonLin = saturate(0.5f + 0.5f * (elev >= 0.0f ? sqrt(elev / (0.5f * PI)) : -sqrt(-elev / (0.5f * PI))));
    // Exact texel-center inverse of AtmosphereLut.hlsl::skyview (texel 0 == -PI, texel 191 == +PI -> zero seam!):
    const float uRaw = frac(az / (2.0f * PI) + 0.5f);
    const float uLut = (uRaw * 191.0f + 0.5f) / 192.0f;
    const float vLut = (vNonLin * 107.0f + 0.5f) / 108.0f;
    float3 c0 = AtmoSkyView.SampleLevel(Lin, float2(uLut, vLut), 0.0f).rgb;
    // Symmetric periodic wrap reinforcement right across the +-PI meridian:
    const float edgeD = min(uRaw, 1.0f - uRaw);
    if (edgeD < 0.020f) {
        const float uOpp = (uRaw < 0.5f) ? (191.5f / 192.0f) : (0.5f / 192.0f);
        const float3 cWrap = AtmoSkyView.SampleLevel(Lin, float2(uOpp, vLut), 0.0f).rgb;
        c0 = lerp(0.5f * (c0 + cWrap), c0, smoothstep(0.0f, 0.020f, edgeD));
    }
    // Eliminate zenith-pole azimuth pinch when looking straight up (d.z > 0.94).
    const float poleW = smoothstep(0.94f, 0.995f, d.z);
    if (poleW > 0.001f) {
        const float3 zAvg = (AtmoSkyView.SampleLevel(Lin, float2(0.25f, 107.5f / 108.0f), 0.0f).rgb +
                             AtmoSkyView.SampleLevel(Lin, float2(0.75f, 107.5f / 108.0f), 0.0f).rgb) * 0.5f;
        c0 = lerp(c0, zAvg, poleW);
    }
    return all(isfinite(c0)) ? max(c0, 0.0f.xxx) : 0.0f.xxx;
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

// Verified HDR scene-rendered combustion core detector:
// Requires high incandescent HDR luminance (luma > 2.10) AND strong flame chromaticity
// (R > G * 1.28 && R > B * 3.00), rejecting warm walls, sunlit roofs, snow, banners, and empty-air point lights.
float rawDetectSceneFlameCore(float3 rgb) {
    if (!all(isfinite(rgb))) { return 0.0f; }
    const float luma = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    if (luma <= 2.10f || rgb.r <= 2.40f) { return 0.0f; }
    if (rgb.r <= rgb.g * 1.28f || rgb.r <= rgb.b * 3.00f) { return 0.0f; }
    const float warmRG = smoothstep(1.28f, 1.62f, rgb.r / max(rgb.g, 0.05f));
    const float warmRB = smoothstep(3.00f, 4.50f, rgb.r / max(rgb.b, 0.02f));
    const float lumW   = smoothstep(2.10f, 4.20f, luma);
    return lumW * lerp(0.55f, 1.0f, warmRG * warmRB);
}

// Wave 39: Wind-Advected & Eddy-Dispersion-Coupled 3D Volumetric Fire, Self-Shadowed Dark Soot Smoke & Embers.
// Strictly anchored to verified real scene-rendered flame pixels in `Lit` + `Depth` (ZERO blind point-light spheres!).
// Couples buoyant 3D curl-advected flames, dark carbon soot billows, and ember spark streaks directly to:
//   - Live exterior wind `cloudWind.xy` (tilts & stretches smoke plumes + spark trails downwind; calm interiors rise straight with mushroom eddies!)
//   - Atmospheric eddy dispersion (`phenology1.w` + wind speed + cold inversion `phenology1.z`): disperses faster in windy storms, hangs thicker in still cold air!
//   - Multi-band internal fire-to-smoke radiance scatter (`janvlietinck` / `benharkervfx` / `mytinodev`): glowing burnt-orange undersides -> cool charcoal tops!
// Returns float4(emissionRGB, backgroundTransmittance).
float4 evalVolumetricFirePlumes(float3 eye, float3 w, float dist, float curZ, float2 px, bool isInt) {
    const float jit = ign(px + float2(7.0f, 19.0f));
    const float tClock = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
    const float2 rawWindXY = isInt ? float2(0.0f, 0.0f) : cloudWind.xy;
    const float windSpd = length(rawWindXY);
    const float windNorm = saturate(windSpd / 280.0f);
    // Physical horizontal plume deflection in world units per unit rise (zero indoors, strong downwind tilt in storms):
    const float2 windTiltXY = isInt ? float2(0.0f, 0.0f) : clamp(rawWindXY * 0.085f, -38.0f.xx, 38.0f.xx);
    // Atmospheric eddy diffusivity dispersion (phenology1.w = evap/eddy mixing, phenology1.z = calm nocturnal inversion):
    const float eddyDisp = isInt ? 0.22f
                                 : saturate(phenology1.w * 0.85f + windNorm * 0.72f + weather.w * 0.28f - phenology1.z * 0.35f);

    // Project wind-tilted world -Z (upwind + down toward verified flame bases) into screen space analytically:
    const float4 sRef0  = screenOf(eye + w * 200.0f);
    const float4 sRefDn = screenOf(eye + w * 200.0f - float3(windTiltXY * 0.38f, 20.0f));
    float2 downPxDir = sRefDn.xy - sRef0.xy;
    const float downLen = length(downPxDir);
    downPxDir = (downLen > 1.0e-3f) ? (downPxDir / downLen) : float2(0.0f, 1.0f);
    const float2 sidePxDir = float2(-downPxDir.y, downPxDir.x);

    // Search a compact wind-tilted buoyant inverted cone below current pixel for verified real scene-rendered flame cores
    // (tight local plume footprint so secondary LightFog smoke never double-stacks over 3D Navier-Stokes fire!).
    static const float kTierFrac[8] = { 0.008f, 0.016f, 0.028f, 0.044f, 0.064f, 0.088f, 0.115f, 0.145f };
    const float maxConePx = min(dims.y, 1440.0f) * 0.78f;

    float bestFlame = 0.0f;
    float2 bestPx   = px;
    float bestD     = 1.0f;
    float bestZ     = 1.0e6f;

    // Center tap (pixel directly on/near a real flame core):
    {
        const float2 cUv = (px + 0.5f) * dims.zw;
        const float d0 = Depth.SampleLevel(Lin, cUv, 0.0f);
        if (d0 < 0.9990f) {
            const float z0 = viewZOf(d0);
            if (z0 > 14.0f && z0 < 1400.0f && z0 <= curZ * 1.04f + 10.0f) {
                const float f0 = rawDetectSceneFlameCore(Lit.SampleLevel(Lin, cUv, 0.0f).rgb);
                if (f0 > bestFlame) { bestFlame = f0; bestPx = px; bestD = d0; bestZ = z0; }
            }
        }
    }

    // Wind-tilted inverted buoyant cone search downward toward flame bases:
    [unroll] for (int ti = 0; ti < 8; ++ti) {
        const float dPx = kTierFrac[ti] * maxConePx * (0.86f + 0.28f * jit);
        const float spreadPx = dPx * lerp(0.22f, 0.32f, eddyDisp);
        [unroll] for (int sideI = 0; sideI < 2; ++sideI) {
            const float sSign = (sideI == 0) ? -1.0f : 1.0f;
            const float latOff = sSign * spreadPx * ((ti & 1) == 0 ? 0.35f : 0.85f);
            const float2 qPx = clamp(px + downPxDir * dPx + sidePxDir * latOff, float2(1.0f, 1.0f), dims.xy - 2.0f);
            const float2 qUv = (qPx + 0.5f) * dims.zw;
            const float dTap = Depth.SampleLevel(Lin, qUv, 0.0f);
            if (dTap >= 0.9990f) { continue; }
            const float zTap = viewZOf(dTap);
            // Strictly reject sky, far horizon, near-clip, and flames occluded behind foreground geometry:
            if (zTap < 14.0f || zTap > 1400.0f || zTap > curZ * 1.04f + 10.0f) { continue; }
            const float fTap = rawDetectSceneFlameCore(Lit.SampleLevel(Lin, qUv, 0.0f).rgb);
            if (fTap > 0.01f) {
                const float score = fTap * (1.0f - float(ti) * 0.055f);
                if (score > bestFlame) {
                    bestFlame = score;
                    bestPx    = qPx;
                    bestD     = dTap;
                    bestZ     = zTap;
                }
            }
        }
    }

    // Immediate early-out on ~95% of pixels with zero verified real flame core below them!
    if (bestFlame <= 0.02f) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

    // Measure local flame footprint around verified anchor (auto-scales candle -> torch -> brazier -> hearth):
    const float footStepPx = clamp(180.0f / max(bestZ, 36.0f) * (dims.y * 0.009f), 3.0f, 18.0f);
    float footHits = 1.0f;
    static const float2 kFootDir[4] = { float2(1.0f, 0.0f), float2(-1.0f, 0.0f), float2(0.0f, 1.0f), float2(0.0f, -1.0f) };
    [unroll] for (int fi = 0; fi < 4; ++fi) {
        const float2 fUv = clamp((bestPx + kFootDir[fi] * footStepPx + 0.5f) * dims.zw, 0.001f.xx, 0.999f.xx);
        const float dF = Depth.SampleLevel(Lin, fUv, 0.0f);
        if (dF < 0.9990f && abs(viewZOf(dF) - bestZ) < max(bestZ * 0.16f, 28.0f)) {
            footHits += step(0.05f, rawDetectSceneFlameCore(Lit.SampleLevel(Lin, fUv, 0.0f).rgb));
        }
    }
    // sizeT: 0.0 = tiny wall candle, 0.35 = torch/sconce, 0.75..1.0 = brazier / campfire / Dragonsreach hearth
    const float sizeT = saturate((footHits - 1.0f) * 0.25f);
    const float rad = lerp(4.2f, 26.0f, sizeT);
    // Windy storms shear plumes lower & wider; still cold air lets plumes tower higher & denser:
    const float hPlume = lerp(22.0f, 152.0f, sizeT) * lerp(1.08f, 0.78f, eddyDisp);
    const float coldHang = isInt ? 1.0f : lerp(1.18f, 0.85f, saturate((seasonThermal0.w + 5.0f) / 22.0f));
    const float smokeScale = smoothstep(0.10f, 0.55f, sizeT) * lerp(0.88f, 1.0f, isInt ? 0.70f : 1.0f)
                           * coldHang * lerp(1.12f, 0.72f, eddyDisp);

    // Reconstruct exact 3D world-space flame base and intersect camera ray with the vertical plume slab:
    const float3 pBase = worldAt(bestPx, bestD);
    const float tMid = dot(pBase - eye, w);
    if (tMid < 14.0f || tMid > 1400.0f) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

    const float slabHalf = rad * lerp(1.75f, 2.25f, eddyDisp);
    const float tEnter = max(14.0f, tMid - slabHalf);
    const float tExit  = min(dist + 2.0f, tMid + slabHalf);
    if (tExit <= tEnter + 0.4f) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

    const float distFade = (1.0f - smoothstep(850.0f, 1400.0f, tMid)) * saturate(bestFlame * 1.05f);
    const int kSteps = 10;
    const float ds = (tExit - tEnter) * (1.0f / float(kSteps));
    const float riseSpeed = lerp(34.0f, 66.0f, sizeT);
    const float nFreq = 1.0f / max(rad * 0.90f, 3.8f);

    // Cool dark charcoal soot ambient illumination for upper billows:
    const float3 ambTop = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w),
                              isInt ? float3(0.014f, 0.014f, 0.018f) : float3(0.025f, 0.032f, 0.048f));
    const float3 sootAlbedo = float3(0.042f, 0.038f, 0.038f);

    float3 accEmit = 0.0f.xxx;
    float T = 1.0f;

    [unroll] for (int si = 0; si < kSteps; ++si) {
        const float t = tEnter + (float(si) + jit) * ds;
        const float3 pRel = eye + w * t;
        const float3 q = pRel - pBase;
        const float u = q.z / hPlume;
        if (u < -0.04f || u > 1.05f) { continue; }
        const float uCl = saturate(u);

        // 3-Octave curl-advected domain-warped FBM turbulence (coupled to live wind advection + calm mushroom eddies!):
        const float3 wpQ = camPosW.xyz + pRel;
        const float3 windAdv = float3(windTiltXY * (tClock * 1.35f), tClock * riseSpeed);
        const float3 nP1 = (wpQ - windAdv) * float3(nFreq, nFreq, nFreq * 0.70f);
        const float4 g1 = rawNoise3Grad(nP1);
        const float3 curl1 = float3(g1.z - g1.w, g1.w - g1.y, g1.y - g1.z);
        const float4 g2 = rawNoise3Grad(nP1 * 2.15f + curl1 * 0.45f + float3(5.3f, 11.7f, -tClock * 1.45f));
        const float4 g3 = rawNoise3Grad(nP1 * 4.35f - curl1 * 0.25f + float3(13.1f, -7.9f, -tClock * 2.55f));
        const float turb = saturate(g1.x * 0.50f + g2.x * 0.34f + g3.x * 0.16f);

        // Calm interiors / still air develop buoyant toroidal mushroom-cap curls near the plume crest:
        const float mushroomRoll = (1.0f - windNorm) * smoothstep(0.45f, 0.92f, uCl) * 0.42f;
        const float2 curlXY = curl1.xy * (0.58f + mushroomRoll) + float2(g2.z - g2.w, g2.w - g2.y) * 0.34f;
        const float2 qWarped = q.xy - windTiltXY * (uCl * uCl * 2.45f) + curlXY * (rad * lerp(0.18f, 1.15f, uCl));
        const float rXY = length(qWarped);

        // 1) Lower Hot Core: crisp turbulent Planck blackbody (1400K..3400K) licking flame tongues (u in 0..0.48):
        float flameDens = 0.0f;
        float3 flameCol = 0.0f.xxx;
        if (u < 0.52f) {
            const float uf = saturate(u / 0.48f);
            const float rFlame = max(rad * lerp(1.02f, 0.34f, uf) * smoothstep(-0.04f, 0.08f, u), 0.6f);
            const float rFNorm = rXY / rFlame;
            if (rFNorm < 1.25f) {
                const float fShape = saturate(1.0f - rFNorm * rFNorm) * smoothstep(1.05f, 0.10f, uf);
                flameDens = smoothstep(lerp(0.22f, 0.58f, uf), 0.90f, fShape + (turb - 0.42f) * lerp(0.55f, 1.05f, uf));
                if (flameDens > 0.01f) {
                    const float temp01 = saturate(flameDens * (1.12f - uf * 0.65f) + (1.0f - saturate(rFNorm)) * 0.28f * (1.0f - uf));
                    const float tempK = lerp(1400.0f, 3200.0f, temp01);
                    // Wave 79: Hue-preserving Rec.709/Rec.2020-safe Planck blackbody fire emission + Beer-Lambert smoke self-shadowing:
                    const RawBlackbodyFireSmokeResult bbW79 = rawEvalBoundedBlackbodyFireSmoke(tempK, 0.72f, (1.0f - T) * 0.85f, 0.36f);
                    flameCol = bbW79.fireEmission;
                }
            }
        }

        // 2) Upper Rising Plume: billowing dark charcoal soot smoke lit from WITHIN by burnt-orange fire scatter!
        float smokeDens = 0.0f;
        float3 smokeScatter = 0.0f.xxx;
        if (smokeScale > 0.02f && u > 0.10f) {
            const float spreadMult = lerp(2.25f, 3.35f, eddyDisp) + mushroomRoll * 0.65f;
            const float rSmoke = max(rad * lerp(0.82f, spreadMult, pow(uCl, 0.72f)), 1.5f);
            const float rSNorm = rXY / rSmoke;
            if (rSNorm < 1.22f) {
                const float billow = 1.0f - abs(g2.x * 2.0f - 1.0f);
                const float dispFade = exp(-uCl * lerp(0.25f, 1.35f, eddyDisp));
                const float sShape = saturate(1.0f - rSNorm * rSNorm) * smoothstep(0.10f, 0.30f, uCl) * smoothstep(1.04f, 0.52f, uCl);
                smokeDens = smoothstep(0.24f, 0.84f, sShape + (turb * 0.62f + billow * 0.38f - 0.39f) * 0.85f) * smokeScale * dispFade;
                if (smokeDens > 0.01f) {
                    // Multi-band internal fire-to-smoke radiance scatter (janvlietinck / benharkervfx / mytinodev):
                    // Rich incandescent burnt-orange illuminated undersides + deep crimson subsurface scatter
                    // transitioning sharply into cool self-shadowed dark charcoal billow tops!
                    const float fireIrradHot  = exp(-max(uCl - 0.14f, 0.0f) * 3.85f) * exp(-rSNorm * rSNorm * 1.35f);
                    const float fireIrradDeep = exp(-max(uCl - 0.14f, 0.0f) * 2.05f) * exp(-rSNorm * rSNorm * 1.10f);
                    const float downFace = saturate(-g1.w * 0.95f + 0.48f);
                    const float upFace   = saturate( g1.w * 0.72f + 0.52f);
                    const float selfAtten = exp(-smokeDens * 0.62f);
                    const RawBlackbodyFireSmokeResult smokeBbW79 = rawEvalBoundedBlackbodyFireSmoke(
                        lerp(1500.0f, 2400.0f, fireIrradHot), fireIrradHot * lerp(0.25f, 1.35f, downFace), smokeDens * 0.75f, 0.40f);
                    const float3 orangeUnder = smokeBbW79.smokeUnderScatter * (1.45f * selfAtten);
                    const float3 crimsonCore = float3(1.65f, 0.26f, 0.03f) * (fireIrradDeep * lerp(0.25f, 0.95f, downFace) * 0.28f);
                    const float3 ambCharcoal = sootAlbedo * ambTop * lerp(0.38f, 1.25f, upFace);
                    smokeScatter = ambCharcoal + (orangeUnder + crimsonCore) * (0.28f + 0.36f * (1.0f - smokeDens));
                }
            }
        }

        const float sigmaF = flameDens * (0.072f / max(rad * 0.08f, 0.45f));
        const float sigmaS = smokeDens * (0.044f / max(rad * 0.07f, 0.55f));
        const float sigmaTot = sigmaF + sigmaS;
        if (sigmaTot > 1.0e-4f) {
            const float stepT = exp(-sigmaTot * ds);
            const float3 stepRadiance = (flameCol * sigmaF + smokeScatter * sigmaS) / sigmaTot;
            accEmit += T * stepRadiance * (1.0f - stepT);
            T *= stepT;
            if (T < 0.06f) { break; }
        }
    }

    // 3) Wind-Stretched Rising World-Locked 3D Embers drifting along buoyant + downwind trajectories:
    if (smokeScale > 0.18f && tMid > 20.0f && tMid < dist && tMid < 1500.0f) {
        const float3 ptRay = eye + w * tMid;
        const float3 offL = ptRay - pBase;
        if (offL.z > hPlume * 0.12f && offL.z < hPlume * 1.35f) {
            const float hE = saturate((offL.z - hPlume * 0.12f) / (hPlume * 1.23f));
            const float maxER = rad * lerp(1.15f, 3.45f, hE);
            const float2 offWind = offL.xy - windTiltXY * (hE * hE * 2.45f);
            if (dot(offWind, offWind) < maxER * maxER) {
                const float3 wpE = camPosW.xyz + ptRay;
                const float2 swirlE = float2(sin(tClock * 2.4f + wpE.z * 0.075f), cos(tClock * 2.0f - wpE.z * 0.065f)) * 5.2f;
                const float3 eGrid = float3((wpE.xy - windTiltXY * (tClock * 1.45f) + swirlE) * 0.16f,
                                            wpE.z * 0.10f - tClock * 2.30f);
                const float3 eCell = floor(eGrid);
                const float3 eJitter = rawHash33(eCell);
                if (eJitter.x > 0.940f) {
                    const float3 relE = (frac(eGrid) - 0.5f) - (eJitter - 0.5f) * 0.55f;
                    // Stretch spark slightly along local buoyant + wind trajectory so windy embers read as glowing streaks:
                    const float3 sparkVel = normalize(float3(windTiltXY * 0.04f, 1.0f));
                    const float alongV = dot(relE, sparkVel);
                    const float3 perpV = relE - sparkVel * alongV;
                    const float streakScale = lerp(1.0f, 0.48f, windNorm);
                    const float dSpark2 = dot(perpV, perpV) + (alongV * streakScale) * (alongV * streakScale);
                    const float flicker = 0.65f + 0.35f * sin(tClock * 18.0f + eJitter.y * 42.0f);
                    const float spark = smoothstep(0.940f, 0.998f, eJitter.x) * exp(-dSpark2 * 64.0f)
                                      * (1.0f - hE) * smoothstep(0.0f, 0.10f, hE) * flicker;
                    const float3 sparkCol = lerp(float3(3.25f, 1.62f, 0.38f), float3(1.65f, 0.26f, 0.03f), hE);
                    accEmit += sparkCol * (spark * 1.15f);
                }
            }
        }
    }

    accEmit *= distFade * 0.62f;
    // Clamp transmittance floor so secondary LightFog smoke never creates dark soot blotches over walls or 3D fire:
    const float finalT = max(lerp(1.0f, T, distFade), isInt ? 0.84f : 0.76f);
    // Smooth HDR fire shoulder so flames stay luminous & vivid without nuclear whiteout:
    const float fPk = max(accEmit.r, max(accEmit.g, accEmit.b));
    if (fPk > 2.2f) { accEmit *= (2.2f + (fPk - 2.2f) / (1.0f + (fPk - 2.2f) * 0.55f)) / fPk; }
    return all(isfinite(accEmit)) ? float4(max(accEmit, 0.0f.xxx), saturate(finalT)) : float4(0.0f, 0.0f, 0.0f, 1.0f);
}

// Wave 42: Physical Underwater Volumetric Sun-Shafts, Pope & Fry Beer-Lambert Marine Absorption & Glacial Silt Motes.
// Activated strictly when camera is submerged ((flags.x & FLAG_UNDERWATER) != 0u):
//   - Pope & Fry Beer-Lambert absorption sigma_a = (0.42, 0.10, 0.055) (red dies fast, glacial teal mid-range, deep indigo at depth)
//   - 12 IGN-jittered ray steps marching refracted downward volumetric sun-shafts (animated surface wave caustics via rawNoise3Grad)
//   - Sparse gently drifting world-locked glacial silt / micro-plankton motes (rawHash33) forward-lit by Mie phase
float3 evalUnderwaterVolumetrics(float3 litCol, float3 eye, float3 w, float dist, float2 px, float liveWaterZ, bool isSkyRay) {
    // Pope & Fry pure/glacial marine spectral absorption (m^-1 scaled cleanly to Skyrim units: 145u ~ 1 optical meter):
    static const float3 kPopeFrySigmaA = float3(0.42f, 0.10f, 0.055f);
    const float invOptU = 1.0f / 145.0f;
    const float wtUw = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
    const float jit = ign(px + float2(13.0f, 37.0f));

    // Unified sun/moon & sky palette so underwater light responds cleanly to time of day & weather:
    float3 uSunUw, uSkyZUw, uSkyHUw, uAmbUpUw, uAmbDnUw;
    evalWorldAtmosphere(uSunUw, uSkyZUw, uSkyHUw, uAmbUpUw, uAmbDnUw);
    const float aElevUw = astroSunElev();
    const float nightUw = smoothstep(0.04f, -0.07f, aElevUw);
    const float dayUw   = smoothstep(-0.04f, 0.18f, aElevUw);
    const float coverUw = saturate(clouds.x);

    // Exact Snell-refracted light vector inside water (eta = n_air / n_water = 1 / 1.333 = 0.75):
    const float3 airLightDir = normalize(lerp(astroSunDir(), sunDir.xyz, nightUw));
    const float2 refrXY = airLightDir.xy * 0.75f;
    const float3 uwLightDir = normalize(float3(refrXY, sqrt(max(1.0f - dot(refrXY, refrXY), 0.04f))));
    const float cosUw = dot(w, uwLightDir);
    const float phUw = min(phaseHG(cosUw, 0.72f) * 2.4f + 0.18f, 2.5f);

    // Effective submerged ray length (for underwater sky rays outside Snell's window, march deep marine column):
    const float camSubDepth = max(liveWaterZ - (camPosW.z + eye.z), 0.0f);
    const float tSurface = (isSkyRay && w.z > 0.02f) ? (camSubDepth / max(w.z, 0.02f)) : dist;
    const float uwDist = isSkyRay ? min(tSurface, 2600.0f) : dist;
    const float pathM = uwDist * invOptU;
    const float3 T_uw = exp(-kPopeFrySigmaA * pathM);

    // Depth-stratified glacial marine ambient color (shallow emerald-teal -> deep arctic indigo):
    const float midSubM = max(liveWaterZ - (camPosW.z + eye.z + w.z * min(uwDist * 0.5f, 1200.0f)), 0.0f) * invOptU;
    const float3 depthAtten = exp(-kPopeFrySigmaA * (midSubM * 0.52f));
    const float lightEnergy = lerp(0.18f, 1.0f, dayUw) * (1.0f - coverUw * 0.35f);
    const float3 shallowTeal = float3(0.042f, 0.195f, 0.245f) * (0.45f + 0.55f * saturate(dot(uSkyHUw + uSunUw, float3(0.25f, 0.50f, 0.25f))));
    const float3 deepIndigo  = float3(0.006f, 0.028f, 0.068f);
    const float3 marineAmb   = lerp(deepIndigo, shallowTeal, saturate(depthAtten.g)) * lightEnergy;

    // Snell's Window modulation on sky rays looking up from below the water surface (critical angle 48.6 deg -> cos = 0.661):
    float3 baseUw = litCol * T_uw + marineAmb * (1.0f.xxx - T_uw);
    if (isSkyRay) {
        const float snellWin = smoothstep(0.60f, 0.70f, w.z);
        const float3 tirWater = marineAmb * (0.75f + 0.25f * saturate(w.z + 0.4f));
        baseUw = lerp(tirWater, litCol * T_uw + marineAmb * (1.0f.xxx - T_uw), snellWin);
    }

    // 12 IGN-jittered underwater ray steps: refracted volumetric sun-shafts + suspended glacial silt / plankton motes!
    const float tMarchMax = min(uwDist, 2200.0f);
    const float ds = tMarchMax * (1.0f / 12.0f);
    const float3 shaftCol = lerp(uSunUw * 1.15f + uSkyHUw * 0.35f, float3(0.16f, 0.32f, 0.58f), nightUw)
                          * (1.0f - coverUw * 0.62f) * (sunColour.w > 0.5f ? 1.0f : 0.22f);

    float3 shaftAcc = 0.0f.xxx;
    float3 moteAcc  = 0.0f.xxx;

    if (tMarchMax > 8.0f) {
        [unroll] for (int si = 0; si < 12; ++si) {
            const float t = (float(si) + jit) * ds;
            const float3 wp = camPosW.xyz + eye + w * t;
            const float subZ = max(liveWaterZ - wp.z, 0.0f);
            // Project along refracted underwater sun vector up to the water surface to sample wave-focused caustics:
            const float2 surfXY = wp.xy + uwLightDir.xy * (subZ / max(uwLightDir.z, 0.22f));
            const float2 cUv = surfXY * 0.0115f + float2(wtUw * 0.26f, -wtUw * 0.18f);
            const float4 cg1 = rawNoise3Grad(float3(cUv, wtUw * 0.32f));
            const float4 cg2 = rawNoise3Grad(float3(cUv * 1.90f + cg1.yz * 0.34f, -wtUw * 0.40f + 5.7f));
            const float cFocus = pow(saturate(1.0f - abs(cg1.x * 2.0f - 1.0f) * 1.85f), 2.3f) * 0.60f
                               + pow(saturate(1.0f - abs(cg2.x * 2.0f - 1.0f) * 2.05f), 2.7f) * 0.40f;

            // Pope & Fry spectral transmittance along both downward light path and camera view path:
            const float downPathM = (subZ / max(uwLightDir.z, 0.22f)) * invOptU;
            const float3 T_down = exp(-kPopeFrySigmaA * downPathM);
            const float3 T_view = exp(-kPopeFrySigmaA * (t * invOptU));
            const float shaftStep = (0.14f + 0.86f * cFocus) * (ds * (1.0f / 1400.0f));
            shaftAcc += shaftCol * (T_down * T_view * phUw * shaftStep);

            // Calm, sparse world-locked suspended glacial silt / micro-plankton motes lit forward by Mie phase:
            if (t > 24.0f && t < 900.0f) {
                const float3 driftM = float3(sin(wtUw * 0.42f + wp.z * 0.018f) * 3.2f,
                                             cos(wtUw * 0.36f - wp.x * 0.018f) * 3.2f,
                                             sin(wtUw * 0.24f + wp.y * 0.014f) * 2.0f);
                const float3 mGrid = (wp + driftM) * 0.145f;
                const float3 mCell = floor(mGrid);
                const float3 mRnd  = rawHash33(mCell);
                if (mRnd.x > 0.940f) {
                    const float dM = length((frac(mGrid) - 0.5f) - (mRnd - 0.5f) * 0.58f);
                    const float mote = smoothstep(0.940f, 0.998f, mRnd.x) * exp(-dM * dM * 58.0f)
                                     * smoothstep(24.0f, 95.0f, t) * (1.0f - smoothstep(480.0f, 900.0f, t));
                    const float3 moteLit = (shaftCol * (0.30f + 0.70f * cFocus) * phUw + marineAmb * 1.45f) * T_down * T_view;
                    moteAcc += moteLit * (mote * 0.38f);
                }
            }
        }
    }

    // Subtle near-surface rising aerated micro-bubbles coupled to planetary gravity:
    const float gNorm = clamp(physWorld[0].y / 9.80665f, 0.2f, 2.5f);
    float3 bubAdd = 0.0f.xxx;
    [unroll] for (int bl = 0; bl < 4; ++bl) {
        const float bDist = 42.0f + float(bl) * 58.0f;
        if (uwDist <= bDist) { continue; }
        const float3 wpB = camPosW.xyz + eye + w * bDist;
        const float subDepth = max(liveWaterZ - wpB.z, 0.0f);
        const float surfZone = exp(-subDepth / 240.0f);
        const float bScale = lerp(0.24f, 0.12f, float(bl) / 3.0f);
        const float3 bCoord = float3((wpB.xy + float2(sin(wtUw * 1.8f + wpB.z * 0.05f), cos(wtUw * 1.5f - wpB.z * 0.04f)) * 4.5f) * bScale,
                                     wpB.z * bScale - wtUw * (3.2f * gNorm));
        const float bCell = cHash(floor(bCoord));
        if (bCell > 0.952f) {
            const float dB = length(frac(bCoord) - 0.5f);
            const float bSp = smoothstep(0.952f, 0.998f, bCell) * exp(-dB * dB * 38.0f) * surfZone;
            bubAdd += float3(0.45f, 0.74f, 0.80f) * (bSp * 0.30f / (1.0f + float(bl) * 0.35f));
        }
    }

    return max(baseUw + min(shaftAcc, 0.85f.xxx) + moteAcc + bubAdd, 0.0f.xxx);
}

// Wave 45: Sub-Zero Thermal Steam Wisps ("Arctic Sea Smoke" over cold open water + Freezing Hearth Condensation Rim).
// Strictly gated on freezing exterior air (seasonThermal0.w < -1.0C, !interior, !underwater, !celestial, d < 0.99995)
// and strictly anchored to verified water-plane surface depth (`liveWaterZ`) or verified scene-anchored flame plumes
// (`fireVol`) - ZERO standalone floating spheres, ZERO activation in warm weather!
// Returns float4(steamEmitRGB, steamTransmittance).
float4 evalSubZeroThermalSteam(
    float3 eye, float3 w, float dist, float d, float3 pAbs,
    float2 px, float liveWaterZ, float3 sunCol, float3 skyCol, float3 sunDirW,
    float4 fireVol)
{
    const float airTempC = seasonThermal0.w;
    if (d >= 0.99995f || airTempC >= -1.0f || dist < 18.0f || dist > 2600.0f) {
        return float4(0.0f, 0.0f, 0.0f, 1.0f);
    }

    const float freeze01     = saturate((-airTempC - 1.0f) / 11.0f);
    const float seaSmokeCold = saturate((-airTempC - 1.5f) / 10.5f);
    const float distFade     = 1.0f - smoothstep(1400.0f, 2600.0f, dist);
    const float tClock       = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
    const float2 windSteam   = clamp(cloudWind.xy * 0.065f, -26.0f.xx, 26.0f.xx);
    const float jit          = ign(px + float2(41.0f, 11.0f));

    // Soft Mie-scattered cool water-vapor condensation radiance lit by live sun/moon & sky:
    const float phSteam = min(phaseHG(dot(w, sunDirW), 0.55f) * 1.65f + 0.30f, 1.85f);
    const float3 steamLit = (sunCol * (phSteam * 0.42f) + skyCol * 0.72f + float3(0.015f, 0.020f, 0.028f))
                          * float3(0.92f, 0.96f, 1.02f);

    float3 accEmit = 0.0f.xxx;
    float T = 1.0f;

    // 1) Cold-Air River / Water Surface Steam ("Arctic Sea Smoke"):
    // Anchored strictly to verified pixels right along `liveWaterZ` (|pAbs.z - liveWaterZ| < 48u) over open water:
    const float waterProx = 1.0f - smoothstep(26.0f, 48.0f, abs(pAbs.z - liveWaterZ));
    // Arctic sea smoke rises from open/partially-open unfrozen rivers & leads, tapering off on solid frozen sheets:
    const float openWater = 1.0f - smoothstep(0.18f, 0.48f, seasonThermal1.x);
    const float waterGate = seaSmokeCold * waterProx * openWater * distFade;
    if (waterGate > 0.01f && dist < 2200.0f) {
        const float slabLen = min(max(dist - 16.0f, 0.0f), 150.0f);
        const float tStart  = dist - slabLen;
        const float ds      = slabLen * (1.0f / 6.0f);
        const float3 advW   = float3(windSteam * tClock, tClock * 15.5f);
        [unroll] for (int si = 0; si < 6; ++si) {
            const float t = tStart + (float(si) + jit) * ds;
            const float3 wp = camPosW.xyz + eye + w * t;
            const float hW = wp.z - liveWaterZ;
            if (hW < -4.0f || hW > 56.0f) { continue; }
            const float hEnv = smoothstep(-4.0f, 8.0f, hW) * (1.0f - smoothstep(20.0f, 56.0f, hW));
            const float3 sP1 = (wp - advW) * float3(0.018f, 0.018f, 0.026f);
            const float4 sg1 = rawNoise3Grad(sP1);
            const float3 sCurl = float3(sg1.z - sg1.w, sg1.w - sg1.y, sg1.y - sg1.z);
            const float4 sg2 = rawNoise3Grad(sP1 * 2.35f + sCurl * 0.42f + float3(6.1f, -9.4f, -tClock * 0.55f));
            const float wisp = smoothstep(0.40f, 0.86f, sg1.x * 0.62f + sg2.x * 0.38f) * hEnv;
            const float sigmaW = wisp * waterGate * 0.0044f;
            if (sigmaW > 1.0e-5f) {
                const float stepT = exp(-sigmaW * ds);
                // Subtle sunlit upper curl shading from analytical vertical gradient sg1.w:
                const float curlLit = lerp(0.74f, 1.16f, saturate(sg1.w * 0.65f + 0.55f));
                accEmit += T * (steamLit * curlLit) * (1.0f - stepT);
                T *= stepT;
            }
        }
    }

    // 2) Freezing Exterior Forge / Hearth Condensation Steam Rim:
    // Strictly coupled to verified real scene-anchored fire plumes (`fireVol`) - NEVER spawns without a real fire!
    const float firePlumePresence = saturate((1.0f - fireVol.a) * 2.2f + max(fireVol.r, fireVol.g) * 0.45f);
    if (firePlumePresence > 0.02f && freeze01 > 0.01f) {
        const float outerHalo = smoothstep(0.02f, 0.28f, firePlumePresence)
                              * (1.0f - smoothstep(0.65f, 1.45f, max(fireVol.r, fireVol.g)));
        const float3 advH = float3(windSteam * (tClock * 1.15f), tClock * 26.0f);
        const float3 hP = (pAbs - advH) * float3(0.024f, 0.024f, 0.020f);
        const float4 hg1 = rawNoise3Grad(hP);
        const float4 hg2 = rawNoise3Grad(hP * 2.25f + hg1.yzw * 0.35f + float3(11.3f, 4.7f, -tClock * 0.70f));
        const float steamWisp = smoothstep(0.36f, 0.84f, hg1.x * 0.60f + hg2.x * 0.40f);
        const float snowBoost = 1.0f + saturate(weather.z + seasonThermal0.x) * 0.45f;
        const float rimAlpha = saturate(outerHalo * steamWisp * freeze01 * snowBoost * distFade * 0.24f);
        if (rimAlpha > 0.002f) {
            const float3 warmKiss = lerp(steamLit, float3(1.12f, 0.68f, 0.38f) * 0.42f + steamLit * 0.65f, saturate(fireVol.r * 0.55f));
            accEmit += T * warmKiss * rimAlpha;
            T *= (1.0f - rimAlpha * 0.65f);
        }
    }

    return float4(min(accEmit, 0.65f.xxx), clamp(T, 0.70f, 1.0f));
}

// Wave 48: Local-Light Illuminated 3-Layer Depth-Sliced Parallax Precipitation Volume (3m / 7m / 14m)
// + Scene-Depth-Verified Near-Emitter Fire-Ember Sparks (< 2.5m).
// Outdoor only (sunColour.w > 0.5), strictly depth-occluded against SceneDepth (sliceDist < sceneViewDist),
// pause-aware (simTime), with Henyey-Greenstein (g = 0.65) forward Mie scatter on rain streaks & torch-lit snow!
float3 evalVolumetricParallaxPrecip(
    float3 eye, float3 w, float sceneViewDist, float sceneD, float3 atmoCol,
    float3 sunDirW, float nightF, float ashW, bool allowEmbers)
{
    if (sunColour.w <= 0.5f) { return 0.0f.xxx; }
    const bool hasRain = (weather.w > 0.05f);
    const bool hasSnow = (weather.z > 0.05f);
    const bool hasAsh  = (ashW > 0.10f);
    const float simTime = (physWorld[0].x > 1.0e-4f) ? physWorld[0].x : camPosW.w;
    const float beauP = clamp(physWorld[1].x, 0.5f, 12.0f);
    const float2 windP = cloudWind.xy * (0.0032f + saturate((beauP - 2.0f) / 8.0f) * 0.0018f);
    const uint nLocalLights = min(flags.y, 16u);
    const float3 moonSkyCool = lerp(max(atmoCol * 1.15f + 0.08f, 0.18f.xxx),
                                    float3(0.24f, 0.34f, 0.52f) * max(dot(atmoCol, float3(0.2126f, 0.7152f, 0.0722f)) * 2.5f + 0.45f, 0.35f),
                                    nightF);
    const float sunMieRain = min(phaseHG(dot(w, sunDirW), 0.65f) * 2.6f + 0.20f, 3.0f) * (1.0f - saturate(clouds.x * 0.70f));

    float3 outPrecip = 0.0f.xxx;

    // 1) 3-Layer Depth-Sliced Parallax Precipitation Volume at exact 3m (210u), 7m (490u), 14m (980u) camera slices:
    // Strictly depth-tested against SceneDepth so streaks NEVER penetrate roofs, overhangs, or walls!
    if (hasRain || hasSnow || hasAsh) {
        static const float kSliceDistU[3] = { 210.0f, 490.0f, 980.0f };
        static const float kRainScale[3]  = { 0.30f,  0.22f,  0.15f  };
        static const float kSnowScale[3]  = { 0.115f, 0.078f, 0.048f };
        static const float kSliceGain[3]  = { 1.00f,  0.74f,  0.50f  };

        [unroll] for (int sl = 0; sl < 3; ++sl) {
            const float sliceDist = kSliceDistU[sl];
            if (sceneViewDist <= sliceDist + 4.0f) { continue; }
            // Smooth depth-occlusion fade right before solid geometry (roofs/walls/pillars):
            const float occFade = (sceneD < 0.99998f) ? smoothstep(4.0f, 44.0f, sceneViewDist - sliceDist) : 1.0f;
            const float3 pRelS = eye + w * sliceDist;
            const float3 wpS = camPosW.xyz + pRelS;

            // Evaluate live local torch/campfire/lantern irradiance with Henyey-Greenstein g = 0.65 forward Mie scatter:
            float3 localSnowLit = 0.0f.xxx;
            float3 localRainMie = 0.0f.xxx;
            [loop] for (uint li = 0u; li < nLocalLights; ++li) {
                const float3 toL = lightPosR[li].xyz - pRelS;
                const float rL = max(lightPosR[li].w * 1.25f, 1.0f);
                const float d2 = dot(toL, toL);
                if (d2 < rL * rL) {
                    const float dLen = sqrt(max(d2, 1.0f));
                    const float att = saturate(1.0f - dLen / rL);
                    const float att2 = att * att;
                    const float3 lDir = toL / dLen;
                    const float hgRain = min(phaseHG(dot(w, lDir), 0.65f) * 3.8f + 0.22f, 3.6f);
                    const float hgSnow = min(phaseHG(dot(w, lDir), 0.38f) * 1.9f + 0.55f, 2.2f);
                    localSnowLit += lightColS[li].rgb * (att2 * hgSnow * 1.65f);
                    localRainMie += lightColS[li].rgb * (att2 * hgRain * 1.25f);
                }
            }

            if (hasRain) {
                const float rainAmt = smoothstep(0.05f, 0.75f, weather.w);
                const float gust = 1.0f + 0.48f * sin(simTime * 1.55f + wpS.x * 0.0024f)
                                        + 0.22f * cos(simTime * 2.70f - wpS.y * 0.0018f);
                // Wind-slanted downward-falling specular rain streak coordinates (pause-aware simTime):
                const float3 rCoord = float3(
                    (wpS.xy - windP * (wpS.z * 0.28f * gust)) * kRainScale[sl],
                    wpS.z * 0.0072f + simTime * (22.0f + float(sl) * 6.5f));
                const float rCell = cHash(floor(rCoord));
                const float rGate = lerp(0.946f, 0.908f, rainAmt);
                if (rCell > rGate) {
                    const float2 rF = frac(rCoord.xy) - 0.5f;
                    const float rThin = exp(-rF.x * rF.x * 240.0f) * exp(-rF.y * rF.y * 10.0f);
                    const float rMask = smoothstep(rGate, 0.998f, rCell) * rThin;
                    const float3 rLit = max(atmoCol * (0.90f + 0.25f * sunMieRain) + 0.05f, 0.10f.xxx)
                                      + sunColour.rgb * (sunMieRain * 0.22f * (1.0f - nightF))
                                      + localRainMie;
                    outPrecip += rLit * (rMask * rainAmt * kSliceGain[sl] * occFade * 0.28f);
                }
                // Sweeping storm wind-squall mist veil on mid/far slices during heavy downpours:
                if (sl >= 1 && weather.w > 0.35f) {
                    const float sqN = cNoise(float3((wpS.xy - windP * (simTime * 46.0f)) * 0.0018f, wpS.z * 0.0012f));
                    const float sqSheet = smoothstep(0.54f, 0.88f, sqN) * saturate((weather.w - 0.35f) * 1.8f);
                    outPrecip += (atmoCol * 0.70f + localRainMie * 0.28f) * (sqSheet * kSliceGain[sl] * occFade * 0.14f);
                }
            }

            if (hasSnow) {
                const float snowAmt = smoothstep(0.05f, 0.72f, weather.z);
                const float swirlPh = simTime * (0.48f + float(sl) * 0.14f);
                const float swayX = sin(swirlPh + wpS.z * 0.015f + wpS.y * 0.005f) * 22.0f;
                const float swayY = cos(swirlPh * 0.82f + wpS.x * 0.013f - wpS.z * 0.008f) * 22.0f;
                const float fSc = kSnowScale[sl];
                const float3 sCoord = float3(
                    (wpS.xy - windP * (wpS.z * 0.18f + simTime * 14.0f) + float2(swayX, swayY)) * fSc,
                    wpS.z * fSc + simTime * (1.35f + float(sl) * 0.34f));
                const float sCell = cHash(floor(sCoord));
                const float sGate = lerp(0.942f, 0.918f, snowAmt);
                if (sCell > sGate) {
                    const float dFlake = length(frac(sCoord) - 0.5f);
                    const float flakeSharp = (sl == 0) ? 10.5f : ((sl == 1) ? 16.5f : 23.0f);
                    const float sMask = smoothstep(sGate, 0.998f, sCell) * exp(-dFlake * dFlake * flakeSharp);
                    // Catch warm orange glow from nearby torches/campfires + cool blue-silver from moon/sky:
                    const float3 sLit = moonSkyCool + localSnowLit * float3(1.12f, 0.96f, 0.82f);
                    outPrecip += sLit * (sMask * snowAmt * kSliceGain[sl] * occFade * 0.48f);
                }
            }

            if (hasAsh) {
                const float aTurb = sin(simTime * 1.6f + wpS.z * 0.02f) * 18.0f;
                const float3 aCoord = float3(
                    (wpS.xy - windP * (simTime * 34.0f) + float2(aTurb, -aTurb * 0.7f)) * lerp(0.13f, 0.075f, float(sl) * 0.5f),
                    wpS.z * 0.10f + simTime * (2.2f + float(sl) * 0.65f));
                const float aCell = cHash(floor(aCoord));
                if (aCell > 0.928f) {
                    const float aMask = smoothstep(0.928f, 0.996f, aCell) * saturate(1.0f - length(frac(aCoord) - 0.5f) * 2.4f);
                    const bool isEmberA = aCell > 0.985f;
                    const float3 aCol = isEmberA ? float3(3.20f, 0.78f, 0.16f) : (float3(0.32f, 0.25f, 0.20f) + localSnowLit * 0.55f);
                    outPrecip += aCol * (aMask * ashW * kSliceGain[sl] * occFade * (isEmberA ? 1.15f : 0.48f));
                }
            }
        }
    }

    // 2) Subtle Upward-Drifting Fire-Ember Sparks strictly anchored within < 2.5m (175u) of verified real hot
    //    firelight emitters (scene-depth verified so embers NEVER spawn behind walls or as floating orbs!):
    if (allowEmbers && sceneD < 0.99998f && nLocalLights > 0u) {
        const float2 emberWindXY = clamp(cloudWind.xy * 0.045f, -18.0f.xx, 18.0f.xx);
        const int2 dimMaxE = max(int2(dims.xy) - int2(1, 1), int2(0, 0));
        [loop] for (uint ei = 0u; ei < nLocalLights; ++ei) {
            const float3 lc = lightColS[ei].rgb;
            // Strict hot-fire chromaticity gate (rejects cool/white ambient fill lights & spell lights):
            if (lc.r < 0.42f || lc.r < lc.g * 1.20f || lc.r < lc.b * 2.10f) { continue; }
            const float3 lPos = lightPosR[ei].xyz;
            const float tClose = dot(lPos - eye, w);
            if (tClose < 22.0f || tClose > min(sceneViewDist - 6.0f, 1650.0f)) { continue; }
            const float3 pClose = eye + w * tClose;
            const float3 dRel = pClose - lPos;
            // Strict < 2.5m (175u) radial + upward drift envelope above emitter (never below ground!):
            if (dRel.z < -8.0f || dRel.z > 175.0f || dot(dRel.xy, dRel.xy) > (175.0f * 175.0f)) { continue; }
            // Verify emitter is NOT occluded behind a wall/roof via standard-Z SceneDepth check:
            const float4 sEm = screenOf(lPos);
            if (sEm.w > 1.0f) {
                const int2 emPx = clamp(int2(sEm.xy), int2(0, 0), dimMaxE);
                const float dEm = Depth.Load(int3(emPx, 0));
                if (dEm < 0.99998f && viewZOf(dEm) < sEm.w * 0.82f - 28.0f) { continue; }
            }
            const float hFrac = saturate((dRel.z + 8.0f) / 183.0f);
            const float radMax = lerp(42.0f, 155.0f, hFrac);
            const float2 driftXY = dRel.xy - emberWindXY * (hFrac * hFrac * 2.2f);
            const float rNorm = length(driftXY) / radMax;
            if (rNorm >= 1.0f) { continue; }
            const float3 wpEm = camPosW.xyz + pClose;
            const float2 swE = float2(sin(simTime * 2.1f + wpEm.z * 0.06f), cos(simTime * 1.8f - wpEm.z * 0.05f)) * 4.2f;
            const float3 eGrid = float3((wpEm.xy - emberWindXY * (simTime * 1.25f) + swE) * 0.165f,
                                        wpEm.z * 0.105f - simTime * 2.45f);
            const float3 eCell = floor(eGrid);
            const float3 eRnd = rawHash33(eCell + float3(float(ei) * 7.3f, 13.1f, 3.7f));
            if (eRnd.x > 0.955f) {
                const float3 eRel = (frac(eGrid) - 0.5f) - (eRnd - 0.5f) * 0.56f;
                const float dE2 = dot(eRel, eRel);
                const float flkE = 0.65f + 0.35f * sin(simTime * 16.0f + eRnd.y * 42.0f);
                const float spE = smoothstep(0.955f, 0.998f, eRnd.x) * exp(-dE2 * 76.0f)
                                * (1.0f - rNorm * rNorm) * (1.0f - hFrac) * smoothstep(0.0f, 0.12f, hFrac) * flkE
                                * (1.0f - smoothstep(900.0f, 1650.0f, tClose));
                const float3 emCol = lerp(float3(3.10f, 1.48f, 0.32f), float3(1.55f, 0.24f, 0.03f), hFrac);
                outPrecip += emCol * (spE * 0.82f);
            }
        }
    }

    return min(outPrecip, 2.4f.xxx);
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
    // Wave 64 Debug View 36 (flags.w == 36u): Froxel + Volumetric Fog + Fire-Plume Diagnostic!
    // R = fire plume emission / opacity (+ subtle Lighting.hlsl thermal pass-through),
    // G = 1 - froxelTransmittance (3D participating media optical density),
    // B = volumetric shaft energy (1/2-res bilateral upsampled god-rays & shafts).
    if (flags.w == 36u) {
        const float2 uv36 = (px + 0.5f) * dims.zw;
        const float4 froxRaw36 = sampleFroxel3D(uv36, min(curZ, kFroxFar));
        const float froxT36 = isfinite(froxRaw36.a) ? saturate(froxRaw36.a) : 1.0f;
        const float2 volPx36 = (px + 0.5f) * 0.5f;
        const float2 volDim36 = max(floor((dims.xy + 1.0f) * 0.5f), float2(1.0f, 1.0f));
        const int2 vBase36 = clamp(int2(floor(volPx36 - 0.5f)), int2(0, 0), int2(volDim36) - 1);
        const int2 vMax36 = int2(volDim36) - 1;
        const float zPix36 = min(curZ, 65000.0f);
        float3 vSum36 = 0.0f.xxx;
        float vW36 = 0.0f;
        [unroll] for (int dy36 = 0; dy36 <= 1; dy36++) {
            [unroll] for (int dx36 = 0; dx36 <= 1; dx36++) {
                const int2 q36 = clamp(vBase36 + int2(dx36, dy36), int2(0, 0), vMax36);
                const float4 tap36 = VolFog.Load(int3(q36, 0));
                if (all(isfinite(tap36))) {
                    const float relZ36 = abs(tap36.a - zPix36) / max(zPix36, 1.0f);
                    const float wz36 = exp(-min(relZ36 * 18.0f, 32.0f));
                    vSum36 += max(tap36.rgb, 0.0f.xxx) * wz36;
                    vW36 += wz36;
                }
            }
        }
        const float3 vol36 = (vW36 > 1.0e-4f) ? (vSum36 / vW36) : max(VolFog.SampleLevel(Lin, uv36, 0.0f).rgb, 0.0f.xxx);
        float4 fireVol36 = float4(0.0f, 0.0f, 0.0f, 1.0f);
        if (d < 1.0f) {
            const float3 eye36 = eyeAt();
            const float3 p36 = worldAt(px, min(d, 0.999999f));
            const float3 toP36 = p36 - eye36;
            const float dist36 = length(toP36);
            const float3 w36 = (isfinite(dist36) && dist36 > 1.0e-4f) ? (toP36 / dist36) : float3(0.0f, 1.0f, 0.0f);
            fireVol36 = evalVolumetricFirePlumes(eye36, w36, dist36, curZ, px, sunColour.w < 0.5f);
        }
        const float fireEmit36 = dot(max(fireVol36.rgb, 0.0f.xxx), float3(0.55f, 0.35f, 0.10f));
        const float litHeat36 = isfinite(lit.r) ? saturate(lit.r) : 0.0f;
        const float diagR = saturate(max(fireEmit36 * 1.40f, (1.0f - saturate(fireVol36.a)) * 2.20f) + litHeat36 * 0.22f);
        const float diagG = saturate(1.0f - froxT36);
        const float diagB = saturate(dot(vol36, float3(0.2126f, 0.7152f, 0.0722f)) * 3.50f);
        const float3 diag36 = float3(diagR, diagG, diagB);
        return float4(all(isfinite(diag36)) ? diag36 : 0.0f.xxx, 1.0f);
    }
    // Wave 65 Bit-Exact Debug-Layer Passthrough: skip lens droplets, atmospheric fog, rain mist, and sky replacement
    // for all Lighting Debug Views 1..35 (normals, albedo, sun_shadow, classes, moss, skin, NaN heatmap, flora, MVs)!
    if (flags.w != 0u) { return lit; }
    // Pre-Pass Weather Optics: procedural HDR rain-on-lens droplets & blizzard crystalline frost edges
    // (3-pass Near/Far Physical Bokeh DoF runs in dedicated DofPass.hlsl right after LightFog).
    if (dofParams.w > 0.5f && (weather.w >= 0.12f || weather.z >= 0.25f)) {
        lit.rgb = elderWeatherLensPrepass(Lit, px, dims.xy, dims.zw, weather.w, weather.z, camPosW.w);
    }
    // Wave 63 Celestial & Vanilla Sky-Card Separation:
    // Separate true night/twilight Masser & Secunda moons (aElev < 0.06, curZ > 110k, celWorldZ > 28.5k) from
    // daytime vanilla Skyrim sky-dome / beige cloud-polygon cards (which MUST be 100% replaced by RAW's physical
    // Hillaire sky + 3D Clouds, never preserved via moonKeep!). Distant mountains stay in the terrestrial fog pass.
    const float3 eyeCel = eyeAt();
    const float3 celRayRaw = worldAt(px, d < 1.0f ? min(d, 0.9995f) : 0.995f) - eyeCel;
    const float celRayLen2 = dot(celRayRaw, celRayRaw);
    const float3 sDirCel = (isfinite(celRayLen2) && celRayLen2 > 1.0e-8f) ? (celRayRaw * rsqrt(celRayLen2)) : float3(0.0f, 0.0f, 1.0f);
    const float celWorldZ = camPosW.z + sDirCel.z * curZ;
    const float aElevPre = astroSunElev();
    const bool isNightOrTwilPre = (aElevPre < 0.06f);
    const bool isMoonGeom = isNightOrTwilPre && (d < 1.0f) && (curZ > 110000.0f) && (celWorldZ > 28500.0f) && (sDirCel.z > 0.05f);
    const bool isSkyCard = (d < 1.0f) && (
        (curZ > 165000.0f && sDirCel.z > 0.03f) ||
        (d > 0.99965f && curZ > 95000.0f && sDirCel.z > 0.06f && celWorldZ > 24000.0f));
    const bool isCelestial = isMoonGeom;
    if (d >= 1.0f || isMoonGeom || isSkyCard) {
        // Unified Hillaire SIGGRAPH 2020 Physical HDR Sky + Solar Disc + Masser/Secunda Celestial Pass + Wrenninge 4-Octave HDR Cloud Composite:
        // Routing celestial sky-dome geometry (curZ > 110k: Masser & Secunda) and vanilla cloud cards here replaces
        // flat beige vanilla polygons with RAW Hillaire sky + 3D volumetric clouds while preserving night moons cleanly!
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
            // Preserve ONLY extreme exotic DLC skies (Apocrypha neon-emerald, Soul Cairn deep-violet) so Nordic
            // sunset/cloud hues never accidentally bypass RAW's Hillaire sky & 3D volumetric clouds:
            const float exoticApoc = smoothstep(0.18f, 0.36f, lit.g - max(lit.r, lit.b) * 1.22f);
            const float exoticCairn = smoothstep(0.20f, 0.38f, min(lit.r, lit.b) - lit.g * 1.32f);
            const float isExotic = saturate(max(exoticApoc, exoticCairn));
            const float cosS = saturate(dot(sDir, aSun));
            const float sunAng = acos(clamp(cosS, -1.0f, 1.0f));
            const float lowS = saturate(1.0f - max(aElev, 0.0f) * 3.2f);
            // Gate Masser & Secunda crater preservation strictly to night & civil twilight (aElev < 0.06) so
            // daytime vanilla Skyrim beige cloud polygons and sky-dome strips NEVER leak through moonKeep!
            const float moonElevGate = 1.0f - smoothstep(-0.04f, 0.06f, aElev);
            const float moonLumF = dot(lit.rgb, float3(0.2126f, 0.7152f, 0.0722f));
            const float masserWarm = saturate((lit.r - lit.b) * 6.5f) * smoothstep(0.015f, 0.06f, moonLumF);
            const float moonMask = max(smoothstep(0.035f, 0.18f, moonLumF), masserWarm);
            const float moonKeep = moonElevGate * (isMoonGeom ? 1.0f : (moonMask * (1.0f - smoothstep(0.985f, 0.996f, cosS))));
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
                // Infinite-Order Multi-Scattering + Planetary Earth Shadow) - ZERO hardcoded fake 3-stop twilight gradients!
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
            // Infinite Distant Horizon Ocean Plane (Wave 35: anchored to live engine tidal water height via rawLiveWaterZ
            // instead of waterA.w glint strength, with Beaufort-coupled multi-swell normals & continuous aerial horizon melt!):
            if (sDir.z < 0.0f) {
                const float liveSeaZ = rawLiveWaterZ(engineWorld, physWorld[0]);
                const float seaElev = max((eyeAt().z + camPosW.z) - liveSeaZ, 120.0f);
                const float tSea = min(seaElev / max(-sDir.z, 0.002f), 260000.0f);
                const float wtSea = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
                const float beauSea = clamp(physWorld[1].x, 0.5f, 12.0f);
                const float2 swellD = length(physWorld[1].yz) > 0.1f ? normalize(physWorld[1].yz) : normalize(cloudWind.xy + float2(1.0f, 0.35f));
                const float swellAmp = lerp(0.025f, 0.085f, saturate((beauSea - 1.0f) / 8.0f));
                const float2 seaP = (camPosW.xy + sDir.xy * tSea) * 0.00045f;
                const float4 g1 = rawNoise3Grad(float3(seaP * 2.2f - swellD * wtSea * 0.045f, wtSea * 0.12f));
                const float4 g2 = rawNoise3Grad(float3(seaP * 5.6f + float2(-swellD.y, swellD.x) * wtSea * 0.065f, wtSea * 0.21f));
                const float3 seaN = normalize(float3(-(g1.y * 0.65f + g2.y * 0.35f) * swellAmp,
                                                     -(g1.z * 0.65f + g2.z * 0.35f) * swellAmp, 1.0f));
                const float ndv = saturate(dot(seaN, -sDir));
                const float fres = 0.02f + 0.98f * pow(1.0f - ndv, 5.0f);
                const float3 reflDir = reflect(sDir, seaN);
                float3 ocZ, ocM, ocH;
                skyPalette(ocZ, ocM, ocH);
                const float3 seaSky = lerp(ocH, ocM, smoothstep(0.0f, 0.35f, max(reflDir.z, 0.0f)));
                const float3 deepSea = lerp(float3(0.008f, 0.024f, 0.044f), float3(0.014f, 0.052f, 0.078f), dayW) * (0.28f + 0.72f * dayW);
                const float3 glDir = normalize(lerp(aSun, sunDir.xyz, smoothstep(0.04f, -0.06f, aElev)));
                const float3 glH = normalize(glDir - sDir);
                const float glint = pow(saturate(dot(seaN, glH)), 140.0f) * (1.0f - cover * 0.75f);
                const float3 glintCol = lerp(float3(1.15f, 0.88f, 0.62f), float3(0.45f, 0.62f, 0.95f), smoothstep(0.04f, -0.06f, aElev));
                const float3 oceanCol = lerp(deepSea, seaSky, fres) + glintCol * (glint * 0.85f);
                const float hazeSea = exp(-tSea * 0.000018f);
                const float subFade = smoothstep(-0.001f, -0.048f, sDir.z);
                fullSky = lerp(fullSky, lerp(fullSky, oceanCol, hazeSea), subFade);
            }
            // Wave 38 First-Principles Cosmos, Sidereal Milky Way/Aetherius, O/B/A/F/G/K/M Starfield & 3D Curtain Auroras
            // (RawCosmosSky.hlsli - unified with AtmosphereLut.hlsl & EnvCubePass.hlsl so water/ice reflect the exact same sky!):
            // Blended strictly on unobstructed sky / replaced sky-card pixels BEHIND Masser/Secunda and BEHIND clouds!
            const float nightSkyW = saturate(1.0f - dayW * 1.35f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
            if ((d >= 0.99999f || isSkyCard) && !isMoonGeom && sDir.z > -0.01f && (nightSkyW > 0.01f || twilightBell > 0.02f || weather.z > 0.10f)) {
                const float simT = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
                const float3 cosmosAdd = evalCosmosAndAurora(sDir, aSun, timeEnv.x, simT, cover, weather.z, camPosW.y);
                fullSky += cosmosAdd * ((1.0f - isExotic) * lerp(1.0f, 0.55f, twilightBell));
            }
            // Preserve Masser & Secunda twin moons cleanly on top of our procedural night/twilight sky.
            // Never flatten craters with distant grey fog - apply only subtle realistic low-horizon atmospheric extinction (sDir.z < 0.08).
            float3 baseSky = lerp(lit.rgb, fullSky, (1.0f - isExotic) * (1.0f - moonKeep * 0.96f));
            if (moonKeep > 0.04f && nightSkyW > 0.04f) {
                baseSky += lit.rgb * float3(1.06f, 0.98f, 0.88f) * (moonKeep * nightSkyW * 0.34f);
            }
            if (isCelestial && sDir.z < 0.08f) {
                const float lowHorExt = 1.0f - smoothstep(0.01f, 0.08f, sDir.z);
                const float3 horWarmTint = lerp(1.0f.xxx, float3(0.94f, 0.80f, 0.64f), lowHorExt * 0.50f);
                baseSky = lerp(baseSky * horWarmTint, fullSky, lowHorExt * 0.28f);
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
            // Wave 81: Physical world-space angular 42-deg rain-mist bow & 22-deg ice-crystal halo scattering (0 screen-UV drift):
            if ((1.0f - isExotic) > 0.2f && dayW > 0.12f && sDir.z > 0.02f && (weather.w > 0.05f || weather.z > 0.08f)) {
                const float altMetersW81 = max(camPosW.z * (1.0f / 70.0f), 0.0f);
                const float tempAtmoW81  = lerp(9.0f, -6.0f, saturate(weather.z * 1.15f));
                const float ctW81        = saturate(1.0f - cover * 0.72f);
                const RawAtmoHaloResult atmoW81 = rawEvalAtmoSpectralHalo(
                    sDir, aSun, weather.w, weather.z, ctW81, altMetersW81, tempAtmoW81);
                baseSky += atmoW81.haloRGB * ((1.0f - isExotic) * dayW * 0.45f);
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
            // When camera is submerged, apply physical underwater Snell's Window + volumetric sun-shafts to sky pixels.
            // Do NOT run screen-space downward flame cones on empty sky pixels (prevents diffuse red smudges across the sky;
            // true world-space volumetric fire & smoke in front of sky are rendered by RawFluidFireComposite.hlsl).
            if ((flags.x & FLAG_UNDERWATER) != 0u) {
                const float liveSeaUw = rawLiveWaterZ(engineWorld, physWorld[0]);
                baseSky = evalUnderwaterVolumetrics(baseSky, eyeCel, sDir, 2600.0f, px, liveSeaUw, true);
            }
            // Wave 48 Seamless sky-layer continuation of outdoor 3m/7m/14m parallax rain & snow streaks:
            if ((flags.x & FLAG_UNDERWATER) == 0u && !isCelestial && (weather.w > 0.05f || weather.z > 0.05f)) {
                baseSky += evalVolumetricParallaxPrecip(eyeCel, sDir, 100000.0f, 1.0f, baseSky, aSun, nightSkyW, 0.0f, false);
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
            if (all(isfinite(tap))) {
                const float relZ = abs(tap.a - zPix) / max(zPix, 1.0f);
                const float wz = exp(-min(relZ * 18.0f, 32.0f));
                vSum += max(tap.rgb, 0.0f.xxx) * wz;
                vW += wz;
            }
        }
    }
    const float3 volRawFallback = VolFog.SampleLevel(Lin, uv, 0.0f).rgb;
    const float3 vol2D = vW > 1.0e-4f ? (vSum / vW) : (all(isfinite(volRawFallback)) ? max(volRawFallback, 0.0f.xxx) : 0.0f.xxx);
    // Live 3D World-Map Depth Isolation + True 3D Froxel Transmittance & In-Scatter Composition:
    // Within 24u..42,000u (kFroxFar), FroxelVol supplies genuine 3D Beer-Lambert transmittance (frox.a) and
    // dynamically lit 3D in-scattering (frox.rgb); VolFog adds fine crepuscular shafts without double-fogging.
    const float mapScale = (timeEnv.w > 0.5f && camPosW.z > 42000.0f) ? 0.18f : 1.0f;
    // Smooth astronomical 24h day ramp (prevents sudden foreground haze pops across dawn/dusk transitions!):
    const float dayFg = (sunColour.w > 0.5f) ? smoothstep(-0.04f, 0.14f, astroSunElev()) : 0.0f;
    const float fgClear = lerp(1.0f, smoothstep(260.0f, 1500.0f, dist), dayFg * (1.0f - saturate(weather.w * 0.65f + weather.z * 0.75f)));
    const float4 froxRaw = sampleFroxel3D(uv, curZ);
    const float4 frox = all(isfinite(froxRaw)) ? float4(clamp(froxRaw.rgb, 0.0f.xxx, 8.0f.xxx), saturate(froxRaw.a)) : float4(0.0f, 0.0f, 0.0f, 1.0f);
    const float froxT = lerp(1.0f, frox.a, mapScale);
    const float3 froxScat = frox.rgb * mapScale;
    const float3 shaftScatter = vol2D * (0.14f * mapScale * fgClear);
    const float3 scatter = froxScat + shaftScatter;
    // Wave 42 Physical Underwater Volumetrics: Pope & Fry Beer-Lambert marine absorption + 12-step refracted sun-shafts + glacial silt motes!
    const float tideWaterZ = rawLiveWaterZ(engineWorld, physWorld[0]);
    if ((flags.x & FLAG_UNDERWATER) != 0u && (engineWorld.z < 0.5f || (p.z + camPosW.z) <= tideWaterZ + 12.0f)) {
        const float3 uwFinal = evalUnderwaterVolumetrics(lit.rgb, eye, w, dist, px, tideWaterZ, false);
        return float4(uwFinal, lit.a);
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
        const float pcDz = (midPt.z - 5200.0f) / 3600.0f;
        const float peakCollar = exp(-min(pcDz * pcDz, 32.0f)) * smoothstep(24000.0f, 52000.0f, dist);
        const float alpineMist = (0.76f + 0.48f * alpineNoise) * (1.0f + peakCollar * (0.42f + 0.50f * alpineNoise));
        // Smooth exponential far-horizon aerial decay aligned with RawTerrainWorld.hlsl's exp(-optDepth) frontier curve:
        const float odFar = clamp(baseSigma * farSpan * elevW * alpineMist * exp(-min(hStart * 0.15f, 24.0f))
                          + max(dist - 85000.0f, 0.0f) * 0.000016f, 0.0f, 40.0f);
        const float horizonMelt = smoothstep(95000.0f, 190000.0f, dist);
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
        // Wave 65: keep rainy/overcast mid-distance mist neutral silver-slate rather than heavy dark indigo washout:
        const float rainOvercastW = saturate(weather.w * 0.85f + saturate(cloudCov - 0.50f) * 0.55f) * (1.0f - nightF);
        const float3 overcastTintF = lerp(float3(0.88f, 0.94f, 1.04f), float3(0.95f, 0.97f, 1.00f), rainOvercastW);
        const float3 overcastScatterF = lerp(hillaireSky, hLumF.xxx * overcastTintF, 0.72f);
        float3 atmoCol = lerp(hillaireSky, overcastScatterF, overcastWF * 0.78f);
        // Seamless Far-Horizon Aerial Fusion with RawTerrainWorld.hlsl's evalWorldAtmosphere() frontier scatter:
        const float frontierFuse = smoothstep(65000.0f, 165000.0f, dist);
        const float3 worldAtmoRef = lerp(uSkyHF, uSkyZF, saturate((w.z * 0.5f + 0.5f) * 0.72f))
                                  + uSunF * (rawMultiScatterPhase(cosSunF, clamp(tune.y, 0.25f, 0.78f), odFar) * saturate(aElevFg * 4.0f + 0.15f) * 0.42f);
        atmoCol = lerp(atmoCol, lerp(uSkyHF, uSkyZF, saturate(max(w.z, 0.0f) * 2.2f)), 0.28f) * (0.92f * lerp(1.0f, 0.78f, twilightF * (1.0f - horizonMelt)));
        atmoCol = lerp(atmoCol, worldAtmoRef, frontierFuse * 0.38f * (1.0f - nightF));
        if ((flags.x & FLAG_CLOUDS) != 0u && horizonMelt > 0.002f) {
            const float4 clF = Clouds.SampleLevel(Lin, uv, 0.0f);
            // Only fuse sky clouds into the extreme distant horizon melt (>115k..190k u), NEVER onto mid-distance mountains!
            const float clMerge = smoothstep(115000.0f, 190000.0f, dist) * horizonMelt;
            atmoCol = lerp(atmoCol, atmoCol * clF.a + clF.rgb, clMerge);
        }
        // Dynamically lit far mountain aerial perspective: cloud-shadowed directional solar in-scattering vs shadowed ridges!
        const float fogCloudVis = (flags.x & FLAG_CLOUDS) != 0u ? cloudShadowN(midPt, 1u) : 1.0f;
        const float fogSunPhase = 0.18f + 0.82f * pow(cosSunF, 3.0f);
        const float3 d65Mie = lerp(float3(1.0f, 0.62f, 0.30f), float3(0.96f, 0.98f, 1.02f), highSunF);
        const float mieHalo = (pow(cosSunF, 5.0f) * 0.10f + pow(cosSunF, 28.0f) * 0.16f) * (1.0f - cloudCov * 0.72f) * (1.0f - nightF) * fogCloudVis;
        const float cloudModFloor = lerp(0.46f, 0.62f, rainOvercastW);
        const float cloudMod = lerp(lerp(cloudModFloor, 0.96f, fogCloudVis * (1.0f - nightF * 0.65f)), 1.0f, horizonMelt);
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
            const float3 daySheetCol = lerp(float3(0.38f, 0.42f, 0.46f), float3(0.62f, 0.68f, 0.76f), weather.z);
            const float3 sheetCol = lerp(daySheetCol, nightHorF * 1.05f, nightF);
            atmoCol = lerp(atmoCol, sheetCol, sheetDens * 0.48f);
            T *= (1.0f - sheetDens * 0.35f);
        }
        // Re-lock distant snowy/overcast twilight & night horizon color to exact sky-dome nightHorF so Winterhold cliffs never cut out!
        atmoCol = lerp(atmoCol, nightHorF, nightF * 0.85f);
        atmoCol = min(atmoCol, 3.5f.xxx);
        // Wave 48 Local-Light Illuminated 3-Layer Depth-Sliced Parallax Precipitation Volume (3m / 7m / 14m)
        // + Scene-Depth-Verified Near-Emitter Fire-Ember Sparks (< 2.5m), strictly outdoor & depth-occluded:
        const float3 precipAdd = (mapScale > 0.5f)
            ? evalVolumetricParallaxPrecip(eye, w, dist, d, atmoCol, aSunFg, nightF, ashW, true)
            : 0.0f.xxx;
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
        // Scene-Anchored 3D Volumetric Fire, Self-Shadowed Dark Soot Smoke & Rising Embers + Scene-Anchored 3D Volumetric Spells:
        // Strictly anchored to real HDR scene flame/spell cores (ZERO floating spheres around empty-air point lights!).
        float4 extFireVol = float4(0.0f, 0.0f, 0.0f, 1.0f);
        float4 extSpellVol = float4(0.0f, 0.0f, 0.0f, 1.0f);
        if (mapScale > 0.5f) {
            extFireVol = evalVolumetricFirePlumes(eye, w, dist, curZ, px, false);
            float2 spellLensPx = 0.0f.xx;
            extSpellVol = evalVolumetricSpells3D(Lit, Depth, Lin, eye, w, dist, curZ, px, false, spellLensPx);
            if (dot(spellLensPx, spellLensPx) > 0.002f) {
                const float2 lensUv = clamp(uv + spellLensPx * dims.zw, 0.002f.xx, 0.998f.xx);
                const float dLens = Depth.SampleLevel(Lin, lensUv, 0.0f);
                if (dLens >= d - 0.002f) { lit.rgb = lerp(lit.rgb, Lit.SampleLevel(Lin, lensUv, 0.0f).rgb, 0.65f); }
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
        // Wave 38 Dynamic Mountain Fog, Valley Inversion Mist, Coastal Sea-Fog & Weather Curtains (RawWeatherAtmo.hlsli):
        // Runs strictly on exterior terrestrial geometry (Masser, Secunda & stars already returned cleanly in isCelestial!).
        DynamicFogSample dynFog;
        dynFog.inscatter     = 0.0f.xxx;
        dynFog.transmittance = 1.0f;
        dynFog.mountainFog01 = 0.0f;
        dynFog.valleyMist01  = 0.0f;
        if (mapScale > 0.5f && dist > 64.0f) {
            const float3 camAbsF = camPosW.xyz + eye;
            const float3 hitAbsF = camPosW.xyz + p;
            const float liveSeaF = rawLiveWaterZ(engineWorld, physWorld[0]);
            const float wxTempF  = lerp(8.0f, -6.0f, saturate(weather.z + smoothstep(55000.0f, 125000.0f, camAbsF.y) * 0.65f));
            dynFog = evalDynamicMountainAndValleyFog(
                camAbsF, hitAbsF, w, dist, aSunFg, uSunF, atmoCol,
                weather, float4(timeEnv.x, aElevFg, timeEnv.z, timeEnv.w),
                float4(cloudWind.xy, saturate(clouds.x), cloudWind.w),
                float2(liveSeaF, wxTempF));
        }
        // Exact front-to-back Beer-Lambert composition:
        //   lit * (froxT * Tfar) + froxScat (0..42k 3D volume) + farInScat (42k..horizon, attenuated by froxT) + shafts
        //   composed with dynamic mountain/valley weather fog, Wave 78 analytical height-fog aerial coupling, then 3D fire/smoke!
        const RawAerialHeightFogResult aerialW78 = rawEvalAnalyticalAerialHeightFog(
            camPosW.z + eye.z, w.z, max(dist - 12000.0f, 0.0f), tideWaterZ,
            baseSigma * 0.65f, lerp(2200.0f, 4200.0f, saturate(clouds.x * 0.6f + weather.w * 0.4f)),
            fogCloudVis, cosSunF, atmoCol, uSunF, nightF);
        const float aerialMixW = smoothstep(12000.0f, 55000.0f, dist) * 0.35f;
        const float3 farTCombined = lerp(Tfar, aerialW78.transmittance.xxx, aerialMixW);
        const float3 farInScat = lerp(atmoCol * (1.0f.xxx - Tfar), aerialW78.inScatter, aerialMixW) * froxT;
        T = froxT * farTCombined;
        const float dynW = 0.52f * smoothstep(260.0f, 1800.0f, dist);
        const float dynT = lerp(1.0f, dynFog.transmittance, dynW);
        const float3 dynScatRaw = dynFog.inscatter * (dynW * lerp(0.55f, 1.0f, froxT));
        // Neutralize excessive indigo chroma in daytime rain/overcast mid-distance mist so rainy Riverwood reads crisp & natural:
        const float froxLumR = dot(froxScat, float3(0.2126f, 0.7152f, 0.0722f));
        const float3 froxBalanced = lerp(froxScat, lerp(froxScat, froxLumR.xxx * float3(0.95f, 0.97f, 1.01f), 0.55f), rainOvercastW);
        const float dynLumR = dot(dynScatRaw, float3(0.2126f, 0.7152f, 0.0722f));
        const float3 dynScat = lerp(dynScatRaw, lerp(dynScatRaw, dynLumR.xxx * float3(0.96f, 0.98f, 1.01f), 0.50f), rainOvercastW);
        const float3 extBase = lit.rgb * T + (froxBalanced * cavityTint) * pdeMistBoost + farInScat + shaftScatter * saturate(T + 0.15f) * pdeMistBoost;
        float3 extScene = (extBase * dynT + dynScat + precipAdd + silDiffAdd) * extSpellVol.a + extSpellVol.rgb;
        extScene = extScene * extFireVol.a + extFireVol.rgb;
        if (mapScale > 0.5f && seasonThermal0.w < -1.0f) {
            const float4 subZeroSteam = evalSubZeroThermalSteam(
                eye, w, dist, d, camPosW.xyz + p, px, tideWaterZ,
                uSunF, atmoCol, aSunFg, extFireVol);
            extScene = extScene * subZeroSteam.a + subZeroSteam.rgb;
        }
        return float4(all(isfinite(extScene)) ? max(extScene, 0.0f.xxx) : max(lit.rgb, 0.0f.xxx), lit.a);
    }
    // Dungeons, Caves, Nordic Tombs, Dwemer Ruins, Blackreach & Interiors (sunColour.w < 0.5):
    // Crisp, deep-contrast interior air (eliminates milky grey Dragonsreach haze & Septimus ice-cave whiteout) + scene-anchored fire/smoke.
    if (sunColour.w < 0.5f && (flags.x & FLAG_FOG) != 0u) {
        const float3 wpInt = camPosW.xyz + eye + w * min(dist * 0.5f, 3200.0f);
        const float caveNoise = 0.78f + 0.44f * cNoise(wpInt * 0.00048f + float3(0.0f, 0.0f, camPosW.w * 0.015f));
        const float intOd = max(dist - 750.0f, 0.0f) * 0.000014f * caveNoise;
        const float intT = exp(-min(intOd, 0.18f));
        const float3 cellAmb = max(float3(ambient[0].w, ambient[1].w, ambient[2].w), float3(0.008f, 0.010f, 0.012f));
        const float ambL = dot(cellAmb, float3(0.2126f, 0.7152f, 0.0722f));
        const float3 caveMist = min(lerp(ambL.xxx, cellAmb, 0.45f) * 0.16f, 0.024f.xxx);
        // Scene-Anchored 3D Volumetric Fire/Smoke/Embers + Scene-Anchored 3D Volumetric Spells (interiors):
        const float4 intFireVol = evalVolumetricFirePlumes(eye, w, dist, curZ, px, true);
        float2 intSpellLensPx = 0.0f.xx;
        const float4 intSpellVol = evalVolumetricSpells3D(Lit, Depth, Lin, eye, w, dist, curZ, px, true, intSpellLensPx);
        if (dot(intSpellLensPx, intSpellLensPx) > 0.002f) {
            const float2 lensUvI = clamp(uv + intSpellLensPx * dims.zw, 0.002f.xx, 0.998f.xx);
            const float dLensI = Depth.SampleLevel(Lin, lensUvI, 0.0f);
            if (dLensI >= d - 0.002f) { lit.rgb = lerp(lit.rgb, Lit.SampleLevel(Lin, lensUvI, 0.0f).rgb, 0.65f); }
        }
        const float3 intScene = (lit.rgb * intT + caveMist * (1.0f - intT) + scatter * 0.34f) * intSpellVol.a + intSpellVol.rgb;
        const float3 intOut = intScene * intFireVol.a + intFireVol.rgb;
        return float4(all(isfinite(intOut)) ? max(intOut, 0.0f.xxx) : max(lit.rgb, 0.0f.xxx), lit.a);
    }
    const float T = (flags.x & FLAG_FOG_REPLACES) != 0u ? exp(-min(max(sigma * tEnd, 0.0f), 40.0f)) : 1.0f;
    const float3 fallbackOut = lit.rgb * T + scatter;
    return float4(all(isfinite(fallbackOut)) ? max(fallbackOut, 0.0f.xxx) : max(lit.rgb, 0.0f.xxx), lit.a);
}
