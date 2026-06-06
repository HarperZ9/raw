// Linearize depth buffer into view-space Z (R32_FLOAT).
// Reads from HiZ mip 0 (reversed-Z after CSCopy conversion), writes to linear depth.
// Input: reversed-Z (near=1, far=0). HiZ CSCopy converts Skyrim's standard depth.

Texture2D<float>    RawDepth    : register(t0);
RWTexture2D<float>  LinearDepth : register(u0);

cbuffer LinearizeParams : register(b0)
{
    uint2 Dims;
    float NearZ;
    float FarZ;
};

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= Dims.x || DTid.y >= Dims.y)
        return;

    float z = RawDepth[DTid.xy];

    // Skyrim reversed-Z: z=1 near, z=0 far
    // Sky pixels (z ≈ 0) → output FarZ (max distance)
    float viewZ = (z < 0.00001)
        ? FarZ
        : NearZ * FarZ / (NearZ + z * (FarZ - NearZ));

    LinearDepth[DTid.xy] = viewZ;
}
