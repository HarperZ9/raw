//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                Advanced Sharpening Addon for ENBSeries  v1.0                                 //
//                by Zain Dana Harper                                                           //
//                                                                                              //
//  Two sharpening algorithms, runtime selectable:                                             //
//    Mode 0: RCAS (Robust Contrast Adaptive Sharpening) — AMD FSR-style                       //
//            5-tap cross, per-channel adaptive weights, negative lobe control                  //
//    Mode 1: KiSharp (Iterative Unsharp Mask) — Kitsuune-inspired                             //
//            N-step iterative, contrast-aware clamping, depth fade, edge mask                  //
//                                                                                              //
//  Single technique: EotE_Sharp                                                                //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//----------------------------------------------------------------------------------------------//
//                                 UI Parameters                                                //
//----------------------------------------------------------------------------------------------//

bool  ui_SharpEnable
<
    string UIName = "SHARP | Enable Sharpening";
> = {false};

int   ui_SharpMode
<
    string UIName = "SHARP | Mode (0=RCAS 1=KiSharp)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 1;
> = {0};

// ---- RCAS Parameters ---- //

float ui_RCASSharpness
<
    string UIName = "SHARP | RCAS Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
> = {0.5};

float ui_RCASDenoise
<
    string UIName = "SHARP | RCAS Denoise";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.0};

// ---- KiSharp Parameters ---- //

float ui_KSAmount
<
    string UIName = "SHARP | KiSharp Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
> = {0.8};

int   ui_KSSteps
<
    string UIName = "SHARP | KiSharp Steps (1-4)";
    string UIWidget = "Spinner";
    int UIMin = 1;
    int UIMax = 4;
> = {2};

float ui_KSStepSize
<
    string UIName = "SHARP | KiSharp Step Size";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 3.0;
> = {1.0};

float ui_KSLumaClamp
<
    string UIName = "SHARP | KiSharp Luma Clamp";
    string UIWidget = "Spinner";
    float UIMin = 0.05;
    float UIMax = 1.0;
> = {0.3};

float ui_KSConWeight
<
    string UIName = "SHARP | KiSharp Contrast Mask";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.5};

float ui_KSDepthFade
<
    string UIName = "SHARP | Depth Fade";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 4.0;
> = {1.0};

float ui_KSEdgeMask
<
    string UIName = "SHARP | Edge Mask Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
> = {0.5};

float ui_KSDepthBlur
<
    string UIName = "SHARP | Background Softening";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.0};

float ui_KSDepthBlurStart
<
    string UIName = "SHARP | Blur Start Depth";
    string UIWidget = "Spinner";
    float UIMin = 0.3;
    float UIMax = 1.0;
> = {0.8};

bool  ui_SharpDebug
<
    string UIName = "SHARP | Show Sharpening Delta";
> = {false};


//----------------------------------------------------------------------------------------------//
//                              Helper Functions                                                //
//----------------------------------------------------------------------------------------------//

static const float SHARP_DELTA = 1e-6;
static const float3 SHARP_LUM = float3(0.2126, 0.7152, 0.0722);

float Sharp_GetLinearDepth(float2 uv)
{
    float d = TextureDepth.SampleLevel(smpPoint, uv, 0).x;
    return d / (d * (-2999.0) + 3000.0);
}


//----------------------------------------------------------------------------------------------//
//                   RCAS — Robust Contrast Adaptive Sharpening                                 //
//----------------------------------------------------------------------------------------------//
//
//  AMD FidelityFX RCAS (simplified for ENB integration).
//  5-tap cross kernel with per-channel adaptive sharpening.
//  High contrast regions are sharpened less to avoid ringing.
//  Negative lobe is clamped by a peak parameter derived from sharpness.

float3 Sharp_RCAS(float2 UV)
{
    float3 c = TextureColor.SampleLevel(smpLinear, UV, 0).rgb;
    float3 n = TextureColor.SampleLevel(smpLinear, UV + float2(0.0, -PixelSize.y), 0).rgb;
    float3 s = TextureColor.SampleLevel(smpLinear, UV + float2(0.0,  PixelSize.y), 0).rgb;
    float3 w = TextureColor.SampleLevel(smpLinear, UV + float2(-PixelSize.x, 0.0), 0).rgb;
    float3 e = TextureColor.SampleLevel(smpLinear, UV + float2( PixelSize.x, 0.0), 0).rgb;

    // Per-channel min/max for robust edge detection
    float3 mn = min(c, min(min(n, s), min(w, e)));
    float3 mx = max(c, max(max(n, s), max(w, e)));

    // Adaptive sharpening amplitude: high contrast → less sharpening
    // amp approaches 0 when mn/mx ratio is small (high contrast)
    float3 amp = saturate(min(mn, 1.0 - mx) / max(mx, SHARP_DELTA));
    amp = sqrt(amp);

    // Optional denoise: reduce amplitude in noisy (low-signal) regions
    if(ui_RCASDenoise > 0.001)
    {
        float noise = saturate(1.0 - dot(mx - mn, SHARP_LUM) * 8.0);
        amp *= lerp(1.0, 1.0 - noise, ui_RCASDenoise);
    }

    // Negative lobe clamping: prevents ringing/halo artifacts
    // peak = -1/lerp(8,5,sharpness) → range [-0.125, -0.200]
    float peak = -rcp(lerp(8.0, 5.0, saturate(ui_RCASSharpness)));
    float3 wt = amp * peak;

    // Weighted sharpen: center + weighted neighbors
    float3 result = (wt * (n + s + w + e) + c) / (4.0 * wt + 1.0);

    return max(result, 0.0);
}


//----------------------------------------------------------------------------------------------//
//                   KiSharp — Iterative Unsharp Mask                                          //
//----------------------------------------------------------------------------------------------//
//
//  N-step iterative sharpening with increasing radius per step.
//  Each step: 4-tap cross blur → compute difference → contrast-aware clamp → accumulate.
//  Depth fade reduces sharpening in distance.
//  Edge mask suppresses sharpening at depth discontinuities.

float3 Sharp_KiSharp(float2 UV)
{
    float3 color = TextureColor.SampleLevel(smpLinear, UV, 0).rgb;
    float3 sharp = color;

    float linDepth = Sharp_GetLinearDepth(UV);

    // Depth fade: reduce sharpening in distance
    float depthFade = saturate(1.0 - pow(abs(linDepth), ui_KSDepthFade));

    // N-step iterative unsharp mask
    int steps = clamp(ui_KSSteps, 1, 4);
    float3 accumDiff = 0.0;
    float totalSteps = 0.0;

    [loop] for(int s = 1; s <= steps; s++)
    {
        float radius = (float)s * ui_KSStepSize;
        float2 off = PixelSize * radius;

        // 4-tap cross kernel
        float3 blur = 0.0;
        blur += TextureColor.SampleLevel(smpLinear, UV + float2( off.x, 0), 0).rgb;
        blur += TextureColor.SampleLevel(smpLinear, UV + float2(-off.x, 0), 0).rgb;
        blur += TextureColor.SampleLevel(smpLinear, UV + float2(0,  off.y), 0).rgb;
        blur += TextureColor.SampleLevel(smpLinear, UV + float2(0, -off.y), 0).rgb;
        blur *= 0.25;

        float3 diff = color - blur;

        // Contrast-aware clamping: limits halo intensity
        float lumaDiff = dot(abs(diff), SHARP_LUM);
        float clampFactor = ui_KSLumaClamp / (lumaDiff + ui_KSLumaClamp);

        // Contrast mask: reduce sharpening in already-contrasty areas
        float contrastMask = lerp(1.0, 1.0 - saturate(lumaDiff * 5.0), ui_KSConWeight);

        accumDiff += diff * clampFactor * contrastMask;
        totalSteps += 1.0;
    }

    sharp = color + accumDiff * ui_KSAmount * depthFade / max(totalSteps, 1.0);

    // Object edge removal: suppress sharpening at depth discontinuities
    if(ui_KSEdgeMask > 0.001)
    {
        float dL = Sharp_GetLinearDepth(UV + float2(-PixelSize.x, 0));
        float dR = Sharp_GetLinearDepth(UV + float2( PixelSize.x, 0));
        float dU = Sharp_GetLinearDepth(UV + float2(0, -PixelSize.y));
        float dD = Sharp_GetLinearDepth(UV + float2(0,  PixelSize.y));

        float depthEdge = max(abs(dL - dR), abs(dU - dD));
        float edgeMask = 1.0 - saturate(depthEdge * 100.0 * ui_KSEdgeMask);
        sharp = lerp(color, sharp, edgeMask);
    }

    // Background softening: blur distant objects
    if(ui_KSDepthBlur > 0.001 && linDepth > ui_KSDepthBlurStart)
    {
        float blurAmount = saturate((linDepth - ui_KSDepthBlurStart) / (1.0 - ui_KSDepthBlurStart + SHARP_DELTA))
                         * ui_KSDepthBlur;
        float2 blOff = PixelSize * 1.5;
        float3 bgBlur = 0.0;
        bgBlur += TextureColor.SampleLevel(smpLinear, UV + float2( blOff.x, 0), 0).rgb;
        bgBlur += TextureColor.SampleLevel(smpLinear, UV + float2(-blOff.x, 0), 0).rgb;
        bgBlur += TextureColor.SampleLevel(smpLinear, UV + float2(0,  blOff.y), 0).rgb;
        bgBlur += TextureColor.SampleLevel(smpLinear, UV + float2(0, -blOff.y), 0).rgb;
        bgBlur *= 0.25;
        sharp = lerp(sharp, bgBlur, blurAmount);
    }

    return max(sharp, 0.0);
}


//----------------------------------------------------------------------------------------------//
//                           Sharpening Pixel Shader (EotE_Sharp)                               //
//----------------------------------------------------------------------------------------------//

float4 PS_Sharpen(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpPoint, txcoord, 0).rgb;

    [branch] if(!ui_SharpEnable)
        return float4(color, 1.0);

    float3 result;

    if(ui_SharpMode == 0) // RCAS
        result = Sharp_RCAS(txcoord);
    else                  // KiSharp
        result = Sharp_KiSharp(txcoord);

    // Debug: show sharpening delta
    if(ui_SharpDebug)
        return float4(abs(result - color) * 10.0, 1.0);

    return float4(result, 1.0);
}
