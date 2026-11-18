// DepthFog.hlsl: the first pass RAW draws on its own D3D12 device.
// Original work, MIT. Shader model 5.1 so fxc can build it for D3D12.
//
// Reads the game's colour and depth (shared from D3D11), writes colour with a
// depth-driven haze. Mode 0 copies colour unchanged (the round-trip test uses
// it for an exact check), mode 1 is the haze, mode 2 shows linear depth.

cbuffer PassConstants : register(b0)
{
    float4 gFogColor;   // rgb haze colour, a = maximum opacity
    float4 gParams;     // x density per game unit, y near plane, z far plane, w mode
    float4 gParams2;    // x marker on, y frame index, zw unused
    float4 gDims;       // width, height, 1/width, 1/height
};

Texture2D<float4> gColor : register(t0);
Texture2D<float>  gDepth : register(t1);

struct VSOut { float4 pos : SV_Position; };

VSOut VSMain(uint id : SV_VertexID)
{
    // One triangle that covers the screen.
    float2 uv = float2((id << 1) & 2, id & 2);
    VSOut o;
    o.pos = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float LinearDepth(float d, float n, float f)
{
    // Standard (not reversed) perspective depth in [0,1] to view distance.
    return (n * f) / (f - d * (f - n));
}

float3 MarkerColour(uint i)
{
    // Four fixed swatches, rotated by frame index, so a capture shows the
    // D3D12 pass ran and that it advances frame to frame.
    const float3 sw[4] = { float3(1, 0.1, 0.6), float3(0.1, 0.9, 1),
                           float3(1, 0.85, 0.1), float3(0.5, 0.2, 1) };
    return sw[i & 3];
}

float4 PSMain(VSOut i) : SV_Target
{
    int2 p = int2(i.pos.xy);
    float4 c = gColor.Load(int3(p, 0));
    uint mode = (uint)gParams.w;

    if (gParams2.x > 0.5 && p.x < 96 && p.y < 24)
        return float4(MarkerColour((uint)p.x / 24u + (uint)gParams2.y), c.a);

    if (mode == 0)
        return c;

    float d = gDepth.Load(int3(p, 0));
    float z = LinearDepth(d, gParams.y, gParams.z);
    if (mode == 2)
        return float4(saturate(z / gParams.z).xxx, c.a);

    float fog = (1.0 - exp(-gParams.x * z)) * gFogColor.a;
    return float4(lerp(c.rgb, gFogColor.rgb, fog), c.a);
}
