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
    const float3 right = normalize(worldAt(float2(min(px.x + 1.0f, lim.x), px.y), d) - p + 1.0e-4f);
    const float3 toCam = normalize(eyeAt() - p);
    const float3 canopy = normalize(right * (0.85f * u) + float3(0.0f, 0.0f, 0.3f + 0.4f * v) + toCam * sqrt(max(1.0f - u * u, 0.15f)));
    // Only on a card: something narrow across the screen (a pixel with LOD on both sides within 24 pixels). Terrain LOD
    // fills its region, l and r both reach 24 and the canopy normal turned the mountains toward the camera: their faces
    // read flat beside the game's (far mountains' luma spread 11.3 against 12.7, rt7-far-7).
    const float cw = 0.7f * saturate((46.0f - float(l + r)) / 12.0f);
    // Reconstruct 3D conical bough relief on flat 2D distant tree LOD billboard cards using world-space noise.
    float3 n = normalize(lerp(worldNormal(Normal.Load(ip)), canopy, cw));
    if (cw > 0.05f) {
        const float3 wpTree = p + camPosW.xyz;
        const float bough = cNoise(wpTree * 0.018f + float3(0.0f, 0.0f, wpTree.z * 0.025f)) * 2.0f - 1.0f;
        const float bough2 = cNoise(wpTree * 0.052f + 7.3f) * 2.0f - 1.0f;
        n = normalize(n + (right * (bough * 0.28f) + float3(0.0f, 0.0f, bough2 * 0.24f)) * cw);
    } else if (viewZOf(d) > 3800.0f) {
        // Seamless Near-to-Far Terrain Continuity: tri-planar geological rock-strata + alpine scree contrast on distant mountain LODs!
        const float3 wpM = p + camPosW.xyz;
        const float strata = cNoise(float3(wpM.xy * 0.00085f, wpM.z * 0.0038f)) * 2.0f - 1.0f;
        const float crag = cNoise(wpM * 0.0026f + 11.7f) * 2.0f - 1.0f;
        n = normalize(n + float3(crag * 0.24f, strata * 0.26f, strata * 0.16f));
        const float lodCliff = smoothstep(0.72f, 0.35f, rawNz);
        albedo *= lerp(1.0f, lerp(0.80f, 1.18f, strata * 0.5f + 0.5f), lodCliff * 0.55f);
    }
    const bool rt = (flags.x & FLAG_RT) != 0u;
    float sh = (flags.x & FLAG_SHADOWS) == 0u ? 1.0f : (flags.x & FLAG_CASCADES) != 0u ? cascadeShadow(p, d, true) : Mask.Load(ip).x;
    if (rt && sunColour.w > 0.5f) { sh = min(sh, RtVis.Load(ip).x); }
    if (sunColour.w > 0.5f && (flags.x & FLAG_CLOUDS) != 0u) { sh *= cloudShadow(p + camPosW.xyz); }
    const float zDist = viewZOf(d);
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
    const float ambScaleL = lerp(0.84f, 0.94f, cloudC);
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
        // Per-blade directional micro-shadow march (grounds grass tufts cleanly inside terrain, rock & tree shadows).
        const float grassMicroSh = zDist < 2600.0f ? screenShadow(p + n * 0.6f, sunDir.xyz, 18.0f, 0.02f, ign(px), 6u) : 1.0f;
        const float gSh = sh * lerp(1.0f, grassMicroSh, saturate(1.0f - zDist / 2600.0f) * 0.82f);
        direct = effSunCol * (nl * gSh * sunBoostL);
        // Subtle deep-emerald chlorophyll subsurface transmission + root grounding (no neon-yellow blowout).
        const float backGrass = pow(saturate(dot(-toCam, sunDir.xyz)), 3.4f);
        direct += effSunCol * float3(0.62f, 0.94f, 0.38f) * (backGrass * 0.24f * (1.0f - saturate(cloudC * 0.75f))) * gSh * sunBoostL;
        direct *= lerp(0.58f, 1.0f, grassAo);
    }
    // True linear HDR distant LOD output (removes 2011 double knee3() crush and duplicate fog; LightFog.hlsl applies unified Hillaire aerial perspective).
    return albedo * (direct + max(amb, 0.0f));
}

#endif
