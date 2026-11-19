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
                  FLAG_FOG_REPLACES = 512u, FLAG_RT = 1024u, FLAG_SKY = 2048u, FLAG_BOUNCE = 4096u, FLAG_REFLECT = 8192u, FLAG_BOUNCE_IN = 16384u, FLAG_CLOUDS = 32768u, FLAG_REST = 65536u, FLAG_LOD = 131072u;   // our fog takes the game's distance fog's place (off: the game's runs after ours)
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

// Sun visibility from the game's cascades: the cascade by the game's split depth, a 3 x 3 tap
// percentage-closer filter (taps a texel apart) on the R32F atlas. 1 outside the maps.
float cascadeShadow(float3 p, float d, bool filtered) {
    if ((flags.x & FLAG_SHADOWS) == 0u || sunColour.w < 0.5f) { return 1.0f; }
    const uint k = (flags.z > 1u && d > cascadeInfo.z) ? 1u : 0u;
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
