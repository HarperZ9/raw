//----------------------------------------------------------------------------------------------//
//                        ENB of the Elders - Lens Effects                                      //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Anamorphic lens flares (ALF) and ghost reflections.                                         //
//                                                                                              //
//  Downsample blur chain + ghost generation + anamorphic streaks.                              //
//  Based on Boris Vorontsov lens framework and kingeric1992 ALF.                               //
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


//=== SKYRIMBRIDGE + ADDON COMPAT ===//

#include "Helper/SkyrimBridge_CB.fxh"
#include "Helper/enbUI_Primer.fxh"

// AMON addon compatibility — map to enblens.fx sampler/convention names
#define Linear_Sampler  Sampler1
#define Point_Sampler   Sampler0
#define NI              nointerpolation
#ifndef DELTA
#define DELTA           1e-6
#endif
#ifndef PI
#define PI              3.1415926535897932384626433832795
#endif
#ifndef TWO_PI
#define TWO_PI          6.2831853071795864769252867665590
#endif
#ifndef K_LUM
#define K_LUM           float3(0.25, 0.60, 0.15)
#endif
#define DNI_SEPARATION(x)  lerp(lerp(Night_##x, Day_##x, ENightDayFactor), Interior_##x, EInteriorFactor)

struct VertexShaderInput {
    float3 pos      : POSITION;
    float2 txcoord  : TEXCOORD0;
};


//=== TEXTURES ===//

Texture2D   TextureDownsampled;     // 1024x1024 HDR scene (downscaled)
Texture2D   TextureColor;           // Output of previous technique
Texture2D   TextureOriginal;        // Original scene color (screen size)
Texture2D   TextureDepth;           // Scene depth
Texture2D   TextureAperture;        // Aperture from DOF (1x1)
Texture2D   RenderTarget1024;
Texture2D   RenderTarget512;
Texture2D   RenderTarget256;
Texture2D   RenderTarget128;
Texture2D   RenderTarget64;
Texture2D   RenderTarget32;
Texture2D   RenderTarget16;
Texture2D   RenderTargetRGBA32;
Texture2D   RenderTargetRGBA64F;


//=== SAMPLERS ===//

SamplerState Sampler0
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState Sampler1
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};


//=== CONSTANTS ===//

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float3 N_LUM = float3(0.2126, 0.7152, 0.0722);
static const float  MAX_LENS_RES = 1024.0;

// ALF sampling
#define ALF_SAMPLE   16
#define ALF_OFFSET   0.5


//=== UI PARAMETERS ===//

float ui_LensIntensityDay
<
    string UIName = "LENS | Day - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_LensIntensityNight
<
    string UIName = "LENS | Night - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.5};

float ui_LensIntensityInterior
<
    string UIName = "LENS | Interior - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.3};

float ui_GhostIntensity
<
    string UIName = "LENS | Ghost Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.5};

float ui_GhostDispersion
<
    string UIName = "LENS | Ghost Chromatic Dispersion";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

float ui_ALFIntensity
<
    string UIName = "LENS | ALF Streak Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.5};

float ui_ALFWidth
<
    string UIName = "LENS | ALF Streak Width";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 20.0;
    float UIStep = 0.1;
> = {8.0};

float ui_ALFSecondary
<
    string UIName = "LENS | ALF Secondary Streak";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

float ui_HaloIntensity
<
    string UIName = "LENS | Halo Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.3};

float ui_Threshold
<
    string UIName = "LENS | Bright Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.8};

// --- Procedural Lens Dirt ---

bool ui_DirtEnable
<
    string UIName = "LENS | Procedural Lens Dirt";
> = {true};

float ui_DirtIntensity
<
    string UIName = "LENS | Dirt Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.3};

float ui_DirtBloomOnly
<
    string UIName = "LENS | Dirt Bloom-Only Mix (0=full 1=bloom only)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.7};

// --- SB Feedback ---

bool ui_FeedbackLens
<
    string UIName = "LENS | SB Feedback Brightness Scaling";
> = {true};

float ui_FeedbackLensScale
<
    string UIName = "LENS | SB Brightness Scale Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

// --- Procedural Weather FX ---

bool UIWFXRain_Enable
<
    string UIName = "WFX | Enable Rain Droplets";
> = {false};

bool UIWFXFrost_Enable
<
    string UIName = "WFX | Enable Frost Vignette";
> = {false};

#define FROSTFX_USE_REFRACTION_METHOD 1
#define SHADERGROUP 1
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP

// --- SkyrimBridge Lens Enhancements ---

#define SHADERGROUP 99
#include "UI/enbUI_Lens.fxh"
#undef SHADERGROUP


//=== ADDONS ===//

#include "Addons/Effect_ProceduralLensDirt.fxh"
#include "Addons/Effect_ProceduralWeatherFX.fxh"


//=== HELPER FUNCTIONS ===//

// Boris circular box blur for downsample chain
float3 FuncBlur(Texture2D inputtex, float2 uv, float srcsize, float destsize)
{
    float scale = 4.0;
    float2 invtargetsize = scale / srcsize;
    invtargetsize.y *= ScreenSize.z;

    float2 fstepcount = srcsize * invtargetsize;
    fstepcount = clamp(fstepcount, 2.0, 16.0);

    int stepcountX = (int)(fstepcount.x + 0.4999);
    int stepcountY = (int)(fstepcount.y + 0.4999);

    fstepcount = 1.0 / fstepcount;
    float4 curr = float4(0.0, 0.0, 0.0, 0.000001);
    float2 pos;
    float2 halfstep = 0.5 * fstepcount.xy;
    pos.x = -0.5 + halfstep.x;
    invtargetsize *= 2.0;

    [loop]
    for (int x = 0; x < stepcountX; x++)
    {
        pos.y = -0.5 + halfstep.y;
        [loop]
        for (int y = 0; y < stepcountY; y++)
        {
            float2 coord = pos.xy * invtargetsize + uv.xy;
            float3 tempcurr = inputtex.SampleLevel(Sampler1, coord.xy, 0).xyz;
            float2 dpos = pos.xy * 2.0;
            float rangefactor = dot(dpos.xy, dpos.xy);
            float tempweight = saturate(1001.0 - 1000.0 * rangefactor);
            tempweight *= saturate(1.0 - rangefactor);
            curr.xyz += tempcurr.xyz * tempweight;
            curr.w += tempweight;
            pos.y += fstepcount.y;
        }
        pos.x += fstepcount.x;
    }

    curr.xyz /= curr.w;
    return curr.xyz;
}


//=== VERTEX SHADER ===//

void VS_Lens(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


//=== PIXEL SHADERS ===//

// Pass 0: Downsample + threshold 1024 → 512
float4 PS_Down512(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = FuncBlur(TextureDownsampled, txcoord, 1024.0, 512.0);

    // Soft threshold: only keep bright areas for lens effects
    float luma = dot(color, N_LUM);
    float soft = saturate(luma - ui_Threshold);
    color *= soft / max(luma, 1e-5);

    return float4(max(color, 0.0), 1.0);
}

// Pass 1: Downsample 512 → 256
float4 PS_Down256(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    return float4(FuncBlur(RenderTarget512, txcoord, 512.0, 256.0), 1.0);
}

// Pass 2: Downsample 256 → 128
float4 PS_Down128(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    return float4(FuncBlur(RenderTarget256, txcoord, 256.0, 128.0), 1.0);
}

// Pass 3: Downsample 128 → 32 (for extra blur)
float4 PS_Down32(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    return float4(FuncBlur(RenderTarget128, txcoord, 128.0, 32.0), 1.0);
}

// Pass 4: ALF streaks + ghosts + halo + SB integration + addons → final output
// NOTE: Output must be lens effects ONLY (no scene). enbeffect.fx ADDs TextureLens to the scene.
// Weather FX use delta encoding: delta = modifiedScene - originalScene (HDR float16 supports negative).
float4 PS_Compose(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    // SB: suppress lens effects during menus and loading screens
#ifdef SKYRIMBRIDGE_FXH
    [branch] if (SB_IsActive() && (SB_IsInMenu() || SB_IsLoading()))
        return float4(0.0, 0.0, 0.0, 1.0);
#endif

    // Day/Night/Interior lens intensity
    float intensity = lerp(lerp(ui_LensIntensityNight, ui_LensIntensityDay, ENightDayFactor),
                           ui_LensIntensityInterior, EInteriorFactor);

    // SB game-state adjustments
#ifdef SKYRIMBRIDGE_FXH
    [branch] if (SB_IsActive())
    {
        // Lightning boost — flares intensify during flashes
        intensity *= 1.0 + SB_Lightning.z * UIBRIDGE_LightningFlash;

        // Precipitation dimming — atmospheric scatter reduces flare visibility
        intensity *= 1.0 - SB_Precipitation.y * 0.3;

        // Accurate interior suppression (SB knows real interior state)
        intensity *= lerp(1.0, 0.7, SB_InteriorFlag() * UIBRIDGE_InteriorAccurate);

        // Feedback: scale lens brightness to match scene luminance
        [branch] if (ui_FeedbackLens && SB_HasFeedback())
        {
            float lumScale = lerp(1.0, saturate(SB_CenterLuminance() * 3.0), ui_FeedbackLensScale);
            intensity *= lumScale;
        }
    }
#endif

    float3 lensResult = 0.0;

    // --- ALF: Anamorphic Lens Flare (horizontal streak from higher-res mip) ---
    if (ui_ALFIntensity > 0.001)
    {
        float3 alfColor = 0.0;

        [unroll]
        for (int i = 0; i < ALF_SAMPLE; i++)
        {
            float2 offset = float2(float(i) - float(ALF_SAMPLE - 1) * ALF_OFFSET, 0.0)
                          * ui_ALFWidth / MAX_LENS_RES;

            float2 coord1 = txcoord + offset;
            if (coord1.x > 0.0 && coord1.x < 1.0)
                alfColor += RenderTarget512.SampleLevel(Sampler1, coord1, 0).rgb;

            float2 coord2 = 1.0 - (txcoord + offset);
            if (coord2.x > 0.0 && coord2.x < 1.0)
                alfColor += RenderTarget512.SampleLevel(Sampler1, coord2, 0).rgb * ui_ALFSecondary;
        }
        alfColor /= float(ALF_SAMPLE);

        lensResult += alfColor * ui_ALFIntensity;
    }

    // --- Ghost reflections (4 ghosts at different offsets) ---
    if (ui_GhostIntensity > 0.001)
    {
        float2 ghostVec = (0.5 - txcoord) * 2.0;

        [unroll]
        for (int g = 0; g < 4; g++)
        {
            float2 gOffset = txcoord + ghostVec * (float(g + 1) * 0.4);

            float3 ghost;
            ghost.r = RenderTarget128.Sample(Sampler1, gOffset + ghostVec * ui_GhostDispersion * 0.01).r;
            ghost.g = RenderTarget128.Sample(Sampler1, gOffset).g;
            ghost.b = RenderTarget128.Sample(Sampler1, gOffset - ghostVec * ui_GhostDispersion * 0.01).b;

            float d = length(gOffset - 0.5) * 2.0;
            float gWeight = 1.0 - saturate(d);
            gWeight *= gWeight;

            lensResult += ghost * gWeight * ui_GhostIntensity;
        }
    }

    // --- Halo ring ---
    if (ui_HaloIntensity > 0.001)
    {
        float2 ghostVec = (0.5 - txcoord) * 2.0;
        float gvLen = length(ghostVec);
        float2 haloVec = (gvLen > 0.001) ? normalize(ghostVec) * 0.35 : 0.0;
        float3 halo = RenderTarget128.Sample(Sampler1, txcoord + haloVec).rgb;
        float haloWeight = length(float2(0.5, 0.5) - txcoord);
        haloWeight = saturate(1.0 - haloWeight * 3.0) * saturate(haloWeight * 5.0 - 1.0);
        lensResult += halo * haloWeight * ui_HaloIntensity;
    }

    // --- SB: Sun color flare tinting ---
#ifdef SKYRIMBRIDGE_FXH
    [branch] if (SB_IsActive() && UIBRIDGE_SunFlare > 0.001)
    {
        float3 sunColor = SB_Sun_Color.rgb;
        float sunLum = dot(sunColor, N_LUM);
        [branch] if (sunLum > 0.01)
        {
            float3 sunTint = sunColor / sunLum;
            lensResult = lerp(lensResult, lensResult * sunTint, UIBRIDGE_SunFlare * UIBRIDGE_SunFlareBlend);
        }
    }
#endif

    // --- Procedural Lens Dirt ---
    [branch] if (ui_DirtEnable)
    {
        float3 dirtMask = ProceduralLensDirt(txcoord);
        // Dirt catches bloom light; mix between accumulated lens and raw bloom
        float3 bloomLight = TextureDownsampled.SampleLevel(Sampler1, txcoord, 0).rgb;
        float3 dirtLight = lerp(lensResult, bloomLight, ui_DirtBloomOnly);
        float dirtInt = ui_DirtIntensity;

#ifdef SKYRIMBRIDGE_FXH
        // Wet dirt: rain makes existing grime more visible
        [branch] if (SB_IsActive() && UIBRIDGE_WetDirt > 0.001)
            dirtInt *= lerp(1.0, UIBRIDGE_WetDirtMult, SB_RainFlag() * UIBRIDGE_WetDirt);
#endif

        lensResult += dirtLight * dirtMask * dirtInt;
    }

    // Apply intensity to all lens-native effects
    float3 finalLens = max(lensResult * intensity, 0.0);

    // --- Weather FX: Lens-surface effects (delta encoded) ---
    // These physically modify what's seen THROUGH the lens surface (refraction).
    // Since enblens.fx output is additive (enbeffect.fx: scene + TextureLens),
    // scene modifications are encoded as delta = modified - original.
#ifdef SKYRIMBRIDGE_FXH
    [branch] if (SB_IsActive())
    {
        float3 sceneOrig = TextureOriginal.Sample(Sampler1, txcoord).rgb;
        float3 sceneMod  = sceneOrig;

        // --- Rain droplets on lens ---
        float rainStr = SB_RainFlag() * SB_Precipitation.y
                      * lerp(Night_UIWFXRain_Intensity, Day_UIWFXRain_Intensity, ENightDayFactor)
                      * UIBRIDGE_RainFromWeather * (1.0 - EInteriorFactor);

        [branch] if (UIWFXRain_Enable && rainStr > 0.01)
        {
            float CellSize = 1.0 / 3.0;
            float3 rainAccum = 0.0;

            [unroll] for (int gy = 0; gy < 3; gy++)
            [unroll] for (int gx = 0; gx < 3; gx++)
            {
                float2 cellOrigin = float2(gx, gy) * CellSize;
                float2 localUV = (txcoord - cellOrigin) / CellSize;

                [branch] if (all(localUV >= 0.0) && all(localUV <= 1.0))
                {
                    // Time-varying seed: new droplets each tick
                    float tick = floor(Timer.x / max(UIWFXRain_Tickrate, 0.01));
                    float2 seed = float2(gx, gy) + tick * 7.13;

                    float4 drop = ProceduralRainDroplet(localUV, seed);

                    [branch] if (drop.w > 0.01)
                    {
                        float2 dropN = drop.xy * 2.0 - 1.0;
                        float2 refUV = saturate(txcoord + dropN * UIWFXRain_Curvature * 0.05);
                        float3 refracted = TextureOriginal.SampleLevel(Sampler1, refUV, 0).rgb;
                        rainAccum += (refracted - sceneOrig) * drop.w;
                    }
                }
            }

            sceneMod += rainAccum * rainStr;
        }

        // --- Frost vignette on lens ---
        float frostStr = SB_SnowFlag() * SB_Precipitation.y
                       * lerp(Night_UIWFXFrost_Intensity, Day_UIWFXFrost_Intensity, ENightDayFactor)
                       * UIBRIDGE_FrostFromSnow * (1.0 - EInteriorFactor);

        [branch] if (UIWFXFrost_Enable && frostStr > 0.01)
        {
            // Radial vignette: frost grows from screen edges inward
            float radDist = length(txcoord - 0.5) * 2.0;
            float vignette = 1.0 - smoothstep(UIWFXFrost_RadiusOuter, UIWFXFrost_RadiusInner, radDist);

            // Animated coverage pulse
            float pulse = 1.0 + sin(Timer.x * UIWFXFrost_PulseRate * TWO_PI) * UIWFXFrost_PulseStrength * 0.1;
            float coverage = saturate(pow(abs(vignette), UIWFXFrost_Curve) * pulse * frostStr);

            [branch] if (coverage > 0.01)
            {
                float3 frostData = ProceduralFrostRefraction(txcoord);
                float  frostMask = frostData.z * coverage;
                float2 frostN    = frostData.xy * 2.0 - 1.0;

                float2 frostUV = txcoord + frostN * UIWFXFrost_RefracRange;
                frostUV = UIWFXFrost_AllowOvershoot ? saturate(frostUV) : clamp(frostUV, 0.001, 0.999);

                float3 frostRef = TextureOriginal.SampleLevel(Sampler1, frostUV, 0).rgb * UIWFXFrost_Tint;
                float  opacity  = saturate(frostMask * UIWFXFrost_Opacity * 0.01);

                sceneMod = lerp(sceneMod, frostRef, opacity);
            }
        }

        // Encode scene modifications as additive delta
        finalLens += sceneMod - sceneOrig;
    }
#endif

    return float4(finalLens, 1.0);
}


//=== TECHNIQUES ===//

// Pass 0: Threshold + downsample → RenderTarget512
technique11 Draw <string UIName = "EotE: Lens"; string RenderTarget="RenderTarget512";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Down512()));
    }
}

// Pass 1: Downsample → RenderTarget256
technique11 Draw1 <string RenderTarget="RenderTarget256";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Down256()));
    }
}

// Pass 2: Downsample → RenderTarget128
technique11 Draw2 <string RenderTarget="RenderTarget128";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Down128()));
    }
}

// Pass 3: Downsample → RenderTarget32
technique11 Draw3 <string RenderTarget="RenderTarget32";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Down32()));
    }
}

// Pass 4: Ghost + ALF streaks + halo → final output (becomes TextureLens)
technique11 Draw4
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Compose()));
    }
}
