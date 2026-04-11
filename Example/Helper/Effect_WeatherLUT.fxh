#ifndef EFFECT_WEATHERLUT_FXH
#define EFFECT_WEATHERLUT_FXH
//=============================================================================
//  Effect_WeatherLUT.fxh — Per-Weather-Per-Time Color Grading LUT System
//
//  Uses SkyrimBridge weather data (SB_Weather_Flags, SB_Weather_Transition,
//  SB_Time) to auto-select and blend between weather-appropriate LUTs.
//
//  Architecture:
//    7 weather categories x 3 time slots (day/dawn-dusk/night) = 21 LUTs
//    Auto-blends between outgoing/incoming weather LUTs during transitions
//
//  LUT format: Standard 1024x32 strip (32 slices of 32x32 tiles)
//
//  Usage:
//    float3 result = SB_ApplyWeatherLUT(color, currentLUT, prevLUT,
//                                       smp, intensity);
//
//  Author: Zain Dana Harper
//  Reference: NVE (Razed) weather-reactive grading system
//=============================================================================


//─────────────────────────────────────────────────────────────────────────────
//  Weather Category Detection
//─────────────────────────────────────────────────────────────────────────────

// Weather categories:
//   0 = Clear, 1 = Cloudy, 2 = Foggy, 3 = Rain, 4 = Thunder, 5 = Snow, 6 = Ash
// Returns integer category from SkyrimBridge flags
int SB_GetWeatherCategory()
{
    if (SB_Lightning.x > 0.1)     return 4;  // Thunder (check before rain)
    if (SB_Weather_Flags.z > 0.5) return 3;  // Rain
    if (SB_Weather_Flags.w > 0.5) return 5;  // Snow
    if (SB_Weather_Flags.y > 0.5) return 1;  // Cloudy
    if (SB_Fog_Density.x > 0.5)   return 2;  // Foggy
    if (SB_Precipitation.x > 2.5) return 6;  // Ash (type 3+)
    return 0;                                 // Clear
}


//─────────────────────────────────────────────────────────────────────────────
//  Time Slot Detection
//─────────────────────────────────────────────────────────────────────────────

// Time slots: 0 = Day, 1 = Dawn/Dusk, 2 = Night
int SB_GetTimeSlot()
{
    float h = SB_Time.x;
    float sr = SB_Time.y;
    float ss = SB_Time.z;
    if (h >= sr + 2.0 && h <= ss - 2.0) return 0;  // Day
    if (h < sr - 1.0 || h > ss + 1.0)   return 2;  // Night
    return 1;                                        // Dawn/Dusk
}

// Smooth time-slot blend factor [0,1] for cross-fading between time LUTs
// Returns weight toward the "next" time slot during transition periods
float SB_GetTimeSlotBlend()
{
    float h = SB_Time.x;
    float sr = SB_Time.y;
    float ss = SB_Time.z;

    // Dawn transition: Night→Dawn/Dusk (sr-1 to sr+2)
    if (h >= sr - 1.0 && h < sr + 2.0)
        return saturate((h - (sr - 1.0)) / 3.0);

    // Dusk transition: Day→Dawn/Dusk→Night (ss-2 to ss+1)
    if (h >= ss - 2.0 && h < ss + 1.0)
        return saturate((h - (ss - 2.0)) / 3.0);

    return 0.0;  // Fully in current slot
}


//─────────────────────────────────────────────────────────────────────────────
//  LUT Sampling
//─────────────────────────────────────────────────────────────────────────────

// Trilinear LUT sampling (legacy — can cause diagonal hue shifts)
float3 SB_SampleLUT_Trilinear(Texture2D lut, SamplerState smp, float3 color)
{
    float3 c = saturate(color);
    float slice = c.b * 31.0;
    float sliceLow = floor(slice);
    float sliceHigh = ceil(slice);
    float sliceFrac = slice - sliceLow;

    float2 uvLow  = float2((sliceLow  * 32.0 + c.r * 31.0 + 0.5) / 1024.0,
                            (c.g * 31.0 + 0.5) / 32.0);
    float2 uvHigh = float2((sliceHigh * 32.0 + c.r * 31.0 + 0.5) / 1024.0,
                            (c.g * 31.0 + 0.5) / 32.0);

    float3 low  = lut.SampleLevel(smp, uvLow,  0).rgb;
    float3 high = lut.SampleLevel(smp, uvHigh, 0).rgb;
    return lerp(low, high, sliceFrac);
}

// Tetrahedral LUT interpolation — eliminates diagonal hue shifts from trilinear.
// Decomposes each cube cell into 6 tetrahedra based on component ordering.
// 4 point samples + barycentric blend (same cost as trilinear, better accuracy).
float3 SB_SampleLUT_Tetrahedral(Texture2D lut, SamplerState smp, float3 color)
{
    float3 c = saturate(color) * 31.0;
    float3 c0 = floor(c);
    float3 c1 = min(c0 + 1.0, 31.0);
    float3 f  = c - c0;

    #define LUT_UV(r, g, b) float2((b * 32.0 + r + 0.5) / 1024.0, (g + 0.5) / 32.0)

    float3 p0 = lut.SampleLevel(smp, LUT_UV(c0.r, c0.g, c0.b), 0).rgb;
    float3 p3 = lut.SampleLevel(smp, LUT_UV(c1.r, c1.g, c1.b), 0).rgb;
    float3 p1, p2;
    float  w0, w1, w2, w3;

    if (f.r > f.g)
    {
        if (f.g > f.b)       // r > g > b
        {
            w0 = 1.0 - f.r; w1 = f.r - f.g; w2 = f.g - f.b; w3 = f.b;
            p1 = lut.SampleLevel(smp, LUT_UV(c1.r, c0.g, c0.b), 0).rgb;
            p2 = lut.SampleLevel(smp, LUT_UV(c1.r, c1.g, c0.b), 0).rgb;
        }
        else if (f.r > f.b)  // r > b > g
        {
            w0 = 1.0 - f.r; w1 = f.r - f.b; w2 = f.b - f.g; w3 = f.g;
            p1 = lut.SampleLevel(smp, LUT_UV(c1.r, c0.g, c0.b), 0).rgb;
            p2 = lut.SampleLevel(smp, LUT_UV(c1.r, c0.g, c1.b), 0).rgb;
        }
        else                  // b > r > g
        {
            w0 = 1.0 - f.b; w1 = f.b - f.r; w2 = f.r - f.g; w3 = f.g;
            p1 = lut.SampleLevel(smp, LUT_UV(c0.r, c0.g, c1.b), 0).rgb;
            p2 = lut.SampleLevel(smp, LUT_UV(c1.r, c0.g, c1.b), 0).rgb;
        }
    }
    else
    {
        if (f.b > f.g)       // b > g > r
        {
            w0 = 1.0 - f.b; w1 = f.b - f.g; w2 = f.g - f.r; w3 = f.r;
            p1 = lut.SampleLevel(smp, LUT_UV(c0.r, c0.g, c1.b), 0).rgb;
            p2 = lut.SampleLevel(smp, LUT_UV(c0.r, c1.g, c1.b), 0).rgb;
        }
        else if (f.b > f.r)  // g > b > r
        {
            w0 = 1.0 - f.g; w1 = f.g - f.b; w2 = f.b - f.r; w3 = f.r;
            p1 = lut.SampleLevel(smp, LUT_UV(c0.r, c1.g, c0.b), 0).rgb;
            p2 = lut.SampleLevel(smp, LUT_UV(c0.r, c1.g, c1.b), 0).rgb;
        }
        else                  // g > r > b
        {
            w0 = 1.0 - f.g; w1 = f.g - f.r; w2 = f.r - f.b; w3 = f.b;
            p1 = lut.SampleLevel(smp, LUT_UV(c0.r, c1.g, c0.b), 0).rgb;
            p2 = lut.SampleLevel(smp, LUT_UV(c1.r, c1.g, c0.b), 0).rgb;
        }
    }

    #undef LUT_UV
    return p0 * w0 + p1 * w1 + p2 * w2 + p3 * w3;
}

// Default LUT sampling — uses tetrahedral for hue-accurate interpolation
float3 SB_SampleLUT(Texture2D lut, SamplerState smp, float3 color)
{
    return SB_SampleLUT_Tetrahedral(lut, smp, color);
}


//─────────────────────────────────────────────────────────────────────────────
//  Weather LUT Application with Transition Blending
//─────────────────────────────────────────────────────────────────────────────

// Apply weather+time LUT with transition blending
// currentLUT = LUT for the incoming/current weather+time combination
// prevLUT    = LUT for the outgoing/previous weather+time combination
// smp        = linear sampler
// intensity  = overall LUT strength [0,1]
float3 SB_ApplyWeatherLUT(float3 color, Texture2D currentLUT, Texture2D prevLUT,
                          SamplerState smp, float intensity)
{
    float transition = SB_Weather_Transition.x;
    float3 currentGraded = SB_SampleLUT(currentLUT, smp, color);
    float3 prevGraded    = SB_SampleLUT(prevLUT, smp, color);
    float3 blended = lerp(prevGraded, currentGraded, transition);
    return lerp(color, blended, intensity);
}

// Simplified version: single LUT, no transition blending
// Use when only applying a single weather LUT without transition awareness
float3 SB_ApplyLUT(float3 color, Texture2D lut, SamplerState smp, float intensity)
{
    float3 graded = SB_SampleLUT(lut, smp, color);
    return lerp(color, graded, intensity);
}


#endif // EFFECT_WEATHERLUT_FXH
