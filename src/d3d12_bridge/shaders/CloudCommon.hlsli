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

// Density at an absolute world point with spherical horizon curvature, wind-sheared cumulus, and high cirrus wisps.
float cloudDensity(float3 pa, bool detail) {
    const float2 dxy = pa.xy - camPosW.xy;
    const float curvedZ = pa.z + dot(dxy, dxy) * 1.35e-8f;
    const float h = (curvedZ - clouds.y) / clouds.z;   // 0 at the floor, 1 at the top
    if (h <= 0.0f || h >= 1.0f) { return 0.0f; }
    const float3 wind = float3(cloudWind.xy * camPosW.w, 0.0f);
    // Anisotropic wind shear leans upper cloud tops naturally along the wind vector.
    const float3 sheared = pa + wind + float3(cloudWind.xy, 0.0f) * (h * 18.0f);
    const float3 q = sheared / 10500.0f;
    float shape = cNoise(q) * 0.52f + cNoise(q * 2.07f + 11.3f) * 0.30f + cNoise(q * 4.19f + 5.7f) * 0.18f;
    // Cumulus vertical profile: flat condensation base, billowed anvil top.
    const float profile = saturate(h * 7.5f) * saturate((1.0f - h) * lerp(2.8f, 1.45f, clouds.x));
    float dens = saturate((shape * profile - (1.0f - clouds.x)) / max(clouds.x * 0.42f, 0.03f));
    if (detail && dens > 0.0f) {
        const float3 r = (sheared + wind * 0.6f) / 2200.0f;
        const float n1 = 1.0f - abs(cNoise(r) * 2.0f - 1.0f);
        const float n2 = 1.0f - abs(cNoise(r * 2.85f + 3.1f) * 2.0f - 1.0f);
        dens = saturate(dens - (n1 * 0.62f + n2 * 0.38f) * 0.68f * (1.0f - dens * 0.65f));
    }
    // High-altitude streaked cirrus veil near the top of the layer.
    if (detail && h > 0.72f && clouds.x > 0.08f) {
        const float3 cq = float3(sheared.x * 0.000045f, sheared.y * 0.00014f, h * 3.0f);
        const float cMask = saturate((cNoise(cq) + 0.5f * cNoise(cq * 2.3f + 4.1f) - 0.92f) * 2.4f);
        const float cVert = saturate((h - 0.72f) * 6.0f) * saturate((1.0f - h) * 8.0f);
        dens = max(dens, cMask * cVert * 0.28f * saturate(clouds.x * 1.4f));
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
