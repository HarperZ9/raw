//----------------------------------------------------------------------------------------------//
//                         ENB of the Elders - Eye Adaptation                                   //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Histogram-based auto-exposure with 64-bin percentile anchoring.                             //
//                                                                                              //
//  Algorithm: kingeric1992 (ENB Forum, Nov 2016)                                               //
//  Based on Unreal Engine auto-exposure description                                            //
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


//=== UI PARAMETERS ===//

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


//=== VERTEX SHADER ===//

void VS_Adapt(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
    txcoord -= 7.0 / 256.0;
}


//=== PIXEL SHADERS ===//

// Pass 0: Downsample 256x256 to 16x16 log2 luminance tiles
// Each output pixel averages an 8x8 block of the input.
// Output: log2(average luminance) in R32F
float4 PS_Downsample(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float res = 0.0;
    float4 coord = float4(txcoord.xyy, 1.0 / 128.0);

    [loop]
    for (int x = 0; x < 8; x++)
    {
        coord.y = coord.z;
        [loop]
        for (int y = 0; y < 8; y++)
        {
            float3 color = TextureCurrent.SampleLevel(smpLinear, coord.xy, 0).rgb;
            // Rec.709 luminance weighting
            float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
            res += max(luma, 1e-5);
            coord.y += coord.w;
        }
        coord.x += coord.w;
    }

    return log2(res) - 6.0; // log2(res / 64.0)
}


// Pass 1: Build 64-bin histogram from 16x16 tiles, extract adaptation value
// Uses percentile anchoring to reject outlier luminance.
// Output: 1x1 R32F adaptation value (linear luminance)
float4 PS_Histogram() : SV_Target
{
    // Build 64-bin histogram from 16x16 input
    float4 coord = float4(1.0 / 32.0, 1.0 / 32.0, 1.0 / 32.0, 1.0 / 16.0);
    float4 bin[16]; // 16 float4s = 64 bins

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
            float color = TextureCurrent.SampleLevel(smpPoint, coord.xy, 0.0).r;
            // Map log2 range [-5, 2] to [0, 63] bin index
            float level = saturate((color + 5.0) / 7.0) * 63.0;
            // Pack into float4 bins (4 bins per float4)
            bin[level * 0.25] += (float4(0.0, 1.0, 2.0, 3.0) == float4(trunc(level % 4).xxxx));
            coord.y += coord.w;
        }
        coord.x += coord.w;
    }

    // Find adaptation anchor using percentile thresholds
    // Walk from highest bin down, accumulating pixel counts
    float2 adaptAnchor = 0.5;
    float2 accumulate = float2(ui_HighPercent - 1.0, ui_LowPercent - 1.0) * 256.0;

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
    float bias = lerp(lerp(ui_AdaptBiasNight, ui_AdaptBiasDay, ENightDayFactor),
                      ui_AdaptBiasInterior, EInteriorFactor);
    float maxBright = lerp(lerp(ui_AdaptMaxNight, ui_AdaptMaxDay, ENightDayFactor),
                           ui_AdaptMaxInterior, EInteriorFactor);
    float minBright = lerp(lerp(ui_AdaptMinNight, ui_AdaptMinDay, ENightDayFactor),
                           ui_AdaptMinInterior, EInteriorFactor);

    // Convert histogram anchor back to log2 luminance and clamp
    float adapt = (adaptAnchor.x + adaptAnchor.y) * 0.5 / 63.0 * 7.0 - 5.0;
    adapt = pow(2.0, clamp(adapt, minBright, maxBright) + bias);

    // Temporal smoothing: blend with previous frame
    float blendSpeed = AdaptationParameters.w;
    float prev = TexturePrevious.Sample(smpPoint, 0.5).x;
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
