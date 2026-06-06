cbuffer MeteringCB : register(b0)
{
    uint2 ScreenDims;
    uint  MeteringMode;   // 0=Evaluative, 1=CenterWeighted, 2=Spot
    float Pad;
};

Texture2D<float4> BackBuffer : register(t0);

// [0].xy = (sumFixedLogLum, sumFixedWeight)  — accumulated via InterlockedAdd
// [1].xy = (minFixedLogLum, maxFixedLogLum)  — accumulated via InterlockedMin/Max
RWStructuredBuffer<uint> MeteringResult : register(u0);  // 4 uints

groupshared float gs_logLum[256];     // weighted log-luminance per thread
groupshared float gs_weight[256];     // weight per thread

// Fixed-point scale: 16.16 gives ~1e-5 precision over [-10, +10] range
static const float FP_SCALE = 65536.0;

[numthreads(16, 16, 1)]
void CSMetering(uint3 DTid : SV_DispatchThreadID, uint GI : SV_GroupIndex)
{
    gs_logLum[GI] = 0.0;
    gs_weight[GI] = 0.0;
    GroupMemoryBarrierWithGroupSync();

    if (DTid.x < ScreenDims.x && DTid.y < ScreenDims.y)
    {
        float2 uv = (float2(DTid.xy) + 0.5) / float2(ScreenDims);
        float2 center = float2(0.5, 0.5);
        float dist = length(uv - center);

        // Compute spatial weight based on metering mode
        float weight = 1.0;
        if (MeteringMode == 1)  // Center-weighted: Gaussian falloff
        {
            weight = exp(-dist * dist / (2.0 * 0.3 * 0.3));
        }
        else if (MeteringMode == 2)  // Spot: center 5% circle
        {
            weight = (dist < 0.05) ? 1.0 : 0.0;
        }
        else  // Evaluative: 5-zone matrix metering
        {
            // Center zone (r < 0.2): 40% weight -> boost 2.5x
            // Mid zone   (r < 0.4): base weight 1.0x
            // Edge zone  (r >= 0.4): 15% per quadrant -> 0.5x
            if (dist < 0.2)
                weight = 2.5;
            else if (dist < 0.4)
                weight = 1.0;
            else
                weight = 0.5;
        }

        float3 color = BackBuffer[DTid.xy].rgb;
        color = clamp(color, 0.0, 64.0);

        // Rec.709 luminance, clamped to avoid log2(0)
        float lum = dot(color, float3(0.2126, 0.7152, 0.0722));
        lum = max(lum, 0.001);

        gs_logLum[GI] = log2(lum) * weight;
        gs_weight[GI] = weight;
    }

    GroupMemoryBarrierWithGroupSync();

    // Parallel reduction (256 threads -> 1)
    for (uint s = 128; s > 0; s >>= 1)
    {
        if (GI < s)
        {
            gs_logLum[GI] += gs_logLum[GI + s];
            gs_weight[GI] += gs_weight[GI + s];
        }
        GroupMemoryBarrierWithGroupSync();
    }

    // Thread 0: atomically accumulate this workgroup's result into global buffer
    if (GI == 0)
    {
        // Encode as signed fixed-point offset by a large bias to keep positive
        // log2(lum) range: roughly [-10, +6] => biased to [0, 16] * 65536
        int fixedLogLum = (int)(gs_logLum[0] * FP_SCALE);
        int fixedWeight = (int)(gs_weight[0] * FP_SCALE);

        // Use InterlockedAdd on uint (reinterpreted from signed int — works
        // because two's complement addition is bit-identical for signed/unsigned)
        InterlockedAdd(MeteringResult[0], (uint)fixedLogLum);
        InterlockedAdd(MeteringResult[1], (uint)fixedWeight);
    }
}
