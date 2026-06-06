struct PS_IN
{
    float4 pos   : SV_Position;
    float4 color : COLOR;
};

float4 main(PS_IN i) : SV_Target
{
    return i.color;
}
