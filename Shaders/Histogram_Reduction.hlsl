RWStructuredBuffer<uint> Histogram : register(u0);
RWStructuredBuffer<float4> Stats   : register(u1);

[numthreads(1, 1, 1)]
void CSReduction(uint3 DTid : SV_DispatchThreadID)
{
    uint totalPixels = 0;
    float sumLogLum = 0.0;
    uint minBin = 255, maxBin = 0;

    for (uint i = 0; i < 256; i++)
    {
        uint count = Histogram[i];
        totalPixels += count;
        if (count > 0 && i < minBin) minBin = i;
        if (count > 0 && i > maxBin) maxBin = i;

        float binCenter = (float(i) + 0.5) / 256.0 * 20.0 - 10.0;
        sumLogLum += binCenter * float(count);
    }

    float invTotal = (totalPixels > 0) ? 1.0 / float(totalPixels) : 0.0;

    // Log-average luminance
    float avgLogLum = sumLogLum * invTotal;
    float avgLum = exp2(avgLogLum);

    // Min/max luminance from bin edges
    float minLum = exp2(float(minBin) / 256.0 * 20.0 - 10.0);
    float maxLum = exp2(float(maxBin + 1) / 256.0 * 20.0 - 10.0);

    // Percentiles (prefix sum)
    uint cumulative = 0;
    float p05 = minLum, p50 = avgLum, p95 = maxLum;
    uint threshold05 = uint(float(totalPixels) * 0.05);
    uint threshold50 = uint(float(totalPixels) * 0.50);
    uint threshold95 = uint(float(totalPixels) * 0.95);
    bool found05 = false, found50 = false, found95 = false;

    for (uint j = 0; j < 256; j++)
    {
        cumulative += Histogram[j];
        float binLum = exp2((float(j) + 0.5) / 256.0 * 20.0 - 10.0);
        if (!found05 && cumulative >= threshold05) { p05 = binLum; found05 = true; }
        if (!found50 && cumulative >= threshold50) { p50 = binLum; found50 = true; }
        if (!found95 && cumulative >= threshold95) { p95 = binLum; found95 = true; }
    }

    Stats[0] = float4(avgLum, minLum, maxLum, float(totalPixels));
    Stats[1] = float4(p05, p50, p95, 0.0);
}
