// Weather.hlsli: rain on the game's surfaces (SKY-11), shared by the lighting pass and the ray trace so
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

struct Wet { float wet, puddle; };

// ng: the geometric normal (from depth): water pools by the ground's shape, not its normal map's bumps.
Wet wetAt(float3 p, float3 n, float3 ng, float skyVis) {
    Wet w;
    w.wet = 0.0f; w.puddle = 0.0f;
    if (weather.x <= 0.0f && weather.y <= 0.0f) { return w; }
    const float up = saturate((n.z - 0.15f) / 0.6f);
    w.wet = saturate(weather.x * lerp(0.35f, 1.0f, up) * skyVis);
    if (weather.y > 0.0f) {
        const float flat = saturate((ng.z - 0.9f) / 0.07f);
        const float f = puddleField((p + camPosW.xyz).xy);
        w.puddle = saturate((f - (1.0f - 0.6f * weather.y)) * 8.0f) * flat * skyVis;
    }
    return w;
}
