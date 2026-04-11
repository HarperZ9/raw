//----------------------------------------------------------------------------------------------//
//                         ENB of the Elders - Dynamic Gaussian Bloom                           //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Multi-pass separable Gaussian bloom with Karis anti-firefly threshold.                      //
//                                                                                              //
//  Algorithm: Dynamic Gaussian Bloom 2.2 by LonelyKitsuune/Skratzer                           //
//  Karis threshold from Brian Karis, "Real Shading in Unreal Engine 4", 2013                   //
//  Adapted for ENB of the Elders by Zain Dana Harper - March 2026                              //
//                                                                                              //
//  Flow: Threshold → 7 mip levels (H+V Gaussian each) → Weighted Mix                          //
//  16 techniques total, operating on 1024x1024 bloom texture                                   //
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

Texture2D   TextureDownsampled;     // 1024x1024 HDR scene (downscaled by ENB)
Texture2D   TextureColor;           // Output of previous technique (1024x1024)
Texture2D   TextureOriginal;        // Full-res scene (avoid: aliasing)
Texture2D   TextureDepth;           // Full-res depth (avoid: aliasing)
Texture2D   TextureAperture;        // 1x1 aperture from DOF

// Render targets for each mip level
Texture2D   RenderTarget1024;
Texture2D   RenderTarget512;
Texture2D   RenderTarget256;
Texture2D   RenderTarget128;
Texture2D   RenderTarget64;
Texture2D   RenderTarget32;
Texture2D   RenderTarget16;


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


//=== SKYRIMBRIDGE DATA ===//
#include "Helper/SkyrimBridge_CB.fxh"


//=== CONSTANTS ===//

#define MAX_BLOOM_RES  1024.0
#define MAXLOOP        30
#define MAXHDR         16384.0

static const float SqrtTwoPI = sqrt(2.0 * 3.14159265);
static const float3 LUM_709 = float3(0.2126, 0.7152, 0.0722);


//=== UI PARAMETERS ===//

// --- Threshold ---

float ui_BloomThreshold
<
    string UIName = "BLOOM | Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_BloomSoftKnee
<
    string UIName = "BLOOM | Soft Knee";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.5};

// --- Gaussian Sigma ---

float ui_BloomSigmaDay
<
    string UIName = "BLOOM | Day - Sigma (blur width)";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 15.0;
    float UIStep = 0.1;
> = {5.0};

float ui_BloomSigmaNight
<
    string UIName = "BLOOM | Night - Sigma (blur width)";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 15.0;
    float UIStep = 0.1;
> = {5.0};

float ui_BloomSigmaInterior
<
    string UIName = "BLOOM | Interior - Sigma (blur width)";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 15.0;
    float UIStep = 0.1;
> = {4.0};

// --- Intensity ---

float ui_BloomIntensityDay
<
    string UIName = "BLOOM | Day - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_BloomIntensityNight
<
    string UIName = "BLOOM | Night - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_BloomIntensityInterior
<
    string UIName = "BLOOM | Interior - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.8};

// --- Saturation ---

float ui_BloomSaturation
<
    string UIName = "BLOOM | Saturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.0};

// --- SkyrimBridge Integration ---

bool ui_SB_AdaptiveThreshold
<
    string UIName = "SB | Feedback-Adaptive Threshold";
> = {false};

float ui_SB_AdaptiveStr
<
    string UIName = "SB | Adaptive Threshold Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

bool ui_SB_WeatherBloom
<
    string UIName = "SB | Weather-Responsive Bloom";
> = {false};

float ui_SB_FogBloomBoost
<
    string UIName = "SB | Fog/Rain Bloom Softness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

// --- Mip Weights ---

float ui_MipWeight1024
<
    string UIName = "BLOOM | Mip Weight 1024";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

float ui_MipWeight512
<
    string UIName = "BLOOM | Mip Weight 512";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.8};

float ui_MipWeight256
<
    string UIName = "BLOOM | Mip Weight 256";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.0};

float ui_MipWeight128
<
    string UIName = "BLOOM | Mip Weight 128";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.9};

float ui_MipWeight64
<
    string UIName = "BLOOM | Mip Weight 64";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.6};

float ui_MipWeight32
<
    string UIName = "BLOOM | Mip Weight 32";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.3};

float ui_MipWeight16
<
    string UIName = "BLOOM | Mip Weight 16";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.1};


//=== STRUCTS ===//

struct VS_OUTPUT_BASIC
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

struct VS_OUTPUT_BLUR
{
    float4 pos             : SV_POSITION;
    float2 texcoord        : TEXCOORD0;
    nointerpolation float2 scaledPixelSize : PRECALC0;
    nointerpolation float4 screenBounds    : PRECALC1;
    nointerpolation float  weightFactor    : PRECALC2;
    nointerpolation float  sigma2Inv       : PRECALC3;
    nointerpolation float  loopCount       : PRECALC4;
    nointerpolation float  intensity       : PRECALC5;
    nointerpolation float  skipPS          : PRECALC6;
};


//=== VERTEX SHADERS ===//

VS_OUTPUT_BASIC VS_Basic(float4 pos : POSITION, float2 txcoord : TEXCOORD0)
{
    VS_OUTPUT_BASIC OUT;
    OUT.pos = float4(pos.xyz, 1.0);
    OUT.texcoord = txcoord;
    return OUT;
}

VS_OUTPUT_BLUR VS_GaussBlur(float3 pos : POSITION, float2 txcoord : TEXCOORD0,
                             uniform float texSize, uniform bool isHorizontal,
                             uniform float maxLoop)
{
    VS_OUTPUT_BLUR OUT;

    // Scale factor: ratio of max bloom resolution to target mip resolution
    float2 transform;
    transform.x = MAX_BLOOM_RES / texSize;
    transform.y = (transform.x - 1.0) / transform.x;

    // H blur: scale screen quad down to target size
    // V blur: scale texcoords to match target resolution
    OUT.pos.xy = isHorizontal ? pos.xy / transform.x + float2(-transform.y, transform.y) : pos.xy;
    OUT.pos.zw = float2(pos.z, 1.0);
    OUT.texcoord = isHorizontal ? txcoord : txcoord / transform.x;

    // Pixel size for blur offset (H or V direction only)
    OUT.scaledPixelSize = rcp(texSize) * (isHorizontal ? float2(1.0, 0.0) : float2(0.0, ScreenSize.z / transform.x));
    OUT.screenBounds = (isHorizontal ? 1.0 : rcp(transform.x)) + float4(-OUT.scaledPixelSize, OUT.scaledPixelSize);
    OUT.scaledPixelSize *= min(transform.x / 4.0, 2.0);

    // DNI sigma interpolation
    float sigma = lerp(lerp(ui_BloomSigmaNight, ui_BloomSigmaDay, ENightDayFactor),
                       ui_BloomSigmaInterior, EInteriorFactor);

    // Skip pixel shader if sigma is too small for this mip level
    OUT.skipPS = 0.0 > (max(sigma, 1.0) - pow(transform.x, 0.2));

    if (!OUT.skipPS)
    {
        sigma = min(sigma, maxLoop - 1.5);
        OUT.intensity = lerp(lerp(ui_BloomIntensityNight, ui_BloomIntensityDay, ENightDayFactor),
                             ui_BloomIntensityInterior, EInteriorFactor);
        OUT.weightFactor = rcp(sigma * SqrtTwoPI);
        OUT.loopCount = min(ceil(sigma * 1.6), MAXLOOP);
        OUT.sigma2Inv = -rcp(sigma * sigma);
    }

    return OUT;
}


//=== PIXEL SHADERS ===//

// Pass 0: Karis soft-knee threshold extraction
float4 PS_Threshold(VS_OUTPUT_BASIC IN) : SV_Target
{
    float3 bloom = TextureDownsampled.Sample(smpLinear, IN.texcoord).rgb;

    // Karis anti-firefly soft threshold
    float luma = dot(bloom, LUM_709);

    // SB: Feedback-adaptive threshold — lower threshold in dark scenes, raise in bright
    float threshold = ui_BloomThreshold;
    [branch]
    if (ui_SB_AdaptiveThreshold && SB_HasFeedback())
    {
        float sceneAvg = SB_Computed_Luminance.x; // avg luminance from feedback
        float adaptBias = lerp(-0.3, 0.3, saturate(sceneAvg * 4.0));
        threshold = max(threshold + adaptBias * ui_SB_AdaptiveStr, 0.05);
    }

    float knee = threshold * ui_BloomSoftKnee;
    float soft = luma - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, luma - threshold) / max(luma, 1e-5);
    bloom *= max(contribution, 0.0);

    // Saturation control
    float bloomLuma = dot(bloom, LUM_709);
    bloom = lerp(bloomLuma, bloom, ui_BloomSaturation);

    return float4(max(bloom, 0.0), 1.0);
}


// Gaussian blur pass (used for all 14 blur techniques)
float4 PS_GaussBlur(VS_OUTPUT_BLUR IN, uniform Texture2D inputTex) : SV_Target
{
    if (IN.skipPS > 0.5) return 0.0;

    // Center sample
    float3 bloom = IN.weightFactor * inputTex.Sample(smpLinear, IN.texcoord).rgb;
    float weightSum = IN.weightFactor;

    // Symmetric Gaussian kernel
    [loop]
    for (float i = 1.0; i <= IN.loopCount; i++)
    {
        float4 offset = IN.texcoord.xyxy + IN.scaledPixelSize.xyxy * float4(i.xx, -i.xx);

        // Clamp offsets to screen bounds to prevent border artifacts
        offset = saturate(min(offset, IN.screenBounds));

        float gaussWeight = IN.weightFactor * exp(i * i * IN.sigma2Inv);
        bloom += gaussWeight * inputTex.SampleLevel(smpLinear, offset.xy, 0).rgb;
        bloom += gaussWeight * inputTex.SampleLevel(smpLinear, offset.zw, 0).rgb;
        weightSum += gaussWeight * 2.0;
    }

    bloom /= weightSum;
    bloom *= IN.intensity;

    return float4(clamp(bloom, 0.0, MAXHDR), 1.0);
}


// Final mix: combine all mip levels with configurable weights
float4 PS_BloomMix(VS_OUTPUT_BASIC IN) : SV_Target
{
    float3 bloom = 0.0;

    // Accumulate all mip levels with their weights (additive — no normalization)
    float3 mip1024 = RenderTarget1024.Sample(smpLinear, IN.texcoord).rgb;
    float3 mip512  = RenderTarget512.Sample(smpLinear, IN.texcoord).rgb;
    float3 mip256  = RenderTarget256.Sample(smpLinear, IN.texcoord).rgb;
    float3 mip128  = RenderTarget128.Sample(smpLinear, IN.texcoord).rgb;
    float3 mip64   = RenderTarget64.Sample(smpLinear, IN.texcoord).rgb;
    float3 mip32   = RenderTarget32.Sample(smpLinear, IN.texcoord).rgb;
    float3 mip16   = RenderTarget16.Sample(smpLinear, IN.texcoord).rgb;

    bloom += mip1024 * ui_MipWeight1024;
    bloom += mip512  * ui_MipWeight512;
    bloom += mip256  * ui_MipWeight256;
    bloom += mip128  * ui_MipWeight128;
    bloom += mip64   * ui_MipWeight64;
    bloom += mip32   * ui_MipWeight32;
    bloom += mip16   * ui_MipWeight16;

    // SB: Weather-responsive bloom — boost wide mips in fog/rain for softer glow
    [branch]
    if (ui_SB_WeatherBloom)
    {
        float fogFactor = saturate(SB_Fog_Density.y * 2.0); // fog max opacity
        float rainFactor = SB_RainFlag() * SB_Precipitation.y;
        float weatherBoost = max(fogFactor, rainFactor) * ui_SB_FogBloomBoost;
        // Shift weight toward wider mips (softer, more diffuse bloom in bad weather)
        bloom += (mip64 + mip32 + mip16) * weatherBoost * 0.5;
    }

    return float4(clamp(bloom, 0.0, MAXHDR), 1.0);
}


//=== TECHNIQUE MACROS ===//

#define BLUR_TECH(NAME, TEX_SIZE, IS_HORI, MAX_LOOP, INPUT_TEX) \
technique11 NAME { pass p0 { \
    SetVertexShader(CompileShader(vs_5_0, VS_GaussBlur(TEX_SIZE, IS_HORI, MAX_LOOP))); \
    SetPixelShader(CompileShader(ps_5_0, PS_GaussBlur(INPUT_TEX))); } }


//=== TECHNIQUES ===//
// 16 techniques: Threshold + 7 mip levels x 2 (H+V) + Mix
// Odd blur techniques write to TextureColor (no annotation)
// Even blur techniques write to RenderTarget (with annotation)

// Tech 0: Threshold extraction
technique11 EotE_BloomThreshold <string UIName = "EotE: Bloom Threshold";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Threshold()));
    }
}

// Tech 1-2: Gaussian blur @ 1024
BLUR_TECH(EotE_Bloom1024H,                                            1024.0, true,  256.0, TextureColor)
BLUR_TECH(EotE_Bloom1024V <string RenderTarget="RenderTarget1024";>,   1024.0, false, 256.0, TextureColor)

// Tech 3-4: Gaussian blur @ 512
BLUR_TECH(EotE_Bloom512H,                                             512.0,  true,  128.0, RenderTarget1024)
BLUR_TECH(EotE_Bloom512V  <string RenderTarget="RenderTarget512";>,    512.0,  false, 128.0, TextureColor)

// Tech 5-6: Gaussian blur @ 256
BLUR_TECH(EotE_Bloom256H,                                             256.0,  true,  64.0,  RenderTarget512)
BLUR_TECH(EotE_Bloom256V  <string RenderTarget="RenderTarget256";>,    256.0,  false, 64.0,  TextureColor)

// Tech 7-8: Gaussian blur @ 128
BLUR_TECH(EotE_Bloom128H,                                             128.0,  true,  32.0,  RenderTarget256)
BLUR_TECH(EotE_Bloom128V  <string RenderTarget="RenderTarget128";>,    128.0,  false, 32.0,  TextureColor)

// Tech 9-10: Gaussian blur @ 64
BLUR_TECH(EotE_Bloom64H,                                              64.0,   true,  16.0,  RenderTarget128)
BLUR_TECH(EotE_Bloom64V   <string RenderTarget="RenderTarget64";>,     64.0,   false, 16.0,  TextureColor)

// Tech 11-12: Gaussian blur @ 32
BLUR_TECH(EotE_Bloom32H,                                              32.0,   true,  8.0,   RenderTarget64)
BLUR_TECH(EotE_Bloom32V   <string RenderTarget="RenderTarget32";>,     32.0,   false, 8.0,   TextureColor)

// Tech 13-14: Gaussian blur @ 16
BLUR_TECH(EotE_Bloom16H,                                              16.0,   true,  4.0,   RenderTarget32)
BLUR_TECH(EotE_Bloom16V   <string RenderTarget="RenderTarget16";>,     16.0,   false, 4.0,   TextureColor)

// Tech 15: Final weighted mix
technique11 EotE_BloomMix
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_BloomMix()));
    }
}
