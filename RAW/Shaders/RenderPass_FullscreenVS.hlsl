struct VSOutput
{
    float4 position : SV_Position;
    float2 texcoord : TEXCOORD0;
};

VSOutput main(uint vertexID : SV_VertexID)
{
    VSOutput o;
    // Generate fullscreen triangle from vertex ID
    o.texcoord = float2((vertexID << 1) & 2, vertexID & 2);
    o.position = float4(o.texcoord * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return o;
}
