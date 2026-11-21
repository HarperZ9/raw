// LightingCommon.hlsli: what the lighting pass (Lighting.hlsl) and its fog pass (LightFog.hlsl)
// share: the uniform, the G-buffer decode, the camera and the sun cascades. The layout of P
// mirrors LightUniform in LightingPass.cpp. Space is the game's camera-relative world space;
// a matrix row dots a column vector. Original work, MIT.

#define MAX_LIGHTS 128
#define TILES_X 16
#define TILES_Y 9
#define SLICES 16

cbuffer P : register(b0) {
    float4 dims;            // W, H, 1/W, 1/H
    float4 proj;            // projA, projB (depth = A - B / viewZ), frame index, bounce strength
    float4 clipToWorld[4];
    float4 worldToClip[4];
    float4 viewToWorld[3];
    float4 resScale;        // x, y: dynamic resolution; z: the game's knee; w: specular scale
    float4 sunDir;          // xyz towards the light; w: 1 with a sun
    float4 sunColour;       // rgb; w: 1 with cascades
    float4 ambient[3];
    float4 cascade[6];      // cascade k: rows 3k .. 3k+2
    float4 cascadeInfo;     // bias 0, bias 1, split depth, atlas side
    uint4  flags;           // x: FLAG_*; y: lights; z: cascades; w: view mode
    float4 cluster;         // near, far, SLICES / log(far / near), sky visibility strength
    float4 tune;            // fog density, fog anisotropy, soft-shadow range, contact length
    float4 tune2;           // AO strength, fog light scale, fog sun scale, fog max distance
    float4 weather;         // wetness, puddles, snow (Weather.hlsli), rain falling now
    float4 camPosW;         // the camera's world position (absolute: world-fixed patterns); w: seconds
    float4 clouds;          // cover 0 to 1, floor height, thickness (absolute world units), shadow strength (CloudCommon.hlsli)
    float4 cloudWind;       // wind x, y (units a second), density scale, history weight
    float4 waterA;          // RtWater.hlsl: reflection strength, caustic strength, absorption a unit, glint strength
    float4 waterB;          // RtWater.hlsl: view (1 reflection, 2 caustics, 3 Fresnel), caustic cell size (units), refraction share, 0
    float4 prevWorldToClip[4];   // last frame's worldToClip (the clouds' history)
    float4 lightPosR[MAX_LIGHTS];   // xyz, radius
    float4 lightColS[MAX_LIGHTS];   // rgb, shadow-mask channel (-1 none)
    uint4  clusters[TILES_X * TILES_Y * SLICES];   // a bit per light
};

static const uint FLAG_MATCH = 1u, FLAG_SOFT = 2u, FLAG_GGX = 4u, FLAG_FOG = 8u, FLAG_CONTACT = 16u, FLAG_AO = 32u,
                  FLAG_CASCADES = 64u, FLAG_SURFACE = 128u, FLAG_SHADOWS = 256u,
                  FLAG_FOG_REPLACES = 512u, FLAG_RT = 1024u, FLAG_SKY = 2048u, FLAG_BOUNCE = 4096u, FLAG_REFLECT = 8192u, FLAG_BOUNCE_IN = 16384u, FLAG_CLOUDS = 32768u, FLAG_REST = 65536u, FLAG_LOD = 131072u, FLAG_ENV = 262144u, FLAG_CHAR = 524288u;   // our fog takes the game's distance fog's place (off: the game's runs after ours)
static const float PI = 3.14159265358979f;

float4 vs(uint i : SV_VertexID) : SV_Position {
    return float4(i == 1u ? 3.0f : -1.0f, i == 2u ? -3.0f : 1.0f, 0.0f, 1.0f);
}

float viewZOf(float d) { return proj.y / max(proj.x - d, 1.0e-7f); }

// Camera-relative world position of a pixel centre and its depth-buffer value.
float3 worldAt(float2 px, float d) {
    const float2 uv = (px + 0.5f) * dims.zw;
    const float4 c = float4(uv.x * resScale.x * 2.0f - 1.0f, (1.0f - uv.y * resScale.y) * 2.0f - 1.0f, d, 1.0f);
    const float4 w = float4(dot(clipToWorld[0], c), dot(clipToWorld[1], c), dot(clipToWorld[2], c), dot(clipToWorld[3], c));
    return w.xyz / w.w;
}

// The eye in the game's camera-relative space. Float4 40 of the per-frame constants, which that space is relative to,
// is the eye in first person but follows the player in third person, so the eye is read from the projection: the
// point clip space sends to infinity, clipToWorld x (0, 0, 1, 0). The third-person primary-ray view traced from the
// origin drew the player about 15 % larger than the game (rt7-hair-2). Past 2,000 units the matrix is taken as not
// projective and the origin stands in.
float3 eyeAt() {
    const float4 e = float4(clipToWorld[0].z, clipToWorld[1].z, clipToWorld[2].z, clipToWorld[3].z);
    const float3 p = abs(e.w) > 1.0e-9f ? e.xyz / e.w : 0.0f.xxx;
    return dot(p, p) < 2000.0f * 2000.0f ? p : 0.0f.xxx;
}

// A camera-relative world point to (pixel x, pixel y, depth-buffer value, view z).
float4 screenOf(float3 p) {
    const float4 q = float4(p, 1.0f);
    const float4 c = float4(dot(worldToClip[0], q), dot(worldToClip[1], q), dot(worldToClip[2], q), dot(worldToClip[3], q));
    const float2 ndc = c.xy / c.w;
    return float4((ndc.x * 0.5f + 0.5f) / resScale.x * dims.x, (0.5f - ndc.y * 0.5f) / resScale.y * dims.y, c.z / c.w, c.w);
}

// The game's normal target: n.xy / sqrt(8 - 8 n.z) + 0.5 in its view space (z into the screen),
// taken to world space with the inverse view rotation.
float3 worldNormal(float4 t) {
    const float2 e = t.xy - 0.5f;
    const float s = min(dot(e, e), 0.25f);
    const float3 nv = float3(e * (4.0f * sqrt(1.0f - 4.0f * s)), 8.0f * s - 1.0f);
    const float3 n = float3(dot(viewToWorld[0].xyz, nv), dot(viewToWorld[1].xyz, nv), dot(viewToWorld[2].xyz, nv));
    return n / max(length(n), 1.0e-6f);
}

// Interleaved gradient noise (Jimenez 2014), turned each frame.
float ign(float2 px) {
    const float2 p = px + 5.588238f * float(uint(proj.z) & 63u);
    return frac(52.9829189f * frac(dot(p, float2(0.06711056f, 0.00583715f))));
}

#ifndef ATLAS_SLOT
#define ATLAS_SLOT t5
#endif
Texture2D<float> Atlas : register(ATLAS_SLOT);

// Analytic Perez/Preetham sky model ported from superframe/native/src/renderer/sky.cpp.
float perezEval(float A, float B, float C, float D, float E, float cosTheta, float gamma) {
    const float ct = max(cosTheta, 0.02f), cg = cos(gamma);
    return (1.0f + A * exp(B / ct)) * (1.0f + C * exp(D * gamma) + E * cg * cg);
}

float3 preethamSky(float3 dir, float3 sun, float turbidity) {
    const float3 d = normalize(dir);
    const float3 s = normalize(sun);
    const float T = clamp(turbidity, 2.0f, 8.0f);
    const float cosT = max(d.z, 0.02f);
    const float ts = acos(clamp(s.z, 0.02f, 1.0f));
    const float g = acos(clamp(dot(d, s), -1.0f, 1.0f));
    const float rY = perezEval(0.1787f * T - 1.4630f, -0.3554f * T + 0.4275f, -0.0227f * T + 5.3251f, 0.1206f * T - 2.5771f, -0.0670f * T + 0.3703f, cosT, g)
                   / max(perezEval(0.1787f * T - 1.4630f, -0.3554f * T + 0.4275f, -0.0227f * T + 5.3251f, 0.1206f * T - 2.5771f, -0.0670f * T + 0.3703f, 1.0f, ts), 1.0e-3f);
    const float rx = perezEval(-0.0193f * T - 0.2592f, -0.0665f * T + 0.0008f, -0.0004f * T + 0.2125f, -0.0641f * T - 0.8989f, -0.0033f * T + 0.0452f, cosT, g)
                   / max(perezEval(-0.0193f * T - 0.2592f, -0.0665f * T + 0.0008f, -0.0004f * T + 0.2125f, -0.0641f * T - 0.8989f, -0.0033f * T + 0.0452f, 1.0f, ts), 1.0e-3f);
    const float ry = perezEval(-0.0167f * T - 0.2608f, -0.0950f * T + 0.0092f, -0.0079f * T + 0.2102f, -0.0441f * T - 1.6537f, -0.0109f * T + 0.0529f, cosT, g)
                   / max(perezEval(-0.0167f * T - 0.2608f, -0.0950f * T + 0.0092f, -0.0079f * T + 0.2102f, -0.0441f * T - 1.6537f, -0.0109f * T + 0.0529f, 1.0f, ts), 1.0e-3f);
    const float t2 = T * T, ts2 = ts * ts, ts3 = ts2 * ts;
    const float xz = (0.00166f * ts3 - 0.00375f * ts2 + 0.00209f * ts) * t2
                   + (-0.02903f * ts3 + 0.06377f * ts2 - 0.03202f * ts + 0.00394f) * T
                   + (0.11693f * ts3 - 0.21196f * ts2 + 0.06052f * ts + 0.25886f);
    const float yz = (0.00275f * ts3 - 0.00610f * ts2 + 0.00317f * ts) * t2
                   + (-0.04214f * ts3 + 0.08970f * ts2 - 0.04153f * ts + 0.00516f) * T
                   + (0.15346f * ts3 - 0.26756f * ts2 + 0.06670f * ts + 0.26688f);
    const float x = clamp(xz * rx, 0.15f, 0.65f), y = clamp(yz * ry, 0.15f, 0.65f);
    const float Y = clamp(rY * 0.48f, 0.02f, 2.5f);
    const float X = (x / y) * Y, Z = ((1.0f - x - y) / y) * Y;
    return max(float3(3.2406f * X - 1.5372f * Y - 0.4986f * Z,
                      -0.9689f * X + 1.8758f * Y + 0.0415f * Z,
                       0.0557f * X - 0.2040f * Y + 1.0570f * Z), 0.0f.xxx);
}

// Dynamic physically based environment cubemap (shared by Lighting, RtShadow, and RtWater).
// Combines Preetham/Perez sky physics with live ambient SH, solar disk, and ground bounce.
float3 evalDynamicEnvCube(float3 rd, float roughness) {
    const float3 d = normalize(rd);
    const float3 ambDir = max(float3(dot(ambient[0].xyz, d) + ambient[0].w,
                                     dot(ambient[1].xyz, d) + ambient[1].w,
                                     dot(ambient[2].xyz, d) + ambient[2].w), 0.02f.xxx);
    const bool ext = sunColour.w > 0.5f;
    if (!ext) {
        return ambDir * lerp(0.55f, 1.15f, saturate(d.z * 0.5f + 0.5f));
    }
    const float dayW = saturate(sunDir.z * 3.5f);
    const float turb = lerp(2.6f, 5.2f, saturate(clouds.x));
    const float3 physSky = preethamSky(d.z >= 0.0f ? d : float3(d.xy, 0.02f), sunDir.xyz, turb);
    const float3 skyCol = lerp(ambDir * 1.1f, physSky * max(dot(ambDir, float3(0.3f, 0.5f, 0.2f)) * 1.8f, 0.25f), dayW * 0.65f);
    const float3 groundCol = ambDir * float3(0.42f, 0.40f, 0.36f);
    float3 env = d.z >= 0.0f ? skyCol : lerp(skyCol * 0.55f, groundCol, saturate(-d.z * 2.5f));
    const float cosS = saturate(dot(d, sunDir.xyz));
    const float expS = lerp(180.0f, 8.0f, saturate(roughness));
    const float sunLobe = pow(cosS, expS) * lerp(2.2f, 0.18f, saturate(roughness));
    const float cloudAtten = 1.0f - saturate(clouds.x * 0.75f);
    env += sunColour.rgb * sunLobe * cloudAtten * saturate(sunDir.z * 5.0f + 0.25f) * step(0.0f, d.z);
    return min(env, 4.0f.xxx);
}

float cascadeSample(float3 p, uint k, bool filtered) {
    const float4 q = float4(p, 1.0f);
    const float3 sc = float3(dot(cascade[3 * k], q), dot(cascade[3 * k + 1], q), dot(cascade[3 * k + 2], q));
    if (any(sc.xy <= 0.0f) || any(sc.xy >= 1.0f)) { return 1.0f; }
    const float side = cascadeInfo.w;
    const float z = sc.z - (k == 0u ? cascadeInfo.x : cascadeInfo.y);
    const int2 t = int2(sc.xy * side);
    if (!filtered) { return z <= Atlas.Load(int3(t.x + int(k * side), t.y, 0)) ? 1.0f : 0.0f; }
    float lit = 0.0f;
    [unroll] for (int j = -1; j <= 1; j++) {
        [unroll] for (int i = -1; i <= 1; i++) {
            const int2 c = clamp(t + int2(i, j), int2(0, 0), int2(side - 1.0f, side - 1.0f));
            lit += z <= Atlas.Load(int3(c.x + int(k * side), c.y, 0)) ? 1.0f : 0.0f;
        }
    }
    return lit / 9.0f;
}

// Sun visibility from the game's cascades with smooth cascade-transition blending (eliminates hard split seams).
float cascadeShadow(float3 p, float d, bool filtered) {
    if ((flags.x & FLAG_SHADOWS) == 0u || sunColour.w < 0.5f) { return 1.0f; }
    if (flags.z <= 1u) { return cascadeSample(p, 0u, filtered); }
    const float split = cascadeInfo.z;
    const float band = split * 0.90f;
    if (d <= band) { return cascadeSample(p, 0u, filtered); }
    if (d >= split) { return cascadeSample(p, 1u, filtered); }
    const float w = smoothstep(band, split, d);
    return lerp(cascadeSample(p, 0u, filtered), cascadeSample(p, 1u, filtered), w);
}
