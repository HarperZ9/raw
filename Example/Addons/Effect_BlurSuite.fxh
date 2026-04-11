//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                Professional Blur Suite Addon for ENBSeries  v1.0                             //
//                by Zain Dana Harper                                                           //
//                                                                                              //
//  Multi-mode blur with vignette and depth masking:                                           //
//    Mode 0: Gaussian (9/17/25 tap separable)                                                 //
//    Mode 1: Box (uniform weight separable)                                                   //
//    Mode 2: Radial/Zoom (centered, single-pass)                                              //
//    Mode 3: Tilt-Shift (Gaussian with focus band)                                            //
//                                                                                              //
//  Two passes: H blur (EotE_PostPass10) + V blur (EotE_PostPass11).                           //
//  Radial mode runs entirely in H pass; V pass is passthrough.                                //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//----------------------------------------------------------------------------------------------//
//                                 UI Parameters                                                //
//----------------------------------------------------------------------------------------------//

bool  ui_BlurEnable
<
    string UIName = "BLUR | Enable Blur Suite";
> = {false};

int   ui_BlurType
<
    string UIName = "BLUR | Blur Mode (0=Gauss 1=Box 2=Radial 3=Tilt)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 3;
> = {0};

int   ui_BlurQuality
<
    string UIName = "BLUR | Quality (0=9tap 1=17tap 2=25tap)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 2;
> = {1};

float ui_BlurAmount
<
    string UIName = "BLUR | Blur Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 8.0;
> = {1.0};

bool  ui_VigEnable
<
    string UIName = "BLUR | Enable Vignette Mask";
> = {false};

float ui_VigInner
<
    string UIName = "BLUR | Vignette Inner";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.3};

float ui_VigOuter
<
    string UIName = "BLUR | Vignette Outer";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 2.0;
> = {0.8};

float ui_VigCurve
<
    string UIName = "BLUR | Vignette Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 4.0;
> = {1.5};

bool  ui_DepthEnable
<
    string UIName = "BLUR | Enable Depth Mask";
> = {false};

float ui_DepthStart
<
    string UIName = "BLUR | Depth Start";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.0};

float ui_DepthEnd
<
    string UIName = "BLUR | Depth End";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 1.0;
> = {1.0};

bool  ui_DepthInvert
<
    string UIName = "BLUR | Invert Depth";
> = {false};

float ui_DepthCurve
<
    string UIName = "BLUR | Depth Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 4.0;
> = {1.0};

float ui_EdgeBleed
<
    string UIName = "BLUR | Edge Bleed Reduction";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
> = {0.5};

float ui_TiltCenter
<
    string UIName = "BLUR | Tilt-Shift Center";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.5};

float ui_TiltRange
<
    string UIName = "BLUR | Tilt-Shift Range";
    string UIWidget = "Spinner";
    float UIMin = 0.05;
    float UIMax = 0.5;
> = {0.15};


//----------------------------------------------------------------------------------------------//
//                            Gaussian Kernel Weights                                           //
//----------------------------------------------------------------------------------------------//

static const float GaussW9[5]   = { 0.2270270, 0.1945946, 0.1216216, 0.0540541, 0.0162162 };
static const float GaussW17[9]  = { 0.1320860, 0.1256690, 0.1064770, 0.0804680, 0.0541830,
                                     0.0324990, 0.0173280, 0.0082070, 0.0034420 };
static const float GaussW25[13] = { 0.0980890, 0.0951660, 0.0868080, 0.0743820, 0.0599120,
                                     0.0453290, 0.0322370, 0.0215620, 0.0135550, 0.0080110,
                                     0.0044480, 0.0023200, 0.0011380 };


//----------------------------------------------------------------------------------------------//
//                              Helper Functions                                                //
//----------------------------------------------------------------------------------------------//

static const float BLUR_DELTA = 1e-6;

float Blur_GetLinearDepth(float2 uv)
{
    float d = TextureDepth.SampleLevel(smpPoint, uv, 0).x;
    return d / (d * (-2999.0) + 3000.0);
}

float Blur_ComputeMask(float2 UV)
{
    float mask = 1.0;

    // Vignette mask
    if(ui_VigEnable)
    {
        float2 centered = UV * 2.0 - 1.0;
        centered.x *= ScreenSize.z; // aspect correct (w/h)
        float dist = length(centered);
        float vig = smoothstep(ui_VigInner, ui_VigOuter, dist);
        mask *= pow(abs(vig), ui_VigCurve);
    }

    // Depth mask
    if(ui_DepthEnable)
    {
        float linDepth = Blur_GetLinearDepth(UV);
        float depthMask = smoothstep(ui_DepthStart, ui_DepthEnd, linDepth);
        depthMask = pow(abs(depthMask), ui_DepthCurve);
        if(ui_DepthInvert) depthMask = 1.0 - depthMask;

        // Bilateral edge guard: suppress blur at depth edges
        if(ui_EdgeBleed > 0.001)
        {
            float dL = Blur_GetLinearDepth(UV + float2(-PixelSize.x, 0));
            float dR = Blur_GetLinearDepth(UV + float2( PixelSize.x, 0));
            float dU = Blur_GetLinearDepth(UV + float2(0, -PixelSize.y));
            float dD = Blur_GetLinearDepth(UV + float2(0,  PixelSize.y));

            float depthVariance = max(max(abs(dL - linDepth), abs(dR - linDepth)),
                                      max(abs(dU - linDepth), abs(dD - linDepth)));
            float edgeReduction = saturate(depthVariance * 50.0 * ui_EdgeBleed);
            depthMask *= (1.0 - edgeReduction);
        }

        mask *= depthMask;
    }

    // Tilt-shift mask (mode 3)
    if(ui_BlurType == 3)
    {
        float tiltMask = smoothstep(0.0, ui_TiltRange, abs(UV.y - ui_TiltCenter));
        mask *= tiltMask;
    }

    return mask;
}

float Blur_GetGaussWeight(int absIndex, int quality)
{
    if(quality == 0) return (absIndex < 5)  ? GaussW9[absIndex]  : 0.0;
    if(quality == 1) return (absIndex < 9)  ? GaussW17[absIndex] : 0.0;
    return                  (absIndex < 13) ? GaussW25[absIndex] : 0.0;
}


//----------------------------------------------------------------------------------------------//
//                        Separable Blur Core (used by H and V passes)                          //
//----------------------------------------------------------------------------------------------//

float3 Blur_Separable(float2 UV, float2 direction, float amount)
{
    int halfTaps;
    if(ui_BlurQuality == 0)      halfTaps = 4;
    else if(ui_BlurQuality == 1) halfTaps = 8;
    else                         halfTaps = 12;

    float2 step = direction * amount;

    float3 accum = 0.0;
    float  totalW = 0.0;

    if(ui_BlurType == 0 || ui_BlurType == 3) // Gaussian / Tilt-Shift
    {
        [loop] for(int i = -halfTaps; i <= halfTaps; i++)
        {
            float w = Blur_GetGaussWeight(abs(i), ui_BlurQuality);
            accum += TextureColor.SampleLevel(smpLinear, UV + step * (float)i, 0).rgb * w;
            totalW += w;
        }
    }
    else // Box
    {
        [loop] for(int j = -halfTaps; j <= halfTaps; j++)
        {
            accum += TextureColor.SampleLevel(smpLinear, UV + step * (float)j, 0).rgb;
            totalW += 1.0;
        }
    }

    return accum / max(totalW, BLUR_DELTA);
}


//----------------------------------------------------------------------------------------------//
//                          Radial/Zoom Blur (single pass)                                      //
//----------------------------------------------------------------------------------------------//

float3 Blur_Radial(float2 UV, float amount)
{
    float2 toCenter = UV - 0.5;
    float2 dir = normalize(toCenter + BLUR_DELTA);
    dir.x *= ScreenSize.z; // aspect correct

    int taps;
    if(ui_BlurQuality == 0)      taps = 9;
    else if(ui_BlurQuality == 1) taps = 17;
    else                         taps = 25;

    int halfTaps = taps / 2;
    float2 step = dir * PixelSize * amount;

    float3 accum = 0.0;
    float  totalW = 0.0;

    [loop] for(int i = -halfTaps; i <= halfTaps; i++)
    {
        // Radial: scale offset by distance from center
        float distScale = abs((float)i / max((float)halfTaps, 1.0));
        float2 offset = step * (float)i * distScale;

        accum += TextureColor.SampleLevel(smpLinear, UV + offset, 0).rgb;
        totalW += 1.0;
    }

    return accum / max(totalW, BLUR_DELTA);
}


//----------------------------------------------------------------------------------------------//
//                            Horizontal Blur Pass (EotE_Blur)                                  //
//----------------------------------------------------------------------------------------------//

float4 PS_BlurH(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpPoint, txcoord, 0).rgb;

    [branch] if(!ui_BlurEnable)
        return float4(color, 1.0);

    float mask = Blur_ComputeMask(txcoord);

    [branch] if(mask < 0.01)
        return float4(color, 1.0);

    float finalAmount = ui_BlurAmount * mask;
    float3 blurred;

    if(ui_BlurType == 2) // Radial: full blur in H pass
    {
        blurred = Blur_Radial(txcoord, finalAmount);
    }
    else // Gaussian, Box, Tilt-Shift: horizontal pass
    {
        blurred = Blur_Separable(txcoord, float2(PixelSize.x, 0.0), finalAmount);
    }

    return float4(lerp(color, blurred, mask), 1.0);
}


//----------------------------------------------------------------------------------------------//
//                            Vertical Blur Pass (EotE_Blur1)                                   //
//----------------------------------------------------------------------------------------------//

float4 PS_BlurV(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpPoint, txcoord, 0).rgb;

    [branch] if(!ui_BlurEnable)
        return float4(color, 1.0);

    // Radial mode: V pass is passthrough (all work done in H pass)
    [branch] if(ui_BlurType == 2)
        return float4(color, 1.0);

    float mask = Blur_ComputeMask(txcoord);

    [branch] if(mask < 0.01)
        return float4(color, 1.0);

    float finalAmount = ui_BlurAmount * mask;
    float3 blurred = Blur_Separable(txcoord, float2(0.0, PixelSize.y), finalAmount);

    return float4(lerp(color, blurred, mask), 1.0);
}
