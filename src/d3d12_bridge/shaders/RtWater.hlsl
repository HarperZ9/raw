// RtWater.hlsl: ray-traced reflection, sun glint and caustics on the game's water (WAT-01, WAT-07;
// RtWater.cpp, docs/RAYTRACING.md "Water").
//
// It runs once a frame, right after the game composites its water into the HDR colour, on the same
// ray-tracing scene the frame's shadows used. Inputs, in RtShadow's binding slots:
//   t0  the opaque depth (the bed under the water)
//   t1  RAW's layer target: r the water surface's depth, gb its view normal after the wave maps
//   t2  RAW's albedo target (the bed's colour, for the caustics)
//   t5  the HDR colour with the game's water in it
//   u0  out: the same colour with RAW's water on top
// Per water pixel: one reflection ray around the mirror direction of the wave normal (on screen the hit's
// colour comes from the frame, off screen from the hit's own material and the radiance cache, a miss sees
// the sky), mixed over the game's water by Fresnel (Schlick, F0 0.02); one shadow ray for the sun's glint
// on the waves (GGX); one shadow ray from the bed for the caustics, a moving network of light focused by
// the waves, projected along the refracted sun ray and dimmed by the water above the bed.
// Original work, MIT.
#define RT_NO_MAIN 1
#include "RtShadow.hlsl"
#include "RawProceduralNoise.hlsli"
#include "RawSeasonsThermals.hlsli"

#define Layer Normal
#define BedAlbedo Spec
#define WaterOut Vis
#define PreWater Refl   // u4: the HDR colour just before the game composites its water (RAW's lit bed), when skyLength > 0.5

// Wave 40 Live 256x256 GPU Shallow-Water Equations (RawFluidWaterSim.hlsl) bindings:
// Safe fallback when fluidWaterDomain.w <= 0.5 (null SRVs, zero cost on WARP tests without fluids).
Texture2D<float4> SimWaterState  : register(t10); // R = height, G = vx, B = vy, A = advected lacy foam persistence
Texture2D<float4> SimWaterNormal : register(t11); // RGB = analytical curl-free Z-up normal, A = Jacobian crest + lacy foam
SamplerState      SimWaterSamp   : register(s0);  // linear wrap for exact toroidal world-locked lookup
cbuffer FluidWaterDomainCB : register(b3) { float4 fluidWaterDomain; }; // xy = originXY, z = invWorldSpanU, w = active (> 0.5)

// Wave 59: Strict NaN/Inf sanitization & spectral Beer-Lambert water absorption helper (Pope & Fry 1997).
float3 sanitizeWater3(float3 c) {
    return all(isfinite(c)) ? max(c, 0.0f.xxx) : 0.0f.xxx;
}

float3 evalSpectralWaterTransmittance(float pathU, float waterAbsorb, bool isExterior, bool isCoastalSea, float turbidity01) {
    const float safePath = clamp(isfinite(pathU) ? max(pathU, 0.0f) : 0.0f, 0.0f, 20000.0f);
    const float safeAbs  = clamp(isfinite(waterAbsorb) ? max(waterAbsorb, 0.0f) : 0.008f, 0.0f, 1.0f);
    // Pope & Fry spectral pure-water absorption (red 650nm dies ~5.5x faster than blue 465nm)
    // blended with Nordic coastal CDOM/phytoplankton & storm-runoff turbidity.
    const float3 pureExt   = isCoastalSea ? float3(2.55f, 0.84f, 0.50f) : float3(2.42f, 0.68f, 0.38f);
    const float3 turbidExt = float3(2.65f, 1.18f, 0.92f);
    const float3 extRGB    = isExterior ? lerp(pureExt, turbidExt, saturate(turbidity01) * 0.55f)
                                        : float3(2.15f, 1.32f, 1.10f);
    const float3 od = clamp(extRGB * (safeAbs * safePath), 0.0f.xxx, 32.0f.xxx);
    return exp(-od);
}

// The distance from a point to the nearest wall between moving cells: 0 on the walls. The walls of
// a cell field whose centres drift slowly read as the bright net that waves focus onto a bed.
float cellWalls(float2 q, float t) {
    const float2 c = floor(q);
    float f1 = 8.0f, f2 = 8.0f;
    [unroll] for (int dy = -1; dy <= 1; dy++) {
        [unroll] for (int dx = -1; dx <= 1; dx++) {
            const float2 cell = c + float2(dx, dy);
            const float2 h = float2(hash21(cell), hash21(cell + 31.7f));
            const float2 at = cell + 0.5f + 0.42f * sin(t * (0.6f + 0.5f * h.yx) + 6.2831853f * h);
            const float dd = length(q - at);
            if (dd < f1) { f2 = f1; f1 = dd; } else if (dd < f2) { f2 = dd; }
        }
    }
    return f2 - f1;
}

// Wave 42 Aperiodic Dual-Layer Curl-Convergence Caustic Filaments (zero Voronoi grids, zero periodic stripes!):
float evalAperiodicCurlCaustic(float2 xy, float tSec, float scaleU) {
    const float invSc = 1.0f / max(scaleU, 12.0f);
    const float2 q1 = xy * invSc + float2(tSec * 0.24f, -tSec * 0.16f);
    const float4 n1 = rawNoise3Grad(float3(q1, tSec * 0.34f));
    const float2 w1 = float2(n1.z, -n1.y) * 0.68f;
    const float f1 = pow(saturate(1.0f - abs(rawNoise3Grad(float3(q1 + w1, tSec * 0.38f + 5.7f)).x * 2.0f - 1.0f) * 2.5f), 3.6f);

    const float2 q2 = float2(xy.x * 0.6018f - xy.y * 0.7986f, xy.x * 0.7986f + xy.y * 0.6018f) * (invSc * 1.92f)
                    + float2(-tSec * 0.19f, tSec * 0.28f);
    const float4 n2 = rawNoise3Grad(float3(q2, -tSec * 0.46f + 17.3f));
    const float2 w2 = float2(n2.z, -n2.y) * 0.56f;
    const float f2 = pow(saturate(1.0f - abs(rawNoise3Grad(float3(q2 + w2, -tSec * 0.50f + 29.1f)).x * 2.0f - 1.0f) * 2.7f), 3.8f);
    return saturate(f1 * 0.55f + f2 * 0.45f + f1 * f2 * 1.15f);
}

// The caustic net at an absolute world point (units), kept for compatibility and routed to aperiodic curl caustics.
float causticNet(float2 w, float t, float scale) {
    return evalAperiodicCurlCaustic(w, t * 2.0f, scale);
}

// True 3D Physical Wave Heightfield Evaluator (for 3D Ray-Marched Self-Occluding Wave Geometry breaking flat 2011 quads):
float evalFluidHeight3D(float2 xy, float2 flowDir, float wt, float ampScale, float wDist) {
    const float2 wpF = xy - flowDir * (wt * 22.0f);
    const float2 flowPerp = float2(-flowDir.y, flowDir.x);
    const float distFade = smoothstep(14000.0f, 800.0f, wDist);
    float h = 0.0f;
    static const float kH_k[4] = { 0.0065f, 0.0135f, 0.0265f, 0.0490f };
    static const float kH_a[4] = { 2.85f,   1.65f,   0.85f,   0.42f   };
    static const float kH_s[4] = { 0.00f,   0.62f,  -0.54f,   1.08f   };
    [unroll] for (int i = 0; i < 4; ++i) {
        const float2 d = normalize(flowDir * cos(kH_s[i]) + flowPerp * sin(kH_s[i]));
        const float omega = sqrt(9.81f * (kH_k[i] * 70.0f));
        const float ph = dot(wpF, d) * kH_k[i] - wt * omega + float(i) * 2.39996f;
        // Trochoidal sharp-crest height profile:
        h += (sin(ph) + 0.25f * sin(2.0f * ph)) * kH_a[i];
    }
    return h * ampScale * distFade;
}

// 8-Wave Trochoidal Gerstner Spectrum (irrational frequency ratios, sharp crests, broad troughs, zero tiling).
float2 evalTrochoidalWaves(float2 xy, float2 wDir, float wt, float wDist, float windAmp) {
    const float2 wPerp = float2(-wDir.y, wDir.x);
    const float oceanFade = saturate(1.0f - wDist / 26000.0f);
    const float nearFade = saturate(1.0f - wDist / 6800.0f);
    const float microFade = saturate(1.0f - wDist / 2200.0f);
    static const float kFreq[8]  = {0.0049f, 0.0097f, 0.0193f, 0.0371f, 0.0683f, 0.1190f, 0.1970f, 0.3110f};
    static const float kSpeed[8] = {0.34f,   0.47f,   0.63f,   0.81f,   1.02f,   1.26f,   1.54f,   1.88f};
    static const float kAng[8]   = {0.00f,   0.52f,  -0.41f,   0.89f,  -0.78f,   1.24f,  -1.15f,   1.57f};
    static const float kAmp[8]   = {0.42f,   0.32f,   0.26f,   0.22f,   0.18f,   0.14f,   0.10f,   0.07f};
    float2 grad = 0.0f.xx;
    [unroll] for (int wi = 0; wi < 8; wi++) {
        const float fade = wi < 2 ? oceanFade : (wi < 6 ? nearFade : microFade);
        if (fade <= 0.001f) { continue; }
        const float c = cos(kAng[wi]), s = sin(kAng[wi]);
        const float2 dir = wDir * c + wPerp * s;
        const float ph = dot(xy, dir) * kFreq[wi] + wt * kSpeed[wi] + float(wi) * 1.618f;
        // Trochoidal steepening: sharpens crests (sin + 0.35 * sin(2*ph)) for realistic liquid geometry.
        const float dWave = cos(ph) + 0.35f * cos(2.0f * ph);
        grad += dir * (dWave * kAmp[wi] * fade);
    }
    return grad * (0.58f + windAmp * 0.28f);
}

// Aperiodic 2D gradient-smooth value noise (zero periodic sine/checkerboard lattice dots on dock pilings).
float vNoise2D(float2 p) {
    const float2 i = floor(p);
    const float2 f = frac(p);
    const float2 u = f * f * (3.0f - 2.0f * f);
    const float a = hash21(i);
    const float b = hash21(i + float2(1.0f, 0.0f));
    const float c = hash21(i + float2(0.0f, 1.0f));
    const float d = hash21(i + float2(1.0f, 1.0f));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// Wave 38 Organic Cellular Open-Cell Lacy Foam Web Generator (Jettelly / 24Ronin inspired):
// Uses domain-warped dual-scale cellular bubble webs so shoreline collars & sparse sea whitecaps
// form crisp aerated lacy trails with dark reflective water peeking through open bubble holes.
float cellularFoamLace(float2 p, float t, float density01) {
    const float2 q = p * 0.044f + float2(t * 0.12f, -t * 0.08f);
    const float4 warpN = rawNoise3Grad(float3(q * 0.65f, t * 0.16f));
    const float2 qw = q + (warpN.yz - 0.5f.xx) * 1.45f;
    const float web1 = cellWalls(qw * 1.10f, t * 0.28f);
    const float web2 = cellWalls(qw * 2.45f + float2(13.7f, 5.3f), t * 0.44f);
    const float lace1 = 1.0f - smoothstep(0.02f, lerp(0.14f, 0.38f, density01), web1);
    const float lace2 = 1.0f - smoothstep(0.015f, lerp(0.12f, 0.32f, density01), web2);
    const float microBub = smoothstep(0.42f, 0.84f, rawNoise3Grad(float3(qw * 5.8f, t * 0.22f)).x);
    const float solidCore = smoothstep(0.62f, 0.94f, density01) * (0.62f + 0.38f * microBub);
    return saturate(max(solidCore, max(lace1 * 0.86f, lace2 * 0.62f) * (0.45f + 0.55f * microBub)));
}

// Wave 38 4-Octave Domain-Warped Aperiodic Fluid Gradient Noise + Subtle Organic Swells:
// Completely eliminates coherent periodic sine-wave zebra stripes and fine creek hatching.
// Uses incommensurately rotated analytical rawNoise3Grad octaves so both glassy Riverwood creeks
// and rolling Sea of Ghosts ocean swells produce silky-smooth, isotropic, C2-continuous water normals.
float evalWaterWaveHeight(float2 xy, float wt, float2 flowDir, float2 flowPerp, float ampScale, float waterDepthU, float gPhys, out float2 outGrad, out float outFold) {
    float h = 0.0f;
    outGrad = 0.0f.xx;
    const float gScale = sqrt(clamp(gPhys / 9.80665f, 0.25f, 2.5f));
    const float depthM = clamp(waterDepthU / 70.0f, 0.05f, 40.0f);
    // Smooth depth damping keeps shallow creeks calm & glassy while open deep seas carry broader swells:
    const float deepSwellMask = smoothstep(0.18f, 2.40f, depthM);

    // Align coordinates gently with live wind/flow basis, then apply low-frequency aperiodic domain warp
    // so zero crests ever lock into parallel diagonal bands:
    const float2 pFlow = float2(dot(xy, flowDir), dot(xy, flowPerp));
    const float4 warpA = rawNoise3Grad(float3(pFlow * 0.0028f - float2(wt * 0.045f, 0.0f), wt * 0.06f));
    const float4 warpB = rawNoise3Grad(float3(pFlow * 0.0061f + float2(11.7f - wt * 0.07f, 4.3f), wt * 0.09f + 7.1f));
    const float2 pWarp = pFlow + float2((warpA.x - 0.5f) * 84.0f + (warpB.x - 0.5f) * 28.0f,
                                        (warpB.x - 0.5f) * 76.0f - (warpA.x - 0.5f) * 24.0f);

    // 4 incommensurately rotated analytical noise octaves (prime/golden angles & non-harmonic frequencies):
    static const float kFreq[4]  = { 0.0068f, 0.0157f, 0.0369f, 0.0841f };
    static const float kAmp[4]   = { 2.65f,   1.32f,   0.58f,   0.22f   };
    static const float kSpd[4]   = { 0.34f,   0.51f,   0.78f,   1.12f   };
    static const float kCosR[4]  = {  1.0000f,  0.7986f, -0.4848f, -0.3907f };
    static const float kSinR[4]  = {  0.0000f,  0.6018f,  0.8746f, -0.9205f };
    static const float kAniso[4] = { 0.72f,   0.82f,   0.92f,   1.00f   };
    static const float3 kOff[4]  = {
        float3( 0.0f,  0.0f,  0.0f),
        float3(17.3f,  9.1f,  3.7f),
        float3(31.7f, 23.4f, 11.9f),
        float3(53.1f, 41.8f, 19.3f)
    };

    float2 gFlow = 0.0f.xx;
    float crestAccum = 0.0f;
    [unroll] for (int oct = 0; oct < 4; ++oct) {
        const float cR = kCosR[oct];
        const float sR = kSinR[oct];
        const float an = kAniso[oct];
        // Rotate & softly drift each octave at its own gravity-scaled speed:
        const float2 qRot = float2(pWarp.x * cR - pWarp.y * sR, (pWarp.x * sR + pWarp.y * cR) * an);
        const float2 adv  = float2(-wt * kSpd[oct] * gScale * 1.15f, wt * kSpd[oct] * gScale * 0.25f);
        const float4 ng   = rawNoise3Grad(float3(qRot * kFreq[oct] + adv, wt * (0.28f + 0.14f * float(oct)) * gScale) + kOff[oct]);
        const float octDepthW = (oct == 0) ? lerp(0.32f, 1.0f, deepSwellMask) : ((oct == 1) ? lerp(0.55f, 1.0f, deepSwellMask) : 1.0f);
        const float aOct = kAmp[oct] * ampScale * octDepthW;
        // Centered smooth wave value with subtle organic ridge fullness (C2 smooth, zero stripes):
        const float vC = ng.x - 0.5f;
        const float vShaped = vC + 0.28f * (ng.x * ng.x - 0.333f);
        const float dShaped = 1.0f + 0.56f * ng.x;
        h += aOct * vShaped;
        // Exact analytical chain-rule gradient transformed back from rotated octave space to flow space:
        const float gxRot = ng.y * kFreq[oct] * aOct * dShaped;
        const float gyRot = ng.z * kFreq[oct] * an * aOct * dShaped;
        gFlow += float2(gxRot * cR + gyRot * sR, -gxRot * sR + gyRot * cR);
        if (oct < 2) {
            crestAccum += smoothstep(0.56f, 0.90f, ng.x) * (oct == 0 ? 0.65f : 0.35f);
        }
    }

    // Subtle low-amplitude organic broad swell undulation on open deep ocean (100% aperiodic noise):
    if (deepSwellMask > 0.02f) {
        const float4 sw = rawNoise3Grad(float3(pWarp.x * 0.0026f - wt * 0.22f * gScale, pWarp.y * 0.0019f, wt * 0.14f * gScale + 29.3f));
        const float swAmp = 3.2f * ampScale * deepSwellMask;
        h += (sw.x - 0.5f) * swAmp;
        gFlow += float2(sw.y * 0.0026f, sw.z * 0.0019f) * swAmp;
    }

    // Map gradient from (flowDir, flowPerp) basis back to world XY:
    outGrad = flowDir * gFlow.x + flowPerp * gFlow.y;
    // Patch-masked aperiodic crest breaking energy (organic scattered ocean whitecaps, NEVER diagonal stripes!):
    const float patchMask = smoothstep(0.46f, 0.80f, rawNoise3Grad(float3(pFlow * 0.0036f - float2(wt * 0.05f, 0.0f), wt * 0.11f + 43.1f)).x);
    outFold = saturate(crestAccum * saturate(length(outGrad) * 2.4f) * patchMask * deepSwellMask);
    return h;
}

float3 waterSky(float3 rd) {
    float3 uSun, uSkyZ, uSkyH, uAmbUp, uAmbDn;
    evalWorldAtmosphere(uSun, uSkyZ, uSkyH, uAmbUp, uAmbDn);
    const float hUp = saturate(rd.z);
    const float aElevW = astroSunElev();
    const float nightW = smoothstep(0.03f, -0.07f, aElevW);
    const float overcastW = saturate(clouds.x * 0.85f - 0.12f + weather.w * 0.45f + weather.z * 0.42f);
    // Exact 1:1 radiometric scale (0.76x daytime) matching LightFog.hlsl sky dome + nocturnal arctic sky floor
    // so Windhelm harbor & dark northern fjords reflect luminous silver-indigo night skies instead of black ink!
    float3 skyCol = lerp(uSkyH, uSkyZ, pow(hUp, 0.62f)) * lerp(0.76f, 1.0f, nightW);
    if (nightW > 0.05f) {
        const float3 ambSkyN = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w), float3(0.028f, 0.042f, 0.068f));
        const float3 arcticNightRefl = lerp(float3(0.032f, 0.050f, 0.082f), ambSkyN * float3(0.75f, 0.84f, 0.96f), overcastW * 0.72f) * lerp(1.18f, 0.68f, pow(hUp, 0.55f));
        skyCol = max(skyCol, arcticNightRefl * nightW);
    }
    // Organic 2-octave drifting cloud-deck reflection matched to Clouds.hlsl wind & slate-silver overcast base:
    if (clouds.x > 0.08f && rd.z > 0.02f) {
        const float2 cUv = (camPosW.xy + rd.xy * (42000.0f / max(rd.z, 0.06f)) + cloudWind.xy * camPosW.w * 0.65f) * 0.000028f;
        const float cn = vNoise2D(cUv * 1.4f) * 0.65f + vNoise2D(cUv * 3.1f + 7.3f) * 0.35f;
        const float cov = smoothstep(lerp(0.66f, 0.28f, saturate(clouds.x)), 0.92f, cn) * smoothstep(0.02f, 0.18f, rd.z);
        const float3 overcastBase = float3(0.32f, 0.37f, 0.44f) * lerp(1.0f, 0.18f, nightW);
        const float3 cloudLit = lerp(lerp(uSkyH * 0.68f, max(uSun, uSkyH) * 0.72f, 0.42f), overcastBase, overcastW * 0.70f);
        skyCol = lerp(skyCol, cloudLit, cov * 0.58f);
    }
    return skyCol;
}

[numthreads(8, 8, 1)]
void water(uint3 id : SV_DispatchThreadID) {
    if (id.x >= uint(dims.x) || id.y >= uint(dims.y)) { return; }
    const int3 ip = int3(id.xy, 0);
    const float4 game = GameColour.Load(ip);
    const float4 L = Layer.Load(ip);
    const float dBed = Depth.Load(ip);
    const bool hasPreWater = (skyLength > 0.5f && waterB.z > 0.0f);
    // On non-water pixels, restore PreWater (captured immediately before Skyrim's 1-draw water composite quad)
    // so Skyrim's water composite can NEVER bleed upside-down reflection sheets onto dry land, snow, NPCs, or interiors.
    float3 col = hasPreWater ? PreWater[id.xy].rgb : game.rgb;
    // 100% Exact Ground-Truth Water Pixel Invariant:
    // 1) GBufferPatch writes Layer.a == 2.0f exclusively for ShaderKind::Water.
    // 2) Whenever PreWater is captured (right before Skyrim's 1-draw water composite quad), any real water pixel
    //    was actually composited by Skyrim (|GameColour - PreWater| > 0.0025), whereas dry land (log piles, roads,
    //    cabbage carts, NPCs, Whiterun Marketplace, Sleeping Giant Inn) has GameColour == PreWater (diff == 0.0).
    // This makes false horizon water sheets over dry land mathematically impossible at ANY pitch or distance
    // while allowing real water right up to 0.5u from the lens when swimming half-submerged!
    const float3 nL = worldNormal(float4(L.y, L.z, 0.0f, 0.0f));
    const float zSurfW = (L.x < 1.0f && L.x > 0.0f) ? viewZOf(L.x) : 1.0e9f;
    const float zBedW  = (dBed < 1.0f && dBed > 0.0f) ? viewZOf(dBed) : 1.0e9f;
    const float waterCompDiff = hasPreWater ? length(game.rgb - PreWater[id.xy].rgb) : 1.0f;
    // Never let clear shallow water disappear when its color matches the riverbed: trust L.a > 1.5f + depth separation!
    const bool gameDrewWater = !hasPreWater || (waterCompDiff > 0.0004f) || (zSurfW < zBedW - 2.0f);
    // Soft 0.25u shoreline depth threshold & sloped aqueduct support (nL.z > 0.25 eliminates camera-angle water drop-out!).
    const bool isWaterPx = (L.a > 1.5f && L.x < 1.0f && zSurfW > 0.5f && zSurfW < zBedW - max(zSurfW * 0.004f, 0.25f) && nL.z > 0.25f && gameDrewWater);
    // SM6.9 Wave-coherent early exit: non-water waves exit immediately with zero RayQuery divergence.
    if (!WaveActiveAnyTrue(isWaterPx)) { WaterOut[id.xy] = float4(sanitizeWater3(col), isfinite(game.a) ? saturate(game.a) : 1.0f); return; }
    if (isWaterPx) {
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, L.x);
        const float3 eyeW = eyeAt();
        // True underwater vs above-water test (supports kUnderwater FLAG_UNDERWATER & sloped rivers/waterfalls):
        const bool isUnderwaterView = (((flags.x & FLAG_UNDERWATER) != 0u) && (p.z > eyeW.z + 0.5f))
                                   || (p.z >= eyeW.z + 6.0f && dot(nL, normalize(eyeW - p)) <= 0.02f);
        if (!isUnderwaterView) {
            // Feature 13: 100% Independent DX12 Fluid Physics (disconnected from Skyrim's 2011 nL water normal map):
            // Pure multi-directional Gerstner/trochoidal + curl-advected obstacle-deflected fluid wave simulation.
            const float3 v = normalize(eyeAt() - p);
            // Analytically intersect the smooth 32-bit view ray against the water plane (eliminates 24-bit depth
            // quantization contour rings & fingerprint lines seen in 13.55.34!):
            const float3 eyePos = eyeAt();
            const float3 rayDir = normalize(worldAt(px, 0.5f) - eyePos);
            const float planeZ = p.z;
            const float tPlane = (rayDir.z < -1.0e-4f) ? clamp((planeZ - eyePos.z) / rayDir.z, 1.0f, 48000.0f) : length(p);
            const float3 pSmooth = (rayDir.z < -1.0e-4f && abs(tPlane - length(p)) < max(length(p) * 0.15f, 32.0f))
                                 ? (eyePos + rayDir * tPlane) : p;
            const float3 wpW = camPosW.xyz + pSmooth;
            float3 n = float3(0.0f, 0.0f, 1.0f);
            const float wDist = length(pSmooth);
            // Compute local water depth & world-space obstacle gradient (rocks, bridge piers, riverbanks)
            // for true potential-flow streamline deflection, upstream bow-wave pileup, and downstream eddy wakes.
            float depthV0 = 120.0f;
            float2 obsGrad = 0.0f.xx;
            float bowHit = 0.0f, eddyLee = 0.0f, jacobianFold = 0.0f;
            if (dBed < 1.0f && wDist < 5500.0f) {
                const float3 pb0 = worldAt(px, dBed);
                depthV0 = max(p.z - pb0.z, 0.0f);
                const int2 limW = int2(dims.xy) - 1;
                const int2 pR = clamp(ip.xy + int2( 4,  0), int2(0, 0), limW);
                const int2 pL = clamp(ip.xy + int2(-4,  0), int2(0, 0), limW);
                const int2 pD = clamp(ip.xy + int2( 0,  4), int2(0, 0), limW);
                const int2 pU = clamp(ip.xy + int2( 0, -4), int2(0, 0), limW);
                const float dR = Depth.Load(int3(pR, 0)), dL = Depth.Load(int3(pL, 0));
                const float dD = Depth.Load(int3(pD, 0)), dU = Depth.Load(int3(pU, 0));
                const float hR = dR < 1.0f ? max(p.z - worldAt(float2(pR), dR).z, 0.0f) : depthV0;
                const float hL = dL < 1.0f ? max(p.z - worldAt(float2(pL), dL).z, 0.0f) : depthV0;
                const float hD = dD < 1.0f ? max(p.z - worldAt(float2(pD), dD).z, 0.0f) : depthV0;
                const float hU = dU < 1.0f ? max(p.z - worldAt(float2(pU), dU).z, 0.0f) : depthV0;
                const float3 dpX = worldAt(float2(pR), L.x) - worldAt(float2(pL), L.x);
                const float3 dpY = worldAt(float2(pD), L.x) - worldAt(float2(pU), L.x);
                const float2 gRaw = dpX.xy * ((hR - hL) / max(dot(dpX.xy, dpX.xy), 1.0f))
                                  + dpY.xy * ((hD - hU) / max(dot(dpY.xy, dpY.xy), 1.0f));
                const float gLen = length(gRaw);
                if (gLen > 0.04f) { obsGrad = gRaw / gLen; }
            }
            float waveHeight3D = 0.0f;
            float2 riverFlowDir = float2(0.866f, 0.500f);
            float riverFlowSpeed = 0.0f;
            const float gPhys = max(physWorld[0].y, 1.0f);
            const float tideVel = physWorld[0].w;
            const float beaufort = clamp(physWorld[1].x, 0.5f, 12.0f);
            const float2 swellDir = length(physWorld[1].yz) > 0.1f ? normalize(physWorld[1].yz) : float2(0.866f, 0.500f);
            if (wDist < 26000.0f) {
                const bool isExtW0 = (sunColour.w > 0.5f);
                const float2 windVecW = normalize(cloudWind.xy + float2(1.0f, 0.35f));
                const float2 wDir = normalize(lerp(windVecW, swellDir, smoothstep(32.0f, 140.0f, depthV0) * 0.55f));
                const float wt = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
                const float windAmp = saturate(length(cloudWind.xy) / 360.0f);
                const float shallowW = 1.0f - smoothstep(4.0f, 52.0f, depthV0);
                const float nearObs = (1.0f - smoothstep(0.0f, 44.0f, depthV0)) * saturate(length(obsGrad) * 2.0f);
                // Live Beaufort sea-state + coastal vs inland amplitude separation:
                // Inland Whiterun basins & Riverwood streams stay crystal clear with gentle ripples;
                // open coastal seas (Sea of Ghosts around Z = -14000u) roll with rich Nordic swells!
                const bool isCoastalSea = (isExtW0 && abs((p.z + camPosW.z) - (-14000.0f)) < 450.0f);
                // Wave 47 River-Current Directional Flow & Potential-Flow Obstacle Deflection:
                // Sloped mountain streams flow coherently downhill (nL.xy) and deflect organically around boulders.
                const float2 slopeXY = nL.xy;
                const float slopeMag = length(slopeXY);
                const float2 baseStreamDir = (!isCoastalSea && slopeMag > 0.012f)
                    ? normalize(lerp(wDir, slopeXY / slopeMag, saturate(slopeMag * 14.0f))) : wDir;
                const float2 obsTang = float2(-obsGrad.y, obsGrad.x);
                const float deflSign = (dot(baseStreamDir, obsTang) >= 0.0f) ? 1.0f : -1.0f;
                const float2 flowDir = (nearObs > 0.02f && !isCoastalSea)
                    ? normalize(lerp(baseStreamDir, obsTang * deflSign, nearObs * 0.62f)) : baseStreamDir;
                const float2 flowPerp = float2(-flowDir.y, flowDir.x);
                riverFlowDir = flowDir;
                riverFlowSpeed = isCoastalSea
                    ? saturate(0.28f + abs(tideVel) * 0.45f)
                    : saturate(0.32f + slopeMag * 9.0f + shallowW * 0.34f + nearObs * 0.25f);
                const float intoObs = dot(baseStreamDir, -obsGrad);
                bowHit = saturate(intoObs) * nearObs * (isCoastalSea ? 0.55f : 0.68f);
                eddyLee = saturate(-intoObs) * nearObs * (isCoastalSea ? 0.40f : 0.56f);
                const float bNorm = saturate((beaufort - 1.0f) / 8.5f);
                const float deepWaterW = smoothstep(14.0f, 130.0f, depthV0);
                const float stormChop = saturate(weather.w * 0.85f + windAmp * 0.65f + bNorm * 0.75f);
                const float seaAmp = lerp(0.32f, 0.72f, deepWaterW) + stormChop * lerp(0.30f, 0.85f, deepWaterW);
                const float inlandAmp = lerp(0.08f, 0.20f, deepWaterW) + stormChop * 0.08f;
                const float ampScale = isExtW0 ? (isCoastalSea ? seaAmp : inlandAmp) : 0.09f;

                // Wave 38 Direct Continuous Aperiodic Wave Evaluation (ZERO 8-step raymarch bands, ZERO shoalPhase rings!):
                const float2 tideDrift = isCoastalSea ? (flowDir * (tideVel * wt * 14.0f)) : (flowDir * (wt * (6.5f + riverFlowSpeed * 12.0f)));
                const float2 evalXY = wpW.xy - tideDrift;
                float2 waveG = 0.0f.xx;
                jacobianFold = 0.0f;
                waveHeight3D = evalWaterWaveHeight(evalXY, wt, flowDir, flowPerp, ampScale, depthV0, gPhys, waveG, jacobianFold);

                // Smooth analytical multi-scale capillary ripples (incommensurately rotated rawNoise3Grad, zero hatching!):
                const float2 pCap1 = evalXY - flowDir * (wt * (4.8f + abs(tideVel) * 3.5f + riverFlowSpeed * 6.0f));
                const float2 pCap2 = float2(evalXY.x * 0.616f - evalXY.y * 0.788f, evalXY.x * 0.788f + evalXY.y * 0.616f)
                                   - float2(wt * 3.2f, -wt * 2.1f);
                const float4 cap1 = rawNoise3Grad(float3(pCap1 * 0.052f, wt * 0.52f + 13.7f));
                const float4 cap2 = rawNoise3Grad(float3(pCap2 * 0.128f, wt * 0.86f + 29.1f));
                const float2 gCap2W = float2(cap2.y * 0.616f + cap2.z * 0.788f, -cap2.y * 0.788f + cap2.z * 0.616f);
                const float nearRip = smoothstep(3800.0f, 160.0f, wDist) * (isCoastalSea ? ampScale : min(ampScale, 0.18f));
                waveG += (cap1.yz * 0.065f + gCap2W * 0.032f) * nearRip;

                // Wave 47 River-Current Directional Advection & Submerged-Rock Kelvin V-Wake Ripples:
                // Anisotropic downstream current facets + oblique +-26 deg V-wake chevrons around boulders/banks!
                if (!isCoastalSea && isExtW0 && wDist < 4200.0f) {
                    const float streamFade = smoothstep(4200.0f, 160.0f, wDist) * riverFlowSpeed;
                    const float2 pStr = float2(dot(wpW.xy, flowDir) - wt * (18.0f + riverFlowSpeed * 20.0f), dot(wpW.xy, flowPerp));
                    const float4 sRip = rawNoise3Grad(float3(pStr.x * 0.036f, pStr.y * 0.092f, wt * 0.74f + 41.3f));
                    const float2 gStrW = flowDir * (sRip.y * 0.036f) + flowPerp * (sRip.z * 0.092f);
                    waveG += gStrW * (1.15f * streamFade);
                    if (nearObs > 0.02f) {
                        // Oblique Kelvin V-wake arms (+-26 deg from river current) clinging downstream of submerged rocks:
                        const float2 vDirA = normalize(flowDir * 0.8988f + flowPerp * 0.4384f);
                        const float2 vDirB = normalize(flowDir * 0.8988f - flowPerp * 0.4384f);
                        const float4 vWakeA = rawNoise3Grad(float3(dot(wpW.xy, vDirA) * 0.145f - wt * 2.4f, dot(wpW.xy, float2(-vDirA.y, vDirA.x)) * 0.042f, wt * 0.62f + 7.9f));
                        const float4 vWakeB = rawNoise3Grad(float3(dot(wpW.xy, vDirB) * 0.145f - wt * 2.4f, dot(wpW.xy, float2(-vDirB.y, vDirB.x)) * 0.042f, wt * 0.62f + 23.1f));
                        const float2 gWakeW = vDirA * (vWakeA.y * 0.145f) + vDirB * (vWakeB.y * 0.145f);
                        const float wakeAmt = nearObs * (0.45f + 0.55f * max(bowHit, eddyLee)) * smoothstep(2600.0f, 140.0f, wDist);
                        waveG += gWakeW * (0.95f * wakeAmt);
                    }
                }
                // Grazing-angle anisotropic Nyquist normal filter: glassy reflective calm water & rich rolling ocean swells!
                const float grazingSmooth = smoothstep(0.04f, 0.42f, saturate(v.z));
                const float normalStrength = isCoastalSea ? lerp(0.45f, 0.92f, grazingSmooth) : lerp(0.28f, 0.64f, grazingSmooth);
                n = normalize(float3(-waveG.x * normalStrength, -waveG.y * normalStrength, 1.0f));
                // Dynamic player/actor wading Kelvin wake ripples & aerated foam churn (GTA VI @OtarieBambelle):
                float wakeFoamEnergy = 0.0f;
                if (wDist < 1600.0f) {
                    float2 wakeG = 0.0f.xx;
                    [loop] for (uint wi = 0u; wi < 32u; wi++) {
                        const float4 ft = footTrail[wi];
                        if (ft.w <= 0.01f) { continue; }
                        const float3 fd = wpW - ft.xyz;
                        const float r2 = dot(fd.xy, fd.xy);
                        if (r2 < 22500.0f && abs(fd.z) < 180.0f) {
                            const float rr = sqrt(r2);
                            const float envW = exp(-rr / 68.0f) * ft.w;
                            const float ring = sin(rr * 0.22f - wt * 2.45f) * envW;
                            wakeG += (fd.xy / max(rr, 1.0f)) * ring * 0.058f;
                            wakeFoamEnergy = max(wakeFoamEnergy, exp(-r2 / 900.0f) * ft.w * 0.78f);
                        }
                    }
                    n = normalize(n + float3(wakeG, 0.0f));
                }
                bowHit = max(bowHit, wakeFoamEnergy);
            }
            if (weather.w > 0.0f && wDist < 1200.0f) {   // delicate physical raindrop ripples (zero dark leopard-spot artifacts!)
                const float rainFade = smoothstep(1200.0f, 120.0f, wDist);
                const float2 g = rainRings(wpW.xy, camPosW.w) * (0.065f * weather.w * rainFade);
                n = normalize(n + float3(-g, 0.0f));
            }
            // Wave 40 Live 256x256 GPU Shallow-Water Equations (RawFluidWaterSim) Coupling:
            // Smoothly blends live GPU player/16-NPC wakes, raindrop impact rings, height ripples,
            // analytical curl-free normals, and advected webbed foam into RtWater when inside [0.02..0.98].
            float simFoamLive = 0.0f;
            if (fluidWaterDomain.w > 0.5f && fluidWaterDomain.z > 0.0f) {
                const float2 winUv = (wpW.xy - fluidWaterDomain.xy) * fluidWaterDomain.z;
                if (winUv.x > 0.02f && winUv.x < 0.98f && winUv.y > 0.02f && winUv.y < 0.98f) {
                    const float edgeFade = smoothstep(0.02f, 0.10f, winUv.x) * (1.0f - smoothstep(0.90f, 0.98f, winUv.x))
                                         * smoothstep(0.02f, 0.10f, winUv.y) * (1.0f - smoothstep(0.90f, 0.98f, winUv.y));
                    const float2 simUv = frac(wpW.xy * fluidWaterDomain.z);
                    const float4 swState = SimWaterState.SampleLevel(SimWaterSamp, simUv, 0.0f);
                    const float4 swNorm  = SimWaterNormal.SampleLevel(SimWaterSamp, simUv, 0.0f);
                    waveHeight3D += swState.x * (0.85f * edgeFade);
                    const float2 sweSlope = swNorm.xy / max(swNorm.z, 0.25f);
                    const float sweActivity = saturate(length(sweSlope) * 3.5f + abs(swState.x) * 0.45f + length(swState.yz) * 0.025f);
                    const float sweNormWeight = edgeFade * lerp(0.45f, 0.88f, sweActivity);
                    n = normalize(float3(n.xy + sweSlope * sweNormWeight, max(n.z, 0.15f)));
                    simFoamLive = saturate(max(swState.w, swNorm.w * 0.85f)) * edgeFade;
                    jacobianFold = max(jacobianFold, swNorm.w * edgeFade * 0.72f);
                    bowHit = max(bowHit, simFoamLive * 0.85f);
                }
            }
            const float nvRaw = dot(n, v);
            if (nvRaw < 0.05f) { n = normalize(n + v * (0.05f - nvRaw)); }   // a wave normal never faces away
            const float nv = saturate(dot(n, v));
            const float F = 0.02f + 0.98f * pow(1.0f - nv, 5.0f);
            const float2 rr = float2(hash(px, frameIndex * 0.291f + 5.0f), hash(px.yx, frameIndex * 0.733f + 2.0f));
            const float3 o = p + float3(0.0f, 0.0f, max(2.5f + waveHeight3D * 0.45f, 1.0f) + 0.003f * length(p));

            // Wave 33 Crisp Liquid Wave-Normal Reflections (0.74..0.32 relief gives genuine 3D ocean/river wave facets!)
            const float reflRelief = lerp(0.74f, 0.32f, saturate(wDist / 6500.0f));
            const float3 nRefl = normalize(lerp(float3(0.0f, 0.0f, 1.0f), n, reflRelief));
            float3 rd = reflect(-v, nRefl);
            if (rd.z < 0.04f) { rd = normalize(float3(rd.xy, 0.04f)); }
            float3 refl;
            // 5-Tap Deterministic Noise-Free Hardware DXR 1.2 RayQuery Water Reflections (zero per-frame random jitter!):
            // Center ray + 4 tight symmetric cardinal cone rays eliminate edge shimmer & reflection sparkle completely!
            if (reflectLength > 0.0f) {
                const bool mats = (giFlags & GI_MATERIALS) != 0u;
                const float3 farReflSky = waterSky(rd);
                const float reflCone = lerp(0.004f, 0.011f, saturate(wDist / 5000.0f));
                const float3 t1R = normalize(cross(rd, abs(rd.z) < 0.9f ? float3(0.0f, 0.0f, 1.0f) : float3(1.0f, 0.0f, 0.0f)));
                const float3 t2R = cross(rd, t1R);
                static const float2 kCross5[5] = {
                    float2( 0.0f,  0.0f), float2( 1.0f,  0.0f), float2(-1.0f,  0.0f),
                    float2( 0.0f,  1.0f), float2( 0.0f, -1.0f)
                };
                static const float kW5[5] = { 2.0f, 1.0f, 1.0f, 1.0f, 1.0f };
                float3 reflAcc = 0.0f.xxx;
                float reflW = 0.0f;
                [unroll] for (int ri = 0; ri < 5; ++ri) {
                    const float3 rdi = normalize(rd + (t1R * kCross5[ri].x + t2R * kCross5[ri].y) * reflCone);
                    Hit h;
                    float3 sCol;
                    if (traceHit(o, rdi, reflectLength, h)) {
                        const float3 hitPt = o + rdi * h.t;
                        const float3 hn = mats ? hitNormal(h, rdi) : -rdi;
                        sCol = mats ? shadeHit(hitPt, hn, hitAlbedo(h), float2(0.5f, 0.5f), true)
                                    : (0.25f * float3(ambient[0].w, ambient[1].w, ambient[2].w));
                        const float reflFog = 1.0f - exp(-h.t * 0.00016f);
                        sCol = lerp(max(sCol, farReflSky * 0.38f), farReflSky, saturate(reflFog));
                    } else {
                        sCol = farReflSky;
                    }
                    const float sLum = dot(sCol, float3(0.2126f, 0.7152f, 0.0722f));
                    const float kw = kW5[ri] / (1.0f + sLum * 0.45f);
                    reflAcc += sCol * kw;
                    reflW += kw;
                }
                refl = reflAcc / max(reflW, 1.0e-4f);
                const float reflMax = max(refl.r, max(refl.g, refl.b));
                if (reflMax > 4.0f) { refl *= 4.0f / reflMax; }
            } else {
                refl = waterSky(rd);
                const float waveTilt = saturate((1.0f - nRefl.z) * 18.0f);
                const float skyFace = saturate(dot(nRefl, normalize(v + float3(0.0f, 0.0f, 1.0f))));
                const float crestSheen = pow(skyFace, 36.0f) * 0.72f;
                refl = refl * lerp(0.60f, 1.36f, smoothstep(0.03f, 0.48f, rd.z)) * (1.0f - waveTilt * 0.28f)
                     + refl * crestSheen;
            }

            // Triple-lobe GGX sun glint driven by FULL crisp wave normal `n`:
            // tight mirror sparkle + mid sheen + broad grazing-angle golden glitter road (photos 1/2/4/8) cut sharply by DXR shadows!
            float3 glint = 0.0f;
            const float3 l = sunDir.xyz;
            const float nl = dot(n, l);
            if (sunDir.w > 0.5f && nl > 0.0f && waterA.w > 0.0f) {
                const float3 hv = normalize(v + l);
                const float nh = saturate(dot(n, hv));
                const float dd1 = nh * nh * (0.0035f - 1.0f) + 1.0f;
                const float dd2 = nh * nh * (0.0220f - 1.0f) + 1.0f;
                const float dd3 = nh * nh * (0.0750f - 1.0f) + 1.0f;
                const float lowSunRoad = saturate(1.0f - l.z * 2.2f);
                const float D = (0.0035f / (PI * dd1 * dd1)) * 0.62f
                              + (0.0220f / (PI * dd2 * dd2)) * 0.26f
                              + (0.0750f / (PI * dd3 * dd3)) * (0.12f + 0.34f * lowSunRoad);
                // Wave 47 Crisp Anisotropic River Sun-Glitter across moving cross-stream facets:
                const float3 rFlow3 = normalize(float3(riverFlowDir, 0.0f));
                const float3 rPerp3 = float3(-rFlow3.y, rFlow3.x, 0.0f);
                const float thR = dot(rFlow3, hv), bhR = dot(rPerp3, hv);
                const float anisoDen = max((thR * thR) / (0.042f * 0.042f) + (bhR * bhR) / (0.165f * 0.165f) + nh * nh, 1.0e-4f);
                const float dAnisoRiver = (1.0f / (PI * 0.042f * 0.165f * anisoDen * anisoDen)) * (riverFlowSpeed * 0.085f);
                const float Fh = 0.02f + 0.98f * pow(1.0f - saturate(dot(v, hv)), 5.0f);
                const float vis = sunColour.w > 0.5f ? visible(o, l, 0.0f, sunLength) : 1.0f;
                glint = min(sunColour.rgb * ((D + dAnisoRiver) * Fh * nl / (4.0f * max(nv, 0.08f))) * vis, 8.0f.xxx) * waterA.w;
            }
            // Dual-lobe elongated liquid specular reflections on water:
            // Softened indoors (sunColour.w < 0.5) so cisterns/caves show natural gentle lantern shimmer instead of white laser beams!
            if (wDist < 8500.0f && flags.y > 0u) {
                const bool isExtW = (sunColour.w > 0.5f);
                const float aTight = isExtW ? 0.014f : 0.042f;
                const float aBroad = isExtW ? 0.055f : 0.110f;
                const float gGain  = isExtW ? 0.12f  : 0.055f;
                const float gCap   = isExtW ? 1.85f  : 0.55f;
                const uint maxWaterLights = min(flags.y, 32u);
                [loop] for (uint wli = 0u; wli < maxWaterLights; wli++) {
                    const float3 toWL = lightPosR[wli].xyz - p;
                    const float dWL = length(toWL);
                    const float rWL = lightPosR[wli].w * 2.10f;
                    if (dWL >= rWL || toWL.z <= 0.0f) { continue; }
                    const float attWL = pow(saturate(1.0f - dWL / rWL), 1.85f);
                    const float3 lWL = toWL / max(dWL, 1.0e-4f);
                    const float3 hWL = normalize(v + lWL);
                    const float nhWL = saturate(dot(nRefl, hWL));
                    const float ddTight = nhWL * nhWL * (aTight - 1.0f) + 1.0f;
                    const float ddBroad = nhWL * nhWL * (aBroad - 1.0f) + 1.0f;
                    const float dSpec = (aTight / (PI * ddTight * ddTight)) * 0.45f + (aBroad / (PI * ddBroad * ddBroad)) * 0.55f;
                    glint += min(lightColS[wli].rgb * (dSpec * attWL * gGain * (0.25f + 0.75f * F)), gCap.xxx);
                }
            }

            // Wave 29: Physical Refracted Flux-Jacobian Chromatic Caustics + Negative-Space Dark Troughs (EXTERIORS ONLY):
            // Computes the live 2x2 wave-refracted sunlight footprint Jacobian J = |dX_sun/dx x dX_sun/dy| directly from
            // our 8-wave trochoidal + capillary normal field at two Cauchy-dispersed wavelengths (n_R=1.331, n_B=1.340).
            // Converging rays (J < J0) spike into blazing filaments; diverging rays (J > J0) carve physical dark troughs!
            float3 caus = 0.0f;
            float3 causTrough = 1.0f.xxx;
            if (dBed < 1.0f && sunColour.w > 0.5f && sunDir.w > 0.5f && waterA.y > 0.0f && l.z > 0.05f) {
                const float3 pb = worldAt(px, dBed);
                const float depthV = max(p.z - pb.z, 0.0f);
                const float3 sr = refract(-l, float3(0.0f, 0.0f, 1.0f), 1.0f / 1.333f);   // unrefracted flat-surface sun ray
                // Live neighbor-differential wave-normal refraction Jacobian across +-6u world footprint:
                const float2 wDirC = normalize(cloudWind.xy + float2(1.0f, 0.35f));
                const float2 wPerpC = float2(-wDirC.y, wDirC.x);
                const float wtC = camPosW.w;
                const float ampC = 0.68f + saturate(weather.w * 0.95f + saturate(length(cloudWind.xy) / 360.0f) * 0.85f) * 1.15f;
                const float epsW = 6.0f;
                float2 g0C = 0.0f.xx, gXC = 0.0f.xx, gYC = 0.0f.xx;
                float fDum = 0.0f;
                evalWaterWaveHeight(wpW.xy, wtC, wDirC, wPerpC, ampC, depthV, gPhys, g0C, fDum);
                evalWaterWaveHeight(wpW.xy + float2(epsW, 0.0f), wtC, wDirC, wPerpC, ampC, depthV, gPhys, gXC, fDum);
                evalWaterWaveHeight(wpW.xy + float2(0.0f, epsW), wtC, wDirC, wPerpC, ampC, depthV, gPhys, gYC, fDum);
                const float3 nC0 = normalize(float3(-g0C.x, -g0C.y, 1.0f));
                const float3 nCX = normalize(float3(-gXC.x, -gXC.y, 1.0f));
                const float3 nCY = normalize(float3(-gYC.x, -gYC.y, 1.0f));
                const float dEff = clamp(depthV, 2.0f, 180.0f);
                // Cauchy dispersion: red (620nm, n=1.331) vs green (540nm, n=1.334) vs blue (470nm, n=1.340)
                float3 jacFocus = 1.0f.xxx;
                static const float kInvIor[3] = { 1.0f / 1.331f, 1.0f / 1.334f, 1.0f / 1.340f };
                [unroll] for (int ci = 0; ci < 3; ci++) {
                    const float3 r0 = refract(-l, nC0, kInvIor[ci]);
                    const float3 rX = refract(-l, nCX, kInvIor[ci]);
                    const float3 rY = refract(-l, nCY, kInvIor[ci]);
                    const float2 h0 = r0.xy * (dEff / max(-r0.z, 0.25f));
                    const float2 hX = float2(epsW, 0.0f) + rX.xy * (dEff / max(-rX.z, 0.25f));
                    const float2 hY = float2(0.0f, epsW) + rY.xy * (dEff / max(-rY.z, 0.25f));
                    const float2 dX = hX - h0;
                    const float2 dY = hY - h0;
                    const float detJ = abs(dX.x * dY.y - dX.y * dY.x);
                    const float flux = clamp((epsW * epsW) / max(detJ, epsW * epsW * 0.22f), 0.38f, 3.6f);
                    if (ci == 0) jacFocus.r = flux;
                    else if (ci == 1) jacFocus.g = flux;
                    else jacFocus.b = flux;
                }
                // Wave 59: Physically-grounded shallow-water refractive RGB caustic shimmer (azimuth-aligned Cauchy dispersion
                // + high-frequency shallow capillary caustic harmonic + spectral Beer-Lambert depth transmittance).
                const float2 flowOff = (cloudWind.xy * 0.012f + nL.xy * 5.5f) * camPosW.w;
                const float3 at = camPosW.xyz + pb - sr * (depthV / max(-sr.z, 0.2f));
                const float t = camPosW.w * 0.42f;
                const float sc = max(waterB.y, 1.0f);
                const float2 srHorRaw = sr.xy;
                const float srHorLen = length(srHorRaw);
                const float2 srDir = (isfinite(srHorLen) && srHorLen > 1.0e-4f) ? (srHorRaw / srHorLen) : float2(0.96f, 0.28f);
                const float disp = clamp(depthV * 0.028f, 0.24f, 1.75f);
                const float2 cPos = at.xy + flowOff;
                const float3 net = float3(causticNet(cPos + srDir * disp, t, sc),
                                          causticNet(cPos, t, sc),
                                          causticNet(cPos - srDir * disp, t, sc));
                // Shallow-water capillary shimmer harmonic sparkles crisply over ankle-to-knee cobblestones (0..85u):
                const float shallowShimW = smoothstep(88.0f, 6.0f, depthV) * smoothstep(0.0f, 3.5f, depthV) * smoothstep(2600.0f, 180.0f, wDist);
                const float3 shimRGB = (shallowShimW > 0.01f)
                    ? float3(evalAperiodicCurlCaustic(cPos * 1.65f + srDir * (disp * 0.7f), t * 2.4f, sc * 0.65f),
                             evalAperiodicCurlCaustic(cPos * 1.65f,                          t * 2.4f, sc * 0.65f),
                             evalAperiodicCurlCaustic(cPos * 1.65f - srDir * (disp * 0.7f), t * 2.4f, sc * 0.65f)) * (shallowShimW * 0.42f)
                    : 0.0f.xxx;
                const float sunVis = sunColour.w > 0.5f ? visible(pb + float3(0.0f, 0.0f, 1.0f + 0.002f * length(pb)), l, 0.0f, sunLength) : 1.0f;
                const bool isSeaCaus = (abs((p.z + camPosW.z) - (-14000.0f)) < 450.0f);
                const float3 throughRGB = evalSpectralWaterTransmittance(depthV + length(pb - p), waterA.z * 0.55f, true, isSeaCaus, saturate(weather.w * 0.5f));
                const float focus = saturate(depthV / 22.0f) * exp(-min(depthV / 340.0f, 20.0f));
                const float clearSun = max(1.0f - saturate(clouds.x * 0.80f), 0.22f);
                // Negative-space caustic trough modulation (< 1.0 where rays diverge, > 1.0 where wave crests converge):
                const float troughFade = saturate(focus * sunVis * clearSun * (1.0f - smoothstep(2600.0f, 5200.0f, wDist)));
                causTrough = lerp(1.0f.xxx, clamp(jacFocus, 0.62f.xxx, 1.25f.xxx), troughFade * 0.65f);
                const float3 jacFilament = max(jacFocus - 1.0f.xxx, 0.0f.xxx) * 0.72f + net * 0.68f + shimRGB;
                caus = sanitizeWater3(BedAlbedo.Load(ip).rgb * sunColour.rgb * throughRGB * (l.z * sunVis * focus * waterA.y * 1.48f * clearSun) * jacFilament);
            }

            // Refraction through the bed's own colour, from the frame RAW lit before the game composited its water: the
            // bed seen through the surface, bent by the wave normal in proportion to the depth, dimmed per channel
            // (red first) and filled in by light the water scatters (its tint under the sky's ambient).
            // Wave 31 Continuous Deep-Water Optical Body (kills the razor-straight cell-boundary seam in 23.51.58.69.png
            // when an adjacent cell lacks riverbed depth dBed == 1.0 by evaluating continuous Beer-Lambert scatter!):
            const bool isExtBody0 = (sunColour.w > 0.5f);
            const float3 deepScatter0 = (isExtBody0 ? float3(0.015f, 0.025f, 0.031f) : float3(0.004f, 0.006f, 0.007f))
                                      * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), float3(0.035f, 0.045f, 0.062f))
                                      * (isExtBody0 ? 1.15f : 0.65f);
            const float waveFacet0 = lerp(0.74f, 1.26f, saturate(dot(n, normalize(float3(0.35f, 0.25f, 0.90f)))));
            const float troughW0 = lerp(0.86f, 1.14f, saturate(0.45f + jacobianFold * 0.65f));
            float3 body = deepScatter0 * (waveFacet0 * troughW0);
            const bool refr = skyLength > 0.5f && dBed < 1.0f && waterB.z > 0.0f;
            if (refr) {
                const float3 pb = worldAt(px, dBed);
                const float depthV = max(p.z - pb.z, 0.0f);
                // True Snell's Law optical underwater refraction (n_air/n_water = 1/1.333):
                // bends view ray into the water column and intersects the riverbed plane at depthV.
                // Disciplined Snell's Law underwater refraction (max 10px offset, scaled smoothly by shallow depth):
                // keeps submerged riverbed pebbles & cobblestones razor-sharp instead of smeared vaseline blur.
                const float3 refrRay = refract(-v, n, 1.0f / 1.333f);
                const float tRefr = min(depthV / max(-refrRay.z, 0.30f), 48.0f);
                const float3 pRefrBed = p + refrRay * tRefr;
                const float4 s2 = screenOf(lerp(pb, pRefrBed, 0.62f));
                float2 qOff = s2.w > 0.0f ? (s2.xy - float2(id.xy)) : 0.0f.xx;
                const float qLen = length(qOff);
                if (qLen > 14.0f) { qOff *= (14.0f / qLen); }
                // Fade refraction offset smoothly to 0 near screen borders (eliminates bottom-edge vertical smear bar
                // seen in 10.51.17.92 & 10.51.15.20!):
                const float borderDist = min(min(float(id.x), dims.x - 1.0f - float(id.x)), min(float(id.y), dims.y - 1.0f - float(id.y)));
                const float borderFade = smoothstep(0.0f, 32.0f, borderDist);
                const float2 qF = clamp(float2(id.xy) + qOff * (saturate(depthV / 28.0f) * borderFade), float2(1.0f, 1.0f), dims.xy - 2.0f);
                int2 q = int2(round(qF));
                const float4 Lq = Layer.Load(int3(q, 0));
                const float dQ = Depth.Load(int3(q, 0));
                const bool qValid = (Lq.a > 1.5f && dQ > Lq.x + 1.0e-5f);
                const float2 qUse = qValid ? qF : float2(id.xy);
                const int2 q0 = clamp(int2(floor(qUse)), int2(0, 0), int2(dims.xy) - 2);
                const float2 qFrac = frac(qUse);
                const float3 b00 = PreWater[q0].rgb, b10 = PreWater[q0 + int2(1, 0)].rgb;
                const float3 b01 = PreWater[q0 + int2(0, 1)].rgb, b11 = PreWater[q0 + int2(1, 1)].rgb;
                float3 bed = lerp(lerp(b00, b10, qFrac.x), lerp(b01, b11, qFrac.x), qFrac.y) * causTrough;
                // Physical 3-Band Cauchy Optical Dispersion (surpassing Blender 5.3 spectral refraction in real time):
                // Splits red (n=1.331) and blue (n=1.340) refracted paths across steep wave facets & shallow riverbeds!
                if (qValid && wDist < 1800.0f && qLen > 0.65f) {
                    const float dispPx = clamp(qLen * 0.14f, 0.0f, 2.2f) * borderFade;
                    const float2 dispDir = qOff / max(qLen, 1.0e-3f);
                    const int2 qR = clamp(int2(round(qUse - dispDir * dispPx)), int2(0, 0), int2(dims.xy) - 1);
                    const int2 qB = clamp(int2(round(qUse + dispDir * dispPx)), int2(0, 0), int2(dims.xy) - 1);
                    const bool okDisp = (Layer.Load(int3(qR, 0)).a > 1.5f) && (Layer.Load(int3(qB, 0)).a > 1.5f);
                    if (okDisp) {
                        const float3 dispBed = float3(PreWater[qR].r, bed.g, PreWater[qB].b);
                        bed = lerp(bed, dispBed, saturate(0.55f * (1.0f - wDist / 1800.0f)));
                    }
                }
                const int2 qDepthPx = qValid ? q : int2(id.xy);
                const float path = length(worldAt(float2(qDepthPx), Depth.Load(int3(qDepthPx, 0))) - p);
                // Wave 34 + Wave 59 Pope & Fry (1997) Spectral Beer-Lambert Water Extinction + Turbidity Coupling:
                // Red light dies ~5.5x faster than blue: shallow stones stay golden-clear, mid-depth turns rich translucent
                // Nordic jade-emerald, and deep pools drop into dark Atlantic sapphire-indigo!
                const bool isExtBody = (sunColour.w > 0.5f);
                const bool isSeaFoam = (isExtBody && abs((p.z + camPosW.z) - (-14000.0f)) < 450.0f);
                const float waterTurbidity = saturate(weather.w * 0.52f + riverFlowSpeed * 0.25f * (1.0f - smoothstep(10.0f, 120.0f, depthV)));
                const float3 T = evalSpectralWaterTransmittance(path, waterA.z, isExtBody, isSeaFoam, waterTurbidity);
                const float3 scatterTint = isExtBody
                    ? lerp(float3(0.008f, 0.032f, 0.044f), float3(0.012f, 0.034f, 0.038f), waterTurbidity * 0.5f)
                    : float3(0.003f, 0.006f, 0.008f);
                const float3 scatter = scatterTint * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), float3(0.035f, 0.045f, 0.062f)) * (isExtBody ? 1.25f : 0.65f);
                const float nearW = 1.0f - smoothstep(3200.0f, 6400.0f, wDist);
                // Physical Backlit & Top-Lit Translucent Jade-Emerald Wave-Crest Subsurface Scattering:
                // Sunlight transmits vividly through thin upper wave ridges (waveHeight3D > 0) while troughs stay deep indigo!
                const float crestThin = saturate(waveHeight3D * 0.12f + (1.0f - n.z) * 4.2f);
                const float sssVisW = (isExtBody && sunDir.w > 0.5f) ? (0.30f + 0.70f * pow(saturate(dot(-v, sunDir.xyz)), 2.8f) + 0.35f * saturate(dot(n, sunDir.xyz))) : 0.0f;
                const float3 waveSSS = (isExtBody && sunDir.w > 0.5f)
                    ? float3(0.018f, 0.165f, 0.145f) * sunColour.rgb * (sssVisW * crestThin * (1.0f - saturate(clouds.x * 0.72f)) * 0.68f)
                    : 0.0f.xxx;
                // Wave 38 + Wave 59 Organic Cellular Lacy Foam + Schlick Fresnel Wind-Capillary Foam Breakup:
                // Sparse wind-blown ocean whitecaps (never parallel zebra stripes!) + organic noise-modulated shoreline
                // & boulder foam collars (never depth-contour rings!) feathered by wind-capillary breakup!
                const float wtF = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
                const float2 foamFlow = normalize(cloudWind.xy + float2(1.0f, 0.35f));
                const float2 foamPerp = float2(-foamFlow.y, foamFlow.x);
                const float windSpeedW = saturate(length(cloudWind.xy) / 340.0f + weather.w * 0.45f + saturate((beaufort - 2.0f) / 7.0f) * 0.65f);
                // Aperiodic anisotropic wind-capillary breakup tears foam edges into organic wisps under strong gusts:
                const float2 wCapP = float2(dot(wpW.xy, foamFlow) * 0.018f - wtF * (0.28f + windSpeedW * 0.45f), dot(wpW.xy, foamPerp) * 0.056f);
                const float windCapBreak = smoothstep(0.24f, 0.78f, rawNoise3Grad(float3(wCapP, wtF * 0.24f + 19.7f)).x);
                // Sparse organic sea-crest foam strictly on steep patch-masked aperiodic crests in choppy sea states:
                const float crestBreak = isSeaFoam
                    ? saturate((jacobianFold - lerp(0.54f, 0.28f, windSpeedW)) * 2.25f) * smoothstep(24.0f, 110.0f, depthV)
                      * (0.35f + 0.65f * windSpeedW) * lerp(0.55f, 1.15f, windCapBreak)
                    : 0.0f;
                // Organic aperiodic shoreline & rock contact collar (modulated by 3D noise instead of periodic sine!):
                // Wave 47 Wet-Shoreline Capillary Meniscus Darkening, Contact Fresnel Glint & Delicate Lacy Foam Cling
                // strictly within shallow water-to-rock/bank contacts (waterDepth in [0.0..0.45m] = [0..31.5u]):
                const float depthM = depthV * (1.0f / 70.0f);
                const float shoreJitter = rawNoise3Grad(float3(wpW.xy * 0.022f, wtF * 0.28f)).x;
                const float shoreFine   = rawNoise3Grad(float3(wpW.xy * 0.085f - riverFlowDir * (wtF * 0.45f), wtF * 0.36f + 11.3f)).x;
                const float meniscusRangeM = 0.45f * (0.68f + 0.64f * shoreJitter);
                const float meniscusMask = (1.0f - smoothstep(0.015f, meniscusRangeM, depthM)) * smoothstep(0.0f, 0.008f, depthM);
                const float capillaryRim = (1.0f - smoothstep(0.0f, 0.11f * (0.62f + 0.76f * shoreFine), depthM)) * smoothstep(0.0f, 0.004f, depthM);
                const float collarReach = (isSeaFoam ? 6.8f : (2.8f + riverFlowSpeed * 2.4f)) * (0.55f + 0.90f * shoreJitter);
                const float contactCollar = (1.0f - smoothstep(0.0f, collarReach, depthV)) * smoothstep(0.0f, 0.30f, depthV);
                // Break up micro-foam cling organically with flow speed, boulder deflection, wind capillary & SWE wake foam:
                const float clingBreakup = smoothstep(0.26f, 0.78f, shoreFine * 0.54f + shoreJitter * 0.28f + windCapBreak * 0.18f + riverFlowSpeed * 0.20f);
                const float microFoamCling = contactCollar * clingBreakup * (isSeaFoam ? 0.68f : lerp(0.28f, 0.58f, riverFlowSpeed));
                const float foamEnergy = saturate(max(max(crestBreak, microFoamCling), max(max(bowHit * 0.66f, eddyLee * 0.40f), simFoamLive * 0.92f)));
                const float2 foamAdvect = isSeaFoam ? (foamFlow * (wtF * 8.0f)) : (riverFlowDir * (wtF * (7.5f + riverFlowSpeed * 9.5f)));
                const float foamLace = foamEnergy > 0.02f ? cellularFoamLace(wpW.xy - foamAdvect, wtF, foamEnergy) : 0.0f;
                const float foamMask = saturate(max(foamLace * smoothstep(0.07f, 0.54f, foamEnergy), simFoamLive * (0.48f + 0.52f * foamLace))) * nearW;
                // True Underwater Volumetric Sunbeam Shafts (EXTERIORS ONLY; never fire cyan sunbeams inside underground sewers!).
                float3 uwShaft = 0.0f.xxx;
                if (isExtBody && sunDir.w > 0.5f && sunDir.z > 0.08f && depthV > 6.0f && wDist < 4200.0f) {
                    const float clearW = 1.0f - saturate(clouds.x * 0.78f);
                    const float cosUw = dot(-v, sunDir.xyz);
                    const float phUw = 0.45f + 0.55f * pow(saturate(cosUw * 0.5f + 0.5f), 2.5f);
                    const float dRay = min(path, 260.0f);
                    const float stepUw = dRay * (1.0f / 6.0f);
                    float uwAcc = 0.0f;
                    [unroll] for (int ui = 0; ui < 6; ui++) {
                        const float tu = (float(ui) + 0.5f) * stepUw;
                        const float3 pu = p - v * tu;
                        const float dCol = max(p.z - pu.z, 0.0f);
                        uwAcc += exp(-min(waterA.z * 0.85f * (tu + dCol), 24.0f)) * stepUw;
                    }
                    uwShaft = sunColour.rgb * float3(0.04f, 0.14f, 0.16f) * (uwAcc * 0.0022f * phUw * clearW);
                }
                const float foamSun = (isExtBody && sunDir.w > 0.5f) ? saturate(dot(n, sunDir.xyz) * 0.55f + 0.45f) : 0.0f;
                const float3 foamDiffuse = (max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.06f.xxx) * 0.92f + sunColour.rgb * (foamSun * 0.55f)) * float3(0.95f, 0.98f, 1.00f);
                // Schlick Fresnel grazing water-film glaze on aerated foam bubbles (prevents flat chalky white foam at low angles):
                const float foamFresnel = saturate(0.0204f + 0.9796f * pow(1.0f - nv, 5.0f));
                const float3 foamLit = lerp(foamDiffuse, sanitizeWater3(refl) * 0.78f + foamDiffuse * 0.30f, foamFresnel * 0.44f);
                // Capillary meniscus wet-rock darkening right at [0..0.45m] shoreline contact (smooth & noise-broken):
                const float shoreDark = lerp(1.0f, lerp(0.56f, 0.76f, shoreJitter), meniscusMask) * lerp(0.84f, 1.0f, smoothstep(0.0f, 24.0f, depthV));
                // Sharp Fresnel specular glint right along the concave capillary meniscus contact edge:
                float3 meniscusGlint = 0.0f.xxx;
                if (capillaryRim > 0.01f && wDist < 2800.0f) {
                    const float3 nMen = normalize(n + float3(-obsGrad * (0.44f * capillaryRim), 0.14f));
                    const float nvMen = saturate(dot(nMen, v));
                    const float fMen = 0.025f + 0.975f * pow(1.0f - nvMen, 4.2f);
                    const float menFade = capillaryRim * smoothstep(2800.0f, 140.0f, wDist) * (0.55f + 0.45f * shoreFine);
                    float menSpec = 0.0f;
                    if (isExtBody && sunDir.w > 0.5f && dot(nMen, l) > 0.02f) {
                        const float3 hMen = normalize(v + l);
                        const float nhMen = saturate(dot(nMen, hMen));
                        menSpec = pow(nhMen, 140.0f) * 0.65f * saturate(dot(nMen, l));
                    }
                    meniscusGlint = (refl * (fMen * 0.48f) + sunColour.rgb * (menSpec * fMen)) * menFade * (1.0f - foamMask * 0.75f);
                }
                // Heavy liquid wave-crest vs trough ambient self-shadowing (makes rolling mass & wave weight unmistakable!):
                const float waveFacet = lerp(0.68f, 1.28f, saturate(dot(n, normalize(float3(0.35f, 0.25f, 0.90f)))));
                const float troughWeight = lerp(0.80f, 1.16f, saturate(0.42f + jacobianFold * 0.68f));
                const float3 waterBody = (((bed * shoreDark + caus) * T + scatter * (1.0f - T)) * (waveFacet * troughWeight) + waveSSS + uwShaft + meniscusGlint);
                body = lerp(waterBody, foamLit, foamMask * 0.84f);
            } else if (simFoamLive > 0.02f) {
                const float3 foamLitFar = (max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.06f.xxx) * 0.92f + sunColour.rgb * (sunDir.w > 0.5f ? 0.38f : 0.0f)) * float3(0.95f, 0.98f, 1.00f);
                body = lerp(body, foamLitFar, saturate(simFoamLive * 0.78f));
            }
            const float shoreFeather = dBed < 1.0f ? smoothstep(0.0f, 8.0f, max(p.z - worldAt(px, dBed).z, 0.0f)) : 1.0f;
            // Continuous single world-space fluid surface from boots to horizon (extends full JONSWAP + RT water out to 42,000u!):
            const float cellFeather = 1.0f - smoothstep(18000.0f, 42000.0f, wDist);
            // Physical Schlick Fresnel + arctic night-sky grazing lift (prevents Windhelm harbor from turning into flat black ink!):
            const float nightF = smoothstep(0.04f, -0.06f, astroSunElev());
            const float effF = saturate(max(F, lerp(0.04f, 0.24f, nightF)) * max(waterA.x, 0.78f) * shoreFeather * cellFeather);
            const uint view = uint(waterB.x + 0.5f);
            const float3 farSkyRefl = waterSky(rd);
            // Deep cold Nordic slate-navy ocean body (keeps liquid depth at night instead of pitch-black void):
            const float3 deepOceanBody = float3(0.016f, 0.028f, 0.042f) * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), float3(0.08f, 0.10f, 0.13f)) * 1.45f;
            const float farF = saturate(max(F * max(waterA.x, 0.85f) * 0.90f, smoothstep(5500.0f, 36000.0f, wDist) * 0.68f));
            const float3 farOcean = lerp(deepOceanBody, farSkyRefl, farF);
            // Wave 33: ALWAYS use continuous Beer-Lambert `body` even when `!refr` (dBed == 1.0 in adjacent distant cell!)
            // so rivers & lakes NEVER draw a hard cell-boundary seam against `game.rgb`!
            const float3 nearCol = lerp(body, refl, effF) + (refr ? 0.0f.xxx : caus * (1.0f - effF));
            col = lerp(farOcean, nearCol, cellFeather) + glint * shoreFeather;
            // Wave 39 Live Freezing-Degree-Day Winter Shoreline & Shallow-Lake Ice Crust (RawSeasonsThermals.hlsli):
            // Forms crisp static IOR 1.31 crystalline ice shelves along frozen shores/lakes with organic spring fracture
            // leads and open thawed water near active dock fires/braziers (ZERO Voronoi polygons, ZERO pulsing!).
            if (sunColour.w > 0.5f && seasonThermal1.x > 0.02f && wDist < 16000.0f) {
                float fireHeatW = 0.0f;
                if (wDist < 3600.0f && flags.y > 0u) {
                    const uint maxWFire = min(flags.y, 16u);
                    [loop] for (uint wfi = 0u; wfi < maxWFire; ++wfi) {
                        const float3 flc = lightColS[wfi].rgb;
                        if (flc.r > flc.b * 1.25f && flc.r > 0.18f) {
                            const float rF = min(lightPosR[wfi].w * 0.50f, 280.0f);
                            fireHeatW = max(fireHeatW, evalFireThermalMeltMask(p, lightPosR[wfi].xyz, rF, saturate(flc.r * 0.85f)));
                        }
                    }
                }
                const float calmIceGate = 1.0f - smoothstep(5.0f, 10.0f, beaufort) * smoothstep(20.0f, 110.0f, depthV0);
                const float freezeMask = evalWaterFreezeMask(wpW, depthV0, seasonThermal0, seasonThermal1, fireHeatW)
                                       * calmIceGate * smoothstep(16000.0f, 900.0f, wDist);
                if (freezeMask > 0.01f) {
                    // 100% static world-locked glacial ice micro-relief & strata (zero time modulation, zero Voronoi!):
                    const float4 iceG = rawNoise3Grad(wpW * 0.065f + float3(5.3f, 11.7f, 2.9f));
                    const float  iceStrata = rawNoise3Grad(wpW * 0.014f + float3(19.1f, 3.7f, 8.3f)).x;
                    const float3 nCrust = normalize(float3(-iceG.y * 0.075f, -iceG.z * 0.075f, 1.0f));
                    const float  nvIce = saturate(dot(nCrust, v));
                    const float  fIce = 0.018f + 0.982f * pow(1.0f - nvIce, 5.0f);
                    // Deep Beer-Lambert cobalt-turquoise translucent ice crust tinted over underwater body + frost rime:
                    const float3 ambIce = max(ambientAt(float3(0.0f, 0.0f, 1.0f)), float3(0.06f, 0.08f, 0.11f));
                    const float3 sunIce = (sunDir.w > 0.5f) ? (sunColour.rgb * saturate(dot(nCrust, sunDir.xyz) * 0.65f + 0.35f) * 0.52f) : 0.0f.xxx;
                    const float3 crustBase = lerp(float3(0.24f, 0.44f, 0.58f), float3(0.54f, 0.72f, 0.84f), iceStrata)
                                           * (ambIce * 0.85f + sunIce);
                    const float  rimeCov = saturate(seasonThermal1.z * (0.45f + 0.55f * iceG.x) * 0.55f);
                    const float3 crustBody = lerp(lerp(body * 0.55f + crustBase * 0.65f, crustBase, 0.68f),
                                                  float3(0.76f, 0.84f, 0.92f) * (ambIce + sunIce), rimeCov);
                    // Crisp IOR 1.31 Fresnel sky reflection + tight sun glaze:
                    float3 iceSpec = 0.0f.xxx;
                    if (sunDir.w > 0.5f && dot(nCrust, sunDir.xyz) > 0.0f) {
                        const float3 hIce = normalize(v + sunDir.xyz);
                        const float  nhIce = saturate(dot(nCrust, hIce));
                        const float  aIce = lerp(0.016f, 0.085f, rimeCov);
                        const float  ddIce = nhIce * nhIce * (aIce - 1.0f) + 1.0f;
                        const float  dIce = aIce / (PI * ddIce * ddIce);
                        iceSpec = min(sunColour.rgb * (dIce * fIce * saturate(dot(nCrust, sunDir.xyz)) * 0.14f), 2.5f.xxx);
                    }
                    const float3 iceCol = lerp(crustBody, refl, saturate(fIce * lerp(0.88f, 0.35f, rimeCov))) + iceSpec;
                    col = lerp(col, iceCol, saturate(freezeMask * 0.92f));
                }
            }
            // 3x3 Sub-Pixel Water-Silhouette Coverage Anti-Aliasing + Wet-Shoreline Feather:
            // Eliminates razor-jagged 1-pixel stairstep edges where floating Windhelm ice floes & piers jut out of deep water!
            float edgeCov = 1.0f;
            if (wDist < 9000.0f) {
                const int2 limE = int2(dims.xy) - 1;
                float wHits = 0.0f;
                [unroll] for (int ey = -1; ey <= 1; ++ey) {
                    [unroll] for (int ex = -1; ex <= 1; ++ex) {
                        if (ex == 0 && ey == 0) continue;
                        const int3 np = int3(clamp(ip.xy + int2(ex, ey), int2(0, 0), limE), 0);
                        const float4 Ln = Layer.Load(np);
                        const float dBn = Depth.Load(np);
                        if (Ln.a > 1.5f && Ln.x < 1.0f && Ln.x < dBn - 1.0e-5f) { wHits += 1.0f; }
                    }
                }
                edgeCov = smoothstep(1.0f, 7.5f, wHits);
            }
            const float3 edgeBg = hasPreWater ? PreWater[id.xy].rgb : game.rgb;
            const float shoreBlend = min(smoothstep(0.0f, 7.0f, depthV0), lerp(0.28f, 1.0f, edgeCov));
            col = lerp(edgeBg, col, shoreBlend);
            // Daylight Cluster D (img-003 68-77, BUILD-LIST #15): Physical Waterline Split-Surface Meniscus Band
            // when the camera lens grazes the exact water plane (abs(eyeW.z - p.z) < 14u and wDist < 220u):
            const float eyeWaterGap = abs(eyeW.z - p.z);
            if (eyeWaterGap < 14.0f && wDist < 220.0f) {
                const float meniscusBand = exp(-pow(eyeWaterGap / 3.2f, 2.0f)) * smoothstep(220.0f, 24.0f, wDist);
                const float3 meniscusTint = lerp(float3(0.015f, 0.032f, 0.042f), farSkyRefl * 0.65f, F);
                col = lerp(col, meniscusTint, meniscusBand * 0.42f);
            }
            // Exact 16-bit horizon merge: at far ocean distances (wDist > 5000u), PreWater[id.xy].rgb holds
            // LightFog.hlsl's exact sky dome + cloud color at that pixel (Hillaire AtmoSkyView / overcast / sunset / night).
            // Merging far ocean directly into PreWater guarantees 0.0000 luminance discontinuity at the Sea of Ghosts horizon!
            if (wDist > 5000.0f) {
                const float3 horSky = hasPreWater ? PreWater[id.xy].rgb : waterSky(normalize(float3(rd.xy, 0.025f)));
                const float seaFog = 1.0f - exp(-max(wDist - 5000.0f, 0.0f) * 0.000065f);
                col = lerp(col, horSky, saturate(seaFog * 0.98f));
            }
            if (view == 1u) { col = refl; }
            else if (view == 2u) { col = caus * 4.0f; }
            else if (view == 3u) { col = float3(F, nv, 1.0f); }
            else if (view == 4u) {   // what the bed gives, in hues the tone map cannot turn: red no bed behind the water, green a bed
                const float dv = dBed < 1.0f ? p.z - worldAt(px, dBed).z : -1.0f;
                col = dv < 1.0f ? float3(1.0f, 0.0f, 0.0f) : float3(0.0f, saturate(dv / 200.0f), 1.0f - saturate(dv / 200.0f));
            }
            else if (view == 6u) { col = body; }   // the water's body alone: the bent bed, dimmed and filled in
            else if (view == 5u) { col = F > 0.25f ? float3(1.0f, 0.0f, 0.0f) : F > 0.06f ? float3(0.0f, 1.0f, 0.0f) : float3(0.0f, 0.0f, 1.0f); }
        } else {
            // Wave 42 Exact Physical Underwater Snell's Window (48.6 deg critical angle, sin(theta_c) = 1 / 1.333):
            // Outside the critical cone: exact Total Internal Reflection of the aqueous Nordic depth & underside foam.
            // Inside Snell's Window: exact water->air dielectric Fresnel transmission into refracted sky & sun disc!
            const float3 vUp = normalize(p - eyeW);
            const float3 wpU = camPosW.xyz + p;
            const float wtU = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
            // Smooth aperiodic underside wave normal via analytical rawNoise3Grad (zero periodic grid!):
            const float4 uWave1 = rawNoise3Grad(float3(wpU.xy * 0.032f + float2(wtU * 0.35f, -wtU * 0.22f), wtU * 0.42f));
            const float4 uWave2 = rawNoise3Grad(float3(wpU.xy * 0.084f + float2(-wtU * 0.28f, wtU * 0.31f), wtU * 0.65f + 13.7f));
            const float2 uwSlope = (uWave1.yz * 0.14f + uWave2.yz * 0.065f);
            const float3 nDown = normalize(float3(uwSlope.x, uwSlope.y, -1.0f));

            // Exact Snell's Law (water n1 = 1.333 -> air n2 = 1.0): sin(theta_t) = 1.333 * sin(theta_i)
            const float kWaterIor = 1.333f;
            const float cosI = saturate(dot(-vUp, nDown));
            const float sin2I = max(1.0f - cosI * cosI, 0.0f);
            const float sin2T = sin2I * (kWaterIor * kWaterIor);
            // Exact unpolarized water->air Fresnel reflectance (1.0 identically for theta_i >= 48.6 deg TIR!):
            float F_uw = 1.0f;
            float3 refrSky = 0.0f.xxx;
            if (sin2T < 0.9999f) {
                const float cosT = sqrt(max(1.0f - sin2T, 0.0f));
                const float rs = (kWaterIor * cosI - cosT) / max(kWaterIor * cosI + cosT, 1.0e-5f);
                const float rp = (cosI - kWaterIor * cosT) / max(cosI + kWaterIor * cosT, 1.0e-5f);
                F_uw = saturate(0.5f * (rs * rs + rp * rp));
                const float3 refrDir = refract(vUp, nDown, kWaterIor);
                const float3 skyRd = (dot(refrDir, refrDir) > 1.0e-4f) ? normalize(refrDir) : float3(0.0f, 0.0f, 1.0f);
                refrSky = waterSky(float3(skyRd.xy, max(skyRd.z, 0.02f)));
                // Direct solar aureole & sun disc refracted sharply through Snell's Window:
                if (sunDir.w > 0.5f && sunDir.z > 0.02f) {
                    const float cosSun = saturate(dot(skyRd, normalize(sunDir.xyz)));
                    const float sunBloom = pow(cosSun, 18.0f) * 0.65f + pow(cosSun, 240.0f) * 4.5f;
                    refrSky += sunColour.rgb * (sunBloom * (1.0f - saturate(clouds.x * 0.78f)));
                }
            }

            const float snellOpen = 1.0f - F_uw;
            const float3 uwAmb = max(ambientAt(float3(0.0f, 0.0f, 1.0f)), float3(0.14f, 0.18f, 0.22f));
            const float3 sunFill = (sunDir.w > 0.5f) ? (sunColour.rgb * 0.36f) : 0.0f.xxx;
            const float uwCaust = evalAperiodicCurlCaustic(wpU.xy, wtU, 42.0f) * snellOpen * 0.45f;
            const float underFoam = cellularFoamLace(wpU.xy, wtU, 0.42f) * 0.36f;
            const float3 tirCol = float3(0.022f, 0.088f, 0.122f) * (uwAmb * 2.2f + sunFill) + uwAmb * (underFoam * 0.62f);
            // Calibrated Pope & Fry red-first underwater Beer-Lambert extinction along ray to surface:
            const float uwDist = length(p - eyeW);
            const float3 uwBeer = exp(-uwDist * float3(0.0036f, 0.0011f, 0.00068f));
            const float3 uwFogCol = float3(0.015f, 0.068f, 0.095f) * (uwAmb * 1.85f + sunFill * 0.45f);
            const float3 surfCol = lerp(refrSky + sunFill * uwCaust + uwAmb * (underFoam * 0.45f), tirCol, F_uw);
            const float3 uwSurf = lerp(uwFogCol, surfCol, uwBeer);
            const float grazingFade = smoothstep(2.0f, 18.0f, abs(p.z - eyeW.z));
            col = lerp(lerp(refrSky * 0.55f + uwFogCol * 0.45f, uwFogCol, F_uw * 0.65f), uwSurf, grazingFade);
        }
    }
    WaterOut[id.xy] = float4(sanitizeWater3(col), isfinite(game.a) ? saturate(game.a) : 1.0f);
}
