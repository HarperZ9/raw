//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                Skin Subsurface Scattering Addon for ENBSeries  v1.0                          //
//                by Zain Dana Harper                                                           //
//                                                                                              //
//  Physically-based separable SSS using the Christensen-Burley normalized                      //
//  diffusion profile.  13-tap kernel with per-channel scatter distances                        //
//  (R=wide, G=medium, B=narrow) fitted to human skin.                                         //
//                                                                                              //
//  Two passes: horizontal blur (EotE_PrePass1) + vertical blur with                           //
//  skin tone controls (EotE_PrePass2).                                                        //
//                                                                                              //
//  Skin detection via TextureMask.alpha (Kitsuune convention).                                //
//  Bilateral depth + normal rejection prevents bleeding across edges.                          //
//  Curvature-aware radius from normal Laplacian.                                              //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//----------------------------------------------------------------------------------------------//
//                                 UI Parameters                                                //
//----------------------------------------------------------------------------------------------//

bool  ui_SSSEnable
<
    string UIName = "05 SSS - Subsurface Scatter (Burley diffusion profile)";
> = {false};

float ui_SSSRadius
<
    string UIName = "05 SSS | Scatter Radius (pixels)";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 10.0;
> = {3.0};

float ui_SSSIntensity
<
    string UIName = "05 SSS | Scatter Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.5};

float ui_SSSMaskThresh
<
    string UIName = "05 SSS | Skin Mask Threshold (alpha cutoff)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.5};

float ui_SSSDepthReject
<
    string UIName = "05 SSS | Depth Rejection (bilateral)";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 100.0;
> = {20.0};

float ui_SSSNormalReject
<
    string UIName = "05 SSS | Normal Rejection (bilateral)";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 16.0;
> = {4.0};

float ui_SSSCurvatureScale
<
    string UIName = "05 SSS | Curvature Radius Scale (Laplacian)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
> = {1.0};

float ui_SSSSkinHue
<
    string UIName = "05 SSS | Skin Hue Shift";
    string UIWidget = "Spinner";
    float UIMin = -0.1;
    float UIMax = 0.1;
> = {0.0};

float ui_SSSSkinSat
<
    string UIName = "05 SSS | Skin Saturation";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 2.0;
> = {1.0};

float ui_SSSSkinBright
<
    string UIName = "05 SSS | Skin Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 2.0;
> = {1.0};

float ui_SSSPreserveSpec
<
    string UIName = "05 SSS | Specular Preservation (highlight restore)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.5};

bool  ui_SSSDebugMask
<
    string UIName = "05 SSS | Show Skin Mask (debug)";
> = {false};


//----------------------------------------------------------------------------------------------//
//                          SSS Kernel Data (Christensen-Burley)                                //
//----------------------------------------------------------------------------------------------//
//
//  13-tap separable kernel fitted to Burley normalized diffusion profile.
//  Per-channel scatter distances: R=2.0mm (widest), G=0.8mm, B=0.4mm (narrowest).
//
//  Offsets are in pixel-space, scaled by ui_SSSRadius at runtime.
//  Weights are normalized per-channel (each sums to ~1.0).

#define SSS_KERNEL_SIZE 13

static const float SSS_Offsets[SSS_KERNEL_SIZE] = {
     0.0,
     1.0,  -1.0,
     2.0,  -2.0,
     3.25, -3.25,
     5.0,  -5.0,
     7.5,  -7.5,
    11.0, -11.0
};

static const float3 SSS_Weights[SSS_KERNEL_SIZE] = {
    float3(0.2300, 0.4400, 0.6200),    // Center: blue dominates (narrow profile)
    float3(0.1000, 0.0930, 0.0560),    // +/- 1px
    float3(0.1000, 0.0930, 0.0560),
    float3(0.0780, 0.0650, 0.0280),    // +/- 2px
    float3(0.0780, 0.0650, 0.0280),
    float3(0.0550, 0.0370, 0.0115),    // +/- 3.25px
    float3(0.0550, 0.0370, 0.0115),
    float3(0.0340, 0.0175, 0.0042),    // +/- 5px: green fading
    float3(0.0340, 0.0175, 0.0042),
    float3(0.0175, 0.0070, 0.0012),    // +/- 7.5px: only red significant
    float3(0.0175, 0.0070, 0.0012),
    float3(0.0055, 0.0013, 0.0002),    // +/- 11px: red tail
    float3(0.0055, 0.0013, 0.0002)
};


//----------------------------------------------------------------------------------------------//
//                              Helper Functions                                                //
//----------------------------------------------------------------------------------------------//

float SSS_GetLinearDepth(float2 uv)
{
    float d = TextureDepth.SampleLevel(smpPoint, uv, 0).x;
    return d / (d * (-2999.0) + 3000.0);
}

float SSS_EstimateCurvature(float2 UV, float Radius)
{
    float2 Offset = PixelSize * Radius;

    float3 NC = TextureNormal.SampleLevel(smpPoint, UV, 0).xyz * 2.0 - 1.0;
    float3 NL = TextureNormal.SampleLevel(smpPoint, UV + float2(-Offset.x, 0), 0).xyz * 2.0 - 1.0;
    float3 NR = TextureNormal.SampleLevel(smpPoint, UV + float2( Offset.x, 0), 0).xyz * 2.0 - 1.0;
    float3 NU = TextureNormal.SampleLevel(smpPoint, UV + float2(0, -Offset.y), 0).xyz * 2.0 - 1.0;
    float3 ND = TextureNormal.SampleLevel(smpPoint, UV + float2(0,  Offset.y), 0).xyz * 2.0 - 1.0;

    float3 Laplacian = NC - 0.25 * (NL + NR + NU + ND);
    return length(Laplacian);
}

float SSS_NormalBilateral(float3 CenterN, float3 SampleN, float Power)
{
    float NdN = saturate(dot(CenterN, SampleN));
    return pow(abs(NdN), Power);
}

float SSS_EstimateSpecular(float3 Original, float3 Blurred)
{
    float origLuma = dot(Original, LUM_709);
    float blurLuma = dot(Blurred, LUM_709);
    return saturate(origLuma - blurLuma);
}

// Simple RGB → HSL → RGB for skin tone controls
float3 SSS_HueShift(float3 color, float shift)
{
    // Rotate in YIQ-like space for efficiency
    float cosA = cos(shift * 6.2831853);
    float sinA = sin(shift * 6.2831853);
    float3 result;
    result.r = color.r * (0.299 + 0.701 * cosA + 0.168 * sinA)
             + color.g * (0.587 - 0.587 * cosA + 0.330 * sinA)
             + color.b * (0.114 - 0.114 * cosA - 0.497 * sinA);
    result.g = color.r * (0.299 - 0.299 * cosA - 0.328 * sinA)
             + color.g * (0.587 + 0.413 * cosA + 0.035 * sinA)
             + color.b * (0.114 - 0.114 * cosA + 0.292 * sinA);
    result.b = color.r * (0.299 - 0.300 * cosA + 1.250 * sinA)
             + color.g * (0.587 - 0.588 * cosA - 1.050 * sinA)
             + color.b * (0.114 + 0.886 * cosA - 0.203 * sinA);
    return result;
}


//----------------------------------------------------------------------------------------------//
//                        SSS Horizontal Pass (EotE_PrePass1)                                  //
//----------------------------------------------------------------------------------------------//

float4 PS_SSS_Horizontal(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpPoint, txcoord, 0).rgb;

    [branch] if(!ui_SSSEnable)
        return float4(color, 1.0);

    // Skin mask check
    float mask = TextureMask.SampleLevel(smpPoint, txcoord, 0).a;
    float skinAmount = saturate((ui_SSSMaskThresh - mask) / max(ui_SSSMaskThresh, DELTA));

    [branch] if(skinAmount < 0.01)
        return float4(color, 1.0);

    // Debug: show skin mask
    if(ui_SSSDebugMask)
        return float4(skinAmount, 0.0, 0.0, 1.0);

    // Center pixel data
    float  centerDepth  = SSS_GetLinearDepth(txcoord);
    float3 centerNormal = TextureNormal.SampleLevel(smpPoint, txcoord, 0).xyz * 2.0 - 1.0;

    // Curvature-aware radius: high curvature → larger scatter radius
    float curvature = SSS_EstimateCurvature(txcoord, 2.0);
    float radiusMod = lerp(1.0, 1.0 + curvature * 3.0, ui_SSSCurvatureScale);
    float radius = ui_SSSRadius * radiusMod;

    // Horizontal blur direction
    float2 blurDir = float2(PixelSize.x, 0.0) * radius;

    float3 accumColor  = 0.0;
    float3 accumWeight = 0.0;

    [unroll] for(int i = 0; i < SSS_KERNEL_SIZE; i++)
    {
        float2 sampleUV = txcoord + blurDir * SSS_Offsets[i];
        float3 sampleColor = TextureColor.SampleLevel(smpLinear, sampleUV, 0).rgb;

        float3 w = SSS_Weights[i];

        // Bilateral depth rejection
        float sampleDepth = SSS_GetLinearDepth(sampleUV);
        float depthDiff = abs(sampleDepth - centerDepth);
        float depthW = exp(-depthDiff * ui_SSSDepthReject);

        // Normal bilateral rejection
        float3 sampleNormal = TextureNormal.SampleLevel(smpPoint, sampleUV, 0).xyz * 2.0 - 1.0;
        float normalW = SSS_NormalBilateral(centerNormal, sampleNormal, ui_SSSNormalReject);

        // Skin mask at sample position
        float sampleMask = TextureMask.SampleLevel(smpPoint, sampleUV, 0).a;
        float sampleSkin = saturate((ui_SSSMaskThresh - sampleMask) / max(ui_SSSMaskThresh, DELTA));

        // Combined bilateral weight
        float bilateral = depthW * normalW * sampleSkin;

        accumColor  += sampleColor * w * bilateral;
        accumWeight += w * bilateral;
    }

    // Normalize per-channel (Burley profile has different weights per channel)
    float3 scattered = accumColor / max(accumWeight, DELTA);

    // Blend with original based on intensity and skin mask
    float blendAmount = ui_SSSIntensity * skinAmount;
    float3 result = lerp(color, scattered, blendAmount);

    return float4(result, 1.0);
}


//----------------------------------------------------------------------------------------------//
//                       SSS Vertical Pass + Tone Controls (EotE_PrePass2)                     //
//----------------------------------------------------------------------------------------------//

float4 PS_SSS_Vertical(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpPoint, txcoord, 0).rgb;

    [branch] if(!ui_SSSEnable)
        return float4(color, 1.0);

    // Skin mask check
    float mask = TextureMask.SampleLevel(smpPoint, txcoord, 0).a;
    float skinAmount = saturate((ui_SSSMaskThresh - mask) / max(ui_SSSMaskThresh, DELTA));

    [branch] if(skinAmount < 0.01)
        return float4(color, 1.0);

    // Debug passthrough (already shown in H pass)
    if(ui_SSSDebugMask)
        return float4(color, 1.0);

    // Store pre-blur color for specular preservation
    float3 preBlur = color;

    // Center pixel data
    float  centerDepth  = SSS_GetLinearDepth(txcoord);
    float3 centerNormal = TextureNormal.SampleLevel(smpPoint, txcoord, 0).xyz * 2.0 - 1.0;

    // Curvature-aware radius (same as H pass)
    float curvature = SSS_EstimateCurvature(txcoord, 2.0);
    float radiusMod = lerp(1.0, 1.0 + curvature * 3.0, ui_SSSCurvatureScale);
    float radius = ui_SSSRadius * radiusMod;

    // Vertical blur direction
    float2 blurDir = float2(0.0, PixelSize.y) * radius;

    float3 accumColor  = 0.0;
    float3 accumWeight = 0.0;

    [unroll] for(int i = 0; i < SSS_KERNEL_SIZE; i++)
    {
        float2 sampleUV = txcoord + blurDir * SSS_Offsets[i];
        float3 sampleColor = TextureColor.SampleLevel(smpLinear, sampleUV, 0).rgb;

        float3 w = SSS_Weights[i];

        // Bilateral depth rejection
        float sampleDepth = SSS_GetLinearDepth(sampleUV);
        float depthDiff = abs(sampleDepth - centerDepth);
        float depthW = exp(-depthDiff * ui_SSSDepthReject);

        // Normal bilateral rejection
        float3 sampleNormal = TextureNormal.SampleLevel(smpPoint, sampleUV, 0).xyz * 2.0 - 1.0;
        float normalW = SSS_NormalBilateral(centerNormal, sampleNormal, ui_SSSNormalReject);

        // Skin mask at sample position
        float sampleMask = TextureMask.SampleLevel(smpPoint, sampleUV, 0).a;
        float sampleSkin = saturate((ui_SSSMaskThresh - sampleMask) / max(ui_SSSMaskThresh, DELTA));

        // Combined bilateral weight
        float bilateral = depthW * normalW * sampleSkin;

        accumColor  += sampleColor * w * bilateral;
        accumWeight += w * bilateral;
    }

    // Normalize per-channel
    float3 scattered = accumColor / max(accumWeight, DELTA);

    // Blend with original based on intensity and skin mask
    float blendAmount = ui_SSSIntensity * skinAmount;
    float3 result = lerp(preBlur, scattered, blendAmount);


    // ---- SPECULAR PRESERVATION ---- //
    // Specular highlights are surface reflections, not scattered light.
    // Restore them proportionally to avoid smeared specular.
    if(ui_SSSPreserveSpec > 0.001)
    {
        float specAmount = SSS_EstimateSpecular(preBlur, result);
        result = lerp(result, preBlur, specAmount * ui_SSSPreserveSpec * skinAmount);
    }


    // ---- SKIN TONE CONTROLS ---- //
    // Applied only to skin-masked pixels, feathered at mask edges.
    if(skinAmount > 0.01)
    {
        float3 toned = result;

        // Hue shift (rotate in color space)
        if(abs(ui_SSSSkinHue) > 0.001)
            toned = SSS_HueShift(toned, ui_SSSSkinHue);

        // Saturation
        if(abs(ui_SSSSkinSat - 1.0) > 0.001)
        {
            float luma = dot(toned, LUM_709);
            toned = lerp(float3(luma, luma, luma), toned, ui_SSSSkinSat);
        }

        // Brightness
        toned *= ui_SSSSkinBright;

        // Feathered blend
        result = lerp(result, toned, skinAmount);
    }

    return float4(max(result, 0.0), 1.0);
}
