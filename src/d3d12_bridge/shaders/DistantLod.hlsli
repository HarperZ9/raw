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
    const float3 albedo = Albedo.Load(ip).rgb;
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
    const float3 n = normalize(lerp(worldNormal(Normal.Load(ip)), canopy, cw));
    const bool rt = (flags.x & FLAG_RT) != 0u;
    float sh = (flags.x & FLAG_SHADOWS) == 0u ? 1.0f : (flags.x & FLAG_CASCADES) != 0u ? cascadeShadow(p, d, true) : Mask.Load(ip).x;
    if (rt && sunColour.w > 0.5f) { sh = min(sh, RtVis.Load(ip).x); }
    if (sunColour.w > 0.5f && (flags.x & FLAG_CLOUDS) != 0u) { sh *= cloudShadow(p + camPosW.xyz); }
    const float nl = saturate((dot(n, sunDir.xyz) + 0.25f) / 1.25f);
    float3 amb = float3(dot(ambient[0].xyz, n) + ambient[0].w, dot(ambient[1].xyz, n) + ambient[1].w, dot(ambient[2].xyz, n) + ambient[2].w);
    const float skyVis = rt && (flags.x & FLAG_SKY) != 0u && sunColour.w > 0.5f ? RtIdx.Load(ip).w : 1.0f;
    amb *= lerp(1.0f, skyVis, cluster.w);
    // What the ridges that block the sky send back (RtShadow.hlsl, farOccluded; [Lighting] RtFar), as the near bounce is.
    if (rt && (flags.x & FLAG_BOUNCE) != 0u && sunColour.w > 0.5f) { amb += RtBounce.Load(ip).rgb * proj.w; }
    return knee3(knee3(albedo * (sunColour.rgb * (nl * sh) + max(amb, 0.0f))));
}

#endif
