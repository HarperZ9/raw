// AoComposite.hlsl: writes the game's frame, with the engine's GTAO visibility
// applied, into the shared output. One full-screen triangle through the
// engine's RHI, whose raster convention puts the uniform at b0, the storage
// buffer at u0 as a root UAV and the texture at t0. Original work, MIT.
//
// The frame here is the game's display-referred image (after its tone map,
// before the UI). Visibility multiplies the light, so the colour is taken to
// linear light with the sRGB transfer function, scaled, and encoded again.

cbuffer C : register(b0)
{
    uint  width;
    uint  height;
    uint  mode;       // 0 pass the frame through, 1 apply visibility, 2 show visibility
    uint  pad0;
    float strength;   // 0 leaves the frame unchanged, 1 applies the full visibility
    float pad1, pad2, pad3;
};
RWStructuredBuffer<float> A : register(u0);   // visibility, one float a pixel, top row first
Texture2D<float4> Frame : register(t0);

float4 vs(uint i : SV_VertexID) : SV_Position
{
    return float4(i == 1u ? 3.0f : -1.0f, i == 2u ? -3.0f : 1.0f, 0.0f, 1.0f);
}

float3 ToLinear(float3 c)
{
    return float3(c.x <= 0.04045f ? c.x / 12.92f : pow((c.x + 0.055f) / 1.055f, 2.4f),
                  c.y <= 0.04045f ? c.y / 12.92f : pow((c.y + 0.055f) / 1.055f, 2.4f),
                  c.z <= 0.04045f ? c.z / 12.92f : pow((c.z + 0.055f) / 1.055f, 2.4f));
}

float3 ToSrgb(float3 c)
{
    return float3(c.x <= 0.0031308f ? c.x * 12.92f : 1.055f * pow(c.x, 1.0f / 2.4f) - 0.055f,
                  c.y <= 0.0031308f ? c.y * 12.92f : 1.055f * pow(c.y, 1.0f / 2.4f) - 0.055f,
                  c.z <= 0.0031308f ? c.z * 12.92f : 1.055f * pow(c.z, 1.0f / 2.4f) - 0.055f);
}

float4 fs(float4 at : SV_Position) : SV_Target0
{
    const uint x = min((uint)at.x, width - 1u);
    const uint y = min((uint)at.y, height - 1u);
    const float4 c = Frame.Load(int3(x, y, 0));
    if (mode == 0u) { return c; }
    const float v = saturate(A[y * width + x]);
    if (mode == 2u) { return float4(v, v, v, 1.0f); }
    const float k = lerp(1.0f, v, saturate(strength));
    return float4(ToSrgb(ToLinear(saturate(c.rgb)) * k), c.a);
}
