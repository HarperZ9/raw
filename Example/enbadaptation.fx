//----------------------------------------------------------------------------------------------//
//                         ENB of the Elders - Eye Adaptation                                    //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Histogram-based auto-exposure with 64-bin percentile anchoring,                              //
//  center-weighted metering, scene-cut detection, and asymmetric speed.                         //
//                                                                                              //
//  Algorithm: kingeric1992 (ENB Forum, Nov 2016)                                                //
//  Based on Unreal Engine auto-exposure description                                             //
//  Adapted for ENB of the Elders by Zain Dana Harper - March 2026                              //
//                                                                                              //
//  Pass 0 (Downsample): 256x256 scene → 16x16 log2 luminance tiles                            //
//  Pass 1 (Histogram):  16x16 tiles → 64-bin histogram → 1x1 adaptation value                 //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=== ENB EXTERNAL VARIABLES ===//

float4  Timer;
float4  ScreenSize;
float   AdaptiveQuality;
float4  Weather;
float4  TimeOfDay1;
float4  TimeOfDay2;
float   ENightDayFactor;
float   EInteriorFactor;
float4  tempF1;
float4  tempF2;
float4  tempF3;
float4  tempInfo1;
float4  tempInfo2;


//=== GAME PARAMETERS ===//

// x = AdaptationMin, y = AdaptationMax, z = AdaptationSensitivity,
// w = AdaptationTime * elapsed (temporal blend factor)
float4  AdaptationParameters;


//=== TEXTURES ===//

Texture2D   TextureCurrent;     // Pass 0: 256x256 scene (HDR). Pass 1: 16x16 log2 tiles (R32F)
Texture2D   TexturePrevious;    // 1x1 previous frame adaptation (R32F)


//=== SAMPLERS ===//

SamplerState smpPoint
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState smpLinear
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};


//=== GLOBALS ===//

#include "enbglobals.fxh"


//=== UI PARAMETERS ===//

// --- Exposure Bias (DNI) ---

float ui_AdaptBiasDay
<
    string UIName = "ADAPT | Day - Exposure Bias (log2)";
    string UIWidget = "Spinner";
    float UIMin = -5.0;
    float UIMax = 5.0;
> = {0.0};

float ui_AdaptBiasNight
<
    string UIName = "ADAPT | Night - Exposure Bias (log2)";
    string UIWidget = "Spinner";
    float UIMin = -5.0;
    float UIMax = 5.0;
> = {0.0};

float ui_AdaptBiasInterior
<
    string UIName = "ADAPT | Interior - Exposure Bias (log2)";
    string UIWidget = "Spinner";
    float UIMin = -5.0;
    float UIMax = 5.0;
> = {0.0};

// --- Brightness Clamps (DNI) ---

float ui_AdaptMaxDay
<
    string UIName = "ADAPT | Day - Max Brightness (log2)";
    string UIWidget = "Spinner";
    float UIMin = -5.0;
    float UIMax = 5.0;
> = {0.80};

float ui_AdaptMaxNight
<
    string UIName = "ADAPT | Night - Max Brightness (log2)";
    string UIWidget = "Spinner";
    float UIMin = -5.0;
    float UIMax = 5.0;
> = {0.80};

float ui_AdaptMaxInterior
<
    string UIName = "ADAPT | Interior - Max Brightness (log2)";
    string UIWidget = "Spinner";
    float UIMin = -5.0;
    float UIMax = 5.0;
> = {0.80};

float ui_AdaptMinDay
<
    string UIName = "ADAPT | Day - Min Brightness (log2)";
    string UIWidget = "Spinner";
    float UIMin = -5.0;
    float UIMax = 5.0;
> = {-2.0};

float ui_AdaptMinNight
<
    string UIName = "ADAPT | Night - Min Brightness (log2)";
    string UIWidget = "Spinner";
    float UIMin = -5.0;
    float UIMax = 5.0;
> = {-2.0};

float ui_AdaptMinInterior
<
    string UIName = "ADAPT | Interior - Min Brightness (log2)";
    string UIWidget = "Spinner";
    float UIMin = -5.0;
    float UIMax = 5.0;
> = {-2.0};

// --- Percentile ---

float ui_LowPercent
<
    string UIName = "ADAPT | Low Percentile";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.80};

float ui_HighPercent
<
    string UIName = "ADAPT | High Percentile";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.95};

// --- Center-Weighted Metering ---

float ui_AdaptCenterWeight
<
    string UIName = "ADAPT | Center Weight";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.0};

// --- Asymmetric Speed ---

float ui_AdaptSpeedBrighten
<
    string UIName = "SPEED | Brighten Multiplier";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_AdaptSpeedDarken
<
    string UIName = "SPEED | Darken Multiplier";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

// --- Scene-Cut Detection ---

float ui_AdaptSceneCutThresh
<
    string UIName = "SPEED | Scene-Cut Threshold (log2)";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 5.0;
    float UIStep = 0.1;
> = {2.0};

float ui_AdaptSceneCutSpeed
<
    string UIName = "SPEED | Scene-Cut Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.5};

// --- Manual EV Override ---

bool ui_ManualEV
<
    string UIName = "ADAPT | Manual EV Enable";
> = {false};

float ui_EVValue
<
    string UIName = "ADAPT | EV Value (stops)";
    string UIWidget = "Spinner";
    float UIMin = -8.0; float UIMax = 8.0; float UIStep = 0.1;
> = {0.0};

float ui_EVCompensation
<
    string UIName = "ADAPT | EV Compensation";
    string UIWidget = "Spinner";
    float UIMin = -3.0; float UIMax = 3.0; float UIStep = 0.1;
> = {0.0};

// --- Metering Mode ---

int ui_MeterMode
<
    string UIName = "ADAPT | Meter Mode (0=Matrix 1=Center 2=Spot)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 2;
> = {1};

float ui_SpotSize
<
    string UIName = "ADAPT | Spot Size (for Spot mode)";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 10.0; float UIStep = 0.1;
> = {3.0};

// --- Histogram Debug ---

bool ui_HistDebug
<
    string UIName = "ADAPT | Histogram Debug Overlay";
> = {false};

// --- Sky Exclusion (luminance-based, no depth buffer in adaptation) ---

float ui_SkyExcludeThresh
<
    string UIName = "ADAPT | Sky Exclusion Threshold (log2)";
    string UIWidget = "Spinner";
    float UIMin = -2.0;
    float UIMax = 5.0;
    float UIStep = 0.1;
> = {1.5};

// --- Hysteresis (anti-oscillation deadzone near target) ---

float ui_AdaptHysteresis
<
    string UIName = "SPEED | Hysteresis Deadzone (log2)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.05};


//=== VERTEX SHADER ===//

void VS_Adapt(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
    txcoord -= 7.0 / 256.0;
}


//=== PIXEL SHADERS ===//

// Pass 0: Downsample 256x256 to 16x16 log2 luminance tiles
// Each output pixel averages an 8x8 block of the input.
// Uses geometric mean (log-domain accumulation) — matches logarithmic human perception.
// Sky exclusion: reject extremely bright pixels to prevent sky over-darkening landscape.
// Output: log2(geometric mean luminance) in R32F
float4 PS_Downsample(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float logSum = 0.0;
    float count = 0.0;
    float4 coord = float4(txcoord.xyy, 1.0 / 128.0);

    [loop]
    for (int x = 0; x < 8; x++)
    {
        coord.y = coord.z;
        [loop]
        for (int y = 0; y < 8; y++)
        {
            float3 color = TextureCurrent.SampleLevel(smpLinear, coord.xy, 0).rgb;
            float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
            float logLuma = log2(max(luma, 1e-5));

            // Sky exclusion: reject pixels above brightness threshold
            // Prevents bright sky from dominating adaptation (same goal as depth>0.999 mask)
            if (logLuma < ui_SkyExcludeThresh)
            {
                // Geometric mean: accumulate log2(luma), average later
                // exp(mean(log(luma))) — logarithmic average instead of arithmetic
                logSum += logLuma;
                count += 1.0;
            }
            coord.y += coord.w;
        }
        coord.x += coord.w;
    }

    // Output: average log2(luma) = log2(geometric_mean_luma)
    // Fallback: if all pixels excluded (indoors facing window), use threshold as estimate
    return (count > 0.5) ? (logSum / count) : ui_SkyExcludeThresh;
}


// Pass 1: Build 64-bin histogram from 16x16 tiles, extract adaptation value
// Multi-mode metering, Manual EV override, scene-cut detection, asymmetric speed.
// Output: 1x1 R32F adaptation value (linear luminance)
float4 PS_Histogram() : SV_Target
{
    // Manual EV bypass: skip histogram, output fixed photographic exposure
    // EV 0 = f/1.0 at 1 second. Each +1 EV = halve brightness.
    if (ui_ManualEV)
    {
        float exposure = pow(2.0, -(ui_EVValue + ui_EVCompensation));
        return exposure;
    }

    // Build 64-bin histogram from 16x16 input
    float4 coord = float4(1.0 / 32.0, 1.0 / 32.0, 1.0 / 32.0, 1.0 / 16.0);
    float4 bin[16]; // 16 float4s = 64 bins
    float totalWeight = 0.0;

    [unroll]
    for (int k = 0; k < 16; k++)
        bin[k] = 0.0;

    [loop]
    for (int i = 0; i < 16; i++)
    {
        coord.y = coord.z;
        [loop]
        for (int j = 0; j < 16; j++)
        {
            // Multi-mode metering weight
            float2 fromCenter = coord.xy - 0.5;
            float dist2 = dot(fromCenter, fromCenter);
            float weight = 1.0;

            if (ui_MeterMode == 1) // Center-Weighted
                weight = lerp(0.4, 1.0, exp(-dist2 * ui_AdaptCenterWeight * 8.0));
            else if (ui_MeterMode == 2) // Spot
                weight = exp(-dist2 * ui_SpotSize * ui_SpotSize);
            // Mode 0 = Matrix: uniform weight (1.0)

            float color = TextureCurrent.SampleLevel(smpPoint, coord.xy, 0.0).r;
            // Map log2 range [-5, 2] to [0, 63] bin index
            float level = saturate((color + 5.0) / 7.0) * 63.0;
            // Pack into float4 bins (4 bins per float4), weighted
            bin[level * 0.25] += weight * (float4(0.0, 1.0, 2.0, 3.0) == float4(trunc(level % 4).xxxx));
            totalWeight += weight;
            coord.y += coord.w;
        }
        coord.x += coord.w;
    }

    // Find adaptation anchor using percentile thresholds
    // Walk from highest bin down, accumulating weighted pixel counts
    float2 adaptAnchor = 0.5;
    float2 accumulate = float2(ui_HighPercent - 1.0, ui_LowPercent - 1.0) * totalWeight;

    [loop]
    for (int l = 15; l > 0; l--)
    {
        accumulate += bin[l].w;
        adaptAnchor = (accumulate.xy < bin[l].ww) ? l * 4.0 + accumulate.xy / bin[l].ww + 3.0 : adaptAnchor;

        accumulate += bin[l].z;
        adaptAnchor = (accumulate.xy < bin[l].zz) ? l * 4.0 + accumulate.xy / bin[l].zz + 2.0 : adaptAnchor;

        accumulate += bin[l].y;
        adaptAnchor = (accumulate.xy < bin[l].yy) ? l * 4.0 + accumulate.xy / bin[l].yy + 1.0 : adaptAnchor;

        accumulate += bin[l].x;
        adaptAnchor = (accumulate.xy < bin[l].xx) ? l * 4.0 + accumulate.xy / bin[l].xx + 0.0 : adaptAnchor;
    }

    // DNI interpolation for user parameters
    float bias = TF(lerp(lerp(ui_AdaptBiasNight, ui_AdaptBiasDay, ENightDayFactor),
                         ui_AdaptBiasInterior, EInteriorFactor),
                    GetTheme().adaptBias);
    float maxBright = lerp(lerp(ui_AdaptMaxNight, ui_AdaptMaxDay, ENightDayFactor),
                           ui_AdaptMaxInterior, EInteriorFactor);
    float minBright = lerp(lerp(ui_AdaptMinNight, ui_AdaptMinDay, ENightDayFactor),
                           ui_AdaptMinInterior, EInteriorFactor);

    // Convert histogram anchor back to log2 luminance and clamp
    float adaptLog2 = (adaptAnchor.x + adaptAnchor.y) * 0.5 / 63.0 * 7.0 - 5.0;
    float adapt = pow(2.0, clamp(adaptLog2, minBright, maxBright) + bias);

    // Temporal smoothing with asymmetric speed and scene-cut detection
    float prev = TexturePrevious.SampleLevel(smpPoint, 0.5, 0).x;
    float blendSpeed = AdaptationParameters.w;

    // Asymmetric: brighten vs darken can have different speeds
    // adapt > prev = scene getting brighter, adapt < prev = scene getting darker
    float speedMul = (adapt > prev) ? ui_AdaptSpeedBrighten : ui_AdaptSpeedDarken;
    blendSpeed *= speedMul;

    // Scene-cut: large luminance jump → fast adaptation
    float prevLog2 = log2(max(prev, 1e-5));
    float newLog2  = log2(max(adapt, 1e-5));
    float adaptDiff = abs(newLog2 - prevLog2);
    if (adaptDiff > ui_AdaptSceneCutThresh)
        blendSpeed = max(blendSpeed, ui_AdaptSceneCutSpeed);

    // Cell-load spike guard: cap blend speed to prevent instant adaptation
    // during cell transitions where frame time can spike to 0.5-2.0 seconds
    // (AdaptationParameters.w = AdaptationTime * elapsed, so huge elapsed → blendSpeed >> 1)
    blendSpeed = min(blendSpeed, 0.3);

    // Hysteresis: reduce blend speed near target to prevent oscillation
    // When adaptation is close to target, proportionally reduce blend
    if (ui_AdaptHysteresis > 0.001 && adaptDiff < ui_AdaptHysteresis)
        blendSpeed *= adaptDiff / ui_AdaptHysteresis;

    // Clamp blend to [0, 1]
    blendSpeed = saturate(blendSpeed);

    return lerp(prev, adapt, blendSpeed);
}


//=== TECHNIQUES ===//

technique11 Downsample <string UIName = "EotE: Adaptation";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Adapt()));
        SetPixelShader(CompileShader(ps_5_0, PS_Downsample()));
    }
}

technique11 Draw
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Adapt()));
        SetPixelShader(CompileShader(ps_5_0, PS_Histogram()));
    }
}
