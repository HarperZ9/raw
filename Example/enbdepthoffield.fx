//----------------------------------------------------------------------------------------------//
//                     ENB of the Elders - Depth of Field  v2.0                                  //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Physical cinematography DOF: N-gon aperture bokeh, cat-eye vignette,                        //
//  spherical aberration (ring bokeh), anamorphic stretch, longitudinal CA,                      //
//  focus peaking overlay.                                                                      //
//                                                                                              //
//  10-technique pipeline:                                                                      //
//    0 ReadFocus:  Weighted autofocus (10x10 grid, variance rejection)                         //
//    1 Focus:      Temporal focus smoothing                                                     //
//    2 DOF:        CoC computation → RenderTargetRGBA32                                        //
//    3 DOF1:       Near CoC 7-tap blur → RenderTargetR16F                                      //
//    4 DOF2:       Combine CoC + near bleed → RenderTargetRGBA32                               //
//    5 DOF3:       Far N-gon bokeh gather → RenderTargetRGBA64F                                //
//    6 DOF4:       Near N-gon bokeh gather → TextureColor                                      //
//    7 DOF5:       Composite + fringing + peaking → TextureColor                               //
//    8 DOF6:       Vertical Gaussian post-blur                                                  //
//    9 DOF7:       Horizontal Gaussian post-blur                                                //
//                                                                                              //
//  Based on AMON ENB / LonelyKitsuune ADOF system.                                            //
//  N-gon vertex-interpolation pattern after Kitsuune FNENB.                                    //
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
float4  DofParameters;     // z = ApertureTime*elapsed, w = FocusingTime*elapsed
float4  tempF1;
float4  tempF2;
float4  tempF3;
float4  tempInfo1;
float4  tempInfo2;


//=== TEXTURES ===//

Texture2D   TextureColor;           // Scene color (current pass input)
Texture2D   TextureOriginal;        // Original unmodified scene
Texture2D   TextureDepth;           // Depth buffer
Texture2D   TextureFocus;           // Focus texture (previous frame focus)
Texture2D   TextureCurrent;         // Current focus computation
Texture2D   TexturePrevious;        // Previous frame focus
Texture2D   TextureAperture;        // Aperture from previous frame
Texture2D   RenderTargetRGBA32;     // 32-bit RT (CoC storage)
Texture2D   RenderTargetRGBA64F;    // 64-bit RT (far bokeh)
Texture2D   RenderTargetR16F;       // 16-bit single channel (near CoC)


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
static const float  DELTA = 1e-6;
static const float  FPS_HAND_CUTOFF = 0.001;
static const float  TWO_PI = 6.28318530717959;
static const float3 K_LUM = float3(0.2126, 0.7152, 0.0722);

#include "Addons/DOF_Advanced.fxh"


//=== UI PARAMETERS ===//

// --- Focus ---

int ui_FocusMode
<
    string UIName = "DOF | Focus Mode (1=Auto 2=Mouse 3=Manual)";
    string UIWidget = "Spinner";
    int UIMin = 1;
    int UIMax = 3;
> = {1};

float2 ui_AutofocusCenter
<
    string UIName = "DOF | Autofocus Center XY";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
> = {0.5, 0.5};

float ui_AutofocusRadius
<
    string UIName = "DOF | Autofocus Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.15};

float ui_FocusSpeed
<
    string UIName = "DOF | Focus Speed Multiplier";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 30.0;
    float UIStep = 0.5;
> = {8.0};

float ui_ManualFocusDepth
<
    string UIName = "DOF | Manual Focus Depth";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.0001;
> = {0.05};

float ui_AFVarianceReject
<
    string UIName = "DOF | AF Variance Rejection";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 20.0;
    float UIStep = 0.5;
> = {4.0};

float ui_AFCenterBias
<
    string UIName = "DOF | AF Center Bias";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 8.0;
    float UIStep = 0.1;
> = {2.0};

// --- Blur ---

float ui_NearBlurCurve
<
    string UIName = "DOF | Near Blur Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 10.0;
    float UIStep = 0.01;
> = {1.0};

float ui_FarBlurCurve
<
    string UIName = "DOF | Far Blur Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 10.0;
    float UIStep = 0.01;
> = {1.4};

bool ui_RemoveFPSHands
<
    string UIName = "DOF | Remove FPS Weapon Blur";
> = {true};

// --- Bokeh Shape ---

float ui_BokehRadius
<
    string UIName = "DOF | Bokeh Max Radius (px)";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 100.0;
    float UIStep = 0.5;
> = {15.0};

int ui_BokehQuality
<
    string UIName = "DOF | Bokeh Quality (rings)";
    string UIWidget = "Spinner";
    int UIMin = 2;
    int UIMax = 12;
> = {5};

// --- Post Processing ---

float ui_SmoothAmount
<
    string UIName = "DOF | Smoothing Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 10.0;
    float UIStep = 0.1;
> = {4.0};

// --- Gather Mode ---

int ui_GatherMode
<
    string UIName = "BOKEH | Gather (0=N-gon 1=Golden)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 1;
> = {0};

int ui_GoldenSamples
<
    string UIName = "BOKEH | Golden Spiral Samples";
    string UIWidget = "Spinner";
    int UIMin = 16;
    int UIMax = 128;
> = {48};

// --- N-gon Aperture ---

int ui_ApertureBlades
<
    string UIName = "BOKEH | Aperture Blades";
    string UIWidget = "Spinner";
    int UIMin = 5;
    int UIMax = 9;
> = {6};

float ui_ApertureCurvature
<
    string UIName = "BOKEH | Blade Roundness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {1.0};

float ui_ApertureRotation
<
    string UIName = "BOKEH | Aperture Rotation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 360.0;
    float UIStep = 1.0;
> = {15.0};

float ui_HighlightBoost
<
    string UIName = "BOKEH | Highlight Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

// --- Cat-Eye (per-bokeh-sample mechanical vignetting) ---
// NOTE: ui_CatEyeEnable is declared here (not in DOF_Advanced.fxh which has a separate full-frame cat-eye)

bool ui_BokehCatEyeEnable
<
    string UIName = "BOKEH | Enable Cat-Eye";
> = {false};

float ui_BokehCatEyeAmount
<
    string UIName = "BOKEH | Cat-Eye Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

// --- Ring Bokeh ---

bool ui_RingEnable
<
    string UIName = "BOKEH | Enable Ring Bokeh";
> = {false};

float ui_RingAmount
<
    string UIName = "BOKEH | Ring Edge Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.5};

float ui_RingCurve
<
    string UIName = "BOKEH | Ring Falloff Curve";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 5.0;
    float UIStep = 0.1;
> = {2.0};

// --- Onion Ring Bokeh ---

bool ui_OnionRingEnable
<
    string UIName = "BOKEH | Enable Onion Rings";
> = {false};

float ui_OnionRingFreq
<
    string UIName = "BOKEH | Onion Ring Frequency";
    string UIWidget = "Spinner";
    float UIMin = 2.0; float UIMax = 30.0; float UIStep = 0.5;
> = {10.0};

float ui_OnionRingAmount
<
    string UIName = "BOKEH | Onion Ring Contrast";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01;
> = {0.15};

// --- Focus Breathing ---

bool ui_FocusBreathingEnable
<
    string UIName = "BOKEH | Enable Focus Breathing";
> = {false};

float ui_FocusBreathingAmount
<
    string UIName = "BOKEH | Breathing Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.05; float UIStep = 0.001;
> = {0.01};

// --- Anamorphic ---

bool ui_AnamorphicEnable
<
    string UIName = "BOKEH | Enable Anamorphic";
> = {false};

float ui_AnamorphicRatio
<
    string UIName = "BOKEH | Anamorphic Ratio";
    string UIWidget = "Spinner";
    float UIMin = 0.3;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.33};

// --- Chromatic Bokeh (per-gather longitudinal CA) ---

float ui_ChromaBokeh
<
    string UIName = "BOKEH | Chromatic Aberration";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

float ui_ChromaFocusDep
<
    string UIName = "BOKEH | CA Focus Dependence";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

// --- Bokeh Fringing ---

bool ui_FringeEnable
<
    string UIName = "FRINGE | Enable Bokeh Fringing";
> = {false};

float ui_FringeAmount
<
    string UIName = "FRINGE | Fringe Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

float ui_FringeSpread
<
    string UIName = "FRINGE | Fringe Spread";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 6.0;
    float UIStep = 0.1;
> = {2.0};

// --- Focus Peaking ---

bool ui_PeakingEnable
<
    string UIName = "PEAKING | Enable Focus Peaking";
> = {false};

float ui_PeakingThreshold
<
    string UIName = "PEAKING | Edge Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.08};

float ui_PeakingCoCMax
<
    string UIName = "PEAKING | Max CoC";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.5;
    float UIStep = 0.01;
> = {0.05};

float ui_PeakingIntensity
<
    string UIName = "PEAKING | Overlay Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.0};


//=== HELPER FUNCTIONS ===//

// Raw depth: [0, 1] range directly from depth buffer (reverse-Z hyperbolic)
float GetRawDepth(float2 uv)
{
    return TextureDepth.SampleLevel(smpPoint, uv, 0).x;
}

// Linear depth: [0, 1] linearized over full z-range (znear=1, zfar=3000)
float GetLinearDepth(float2 uv)
{
    float d = TextureDepth.SampleLevel(smpPoint, uv, 0).x;
    return d / (d * (-2999.0) + 3000.0);
}

// N-gon shape: interpolate between polygon and circle
// curvature: 0.0 = hard polygon edges, 1.0 = perfect circle
void ShapeRoundness(inout float2 offset, float roundness)
{
    offset *= (1.0 - roundness) + roundness * rsqrt(dot(offset, offset) + DELTA);
}

// Cat-eye / optical vignette: mechanical aperture clipping at field edges
// sampleOffset: normalized offset within aperture (ring-scaled)
// sensorUV: pixel position relative to optical axis ([-1,1] range, scaled by vignette amount)
void CatEyeClip(float2 sampleOffset, float2 sensorUV, inout float weight)
{
    float2 shifted = sampleOffset - sensorUV;
    weight *= saturate(3.33 - dot(shifted, shifted) * 1.666);
}


//=== VERTEX SHADERS ===//

// VS input struct — ENB vertex buffer uses POSITION semantic (not SV_POSITION)
struct VS_INPUT
{
    float3 pos     : POSITION;
    float2 txcoord : TEXCOORD0;
};

void VS_Basic(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}

// Focus readback VS — scales quad to 1/16th size for minification
struct VS_FOCUS_OUT
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

VS_FOCUS_OUT VS_ReadFocus(VS_INPUT IN)
{
    VS_FOCUS_OUT OUT;
    OUT.pos = float4(IN.pos.xy * 0.0625 + float2(-0.9375, 0.9375), 0.5, 1.0);
    OUT.texcoord = IN.txcoord;
    return OUT;
}

VS_FOCUS_OUT VS_Focus(VS_INPUT IN)
{
    VS_FOCUS_OUT OUT;
    OUT.pos = float4(IN.pos.xy, 0.5, 1.0);
    OUT.texcoord = IN.txcoord;
    return OUT;
}

// Standard DOF VS — full screen with focus data
struct VS_DOF_OUT
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float  focus    : TEXCOORD1;
};

VS_DOF_OUT VS_DoF(VS_INPUT IN)
{
    VS_DOF_OUT OUT;
    OUT.pos = float4(IN.pos.xy, 0.5, 1.0);
    OUT.texcoord = IN.txcoord;
    OUT.focus = 0;
    return OUT;
}

// Bokeh VS — precomputes N-gon aperture vertices (up to 10 for 9 blades + wrap)
struct VS_BOKEH_OUT
{
    float4 pos                              : SV_POSITION;
    float2 texcoord                         : TEXCOORD0;
    float  focus                            : TEXCOORD1;
    nointerpolation float2 verts[10]        : TEXCOORD2;
};

VS_BOKEH_OUT VS_Bokeh(VS_INPUT IN)
{
    VS_BOKEH_OUT OUT;
    OUT.pos = float4(IN.pos.xy, 0.5, 1.0);
    OUT.texcoord = IN.txcoord;
    OUT.focus = 0;

    float rotRad = ui_ApertureRotation * 0.01745329;
    float blades = (float)ui_ApertureBlades;

    [unroll(10)]
    for (int i = 0; i < 10; i++)
    {
        float angle = (float)i * TWO_PI / blades + rotRad;
        sincos(angle, OUT.verts[i].y, OUT.verts[i].x);
    }

    return OUT;
}

// Helper: read focus in pixel shader (reliable — PS always has texture bindings)
float GetFocusDistance()
{
    return TextureFocus.Load(int3(0, 0, 0)).x;
}

// Chromatic bokeh gather weight: per-channel Gaussian modulation based on radial position
// Red focuses slightly beyond sensor → larger disc (weight peaks at outer ring)
// Blue focuses slightly before sensor → smaller disc (weight peaks at inner ring)
// Green is neutral. Zero extra texture fetches — just reweights existing samples.
float3 ChromaticBokehWeight(float normalizedRadius, float strength)
{
    float r = normalizedRadius; // [0..1] within aperture disc
    float s = strength;

    // Per-channel radial bias: red = wider disc (weight grows with r), blue = tighter
    // Gaussian: exp(-(r - center)^2 / (2 * sigma^2))
    // Red center shifted outward, blue shifted inward
    float rR = exp(-((r - 0.15 * s) * (r - 0.15 * s)) * 2.0);
    float rG = exp(-(r * r) * 2.0);
    float rB = exp(-((r + 0.15 * s) * (r + 0.15 * s)) * 2.0);

    // Normalize so green channel is unaffected at zero strength
    return lerp(1.0, float3(rR, rG, rB) / max(rG, 0.001), s);
}

// Focus-dependent CA: real lenses exhibit more longitudinal CA when defocused.
// coc: normalized circle of confusion [0..1], maxCoc: maximum expected CoC.
// At ui_ChromaFocusDep=0: constant CA (current behavior).
// At ui_ChromaFocusDep=1: CA scales with defocus (physically correct).
float FocusDependentCA(float coc, float maxCoc)
{
    float cocNorm = saturate(coc / max(maxCoc, 0.01));
    return lerp(ui_ChromaBokeh, ui_ChromaBokeh * cocNorm, ui_ChromaFocusDep);
}

// Golden spiral sampling: r = sqrt(i/N), theta = i * golden_angle
// Produces excellent disc coverage for any sample count.
static const float GOLDEN_ANGLE = 2.39996323;  // PI * (3 - sqrt(5))

float2 GoldenSpiralSample(int sampleIndex, int totalSamples)
{
    float r = sqrt((float)sampleIndex / (float)totalSamples);
    float theta = (float)sampleIndex * GOLDEN_ANGLE;
    float2 sc;
    sincos(theta, sc.x, sc.y);
    return float2(sc.y, sc.x) * r;
}


//=== PIXEL SHADERS ===//

// Pass 0: Read focus — weighted depth sampling with variance rejection
float4 PS_ReadFocus(VS_FOCUS_OUT IN) : SV_Target
{
    if (ui_FocusMode == 3) return ui_ManualFocusDepth;

    float2 center = (ui_FocusMode == 1) ? ui_AutofocusCenter : tempInfo2.zw;
    float radius = ui_AutofocusRadius;

    // First pass: gather depths and compute running statistics
    float focusSum = 0.0;
    float weightSum = DELTA;
    float depthMean = 0.0;
    float depthCount = 0.0;
    float depths[100];

    [loop]
    for (int ix = 0; ix < 10; ix++)
    {
        [loop]
        for (int iy = 0; iy < 10; iy++)
        {
            float2 offset = (float2(ix, iy) + 0.5) * 0.2 - 1.0;
            offset *= radius;
            float2 sampleUV = center + offset;

            float d = GetLinearDepth(sampleUV);
            int idx = ix * 10 + iy;
            depths[idx] = d;

            // FPS hand rejection
            if (ui_RemoveFPSHands && d < FPS_HAND_CUTOFF)
            {
                depths[idx] = -1.0;
                continue;
            }

            depthMean += d;
            depthCount += 1.0;
        }
    }

    depthMean = (depthCount > 0.5) ? depthMean / depthCount : 0.0;

    // Second pass: weighted accumulation with variance rejection
    [loop]
    for (int jx = 0; jx < 10; jx++)
    {
        [loop]
        for (int jy = 0; jy < 10; jy++)
        {
            int jdx = jx * 10 + jy;
            float d = depths[jdx];
            if (d < 0.0) continue;

            float2 offset = (float2(jx, jy) + 0.5) * 0.2 - 1.0;
            offset *= radius;

            // Gaussian center bias (configurable tightness)
            float w = saturate(1.2 * exp2(dot(offset, offset) * -4.0 * ui_AFCenterBias));

            // Inverse depth weighting — bias toward nearer objects
            w /= (d + DELTA);

            // Variance rejection: suppress samples at mixed-depth boundaries
            // Compare to neighbors (approximate local variance)
            [branch] if (ui_AFVarianceReject > 0.01)
            {
                float localVar = 0.0;
                float varCount = 0.0;

                // 4-neighbor variance estimate
                int nl = max(jx - 1, 0) * 10 + jy;
                int nr = min(jx + 1, 9) * 10 + jy;
                int nu = jx * 10 + max(jy - 1, 0);
                int nd = jx * 10 + min(jy + 1, 9);

                if (depths[nl] > 0.0) { float dd = depths[nl] - d; localVar += dd * dd; varCount += 1.0; }
                if (depths[nr] > 0.0) { float dd = depths[nr] - d; localVar += dd * dd; varCount += 1.0; }
                if (depths[nu] > 0.0) { float dd = depths[nu] - d; localVar += dd * dd; varCount += 1.0; }
                if (depths[nd] > 0.0) { float dd = depths[nd] - d; localVar += dd * dd; varCount += 1.0; }

                localVar = (varCount > 0.5) ? localVar / varCount : 0.0;
                w *= exp(-localVar * ui_AFVarianceReject * 10000.0);
            }

            focusSum += d * w;
            weightSum += w;
        }
    }

    float focus = focusSum / weightSum;
    return (weightSum > DELTA * 2.0) ? focus : -1.0;
}

// Pass 1: Temporal focus smoothing
float4 PS_Focus(VS_FOCUS_OUT IN) : SV_Target
{
    float prevFocus = TexturePrevious.Load(int3(0, 0, 0)).x;
    float currFocus = TextureCurrent.Load(int3(0, 0, 0)).x;
    float speed = saturate(DofParameters.w * ui_FocusSpeed);

    // Freeze when all samples blocked
    speed *= (currFocus > -DELTA);

    return (ui_FocusMode == 3) ? currFocus : lerp(prevFocus, currFocus, speed);
}

// Pass 2: CoC computation → RenderTargetRGBA32
float4 PS_DrawCoC(VS_DOF_OUT IN) : SV_Target
{
    float focus = GetFocusDistance();
    float depth = GetLinearDepth(IN.texcoord);

    float depthDiff = depth - focus;
    float2 coc = 0.0;

    if (depthDiff > 0.0)
    {
        float raw = saturate(depthDiff * ui_FarBlurCurve / max(focus, DELTA));
        coc.x = smoothstep(0.0, 1.0, raw);
    }
    else
    {
        float raw = saturate(-depthDiff * ui_NearBlurCurve / max(focus, DELTA));
        coc.y = smoothstep(0.0, 1.0, raw);
    }

    if (ui_RemoveFPSHands && depth < FPS_HAND_CUTOFF)
        coc = 0.0;

    float4 sep = float4(coc.x, coc.y, coc.x, coc.y);
    sep.zw = sep.zw * sep.zw * (3.0 - 2.0 * sep.zw);

    return sep;
}

// Pass 3: Near CoC downsample + blur → R16F
float4 PS_NearCoCBlur(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float nearCoC = 0.0;
    float totalWeight = 0.0;

    [unroll]
    for (int i = -3; i <= 3; i++)
    {
        float2 offset = float2(PixelSize.x * float(i) * 2.0, 0.0);
        float sample0 = RenderTargetRGBA32.SampleLevel(smpLinear, txcoord + offset, 0).y;
        float w = exp(-abs(float(i)) * 0.5);
        nearCoC += sample0 * w;
        totalWeight += w;
    }

    return nearCoC / totalWeight;
}

// Pass 4: Combine CoC with near bleed → overwrite RenderTargetRGBA32
float4 PS_CombineCoC(VS_DOF_OUT IN) : SV_Target
{
    float2 txcoord = IN.texcoord;

    float nearBleed = RenderTargetR16F.SampleLevel(smpLinear, txcoord, 0).x;

    float focus = GetFocusDistance();
    float depth = GetLinearDepth(txcoord);

    float depthDiff = depth - focus;
    float2 coc = 0.0;

    if (depthDiff > 0.0)
        coc.x = smoothstep(0.0, 1.0, saturate(depthDiff * ui_FarBlurCurve / max(focus, DELTA)));
    else
        coc.y = smoothstep(0.0, 1.0, saturate(-depthDiff * ui_NearBlurCurve / max(focus, DELTA)));

    if (ui_RemoveFPSHands && depth < FPS_HAND_CUTOFF)
        coc = 0.0;

    float blurNear = max(nearBleed, coc.y);
    return float4(coc.x, coc.y, blurNear, blurNear * blurNear * (3.0 - 2.0 * blurNear));
}


//=== BOKEH GATHER ===//

// Pass 5: Far N-gon bokeh gather → RenderTargetRGBA64F
float4 PS_FarBokeh(VS_BOKEH_OUT IN) : SV_Target
{
    float3 centerColor = TextureOriginal.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float4 cocData = RenderTargetRGBA32.SampleLevel(smpPoint, IN.texcoord, 0);
    float coc = cocData.x; // far CoC

    float radiusPx = coc * ui_BokehRadius;
    if (radiusPx < 0.5) return float4(centerColor, coc);

    // Focus-dependent CA: aberration scales with defocus amount
    float chromaStr = FocusDependentCA(coc, 1.0);

    // Bokeh radius in UV space, with anamorphic stretch
    float2 bokehRadius = float2(radiusPx * PixelSize.x, radiusPx * PixelSize.y);
    [branch] if (ui_AnamorphicEnable)
        bokehRadius.x *= ui_AnamorphicRatio;

    // Adaptive ring count
    float nRings = lerp(2.0, (float)ui_BokehQuality, saturate(coc));
    int bladeCount = ui_ApertureBlades;

    // Cat-eye precomputation
    float2 sensorPos = IN.texcoord * 2.0 - 1.0;
    float  sensorDist = length(sensorPos);
    float2 catEyeVec = float2(0, 0);
    bool   catEyeActive = ui_BokehCatEyeEnable && ui_BokehCatEyeAmount > 0.001;
    [branch] if (catEyeActive)
    {
        float vignette = pow(sensorDist, 1.5) * ui_BokehCatEyeAmount;
        catEyeVec = (sensorDist > DELTA) ? (sensorPos / sensorDist) * vignette : 0.0;
    }

    // Accumulator — center sample
    float3 BokehSum = centerColor;
    float3 BokehMax = centerColor;
    float  wSum = 1.0;

    if (ui_GatherMode == 1)
    {
        // --- Golden Spiral Gather ---
        // Produces excellent disc coverage at any sample count.
        // Supports ring bokeh, cat-eye, chromatic bokeh, leak prevention.
        int totalSamples = ui_GoldenSamples;

        [loop]
        for (int si = 1; si < totalSamples; si++)
        {
            float2 offset = GoldenSpiralSample(si, totalSamples);
            float  ringT = length(offset); // [0..1] normalized radius

            // Anamorphic
            float2 sampleOffset = offset * bokehRadius;

            // Ring bokeh (spherical aberration)
            float sw = 1.0;
            if (ui_RingEnable)
                sw = lerp(1.0, pow(ringT, ui_RingCurve), ui_RingAmount);

            // Onion ring: concentric sinusoidal modulation from aspherical element moulding
            if (ui_OnionRingEnable)
                sw *= 1.0 + ui_OnionRingAmount * sin(ringT * ui_OnionRingFreq * 6.283);

            // Cat-eye clipping
            if (catEyeActive)
            {
                CatEyeClip(offset * ringT, catEyeVec, sw);
                if (sw < 0.001) continue;
            }

            float2 sUV = IN.texcoord + sampleOffset;
            float3 sCol = TextureOriginal.SampleLevel(smpLinear, sUV, 0).rgb;

            // Leak prevention: only include if sample is also out-of-focus far
            float sCoc = RenderTargetRGBA32.SampleLevel(smpPoint, sUV, 0).x;
            sw *= saturate(sCoc * 10.0);

            // Karis anti-firefly
            sw *= 1.0 / (max(max(sCol.r, sCol.g), sCol.b) + 1.0);

            // Chromatic bokeh
            float3 chromaW = ChromaticBokehWeight(ringT, chromaStr);

            BokehSum += sCol * sw * chromaW;
            BokehMax = max(BokehMax, sCol * sw);
            wSum += sw;
        }
    }
    else
    {
        // --- N-gon Gather ---
        // Per-ring radius step
        float2 ringStep = bokehRadius / nRings;

        // Triple-nested N-gon gather: rings → samples per ring → blade edges
        [loop]
        for (float ring = 1.0; ring <= nRings; ring += 1.0)
        {
            float2 RingScale = ringStep * ring;
            float  ringT = ring / nRings; // [0..1] normalized radius
            float  RingSampleWeight = 1.0;

            // Ring bokeh (spherical aberration): outer rings weighted more
            [branch] if (ui_RingEnable)
                RingSampleWeight = lerp(1.0, pow(ringT, ui_RingCurve), ui_RingAmount);

            // Onion ring: concentric sinusoidal modulation from aspherical element moulding
            if (ui_OnionRingEnable)
                RingSampleWeight *= 1.0 + ui_OnionRingAmount * sin(ringT * ui_OnionRingFreq * 6.283);

            [loop]
            for (float spr = 0.0; spr < ring; spr += 1.0)
            {
                float t = spr / ring;

                [loop]
                for (int blade = 0; blade < bladeCount; blade++)
                {
                    // Vertex interpolation: lerp between adjacent N-gon vertices
                    float2 offset = lerp(IN.verts[blade], IN.verts[blade + 1], t);

                    // Apply curvature: 0 = polygon, 1 = circle
                    ShapeRoundness(offset, ui_ApertureCurvature);

                    // Scale to ring radius
                    float2 sampleOffset = offset * RingScale;
                    float  sw = RingSampleWeight;

                    // Cat-eye clipping
                    [branch] if (catEyeActive)
                    {
                        CatEyeClip(offset * ringT, catEyeVec, sw);
                        if (sw < 0.001) continue;
                    }

                    // Sample scene
                    float2 sUV = IN.texcoord + sampleOffset;
                    float3 sCol = TextureOriginal.SampleLevel(smpLinear, sUV, 0).rgb;

                    // Leak prevention: only include if sample is also out-of-focus far
                    float sCoc = RenderTargetRGBA32.SampleLevel(smpPoint, sUV, 0).x;
                    sw *= saturate(sCoc * 10.0);

                    // Karis anti-firefly tone weight
                    sw *= 1.0 / (max(max(sCol.r, sCol.g), sCol.b) + 1.0);

                    // Chromatic bokeh: per-channel radial weighting (longitudinal CA in gather)
                    float3 chromaW = ChromaticBokehWeight(ringT, chromaStr);

                    BokehSum += sCol * sw * chromaW;
                    BokehMax = max(BokehMax, sCol * sw);
                    wSum += sw;
                }
            }
        }
    }

    BokehSum /= wSum;
    BokehMax = max(BokehMax, BokehSum);

    // Highlight boost (Kitsuune BokehMax pattern)
    float3 result = BokehSum;
    [branch] if (ui_HighlightBoost > 0.001)
    {
        float intensity = saturate(ui_HighlightBoost * pow(dot(BokehMax, K_LUM), 2.0));
        intensity *= saturate(coc * 4.0);
        result = lerp(BokehSum, BokehMax, intensity);
    }

    return float4(result, coc);
}

// Pass 6: Near bokeh gather → TextureColor
float4 PS_NearBokeh(VS_BOKEH_OUT IN) : SV_Target
{
    float3 centerColor = TextureOriginal.SampleLevel(smpLinear, IN.texcoord, 0).rgb;
    float4 cocData = RenderTargetRGBA32.SampleLevel(smpPoint, IN.texcoord, 0);
    float coc = cocData.z; // near bleed CoC

    float radiusPx = coc * ui_BokehRadius;
    if (radiusPx < 0.5) return float4(centerColor, coc);

    // Focus-dependent CA for near field
    float chromaStr = FocusDependentCA(coc, 1.0);

    float2 bokehRadius = float2(radiusPx * PixelSize.x, radiusPx * PixelSize.y);
    [branch] if (ui_AnamorphicEnable)
        bokehRadius.x *= ui_AnamorphicRatio;

    float nRings = lerp(2.0, (float)ui_BokehQuality, saturate(coc));
    int bladeCount = ui_ApertureBlades;

    // Cat-eye
    float2 sensorPos = IN.texcoord * 2.0 - 1.0;
    float  sensorDist = length(sensorPos);
    float2 catEyeVec = float2(0, 0);
    bool   catEyeActive = ui_BokehCatEyeEnable && ui_BokehCatEyeAmount > 0.001;
    [branch] if (catEyeActive)
    {
        float vignette = pow(sensorDist, 1.5) * ui_BokehCatEyeAmount;
        catEyeVec = (sensorDist > DELTA) ? (sensorPos / sensorDist) * vignette : 0.0;
    }

    float3 BokehSum = centerColor;
    float3 BokehMax = centerColor;
    float  wSum = 1.0;

    if (ui_GatherMode == 1)
    {
        // --- Golden Spiral Gather (near) ---
        int totalSamples = ui_GoldenSamples;

        [loop]
        for (int si = 1; si < totalSamples; si++)
        {
            float2 offset = GoldenSpiralSample(si, totalSamples);
            float  ringT = length(offset);

            float2 sampleOffset = offset * bokehRadius;

            float sw = 1.0;
            if (ui_RingEnable)
                sw = lerp(1.0, pow(ringT, ui_RingCurve), ui_RingAmount);

            // Onion ring: concentric sinusoidal modulation
            if (ui_OnionRingEnable)
                sw *= 1.0 + ui_OnionRingAmount * sin(ringT * ui_OnionRingFreq * 6.283);

            if (catEyeActive)
            {
                CatEyeClip(offset * ringT, catEyeVec, sw);
                if (sw < 0.001) continue;
            }

            float2 sUV = IN.texcoord + sampleOffset;
            float3 sCol = TextureOriginal.SampleLevel(smpLinear, sUV, 0).rgb;

            // Near field: no leak prevention
            sw *= 1.0 / (max(max(sCol.r, sCol.g), sCol.b) + 1.0);

            float3 chromaW = ChromaticBokehWeight(ringT, chromaStr);

            BokehSum += sCol * sw * chromaW;
            BokehMax = max(BokehMax, sCol * sw);
            wSum += sw;
        }
    }
    else
    {
        // --- N-gon Gather (near) ---
        float2 ringStep = bokehRadius / nRings;

        [loop]
        for (float ring = 1.0; ring <= nRings; ring += 1.0)
        {
            float2 RingScale = ringStep * ring;
            float  ringT = ring / nRings;
            float  RingSampleWeight = 1.0;

            [branch] if (ui_RingEnable)
                RingSampleWeight = lerp(1.0, pow(ringT, ui_RingCurve), ui_RingAmount);

            // Onion ring: concentric sinusoidal modulation
            if (ui_OnionRingEnable)
                RingSampleWeight *= 1.0 + ui_OnionRingAmount * sin(ringT * ui_OnionRingFreq * 6.283);

            [loop]
            for (float spr = 0.0; spr < ring; spr += 1.0)
            {
                float t = spr / ring;

                [loop]
                for (int blade = 0; blade < bladeCount; blade++)
                {
                    float2 offset = lerp(IN.verts[blade], IN.verts[blade + 1], t);
                    ShapeRoundness(offset, ui_ApertureCurvature);

                    float2 sampleOffset = offset * RingScale;
                    float  sw = RingSampleWeight;

                    [branch] if (catEyeActive)
                    {
                        CatEyeClip(offset * ringT, catEyeVec, sw);
                        if (sw < 0.001) continue;
                    }

                    float2 sUV = IN.texcoord + sampleOffset;
                    float3 sCol = TextureOriginal.SampleLevel(smpLinear, sUV, 0).rgb;

                    // Near field: no leak prevention (all near samples contribute)
                    sw *= 1.0 / (max(max(sCol.r, sCol.g), sCol.b) + 1.0);

                    // Chromatic bokeh: per-channel radial weighting (longitudinal CA in gather)
                    float3 chromaW = ChromaticBokehWeight(ringT, chromaStr);

                    BokehSum += sCol * sw * chromaW;
                    BokehMax = max(BokehMax, sCol * sw);
                    wSum += sw;
                }
            }
        }
    }

    BokehSum /= wSum;
    BokehMax = max(BokehMax, BokehSum);

    float3 result = BokehSum;
    [branch] if (ui_HighlightBoost > 0.001)
    {
        float intensity = saturate(ui_HighlightBoost * pow(dot(BokehMax, K_LUM), 2.0));
        intensity *= saturate(coc * 4.0);
        result = lerp(BokehSum, BokehMax, intensity);
    }

    return float4(result, coc);
}


//=== COMPOSITE ===//

// Pass 7: Final composite + longitudinal CA + focus peaking
float4 PS_Combine(VS_DOF_OUT IN) : SV_Target
{
    // Focus breathing: UV scale shift simulating real lens element movement.
    // Closer focus = slight zoom in (shorter effective focal length), matching
    // physical lens behavior where focusing changes magnification.
    // Ref: Canon EF focus breathing specs
    float2 combineUV = IN.texcoord;
    if (ui_FocusBreathingEnable && ui_FocusBreathingAmount > 0.0001)
    {
        // Read current focus plane from aperture texture (1x1)
        float focusPlane = TextureAperture.SampleLevel(smpPoint, float2(0.5, 0.5), 0).x;
        float nominalFocus = 0.5; // nominal mid-range focus
        float breathScale = 1.0 + ui_FocusBreathingAmount * (focusPlane - nominalFocus);
        combineUV = (combineUV - 0.5) * breathScale + 0.5;
    }

    float3 original = TextureOriginal.SampleLevel(smpPoint, combineUV, 0).rgb;
    float4 farBokeh = RenderTargetRGBA64F.SampleLevel(smpLinear, combineUV, 0);
    float3 nearBokeh = TextureColor.SampleLevel(smpLinear, combineUV, 0).rgb;
    float4 cocData = RenderTargetRGBA32.SampleLevel(smpPoint, combineUV, 0);

    float farCoC = cocData.x;
    float nearCoC = cocData.w; // smoothstepped near bleed

    // Far blend
    float farBlend = smoothstep(0.02, 0.5, farCoC);

    // Near blend
    float nearBlend = smoothstep(0.02, 0.5, nearCoC);

    // Layered composite: original → far → near
    float3 result = lerp(original, farBokeh.rgb, farBlend);
    result = lerp(result, nearBokeh, nearBlend);

    // --- Longitudinal CA / Bokeh Fringing ---
    [branch] if (ui_FringeEnable && ui_FringeAmount > 0.001)
    {
        float totalCoC = max(farCoC, nearCoC);
        float fringeStr = ui_FringeAmount * saturate(totalCoC * 20.0);

        // Radial direction from screen center
        float2 radialDelta = IN.texcoord - 0.5;
        float  radialDist = length(radialDelta);
        float2 radialDir = (radialDist > DELTA) ? radialDelta / radialDist : float2(0, 1);

        // Offset magnitude scales with CoC and pixel size
        float2 fringeOff = radialDir * PixelSize * ui_FringeSpread * totalCoC * ui_BokehRadius;

        // Direction depends on near vs far (purple foreground, green background)
        float cocSign = (farCoC > nearCoC) ? 1.0 : -1.0;
        fringeOff *= cocSign;

        // Per-channel radial offset on the composite result
        float3 fringeColor;
        fringeColor.r = TextureColor.SampleLevel(smpLinear, IN.texcoord + fringeOff, 0).r;
        fringeColor.g = result.g; // green stays centered
        fringeColor.b = TextureColor.SampleLevel(smpLinear, IN.texcoord - fringeOff, 0).b;

        // Blend fringing into composite based on blur strength
        float frBlend = max(farBlend, nearBlend);
        result = lerp(result, fringeColor, fringeStr * frBlend);
    }

    // --- Focus Peaking ---
    [branch] if (ui_PeakingEnable)
    {
        float totalCoC = max(farCoC, nearCoC);
        [branch] if (totalCoC < ui_PeakingCoCMax)
        {
            // Sobel depth-edge detection (4-neighbor)
            float dC = GetLinearDepth(IN.texcoord);
            float dL = GetLinearDepth(IN.texcoord + float2(-PixelSize.x, 0));
            float dR = GetLinearDepth(IN.texcoord + float2( PixelSize.x, 0));
            float dU = GetLinearDepth(IN.texcoord + float2(0, -PixelSize.y));
            float dD = GetLinearDepth(IN.texcoord + float2(0,  PixelSize.y));

            float edge = abs(dL - dR) + abs(dU - dD);
            edge = saturate((edge - ui_PeakingThreshold) * 50.0);

            float peakMask = edge * ui_PeakingIntensity;
            peakMask *= 1.0 - smoothstep(0.0, ui_PeakingCoCMax, totalCoC);

            result = lerp(result, float3(1.0, 0.2, 0.2), peakMask);
        }
    }

    // Alpha encodes blur amount for post-blur (wide transition avoids hard edges)
    float alpha = smoothstep(0.0, 1.0, max(nearCoC, farCoC));

    return float4(result, alpha);
}


//=== POST-BLUR ===//

// Pass 8-9: Bilateral post-blur (V then H) — CoC-weighted to prevent sharp edges
float4 PS_GaussBlur(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0,
                    uniform float2 blurDir) : SV_Target
{
    float4 center = TextureColor.SampleLevel(smpPoint, txcoord, 0);

    if (ui_SmoothAmount < 0.01) return center;

    float cocAlpha = center.a;
    float blurSize = ui_SmoothAmount * cocAlpha;

    float3 result = center.rgb;
    float totalWeight = 1.0;

    [unroll]
    for (int i = 1; i <= 4; i++)
    {
        float2 offset = blurDir * PixelSize * float(i) * blurSize;
        float gaussW = exp(-float(i * i) * 0.5);

        float4 s1 = TextureColor.SampleLevel(smpLinear, txcoord + offset, 0);
        float4 s2 = TextureColor.SampleLevel(smpLinear, txcoord - offset, 0);

        // Bilateral CoC rejection: don't blend sharp pixels into blurred areas
        float cocW1 = saturate(1.0 - abs(s1.a - cocAlpha) * 4.0);
        float cocW2 = saturate(1.0 - abs(s2.a - cocAlpha) * 4.0);

        float w1 = gaussW * cocW1;
        float w2 = gaussW * cocW2;

        result += s1.rgb * w1 + s2.rgb * w2;
        totalWeight += w1 + w2;
    }

    return float4(result / totalWeight, cocAlpha);
}


//=== TECHNIQUES ===//
// ENB requires these EXACT technique names for DOF:
//   ReadFocus (→ TextureCurrent), Focus (→ TextureFocus), then DOF* for processing
// UIName must be on the DOF technique (3rd), NOT on ReadFocus or Focus

// Technique 1: Focus readback → TextureCurrent (1x1 R32F)
technique11 ReadFocus
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_ReadFocus()));
        SetPixelShader(CompileShader(ps_5_0, PS_ReadFocus()));
    }
}

// Technique 2: Temporal focus smoothing → TextureFocus (1x1 R32F)
technique11 Focus
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Focus()));
        SetPixelShader(CompileShader(ps_5_0, PS_Focus()));
    }
}

// Technique 3: CoC computation → RenderTargetRGBA32
technique11 DOF <string UIName = "EotE: Depth of Field";  string RenderTarget = "RenderTargetRGBA32";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_DoF()));
        SetPixelShader(CompileShader(ps_5_0, PS_DrawCoC()));
    }
}

// Technique 4: Near CoC blur → RenderTargetR16F
technique11 DOF1 <string RenderTarget = "RenderTargetR16F";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_NearCoCBlur()));
    }
}

// Technique 5: Combine CoC → RenderTargetRGBA32
technique11 DOF2 <string RenderTarget = "RenderTargetRGBA32";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_DoF()));
        SetPixelShader(CompileShader(ps_5_0, PS_CombineCoC()));
    }
}

// Technique 6: Far N-gon bokeh → RenderTargetRGBA64F
technique11 DOF3 <string RenderTarget = "RenderTargetRGBA64F";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Bokeh()));
        SetPixelShader(CompileShader(ps_5_0, PS_FarBokeh()));
    }
}

// Technique 7: Near N-gon bokeh (writes to default TextureColor)
technique11 DOF4
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Bokeh()));
        SetPixelShader(CompileShader(ps_5_0, PS_NearBokeh()));
    }
}

// Technique 8: Composite + fringing + peaking
technique11 DOF5
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_DoF()));
        SetPixelShader(CompileShader(ps_5_0, PS_Combine()));
    }
}

// Technique 9: Vertical Gaussian post-blur
technique11 DOF6
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_GaussBlur(float2(0.0, 1.0))));
    }
}

// Technique 10: Horizontal Gaussian post-blur
technique11 DOF7
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_GaussBlur(float2(1.0, 0.0))));
    }
}

// Technique 11: Tilt-Shift (Scheimpflug plane with rotatable axis)
technique11 DOF8
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_TiltShift()));
    }
}

// Technique 12: Cat's Eye Vignetting (optical entrance/exit pupil model)
technique11 DOF9
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_CatEye()));
    }
}
