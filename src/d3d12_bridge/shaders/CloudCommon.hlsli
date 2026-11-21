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

bool cloudsOn() { return clouds.x > 0.0f && clouds.z > 0.0f; }
static const float kCloudSigma = 0.0015f;   // extinction per unit of density and game unit: about 670 units of full density is one optical depth

// Density at an absolute world point: kilometer-scale sweeping cloud decks with wispy wind-sheared erosion and cirrus veil.
float cloudDensity(float3 pa, bool detail) {
    const float2 dxy = pa.xy - camPosW.xy;
    const float curvedZ = pa.z + dot(dxy, dxy) * 1.15e-8f;
    const float h = (curvedZ - clouds.y) / clouds.z;   // 0 at the floor, 1 at the top
    if (h <= 0.0f || h >= 1.0f) { return 0.0f; }
    const float3 wind = float3(cloudWind.xy * camPosW.w, 0.0f);
    // Anisotropic wind shear leans upper cloud decks naturally along the wind vector.
    const float3 sheared = pa + wind + float3(cloudWind.xy, 0.0f) * (h * 45.0f);
    // Broad kilometer-scale cloud banks (52,000 units ~ 750 m per macro cell, stretched horizontally).
    const float3 q = float3(sheared.x / 56000.0f, sheared.y / 44000.0f, h * 1.4f);
    const float warp = cNoise(q * 0.85f + 7.1f) * 0.45f;
    float shape = cNoise(q + warp) * 0.50f + cNoise(q * 2.13f + 11.3f) * 0.30f + cNoise(q * 4.31f + 5.7f) * 0.20f;
    // Soft strato-cumulus vertical profile (avoids stubby cotton-ball spheres).
    const float profile = saturate(h * 5.0f) * saturate((1.0f - h) * lerp(2.2f, 1.35f, clouds.x));
    const float cover = saturate(clouds.x * 1.15f);
    float dens = saturate((shape * profile - (0.82f - cover * 0.52f)) / max(0.32f + cover * 0.25f, 0.15f));
    if (detail && dens > 0.0f) {
        const float3 r = float3((sheared.x + wind.x * 0.5f) / 9500.0f, (sheared.y + wind.y * 0.5f) / 7800.0f, h * 4.5f);
        const float n1 = 1.0f - abs(cNoise(r) * 2.0f - 1.0f);
        const float n2 = 1.0f - abs(cNoise(r * 2.65f + 3.1f) * 2.0f - 1.0f);
        dens = saturate(dens - (n1 * 0.58f + n2 * 0.42f) * 0.55f * (1.0f - dens * 0.55f));
    }
    // High-altitude streaked cirrus veil across the upper sky.
    if (detail && h > 0.68f && clouds.x > 0.05f) {
        const float3 cq = float3(sheared.x * 0.000018f, sheared.y * 0.000065f, h * 2.5f);
        const float cMask = saturate((cNoise(cq) + 0.5f * cNoise(cq * 2.4f + 4.1f) - 0.88f) * 2.0f);
        const float cVert = saturate((h - 0.68f) * 5.0f) * saturate((1.0f - h) * 7.0f);
        dens = max(dens, cMask * cVert * 0.24f * saturate(clouds.x * 1.3f));
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
