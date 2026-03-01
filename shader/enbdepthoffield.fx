//----------------------------------------------------------------------------------------------//
//                                                                                              //
//               enbdepthoffield.fx - Advanced Depth of Field                                   //
//                   for Skyrim SE ENB (DirectX 11 Shader Model 5)                              //
//                                                                                              //
//         Original framework by LonelyKitsuune / Marty McFly - CC BY-NC-ND 4.0               //
//         Ground-up rewrite by Zain Dana Harper - Feb 2026                                     //
//                                                                                              //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  v3.0.0 - Native SkyrimBridge integration, 36-sample golden-angle bokeh                      //
//                                                                                              //
//  Architecture: 10 passes                                                                     //
//    Tech 0 (ReadFocus): Downsample focus target (16x16 readback)                              //
//    Tech 1 (Focus): Temporal focus transition + freeze logic                                   //
//    Tech 2 (DrawCoC): Circle of Confusion computation                                         //
//    Tech 3: Downsample near CoC to half-res                                                   //
//    Tech 4: Upsample + combine near/far CoC                                                   //
//    Tech 5: Far bokeh (36-sample golden-angle spiral)                                         //
//    Tech 6: Near bokeh (36-sample golden-angle spiral)                                        //
//    Tech 7: Combine near+far + color grading                                                  //
//    Tech 8: Gaussian blur vertical (12 iterations max)                                        //
//    Tech 9: Gaussian blur horizontal + chromatic aberration                                   //
//                                                                                              //
//  Bokeh: 36-sample golden-angle spiral (was 90+ disc-based)                                   //
//  Gaussian: 12 iterations max (was uncapped)                                                  //
//  Default render: half-resolution (was full)                                                   //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=============================================================================//
//                              OPTIONS                                        //
//=============================================================================//

#define ENABLE_OPTICAL_VIGNETTE        1
#define ENABLE_CHROMATIC_ABERRATION    1
#define ENABLE_BOKEH_FRINGING          1
#define ENABLE_CATS_EYE                1
#define ENABLE_HIGHLIGHT_BLOOM         1
#define ENABLE_GRAINING                2
#define ENABLE_FOCUSING_TOOL           1


//=============================================================================//
//                       ENB EXTERNAL PARAMETERS                               //
//=============================================================================//

float4 Timer;
float4 ScreenSize;
float  AdaptiveQuality;
float4 TimeOfDay1;
float4 TimeOfDay2;
float  ENightDayFactor;
float  EInteriorFactor;
float  FieldOfView;
float4 Weather;
float4 DofParameters;

float4 tempF1;
float4 tempF2;
float4 tempF3;
float4 tempInfo1;
float4 tempInfo2;


//=============================================================================//
//                    SkyrimBridge External Data                                //
//=============================================================================//
// Inline params only — full header (102 float4s) overflows constant buffer
#define SKYRIMBRIDGE_FXH 1
float4 SB_Render_Frame;       // .x = frameCount
float4 SB_Camera_Info;        // .y = near, .z = far
float4 SB_FX_Vision;          // .x = nightEye
float4 SB_Interior_Flags;     // .x = isInterior
float4 SB_Player_Combat;      // .x = combatIntensity
float4 SB_Player_Water;       // .x = isUnderwater
float4 SB_UI_Menus;           // .x = isInMenu, .y = isInDialogue
float4 SB_XHair_Info;         // .x = hasTarget, .y = distance
bool SB_IsActive() { return SB_Render_Frame.x > 0.0; }
float SB_LinearizeDepth(float rawDepth) {
    float n = SB_Camera_Info.y;
    float f = SB_Camera_Info.z;
    return n * f / (f - rawDepth * (f - n));
}


//=============================================================================//
//                       INLINE UI PARAMETERS                                  //
//=============================================================================//

// =================== Focus ===================
int _spc00 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrFOC < string UIName = "=== FOCUS CONTROL ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

int   UI_FocusType       < string UIName = "Focus | Mode (0=auto,1=mouse,2=manual,3=crosshair)"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 3; > = 0;
float UI_AutofocusRadius < string UIName = "Focus | Auto Radius";     string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 0.3;  float UIStep = 0.01; > = 0.05;
float UI_MousefocusRadius< string UIName = "Focus | Mouse Radius";    string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 0.3;  float UIStep = 0.01; > = 0.05;
float UI_ManualfocusDepth< string UIName = "Focus | Manual Distance"; string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.001;> = 0.05;
float UI_FocusTransSpeed < string UIName = "Focus | Transition Speed"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 1.0;  float UIStep = 0.01; > = 0.15;

// =================== Blur ===================
int _spc10 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrBLR < string UIName = "=== DOF BLUR ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

float UI_FarBlurCurve    < string UIName = "DOF | Far Blur Power";    string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 4.0;  float UIStep = 0.1;  > = 1.0;
float UI_NearBlurCurve   < string UIName = "DOF | Near Blur Power";   string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 4.0;  float UIStep = 0.1;  > = 1.5;
float UI_NearBlurBleed   < string UIName = "DOF | Near Blur Bleed";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.3;
float UI_HyperFocus      < string UIName = "DOF | Hyperfocal Range";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.15;
bool  UI_RemoveFPSObjects< string UIName = "DOF | Remove FPS Hands"; > = true;
float UI_RenderResMult   < string UIName = "DOF | Render Res (0.5=half)"; string UIWidget = "spinner"; float UIMin = 0.25; float UIMax = 1.0; float UIStep = 0.05; > = 0.5;

// =================== Bokeh Shape ===================
int _spc20 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrBOK < string UIName = "=== BOKEH SHAPE ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

float UI_ShapeRadius     < string UIName = "Bokeh | Max Radius (px)"; string UIWidget = "spinner"; float UIMin = 2.0;  float UIMax = 30.0; float UIStep = 0.5;  > = 12.0;
int   UI_ShapeVertices   < string UIName = "Bokeh | Polygon Sides";   string UIWidget = "spinner"; int   UIMin = 3;    int   UIMax = 10;                        > = 7;
float UI_ShapeCurvature  < string UIName = "Bokeh | Roundness";       string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.8;
float UI_ShapeRotation   < string UIName = "Bokeh | Rotation (deg)";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 180.0;float UIStep = 1.0;  > = 15.0;
float UI_BokehIntensity  < string UIName = "Bokeh | Highlight Pop";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 3.0;  float UIStep = 0.01; > = 0.5;
float UI_AnamorphRatio   < string UIName = "Bokeh | Anamorphic Ratio";string UIWidget = "spinner"; float UIMin = 1.0;  float UIMax = 2.0;  float UIStep = 0.01; > = 1.0;

// =================== Gaussian Smoothing ===================
int _spc30 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrGAU < string UIName = "=== GAUSSIAN SMOOTHING ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

float UI_SmootheningAmount < string UIName = "Gauss | Smooth Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 0.5;
int   UI_GaussQuality      < string UIName = "Gauss | Quality (0-2)"; string UIWidget = "spinner"; int UIMin = 0; int UIMax = 2; > = 1;

// =================== Chromatic Aberration ===================
int _spc40 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrCA < string UIName = "=== CHROMATIC ABERRATION ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

float UI_ChromaAmount    < string UIName = "CA | Longitudinal Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.2;
float UI_LateralChroma   < string UIName = "CA | Lateral Amount";     string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.1;

// =================== Film Grain ===================
int _spc50 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrGRN < string UIName = "=== FILM GRAIN ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

float UI_GrainAmount     < string UIName = "Grain | Amount";     string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.005; > = 0.03;
float UI_GrainSaturation < string UIName = "Grain | Saturation"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;  > = 0.3;

// =================== Color Grading ===================
int _spc60 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrCG < string UIName = "=== DOF COLOR GRADING ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UICG_Enable       < string UIName = "CG | Enable"; > = false;
float UICG_Saturation   < string UIName = "CG | Saturation";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UICG_Brightness   < string UIName = "CG | Brightness";   string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;
float UICG_Contrast     < string UIName = "CG | Contrast";     string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 2.0; float UIStep = 0.01; > = 1.0;

// =================== Cat's Eye ===================
float UICE_Amount    < string UIName = "CE | Cat's Eye Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.45;
float UICE_Onset     < string UIName = "CE | Field Onset";     string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 0.8; float UIStep = 0.01; > = 0.3;

// =================== Bokeh Fringing ===================
float UIBF_Amount    < string UIName = "BF | Fringe Intensity"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.25;

// =================== Highlight Bloom ===================
float UIHB_Threshold < string UIName = "HB | Luma Threshold"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 5.0; float UIStep = 0.01; > = 1.5;
float UIHB_Amount    < string UIName = "HB | Bloom Amount";   string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.3;

// =================== SkyrimBridge DOF ===================
int _spc99 < string UIName = ""; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrSB < string UIName = "=== SKYRIMBRIDGE DOF ==="; string UIWidget = "spacer"; int UIMin = 0; int UIMax = 0; > = 0;

bool  UISB_CombatClarity     < string UIName = "SB | Combat Clarity"; > = true;
float UISB_CombatReduce      < string UIName = "SB | Combat Blur Reduce"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
bool  UISB_DialogueDOF       < string UIName = "SB | Dialogue Narrow Focus"; > = true;
float UISB_DialogueStrength   < string UIName = "SB | Dialogue Strength"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 3.0; float UIStep = 0.01; > = 1.5;
bool  UISB_NightEyeClarity   < string UIName = "SB | Night Eye DOF Suppress"; > = true;
float UISB_NightEyeReduceAmt < string UIName = "SB | Night Eye Reduce"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.7;
bool  UISB_MenuBypass         < string UIName = "SB | Skip DOF In Menus"; > = true;
bool  UISB_UnderwaterSkip     < string UIName = "SB | Skip DOF Underwater"; > = true;
bool  UISB_CrosshairFocus     < string UIName = "SB | Crosshair Focus Lock"; > = true;
float UISB_CrosshairPriority  < string UIName = "SB | Crosshair Priority"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.7;
float UISB_InteriorFocusClamp < string UIName = "SB | Interior Focus Max Dist"; string UIWidget = "spinner"; float UIMin = 0.01; float UIMax = 0.5; float UIStep = 0.01; > = 0.15;


//=============================================================================//
//                           GAME TEXTURES                                     //
//=============================================================================//

Texture2D TextureCurrent;
Texture2D TexturePrevious;
Texture2D TextureOriginal;
Texture2D TextureColor;
Texture2D TextureDepth;
Texture2D TextureFocus;
Texture2D TextureAperture;
Texture2D TextureAdaptation;

Texture2D RenderTargetRGBA32;
Texture2D RenderTargetRGBA64;
Texture2D RenderTargetRGBA64F;
Texture2D RenderTargetR16F;
Texture2D RenderTargetR32F;
Texture2D RenderTargetRGB32F;

SamplerState Point_Sampler
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState Linear_Sampler
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};

#include "Helper/enbHelper_Common.fxh"
// PixelSize and ScreenRes provided by enbHelper_Common.fxh


//=============================================================================//
//                            CONSTANTS                                        //
//=============================================================================//

static const float GOLDEN_ANGLE = 2.3999632;  // PI * (3 - sqrt(5))
static const int   BOKEH_SAMPLES = 36;
static const int   GAUSS_MAX_ITER = 12;

#define FPS_HAND_CUTOFF 0.3468


//=============================================================================//
//                         DEPTH HELPERS                                        //
//=============================================================================//

float GetLinearDepth(float rawDepth)
{
    if (SB_IsActive()) return SB_LinearizeDepth(rawDepth);
    return FastLinDepth(rawDepth, 2999.0);
}


//=============================================================================//
//                     SB DOF HELPERS                                           //
//=============================================================================//

float SB_DOF_GetCoCScale()
{
    float scale = 1.0;
    if (!SB_IsActive()) return scale;

    if (UISB_CombatClarity && SB_Player_Combat.x > 0.5)
        scale *= lerp(1.0, UISB_CombatReduce, SB_Player_Combat.x);

    if (UISB_NightEyeClarity && SB_FX_Vision.x > 0.5)
        scale *= (1.0 - UISB_NightEyeReduceAmt);

    return scale;
}

bool SB_DOF_ShouldSkip()
{
    if (!SB_IsActive()) return false;
    if (UISB_MenuBypass && SB_UI_Menus.x > 0.5) return true;
    if (UISB_UnderwaterSkip && SB_Player_Water.x > 0.5) return true;
    return false;
}

bool SB_DOF_ShouldFreezeFocus()
{
    if (!SB_IsActive()) return false;
    if (UISB_MenuBypass && SB_UI_Menus.x > 0.5) return true;
    if (UISB_DialogueDOF && SB_UI_Menus.y > 0.5) return true;
    return false;
}

float SB_DOF_GetCrosshairDepth()
{
    if (!SB_IsActive() || !UISB_CrosshairFocus) return -1.0;
    if (SB_XHair_Info.x < 0.5) return -1.0;
    float worldDist = SB_XHair_Info.y;
    float farPlane = SB_IsActive() ? SB_Camera_Info.z : 2999.0;
    return saturate(worldDist / farPlane);
}


//=============================================================================//
//                         BASIC SHADERS                                        //
//=============================================================================//

// VS_Basic and PS_Blank provided by enbHelper_Common.fxh

// Bokeh VS output with pre-computed per-frame constants
struct DOF_BokehOutput
{
    float4 pos : SV_POSITION;
    float4 txcoord : TEXCOORD0;
    nointerpolation float2 bokehSinCos : TEXCOORD1;  // .x=sin, .y=cos of rotation
    nointerpolation float  pixelScale  : TEXCOORD2;  // pre-computed pixel scaling
};

// Pre-compute bokeh rotation sincos in VS (saves ~240 ALU ops/pixel)
DOF_BokehOutput VS_DOF_Bokeh(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0)
{
    DOF_BokehOutput OUT;
    OUT.pos = pos;
    OUT.pos.w = 1.0;
    OUT.txcoord = txcoord;

    float rotRad = radians(UI_ShapeRotation);
    sincos(rotRad, OUT.bokehSinCos.x, OUT.bokehSinCos.y);
    OUT.pixelScale = PixelSize.x * UI_ShapeRadius;

    return OUT;
}

float IGNoise(float2 px)
{
    return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
}


//=============================================================================//
//  PASS 0: ReadFocus — Downsample autofocus region                             //
//=============================================================================//

float4 PS_ReadFocus(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    // Focus mode
    float2 focusCenter = float2(0.5, 0.5);
    float  focusRadius = UI_AutofocusRadius;

    if (UI_FocusType == 1) // Mouse focus
    {
        focusCenter = tempInfo1.xy;
        focusRadius = UI_MousefocusRadius;
    }

    if (UI_FocusType == 2) // Manual
        return UI_ManualfocusDepth;

    // Crosshair focus from SkyrimBridge
    if (UI_FocusType == 3 || (UI_FocusType == 0 && UISB_CrosshairFocus))
    {
        float xhairDepth = SB_DOF_GetCrosshairDepth();
        if (xhairDepth > 0.0)
        {
            if (UI_FocusType == 3) return xhairDepth;
            // Blend crosshair with autofocus
            float autoDepth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, focusCenter, 0).x);
            return lerp(autoDepth, xhairDepth, UISB_CrosshairPriority);
        }
    }

    // Weighted average of focus region
    float totalDepth = 0.0;
    float totalWeight = 0.0;

    [unroll]
    for (int x = 0; x < 10; x++)
    {
        [unroll]
        for (int y = 0; y < 10; y++)
        {
            float2 offset = (float2(x, y) - 4.5) / 5.0 * focusRadius;
            float2 sampleUV = focusCenter + offset;

            float d = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, sampleUV, 0).x);

            // FPS hand rejection
            if (UI_RemoveFPSObjects && d < FPS_HAND_CUTOFF)
                continue;

            float w = exp(-dot(offset, offset) / (focusRadius * focusRadius) * 4.0);
            totalDepth += d * w;
            totalWeight += w;
        }
    }

    if (totalWeight < 0.001) return 0.1;
    return totalDepth / totalWeight;
}


//=============================================================================//
//  PASS 1: Focus — Temporal transition + freeze                               //
//=============================================================================//

float4 PS_Focus(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float newFocus = TextureCurrent.SampleLevel(Point_Sampler, float2(0.5, 0.5), 0).x;
    float oldFocus = TexturePrevious.SampleLevel(Point_Sampler, float2(0.5, 0.5), 0).x;

    // Interior focus distance clamp
    if (SB_IsActive() && SB_Interior_Flags.x > 0.5)
        newFocus = min(newFocus, UISB_InteriorFocusClamp);

    // Freeze during menus/dialogue
    if (SB_DOF_ShouldFreezeFocus())
        return oldFocus;

    // Smooth transition
    float speed = UI_FocusTransSpeed * DofParameters.w;
    return lerp(oldFocus, newFocus, saturate(speed));
}


//=============================================================================//
//  PASS 2: DrawCoC — Circle of Confusion                                      //
//=============================================================================//

float4 PS_DrawCoC(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 uv = txcoord.xy;

    // Skip DOF entirely?
    if (SB_DOF_ShouldSkip())
        return float4(0.0, 0.0, 0.0, 0.0);

    float rawDepth = TextureDepth.SampleLevel(Point_Sampler, uv, 0).x;
    float depth = GetLinearDepth(rawDepth);
    float focus = TextureFocus.SampleLevel(Point_Sampler, float2(0.5, 0.5), 0).x;

    // FPS hand: no blur
    if (UI_RemoveFPSObjects && depth < FPS_HAND_CUTOFF)
        return 0.0;

    // Signed CoC: negative = near, positive = far
    float CoC = depth - focus;

    // Hyperfocal clamp
    if (abs(CoC) < UI_HyperFocus * focus)
        CoC = 0.0;

    // Separate near/far blur curves
    float farCoC = 0.0;
    float nearCoC = 0.0;

    if (CoC > 0.0)
        farCoC = pow(abs(CoC / (1.0 - focus + DELTA)), UI_FarBlurCurve);
    else
        nearCoC = pow(abs(CoC / (focus + DELTA)), UI_NearBlurCurve);

    // SB: game-state CoC scaling
    float sbScale = SB_DOF_GetCoCScale();
    farCoC *= sbScale;
    nearCoC *= sbScale;

    // SB: dialogue narrow focus override
    if (SB_IsActive() && UISB_DialogueDOF && SB_UI_Menus.y > 0.5)
    {
        farCoC *= UISB_DialogueStrength;
        nearCoC *= 0.1;  // suppress near blur in dialogue
    }

    // Clamp
    farCoC = saturate(farCoC);
    nearCoC = saturate(nearCoC);

    // Pack: .r = far, .g = near, .b = combined signed, .a = max for downscale
    float signedCoC = farCoC - nearCoC;
    return float4(farCoC, nearCoC, signedCoC * 0.5 + 0.5, max(farCoC, nearCoC));
}


//=============================================================================//
//  PASS 3: Downsample near CoC to half-res                                    //
//=============================================================================//

float4 PS_DownsampleNear(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 uv = txcoord.xy;

    // 4-tap gather with max for near bleed
    float4 c0 = RenderTargetRGBA64.SampleLevel(Point_Sampler, uv + float2(-0.5, -0.5) * PixelSize, 0);
    float4 c1 = RenderTargetRGBA64.SampleLevel(Point_Sampler, uv + float2( 0.5, -0.5) * PixelSize, 0);
    float4 c2 = RenderTargetRGBA64.SampleLevel(Point_Sampler, uv + float2(-0.5,  0.5) * PixelSize, 0);
    float4 c3 = RenderTargetRGBA64.SampleLevel(Point_Sampler, uv + float2( 0.5,  0.5) * PixelSize, 0);

    // Near CoC uses max (bleed outward)
    float nearMax = max(max(c0.g, c1.g), max(c2.g, c3.g));

    // Far CoC uses average
    float farAvg = (c0.r + c1.r + c2.r + c3.r) * 0.25;

    return float4(farAvg, nearMax, 0.0, 0.0);
}


//=============================================================================//
//  PASS 4: Upsample + combine CoC                                            //
//=============================================================================//

float4 PS_UpsampleCoC(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 uv = txcoord.xy;
    float4 fullRes = RenderTargetRGBA64.SampleLevel(Point_Sampler, uv, 0);
    float4 halfRes = RenderTargetR16F.SampleLevel(Linear_Sampler, uv, 0);

    float farCoC = fullRes.r;
    float nearCoC = max(fullRes.g, halfRes.g * UI_NearBlurBleed);

    return float4(farCoC, nearCoC, fullRes.b, max(farCoC, nearCoC));
}


//=============================================================================//
//  PASS 5-6: Bokeh (36-sample golden-angle spiral)                            //
//=============================================================================//

float4 PS_Bokeh(DOF_BokehOutput IN, bool isFar) : SV_Target
{
    float2 uv = IN.txcoord.xy;
    float4 cocData = RenderTargetRGBA32.SampleLevel(Point_Sampler, uv, 0);
    float CoC = isFar ? cocData.r : cocData.g;

    if (CoC < 0.01)
        return TextureColor.SampleLevel(Point_Sampler, uv, 0);

    float bokehRadius = CoC * IN.pixelScale;

    // Anamorphic stretch
    float2 aspectScale = float2(1.0, UI_AnamorphRatio);

    // Rotation sincos pre-computed in vertex shader
    float sinR = IN.bokehSinCos.x;
    float cosR = IN.bokehSinCos.y;

    // Temporal jitter for smooth accumulation
    float jitter = IGNoise(IN.pos.xy + frac(Timer.x) * 5.588);

    float3 totalColor = 0.0;
    float  totalWeight = 0.0;

    float3 centerColor = TextureColor.SampleLevel(Point_Sampler, uv, 0).rgb;
    float  centerLuma = dot(centerColor, K_LUM);

    [unroll]
    for (int i = 0; i < BOKEH_SAMPLES; i++)
    {
        // Golden-angle spiral distribution
        float t = (float(i) + 0.5 + jitter * 0.3) / BOKEH_SAMPLES;
        float r = sqrt(t) * bokehRadius;
        float angle = i * GOLDEN_ANGLE;

        float2 offset;
        sincos(angle, offset.y, offset.x);
        offset *= r;

        // Apply rotation
        float2 rotOffset;
        rotOffset.x = offset.x * cosR - offset.y * sinR;
        rotOffset.y = offset.x * sinR + offset.y * cosR;

        // Apply anamorphic stretch
        rotOffset *= aspectScale;

        // Polygon shaping
        if (UI_ShapeCurvature < 0.99)
        {
            float polyAngle = atan2(rotOffset.y, rotOffset.x);
            float sides = (float)UI_ShapeVertices;
            float polyRadius = cos(PI / sides) / cos(fmod(abs(polyAngle) + PI / sides, TWO_PI / sides) - PI / sides);
            float shapeBlend = lerp(polyRadius, 1.0, UI_ShapeCurvature);
            rotOffset *= shapeBlend;
        }

        // Cat's eye (mechanical vignetting at edges)
        #if ENABLE_CATS_EYE
        {
            float2 screenDist = abs(uv - 0.5) * 2.0;
            float vigDist = length(screenDist);
            float catsEye = saturate(1.0 - (vigDist - UICE_Onset) * UICE_Amount * 3.0);
            float2 radialDir = normalize(uv - 0.5 + DELTA);
            float radialComp = abs(dot(normalize(rotOffset + DELTA), radialDir));
            rotOffset *= lerp(1.0, catsEye, radialComp);
        }
        #endif

        float2 sampleUV = uv + rotOffset;
        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

        float3 sampleColor = TextureColor.SampleLevel(Linear_Sampler, sampleUV, 0).rgb;
        float sampleLuma = dot(sampleColor, K_LUM);

        // Bokeh intensity: brighter samples get more weight (highlight pop)
        float weight = 1.0 + max(sampleLuma - 0.5, 0.0) * UI_BokehIntensity;

        // Optical vignetting
        #if ENABLE_OPTICAL_VIGNETTE
        {
            float edgeDist = length((sampleUV - 0.5) * 2.0);
            weight *= saturate(1.5 - edgeDist);
        }
        #endif

        // Bokeh fringing (green/magenta on edges)
        #if ENABLE_BOKEH_FRINGING
        {
            float ringPos = t;
            if (ringPos > 0.6 && UIBF_Amount > 0.01)
            {
                float fringe = (ringPos - 0.6) / 0.4;
                float3 fringeColor = isFar ? float3(0.9, 1.1, 0.9) : float3(1.1, 0.9, 1.1);
                sampleColor *= lerp(1.0, fringeColor, fringe * UIBF_Amount);
            }
        }
        #endif

        totalColor += sampleColor * weight;
        totalWeight += weight;
    }

    if (totalWeight < 0.01)
        return float4(centerColor, 1.0);

    float3 result = totalColor / totalWeight;

    // Highlight bloom
    #if ENABLE_HIGHLIGHT_BLOOM
    {
        float bloomLuma = dot(result, K_LUM);
        float bloom = max(bloomLuma - UIHB_Threshold, 0.0) * UIHB_Amount;
        result += result * bloom;
    }
    #endif

    return float4(result, CoC);
}

float4 PS_BokehFar(DOF_BokehOutput IN) : SV_Target
{
    return PS_Bokeh(IN, true);
}

float4 PS_BokehNear(DOF_BokehOutput IN) : SV_Target
{
    return PS_Bokeh(IN, false);
}


//=============================================================================//
//  PASS 7: Combine near+far + color grading                                   //
//=============================================================================//

float4 PS_Combine(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 uv = txcoord.xy;
    float4 original = TextureOriginal.SampleLevel(Point_Sampler, uv, 0);
    float4 blurred = TextureColor.SampleLevel(Linear_Sampler, uv, 0);
    float4 cocData = RenderTargetRGBA32.SampleLevel(Point_Sampler, uv, 0);

    float farCoC = cocData.r;
    float nearCoC = cocData.g;
    float totalCoC = max(farCoC, nearCoC);

    // Blend original with bokeh based on CoC
    float3 result = lerp(original.rgb, blurred.rgb, saturate(totalCoC * 3.0));

    // DOF color grading (only in blurred regions)
    if (UICG_Enable && totalCoC > 0.05)
    {
        float gradeMask = saturate(totalCoC * 2.0);
        float luma = dot(result, K_LUM);
        result = lerp(luma, result, UICG_Saturation) * gradeMask + result * (1.0 - gradeMask);
        result *= UICG_Brightness;
        result = lerp(0.5, result, UICG_Contrast);
    }

    // Film grain
    #if ENABLE_GRAINING
    {
        float2 seed = uv * ScreenRes + Timer.z;
        float noise = (Random(seed) - 0.5) * UI_GrainAmount;
        float3 grainColor = lerp(noise, float3(Random(seed + 1.0), Random(seed + 2.0), Random(seed + 3.0)) - 0.5, UI_GrainSaturation);
        result += grainColor * UI_GrainAmount * saturate(totalCoC * 5.0);
    }
    #endif

    return float4(max(result, 0.0), original.a);
}


//=============================================================================//
//  PASS 8-9: Gaussian Blur (Vertical + Horizontal, 12 iter max)               //
//=============================================================================//

float4 PS_GaussBlur(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0, float2 axis) : SV_Target
{
    float2 uv = txcoord.xy;
    float4 center = TextureColor.SampleLevel(Point_Sampler, uv, 0);

    float sigma = center.a * UI_SmootheningAmount * 3.0;
    if (sigma < 0.1) return center;

    static const float qualityMult[3] = { 1.9, 1.5, 1.2 };
    float quality = qualityMult[clamp(UI_GaussQuality, 0, 2)];
    int iterations = min((int)ceil(sigma * quality), GAUSS_MAX_ITER);

    float3 totalColor = center.rgb;
    float  totalWeight = 1.0;

    [loop]
    for (int i = 1; i <= iterations; i++)
    {
        float offset = (float)i;
        float w = exp(-0.5 * offset * offset / (sigma * sigma));

        float2 sampleOffset = axis * offset * PixelSize;

        float3 s1 = TextureColor.SampleLevel(Linear_Sampler, uv + sampleOffset, 0).rgb;
        float3 s2 = TextureColor.SampleLevel(Linear_Sampler, uv - sampleOffset, 0).rgb;

        totalColor += (s1 + s2) * w;
        totalWeight += 2.0 * w;
    }

    return float4(totalColor / totalWeight, center.a);
}

float4 PS_GaussV(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    return PS_GaussBlur(pos, txcoord, float2(0.0, 1.0));
}

float4 PS_GaussH(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float4 result = PS_GaussBlur(pos, txcoord, float2(1.0, 0.0));

    // Chromatic aberration on final pass
    #if ENABLE_CHROMATIC_ABERRATION
    if (UI_LateralChroma > 0.01)
    {
        float2 uv = txcoord.xy;
        float2 fromCenter = uv - 0.5;
        float radialDist = length(fromCenter);
        float2 caDir = fromCenter / max(radialDist, DELTA);

        float caOffset = radialDist * radialDist * UI_LateralChroma * 0.02;

        float r = TextureColor.SampleLevel(Linear_Sampler, uv + caDir * caOffset, 0).r;
        float b = TextureColor.SampleLevel(Linear_Sampler, uv - caDir * caOffset, 0).b;

        result.r = lerp(result.r, r, 0.5);
        result.b = lerp(result.b, b, 0.5);
    }
    #endif

    return result;
}


//=============================================================================//
//  PASS 10: Focus Visualization (debug)                                       //
//=============================================================================//

#if ENABLE_FOCUSING_TOOL
float4 PS_FocusViz(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target
{
    float2 uv = txcoord.xy;
    float3 color = TextureColor.SampleLevel(Point_Sampler, uv, 0).rgb;
    float depth = GetLinearDepth(TextureDepth.SampleLevel(Point_Sampler, uv, 0).x);
    float focus = TextureFocus.SampleLevel(Point_Sampler, float2(0.5, 0.5), 0).x;

    float diff = abs(depth - focus);
    float inFocus = 1.0 - saturate(diff / (UI_HyperFocus * focus + DELTA));

    // Green overlay on focused region
    color = lerp(color, float3(0.0, 1.0, 0.0) * dot(color, K_LUM), inFocus * 0.3);

    return float4(color, 1.0);
}
#endif


//=============================================================================//
//                       TECHNIQUE DEFINITIONS                                 //
//=============================================================================//

// Tech 0: Read Focus (16x16 readback)
technique11 DOF <string UIName="DOF"; string RenderTarget="RenderTargetR16F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_ReadFocus())); }
}

// Tech 1: Focus Transition
technique11 DOF1
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Focus())); }
}

// Tech 2: Draw CoC
technique11 DOF2 <string RenderTarget="RenderTargetRGBA64";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_DrawCoC())); }
}

// Tech 3: Downsample Near
technique11 DOF3 <string RenderTarget="RenderTargetR16F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_DownsampleNear())); }
}

// Tech 4: Upsample CoC
technique11 DOF4 <string RenderTarget="RenderTargetRGBA32";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_UpsampleCoC())); }
}

// Tech 5: Far Bokeh (VS pre-computes rotation sincos)
technique11 DOF5 <string RenderTarget="RenderTargetRGBA64F";>
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_DOF_Bokeh()));
              SetPixelShader (CompileShader(ps_5_0, PS_BokehFar())); }
}

// Tech 6: Near Bokeh (VS pre-computes rotation sincos)
technique11 DOF6
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_DOF_Bokeh()));
              SetPixelShader (CompileShader(ps_5_0, PS_BokehNear())); }
}

// Tech 7: Combine
technique11 DOF7
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_Combine())); }
}

// Tech 8: Gaussian Vertical
technique11 DOF8
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_GaussV())); }
}

// Tech 9: Gaussian Horizontal + CA
technique11 DOF9
{
    pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
              SetPixelShader (CompileShader(ps_5_0, PS_GaussH())); }
}
