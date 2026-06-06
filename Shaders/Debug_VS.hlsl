cbuffer CB : register(b0)
{
    float4x4 ViewProj;
};

struct VS_IN
{
    float3 pos   : POSITION;
    uint   color : COLOR;
};

struct VS_OUT
{
    float4 pos   : SV_Position;
    float4 color : COLOR;
};

VS_OUT main(VS_IN i)
{
    VS_OUT o;
    o.pos = mul(float4(i.pos, 1.0), ViewProj);
    o.color = float4(
        ((i.color >>  0) & 0xFF) / 255.0,
        ((i.color >>  8) & 0xFF) / 255.0,
        ((i.color >> 16) & 0xFF) / 255.0,
        ((i.color >> 24) & 0xFF) / 255.0);
    return o;
}
