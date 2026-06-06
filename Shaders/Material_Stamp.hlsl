cbuffer StampCB : register(b7)
{
    uint MaterialID;
    uint pad0, pad1, pad2;
};

float4 main() : SV_Target
{
    return float4(float(MaterialID) / 255.0, 0, 0, 1);
}
