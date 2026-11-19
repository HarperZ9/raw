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

// Density at an absolute world point; detail adds the finer erosion (the sky's march, not the shadows).
float cloudDensity(float3 pa, bool detail) {
    const float h = (pa.z - clouds.y) / clouds.z;   // 0 at the floor, 1 at the top
    if (h <= 0.0f || h >= 1.0f) { return 0.0f; }
    const float3 wind = float3(cloudWind.xy * camPosW.w, 0.0f);
    const float3 q = (pa + wind) / 11000.0f;   // cloud cells about a kilometre (70,000 units) apart at full cover
    float shape = cNoise(q) * 0.55f + cNoise(q * 2.03f + 11.3f) * 0.3f + cNoise(q * 4.11f + 5.7f) * 0.15f;
    // Rounded floor, softer top; heavy cover rises higher.
    const float profile = saturate(h * 6.0f) * saturate((1.0f - h) * lerp(2.5f, 1.4f, clouds.x));
    // A sharper edge than a plain cut: the cloud's body fills within a short span of the cover threshold.
    float dens = saturate((shape * profile - (1.0f - clouds.x)) / max(clouds.x * 0.45f, 0.03f));
    if (detail && dens > 0.0f) {
        // Billowed erosion (1 - |2n - 1|) carves the soft body into heaped, rounded edges.
        const float3 r = (pa + wind * 1.6f) / 2600.0f;
        const float n1 = 1.0f - abs(cNoise(r) * 2.0f - 1.0f), n2 = 1.0f - abs(cNoise(r * 2.7f + 3.1f) * 2.0f - 1.0f);
        dens = saturate(dens - (n1 * 0.65f + n2 * 0.35f) * 0.6f * (1.0f - dens * 0.7f));
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
