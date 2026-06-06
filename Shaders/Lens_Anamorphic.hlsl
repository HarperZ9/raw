// Copyright (c) 2026 Zain D. Harper. All rights reserved.
//
// Pass 4: Anamorphic Horizontal Streak (1D Gaussian Blur)
//
// Anamorphic lenses use cylindrical elements that create a characteristic
// horizontal (or angled) streak from bright sources. This is modeled as a
// 1D Gaussian blur along the streak direction of the bright-extracted pixels.
//
// The primary streak runs along StreakAngle (typically 0 = horizontal).
// An optional secondary streak at 90 degrees adds cross-shaped flares at
// reduced intensity, simulating multi-element anamorphic lens designs.
//
// Reference: Standard 1D Gaussian separable filter; anamorphic lens behavior
//            documented in cinematography literature (Vantage, Panavision).

cbuffer AnamorphicCB : register(b0)
{
    uint2  OutputDims;    // Half-res output
    uint2  SrcDims;       // Source (half-res bright) dimensions
    float  FlareIntensity;
    float  FalloffRate;   // Exponential falloff rate (higher = shorter streaks)
    float  StreakAngle;   // Primary streak angle in radians (0 = horizontal)
    float  SecondaryIntensity; // Secondary 90-degree streak strength
    float  DispersionScale;    // Spectral dispersion amount (R wider than B)
    float3 pad0;
}

static const int kSamples = 32;

Texture2D<float4> SrcTex  : register(t0); // Half-res bright extract
SamplerState      LinSamp : register(s0);
RWTexture2D<float4> FlareOutput : register(u0);

[numthreads(8, 8, 1)]
void main(uint3 DTid : SV_DispatchThreadID)
{
    if (DTid.x >= OutputDims.x || DTid.y >= OutputDims.y)
        return;

    float2 uv = (float2(DTid.xy) + 0.5) / float2(OutputDims);
    float2 texelSize = 1.0 / float2(SrcDims);

    // Primary streak direction (default horizontal)
    float cosA = cos(StreakAngle);
    float sinA = sin(StreakAngle);
    float2 primaryDir = float2(cosA, sinA);

    // Secondary streak direction (perpendicular)
    float2 secondaryDir = float2(-sinA, cosA);

    // ── 1D Gaussian blur along primary direction ─────────────────────
    // Sigma is derived from the falloff rate. Lower falloff = wider streaks.
    // The Gaussian weight is: exp(-0.5 * (offset / sigma)^2)
    // We use the exponential falloff directly for a more physically motivated
    // streak shape that matches anamorphic lens measurements.
    float3 primaryAccum = 0.0;
    float primaryWeight = 0.0;

    for (int i = -kSamples; i <= kSamples; i++)
    {
        float offset = float(i);
        float2 sampleUV = uv + primaryDir * offset * texelSize * 2.0;

        // Clamp to valid range
        sampleUV = clamp(sampleUV, 0.0, 1.0);

        // Exponential falloff: intensity drops with distance from center
        float w = exp(-abs(offset) * FalloffRate);

        float3 src = SrcTex.SampleLevel(LinSamp, sampleUV, 0).rgb;
        primaryAccum += src * w;
        primaryWeight += w;
    }
    primaryAccum /= max(primaryWeight, 1e-5);

    // ── 1D Gaussian blur along secondary direction ───────────────────
    float3 secondaryAccum = 0.0;
    float secondaryWeight = 0.0;

    if (SecondaryIntensity > 0.001)
    {
        for (int j = -kSamples; j <= kSamples; j++)
        {
            float offset = float(j);
            float2 sampleUV = uv + secondaryDir * offset * texelSize * 2.0;
            sampleUV = clamp(sampleUV, 0.0, 1.0);

            // Secondary streaks use a tighter falloff (shorter)
            float w = exp(-abs(offset) * FalloffRate * 2.0);

            float3 src = SrcTex.SampleLevel(LinSamp, sampleUV, 0).rgb;
            secondaryAccum += src * w;
            secondaryWeight += w;
        }
        secondaryAccum /= max(secondaryWeight, 1e-5);
    }

    // Combine primary and secondary streaks
    float3 streak = primaryAccum + secondaryAccum * SecondaryIntensity;

    FlareOutput[DTid.xy] = float4(streak * FlareIntensity, 1.0);
}
