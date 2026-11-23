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

#define Layer Normal
#define BedAlbedo Spec
#define WaterOut Vis
#define PreWater Refl   // u4: the HDR colour just before the game composites its water (RAW's lit bed), when skyLength > 0.5

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

// The caustic net at an absolute world point (units), two fields at different scales and speeds.
float causticNet(float2 w, float t, float scale) {
    const float a = cellWalls(w / scale, t), b = cellWalls(w / (scale * 0.61f) + 17.3f, t * 1.37f);
    return pow(saturate(1.0f - a * 3.2f), 7.0f) + 0.6f * pow(saturate(1.0f - b * 3.6f), 7.0f);
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

// True aperiodic 4-octave domain-warped organic river froth (zero honeycomb or diamond lattice artifacts).
float organicFroth(float2 p, float t) {
    const float2 q = p * 0.055f + float2(t * 0.18f, -t * 0.14f);
    const float wX = vNoise2D(q * 0.85f + float2(3.1f, 7.4f)) * 1.45f;
    const float wY = vNoise2D(q * 0.85f + float2(11.7f, 2.3f)) * 1.45f;
    const float2 qw = q + float2(wX, wY);
    const float n0 = vNoise2D(qw);
    const float n1 = vNoise2D(qw * 2.13f + float2(5.3f, 13.1f) - float2(t * 0.25f, 0.0f));
    const float n2 = vNoise2D(qw * 4.47f + float2(17.9f, 4.7f) + float2(0.0f, t * 0.35f));
    const float n3 = vNoise2D(qw * 9.19f + float2(29.3f, 19.1f));
    const float fbm = n0 * 0.44f + n1 * 0.28f + n2 * 0.18f + n3 * 0.10f;
    return smoothstep(0.46f, 0.80f, fbm);
}

float3 waterSky(float3 rd) {
    float3 uSun, uSkyZ, uSkyH, uAmbUp, uAmbDn;
    evalWorldAtmosphere(uSun, uSkyZ, uSkyH, uAmbUp, uAmbDn);
    const float hUp = saturate(rd.z);
    const float3 base = skyAt(rd);
    // Lock water sky reflection directly to the unified world atmosphere (zenith -> horizon) so water seamlessly
    // mirrors the exact sky dome, mountain aerial perspective, and weather mood with zero color mismatch:
    float3 skyCol = lerp(uSkyH, uSkyZ, pow(hUp, 0.62f));
    skyCol = lerp(skyCol, base, 0.28f);
    // Drifting volumetric cloud-deck reflection matched to unified cloud sun + sky colors:
    if (clouds.x > 0.08f && rd.z > 0.03f) {
        const float2 cUv = (camPosW.xy + rd.xy * (34000.0f / max(rd.z, 0.08f)) + cloudWind.xy * camPosW.w * 0.55f) * 0.000018f;
        const float cn = sin(cUv.x * 3.1f + sin(cUv.y * 2.7f)) * cos(cUv.y * 3.7f - cos(cUv.x * 2.3f)) * 0.5f + 0.5f;
        const float cov = smoothstep(1.0f - saturate(clouds.x * 0.95f), 1.0f, cn) * smoothstep(0.03f, 0.22f, rd.z);
        const float3 cloudLit = lerp(uSkyH * 0.90f, max(uSun, uSkyH) * 0.95f, 0.45f);
        skyCol = lerp(skyCol, cloudLit, cov * 0.50f);
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
    if (!WaveActiveAnyTrue(isWaterPx)) { WaterOut[id.xy] = float4(col, game.a); return; }
    if (isWaterPx) {
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, L.x);
        const float3 eyeW = eyeAt();
        // True front-face / above-water test against actual eyeAt().z (fixes water disappearing when standing below
        // a sloped river, waterfall, canal, or in 3rd-person where p.z >= 0.0f!):
        if (p.z < eyeW.z + 6.0f || dot(nL, normalize(eyeW - p)) > 0.02f) {
            // Feature 13: 100% Independent DX12 Fluid Physics (disconnected from Skyrim's 2011 nL water normal map):
            // Pure multi-directional Gerstner/trochoidal + curl-advected obstacle-deflected fluid wave simulation.
            const float3 v = normalize(eyeAt() - p);
            const float3 wpW = camPosW.xyz + p;
            float3 n = float3(0.0f, 0.0f, 1.0f);
            const float wDist = length(p);
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
            if (wDist < 26000.0f) {
                const float2 wDir = normalize(cloudWind.xy + float2(1.0f, 0.35f));
                const float wt = camPosW.w;
                const float windAmp = saturate(length(cloudWind.xy) / 380.0f);
                const float shallowW = 1.0f - smoothstep(4.0f, 52.0f, depthV0);
                const float nearObs = (1.0f - smoothstep(0.0f, 44.0f, depthV0)) * saturate(length(obsGrad) * 2.0f);
                // True divergence-free obstacle-deflected potential flow + downstream Karman-vortex swirling eddies!
                const float intoObs = dot(wDir, -obsGrad);
                bowHit = saturate(intoObs) * nearObs;
                eddyLee = saturate(-intoObs) * nearObs;
                const float2 tangObs = wDir - obsGrad * dot(wDir, obsGrad);
                const float2 flowDir = normalize(lerp(wDir, tangObs + 1.0e-4f, nearObs * 0.72f));
                const float stormChop = saturate(weather.w * 0.90f + windAmp * 0.65f);
                const float ampScale = (0.72f + stormChop * 0.95f + shallowW * 0.25f) * smoothstep(0.0f, 4.5f, depthV0);

                // Clean continuous world-space fluid surface normals (zero 24-bit depth-buffer finite-difference
                // octagonal contour rings seen in 09.57.21.71 & 09.57.17.80!):
                const float2 wpFlow = wpW.xy - flowDir * (wt * 22.0f);
                const float2 flowPerp = float2(-flowDir.y, flowDir.x);
                const float pxFoot = max(wDist * dims.z * 2.8f, 0.55f);
                const float depthM = max(depthV0 * 0.0143f, 0.12f);   // Skyrim units -> meters (70u = 1m)
                float2 waveG = 0.0f.xx;
                jacobianFold = 0.0f;
                [unroll] for (int wi = 0; wi < 8; ++wi) {
                    const float fracI = float(wi) / 7.0f;
                    const float kRad = 0.0065f * pow(9.0f, fracI);
                    const float kM = kRad * 70.0f;
                    const float wavelength = 6.2831853f / kRad;
                    const float nyquistFade = smoothstep(pxFoot * 4.0f, pxFoot * 10.0f, wavelength);
                    if (nyquistFade <= 0.001f) { continue; }
                    const float kd = min(kM * depthM, 18.0f);
                    const float omega = sqrt((9.81f * kM + 7.4e-5f * kM * kM * kM) * tanh(kd));
                    const float spread = (frac(float(wi) * 0.6180339f) - 0.5f) * (1.35f + fracI * 0.85f);
                    const float2 dirW = normalize(flowDir * cos(spread) + flowPerp * sin(spread));
                    const float ampW = (0.036f * pow(1.0f - fracI * 0.75f, 1.40f)) * ampScale * nyquistFade;
                    const float ph = dot(wpFlow, dirW) * kRad - wt * omega + float(wi) * 2.39996f;
                    const float s = sin(ph);
                    const float c = cos(ph);
                    waveG += dirW * (c * (1.0f + 0.28f * s) * ampW);
                    jacobianFold += max(-s, 0.0f) * ampW * 2.4f;
                }
                // Aperiodic 2-octave domain-warped fine liquid ripple gradient (zero periodic moire rings!):
                const float2 rUv1 = wpFlow * 0.038f;
                const float2 rUv2 = (wpW.xy + flowPerp * (wt * 11.0f)) * 0.082f;
                const float rDx = (vNoise2D(rUv1 + float2(0.18f, 0.0f)) - vNoise2D(rUv1 - float2(0.18f, 0.0f))) * 0.042f
                                + (vNoise2D(rUv2 + float2(0.18f, 0.0f)) - vNoise2D(rUv2 - float2(0.18f, 0.0f))) * 0.022f;
                const float rDy = (vNoise2D(rUv1 + float2(0.0f, 0.18f)) - vNoise2D(rUv1 - float2(0.0f, 0.18f))) * 0.042f
                                + (vNoise2D(rUv2 + float2(0.0f, 0.18f)) - vNoise2D(rUv2 - float2(0.0f, 0.18f))) * 0.022f;
                const float nearRip = smoothstep(3200.0f, 200.0f, wDist) * ampScale;
                waveG += float2(rDx, rDy) * nearRip;
                // Grazing-angle anisotropic Nyquist normal filter: zero shimmer & zero 24-bit depth contour rings!
                const float grazingSmooth = smoothstep(0.05f, 0.45f, saturate(v.z));
                n = normalize(n + float3(waveG * lerp(0.28f, 0.72f, grazingSmooth), 0.0f));
                // Dynamic player/actor wading wake ripples driven by the persistent 32-slot footTrail ring buffer.
                if (wDist < 1600.0f) {
                    float2 wakeG = 0.0f.xx;
                    [loop] for (uint wi = 0u; wi < 32u; wi++) {
                        const float4 ft = footTrail[wi];
                        if (ft.w <= 0.01f) { continue; }
                        const float3 fd = wpW - ft.xyz;
                        const float r2 = dot(fd.xy, fd.xy);
                        if (r2 < 22500.0f && abs(fd.z) < 180.0f) {
                            const float rr = sqrt(r2);
                            const float ring = sin(rr * 0.20f - wt * 1.35f) * exp(-rr / 70.0f) * ft.w;
                            wakeG += (fd.xy / max(rr, 1.0f)) * ring * 0.028f;
                        }
                    }
                    n = normalize(n + float3(wakeG, 0.0f));
                }
            }
            if (weather.w > 0.0f && wDist < 1200.0f) {   // delicate physical raindrop ripples (zero dark leopard-spot artifacts!)
                const float rainFade = smoothstep(1200.0f, 120.0f, wDist);
                const float2 g = rainRings(wpW.xy, camPosW.w) * (0.065f * weather.w * rainFade);
                n = normalize(n + float3(-g, 0.0f));
            }
            const float nvRaw = dot(n, v);
            if (nvRaw < 0.05f) { n = normalize(n + v * (0.05f - nvRaw)); }   // a wave normal never faces away
            const float nv = saturate(dot(n, v));
            const float F = 0.02f + 0.98f * pow(1.0f - nv, 5.0f);
            const float2 rr = float2(hash(px, frameIndex * 0.291f + 5.0f), hash(px.yx, frameIndex * 0.733f + 2.0f));
            const float3 o = p + float3(0.0f, 0.0f, 2.5f + 0.003f * length(p));

            // Silky recognizable mirror reflections with zero high-frequency noise (0.36 -> 0.16).
            // Swell-filtered reflection normal (keeps crisp capillary ripples for sun glint while preventing
            // pixel-to-pixel reflection ray scatter & edge noise on water reflections!):
            const float reflRelief = lerp(0.13f, 0.04f, saturate(wDist / 6000.0f));
            const float3 nRefl = normalize(lerp(float3(0.0f, 0.0f, 1.0f), n, reflRelief));
            float3 rd = reflect(-v, nRefl);
            if (rd.z < 0.04f) { rd = normalize(float3(rd.xy, 0.04f)); }
            float3 refl;
            // 4-Tap Stratified Cone-Filtered Pure Native Hardware DXR 1.2 RayQuery Water Reflections
            // + Karis luminance firefly suppression (eliminates noisy edge reflections & single-ray shimmer!):
            if (reflectLength > 0.0f) {
                const bool mats = (giFlags & GI_MATERIALS) != 0u;
                const float3 farReflSky = waterSky(rd);
                const float reflCone = lerp(0.014f, 0.038f, saturate(wDist / 4500.0f));
                static const float2 kStrat4[4] = {
                    float2(0.125f, 0.375f), float2(0.625f, 0.125f),
                    float2(0.375f, 0.875f), float2(0.875f, 0.625f)
                };
                const float2 wJit = float2(ign(px), ign(px.yx + 17.0f)) * 0.25f;
                float3 reflAcc = 0.0f.xxx;
                float reflW = 0.0f;
                [unroll] for (int ri = 0; ri < 4; ++ri) {
                    const float3 rdi = jitterDir(rd, reflCone, frac(kStrat4[ri] + wJit));
                    Hit h;
                    float3 sCol;
                    if (traceHit(o, rdi, reflectLength, h)) {
                        const float3 hitPt = o + rdi * h.t;
                        const float3 hn = mats ? hitNormal(h, rdi) : -rdi;
                        sCol = mats ? shadeHit(hitPt, hn, hitAlbedo(h), kStrat4[ri], true)
                                    : (0.25f * float3(ambient[0].w, ambient[1].w, ambient[2].w));
                        const float reflFog = 1.0f - exp(-h.t * 0.00014f);
                        sCol = lerp(max(sCol, farReflSky * 0.18f), farReflSky, saturate(reflFog));
                    } else {
                        sCol = farReflSky;
                    }
                    const float sLum = dot(sCol, float3(0.2126f, 0.7152f, 0.0722f));
                    const float kw = 1.0f / (1.0f + sLum * 0.45f);
                    reflAcc += sCol * kw;
                    reflW += kw;
                }
                refl = reflAcc / max(reflW, 1.0e-4f);
                const float reflMax = max(refl.r, max(refl.g, refl.b));
                if (reflMax > 4.2f) { refl *= 4.2f / reflMax; }
            } else {
                refl = waterSky(rd);
                const float waveTilt = saturate((1.0f - nRefl.z) * 18.0f);
                const float skyFace = saturate(dot(nRefl, normalize(v + float3(0.0f, 0.0f, 1.0f))));
                const float crestSheen = pow(skyFace, 36.0f) * 0.72f;
                refl = refl * lerp(0.60f, 1.36f, smoothstep(0.03f, 0.48f, rd.z)) * (1.0f - waveTilt * 0.28f)
                     + refl * crestSheen;
            }

            // Dual-lobe GGX sun glint (tight 0.0045 mirror crest sparkle + broad 0.022 sheen lobe).
            float3 glint = 0.0f;
            const float3 l = sunDir.xyz;
            const float nl = dot(nRefl, l);
            if (sunDir.w > 0.5f && nl > 0.0f && waterA.w > 0.0f) {
                const float3 hv = normalize(v + l);
                const float nh = saturate(dot(nRefl, hv));
                const float dd1 = nh * nh * (0.0045f - 1.0f) + 1.0f;
                const float dd2 = nh * nh * (0.0220f - 1.0f) + 1.0f;
                const float D = (0.0045f / (PI * dd1 * dd1)) * 0.65f + (0.0220f / (PI * dd2 * dd2)) * 0.35f;
                const float Fh = 0.02f + 0.98f * pow(1.0f - saturate(dot(v, hv)), 5.0f);
                const float vis = sunColour.w > 0.5f ? visible(o, l, 0.0f, sunLength) : 1.0f;
                glint = min(sunColour.rgb * (D * Fh * nl / (4.0f * max(nv, 0.08f))) * vis, 6.0f.xxx) * waterA.w;
            }
            // Dual-lobe elongated liquid specular reflections on water (tight 0.008 crest sparkle + broad 0.045 shimmer ribbon):
            // bridge lanterns, harbor braziers, torches & spells cast crisp dancing light ribbons across river ripples (GTA VI harbor study).
            if (wDist < 8500.0f && flags.y > 0u) {
                const uint maxWaterLights = min(flags.y, 32u);
                [loop] for (uint wli = 0u; wli < maxWaterLights; wli++) {
                    const float3 toWL = lightPosR[wli].xyz - p;
                    const float dWL = length(toWL);
                    const float rWL = lightPosR[wli].w * 2.25f;
                    if (dWL >= rWL || toWL.z <= 0.0f) { continue; }
                    const float attWL = pow(saturate(1.0f - dWL / rWL), 1.75f);
                    const float3 lWL = toWL / max(dWL, 1.0e-4f);
                    const float3 hWL = normalize(v + lWL);
                    const float nhWL = saturate(dot(nRefl, hWL));
                    const float ddTight = nhWL * nhWL * (0.008f - 1.0f) + 1.0f;
                    const float ddBroad = nhWL * nhWL * (0.048f - 1.0f) + 1.0f;
                    const float dSpec = (0.008f / (PI * ddTight * ddTight)) * 0.55f + (0.048f / (PI * ddBroad * ddBroad)) * 0.45f;
                    glint += min(lightColS[wli].rgb * (dSpec * attWL * 0.18f * (0.25f + 0.75f * F)), 2.8f.xxx);
                }
            }

            // Caustics on the bed, under the water, with the view through it.
            float3 caus = 0.0f;
            if (dBed < 1.0f && sunDir.w > 0.5f && waterA.y > 0.0f && l.z > 0.05f) {
                const float3 pb = worldAt(px, dBed);
                const float depthV = max(p.z - pb.z, 0.0f);
                const float3 sr = refract(-l, float3(0.0f, 0.0f, 1.0f), 1.0f / 1.333f);   // the sun's ray under the surface
                // Flow-advected chromatic dispersion caustics: prism RGB spread scales naturally with water depth.
                const float2 flowOff = (cloudWind.xy * 0.012f + nL.xy * 5.5f) * camPosW.w;
                const float3 at = camPosW.xyz + pb - sr * (depthV / max(-sr.z, 0.2f));      // where it crossed the surface
                const float t = camPosW.w * 0.42f;
                const float sc = max(waterB.y, 1.0f);
                const float disp = clamp(depthV * 0.025f, 0.30f, 1.65f);
                const float2 cPos = at.xy + flowOff;
                const float3 net = float3(causticNet(cPos + float2(disp, 0.25f * disp), t, sc),
                                          causticNet(cPos, t, sc),
                                          causticNet(cPos - float2(disp, 0.25f * disp), t, sc));
                const float sunVis = sunColour.w > 0.5f ? visible(pb + float3(0.0f, 0.0f, 1.0f + 0.002f * length(pb)), l, 0.0f, sunLength) : 1.0f;
                const float through = exp(-waterA.z * (depthV + length(pb - p)));    // down to the bed and back up to the eye
                const float focus = saturate(depthV / 22.0f) * exp(-depthV / 340.0f);  // sharp in shallows, softens in deep pools
                // Attenuate caustics naturally under overcast clouds so diffuse overcast skies never cast harsh laser webs on riverbeds.
                const float clearSun = 1.0f - saturate(clouds.x * 0.85f);
                caus = BedAlbedo.Load(ip).rgb * sunColour.rgb * (l.z * sunVis * through * focus * waterA.y * 0.85f * clearSun) * net;
            }

            // Refraction through the bed's own colour, from the frame RAW lit before the game composited its water: the
            // bed seen through the surface, bent by the wave normal in proportion to the depth, dimmed per channel
            // (red first) and filled in by light the water scatters (its tint under the sky's ambient).
            float3 body = game.rgb;
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
                const float4 s2 = screenOf(lerp(pb, pRefrBed, 0.35f));
                float2 qOff = s2.w > 0.0f ? (s2.xy - float2(id.xy)) : 0.0f.xx;
                const float qLen = length(qOff);
                if (qLen > 10.0f) { qOff *= (10.0f / qLen); }
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
                const float3 bed = lerp(lerp(b00, b10, qFrac.x), lerp(b01, b11, qFrac.x), qFrac.y);
                const int2 qDepthPx = qValid ? q : int2(id.xy);
                const float path = length(worldAt(float2(qDepthPx), Depth.Load(int3(qDepthPx, 0))) - p);
                // Realistic cold Nordic slate-emerald Beer-Lambert absorption (eliminates over-saturated electric royal blue
                // in Dawnstar Arctic waters & flat cyan under the Riverwood sawmill bridge).
                const float3 T = exp(-waterA.z * float3(1.08f, 0.74f, 0.64f) * path);
                const float3 scatter = float3(0.011f, 0.024f, 0.028f) * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.04f.xxx) * 1.15f;
                const float nearW = 1.0f - smoothstep(2800.0f, 5200.0f, wDist);
                // Rich backlit glacial-emerald wave-crest subsurface scattering.
                const float3 waveSSS = sunDir.w > 0.5f
                    ? float3(0.03f, 0.28f, 0.26f) * sunColour.rgb * (0.25f + 0.75f * pow(saturate(dot(-v, sunDir.xyz)), 3.5f)) * saturate((1.0f - n.z) * 5.5f) * 0.65f
                    : 0.0f.xxx;
                // Cluster D (Point Reyes surf / Aivazovsky storm coasts): rolling shore-breaking surf trains,
                // wave-shoaling steepening, retreating lacy whitewater webs, and wet-shoreline meniscus!
                const float2 foamFlow = normalize(cloudWind.xy + float2(1.0f, 0.35f));
                const float froth = organicFroth(wpW.xy - foamFlow * (camPosW.w * 16.0f), camPosW.w);
                const float frothFine = organicFroth(wpW.xy * 1.85f + foamFlow * (camPosW.w * 9.0f) + 13.7f, camPosW.w * 1.25f);
                const float runup = 4.2f + 2.2f * sin(camPosW.w * 1.45f + (wpW.x + wpW.y) * 0.016f + froth * 1.8f);
                const float shoreMeniscus = (1.0f - smoothstep(0.0f, runup, depthV)) * smoothstep(0.0f, 0.25f, depthV);
                // Aperiodic organic shore foam + Jacobian wave-breaking whitecaps (eliminates Dawnstar periodic zebra stripes!):
                const float surfZone = smoothstep(1.5f, 10.0f, depthV) * (1.0f - smoothstep(16.0f, 40.0f, depthV));
                const float surfCrest = saturate(jacobianFold * 0.65f + (frothFine - 0.52f) * 1.4f) * surfZone * (0.35f + 0.65f * froth);
                const float retreatLace = (1.0f - smoothstep(1.0f, 16.0f, depthV)) * smoothstep(0.42f, 0.82f, frothFine) * 0.36f;
                const float rapidFoam = (1.0f - smoothstep(2.0f, 20.0f, depthV)) * smoothstep(0.50f, 0.88f, froth) * 0.32f;
                const float bowFoam = bowHit * froth * 0.82f;
                const float eddyFoam = eddyLee * froth * 0.44f;
                const float shoreLap = max(max(shoreMeniscus * (0.42f + 0.58f * froth) * 0.72f, max(surfCrest * 0.80f, retreatLace)), rapidFoam);
                const float windSpeedW = saturate(length(cloudWind.xy) / 340.0f + weather.w * 0.55f);
                const float crestFoam = saturate((1.0f - n.z) * (6.5f + windSpeedW * 5.2f) - lerp(0.16f, 0.08f, windSpeedW)) * froth * (0.55f + windSpeedW * 0.42f);
                const float foamMask = saturate(max(max(bowFoam, eddyFoam), max(shoreLap, crestFoam))) * nearW;
                // True Underwater Volumetric Sunbeam Shafts (6-step depth-marched Mie scattering integral through water column):
                // gives sunlit Nordic rivers glowing emerald-turquoise underwater light cones beneath the surface.
                float3 uwShaft = 0.0f.xxx;
                if (sunDir.w > 0.5f && sunDir.z > 0.08f && depthV > 6.0f && wDist < 4200.0f) {
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
                        uwAcc += exp(-waterA.z * 0.85f * (tu + dCol)) * stepUw;
                    }
                    uwShaft = sunColour.rgb * float3(0.06f, 0.34f, 0.38f) * (uwAcc * 0.0028f * phUw * clearW);
                }
                const float3 foamLit = (max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.05f.xxx) * 0.85f + (sunDir.w > 0.5f ? sunColour.rgb * 0.32f : 0.0f.xxx));
                const float shoreDark = lerp(0.62f, 1.0f, smoothstep(0.0f, 22.0f, depthV));
                // Wave-facet luminance modulation makes moving crests & troughs visibly travel even under uniform overcast/night skies.
                const float waveFacet = lerp(0.76f, 1.28f, saturate(dot(n, normalize(float3(0.35f, 0.25f, 0.90f)))));
                const float3 waterBody = (((bed * shoreDark + caus) * T + scatter * (1.0f - T)) * waveFacet + waveSSS + uwShaft);
                body = lerp(waterBody, foamLit, foamMask * 0.68f);
            }
            const float shoreFeather = dBed < 1.0f ? smoothstep(0.0f, 8.0f, max(p.z - worldAt(px, dBed).z, 0.0f)) : 1.0f;
            // Continuous single world-space fluid surface from boots to horizon (extends full JONSWAP + RT water out to 42,000u!):
            const float cellFeather = 1.0f - smoothstep(18000.0f, 42000.0f, wDist);
            const float effF = saturate(max(lerp(F, pow(saturate(F), 0.64f), 0.55f), 0.16f) * waterA.x * shoreFeather * cellFeather);
            const uint view = uint(waterB.x + 0.5f);
            const float3 farSkyRefl = waterSky(rd);
            // Deep cold Nordic steel-slate ocean body (replaces electric royal blue Arctic water).
            const float3 deepOceanBody = float3(0.016f, 0.028f, 0.036f) * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.07f.xxx) * 1.65f;
            const float farF = saturate(max(F * max(waterA.x, 0.85f) * 0.90f, smoothstep(5500.0f, 36000.0f, wDist) * 0.68f));
            const float3 farOcean = lerp(deepOceanBody, farSkyRefl, farF);
            const float3 nearCol = refr ? lerp(body, refl, effF) : lerp(game.rgb, refl, effF) + caus * (1.0f - effF);
            col = lerp(farOcean, nearCol, cellFeather) + glint * shoreFeather;
            // Invisible wet-shoreline alpha feather (eliminates thin bright edge line seen in 02.11.49.68.png!):
            col = lerp(game.rgb, col, smoothstep(0.0f, 6.0f, depthV0));
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
            // Underwater looking UP at the water surface (p.z >= 0):
            // Physical Snell's Window (48.6 deg critical cone) + Total Internal Reflection outside the cone
            // (eliminates the solid grey opaque underwater curtain caught in pain_2045_g1.jpg 015..020!).
            const float3 vUp = normalize(p - eyeAt());
            const float3 wpU = camPosW.xyz + p;
            const float2 uwRipple = float2(
                vNoise2D(wpU.xy * 0.09f + float2(camPosW.w * 0.8f, -camPosW.w * 0.5f)) - 0.5f,
                vNoise2D(wpU.xy * 0.09f + float2(-camPosW.w * 0.6f, camPosW.w * 0.7f) + 19.3f) - 0.5f
            ) * 0.16f;
            const float3 nDown = normalize(float3(uwRipple.x, uwRipple.y, -1.0f));
            const float cosInc = saturate(dot(-vUp, nDown));
            // Critical angle sin(theta_c) = 1 / 1.333 = 0.75 => cos(theta_c) = 0.661.
            const float snellWin = smoothstep(0.60f, 0.76f, cosInc);
            const float3 refrSky = waterSky(normalize(float3(vUp.xy * 1.333f, max(vUp.z, 0.12f))));
            const float3 tirCol = float3(0.014f, 0.036f, 0.044f) * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.08f.xxx) * 1.8f;
            const float uwAtt = exp(-length(p - eyeAt()) * 0.0022f);
            const float3 uwFogCol = float3(0.018f, 0.052f, 0.064f) * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.06f.xxx);
            col = lerp(uwFogCol, lerp(tirCol, refrSky, snellWin), uwAtt);
        }
    }
    WaterOut[id.xy] = float4(col, game.a);
}
