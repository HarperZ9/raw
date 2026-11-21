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
static const float kCloudSigma = 0.00175f;   // sculpted optical depth for crisp billowing cumulus & dramatic storm decks

// Density at an absolute world point: RDR2/GTA6-inspired multi-altitude Worley-Perlin cumulus + fibrous cirrus veil.
float cloudDensity(float3 pa, bool detail) {
    const float2 dxy = pa.xy - camPosW.xy;
    const float curvedZ = pa.z + dot(dxy, dxy) * 1.15e-8f;
    const float h = (curvedZ - clouds.y) / clouds.z;   // 0 at the floor, 1 at the top
    if (h <= 0.0f || h >= 1.0f) { return 0.0f; }
    const float3 wind = float3(cloudWind.xy * camPosW.w, 0.0f);
    // Anisotropic wind shear leans upper cumulus towers and cirrus streaks along the wind vector.
    const float3 sheared = pa + wind + float3(cloudWind.xy, 0.0f) * (h * 52.0f);
    // Macro weather map (kilometer-scale cumulus decks with organic domain warp).
    const float3 q = float3(sheared.x / 52000.0f, sheared.y / 42000.0f, h * 1.35f);
    const float warp = cNoise(q * 0.80f + 7.1f) * 0.42f;
    const float perlinBase = cNoise(q + warp) * 0.52f + cNoise(q * 2.08f + 11.3f) * 0.30f + cNoise(q * 4.19f + 5.7f) * 0.18f;
    const float worleyMacro = cWorley(q * 1.55f + 2.7f);
    const float shape = lerp(perlinBase, perlinBase * 0.65f + worleyMacro * 0.48f, 0.55f);
    // Flat condensation base (sharp rise h 0..0.14) + towering cumulus anvil top (scales with coverage).
    const float baseFlat = saturate(h / 0.14f);
    const float topRound = saturate((1.0f - h) / lerp(0.45f, 0.22f, saturate(clouds.x)));
    const float profile = baseFlat * topRound;
    const float cover = saturate(clouds.x * 1.18f);
    float dens = saturate((shape * profile - (0.76f - cover * 0.48f)) / max(0.30f + cover * 0.22f, 0.15f));
    if (detail && dens > 0.0f) {
        // Cauliflower Worley billow erosion at edges while preserving dense convective cores.
        const float3 r = float3((sheared.x + wind.x * 0.45f) / 10500.0f, (sheared.y + wind.y * 0.45f) / 8800.0f, h * 4.2f);
        const float wErode = (1.0f - cWorley(r)) * 0.62f + (1.0f - abs(cNoise(r * 2.5f + 3.1f) * 2.0f - 1.0f)) * 0.38f;
        const float heightErode = lerp(0.48f, 0.72f, h);
        dens = saturate((dens - wErode * heightErode * (1.0f - dens * 0.50f)) / max(1.0f - wErode * 0.32f, 0.35f));
    }
    // High-altitude fibrous ice-crystal cirrus veil (upper 28% of the sky slab).
    if (detail && h > 0.72f && clouds.x > 0.04f) {
        const float3 cq = float3(sheared.x * 0.000016f, sheared.y * 0.000072f, h * 2.2f);
        const float cCurl = cNoise(cq * 0.9f + 1.7f) * 0.55f;
        const float cMask = saturate((cNoise(cq + cCurl) + 0.5f * cNoise(cq * 2.6f + 4.1f) - 0.86f) * 2.2f);
        const float cVert = saturate((h - 0.72f) * 6.0f) * saturate((1.0f - h) * 8.0f);
        dens = max(dens, cMask * cVert * 0.28f * saturate(clouds.x * 1.35f));
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
