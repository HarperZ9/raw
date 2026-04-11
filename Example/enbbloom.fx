//----------------------------------------------------------------------------------------------//
//                         ENB of the Elders - Cinematic Bloom                                   //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Multi-pass bloom with Gaussian or Dual Kawase blur modes, Karis anti-firefly                 //
//  threshold, anamorphic horizontal stretch, and color-temperature tinting.                     //
//                                                                                              //
//  Base: Dynamic Gaussian Bloom 2.2 by LonelyKitsuune/Skratzer                                 //
//  Karis threshold: Brian Karis, "Real Shading in Unreal Engine 4", 2013                        //
//  Kelvin: Tanner Helland, "How to Convert Temperature in Kelvin to RGB"                        //
//  Adapted for ENB of the Elders by Zain Dana Harper - March 2026                              //
//                                                                                              //
//  Flow: Threshold → 7 mip levels (H+V Gaussian each) → Weighted Tinted Mix                   //
//  16 techniques total, operating on 1024x1024 bloom texture                                    //
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


//=== GLOBALS ===//

#include "enbglobals.fxh"


//=== CONSTANTS ===//

#define MAX_BLOOM_RES  1024.0
#define MAXLOOP        30
#define MAXHDR         16384.0

static const float SqrtTwoPI = sqrt(2.0 * 3.14159265);
static const float3 LUM_709 = float3(0.2126, 0.7152, 0.0722);


//=== UI PARAMETERS ===//

// --- Mode ---

int ui_BloomMode
<
    string UIName = "BLOOM | Mode (0=Gaussian 1=Kawase)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 1;
> = {0};

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

float ui_BloomThreshCurve
<
    string UIName = "BLOOM | Threshold Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {1.0};

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

// --- Anamorphic ---

float ui_BloomAnamorphic
<
    string UIName = "BLOOM | Anamorphic Ratio";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {1.0};

// --- Bloom Tint (Color Temperature per DNI) ---

float ui_BloomTintTempDay
<
    string UIName = "TINT | Day - Temperature (K)";
    string UIWidget = "Spinner";
    float UIMin = 2000.0;
    float UIMax = 12000.0;
    float UIStep = 50.0;
> = {6500.0};

float ui_BloomTintTempNight
<
    string UIName = "TINT | Night - Temperature (K)";
    string UIWidget = "Spinner";
    float UIMin = 2000.0;
    float UIMax = 12000.0;
    float UIStep = 50.0;
> = {6500.0};

float ui_BloomTintTempInterior
<
    string UIName = "TINT | Interior - Temperature (K)";
    string UIWidget = "Spinner";
    float UIMin = 2000.0;
    float UIMax = 12000.0;
    float UIStep = 50.0;
> = {6500.0};

float ui_BloomTintStrength
<
    string UIName = "TINT | Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

// --- Mip Desaturation ---

float ui_BloomMipDesatFalloff
<
    string UIName = "BLOOM | Mip Desat Falloff";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

// --- Per-Mip Spectral Tint (Rayleigh-motivated warm→cool gradient) ---

float ui_BloomSpectralTint
<
    string UIName = "BLOOM | Spectral Tint (warm near, cool far)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

// --- Chromatic Bloom Dispersion ---

float ui_BloomChromaDisp
<
    string UIName = "BLOOM | Chromatic Dispersion";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

// --- Depth-Masked Bloom ---

bool ui_BloomDepthMask
<
    string UIName = "BLOOM | Enable Depth Mask (reduce near bloom)";
> = {false};

float ui_BloomDepthMaskStart
<
    string UIName = "BLOOM | Depth Mask Start (near plane)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.01;
    float UIStep = 0.0001;
> = {0.0};

float ui_BloomDepthMaskEnd
<
    string UIName = "BLOOM | Depth Mask End (fade distance)";
    string UIWidget = "Spinner";
    float UIMin = 0.001;
    float UIMax = 0.1;
    float UIStep = 0.001;
> = {0.01};

// --- Height-Based Bloom ---

bool ui_BloomHeightMask
<
    string UIName = "BLOOM | Enable Height Mask";
> = {false};

float ui_BloomHeightCenter
<
    string UIName = "BLOOM | Height Center (screen Y)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

float ui_BloomHeightWidth
<
    string UIName = "BLOOM | Height Band Width";
    string UIWidget = "Spinner";
    float UIMin = 0.05; float UIMax = 1.0; float UIStep = 0.01;
> = {0.4};

float ui_BloomHeightBoost
<
    string UIName = "BLOOM | Height Boost (center band)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.05;
> = {1.5};

float ui_BloomHeightMin
<
    string UIName = "BLOOM | Height Min (outside band)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

// --- Color-Selective Bloom ---

bool ui_BloomColorSelect
<
    string UIName = "BLOOM | Enable Color-Selective Bloom";
> = {false};

float3 ui_BloomColorTarget
<
    string UIName = "BLOOM | Color Target (bloom this hue)";
    string UIWidget = "Color";
> = {1.0, 0.8, 0.4};

float ui_BloomColorRange
<
    string UIName = "BLOOM | Color Range (tolerance)";
    string UIWidget = "Spinner";
    float UIMin = 0.01; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_BloomColorBoost
<
    string UIName = "BLOOM | Color Boost";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 5.0; float UIStep = 0.1;
> = {2.0};

// --- Energy Conservation ---

bool ui_BloomEnergyConserve
<
    string UIName = "MIX | Energy-Conserving Blend";
> = {false};

float ui_BloomEnergyStr
<
    string UIName = "MIX | Conservation Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

// --- Adaptation-Proxy Feedback ---

bool ui_BloomAdaptFeedback
<
    string UIName = "MIX | Adaptation Feedback";
> = {false};

float ui_BloomAdaptFeedbackStr
<
    string UIName = "MIX | Adapt Feedback Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.5};

// --- GMM PSF Bloom (lens character presets) ---

int ui_BloomPSF
<
    string UIName = "MIX | PSF Preset (0=Manual)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 4;
> = {0};

// --- Anamorphic Streak (mip-derived, zero extra passes) ---

float ui_StreakIntensity
<
    string UIName = "STREAK | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.0};

float ui_StreakSpread
<
    string UIName = "STREAK | Spread";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 6.0;
    float UIStep = 0.1;
> = {2.0};

float ui_StreakTint
<
    string UIName = "STREAK | Anamorphic Blue Tint";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

// --- Mip Weights ---

float ui_MipWeight1024
<
    string UIName = "MIX | Mip Weight 1024";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

float ui_MipWeight512
<
    string UIName = "MIX | Mip Weight 512";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.8};

float ui_MipWeight256
<
    string UIName = "MIX | Mip Weight 256";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.0};

float ui_MipWeight128
<
    string UIName = "MIX | Mip Weight 128";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.9};

float ui_MipWeight64
<
    string UIName = "MIX | Mip Weight 64";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.6};

float ui_MipWeight32
<
    string UIName = "MIX | Mip Weight 32";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.3};

float ui_MipWeight16
<
    string UIName = "MIX | Mip Weight 16";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.1};


//=== HELPERS ===//

// Kelvin → RGB (Tanner Helland approximation)
// 6500K = D65 neutral. Lower = warm (orange). Higher = cool (blue).
float3 KelvinToRGB(float K)
{
    float temp = K * 0.01;
    float3 rgb;

    rgb.r = (temp <= 66.0) ? 1.0
          : saturate(1.292936 * pow(abs(temp - 60.0), -0.1332047));

    rgb.g = (temp <= 66.0)
          ? saturate(0.390082 * log(max(temp, 1.0)) - 0.631889)
          : saturate(1.129891 * pow(abs(temp - 60.0), -0.0755148));

    rgb.b = (temp >= 66.0) ? 1.0
          : (temp <= 19.0) ? 0.0
          : saturate(0.543207 * log(max(temp - 10.0, 1.0)) - 1.19625);

    return rgb;
}

// Horizontal-only Gaussian streak from a bloom mip (KinoStreak-inspired)
// Ref: Keijiro Takahashi, Unity Post Processing Stack v2
// 9-tap (±4) horizontal Gaussian with configurable spread. Zero extra passes.
float3 MipStreak(Texture2D mipTex, float2 uv, float texelWidth, float spread)
{
    float3 streak = mipTex.SampleLevel(smpLinear, uv, 0).rgb;
    float totalW = 1.0;

    [unroll] for (int i = 1; i <= 4; i++)
    {
        float t = (float)i / 4.0;
        float w = exp(-t * t * 2.0);
        float2 off = float2(float(i) * texelWidth * spread, 0.0);
        streak += mipTex.SampleLevel(smpLinear, saturate(uv + off), 0).rgb * w;
        streak += mipTex.SampleLevel(smpLinear, saturate(uv - off), 0).rgb * w;
        totalW += w * 2.0;
    }
    return streak / totalW;
}


// GMM PSF Bloom: 4-component Gaussian Mixture Model mapped to 7 mip levels
// Each component: { weight, sigma } — sigma in pixels at 1024 base resolution
// Log-sigma splatting distributes each Gaussian component across mip levels
// whose spatial extent (resolution) best matches that sigma.
//
// Preset 0: Manual (use ui_MipWeight* directly)
// Preset 1: Clean — sharp modern lens, tight core
// Preset 2: Vintage — soft vintage lens with wide halo
// Preset 3: Diffusion — pro-mist / soft-FX filter look
// Preset 4: Cinema — anamorphic prime character

// Each preset: 4 components x float2(weight, sigma)
// Sigma in pixels at 1024 base, weight is relative energy
static const float2 GMM_CLEAN[4] = {
    float2(0.60, 2.0),   // tight core
    float2(0.25, 8.0),   // inner glow
    float2(0.10, 32.0),  // mid spread
    float2(0.05, 128.0)  // faint wide halo
};

static const float2 GMM_VINTAGE[4] = {
    float2(0.30, 4.0),   // softer core
    float2(0.30, 16.0),  // prominent inner glow
    float2(0.25, 64.0),  // wide spread
    float2(0.15, 256.0)  // strong halo
};

static const float2 GMM_DIFFUSION[4] = {
    float2(0.20, 4.0),   // weak core
    float2(0.25, 12.0),  // soft glow
    float2(0.30, 48.0),  // dominant spread
    float2(0.25, 200.0)  // dreamy wide
};

static const float2 GMM_CINEMA[4] = {
    float2(0.45, 3.0),   // sharp core
    float2(0.25, 10.0),  // controlled glow
    float2(0.20, 40.0),  // gentle spread
    float2(0.10, 160.0)  // subtle halo
};

// Map sigma to mip index: mip i covers spatial extent ~1024/2^i pixels
// log2(sigma) maps to mip level, Gaussian splatting distributes across neighbors
void GMMToMipWeights(int preset, out float weights[7])
{
    // Mip spatial extents: 1024, 512, 256, 128, 64, 32, 16
    // log2(extent): 10, 9, 8, 7, 6, 5, 4
    static const float MIP_LOG2[7] = { 10.0, 9.0, 8.0, 7.0, 6.0, 5.0, 4.0 };

    [unroll] for (int m = 0; m < 7; m++) weights[m] = 0.0;

    float2 components[4];
    if      (preset == 1) { components[0] = GMM_CLEAN[0];     components[1] = GMM_CLEAN[1];     components[2] = GMM_CLEAN[2];     components[3] = GMM_CLEAN[3]; }
    else if (preset == 2) { components[0] = GMM_VINTAGE[0];   components[1] = GMM_VINTAGE[1];   components[2] = GMM_VINTAGE[2];   components[3] = GMM_VINTAGE[3]; }
    else if (preset == 3) { components[0] = GMM_DIFFUSION[0]; components[1] = GMM_DIFFUSION[1]; components[2] = GMM_DIFFUSION[2]; components[3] = GMM_DIFFUSION[3]; }
    else                  { components[0] = GMM_CINEMA[0];    components[1] = GMM_CINEMA[1];    components[2] = GMM_CINEMA[2];    components[3] = GMM_CINEMA[3]; }

    // For each GMM component, splat its weight into neighboring mip levels
    // using Gaussian kernel in log-sigma space (splatRadius = 0.8 octaves)
    [unroll] for (int c = 0; c < 4; c++)
    {
        float compWeight = components[c].x;
        float compSigma  = components[c].y;
        float logSigma   = log2(max(compSigma, 1.0));

        [unroll] for (int m2 = 0; m2 < 7; m2++)
        {
            float dist = logSigma - MIP_LOG2[m2];
            // Gaussian splatting: sigma = 0.8 octaves
            float splat = exp(-dist * dist * 0.78125); // 1/(2*0.8^2) = 0.78125
            weights[m2] += compWeight * splat;
        }
    }

    // Normalize so total weight = 1
    float total = 0.0;
    [unroll] for (int n = 0; n < 7; n++) total += weights[n];
    float invTotal = 1.0 / max(total, 0.001);
    [unroll] for (int n2 = 0; n2 < 7; n2++) weights[n2] *= invTotal;
}


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
    nointerpolation float2 kawasePixel     : PRECALC7;
    nointerpolation float  kawaseIter      : PRECALC8;
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

    // Anamorphic: stretch horizontal blur sampling distance
    // >1.0 = wider horizontal bloom (cinema). 1.0 = standard circular.
    if (isHorizontal)
        OUT.scaledPixelSize *= ui_BloomAnamorphic;

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
        OUT.intensity = TF(lerp(lerp(ui_BloomIntensityNight, ui_BloomIntensityDay, ENightDayFactor),
                               ui_BloomIntensityInterior, EInteriorFactor),
                           GetTheme().bloomIntensity);
        OUT.weightFactor = rcp(sigma * SqrtTwoPI);
        OUT.loopCount = min(ceil(sigma * 1.6), MAXLOOP);
        OUT.sigma2Inv = -rcp(sigma * sigma);

        // Kawase: pre-scaled 2D pixel size for diagonal sampling
        OUT.kawasePixel = isHorizontal
            ? float2(rcp(texSize), rcp(texSize) * ScreenSize.z)
            : float2(1.0, ScreenSize.z) / MAX_BLOOM_RES;
        OUT.kawasePixel *= max(sigma / 3.0, 1.0);
        OUT.kawasePixel.x *= ui_BloomAnamorphic;
        OUT.kawaseIter = isHorizontal ? 0.0 : 1.0;
    }

    return OUT;
}


//=== PIXEL SHADERS ===//

// Pass 0: Karis anti-firefly threshold with soft-knee curve control
// Ref: Brian Karis, "Real Shading in Unreal Engine 4" (SIGGRAPH 2013)
//      Jorge Jimenez, "Next Generation Post Processing in Call of Duty" (2014)
float4 PS_Threshold(VS_OUTPUT_BASIC IN) : SV_Target
{
    // Karis 2014 anti-firefly: 2x2 neighborhood weighted by 1/(1+luma)
    // Prevents single bright pixels from creating disproportionate bloom spikes.
    // Cost: 3 extra texture reads (4 total vs 1), but eliminates firefly artifacts
    // which allows pushing bloom intensity higher without visual noise.
    float2 px = float2(1.0 / MAX_BLOOM_RES, 1.0 / MAX_BLOOM_RES * ScreenSize.z);
    float3 a = TextureDownsampled.SampleLevel(smpLinear, IN.texcoord + float2(-px.x, -px.y) * 0.5, 0).rgb;
    float3 b = TextureDownsampled.SampleLevel(smpLinear, IN.texcoord + float2( px.x, -px.y) * 0.5, 0).rgb;
    float3 c = TextureDownsampled.SampleLevel(smpLinear, IN.texcoord + float2(-px.x,  px.y) * 0.5, 0).rgb;
    float3 d = TextureDownsampled.SampleLevel(smpLinear, IN.texcoord + float2( px.x,  px.y) * 0.5, 0).rgb;

    float wa = 1.0 / (1.0 + dot(a, LUM_709));
    float wb = 1.0 / (1.0 + dot(b, LUM_709));
    float wc = 1.0 / (1.0 + dot(c, LUM_709));
    float wd = 1.0 / (1.0 + dot(d, LUM_709));

    float3 bloom = (a * wa + b * wb + c * wc + d * wd) / (wa + wb + wc + wd);

    // Karis soft-knee threshold
    float luma = dot(bloom, LUM_709);
    float knee = ui_BloomThreshold * ui_BloomSoftKnee;
    float soft = luma - ui_BloomThreshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, luma - ui_BloomThreshold) / max(luma, 1e-5);

    // Threshold curve: <1 = gentler rolloff (more bloom), >1 = steeper (less bloom)
    contribution = pow(max(contribution, 0.0), ui_BloomThreshCurve);
    bloom *= contribution;

    // Depth mask: reduce bloom contribution from near-field objects
    // Prevents character self-bloom (bright armor, skin highlights)
    if (ui_BloomDepthMask)
    {
        float depth = TextureDepth.SampleLevel(smpLinear, IN.texcoord, 0).x;
        float depthMask = smoothstep(ui_BloomDepthMaskStart, ui_BloomDepthMaskEnd, depth);
        bloom *= depthMask;
    }

    // Height mask: boost/suppress bloom in a screen-space horizontal band
    // Useful for horizon bloom emphasis, reduce ground/sky bloom
    if (ui_BloomHeightMask)
    {
        float heightDist = abs(IN.texcoord.y - ui_BloomHeightCenter) / max(ui_BloomHeightWidth, 0.01);
        float heightMask = lerp(ui_BloomHeightBoost, ui_BloomHeightMin, saturate(heightDist));
        bloom *= heightMask;
    }

    // Color-selective bloom: boost bloom for pixels matching a target hue
    if (ui_BloomColorSelect)
    {
        float3 normBloom = bloom / max(dot(bloom, LUM_709), 0.001);
        float3 normTarget = ui_BloomColorTarget / max(dot(ui_BloomColorTarget, LUM_709), 0.001);
        float colorDist = length(normBloom - normTarget);
        float colorMatch = 1.0 - saturate(colorDist / ui_BloomColorRange);
        bloom *= lerp(1.0, ui_BloomColorBoost, colorMatch * colorMatch);
    }

    // Saturation control
    float bloomLuma = dot(bloom, LUM_709);
    bloom = lerp(bloomLuma, bloom, ui_BloomSaturation);

    return float4(max(bloom, 0.0), 1.0);
}


// Gaussian blur pass (used for all 14 blur techniques)
float4 PS_GaussBlur(VS_OUTPUT_BLUR IN, uniform Texture2D inputTex) : SV_Target
{
    if (IN.skipPS > 0.5) return 0.0;

    // Kawase dual-filter blur: 5 taps per pass (2 passes per mip = 10 total)
    // vs Gaussian: up to 60+ taps. Faster, softer falloff, good for wide bloom.
    [branch] if (ui_BloomMode == 1)
    {
        float2 d = IN.kawasePixel * (IN.kawaseIter + 0.5);
        float3 k = inputTex.SampleLevel(smpLinear, IN.texcoord, 0).rgb * 4.0;
        k += inputTex.SampleLevel(smpLinear, saturate(IN.texcoord + float2(-d.x, -d.y)), 0).rgb;
        k += inputTex.SampleLevel(smpLinear, saturate(IN.texcoord + float2( d.x, -d.y)), 0).rgb;
        k += inputTex.SampleLevel(smpLinear, saturate(IN.texcoord + float2(-d.x,  d.y)), 0).rgb;
        k += inputTex.SampleLevel(smpLinear, saturate(IN.texcoord + float2( d.x,  d.y)), 0).rgb;
        k = k / 8.0 * IN.intensity;
        return float4(clamp(k, 0.0, MAXHDR), 1.0);
    }

    // Center sample
    float3 bloom = IN.weightFactor * inputTex.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
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


// Final mix: combine all mip levels with weights, desaturation, and tint
float4 PS_BloomMix(VS_OUTPUT_BASIC IN) : SV_Target
{
    // Sample all mip levels
    float3 mip1024 = RenderTarget1024.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float3 mip512  = RenderTarget512.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float3 mip256  = RenderTarget256.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float3 mip128  = RenderTarget128.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float3 mip64   = RenderTarget64.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float3 mip32   = RenderTarget32.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float3 mip16   = RenderTarget16.SampleLevel(smpLinear, IN.texcoord, 0).rgb;

    // Per-channel chromatic dispersion: wider mips shift R/B outward/inward
    // Simulates lens dispersion where different wavelengths bloom at different rates
    if (ui_BloomChromaDisp > 0.001)
    {
        float2 dispDir = (IN.texcoord - 0.5) * ui_BloomChromaDisp * 0.003;

        // Mip 128: mild dispersion (scale 1x)
        mip128.r = RenderTarget128.SampleLevel(smpLinear, IN.texcoord + dispDir * 1.0, 0).r;
        mip128.b = RenderTarget128.SampleLevel(smpLinear, IN.texcoord - dispDir * 1.0, 0).b;

        // Mip 64: moderate dispersion (scale 2x)
        mip64.r = RenderTarget64.SampleLevel(smpLinear, IN.texcoord + dispDir * 2.0, 0).r;
        mip64.b = RenderTarget64.SampleLevel(smpLinear, IN.texcoord - dispDir * 2.0, 0).b;

        // Mip 32: strong dispersion (scale 3x)
        mip32.r = RenderTarget32.SampleLevel(smpLinear, IN.texcoord + dispDir * 3.0, 0).r;
        mip32.b = RenderTarget32.SampleLevel(smpLinear, IN.texcoord - dispDir * 3.0, 0).b;

        // Mip 16: widest dispersion (scale 4x)
        mip16.r = RenderTarget16.SampleLevel(smpLinear, IN.texcoord + dispDir * 4.0, 0).r;
        mip16.b = RenderTarget16.SampleLevel(smpLinear, IN.texcoord - dispDir * 4.0, 0).b;
    }

    // Per-mip desaturation: wider mips progressively lose color (natural diffusion)
    // At 0.0: no effect. At 1.0: widest mip (16) is fully desaturated.
    if (ui_BloomMipDesatFalloff > 0.001)
    {
        // mip1024: index 0/6 = 0% desat (sharpest bloom stays saturated)
        // mip16:   index 6/6 = 100% of falloff applied (widest bloom washes out)
        mip512  = lerp(mip512,  dot(mip512,  LUM_709), ui_BloomMipDesatFalloff * (1.0 / 6.0));
        mip256  = lerp(mip256,  dot(mip256,  LUM_709), ui_BloomMipDesatFalloff * (2.0 / 6.0));
        mip128  = lerp(mip128,  dot(mip128,  LUM_709), ui_BloomMipDesatFalloff * (3.0 / 6.0));
        mip64   = lerp(mip64,   dot(mip64,   LUM_709), ui_BloomMipDesatFalloff * (4.0 / 6.0));
        mip32   = lerp(mip32,   dot(mip32,   LUM_709), ui_BloomMipDesatFalloff * (5.0 / 6.0));
        mip16   = lerp(mip16,   dot(mip16,   LUM_709), ui_BloomMipDesatFalloff);
    }

    // Per-mip spectral tinting: warm near-field, cool far-field (Rayleigh motivation)
    // Blue light scatters more → near-field bloom (sharp detail) appears warmer,
    // far-field bloom (wide glow) picks up scattered blue. Simulates atmospheric behavior.
    float spectralTint = TF(ui_BloomSpectralTint, GetTheme().bloomSpectralTint);
    if (spectralTint > 0.001)
    {
        float s = spectralTint;
        mip1024 *= lerp(1.0, float3(1.04, 1.00, 0.95), s); // warmest (near-field)
        mip512  *= lerp(1.0, float3(1.02, 1.00, 0.97), s);
        mip256  *= 1.0;                                       // neutral midpoint
        mip128  *= lerp(1.0, float3(0.98, 1.00, 1.02), s);
        mip64   *= lerp(1.0, float3(0.96, 1.00, 1.05), s);
        mip32   *= lerp(1.0, float3(0.93, 1.00, 1.08), s);
        mip16   *= lerp(1.0, float3(0.90, 1.00, 1.12), s); // coolest (far-field)
    }

    // Weighted accumulation
    float3 bloom = 0.0;

    [branch] if (ui_BloomPSF > 0)
    {
        // GMM PSF: lens character preset overrides manual mip weights
        float gmmW[7];
        GMMToMipWeights(ui_BloomPSF, gmmW);

        bloom += mip1024 * gmmW[0];
        bloom += mip512  * gmmW[1];
        bloom += mip256  * gmmW[2];
        bloom += mip128  * gmmW[3];
        bloom += mip64   * gmmW[4];
        bloom += mip32   * gmmW[5];
        bloom += mip16   * gmmW[6];
    }
    else
    {
        // Manual mip weights (additive — no normalization)
        bloom += mip1024 * ui_MipWeight1024;
        bloom += mip512  * ui_MipWeight512;
        bloom += mip256  * ui_MipWeight256;
        bloom += mip128  * ui_MipWeight128;
        bloom += mip64   * ui_MipWeight64;
        bloom += mip32   * ui_MipWeight32;
        bloom += mip16   * ui_MipWeight16;
    }

    // Anamorphic streak: horizontal-only mip sampling (KinoStreak-inspired)
    // Derives streak from existing mip data — zero extra passes.
    if (ui_StreakIntensity > 0.001)
    {
        float3 streak = MipStreak(RenderTarget32, IN.texcoord, 1.0 / 32.0, ui_StreakSpread)
                       + MipStreak(RenderTarget16, IN.texcoord, 1.0 / 16.0, ui_StreakSpread * 1.5);
        streak *= 0.5; // normalize the two-mip average

        // Anamorphic blue tint (characteristic of anamorphic lenses)
        float3 tint = lerp(1.0, float3(0.7, 0.85, 1.25), ui_StreakTint);
        bloom += streak * ui_StreakIntensity * tint;
    }

    // Color temperature tint (DNI-interpolated Kelvin)
    // 6500K = neutral (no change). <6500 = warm golden bloom. >6500 = cool blue bloom.
    if (ui_BloomTintStrength > 0.001)
    {
        float tempK = lerp(lerp(ui_BloomTintTempNight, ui_BloomTintTempDay, ENightDayFactor),
                           ui_BloomTintTempInterior, EInteriorFactor);
        float3 tintRGB = KelvinToRGB(tempK) / max(KelvinToRGB(6500.0), 0.001);
        bloom *= lerp(1.0, tintRGB, ui_BloomTintStrength);
    }

    // Adaptation-proxy feedback: estimate scene brightness from lowest bloom mip
    // and modulate bloom output inversely. Bright scene = less bloom contribution,
    // dark scene = more. Prevents over-bloom in dark scenes and under-bloom in bright.
    // Uses the widest mip as a scene-average luminance proxy (no TextureAdaptation needed).
    if (ui_BloomAdaptFeedback)
    {
        float3 sceneAvg = RenderTarget16.SampleLevel(smpLinear, float2(0.5, 0.5), 0).rgb;
        float avgLuma = dot(sceneAvg, LUM_709);
        // Inverse modulation: dim scenes boost, bright scenes suppress
        float adaptMod = 1.0 / max(1.0 + avgLuma * ui_BloomAdaptFeedbackStr * 3.0, 0.1);
        bloom *= lerp(1.0, adaptMod, ui_BloomAdaptFeedbackStr);
    }

    // Energy-conserving bloom composition: limits total bloom energy to prevent
    // blown-out highlights. Uses lerp-based blending where bloom replaces scene
    // energy rather than adding to it. At full strength, total brightness never
    // exceeds the brighter of (scene, bloom). At 0, reverts to additive.
    // Ref: "Physically-Based Bloom" (Karis 2014, Jimenez 2014)
    if (ui_BloomEnergyConserve)
    {
        float bloomLuma = dot(bloom, LUM_709);
        // Normalize bloom to unit energy, scale by conservation strength
        float normFactor = max(bloomLuma, 0.001);
        float3 bloomNorm = bloom / normFactor;
        float conservedLuma = min(bloomLuma, normFactor);
        bloom = lerp(bloom, bloomNorm * conservedLuma, ui_BloomEnergyStr);
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
// H blur techniques write to TextureColor (no RT annotation)
// V blur techniques write to named RenderTarget (RT annotation)

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

// Tech 15: Final weighted mix with tint and desaturation
technique11 EotE_BloomMix
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_BloomMix()));
    }
}
