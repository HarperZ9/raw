// GenerateMips on D3D12: one level drawn from the level above.
// A full-screen triangle from the vertex id; the linear sampler at the centre
// of four source texels gives a box filter for a 2:1 reduction.
Texture2DArray<float4> Source : register(t0);
SamplerState Linear : register(s0);

struct VSOut {
    float4 pos : SV_Position;
    float2 uv : TEXCOORD0;
};

VSOut VSMain(uint id : SV_VertexID)
{
    VSOut o;
    o.uv = float2((id << 1) & 2, id & 2);
    o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}

float4 PSMain(VSOut i) : SV_Target
{
    return Source.SampleLevel(Linear, float3(i.uv, 0.0), 0.0);
}
