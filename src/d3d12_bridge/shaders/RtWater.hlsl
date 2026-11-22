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
    return grad * (0.095f + windAmp * 0.085f);
}

float3 waterSky(float3 rd) {
    const float3 base = skyAt(rd);
    const float sunLumW = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float dayW = saturate((sunDir.z - 0.01f) * 4.0f) * saturate((sunLumW - 0.20f) * 2.5f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
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
    float3 col = game.rgb;
    // The water shaders write no alpha, so their layer pixels keep the cleared alpha: a water pixel is one with a
    // depth in front of the bed and a normal (blended draws without one carry b = -1) that faces up.
    const float3 nL = worldNormal(float4(L.y, L.z, 0.0f, 0.0f));
    const bool isWaterPx = (L.z >= 0.0f && L.x < 1.0f && L.x < dBed - 1.0e-5f && nL.z > 0.6f);
    // SM6.9 Wave-coherent early exit: non-water waves exit immediately with zero RayQuery divergence.
    if (!WaveActiveAnyTrue(isWaterPx)) { WaterOut[id.xy] = float4(col, game.a); return; }
    if (isWaterPx) {
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, L.x);
        if (p.z < 0.0f) {   // the surface is below the camera: seen from above
            // Replace Skyrim's frantic fast-scrolling vanilla water normal map (nL) with our own calm, slow-drifting
            // 4-octave world-space Gerstner & capillary wave field (keeping only 12% of nL for river-flow curvature).
            const float3 v = normalize(eyeAt() - p);
            const float3 wpW = camPosW.xyz + p;
            float3 n = normalize(lerp(float3(0.0f, 0.0f, 1.0f), nL, 0.14f));
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
                // Shallow-water Bernoulli/Venturi acceleration: shallow channels flow faster with tighter ripples;
                // deep pools calm into broad smooth swells (c = sqrt(g * h)).
                const float shallowW = 1.0f - smoothstep(4.0f, 52.0f, depthV0);
                const float nearObs = (1.0f - smoothstep(0.0f, 38.0f, depthV0)) * saturate(length(obsGrad) * 2.0f);
                const float2 flowBase = nL.xy * (22.0f + shallowW * 16.0f) + wDir * (1.8f + windAmp * 2.2f);
                const float flowSpd = max(length(flowBase), 0.1f);
                const float2 flowDir = flowBase / flowSpd;
                // Potential-flow streamline deflection: current parts & curves smoothly AROUND rocks and riverbanks!
                const float intoObs = dot(flowDir, -obsGrad);
                const float2 flowTang = flowBase - obsGrad * (dot(flowBase, obsGrad) * nearObs * 0.92f);
                // Divergence-free 2D curl-noise eddies in calm backwaters & lee wakes behind boulders.
                const float cEps = 18.0f;
                const float cn0 = hash21(floor(wpW.xy * 0.015f));
                const float2 curlPhase = float2(sin(wpW.y * 0.024f + wt * 0.55f + cn0), -cos(wpW.x * 0.024f - wt * 0.48f));
                bowHit = saturate(intoObs) * nearObs;
                eddyLee = saturate(-intoObs) * nearObs;
                const float2 flowVec = flowTang + curlPhase * (1.4f + eddyLee * 6.5f);
                // Vlachos SIGGRAPH 2010 Dual-Phase Directional Flow-Map Advection along deflected streamlines.
                const float ph0 = frac(wt * (0.24f + shallowW * 0.14f));
                const float ph1 = frac(wt * (0.24f + shallowW * 0.14f) + 0.5f);
                const float flowBlend = abs(2.0f * ph0 - 1.0f);
                const float2 g0 = evalTrochoidalWaves(wpW.xy - flowVec * (ph0 * 16.0f), flowDir, wt, wDist, windAmp + shallowW * 0.45f);
                const float2 g1 = evalTrochoidalWaves(wpW.xy - flowVec * (ph1 * 16.0f) + float2(73.1f, 41.7f), flowDir, wt, wDist, windAmp + shallowW * 0.45f);
                // Damp wave height smoothly at razor-thin 0..4u shore edge while raising upstream bow-wave crest & lee eddy ripples.
                const float depthDamp = smoothstep(0.0f, 6.0f, depthV0) * lerp(1.0f, 0.72f, smoothstep(50.0f, 160.0f, depthV0));
                const float2 bowNormal = obsGrad * (bowHit * (0.14f + 0.08f * sin(depthV0 * 0.45f - wt * 3.2f)));
                const float2 eddyRipple = curlPhase * (eddyLee * 0.09f * sin(dot(wpW.xy, flowDir) * 0.18f - wt * 2.8f));
                const float2 waveG = lerp(g0, g1, flowBlend) * depthDamp + bowNormal + eddyRipple;
                n = normalize(n + float3(waveG, 0.0f));
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

            // Distance-filtered 3D liquid reflection normal (crisp sculpted ripples up close, silky smooth swells at distance).
            const float reflRelief = lerp(0.82f, 0.34f, saturate(wDist / 6500.0f));
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
            }

            // The sun's glint on the waves (true HDR peak feeds 12-pass Jimenez HDR Bloom cleanly).
            float3 glint = 0.0f;
            const float3 l = sunDir.xyz;
            const float nl = dot(nRefl, l);
            if (sunDir.w > 0.5f && nl > 0.0f && waterA.w > 0.0f) {
                const float3 hv = normalize(v + l);
                const float nh = saturate(dot(nRefl, hv)), a2 = 0.014f;
                const float dd = nh * nh * (a2 - 1.0f) + 1.0f;
                const float D = a2 / (PI * dd * dd);
                const float Fh = 0.02f + 0.98f * pow(1.0f - saturate(dot(v, hv)), 5.0f);
                const float vis = sunColour.w > 0.5f ? visible(o, l, 0.0f, sunLength) : 1.0f;
                glint = min(sunColour.rgb * (D * Fh * nl / (4.0f * max(nv, 0.08f))) * vis, 6.0f.xxx) * waterA.w;
            }
            // Point-light GGX specular reflections on water (bridge lanterns, harbor braziers, torches & spells shimmer across ripples).
            if (wDist < 5500.0f && flags.y > 0u) {
                const uint maxWaterLights = min(flags.y, 24u);
                [loop] for (uint wli = 0u; wli < maxWaterLights; wli++) {
                    const float3 toWL = lightPosR[wli].xyz - p;
                    const float dWL = length(toWL);
                    const float rWL = lightPosR[wli].w * 1.65f;
                    if (dWL >= rWL || toWL.z <= 0.0f) { continue; }
                    const float attWL = pow(saturate(1.0f - dWL / rWL), 2.0f);
                    const float3 lWL = toWL / max(dWL, 1.0e-4f);
                    const float3 hWL = normalize(v + lWL);
                    const float nhWL = saturate(dot(nRefl, hWL));
                    const float ddWL = nhWL * nhWL * (0.024f - 1.0f) + 1.0f;
                    const float dSpec = 0.024f / (PI * ddWL * ddWL);
                    glint += min(lightColS[wli].rgb * (dSpec * attWL * 0.12f * F), 1.4f.xxx);
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
                const float refrShift = min(depthV, 48.0f) * 0.16f;
                const float4 s2 = screenOf(pb + float3(n.x, n.y, 0.0f) * refrShift);
                const bool inBounds = s2.w > 0.0f && all(s2.xy >= 2.0f.xx) && all(s2.xy <= dims.xy - 3.0f);
                int2 q = inBounds ? int2(s2.xy) : int2(id.xy);
                // A bent look-up that lands off-screen or on something above the water keeps the straight one (prevents vertical edge stretching).
                const float4 Lq = Layer.Load(int3(q, 0));
                if (!inBounds || Depth.Load(int3(q, 0)) <= Lq.x || Lq.z < 0.0f) { q = int2(id.xy); }
                // Chromatic shallow-water refraction: subtle RGB dispersion across pebbles & rocks in clear Nordic streams.
                const int2 qR = clamp(q + int2(round(n.xy * 1.8f)), int2(0, 0), int2(dims.xy) - 1);
                const int2 qB = clamp(q - int2(round(n.xy * 1.8f)), int2(0, 0), int2(dims.xy) - 1);
                const float3 bed = float3(PreWater[qR].r, PreWater[q].g, PreWater[qB].b);
                const float path = length(worldAt(float2(q), Depth.Load(int3(q, 0))) - p);
                const float3 T = exp(-waterA.z * float3(2.45f, 1.05f, 0.72f) * path);
                const float3 scatter = float3(0.018f, 0.068f, 0.092f) * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.05f.xxx) * 1.95f;
                const float nearW = 1.0f - smoothstep(2800.0f, 5200.0f, wDist);
                // Rich backlit glacial-emerald wave-crest subsurface scattering.
                const float3 waveSSS = sunDir.w > 0.5f
                    ? float3(0.03f, 0.28f, 0.26f) * sunColour.rgb * (0.25f + 0.75f * pow(saturate(dot(-v, sunDir.xyz)), 3.5f)) * saturate((1.0f - n.z) * 5.5f) * 0.65f
                    : 0.0f.xxx;
                // Hydrodynamic foam: crisp white-water spray ONLY on upstream rock bow-waves, trailing lee eddies, and steep rapids crests
                // (calm banks stay crystal-clear with only a hairline of broken cellular froth).
                const float shoreBand = (1.0f - smoothstep(0.0f, 6.5f, depthV)) * smoothstep(0.0f, 0.6f, depthV);
                const float foamCell = saturate(1.0f - cellWalls(wpW.xy / 13.0f, camPosW.w * 0.55f) * 2.5f);
                const float laceFoam = pow(foamCell, 2.3f);
                const float lapWave = pow(saturate(sin(depthV * 0.55f - camPosW.w * 2.1f + foamCell * 2.6f) * 0.5f + 0.5f), 3.0f);
                const float bowFoam = bowHit * laceFoam * (0.55f + 0.45f * lapWave) * 0.78f;
                const float eddyFoam = eddyLee * laceFoam * 0.38f;
                const float shoreLap = shoreBand * laceFoam * (0.35f + 0.65f * lapWave) * 0.24f;
                const float crestFoam = saturate((1.0f - n.z) * 7.5f - 0.20f) * laceFoam * 0.55f;
                const float foamMask = saturate(max(max(bowFoam, eddyFoam), max(shoreLap, crestFoam))) * nearW;
                const float3 foamLit = (max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.05f.xxx) * 0.85f + (sunDir.w > 0.5f ? sunColour.rgb * 0.32f : 0.0f.xxx));
                const float3 waterBody = (bed + caus) * T + scatter * (1.0f - T) + waveSSS;
                body = lerp(waterBody, foamLit, foamMask * 0.68f);
            }
            const float shoreFeather = dBed < 1.0f ? smoothstep(0.0f, 8.0f, max(p.z - worldAt(px, dBed).z, 0.0f)) : 1.0f;
            const float cellFeather = 1.0f - smoothstep(2200.0f, 4400.0f, wDist);
            const float effF = saturate(F * waterA.x * shoreFeather * cellFeather);
            const uint view = uint(waterB.x + 0.5f);
            const float3 farSkyRefl = waterSky(rd);
            // Deep Nordic/Arctic ocean body for mid/far water (replaces matte grey-blue vanilla Sea of Ghosts water).
            const float3 deepOceanBody = float3(0.020f, 0.056f, 0.084f) * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.06f.xxx) * 1.65f;
            const float farF = saturate(F * max(waterA.x, 0.85f) * 0.88f);
            const float3 farOcean = lerp(deepOceanBody, farSkyRefl, farF);
            const float3 nearCol = refr ? lerp(body, refl, effF) : lerp(game.rgb, refl, effF) + caus * (1.0f - effF);
            col = lerp(farOcean, nearCol, cellFeather) + glint * shoreFeather;
            // Seamless far-ocean atmospheric horizon merge so the Sea of Ghosts blends smoothly into the sky dome.
            if (wDist > 12000.0f) {
                float3 sZ, sM, sH;
                skyPalette(sZ, sM, sH);
                const float seaFog = saturate((wDist - 12000.0f) / 110000.0f);
                col = lerp(col, sH, seaFog * seaFog * 0.85f);
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
        }
    }
    WaterOut[id.xy] = float4(col, game.a);
}
