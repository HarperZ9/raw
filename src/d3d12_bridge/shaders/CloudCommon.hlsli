// CloudCommon.hlsli: one cloud layer for the sky (Clouds.hlsl), its shadow on the ground (Lighting.hlsl)
// and the shafts of sun between clouds in the fog (LightFog.hlsl), so all three see the same clouds.
// The layer is a slab of absolute world height clouds.y to clouds.y + clouds.z (game units), filled by a
// density field: low-frequency shape noise, cut by the coverage (clouds.x, from the game's weather and
// [Lighting] CloudCover), rounded at its floor and top, eroded by finer noise, carried by the wind
// (cloudWind.xy, units a second; camPosW.w is the clock). Needs LightingCommon.hlsli. Original work, MIT.

float cHash(float3 p) {
    p = frac(p * 0.3183099f + 0.1f);
    p *= 17.0f;
    return frac(p.x * p.y * p.z * (p.x + p.y + p.z));
}

float cNoise(float3 x) {
    const float3 i = floor(x), f = frac(x);
    const float3 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(lerp(cHash(i), cHash(i + float3(1, 0, 0)), u.x), lerp(cHash(i + float3(0, 1, 0)), cHash(i + float3(1, 1, 0)), u.x), u.y),
                lerp(lerp(cHash(i + float3(0, 0, 1)), cHash(i + float3(1, 0, 1)), u.x), lerp(cHash(i + float3(0, 1, 1)), cHash(i + float3(1, 1, 1)), u.x), u.y),
                u.z);
}

// Fast 2x2x2 inverted Worley cellular noise for RDR2/Horizon-style cauliflower cumulus billows.
float cWorley(float3 p) {
    const float3 id = floor(p);
    const float3 f = frac(p);
    float minD = 1.0f;
    [unroll] for (int z = 0; z <= 1; z++) {
        [unroll] for (int y = 0; y <= 1; y++) {
            [unroll] for (int x = 0; x <= 1; x++) {
                const float3 cell = id + float3(x, y, z);
                const float h0 = cHash(cell);
                const float h1 = cHash(cell + float3(17.3f, 31.7f, 11.1f));
                const float h2 = cHash(cell + float3(43.1f, 13.9f, 29.3f));
                const float3 pt = float3(x, y, z) + float3(h0, h1, h2) * 0.85f;
                const float3 d = pt - f;
                minD = min(minD, dot(d, d));
            }
        }
    }
    return 1.0f - saturate(sqrt(minD));
}

bool cloudsOn() { return clouds.x > 0.0f && clouds.z > 0.0f; }
static const float kCloudSigma = 0.0022f;   // crisp optical density for detailed cumulus edges

// High-frequency 4-octave photographic Worley-Perlin cumulus + crisp wispy cirrus veil.
float cloudDensity(float3 pa, bool detail) {
    const float2 dxy = pa.xy - camPosW.xy;
    const float curvedZ = pa.z + dot(dxy, dxy) * 1.15e-8f;
    const float h = (curvedZ - clouds.y) / clouds.z;   // 0 at floor, 1 at top
    if (h <= 0.0f || h >= 1.0f) { return 0.0f; }
    const float3 wind = float3(cloudWind.xy * camPosW.w, 0.0f);
    const float3 sheared = pa + wind + float3(cloudWind.xy, 0.0f) * (h * 6.0f);
    // Macro weather coverage field (distinct, well-spaced fair-weather cumulus banks).
    const float3 q = float3(sheared.x / 26000.0f, sheared.y / 24000.0f, h * 1.15f);
    const float warp = cNoise(q * 1.15f + 7.1f) * 0.35f;
    const float baseFbm = cNoise(q + warp) * 0.50f + cNoise(q * 2.15f + 11.3f) * 0.30f + cNoise(q * 4.45f + 5.7f) * 0.20f;
    const float macroWorley = cWorley(q * 2.1f + 2.7f);
    const float shape = lerp(baseFbm, baseFbm * 0.58f + macroWorley * 0.52f, 0.55f);
    // Flat condensation base + sculpted cumulus towers.
    const float baseFlat = saturate(h / 0.12f);
    const float topRound = saturate((1.0f - h) / lerp(0.42f, 0.20f, saturate(clouds.x)));
    const float cover = saturate(clouds.x);
    // Calibrated threshold so fair-weather cover (0.30..0.50) produces bold, distinct cumulus clouds with clear blue sky between them.
    const float thresh = lerp(0.56f, 0.22f, cover);
    float dens = saturate((shape * baseFlat * topRound - thresh) / max(0.32f, 0.14f));
    if (detail && dens > 0.0f) {
        // Multi-scale high-frequency Worley + FBM erosion for razor-sharp cauliflower cumulus edges.
        const float3 r1 = float3((sheared.x + wind.x * 0.35f) / 3600.0f, (sheared.y + wind.y * 0.35f) / 3400.0f, h * 5.8f);
        const float w1 = 1.0f - cWorley(r1);
        const float f2 = cNoise(r1 * 2.13f + 3.7f);
        const float f3 = cNoise(r1 * 4.61f + 9.2f);
        const float f4 = cNoise(r1 * 9.37f + 17.5f);
        const float fineErode = w1 * 0.42f + f2 * 0.28f + f3 * 0.19f + f4 * 0.11f;
        const float erodeAmt = lerp(0.45f, 0.72f, h) * (1.0f - dens * 0.55f);
        dens = saturate((dens - fineErode * erodeAmt) / max(1.0f - fineErode * 0.42f, 0.28f));
    }
    // Crisp high-altitude streaked cirrus wisps (upper 26% of the slab).
    if (detail && h > 0.74f && clouds.x > 0.05f) {
        const float3 cq = float3(sheared.x * 0.000045f, sheared.y * 0.00018f, h * 3.0f);
        const float cCurl = cNoise(cq * 1.4f + 1.7f) * 0.65f;
        const float cMask = saturate((cNoise(cq + cCurl) * 0.55f + cNoise(cq * 2.7f + 4.1f) * 0.30f + cNoise(cq * 5.8f + 8.3f) * 0.15f - 0.56f) * 3.2f);
        const float cVert = saturate((h - 0.74f) * 7.0f) * saturate((1.0f - h) * 9.0f);
        dens = max(dens, cMask * cVert * 0.26f * saturate(clouds.x * 1.25f));
    }
    return dens * cloudWind.z;
}

// The layer's height range a ray crosses, from an absolute origin; false when it misses or ends first.
bool cloudSpan(float3 o, float3 dir, float tMax, out float t0, out float t1) {
    const float top = clouds.y + clouds.z;
    if (abs(dir.z) < 1.0e-5f) {
        t0 = 0.0f; t1 = tMax;
        return o.z > clouds.y && o.z < top;
    }
    const float a = (clouds.y - o.z) / dir.z, b = (top - o.z) / dir.z;
    t0 = max(min(a, b), 0.0f);
    t1 = min(max(a, b), tMax);
    return t1 > t0;
}

// How much sun reaches an absolute point through the layer: n samples up the sun's path through it
// (3 for the ground, 1 for each step of the fog's march).
float cloudShadowN(float3 pa, uint n) {
    if (!cloudsOn() || sunDir.z <= 0.02f || clouds.w <= 0.0f) { return 1.0f; }
    float t0, t1;
    if (!cloudSpan(pa, sunDir.xyz, 1.0e7f, t0, t1)) { return 1.0f; }
    const float dt = (t1 - t0) / float(n);
    float od = 0.0f;
    [loop] for (uint i = 0u; i < n; i++) { od += cloudDensity(pa + sunDir.xyz * (t0 + (float(i) + 0.5f) * dt), false); }
    return lerp(1.0f, exp(-od * dt * kCloudSigma), clouds.w);
}
float cloudShadow(float3 pa) { return cloudShadowN(pa, 3u); }
