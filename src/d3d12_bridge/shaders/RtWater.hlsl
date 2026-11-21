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

float3 waterSky(float3 rd) {
    const float3 base = skyAt(rd);
    const float sunLumW = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    const float dayW = saturate((sunDir.z - 0.01f) * 4.0f) * saturate((sunLumW - 0.20f) * 2.5f) * (sunDir.w > 0.5f ? 1.0f : 0.0f);
    if (dayW > 0.02f && rd.z > 0.0f) {
        float3 sZ, sM, sH;
        skyPalette(sZ, sM, sH);
        const float hUp = saturate(rd.z);
        const float3 pSky = hUp < 0.25f ? lerp(sH, sM, pow(hUp / 0.25f, 0.75f)) : lerp(sM, sZ, pow((hUp - 0.25f) / 0.75f, 0.85f));
        return lerp(base, pSky, dayW * 0.65f);
    }
    return base;
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
    if (L.z >= 0.0f && L.x < 1.0f && L.x < dBed - 1.0e-5f && nL.z > 0.6f) {
        const float2 px = float2(id.xy);
        const float3 p = worldAt(px, L.x);
        if (p.z < 0.0f) {   // the surface is below the camera: seen from above
            // Replace Skyrim's frantic fast-scrolling vanilla water normal map (nL) with our own calm, slow-drifting
            // 4-octave world-space Gerstner & capillary wave field (keeping only 12% of nL for river-flow curvature).
            const float3 v = normalize(eyeAt() - p);
            const float3 wpW = camPosW.xyz + p;
            float3 n = normalize(lerp(float3(0.0f, 0.0f, 1.0f), nL, 0.12f));
            const float wDist = length(p);
            if (wDist < 6500.0f) {
                const float wFade = saturate(1.0f - wDist / 6500.0f);
                const float2 wDir = normalize(cloudWind.xy + float2(1.0f, 0.35f));
                const float2 wPerp = float2(-wDir.y, wDir.x);
                const float wt = camPosW.w;
                const float g1 = sin(dot(wpW.xy, wDir) * 0.022f + wt * 0.48f);
                const float g2 = sin(dot(wpW.xy, normalize(wDir + wPerp * 0.65f)) * 0.048f + wt * 0.72f);
                const float g3 = cos(dot(wpW.xy, normalize(wDir - wPerp * 0.85f)) * 0.092f - wt * 0.94f);
                const float g4 = sin(dot(wpW.xy, wPerp) * 0.145f + wt * 1.15f);
                const float windScale = (0.014f + saturate(length(cloudWind.xy) / 420.0f) * 0.020f) * wFade;
                n = normalize(n + float3((wDir * (g1 * 0.45f + g2 * 0.28f) + wPerp * (g3 * 0.18f + g4 * 0.09f)) * windScale, 0.0f));
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

            // Calm reflection normal derived from our slow procedural Gerstner field (eliminates frantic vanilla specular flecks).
            const float3 nRefl = normalize(lerp(float3(0.0f, 0.0f, 1.0f), n, 0.55f));
            float3 rd = reflect(-v, nRefl);
            if (rd.z < 0.04f) { rd = normalize(float3(rd.xy, 0.04f)); }
            float3 refl;
            Hit h;
            if (reflectLength > 0.0f && traceHit(o, rd, reflectLength, h)) {
                float3 hn;
                refl = radianceAt(h, o, rd, float2(0.5f, 0.5f), true, hn);
                const float reflMax = max(refl.r, max(refl.g, refl.b));
                if (reflMax > 2.2f) { refl *= 2.2f / reflMax; }
            } else {
                refl = waterSky(rd);
            }

            // The sun's glint on the waves (broader GGX lobe prevents texel-facet white scratches in motion).
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
                glint = min(sunColour.rgb * (D * Fh * nl / (4.0f * max(nv, 0.08f))) * vis, 2.5f.xxx) * waterA.w;
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
                const float3 bed = PreWater[q].rgb;
                const float path = length(worldAt(float2(q), Depth.Load(int3(q, 0))) - p);
                const float3 T = exp(-waterA.z * float3(2.2f, 1.0f, 0.7f) * path);
                const float3 scatter = float3(0.05f, 0.10f, 0.11f) * max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.0f.xxx) * 2.0f;
                // Smoothly feather RT water body into distant water LOD well before Skyrim's 4096-unit cell edge.
                const float nearW = 1.0f - smoothstep(1800.0f, 3400.0f, length(p));
                // Subsurface turquoise wave-crest forward scatter when backlit by the sun.
                const float3 waveSSS = sunDir.w > 0.5f
                    ? float3(0.04f, 0.22f, 0.24f) * sunColour.rgb * pow(saturate(dot(-v, sunDir.xyz)), 4.0f) * saturate((1.0f - n.z) * 7.0f) * 0.45f
                    : 0.0f.xxx;
                // Animated shore-contact foam + turbulent rapids/waterfall whitewater crests.
                const float shoreBand = (1.0f - smoothstep(0.0f, 20.0f, depthV)) * smoothstep(0.0f, 1.5f, depthV);
                const float foamCell = saturate(1.0f - cellWalls(wpW.xy / 28.0f, camPosW.w * 0.38f) * 2.4f);
                const float crestFoam = saturate((1.0f - n.z) * 11.0f - 0.12f) * foamCell;
                const float foamMask = saturate(max(shoreBand * (0.45f + 0.55f * foamCell * foamCell), crestFoam * 0.72f)) * nearW;
                const float3 foamLit = (max(ambientAt(float3(0.0f, 0.0f, 1.0f)), 0.05f.xxx) + (sunDir.w > 0.5f ? sunColour.rgb * 0.45f : 0.0f.xxx)) * 0.85f;
                const float3 waterBody = (bed + caus) * T + scatter * (1.0f - T) + waveSSS;
                body = lerp(game.rgb, lerp(waterBody, foamLit, foamMask * 0.65f), saturate(waterB.z) * nearW);
            }
            const float shoreFeather = dBed < 1.0f ? smoothstep(0.0f, 8.0f, max(p.z - worldAt(px, dBed).z, 0.0f)) : 1.0f;
            const float cellFeather = 1.0f - smoothstep(1800.0f, 3400.0f, length(p));
            const float farSkyF = saturate(F * waterA.x * 0.42f * (1.0f - cellFeather));
            const float effF = saturate(F * waterA.x * shoreFeather * cellFeather);
            const uint view = uint(waterB.x + 0.5f);
            const float3 farSkyRefl = waterSky(rd);
            const float3 baseCol = refr ? lerp(body, refl, effF) : lerp(game.rgb, refl, effF) + caus * (1.0f - effF);
            col = lerp(baseCol, farSkyRefl, farSkyF) + glint * shoreFeather;
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
