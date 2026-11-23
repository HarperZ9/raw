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

float cRemap(float v, float lo, float hi, float nLo, float nHi) {
    return nLo + saturate((v - lo) / max(hi - lo, 1.0e-5f)) * (nHi - nLo);
}

// Mathematically C2-continuous 3D domain-warped billow noise (100% zero cell boundaries or straight seams ever).
float cBillow(float3 p) {
    const float w = cNoise(p * 0.85f + float3(3.1f, 7.4f, 1.9f)) * 0.65f;
    const float n0 = cNoise(p + w);
    const float n1 = cNoise(p * 2.07f + float3(11.3f, 5.1f, 8.7f) - w * 0.5f);
    return saturate(1.0f - abs((n0 * 0.65f + n1 * 0.35f) * 2.0f - 1.0f));
}

bool cloudsOn() { return clouds.x > 0.0f && clouds.z > 0.0f; }
static const float kCloudSigma = 0.0038f;   // crisp, well-defined cumulus optical density (no foggy blurry puffs)

// Seam-free C2-continuous 6-octave curl-eroded cumulus volume + sculptural broken coverage (zero Swiss-cheese sheets).
float cloudDensity(float3 pa, bool detail) {
    const float2 dxy = pa.xy - camPosW.xy;
    const float curvedZ = pa.z + dot(dxy, dxy) * 1.15e-8f;
    const float h = (curvedZ - clouds.y) / clouds.z;   // 0 at floor, 1 at top
    if (h <= 0.0f || h >= 1.0f) { return 0.0f; }
    const float3 wind = float3(cloudWind.xy * camPosW.w, 0.0f);
    const float3 sheared = pa + wind + float3(cloudWind.xy, 0.0f) * (h * 6.0f);
    const float cover = saturate(clouds.x);
    const float stormType = saturate(weather.w * 1.4f + max(cover - 0.78f, 0.0f) * 2.5f);
    const float topCap = lerp(0.72f, 0.96f, stormType);
    const float baseGrad = smoothstep(0.0f, lerp(0.14f, 0.07f, stormType), h);
    const float topGrad  = 1.0f - smoothstep(topCap * 0.58f, topCap, h);
    const float anvilSpread = stormType * smoothstep(0.65f, 0.88f, h) * (1.0f - smoothstep(0.88f, 0.99f, h)) * 0.16f;
    const float hGrad = saturate(baseGrad * topGrad + anvilSpread);
    // Sculpted broken cumulus banks with soft feathered edges (eliminates flat white Swiss-cheese sheet with sharp dark holes).
    const float3 q = float3(sheared.x / 18000.0f, sheared.y / 17000.0f, h * 1.35f);
    const float qW = cNoise(q * 1.15f + 7.1f) * 0.48f;
    const float baseFbm = cNoise(q + qW) * 0.46f + cNoise(q * 2.13f + 11.3f) * 0.32f + cNoise(q * 4.37f + 5.7f) * 0.22f;
    const float macroBillow = cBillow(q * 1.85f + 2.7f);
    const float shape = lerp(baseFbm, baseFbm * 0.45f + macroBillow * 0.58f, 0.65f);
    const float effCov = saturate(cover * 0.78f);
    const float thresh = lerp(0.68f, lerp(0.48f, 0.34f, stormType), effCov);
    float dens = pow(saturate((shape * hGrad - thresh) / max(1.0f - thresh, 0.26f)), 1.35f);
    if (detail && dens > 0.0f) {
        // High-frequency C2-continuous curl-warped wispy detail erosion (crisp cumulus edges even overhead).
        const float3 r1 = float3((sheared.x + wind.x * 0.35f) / 1800.0f, (sheared.y + wind.y * 0.35f) / 1700.0f, h * 7.0f);
        const float curl = (cNoise(r1 * 0.80f + float3(1.7f, 5.3f, 2.1f)) - 0.5f) * 0.68f;
        const float3 rW = r1 + float3(curl, -curl * 0.85f, 0.0f);
        const float b1 = 1.0f - cBillow(rW);
        const float f2 = cNoise(rW * 2.23f + 3.7f);
        const float f3 = cNoise(rW * 4.71f + 9.2f);
        const float f4 = cNoise(rW * 9.63f + 17.5f);
        const float fineErode = b1 * 0.36f + f2 * 0.26f + f3 * 0.21f + f4 * 0.17f;
        const float erodeAmt = lerp(0.46f, 0.72f, h) * (1.0f - dens * 0.45f);
        dens = cRemap(dens, fineErode * erodeAmt, 1.0f, 0.0f, 1.0f);
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
