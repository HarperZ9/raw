// Weather.hlsli: rain on the game's surfaces (SKY-11) and snow (SKY-09), shared by the lighting pass and the ray trace so
// both agree on which pixels are wet. Rain wets what the sky can reach: upward faces fully, walls a
// little, and only where the pixel sees the sky (the ray-traced sky visibility, where the caller has
// it). Water gathers in puddles on flat ground, where a value-noise field over the absolute world
// position (fixed to the world, not the camera) rises above the puddle level. A wet surface darkens
// (water fills its pores) and turns glossy; a puddle is a flat mirror. weather: x wetness, y puddles,
// both 0 to 1 ([Lighting] Wetness, Puddles; the game's weather drives them once the engine exposes it).
// Needs LightingCommon.hlsli. Original work, MIT.

float hash21(float2 p) {
    p = frac(p * float2(123.34f, 456.21f));
    p += dot(p, p + 45.32f);
    return frac(p.x * p.y);
}

float valueNoise(float2 x) {
    const float2 i = floor(x), f = frac(x);
    const float2 u = f * f * (3.0f - 2.0f * f);
    return lerp(lerp(hash21(i), hash21(i + float2(1, 0)), u.x), lerp(hash21(i + float2(0, 1)), hash21(i + float2(1, 1)), u.x), u.y);
}

// Puddle field: three octaves, features about 200 game units (3 m) across.
float puddleField(float2 w) {
    const float2 q = w / 200.0f;
    return valueNoise(q) * 0.55f + valueNoise(q * 2.13f + 7.1f) * 0.3f + valueNoise(q * 4.37f + 3.3f) * 0.15f;
}

struct Wet { float wet, puddle, snow; };

// Rain on standing water: each cell of a world-fixed grid (ringCell units) takes a drop about once a
// second at a random place and time; its ring spreads and fades. Two offset layers so the rings overlap.
// Returns the slope of the water's surface (a gradient in x, y), to tilt a puddle's normal.
float2 rainRings(float2 w, float t) {
    const float ringCell = 9.0f;
    float2 g = 0.0f;
    [unroll] for (uint layer = 0u; layer < 2u; layer++) {
        const float2 q = w / ringCell + float(layer) * float2(5.37f, 3.11f);
        const float2 c = floor(q);
        [unroll] for (int dy = -1; dy <= 1; dy++) {
            [unroll] for (int dx = -1; dx <= 1; dx++) {
                const float2 cell = c + float2(dx, dy);
                const float2 at = cell + float2(hash21(cell), hash21(cell + 19.1f));
                const float age = frac(t * 0.9f + hash21(cell + 7.7f + float(layer)));
                const float2 dv = q - at;
                const float dist = length(dv);
                const float x = (dist - age * 1.3f) * 14.0f;   // the ring's crest at age x 1.3 cells
                const float env = (1.0f - age) * (1.0f - age) * saturate(1.0f - abs(x) / 9.42f);
                if (dist > 1.0e-4f) { g += dv / dist * sin(x) * env; }
            }
        }
    }
    return g;
}

// Glints on snow: grains on a world-fixed grid (grain units, growing with distance so a grain never gets
// smaller than about a pixel), one in six a flat crystal with its own tilt; it flashes when it mirrors the
// light into the eye, so the glints move as the camera does.
float snowGlint(float3 pa, float3 n, float3 v, float3 l, float grain) {
    const float3 q = floor(pa / grain);
    const float h = hash21(q.xy + q.z * 13.7f);
    if (h < 0.83f) { return 0.0f; }
    const float3 tilt = float3(hash21(q.xy + 3.1f), hash21(q.yz + 5.3f), hash21(q.zx + 9.7f)) - 0.5f;
    const float3 fn = normalize(n + tilt * 1.4f);
    return pow(saturate(dot(fn, normalize(l + v))), 600.0f);
}

// ng: the geometric normal (from depth): water pools by the ground's shape, not its normal map's bumps.
Wet wetAt(float3 p, float3 n, float3 ng, float skyVis) {
    Wet w;
    w.wet = 0.0f; w.puddle = 0.0f; w.snow = 0.0f;
    if (weather.x <= 0.0f && weather.y <= 0.0f && weather.z <= 0.0f) { return w; }
    if (weather.z > 0.0f) {
        // Snow (SKY-09) settles where the geometric surface faces up and the sky reaches; it thins in
        // drifts by a world-fixed field and thickens with the amount. It covers before it soaks.
        const float face = saturate((ng.z - 0.35f) / 0.45f) * saturate((n.z - 0.2f) / 0.5f);
        const float drift = puddleField((p + camPosW.xyz).xy * 0.7f + 31.0f);
        w.snow = saturate((face * (0.55f + 0.45f * drift) - (1.0f - weather.z)) * 3.0f) * skyVis;
    }
    const float up = saturate((n.z - 0.15f) / 0.6f);
    w.wet = saturate(weather.x * lerp(0.35f, 1.0f, up) * skyVis);
    if (weather.y > 0.0f) {
        const float flat = saturate((ng.z - 0.9f) / 0.07f);
        const float f = puddleField((p + camPosW.xyz).xy);
        w.puddle = saturate((f - (1.0f - 0.6f * weather.y)) * 8.0f) * flat * skyVis;
    }
    return w;
}
