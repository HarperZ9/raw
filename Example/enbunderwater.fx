//----------------------------------------------------------------------------------------------//
//                     ENB of the Elders - Underwater Effects                                    //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Underwater post-processing: mask blur, depth blur, tinting, wave distortion.                //
//  Based on AMON ENB / Reforged underwater systems.                                            //
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

Texture2D   TextureColor;           // Current scene (changes between techniques)
Texture2D   TextureOriginal;        // Original scene before underwater processing
Texture2D   TextureDepth;           // Depth buffer
Texture2D   TextureMask;            // Underwater mask (ENB-provided)
Texture2D   RenderTargetRGBA32;     // 32-bit RT
Texture2D   RenderTargetR16F;       // 16-bit single channel RT


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

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);


//=== UI PARAMETERS ===//

// --- Color ---

float ui_ExposureDay
<
    string UIName = "UW | Day - Exposure";
    string UIWidget = "Spinner";
    float UIMin = -2.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

float ui_ExposureNight
<
    string UIName = "UW | Night - Exposure";
    string UIWidget = "Spinner";
    float UIMin = -2.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {-0.3};

float ui_TintAmount
<
    string UIName = "UW | Tint Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {1.0};

float3 ui_TintColor
<
    string UIName = "UW | Tint Color";
    string UIWidget = "Color";
> = {0.3, 0.5, 0.4};

// --- Blur ---

float ui_BlurAmount
<
    string UIName = "UW | Blur Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {1.0};

float ui_BlurRange
<
    string UIName = "UW | Blur Range";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.5;
    float UIStep = 0.01;
> = {1.0};

float ui_SurfaceLineBlur
<
    string UIName = "UW | Surface Line Blur";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.0};

// --- Waves ---

bool ui_WavesEnable
<
    string UIName = "UW | Enable Waves";
> = {true};

float ui_WavesSpeed
<
    string UIName = "UW | Wave Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.0};

float ui_WavesAmplitude
<
    string UIName = "UW | Wave Amplitude";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 20.0;
    float UIStep = 1.0;
> = {12.0};

float ui_WavesFrequency
<
    string UIName = "UW | Wave Frequency";
    string UIWidget = "Spinner";
    float UIMin = 4.0;
    float UIMax = 20.0;
    float UIStep = 2.0;
> = {20.0};

// --- Absorption ---

float3 ui_AbsorptionRGB
<
    string UIName = "UW | Absorption RGB";
    string UIWidget = "Color";
> = {0.8, 0.4, 0.3};

float ui_AbsorptionStrength
<
    string UIName = "UW | Absorption Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};


//=== ADDON INCLUDES (after UI params — addons reference host variables) ===//

#include "Addons/Underwater_VolumetricFX.fxh"
#include "Addons/Underwater_SurfaceFX.fxh"


//=== VERTEX SHADER ===//

void VS_Basic(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


//=== PIXEL SHADERS ===//

// Pass 0: Horizontal mask blur → RenderTargetRGBA32
float4 PS_MaskBlurH(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float pixelOffset = ScreenSize.y * ui_SurfaceLineBlur;

    float result = 0.0;
    float offsets[5] = { -3.5, -1.5, 0.0, 1.5, 3.5 };

    [unroll]
    for (int i = 0; i < 5; i++)
    {
        float2 coord = txcoord + float2(offsets[i] * pixelOffset, 0.0);
        result += TextureMask.SampleLevel(smpLinear, coord, 0).x;
    }

    return result * 0.2;
}

// Pass 1: Vertical mask blur → RenderTargetR16F
float4 PS_MaskBlurV(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float pixelOffset = ScreenSize.y * ui_SurfaceLineBlur * ScreenSize.z;

    float result = 0.0;
    float offsets[5] = { -3.5, -1.5, 0.0, 1.5, 3.5 };

    [unroll]
    for (int i = 0; i < 5; i++)
    {
        float2 coord = txcoord + float2(0.0, offsets[i] * pixelOffset);
        result += RenderTargetRGBA32.SampleLevel(smpLinear, coord, 0).x;
    }

    return result * 0.2;
}

// Pass 2-3: Bilateral depth blur (8-tap cross + diagonal, run twice for stronger effect)
// Reads TextureColor (which is the scene on first call, then blurred result on second)
float4 PS_DepthBlur(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    // 8-tap pattern: 4 cardinal + 4 diagonal with appropriate weights
    static const float2 offsets[8] =
    {
        float2(-1.0,  0.0), float2( 1.0,  0.0),
        float2( 0.0, -1.0), float2( 0.0,  1.0),
        float2(-0.7, -0.7), float2( 0.7, -0.7),
        float2(-0.7,  0.7), float2( 0.7,  0.7),
    };
    static const float tapWeights[8] = {
        1.0, 1.0, 1.0, 1.0, 0.707, 0.707, 0.707, 0.707
    };

    float2 pixelOffset = float2(ScreenSize.y * ui_BlurRange, ScreenSize.y * ui_BlurRange * ScreenSize.z);

    float3 centerColor = TextureColor.SampleLevel(smpPoint, txcoord, 0).rgb;
    float centerDepth = TextureDepth.SampleLevel(smpPoint, txcoord, 0).x;

    // Depth-based blur strength: deep = strong blur, near = sharp
    float depthW = centerDepth * centerDepth;
    depthW = depthW * depthW; // pow4
    float centerW = 1.000001 - depthW;

    float3 color = centerColor * centerW;
    float totalW = centerW;

    [unroll]
    for (int i = 0; i < 8; i++)
    {
        float2 coord = offsets[i] * pixelOffset + txcoord;
        float sDepth = TextureDepth.SampleLevel(smpPoint, coord, 0).x;

        // Bilateral: reject samples at very different depths (prevents edge bleeding)
        float depthDiff = abs(sDepth - centerDepth);
        float bilateral = exp(-depthDiff * depthDiff * 10000.0);

        float sW = sDepth * sDepth;
        sW = sW * sW;
        sW *= bilateral * tapWeights[i];

        color += TextureColor.SampleLevel(smpLinear, coord, 0).rgb * sW;
        totalW += sW;
    }

    color /= totalW;

    float3 result = lerp(centerColor, color, ui_BlurAmount);
    return float4(result, 1.0);
}

// Pass 4: Main draw (tint, absorption, exposure, mask composite)
float4 PS_Draw(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float mask = RenderTargetR16F.SampleLevel(smpLinear, txcoord, 0).x;
    float3 color = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;

    // Save for mask blending (this is the depth-blurred scene)
    float3 blurred = color;

    // Get original unprocessed scene for blending
    float3 original = TextureOriginal.SampleLevel(smpLinear, txcoord, 0).rgb;

    // Depth for absorption
    float depth = TextureDepth.SampleLevel(smpPoint, txcoord, 0).x;

    // Beer-Lambert absorption
    float3 absorption = exp(-ui_AbsorptionRGB * depth * ui_AbsorptionStrength * 10.0);
    color *= absorption;

    // Photic zone grading (surface warmth, depth desat, cold tint, contrast loss)
    [branch] if (ui_AbsorpEnable)
        color = UW_Absorption(color, txcoord, depth);

    // Tint
    float3 tinted = color * ui_TintColor * 2.0;
    color = lerp(color, tinted, ui_TintAmount);

    // Caustics (trochoidal / noise / voronoi)
    [branch] if (ui_CausticEnable)
        color = UW_Caustics(color, txcoord, depth);

    // God rays (screen-space radial sampling)
    [branch] if (ui_GodRayEnable)
        color += UW_GodRays(txcoord, depth);

    // Tyndall scattering (forward-scatter haze)
    [branch] if (ui_TyndallEnable)
        color += UW_Tyndall(txcoord, depth);

    // Bioluminescence (depth-stratified plankton)
    [branch] if (ui_BioEnable)
        color += UW_Bioluminescence(txcoord, depth);

    // Particles (suspended sediment)
    [branch] if (ui_ParticleEnable)
        color += UW_Particles(txcoord, depth);

    // Bubbles (rising with wobble + refraction)
    [branch] if (ui_BubbleEnable)
        color = UW_Bubbles(color, txcoord, depth);

    // Exposure (DNI)
    float exposure = lerp(ui_ExposureNight, ui_ExposureDay, ENightDayFactor);
    color *= pow(2.0, exposure);

    // Mask compositing: blend between original and processed based on mask
    // mask=1 means fully underwater, mask=0 means above water
    float3 result = lerp(original, color, saturate(mask));

    return float4(result, 1.0);
}

// Pass 5: Gerstner wave UV distortion (4-octave multi-directional)
float4 PS_Waves(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;
    float mask = RenderTargetR16F.SampleLevel(smpLinear, txcoord, 0).x;

    if (!ui_WavesEnable) return float4(color, 1.0);

    float time = Timer.x * 1677.7216 * ui_WavesSpeed;

    // 4-octave Gerstner waves: each with distinct direction, frequency, amplitude
    static const float2 waveDirs[4] = {
        float2( 1.0,  0.3), float2(-0.5,  0.8),
        float2( 0.3, -1.0), float2(-0.8, -0.5),
    };
    static const float waveFreqs[4]  = { 8.0, 12.0, 6.0, 10.0 };
    static const float waveAmps[4]   = { 1.0, 0.6, 0.8, 0.4 };
    static const float waveSpeeds[4] = { 1.0, 1.3, 0.8, 1.1 };

    float2 displacement = 0.0;

    [unroll]
    for (int w = 0; w < 4; w++)
    {
        float2 dir = normalize(waveDirs[w]);
        float phase = dot(txcoord * ui_WavesFrequency, dir) * waveFreqs[w]
                    + time * waveSpeeds[w];

        // Gerstner: sinusoidal displacement along wave direction
        float2 offset = dir * waveAmps[w] * sin(phase);
        // Steepness harmonic (sharper crests)
        offset += dir * waveAmps[w] * 0.25 * sin(phase * 2.0 + 0.5);

        displacement += offset;
    }

    displacement *= ui_WavesAmplitude * PixelSize;

    float3 waved = TextureColor.SampleLevel(smpLinear, txcoord + displacement, 0).rgb;
    float3 result = lerp(color, waved, saturate(mask));

    return float4(result, 1.0);
}


//=== TECHNIQUES ===//

// Pass 0: Horizontal mask blur → RenderTargetRGBA32
technique11 EotE_MaskBlurH <string UIName = "EotE: Underwater"; string RenderTarget = "RenderTargetRGBA32";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_MaskBlurH()));
    }
}

// Pass 1: Vertical mask blur → RenderTargetR16F
technique11 EotE_MaskBlurV <string RenderTarget = "RenderTargetR16F";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_MaskBlurV()));
    }
}

// Pass 2: Depth blur pass 1
technique11 EotE_DepthBlur1
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_DepthBlur()));
    }
}

// Pass 3: Depth blur pass 2
technique11 EotE_DepthBlur2
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_DepthBlur()));
    }
}

// Pass 4: Main draw
technique11 EotE_Draw
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Draw()));
    }
}

// Pass 5: Wave distortion
technique11 EotE_Waves
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Waves()));
    }
}

// Pass 6: Snell's Window / Total Internal Reflection
technique11 EotE_Waves1
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_SnellWindow()));
    }
}

// Pass 7: Wet Lens (droplets + streaks)
technique11 EotE_Waves2
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_WetLens()));
    }
}
