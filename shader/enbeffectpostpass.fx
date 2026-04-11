//----------------------------------------------------------------------------------------------//
//                     ENB of the Elders - Post-Processing                                      //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Final polish: CAS sharpening, film grain, optical vignette, dithering.                      //
//                                                                                              //
//  CAS: AMD FidelityFX Contrast Adaptive Sharpening                                           //
//  Grain: Luminance-adaptive with IGN temporal noise                                           //
//  Vignette: Natural cos^4 optical falloff                                                     //
//  Dither: Valve/Iestyn RGB quantization dither                                                //
//                                                                                              //
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


//=== TEXTURES ===//

Texture2D   TextureColor;       // Scene from enbeffect.fx
Texture2D   TextureDepth;       // Scene depth


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


//=== CONSTANTS ===//

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);


//=== UI PARAMETERS ===//

// --- Sharpening ---

bool ui_SharpenEnable
<
    string UIName = "SHARP | Enable CAS";
> = {true};

float ui_SharpenIntensity
<
    string UIName = "SHARP | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

// --- Film Grain ---

bool ui_GrainEnable
<
    string UIName = "GRAIN | Enable Film Grain";
> = {true};

float ui_GrainIntensity
<
    string UIName = "GRAIN | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.5;
    float UIStep = 0.001;
> = {0.12};

float ui_GrainSize
<
    string UIName = "GRAIN | Size";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 4.0;
    float UIStep = 0.1;
> = {1.5};

// --- Vignette ---

float ui_VignetteAmount
<
    string UIName = "VIGNETTE | Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.6};

float ui_VignetteRadius
<
    string UIName = "VIGNETTE | Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.0};


//=== VERTEX SHADER ===//

void VS_Basic(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


//=== PIXEL SHADER ===//

// Single pass: CAS Sharpening + Vignette + Film Grain + Dither
float4 PS_PostPass(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.Sample(smpPoint, txcoord).rgb;

    // --- CAS Sharpening ---
    if (ui_SharpenEnable)
    {
        // Sample 5-tap cross pattern
        float3 n = TextureColor.Sample(smpPoint, txcoord + float2(0, -PixelSize.y)).rgb;
        float3 s = TextureColor.Sample(smpPoint, txcoord + float2(0,  PixelSize.y)).rgb;
        float3 e = TextureColor.Sample(smpPoint, txcoord + float2( PixelSize.x, 0)).rgb;
        float3 w = TextureColor.Sample(smpPoint, txcoord + float2(-PixelSize.x, 0)).rgb;

        // Find local min/max luma
        float lumaC = dot(color, float3(0.2126, 0.7152, 0.0722));
        float lumaN = dot(n, float3(0.2126, 0.7152, 0.0722));
        float lumaS = dot(s, float3(0.2126, 0.7152, 0.0722));
        float lumaE = dot(e, float3(0.2126, 0.7152, 0.0722));
        float lumaW = dot(w, float3(0.2126, 0.7152, 0.0722));

        float lumaMin = min(lumaC, min(min(lumaN, lumaS), min(lumaE, lumaW)));
        float lumaMax = max(lumaC, max(max(lumaN, lumaS), max(lumaE, lumaW)));

        // CAS: sharpen proportional to local contrast
        float contrast = lumaMax - lumaMin;
        float sharpAmount = saturate(contrast / max(lumaMax, 0.04)) * ui_SharpenIntensity;

        // Apply unsharp mask weighted by contrast
        float3 blur = (n + s + e + w) * 0.25;
        color = saturate(color + (color - blur) * sharpAmount);
    }

    // --- Vignette (cos^4 optical falloff) ---
    if (ui_VignetteAmount > 0.0)
    {
        float2 centered = txcoord * 2.0 - 1.0;
        float dist = length(centered) / ui_VignetteRadius;
        float cos2 = saturate(1.0 - dist * dist);
        float vignette = lerp(1.0, cos2 * cos2, ui_VignetteAmount);
        color *= vignette;
    }

    // --- Film Grain ---
    if (ui_GrainEnable)
    {
        float2 grainCoord = pos.xy / ui_GrainSize;
        float seed = dot(grainCoord, float2(12.9898, 78.233)) + Timer.x * 43758.5453;
        float grain = frac(sin(seed) * 43758.5453) - 0.5;

        float luma = dot(color, float3(0.2126, 0.7152, 0.0722));
        float adaptiveScale = lerp(1.0, 0.3, saturate(luma));

        color += grain * ui_GrainIntensity * adaptiveScale;
    }

    // --- Final dither (anti-banding) ---
    float3 noise = float3(
        frac(52.9829189 * frac(dot(pos.xy, float2(0.06711056, 0.00583715)))),
        frac(52.9829189 * frac(dot(pos.xy + 72.0, float2(0.06711056, 0.00583715)))),
        frac(52.9829189 * frac(dot(pos.xy + 144.0, float2(0.06711056, 0.00583715))))
    );
    color += (noise - 0.5) / 255.0;

    return float4(saturate(color), 1.0);
}


//=== TECHNIQUES ===//

technique11 EotE_PostPass <string UIName = "EotE: Post-Pass";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_PostPass()));
    }
}
