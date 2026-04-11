//----------------------------------------------------------------------------------------------//
//                      ENB of the Elders - HDR Compositor                                      //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Main post-processing pass: bloom/lens mixing, adaptation, tonemapping, color grading.       //
//                                                                                              //
//  Tonemapper based on Boris Vorontsov extended Reinhard (AMON/PRT standard)                   //
//  Multi-operator selector via EotE_Tonemappers.fxh                                            //
//  Adapted for ENB of the Elders by Zain Dana Harper - March 2026                              //
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
float   FieldOfView;
float4  tempF1;
float4  tempF2;
float4  tempF3;
float4  tempInfo1;
float4  tempInfo2;


//=== GAME PARAMETERS ===//

float4  Params01[7];    // Skyrim SE parameters
float4  ENBParams01;    // x = bloom amount, y = lens amount


//=== TEXTURES ===//

Texture2D   TextureColor;       // HDR scene color (full-res)
Texture2D   TextureBloom;       // Bloom texture from enbbloom.fx
Texture2D   TextureLens;        // Lens effects from enblens.fx
Texture2D   TextureDepth;       // Scene depth
Texture2D   TextureAdaptation;  // 1x1 adaptation value from enbadaptation.fx
Texture2D   TextureAperture;    // 1x1 DOF aperture from enbdepthoffield.fx


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


//=== TONEMAPPER LIBRARY ===//

#include "Helper/EotE_Tonemappers.fxh"


//=== UI PARAMETERS ===//

// --- Fade ---

bool ui_EnableFade
<
    string UIName = "SCENE | Enable Fade Transition";
> = {true};

// --- Tonemapping ---

int ui_TonemapMode
<
    string UIName = "TONEMAP | Operator (0=Lin 1=Reinhard 2=Hejl 3=Hable 4=ACES 5=AgX 6=Lottes)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 6;
> = {1};

float ui_TonemapCurveDay
<
    string UIName = "TONEMAP | Day - Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 5.0;
> = {0.5};

float ui_TonemapCurveNight
<
    string UIName = "TONEMAP | Night - Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 5.0;
> = {0.5};

float ui_TonemapCurveInterior
<
    string UIName = "TONEMAP | Interior - Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 5.0;
> = {0.5};

float ui_WhitePointDay
<
    string UIName = "TONEMAP | Day - White Point";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 200.0;
> = {60.0};

float ui_WhitePointNight
<
    string UIName = "TONEMAP | Night - White Point";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 200.0;
> = {50.0};

float ui_WhitePointInterior
<
    string UIName = "TONEMAP | Interior - White Point";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 200.0;
> = {30.0};

// --- Adaptation ---

float ui_AdaptMaxDay
<
    string UIName = "ADAPT | Day - Max";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 10.0;
> = {1.0};

float ui_AdaptMaxNight
<
    string UIName = "ADAPT | Night - Max";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 10.0;
> = {1.0};

float ui_AdaptMaxInterior
<
    string UIName = "ADAPT | Interior - Max";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 10.0;
> = {1.0};

float ui_AdaptMinDay
<
    string UIName = "ADAPT | Day - Min";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.001;
> = {0.05};

float ui_AdaptMinNight
<
    string UIName = "ADAPT | Night - Min";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.001;
> = {0.05};

float ui_AdaptMinInterior
<
    string UIName = "ADAPT | Interior - Min";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.001;
> = {0.05};

// --- Brightness ---

float ui_BrightnessDay
<
    string UIName = "COLOR | Day - Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
> = {1.0};

float ui_BrightnessNight
<
    string UIName = "COLOR | Night - Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
> = {0.75};

float ui_BrightnessInterior
<
    string UIName = "COLOR | Interior - Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
> = {1.0};

// --- Contrast ---

float ui_ContrastDay
<
    string UIName = "COLOR | Day - Contrast";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 3.0;
> = {1.0};

float ui_ContrastNight
<
    string UIName = "COLOR | Night - Contrast";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 3.0;
> = {1.0};

float ui_ContrastInterior
<
    string UIName = "COLOR | Interior - Contrast";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 3.0;
> = {1.0};

// --- Saturation ---

float ui_SaturationDay
<
    string UIName = "COLOR | Day - Saturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
> = {1.0};

float ui_SaturationNight
<
    string UIName = "COLOR | Night - Saturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
> = {0.9};

float ui_SaturationInterior
<
    string UIName = "COLOR | Interior - Saturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
> = {1.0};


//=== CONSTANTS ===//

static const float3 LUM = float3(0.2126, 0.7152, 0.0722);


//=== VERTEX SHADER ===//

void VS_Draw(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


//=== PIXEL SHADER ===//

float4 PS_Draw(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    // Sample scene
    float4 color = TextureColor.Sample(smpPoint, txcoord);

    // Add lens effects
    float3 lens = TextureLens.Sample(smpLinear, txcoord).rgb;
    color.rgb += lens * ENBParams01.y;

    // Add bloom (subtractive method: only add what bloom adds above the scene)
    float3 bloom = TextureBloom.Sample(smpLinear, txcoord).rgb;
    bloom = max(bloom - color.rgb, 0.0);
    color.rgb += bloom * ENBParams01.x;

    // Fade transition (loading screens, etc.)
    if (ui_EnableFade)
    {
        float3 fadeColor = Params01[5].xyz;
        float fadeWeight = Params01[5].w;
        color.rgb = lerp(color.rgb, fadeColor, fadeWeight);
    }

    // Read adaptation
    float adaptation = TextureAdaptation.Sample(smpPoint, 0.5).x;
    adaptation = clamp(adaptation, 0.001, 50.0);

    // DNI interpolation for all parameters
    float adaptMax = lerp(lerp(ui_AdaptMaxNight, ui_AdaptMaxDay, ENightDayFactor),
                          ui_AdaptMaxInterior, EInteriorFactor);
    float adaptMin = lerp(lerp(ui_AdaptMinNight, ui_AdaptMinDay, ENightDayFactor),
                          ui_AdaptMinInterior, EInteriorFactor);
    float brightness = lerp(lerp(ui_BrightnessNight, ui_BrightnessDay, ENightDayFactor),
                            ui_BrightnessInterior, EInteriorFactor);
    float contrast = lerp(lerp(ui_ContrastNight, ui_ContrastDay, ENightDayFactor),
                          ui_ContrastInterior, EInteriorFactor);
    float saturation = lerp(lerp(ui_SaturationNight, ui_SaturationDay, ENightDayFactor),
                            ui_SaturationInterior, EInteriorFactor);
    float curve = lerp(lerp(ui_TonemapCurveNight, ui_TonemapCurveDay, ENightDayFactor),
                       ui_TonemapCurveInterior, EInteriorFactor);
    float whitePoint = lerp(lerp(ui_WhitePointNight, ui_WhitePointDay, ENightDayFactor),
                            ui_WhitePointInterior, EInteriorFactor);

    // Apply adaptation (exposure normalization)
    color.rgb = color.rgb / max(adaptation * adaptMax + adaptMin, 1e-4);

    // Apply brightness
    color.rgb *= brightness;

    // Apply contrast (mid-gray pivot)
    color.rgb = max(color.rgb, 1e-6);
    float midGray = 0.18;
    color.rgb = pow(color.rgb / midGray, contrast) * midGray;

    // Apply saturation (luminance-preserving)
    float luma = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    color.rgb = max(luma + saturation * (color.rgb - luma), 0.0);

    // Apply tonemapping
    color.rgb = ApplyTonemap(color.rgb, ui_TonemapMode, curve, whitePoint, 1.0);

    // Final output dither (anti-banding)
    float3 noise = float3(
        frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715)))),
        frac(52.9829189 * frac(dot(pos.xy + 72.0, float2(0.06711056, 0.00583715)))),
        frac(52.9829189 * frac(dot(pos.xy + 144.0, float2(0.06711056, 0.00583715))))
    );
    color.rgb += (noise - 0.5) / 255.0;

    color.rgb = saturate(color.rgb);
    return color;
}


//=== TECHNIQUES ===//

technique11 EotE_Draw <string UIName = "EotE: HDR Compositor";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
        SetPixelShader(CompileShader(ps_5_0, PS_Draw()));
    }
}
