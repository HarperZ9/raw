#pragma once
// Test shaders with the structure the patch rules look for. They are written
// for this test; none is a shader of the game. Original work, MIT.

namespace bench
{

inline const char* kCommon = R"(
struct In { float4 pos : SV_Position; float2 uv : TEXCOORD0; float3 world : TEXCOORD1; };
Texture2D tColour : register(t0);
Texture2D tNormal : register(t1);
SamplerState s0 : register(s0);
SamplerState s1 : register(s1);
)";

// A lit surface: base colour from slot 0 with a tint, normal map with a mask in
// alpha from slot 1, lights in buffer 2 (count in element 29), shininess and
// specular colour in element 4 of buffer 1, emissive colour and scale in element 8.
inline const char* kLit = R"(
cbuffer Material : register(b1) { float4 mat[9]; }
cbuffer Lights : register(b2) { float4 geo[30]; }
struct Out { float4 c : SV_Target0; float4 m : SV_Target1; float4 n : SV_Target2; };
Out main(In i) {
    float4 base = tColour.Sample(s0, i.uv);
    float4 nm = tNormal.Sample(s1, i.uv);
    base.rgb = base.rgb * mat[0].rgb;
    float3 n = normalize(nm.xyz * 2 - 1);
    float count = min(geo[29].x, 7);
    float3 light = saturate(dot(n, geo[0].xyz)) * geo[1].rgb * count;
    float spec = pow(saturate(dot(n, normalize(geo[0].xyz + float3(0.1, 0.2, 1)))), mat[4].w);
    light += mat[8].yzw * mat[8].x;
    Out o;
    o.c.rgb = base.rgb * light + spec * nm.a * mat[4].xyz;
    o.c.a = base.a * geo[3].z;
    o.m = float4(0.25, 0.5, 0, 1);
    float z = max(sqrt(8 - 8 * n.z), 0.001);
    o.n = float4(n.xy / z + 0.5, 0, nm.a);
    return o;
})";

// A water surface: a world-space normal tested against the fixed direction.
inline const char* kWater = R"(
cbuffer Material : register(b1) { float4 w[8]; }
cbuffer Frame : register(b12) { float4 frame[43]; }
float4 main(In i) : SV_Target0 {
    float3 n = tNormal.Sample(s1, i.uv).xyz * 2 - 1;
    n = normalize(n * float3(w[6].x, w[6].x, 1));
    float f = pow(saturate(dot(n, float3(-0.099, -0.099, 0.99))), w[0].w);
    return float4(f * w[1].rgb + frame[42].w, 1);
})";

// A second water pass: the same wave normal, lit by a point light, with no fixed direction
// and no buffer 12 declared.
inline const char* kWaterLight = R"(
cbuffer Material : register(b1) { float4 w[8]; }
cbuffer Lights : register(b2) { float4 geo[30]; }
float4 main(In i) : SV_Target0 {
    float3 n = tNormal.Sample(s1, i.uv).xyz * 2 - float3(1, 1, 2);
    n = w[6].x * n + float3(0, 0, 1);
    n = normalize(n);
    float3 l = normalize(geo[6].xyz - i.world);
    return float4(pow(saturate(dot(l, n)), w[3].z) * geo[14].xyz, 1);
})";

// An effect draw: small material and geometry buffers, alpha from the texture.
inline const char* kEffect = R"(
cbuffer A : register(b1) { float4 a[3]; }
cbuffer B : register(b2) { float4 b[9]; }
struct Out { float4 c : SV_Target0; float4 m : SV_Target1; float4 n : SV_Target2; };
Out main(In i) {
    float4 t = tColour.Sample(s0, i.uv) * a[2];
    Out o;
    o.c = t * b[8];
    o.m = float4(1, 0, 0, t.a);
    o.n = o.c;
    return o;
})";

} // namespace bench
