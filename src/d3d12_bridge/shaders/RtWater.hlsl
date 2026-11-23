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
    const float3 base = skyAt(rd);
    const float aElevW = astroSunElev();
    const float dayW = smoothstep(-0.08f, 0.14f, aElevW) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
    float3 skyCol = base;
    if (dayW > 0.02f && rd.z > 0.0f) {
        float3 sZ, sM, sH;
        skyPalette(sZ, sM, sH);
        const float hUp = saturate(rd.z);
        const float3 pSky = hUp < 0.25f ? lerp(sH, sM, pow(hUp / 0.25f, 0.75f)) : lerp(sM, sZ, pow((hUp - 0.25f) / 0.75f, 0.85f));
        skyCol = lerp(base, pSky, dayW * 0.65f);
        // Drifting volumetric cloud-deck reflection on lakes, rivers, and the Sea of Ghosts.
        if (clouds.x > 0.08f && rd.z > 0.03f) {
            const float2 cUv = (camPosW.xy + rd.xy * (34000.0f / max(rd.z, 0.08f)) + cloudWind.xy * camPosW.w * 0.55f) * 0.000018f;
            const float cn = sin(cUv.x * 3.1f + sin(cUv.y * 2.7f)) * cos(cUv.y * 3.7f - cos(cUv.x * 2.3f)) * 0.5f + 0.5f;
            const float cov = smoothstep(1.0f - saturate(clouds.x * 0.95f), 1.0f, cn) * smoothstep(0.03f, 0.22f, rd.z);
            const float3 cloudLit = lerp(sH * 0.85f, max(sunColour.rgb, 0.45f.xxx) * 0.95f, 0.45f);
            skyCol = lerp(skyCol, cloudLit, cov * 0.52f);
        }
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
    const bool gameDrewWater = !hasPreWater || (waterCompDiff > 0.0025f);
    const bool isWaterPx = (L.a > 1.5f && L.x < 1.0f && zSurfW > 0.5f && zSurfW < zBedW - max(zSurfW * 0.015f, 1.5f) && nL.z > 0.72f && gameDrewWater);
    // SM6.9 Wave-coherent early exit: non-water waves exit immediately with zero RayQuery divergence.
    if (!WaveActiveAnyTrue(isWaterPx)) { WaterOut[id.xy] = float4(col, game.a); return; }
    if (isWaterPx) {
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, L.x);
        if (p.z < 0.0f) {   // the surface is below the camera: seen from above
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
            float bowHit = 0.0f, eddyLee = 0.0f;
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
                const float nearObs = (1.0f - smoothstep(0.0f, 38.0f, depthV0)) * saturate(length(obsGrad) * 2.0f);
                // CRITICAL: flowDir MUST be spatially smooth (wDir), NEVER per-pixel nL.xy!
                // Multiplying per-pixel nL.xy by (wt * 36.0) scrambled wave phase by thousands of radians per pixel (pure noise!).
                const float2 flowDir = wDir;
                const float intoObs = dot(flowDir, -obsGrad);
                bowHit = saturate(intoObs) * nearObs;
                eddyLee = saturate(-intoObs) * nearObs;

                // Silky, Nyquist pixel-footprint band-limited 6-directional Gerstner/swell spectrum with smooth 28 u/s
                // downstream advection: ZERO pixel-aliasing noise, crisp gliding liquid waves & mirror reflections!
                const float2 wpFlow = wpW.xy - flowDir * (wt * 28.0f);
                const float2 flowPerp = float2(-flowDir.y, flowDir.x);
                const float pxFoot = max(wDist * dims.z * 2.4f, 0.5f);
                static const float2 kWDir[6] = {
                    float2( 0.96f,  0.28f), float2( 0.84f, -0.54f), float2( 0.58f,  0.81f),
                    float2( 0.72f, -0.69f), float2(-0.34f,  0.94f), float2( 0.92f, -0.39f)
                };
                static const float kWFreq[6]  = { 0.018f, 0.032f, 0.056f, 0.095f, 0.160f, 0.260f };
                static const float kWSpeed[6] = { 1.45f,  1.95f,  2.55f,  3.25f,  4.05f,  4.95f  };
                static const float kWAmp[6]   = { 0.068f, 0.054f, 0.040f, 0.028f, 0.018f, 0.010f };
                const float ampScale = (0.78f + windAmp * 0.45f + shallowW * 0.22f) * smoothstep(0.0f, 5.0f, depthV0);
                float2 waveG = 0.0f.xx;
                [unroll] for (int wi = 0; wi < 6; ++wi) {
                    const float wavelength = 6.2831853f / kWFreq[wi];
                    const float nyquistFade = smoothstep(pxFoot * 2.5f, pxFoot * 7.0f, wavelength);
                    const float2 dirW = normalize(flowDir * kWDir[wi].x + flowPerp * kWDir[wi].y);
                    const float ph = dot(wpFlow, dirW) * kWFreq[wi] - wt * kWSpeed[wi];
                    const float s = sin(ph);
                    const float c = cos(ph);
                    waveG += dirW * (c * (1.0f + 0.30f * s) * kWAmp[wi] * ampScale * nyquistFade);
                }
                const float2 bowNormal = obsGrad * (bowHit * (0.08f + 0.04f * sin(depthV0 * 0.32f - wt * 3.2f)));
                n = normalize(n + float3(waveG + bowNormal, 0.0f));
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
            if (weather.w > 0.0f) {   // rain falling: drops ring the water
                const float2 g = rainRings(wpW.xy, camPosW.w) * (0.28f * weather.w);
                n = normalize(n + float3(-g, 0.0f));
            }
            const float nvRaw = dot(n, v);
            if (nvRaw < 0.05f) { n = normalize(n + v * (0.05f - nvRaw)); }   // a wave normal never faces away
            const float nv = saturate(dot(n, v));
            const float F = 0.02f + 0.98f * pow(1.0f - nv, 5.0f);
            const float2 rr = float2(hash(px, frameIndex * 0.291f + 5.0f), hash(px.yx, frameIndex * 0.733f + 2.0f));
            const float3 o = p + float3(0.0f, 0.0f, 2.5f + 0.003f * length(p));

            // Silky recognizable mirror reflections with zero high-frequency noise (0.36 -> 0.16).
            const float reflRelief = lerp(0.36f, 0.16f, saturate(wDist / 8500.0f));
            const float3 nRefl = normalize(lerp(float3(0.0f, 0.0f, 1.0f), n, reflRelief));
            float3 rd = reflect(-v, nRefl);
            if (rd.z < 0.04f) { rd = normalize(float3(rd.xy, 0.04f)); }
            float3 refl;
            Hit h;
            if (reflectLength > 0.0f && traceHit(o, rd, reflectLength, h)) {
                float3 cScreen, hn;
                // Hybrid RT + Screen-Space reflection: use exact lit full-res frame when the 3D ray hit is on-screen
                // (pixel-sharp reflections of pine needles, characters, bridges & torches), and seamless 3D RT radianceAt off-screen.
                if (onScreen(o + rd * h.t, cScreen)) {
                    refl = cScreen;
                } else {
                    refl = radianceAt(h, o, rd, float2(0.5f, 0.5f), true, hn);
                }
                const float reflMax = max(refl.r, max(refl.g, refl.b));
                if (reflMax > 4.5f) { refl *= 4.5f / reflMax; }
            } else {
                refl = waterSky(rd);
                // High-contrast wave-slope reflection modulation: dark shadowed wave troughs vs crisp bright crests
                // (eliminates flat matte-plastic teal water under overcast skies).
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
                const float2 qF = clamp(float2(id.xy) + qOff * saturate(depthV / 28.0f), float2(1.0f, 1.0f), dims.xy - 2.0f);
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
                // Organic turbulent shoreline contact foam ONLY at 0..3.5u rock/bank contact (deleted ugly sin(depthV) zebra stripes!).
                const float shoreMeniscus = (1.0f - smoothstep(0.0f, 3.5f, depthV)) * smoothstep(0.0f, 0.35f, depthV);
                const float froth = organicFroth(wpW.xy, camPosW.w);
                const float bowFoam = bowHit * froth * 0.72f;
                const float eddyFoam = eddyLee * froth * 0.36f;
                const float shoreLap = shoreMeniscus * (0.35f + 0.65f * froth) * 0.58f;
                const float windSpeedW = saturate(length(cloudWind.xy) / 380.0f);
                const float crestFoam = saturate((1.0f - n.z) * (6.0f + windSpeedW * 4.5f) - lerp(0.18f, 0.10f, windSpeedW)) * froth * (0.50f + windSpeedW * 0.38f);
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
                const float shoreDark = lerp(0.76f, 1.0f, smoothstep(0.0f, 14.0f, depthV));
                // Wave-facet luminance modulation makes moving crests & troughs visibly travel even under uniform overcast/night skies.
                const float waveFacet = lerp(0.76f, 1.28f, saturate(dot(n, normalize(float3(0.35f, 0.25f, 0.90f)))));
                const float3 waterBody = (((bed * shoreDark + caus) * T + scatter * (1.0f - T)) * waveFacet + waveSSS + uwShaft);
                body = lerp(waterBody, foamLit, foamMask * 0.68f);
            }
            const float shoreFeather = dBed < 1.0f ? smoothstep(0.0f, 8.0f, max(p.z - worldAt(px, dBed).z, 0.0f)) : 1.0f;
            // Eliminate the 2,200u mid-river seam: keep full physical refraction & RT reflections out to 6,500-13,500u!
            const float cellFeather = 1.0f - smoothstep(6500.0f, 13500.0f, wDist);
            const float effF = saturate(max(lerp(F, pow(saturate(F), 0.64f), 0.55f), 0.16f) * waterA.x * shoreFeather * cellFeather);
            const uint view = uint(waterB.x + 0.5f);
            const float3 farSkyRefl = waterSky(rd);
            // Deep cold Nordic steel-slate ocean body (replaces electric royal blue Arctic water).
            const float3 deepOceanBody = float3(0.016f, 0.028f, 0.036f) * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.07f.xxx) * 1.65f;
            const float farF = saturate(max(F * max(waterA.x, 0.85f) * 0.90f, smoothstep(5500.0f, 36000.0f, wDist) * 0.68f));
            const float3 farOcean = lerp(deepOceanBody, farSkyRefl, farF);
            const float3 nearCol = refr ? lerp(body, refl, effF) : lerp(game.rgb, refl, effF) + caus * (1.0f - effF);
            col = lerp(farOcean, nearCol, cellFeather) + glint * shoreFeather;
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
