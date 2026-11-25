// Distant LOD lit by RAW (FOL-04). Included by Lighting.hlsl after its resources and helpers. Original work, MIT.
#ifndef RAW_DISTANT_LOD_HLSLI
#define RAW_DISTANT_LOD_HLSLI

bool lodAt(int2 q) {
    q = clamp(q, int2(0, 0), int2(dims.xy) - 1);
    return (uint(Spec.Load(int3(q, 0)).z * 255.0f + 0.5f) & 255u) == 7u;
}

// Distant LOD in improved mode: the base colour the patch tapped, lit by the sun with the shadows that reach that
// far (cascades where they cover it, the ray-traced sun visibility, the clouds' shadow), a mild wrap for the canopy
// cards and the directional ambient times the sky the pixel sees. No lobe: LOD textures carry no specular mask.
float3 distantLit(int3 ip, float2 at, float d) {
    const float2 px = floor(at);
    const float3 p = worldAt(px, d);
    float3 albedo = Albedo.Load(ip).rgb;
    const uint seasonL = uint(waterB.w + 0.5f);
    if (seasonL == 3u) {
        // Autumn: shift distant grass and tree foliage toward warm golden-amber/russet.
        const float grn = saturate((albedo.g - max(albedo.r, albedo.b) * 0.85f) * 3.5f);
        albedo = lerp(albedo, float3(albedo.g * 1.25f, albedo.g * 0.74f, albedo.b * 0.42f), grn * 0.72f);
    } else if (seasonL == 4u) {
        // Winter: dust distant slopes and grass with crisp snowpack matching foreground winter snow.
        const float snowSlope = saturate((worldNormal(Normal.Load(ip)).z - 0.35f) * 2.2f);
        albedo = lerp(albedo, float3(0.54f, 0.58f, 0.64f), snowSlope * 0.78f);
    }
    const float3 wpLod = p + camPosW.xyz;
    // Live weather snow & rain response across distant terrain/mountain LODs (eliminates 4,000u weather pop-in boundary!).
    const float rawNz = worldNormal(Normal.Load(ip)).z;
    if (weather.z > 0.05f && rawNz > 0.30f) {
        const float lodSnow = saturate((rawNz - 0.30f) * 2.2f) * saturate(weather.z * 1.15f);
        albedo = lerp(albedo, float3(0.58f, 0.63f, 0.70f), lodSnow * 0.78f);
    }
    if (weather.x > 0.05f) {
        albedo *= lerp(1.0f, 0.80f, saturate(weather.x) * 0.65f);
    }
    // Tree LOD is a card whose G-buffer normal is one direction for the whole card (view 14, ec7-3), so a lit card
    // still reads flat. A canopy normal stands in: where the pixel sits across the card's silhouette on screen
    // (its distance to the card's edge left, right, up and down, up to 24 pixels) bends the normal from the camera
    // toward screen right and up, the way a rounded crown turns. 70 % canopy, 30 % the card's own normal.
    int l = 0, r = 0, t = 0, b = 0;
    [loop] for (int k = 1; k <= 24 && l == k - 1; ++k) { if (lodAt(ip.xy + int2(-k, 0))) l = k; }
    [loop] for (int k = 1; k <= 24 && r == k - 1; ++k) { if (lodAt(ip.xy + int2(k, 0))) r = k; }
    [loop] for (int k = 1; k <= 24 && t == k - 1; ++k) { if (lodAt(ip.xy + int2(0, -k))) t = k; }
    [loop] for (int k = 1; k <= 24 && b == k - 1; ++k) { if (lodAt(ip.xy + int2(0, k))) b = k; }
    const float u = float(l - r) / float(l + r + 1), v = float(b - t) / float(t + b + 1);
    const float2 lim = dims.xy - 1.0f;
    const float3 rightRaw = worldAt(float2(min(px.x + 1.0f, lim.x), px.y), d) - p;
    const float rightLen2 = dot(rightRaw, rightRaw);
    const float3 right = (isfinite(rightLen2) && rightLen2 > 1.0e-10f) ? (rightRaw * rsqrt(rightLen2)) : float3(1.0f, 0.0f, 0.0f);
    const float3 toCamRaw = eyeAt() - p;
    const float toCamLen2 = dot(toCamRaw, toCamRaw);
    const float3 toCam = (isfinite(toCamLen2) && toCamLen2 > 1.0e-10f) ? (toCamRaw * rsqrt(toCamLen2)) : float3(0.0f, 0.0f, 1.0f);
    const float3 canopyRaw = right * (0.85f * u) + float3(0.0f, 0.0f, 0.3f + 0.4f * v) + toCam * sqrt(max(1.0f - u * u, 0.15f));
    const float canopyLen2 = dot(canopyRaw, canopyRaw);
    const float3 canopy = (isfinite(canopyLen2) && canopyLen2 > 1.0e-10f) ? (canopyRaw * rsqrt(canopyLen2)) : float3(0.0f, 0.0f, 1.0f);
    const float zDist = max(isfinite(viewZOf(d)) ? viewZOf(d) : 1000.0f, 0.1f);
    // Only on a card: something narrow across the screen (a pixel with LOD on both sides within 24 pixels). Terrain LOD
    // fills its region, l and r both reach 24 and the canopy normal turned the mountains toward the camera.
    // Overlap-resilient forest fallback: when dense pine billboards overlap (l+r >= 44), detect green evergreen foliage
    // chroma on non-horizontal cards so distant conifer walls keep 3D bough sculpting instead of flattening!
    const float cardNarrow = saturate((46.0f - float(l + r)) / 12.0f);
    const float greenForest = saturate((albedo.g - max(albedo.r, albedo.b) * 0.88f) * 5.0f) * smoothstep(0.92f, 0.55f, abs(rawNz));
    const float cw = max(0.72f * cardNarrow, 0.42f * greenForest * smoothstep(26000.0f, 4500.0f, zDist));
    // Reconstruct 3D conical bough relief on flat 2D distant tree LOD billboard cards using world-space noise.
    const float3 nBase = lerp(worldNormal(Normal.Load(ip)), canopy, cw);
    float3 n = (dot(nBase, nBase) > 1.0e-10f) ? normalize(nBase) : float3(0.0f, 0.0f, 1.0f);
    if (cw > 0.05f) {
        const float3 wpTree = p + camPosW.xyz;
        const float2 wTreeRaw = cloudWind.xy + float2(1.0f, 0.35f);
        const float2 wTree = wTreeRaw * rsqrt(max(dot(wTreeRaw, wTreeRaw), 1.0e-6f));
        const float swayT = sin(dot(wpTree.xy, wTree) * 0.0045f - camPosW.w * 1.65f) * 0.18f;
        const float bough = cNoise(wpTree * 0.018f + float3(wTree * swayT, wpTree.z * 0.025f)) * 2.0f - 1.0f;
        const float bough2 = cNoise(wpTree * 0.052f + 7.3f) * 2.0f - 1.0f;
        const float3 nTree = n + (right * (bough * 0.36f) + float3(0.0f, 0.0f, bough2 * 0.30f)) * cw;
        n = (dot(nTree, nTree) > 1.0e-10f) ? normalize(nTree) : n;
        // Tame chalky snow-pine billboards to match Lighting.hlsl near-field canopy ceiling:
        albedo = min(albedo, float3(0.58f, 0.62f, 0.60f));
    } else if (zDist > 2200.0f) {
        // Wave 35 + Wave 56 Seamless Grand Alpine Mountains & Continuous Seasonal Snowline Elevation Blend:
        // Analytical-derivative tri-planar granite cliff strata + snowpack-coupled snowline (seasonThermal0.x)
        // smoothly ramping in over [2200..5200]u for zero LOD pop-in seam!
        const float3 wpM = wpLod;
        const float distRamp = smoothstep(2200.0f, 5200.0f, zDist);
        const float4 sGrad = rawNoise3Grad(float3(wpM.xy * 0.00085f, wpM.z * 0.0038f));
        const float4 cGrad = rawNoise3Grad(wpM * 0.0026f + 11.7f);
        const float strata = sGrad.x * 2.0f - 1.0f;
        const float3 nM = n + (cGrad.yzw * 0.30f + float3(sGrad.y * 0.18f, sGrad.z * 0.18f, strata * 0.18f)) * distRamp;
        n = (dot(nM, nM) > 1.0e-10f) ? normalize(nM) : n;
        const float lodCliff = smoothstep(0.74f, 0.32f, rawNz);
        albedo *= lerp(1.0f, lerp(0.76f, 1.22f, sGrad.x), lodCliff * 0.62f * distRamp);

        // Seasonal snowline elevation blend driven by live RawWeatherSim snowpack (seasonThermal0.x, surfaceTempC w)
        // + winter season + weather.z snowfall, organically sculpted by strata/gully noise:
        const float thermSnow = saturate(max(seasonThermal0.x, 0.0f) * 1.45f);
        const float coldBias  = (seasonThermal0.w < 2.0f && abs(seasonThermal0.w) > 1.0e-4f) ? saturate((2.0f - seasonThermal0.w) / 14.0f) * 0.35f : 0.0f;
        const float pack01    = saturate(max(thermSnow, max(weather.z * 0.85f, seasonL == 4u ? 0.62f : 0.0f)) + coldBias * 0.35f);
        const float snowLineLo = lerp(3100.0f, -450.0f, pack01);
        const float snowLineHi = snowLineLo + 2500.0f;
        const float gullyPerturb = (cGrad.x - 0.5f) * 720.0f + (sGrad.x - 0.5f) * 320.0f;
        const float elevSnow = smoothstep(snowLineLo, snowLineHi, wpM.z + gullyPerturb);
        const float slopeHold = smoothstep(0.38f, 0.78f, n.z);
        const float ridgeSnow = elevSnow * slopeHold * lerp(0.55f, 0.84f, pack01) * distRamp;
        if (ridgeSnow > 0.015f) {
            const float albQ = saturate(seasonThermal0.y > 0.05f ? seasonThermal0.y : 0.68f);
            const float3 snowCol = lerp(float3(0.58f, 0.64f, 0.72f), float3(0.68f, 0.74f, 0.82f), albQ);
            albedo = lerp(albedo, snowCol, saturate(ridgeSnow));
        }
    }
    const bool rt = (flags.x & FLAG_RT) != 0u;
    float sh = (flags.x & FLAG_SHADOWS) == 0u ? 1.0f : (flags.x & FLAG_CASCADES) != 0u ? cascadeShadow(p, d, true) : Mask.Load(ip).x;
    if (rt && sunColour.w > 0.5f) { sh = min(sh, RtVis.Load(ip).x); }
    float cloudVisLod = 1.0f;
    if (sunColour.w > 0.5f && (flags.x & FLAG_CLOUDS) != 0u && clouds.x > 0.005f) {
#ifdef RAW_HAS_UNIFIED_CLOUD_SHADOW
        // Wave 56: Unified 2-tap convective cloudShadowN so distant Tamriel ridges roll seamlessly under moving clouds!
        cloudVisLod = pow(saturate(cloudShadowN(wpLod, 2u)), 1.15f);
#else
        cloudVisLod = saturate(cloudShadow(wpLod));
#endif
        sh *= cloudVisLod;
    }
    const bool nearGrass = (zDist < 4200.0f) && (l + r < 44);
    const bool farTreeCard = (!nearGrass && cw > 0.05f);
    const float nl = nearGrass ? saturate((dot(n, sunDir.xyz) + 0.15f) / 1.15f)
                   : farTreeCard ? saturate((dot(n, sunDir.xyz) + 0.12f) / 1.12f)
                   : saturate((dot(n, sunDir.xyz) + 0.25f) / 1.25f);
    float3 amb = float3(dot(ambient[0].xyz, n) + ambient[0].w, dot(ambient[1].xyz, n) + ambient[1].w, dot(ambient[2].xyz, n) + ambient[2].w);
    const float skyVis = rt && (flags.x & FLAG_SKY) != 0u && sunColour.w > 0.5f ? RtIdx.Load(ip).w : 1.0f;
    // Physical sun-to-ambient HDR ratio matched to Lighting.hlsl so near terrain and distant mountains share one disciplined HDR scale.
    const float cloudC = saturate(clouds.x);
    const float aElevLod = astroSunElev();
    const float nightLod = smoothstep(0.03f, -0.06f, aElevLod);
    const float goldenLod = smoothstep(-0.04f, 0.04f, aElevLod) * (1.0f - smoothstep(0.14f, 0.34f, aElevLod));
    // Synchronize distant mountains & tree billboards with Lighting.hlsl's 24h Sun-vs-Moon direct lighting + sunset alpenglow!
    const float dirLumL = dot(sunColour.rgb, float3(0.2126f, 0.7152f, 0.0722f));
    float3 lodSunCol = sunColour.rgb;
    lodSunCol = lerp(lodSunCol, lodSunCol * float3(1.36f, 0.80f, 0.48f), goldenLod * 0.72f);
    const float3 lodMoonCol = float3(0.22f, 0.30f, 0.50f) * clamp(dirLumL * 0.72f, 0.14f, 0.46f);
    const float3 effSunCol = lerp(lodSunCol, lodMoonCol, nightLod);
    const float sunBoostL = lerp(1.20f, 1.00f, saturate(cloudC * 1.15f)) * lerp(1.0f, 0.78f, nightLod);
    const float ambScaleL = lerp(0.84f, 0.94f, cloudC) * lerp(0.88f, 1.0f, cloudVisLod);
    // Dynamic near-field grass lighting & distant tree-LOD crown self-shadowing (grounds bright snow pines against alpine ridges).
    const float grassAo = nearGrass && (flags.x & FLAG_AO) != 0u ? lerp(1.0f, AoHist.Load(ip).x, 0.80f) : 1.0f;
    const float occ = min(lerp(1.0f, pow(saturate(skyVis), 1.38f), max(cluster.w, 0.80f)), grassAo);
    const float crownAo = farTreeCard ? lerp(0.38f, 0.88f, saturate(v * 0.5f + 0.5f)) : 1.0f;
    amb *= (nearGrass ? lerp(occ.xxx, gtaoMultiBounce(occ, saturate(albedo)), 0.45f) * lerp(0.52f, 1.0f, saturate(v * 0.5f + 0.65f)) : (occ * crownAo).xxx) * ambScaleL;
    // What the ridges that block the sky send back (RtShadow.hlsl, farOccluded; [Lighting] RtFar), as the near bounce is.
    if (rt && (flags.x & FLAG_BOUNCE) != 0u && sunColour.w > 0.5f) { amb += RtBounce.Load(ip).rgb * proj.w * (farTreeCard ? 0.65f : 1.25f); }
    float3 direct = effSunCol * (nl * sh * sunBoostL * (farTreeCard ? 0.84f : 1.0f));
    // Warm sunrise/sunset canopy rim transmission on distant tree billboards (FOL-04 relit impostors).
    if (farTreeCard && sunColour.w > 0.5f && nightLod < 0.85f) {
        const float backTree = pow(saturate(dot(-toCam, sunDir.xyz)), 3.0f);
        direct += effSunCol * float3(0.52f, 0.82f, 0.34f) * (backTree * sh * 0.16f * (1.0f - nightLod));
    }
    if (nearGrass && sunColour.w > 0.5f) {
        // Pillar 7(a): Real-time GPU Grass Bending around player footsteps (footTrail[0..7]) + rolling wind-gust meadow waves!
        const float3 wpG = p + camPosW.xyz;
        const float2 windG = normalize(cloudWind.xy + float2(1.0f, 0.35f));
        const float gustG = sin(dot(wpG.xy, windG) * 0.0048f - camPosW.w * 2.15f) * 0.5f + 0.5f;
        float2 trampleBend = 0.0f.xx;
        float trampleOcc = 1.0f;
        [loop] for (uint gi = 0u; gi < 32u; ++gi) {
            const float4 ft = footTrail[gi];
            if (ft.w <= 0.05f) { continue; }
            const float2 gd = wpG.xy - ft.xy;
            const float r2 = dot(gd, gd);
            if (r2 < 2025.0f && abs(wpG.z - ft.z) < 120.0f) {
                const float rr = sqrt(r2);
                const float push = (1.0f - rr / 45.0f) * ft.w;
                trampleBend += (gd / max(rr, 1.0f)) * push * 0.72f;
                trampleOcc = min(trampleOcc, lerp(1.0f, 0.62f, push));
            }
        }
        // Wave 35 Interactive Camera-Body & Actor Grass Parting (Vidar / nintendobu) + Traveling Wind Gust Waves:
        const float2 fromEyeG = p.xy - eyeAt().xy;
        const float eyeD2 = dot(fromEyeG, fromEyeG);
        if (eyeD2 > 1.0f && eyeD2 < 9025.0f && abs(p.z - eyeAt().z) < 140.0f) {
            const float eDist = sqrt(eyeD2);
            const float ePush = saturate(1.0f - eDist / 95.0f);
            trampleBend += (fromEyeG / eDist) * (ePush * ePush * 0.85f);
            trampleOcc = min(trampleOcc, lerp(1.0f, 0.74f, ePush));
        }
        n = normalize(n + float3(trampleBend + windG * (gustG * 0.28f), 0.0f));
        // Per-blade directional micro-shadow march (grounds grass tufts cleanly inside terrain, rock & tree shadows).
        const float grassMicroSh = (!rt && zDist < 2600.0f) ? screenShadow(p + n * 0.6f, sunDir.xyz, 18.0f, 0.02f, ign(px), 6u) : 1.0f;
        const float gSh = sh * lerp(1.0f, grassMicroSh, saturate(1.0f - zDist / 2600.0f) * 0.82f) * trampleOcc;
        direct = effSunCol * (saturate(dot(n, sunDir.xyz)) * gSh * sunBoostL * lerp(0.88f, 1.16f, gustG));
        // Subtle deep-emerald chlorophyll subsurface transmission + root grounding (no neon-yellow blowout).
        const float backGrass = pow(saturate(dot(-toCam, sunDir.xyz)), 3.4f);
        direct += effSunCol * float3(0.62f, 0.94f, 0.38f) * (backGrass * (0.24f + gustG * 0.10f) * (1.0f - saturate(cloudC * 0.75f))) * gSh * sunBoostL;
        direct *= lerp(0.58f, 1.0f, grassAo);
    }
    // True linear HDR distant LOD output (removes 2011 double knee3() crush and duplicate fog; LightFog.hlsl applies unified Hillaire aerial perspective).
    float3 outCol = albedo * (direct + max(amb, 0.0f.xxx));

    // Wave 56: Altitude-Stratified River-Valley Mist Cohesion (distant exterior terrain & mountain LODs ONLY):
    // Distant ridges rise smoothly out of river-valley inversion mist (coupled with engineWorld.x waterHeight,
    // phenology1.z inversionStrength01, golden-hour dawn/dusk & cloudVisLod) with zero pop-in seam!
    if (sunColour.w > 0.5f && zDist > 2600.0f) {
        const float valleyFloorZ = (engineWorld.z > 0.5f && engineWorld.x > -13000.0f)
            ? engineWorld.x
            : min(camPosW.z - 260.0f, -120.0f);
        const float hAboveValley = max(wpLod.z - valleyFloorZ, 0.0f);
        const float invStr = saturate(phenology1.z);
        const float humidLod = saturate(weather.x * 0.50f + weather.w * 0.75f + cloudC * 0.35f);
        const float mistScaleH = lerp(850.0f, 1650.0f, saturate(invStr * 0.65f + humidLod * 0.45f));
        const float valleyPool = exp(-min(hAboveValley / max(mistScaleH, 100.0f), 24.0f));
        const float distMistW = smoothstep(2600.0f, 18500.0f, zDist);
        const float mistFactor = saturate(valleyPool * distMistW * lerp(0.18f, 0.42f, saturate(invStr * 0.60f + goldenLod * 0.35f + humidLod * 0.35f)));
        if (mistFactor > 0.004f) {
            const float3 dayMist = lerp(float3(0.32f, 0.44f, 0.62f), float3(0.46f, 0.52f, 0.60f), cloudC) * lerp(0.62f, 1.0f, cloudVisLod);
            const float3 goldMist = float3(0.68f, 0.42f, 0.28f) * lerp(0.55f, 1.0f, cloudVisLod);
            const float3 nightMist = float3(0.022f, 0.036f, 0.068f);
            float3 mistCol = lerp(dayMist, goldMist, goldenLod * 0.68f);
            mistCol = lerp(mistCol, nightMist, nightLod);
            outCol = lerp(outCol, mistCol, mistFactor);
        }
    }
    return all(isfinite(outCol)) ? max(outCol, 0.0f.xxx) : 0.0f.xxx;
}

#endif
