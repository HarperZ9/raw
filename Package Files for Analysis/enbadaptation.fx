//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                      enbadaptation.fx — Eye Adaptation                                       //
//                          for Silent Horizons ENB (Skyrim SE)                                 //
//                        DirectX 11, Shader Model 5.0                                         //
//                                                                                              //
//  Original histogram adaptation by Kingeric1992                                               //
//  Original framework by LonelyKitsuune / T.Thanner — CC BY-NC-ND 4.0                        //
//  Boris Vorontsov for ENBSeries                                                               //
//                                                                                              //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Rebuilt implementation by Zain Dana Harper — February 2026                                  //
//                                                                                              //
//  Architecture: 2 techniques, 2 passes                                                       //
//    Tech 0 (Downsample): TextureCurrent 256×256 → 16×16 R32F                                //
//      - Focus-weighted luminance metering with emissive rejection                            //
//    Tech 1 (Draw): Histogram-based adaptation 16×16 → 1×1 R32F                              //
//      - 64-bin histogram with percentile anchoring (Kingeric1992)                            //
//      - 7-TOD exposure compensation with darkness/lightness attenuation                      //
//      - SkyrimBridge: combat speed, Night Eye, lightning, torch, storm,                      //
//        interior ambient, nearby lights, altitude, dialogue, slow-time                       //
//      - Asymmetric temporal smoothing with hysteresis and rate limiting                      //
//                                                                                              //
//  References:                                                                                 //
//    [1] Kingeric1992, "Histogram based adaptation", ENBDev Forum                             //
//    [2] Reinhard et al., "Photographic Tone Reproduction", SIGGRAPH 2002                      //
//    [3] Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013                              //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=============================================================================//
//                              INCLUDES                                       //
//=============================================================================//

#include "UI/enbUI_Primer.fxh"


//=============================================================================//
//                       ENB EXTERNAL PARAMETERS                               //
//=============================================================================//

float4 Timer;           // x=generic [0..1], y=avg fps, z=frame# mod 9999, w=frame time
float4 ScreenSize;      // x=Width, y=1/Width, z=aspect(W/H), w=1/aspect
float  AdaptiveQuality;
float4 TimeOfDay1;      // x=dawn, y=sunrise, z=day, w=sunset [0..1]
float4 TimeOfDay2;      // x=dusk, y=night [0..1]
float  ENightDayFactor; // 0=night, 1=day
float  EInteriorFactor; // 0=exterior, 1=interior
float  FieldOfView;
float4 Weather;
float4 tempF1; float4 tempF2; float4 tempF3;
float4 tempInfo1; float4 tempInfo2;


//=============================================================================//
//                    ADAPTATION-SPECIFIC ENB PARAMETERS                        //
//=============================================================================//
//
//  These are ONLY available in the adaptation shader context.
//  AdaptationParameters.w = pre-computed temporal lerp factor from enbseries.ini
//
//  TextureCurrent:  Pass 0 = 256×256 downscaled scene (R16G16B16A16 or R11G11B10)
//                   Pass 1 = 16×16 output of Pass 0 (R32F)
//  TexturePrevious: Pass 1 only = 1×1 previous frame result (R32F)

float4    AdaptationParameters;  // x=Min, y=Max, z=Sensitivity, w=deltaTime*speed
Texture2D TextureCurrent;
Texture2D TexturePrevious;


//=============================================================================//
//                           SAMPLER STATES                                    //
//=============================================================================//

SamplerState Point_Sampler
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Border;
    AddressV = Border;
};

SamplerState Linear_Sampler
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Border;
    AddressV = Border;
};

#include "Helper/enbHelper_Common.fxh"


//=============================================================================//
//                  SKYRIMBRIDGE INLINE PARAMETERS                             //
//=============================================================================//
//
//  Only the 16 SB_ float4 params actually used by this shader are declared
//  here, rather than including the full 103-param SkyrimBridge.fxh.
//  This keeps the constant buffer well under ENB's limits.

#define SKYRIMBRIDGE_FXH 1

float4 SB_Render_Frame;      // x=frameCount, y=deltaTime, z=fps, w=gameTime
float4 SB_UI_Menus;          // x=isInMenu, y=isInDialogue, z=isConsoleOpen, w=reserved
float4 SB_UI_HUD;            // x=compassActive, y=subtitleActive, z=cinematic, w=isLoading
float4 SB_Player_Combat;     // x=inCombat, y=combatState, z=killcam, w=reserved
float4 SB_FX_Vision;         // x=nightEye, y=detectLife, z=auraWhisper, w=reserved
float4 SB_Lightning;         // x=thunderTimer, y=isActive, z=flashIntensity, w=reserved
float4 SB_Equip_Flags;       // x=weaponDrawn, y=hasBow, z=hasTorch, w=isTwoHanding
float4 SB_Weather_Flags;     // x=isStormy, y=reserved, z=isWet, w=isCold
float4 SB_Precipitation;     // x=type, y=intensity, z=windDir, w=reserved
float4 SB_Fog_Density;       // x=nearDist, y=density, z=farDist, w=reserved
float4 SB_Interior_Ambient;  // xyz=ambientColor, w=ambientScale
float4 SB_Light_Summary;     // x=count, y=avgRadius, z=totalFlux, w=dominantDist
float4 SB_Player_Position;   // xyz=worldPos, w=altitudeAboveWater
float4 SB_Sun_Direction;     // xyz=direction, w=elevation(rad)
float4 SB_FX_Time;           // x=slowTimeFactor, y=reserved, z=reserved, w=reserved
float4 SB_IS_HDR;            // x=eyeAdaptSpeed, y=bloomScale, z=targetLum, w=sunlightScale

// Inline helper functions matching SkyrimBridge.fxh API
bool SB_IsActive()       { return SB_Render_Frame.x > 0.0; }
bool SB_IsInMenu()       { return SB_UI_Menus.x > 0.5; }
bool SB_IsInDialogue()   { return SB_UI_Menus.y > 0.5; }
bool SB_IsLoading()      { return SB_UI_HUD.w > 0.5; }
bool SB_HasTorchEquipped() { return SB_Equip_Flags.z > 0.5; }


//=============================================================================//
//                         UI PARAMETERS                                       //
//=============================================================================//

UI_FileHeaderLong("   Silent Horizons — Eye Adaptation", "         Kitsuune / Zain Dana Harper")


// ─────────────── Histogram Anchors (Kingeric1992) ────────────────────────────
int _spc00 < string UIName = ""; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrHIST < string UIName = "======== Histogram ========"; int UIMin = 0; int UIMax = 0; > = 0;

float Bias          < string UIName = "|- Auto Exposure Bias";       > = 0.0;
float MaxBrightness < string UIName = "|- Adapt Max Brightness"; string UIWidget = "spinner"; float UIMin = -9.0; float UIMax = 3.0; float UIStep = 0.01; > = 1.0;
float MinBrightness < string UIName = "|- Adapt Min Brightness"; string UIWidget = "spinner"; float UIMin = -9.0; float UIMax = 3.0; float UIStep = 0.01; > = -4.0;
float LowPercent    < string UIName = "|- Adapt Low Percent";    string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 1.0; float UIStep = 0.01; > = 0.80;
float HighPercent   < string UIName = "|- Adapt High Percent";   string UIWidget = "spinner"; float UIMin = 0.5;  float UIMax = 1.0; float UIStep = 0.01; > = 0.95;


// ─────────────── Focus Weighting ─────────────────────────────────────────────
int _spc01 < string UIName = ""; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrFOC < string UIName = "-------- Focus --------"; int UIMin = 0; int UIMax = 0; > = 0;

float UIFocusAmount < string UIName = "|- Focus Amount";       string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
float UIFocusInner  < string UIName = "|- Focus Inner Radius"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.3;
float UIFocusOuter  < string UIName = "|- Focus Outer Radius"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.7;
float UIEmissiveThresh < string UIName = "|- Emissive Threshold"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 20.0; float UIStep = 0.1; > = 8.0;
float UIEmissiveWeight < string UIName = "|- Emissive Weight";    string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;  float UIStep = 0.01; > = 0.1;


// ─────────────── 7-TOD Exposure Compensation ─────────────────────────────────
int _spc02 < string UIName = ""; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrTOD < string UIName = "======== Per Time-of-Day ========"; int UIMin = 0; int UIMax = 0; > = 0;

#define ADAPT_TOD(TOD, DEXP, DDRK, DLIT) \
float UIAdapt_##TOD##_Exp   < string UIName = "|- " #TOD " - Exposure (EV)";          string UIWidget = "spinner"; float UIMin = -5.0; float UIMax = 5.0;  float UIStep = 0.01; > = DEXP; \
float UIAdapt_##TOD##_Dark  < string UIName = "|- " #TOD " - Darkness Attenuation";   string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 10.0; float UIStep = 0.01; > = DDRK; \
float UIAdapt_##TOD##_Light < string UIName = "|- " #TOD " - Lightness Attenuation";  string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 10.0; float UIStep = 0.01; > = DLIT;

ADAPT_TOD(Dawn,     0.0, 2.5, 1.5)
ADAPT_TOD(Sunrise,  0.0, 2.5, 1.5)
ADAPT_TOD(Day,      0.0, 2.5, 1.5)
ADAPT_TOD(Sunset,   0.0, 2.5, 1.5)
ADAPT_TOD(Dusk,     0.0, 2.5, 1.5)
ADAPT_TOD(Night,    0.0, 2.5, 1.5)
ADAPT_TOD(Interior, 0.0, 2.5, 1.5)


// ─────────────── Temporal Smoothing ──────────────────────────────────────────
int _spc03 < string UIName = ""; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrTMP < string UIName = "-------- Temporal --------"; int UIMin = 0; int UIMax = 0; > = 0;

float UIDarkDelay     < string UIName = "|- Darkness Delay (frames)";    string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 120.0; float UIStep = 0.5; > = 35.0;
float UILightDelay    < string UIName = "|- Lightness Delay (frames)";   string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 120.0; float UIStep = 0.5; > = 15.0;
float UIHysteresis    < string UIName = "|- Hysteresis Width (EV)";      string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0;   float UIStep = 0.01; > = 0.05;
float UIMaxRate       < string UIName = "|- Max Rate (EV/sec)";          string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 20.0;  float UIStep = 0.1;  > = 6.0;
float UISceneAccel    < string UIName = "|- Scene Change Acceleration";  string UIWidget = "spinner"; float UIMin = 1.0; float UIMax = 10.0;  float UIStep = 0.1;  > = 4.0;
float UISceneThresh   < string UIName = "|- Scene Change Threshold (EV)"; string UIWidget = "spinner"; float UIMin = 0.5; float UIMax = 5.0;   float UIStep = 0.1; > = 2.0;


// ─────────────── Advanced ────────────────────────────────────────────────────
int _spc04 < string UIName = ""; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrADV < string UIName = "-------- Advanced --------"; int UIMin = 0; int UIMax = 0; > = 0;

float UIAttenMidGrey  < string UIName = "|- Attenuation Mid Grey (EV)";  string UIWidget = "spinner"; float UIMin = -3.0; float UIMax = 3.0;  float UIStep = 0.01; > = 0.0;
float UIDarkSens      < string UIName = "|- Darkness Sensitivity";       string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.5;
float UILightSens     < string UIName = "|- Lightness Sensitivity";      string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.5;
float UIDarkSensP     < string UIName = "|- Darkness Sensitivity %";     string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.5;
float UILightSensP    < string UIName = "|- Lightness Sensitivity %";    string UIWidget = "spinner"; float UIMin = 0.0;  float UIMax = 1.0;  float UIStep = 0.01; > = 0.9;


// ─────────────── SkyrimBridge Tuning ─────────────────────────────────────────
int _spc05 < string UIName = ""; int UIMin = 0; int UIMax = 0; > = 0;
int _hdrSB < string UIName = "-------- SkyrimBridge --------"; int UIMin = 0; int UIMax = 0; > = 0;

float UISB_NightEyeMul  < string UIName = "|- Night Eye Boost";           string UIWidget = "spinner"; float UIMin = 1.0; float UIMax = 4.0; float UIStep = 0.1;  > = 2.0;
float UISB_LightningMul < string UIName = "|- Lightning Spike Mult";      string UIWidget = "spinner"; float UIMin = 1.0; float UIMax = 8.0; float UIStep = 0.1;  > = 3.0;
float UISB_TorchLift    < string UIName = "|- Torch Dark Lift";           string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.3;
float UISB_RainDim      < string UIName = "|- Rain/Snow Dimming";         string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01; > = 0.15;
float UISB_CombatSpeed  < string UIName = "|- Combat Speed Mult";         string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
float UISB_DialogSpeed  < string UIName = "|- Dialogue Slow Factor";      string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.3;
float UISB_IntTransit   < string UIName = "|- Interior Transition Spd";   string UIWidget = "spinner"; float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.1;  > = 1.5;
float UISB_NearLightBias< string UIName = "|- Nearby Light Lift";         string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.15;
float UISB_AltitudeBias < string UIName = "|- Altitude Brightness Bias";  string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01; > = 0.05;


//=============================================================================//
//                       HELPER: 7-TOD INTERPOLATION                           //
//=============================================================================//

float TOD7(float dawn, float sunrise, float day, float sunset, float dusk, float night, float interior)
{
    float exterior = dawn    * TimeOfDay1.x
                   + sunrise * TimeOfDay1.y
                   + day     * TimeOfDay1.z
                   + sunset  * TimeOfDay1.w
                   + dusk    * TimeOfDay2.x
                   + night   * TimeOfDay2.y;
    return lerp(exterior, interior, EInteriorFactor);
}


//=============================================================================//
//                           VERTEX SHADER                                     //
//=============================================================================//
//
//  Adaptation uses a custom VS with -7/256 texcoord offset for proper
//  tile centering on the 256→16 downsample grid.

void VS_Quad(inout float4 pos : SV_POSITION, inout float2 txcoord0 : TEXCOORD0)
{
    pos.w     = 1.0;
    txcoord0 -= 7.0 / 256.0;
}


//=============================================================================//
//                                                                             //
//     PASS 0: DOWNSAMPLE  (TextureCurrent 256×256 → 16×16 R32F)             //
//                                                                             //
//=============================================================================//
//
//  TextureCurrent is ENB's internal 256×256 downscale of the full scene.
//  Format: R16G16B16A16F or R11G11B10F (alpha ignored).
//  Output: R32F containing log2(focus-weighted luminance per tile).
//
//  We sample an 8×8 grid per tile with:
//    - Center focus weighting (configurable inner/outer radius)
//    - Emissive outlier rejection (brightness-capped contribution)

float4 PS_Downsample(float4 pos : SV_POSITION, float2 txcoord0 : TEXCOORD0) : SV_Target
{
    float  logSum    = 0.0;
    float  weightSum = 0.0;
    float4 coord     = float4(txcoord0.xyy, 1.0 / 128.0);

    for(int x = 0; x < 8; x++)
    {
        coord.y = coord.z;
        for(int y = 0; y < 8; y++)
        {
            float4 color = TextureCurrent.Sample(Linear_Sampler, coord.xy);
            float  luma  = max(max3(color.rgb), 0.0001);

            // Focus weighting: center of screen gets more influence
            float2 uvNorm = coord.xy;
            float  dist   = length(uvNorm - 0.5) * 2.0;
            float  fw     = 1.0 - UIFocusAmount
                                * smoothstep(UIFocusInner, UIFocusOuter, dist);
            fw = max(fw, 0.05);

            // Emissive outlier rejection: cap very bright pixel influence
            float emissiveMask = (luma > UIEmissiveThresh)
                               ? lerp(UIEmissiveWeight, 1.0,
                                      saturate(UIEmissiveThresh / luma))
                               : 1.0;

            float w = fw * emissiveMask;
            logSum    += log2(luma) * w;
            weightSum += w;

            coord.y += coord.w;
        }
        coord.x += coord.w;
    }

    // Output weighted log2 luminance (if no samples, return log2(0.001))
    float result = (weightSum > 0.0001) ? (logSum / weightSum) : -10.0;
    return result;
}


//=============================================================================//
//                                                                             //
//     PASS 1: HISTOGRAM ADAPTATION  (TextureCurrent 16×16 → 1×1 R32F)       //
//                                                                             //
//=============================================================================//
//
//  Reads the 16×16 focus-weighted log2-luminance from Pass 0.
//  Builds a 64-bin histogram, finds percentile anchors (Kingeric1992),
//  applies 7-TOD exposure compensation and SkyrimBridge modulation,
//  then temporal smoothing against TexturePrevious.

float4 PS_Histogram() : SV_Target
{
    //─── Previous frame adaptation ─────────────────────────────────────────
    float prevAdapt = TexturePrevious.Sample(Point_Sampler, 0.5).x;
    bool firstFrame = (prevAdapt < 0.0001);
    prevAdapt = max(prevAdapt, 0.0001);


    //─── SkyrimBridge: pause during menus/loading ──────────────────────────
    bool sbActive = SB_IsActive();
    [branch] if(sbActive)
    {
        if(SB_IsInMenu() || SB_IsLoading())
            return prevAdapt;

        // Killcam: freeze adaptation
        if(SB_Player_Combat.z > 0.5)
            return prevAdapt;
    }


    //=== Build 64-bin histogram from 16×16 tiles ===========================//

    float4 coord = float4(1.0 / 32.0, 1.0 / 32.0, 1.0 / 32.0, 1.0 / 16.0);
    float4 bin[16];

    [unroll] for(int k = 0; k < 16; k++)
        bin[k] = 0.0;

    [loop] for(int i = 0; i < 16; i++)
    {
        coord.y = coord.z;
        [loop] for(int j = 0; j < 16; j++)
        {
            float  color = TextureCurrent.SampleLevel(Point_Sampler, coord.xy, 0.0).r;
            float  level = saturate((color + 9.0) / 12.0) * 63.0; // map [-9, 3] → [0, 63]
            bin[level * 0.25] += float4(0.0, 1.0, 2.0, 3.0) == float4(trunc(level % 4).xxxx);
            coord.y += coord.w;
        }
        coord.x += coord.w;
    }


    //=== Percentile anchoring =============================================//

    float2 adaptAnchor = 0.5;
    float2 accumulate  = float2(HighPercent - 1.0, LowPercent - 1.0) * 256.0;

    [loop] for(int l = 15; l > 0; l--)
    {
        accumulate += bin[l].w;
        adaptAnchor = (accumulate.xy < bin[l].ww) ? l * 4.0 + accumulate.xy / bin[l].ww + 3.0 : adaptAnchor;

        accumulate += bin[l].z;
        adaptAnchor = (accumulate.xy < bin[l].zz) ? l * 4.0 + accumulate.xy / bin[l].zz + 2.0 : adaptAnchor;

        accumulate += bin[l].y;
        adaptAnchor = (accumulate.xy < bin[l].yy) ? l * 4.0 + accumulate.xy / bin[l].yy + 1.0 : adaptAnchor;

        accumulate += bin[l].x;
        adaptAnchor = (accumulate.xy < bin[l].xx) ? l * 4.0 + accumulate.xy / bin[l].xx + 0.0 : adaptAnchor;
    }

    // Convert histogram anchor to linear luminance
    float adaptLog = (adaptAnchor.x + adaptAnchor.y) * 0.5 / 63.0 * 12.0 - 9.0;
    float avgLuma  = pow(2.0, clamp(adaptLog, MinBrightness, MaxBrightness) + Bias);


    //=== 7-TOD Exposure Compensation =======================================//

    float expComp = TOD7(UIAdapt_Dawn_Exp,    UIAdapt_Sunrise_Exp,  UIAdapt_Day_Exp,
                         UIAdapt_Sunset_Exp,   UIAdapt_Dusk_Exp,    UIAdapt_Night_Exp,
                         UIAdapt_Interior_Exp);
    avgLuma *= exp2(expComp);


    //=== Darkness / Lightness Attenuation ==================================//

    float midGrey = exp2(UIAttenMidGrey) * 0.18;

    float darkAtten  = TOD7(UIAdapt_Dawn_Dark,    UIAdapt_Sunrise_Dark,  UIAdapt_Day_Dark,
                            UIAdapt_Sunset_Dark,   UIAdapt_Dusk_Dark,    UIAdapt_Night_Dark,
                            UIAdapt_Interior_Dark);
    float lightAtten = TOD7(UIAdapt_Dawn_Light,    UIAdapt_Sunrise_Light, UIAdapt_Day_Light,
                            UIAdapt_Sunset_Light,   UIAdapt_Dusk_Light,   UIAdapt_Night_Light,
                            UIAdapt_Interior_Light);

    float darkSens  = lerp(1.0, UIDarkSensP,  UIDarkSens);
    float lightSens = lerp(1.0, UILightSensP, UILightSens);

    if(avgLuma < midGrey)
    {
        float ratio = saturate(1.0 - avgLuma / max(midGrey, 0.0001));
        float pull  = pow(ratio, darkAtten) * darkSens;
        avgLuma = lerp(avgLuma, midGrey, pull);
    }
    else
    {
        float ratio = saturate(1.0 - midGrey / max(avgLuma, 0.0001));
        float pull  = pow(ratio, lightAtten) * lightSens;
        avgLuma = lerp(avgLuma, midGrey, pull);
    }


    //=== SkyrimBridge Modulation ===========================================//

    [branch] if(sbActive)
    {
        // Night Eye: magical brightness boost
        float nightEye = SB_FX_Vision.x;
        if(nightEye > 0.01)
            avgLuma *= lerp(1.0, UISB_NightEyeMul, nightEye);

        // Lightning flash: spike adaptation
        float lightningFlash = SB_Lightning.z;
        if(SB_Lightning.y > 0.5 && lightningFlash > 0.01)
            avgLuma = lerp(avgLuma, avgLuma * UISB_LightningMul, lightningFlash * 0.5);

        // Torch: in dark scenes, raise perceived ambient
        if(SB_HasTorchEquipped() && avgLuma < midGrey)
            avgLuma = lerp(avgLuma, avgLuma * 1.3, UISB_TorchLift);

        // Rain/snow: overcast dimming
        float precip = SB_Precipitation.y;
        if((SB_Weather_Flags.z > 0.5 || SB_Weather_Flags.w > 0.5) && precip > 0.01)
            avgLuma *= (1.0 - precip * UISB_RainDim);

        // Fog: thick fog scatters light, raising perceived brightness
        float fogDensity = SB_Fog_Density.y;
        if(fogDensity > 0.3 && EInteriorFactor < 0.5)
            avgLuma *= lerp(1.0, 1.15, saturate((fogDensity - 0.3) * 1.43));

        // Interior ambient: scale by cell's ambient intensity
        if(EInteriorFactor > 0.5)
        {
            float ambientScale = SB_Interior_Ambient.a;
            if(ambientScale > 0.0001)
                avgLuma = lerp(avgLuma, avgLuma * ambientScale, 0.2);
        }

        // Nearby lights: bias adaptation upward
        float nearbyFlux = SB_Light_Summary.z;
        if(nearbyFlux > 0.1)
        {
            float liftFactor = saturate(nearbyFlux * 0.2) * UISB_NearLightBias;
            avgLuma *= (1.0 + liftFactor);
        }

        // Altitude: higher = brighter
        if(EInteriorFactor < 0.5)
        {
            float altitude = SB_Player_Position.w;
            float altFactor = saturate(altitude / 10000.0) * UISB_AltitudeBias;
            avgLuma *= (1.0 + altFactor);
        }

        // Sun elevation: near-horizon = dimmer than luminance suggests
        if(EInteriorFactor < 0.5)
        {
            float sunElev = SB_Sun_Direction.w;
            if(sunElev > -0.1 && sunElev < 0.15)
            {
                float lowSunFade = smoothstep(-0.1, 0.15, sunElev);
                avgLuma *= lerp(0.85, 1.0, lowSunFade);
            }
        }
    }


    //=== Temporal Smoothing ================================================//

    // Frame delta time
    float dt;
    [branch] if(sbActive && SB_Render_Frame.y > 0.0)
        dt = SB_Render_Frame.y;
    else
        dt = Timer.w;
    dt = clamp(dt, 0.0001, 0.5);

    // Base adaptation delays
    float darkDelay  = UIDarkDelay;
    float lightDelay = UILightDelay;

    // SB temporal modifiers
    [branch] if(sbActive)
    {
        // Combat: faster reaction
        if(SB_Player_Combat.x > 0.5)
        {
            darkDelay  *= UISB_CombatSpeed;
            lightDelay *= UISB_CombatSpeed;
        }

        // Dialogue: slow down adaptation
        if(SB_IsInDialogue())
        {
            float dialogSlow = 1.0 + (1.0 - UISB_DialogSpeed) * 3.0;
            darkDelay  *= dialogSlow;
            lightDelay *= dialogSlow;
        }

        // Slow time: stretch adaptation
        float slowFactor = SB_FX_Time.x;
        if(slowFactor > 0.01 && slowFactor < 0.9)
        {
            float sf = max(slowFactor, 0.1);
            darkDelay  /= sf;
            lightDelay /= sf;
        }

        // Interior transition: specific speed
        if(EInteriorFactor > 0.01 && EInteriorFactor < 0.99)
        {
            float transitBlend = smoothstep(0.01, 0.2, EInteriorFactor)
                               * smoothstep(0.99, 0.8, EInteriorFactor);
            darkDelay  = lerp(darkDelay,  darkDelay  / UISB_IntTransit, transitBlend);
            lightDelay = lerp(lightDelay, lightDelay / UISB_IntTransit, transitBlend);
        }

        // Game's own eye adapt speed
        float gameAdaptSpeed = SB_IS_HDR.x;
        if(gameAdaptSpeed > 0.01 && gameAdaptSpeed < 100.0)
        {
            float gameTau = 1.0 / max(gameAdaptSpeed, 0.01);
            darkDelay  = lerp(darkDelay,  gameTau * 60.0, 0.2);
            lightDelay = lerp(lightDelay, gameTau * 60.0, 0.2);
        }
    }

    // Scene change detection
    float prevEV   = log2(max(prevAdapt, 0.0001) / 0.18);
    float targetEV = log2(max(avgLuma,   0.0001) / 0.18);
    float deltaEV  = abs(targetEV - prevEV);

    float sceneAccel = 1.0;
    if(deltaEV > UISceneThresh)
    {
        sceneAccel = lerp(1.0, UISceneAccel,
                          saturate((deltaEV - UISceneThresh) / max(UISceneThresh, 0.5)));
    }

    // Hysteresis dead zone
    float effectiveTarget = avgLuma;
    if(deltaEV < UIHysteresis && !firstFrame)
        effectiveTarget = prevAdapt;

    // Exponential smoothing with rate limiting
    float adaptDelay = (effectiveTarget > prevAdapt) ? lightDelay : darkDelay;
    adaptDelay /= sceneAccel;

    float tau  = max(adaptDelay / 60.0, 0.01);
    float rate = 1.0 - exp(-dt / tau);

    float adapted = lerp(prevAdapt, effectiveTarget, rate);

    // Maximum rate limiter
    float maxDeltaPerFrame = UIMaxRate * dt;
    float maxMultiplier    = exp2(maxDeltaPerFrame);
    adapted = clamp(adapted, prevAdapt / maxMultiplier, prevAdapt * maxMultiplier);

    // First-frame snap
    if(firstFrame)
        adapted = avgLuma;

    // Safety clamp
    adapted = clamp(adapted, 0.0001, 100.0);

    return adapted;
}


//=============================================================================//
//                              TECHNIQUES                                     //
//=============================================================================//
//
//  ENB adaptation requires exactly this two-technique structure:
//    Tech 0 "Downsample": reads 256×256 TextureCurrent → 16×16 R32F
//    Tech 1 "Draw": reads 16×16 TextureCurrent + 1×1 TexturePrevious → 1×1 R32F

TECH11 (Downsample,                                               VS_Quad(), PS_Downsample())
TECH11 (Draw <string UIName="Adaptation - Silent Horizons";>,     VS_Quad(), PS_Histogram())
