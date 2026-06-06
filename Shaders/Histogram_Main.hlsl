Texture2D<float4> BackBuffer : register(t0);

RWStructuredBuffer<uint> Histogram : register(u0);     // 256 bins
RWStructuredBuffer<float4> Stats   : register(u1);     // [0]=sum(rgb,lum)

groupshared uint gs_hist[256];

[numthreads(16, 16, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    // Clear shared memory
    if (GI < 256)
        gs_hist[GI] = 0;
    GroupMemoryBarrierWithGroupSync();

    uint2 dims;
    BackBuffer.GetDimensions(dims.x, dims.y);

    if (DTid.x < dims.x && DTid.y < dims.y)
    {
        float3 color = BackBuffer[DTid.xy].rgb;
        color = clamp(color, 0.0, 64.0);

        // Rec.709 luminance
        float lum = dot(color, float3(0.2126, 0.7152, 0.0722));

        // Log-space binning: maps [~0.001, ~1024] to [0, 255]
        float logLum = log2(lum + 0.001);
        uint bin = (uint)clamp(floor((logLum + 10.0) / 20.0 * 256.0), 0.0, 255.0);

        InterlockedAdd(gs_hist[bin], 1);
    }

    GroupMemoryBarrierWithGroupSync();

    // Merge shared memory to global
    if (GI < 256)
        InterlockedAdd(Histogram[GI], gs_hist[GI]);
}
