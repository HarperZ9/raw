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
        result += TextureMask.Sample(smpLinear, coord).x;
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
        result += RenderTargetRGBA32.Sample(smpLinear, coord).x;
    }

    return result * 0.2;
}

// Pass 2-3: Depth-weighted blur (run twice for stronger effect)
// Reads TextureColor (which is the scene on first call, then blurred result on second)
float4 PS_DepthBlur(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float2 offsets[4] =
    {
        float2(-1.0, -1.0),
        float2(-1.0,  1.0),
        float2( 1.0, -1.0),
        float2( 1.0,  1.0),
    };

    float2 pixelOffset = float2(ScreenSize.y * ui_BlurRange, ScreenSize.y * ui_BlurRange * ScreenSize.z);

    float3 centerColor = TextureColor.Sample(smpPoint, txcoord).rgb;
    float depth = TextureDepth.Sample(smpPoint, txcoord).x;

    // Center weight: inverse of depth^4 (near = sharp)
    float centerW = saturate(depth * depth);
    centerW = 1.000001 - centerW * centerW;

    float3 color = centerColor * centerW;
    float weight = centerW;

    [unroll]
    for (int i = 0; i < 4; i++)
    {
        float2 coord = offsets[i] * pixelOffset + txcoord;
        float sDepth = TextureDepth.Sample(smpPoint, coord).x;

        float sW = saturate(sDepth * sDepth);
        sW = sW * sW;

        color += TextureColor.Sample(smpLinear, coord).rgb * sW;
        weight += sW;
    }

    color /= weight;

    float3 result = lerp(centerColor, color, ui_BlurAmount);
    return float4(result, 1.0);
}

// Pass 4: Main draw (tint, absorption, exposure, mask composite)
float4 PS_Draw(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float mask = RenderTargetR16F.Sample(smpLinear, txcoord).x;
    float3 color = TextureColor.Sample(smpLinear, txcoord).rgb;

    // Save for mask blending (this is the depth-blurred scene)
    float3 blurred = color;

    // Get original unprocessed scene for blending
    float3 original = TextureOriginal.SampleLevel(smpLinear, txcoord, 0).rgb;

    // Depth for absorption
    float depth = TextureDepth.Sample(smpPoint, txcoord).x;

    // Beer-Lambert absorption
    float3 absorption = exp(-ui_AbsorptionRGB * depth * ui_AbsorptionStrength * 10.0);
    color *= absorption;

    // Tint
    float3 tinted = color * ui_TintColor * 2.0;
    color = lerp(color, tinted, ui_TintAmount);

    // Exposure (DNI)
    float exposure = lerp(ui_ExposureNight, ui_ExposureDay, ENightDayFactor);
    color *= pow(2.0, exposure);

    // Mask compositing: blend between original and processed based on mask
    // mask=1 means fully underwater, mask=0 means above water
    float3 result = lerp(original, color, saturate(mask));

    return float4(result, 1.0);
}

// Pass 5: Wave UV distortion
float4 PS_Waves(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.Sample(smpLinear, txcoord).rgb;
    float mask = RenderTargetR16F.Sample(smpLinear, txcoord).x;

    if (!ui_WavesEnable) return float4(color, 1.0);

    float time = Timer.x * 1677.7216 * ui_WavesSpeed;
    float2 freq = ui_WavesFrequency * (txcoord + time);

    // Nested trig wave pattern
    float2 p = cos(float2(
        cos(freq.x - freq.y) * cos(freq.y),
        sin(freq.x + freq.y) * sin(freq.y)
    ));

    float2 freq2 = ui_WavesFrequency * (txcoord + 1.0 + time);
    float2 q = cos(float2(
        cos(freq2.x - freq2.y) * cos(freq2.y),
        sin(freq2.x + freq2.y) * sin(freq2.y)
    ));

    float amplitude = ui_WavesAmplitude;
    float2 displaced = txcoord + amplitude * (p - q) * PixelSize;

    float3 waved = TextureColor.Sample(smpLinear, displaced).rgb;
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
