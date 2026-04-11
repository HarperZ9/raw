//----------------------------------------------------------------------------------------------//
//                     ENB of the Elders - Sun Sprite                                            //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Procedural lens flare system: N-gon ghosts, anamorphic streaks, starburst, hoop, glare.     //
//  All passes use additive blending (forced by ENB for enbsunsprite.fx).                       //
//                                                                                              //
//  Based on LonelyKitsuune sunsprite system (Silent Horizons / AMON ENB).                      //
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
float4  LightParameters;   // xy = sun NDC position, w = visibility
float4  tempF1;
float4  tempF2;
float4  tempF3;
float4  tempInfo1;
float4  tempInfo2;


//=== TEXTURES ===//

Texture2D   TextureColor;       // HDR scene
Texture2D   TextureDepth;       // Depth buffer
Texture2D   TextureMask;        // Sun occlusion mask


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

static const float PI = 3.14159265;
static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float SUN_CLIP = 0.001;

#include "Addons/Sunsprite_AtmosphericOptics.fxh"

// Theme-aware global intensity
#define SPRITE_INTENSITY  TF(ui_GlobalIntensity, GetTheme().sunspriteIntensity)


//=== UI PARAMETERS ===//

// --- Global ---

float ui_GlobalIntensity
<
    string UIName = "SPRITE | Global Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

int ui_ApertureBlades
<
    string UIName = "SPRITE | Aperture Blades";
    string UIWidget = "Spinner";
    int UIMin = 4;
    int UIMax = 9;
> = {6};

float ui_ApertureRotation
<
    string UIName = "SPRITE | Aperture Rotation (deg)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 360.0;
    float UIStep = 1.0;
> = {0.0};

float ui_ApertureRoundness
<
    string UIName = "SPRITE | Aperture Roundness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.1};

// --- Starburst ---

bool ui_StarburstEnable
<
    string UIName = "STARBURST | Enable";
> = {true};

float ui_StarburstIntensity
<
    string UIName = "STARBURST | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_StarburstScale
<
    string UIName = "STARBURST | Scale";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {1.0};

float ui_StarburstFalloff
<
    string UIName = "STARBURST | Falloff";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 10.0;
    float UIStep = 0.1;
> = {2.0};

// --- Sun Glare ---

bool ui_SunGlareEnable
<
    string UIName = "SUNGLARE | Enable";
> = {true};

float ui_SunGlareIntensity
<
    string UIName = "SUNGLARE | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.5};

float ui_SunGlareScale
<
    string UIName = "SUNGLARE | Scale";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.8};

// --- Ghosts ---

bool ui_GhostEnable
<
    string UIName = "GHOST | Enable";
> = {true};

float ui_GhostIntensity
<
    string UIName = "GHOST | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.8};

float ui_GhostCA
<
    string UIName = "GHOST | Chromatic Aberration";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.1};

// --- Anamorphic ---

bool ui_AnamorphicEnable
<
    string UIName = "ANAM | Enable";
> = {true};

float ui_AnamorphicIntensity
<
    string UIName = "ANAM | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_AnamorphicWidth
<
    string UIName = "ANAM | Width";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {2.0};

// --- Hoop ---

bool ui_HoopEnable
<
    string UIName = "HOOP | Enable";
> = {true};

float ui_HoopIntensity
<
    string UIName = "HOOP | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 10.0;
    float UIStep = 0.01;
> = {2.0};

float ui_HoopScale
<
    string UIName = "HOOP | Scale";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 20.0;
    float UIStep = 0.1;
> = {7.5};


//=== STRUCTURES ===//

struct VS_OUT
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
    float2 centered : TEXCOORD1;
    float3 sunCol   : TEXCOORD2;
    float  sunInt   : TEXCOORD3;
};


//=== HELPER FUNCTIONS ===//

// Get sun visibility and color — called from VS
float GetSunIntensity(float flareInt, out float3 sunColor)
{
    // 4-tap cross sample at sun position for color
    float2 sunUV = LightParameters.xy * float2(0.5, -0.5) + 0.5;

    float3 col = 0.0;
    col += TextureColor.SampleLevel(smpLinear, sunUV + float2( PixelSize.x * 1.5, 0), 0).rgb;
    col += TextureColor.SampleLevel(smpLinear, sunUV + float2(-PixelSize.x * 1.5, 0), 0).rgb;
    col += TextureColor.SampleLevel(smpLinear, sunUV + float2(0,  PixelSize.y * 1.5), 0).rgb;
    col += TextureColor.SampleLevel(smpLinear, sunUV + float2(0, -PixelSize.y * 1.5), 0).rgb;
    col *= 0.25;

    // Reinhard tonemap the sampled color
    sunColor = col / (col + 1.0);

    // Sun mask from ENB (visibility * occlusion)
    float mask = TextureMask.Load(int3(0, 0, 0)).x;
    mask = saturate(mask * LightParameters.w);

    // Intensity: luminance-based pow4 curve (AMON-proven — only strong sun triggers flares)
    float luma = dot(col, float3(0.2126, 0.7152, 0.0722));
    float intensity = saturate(luma / 2.5);
    intensity = intensity * intensity;
    intensity = intensity * intensity; // pow 4 curve

    return intensity * mask * flareInt;
}

float2 GetDirVec(float angleDeg)
{
    float rad = radians(angleDeg);
    float s, c;
    sincos(rad, s, c);
    return float2(c, s);
}


//=== VERTEX SHADERS ===//

void VS_Basic(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}

// Ghost sprite VS — position billboard along sun-center axis
VS_OUT VS_Ghost(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0,
                uniform float ghostOffset, uniform float ghostScale)
{
    VS_OUT OUT;
    OUT.texcoord = txcoord;

    float3 sunCol;
    float sunInt = GetSunIntensity(SPRITE_INTENSITY * ui_GhostIntensity, sunCol);
    OUT.sunCol = sunCol;
    OUT.sunInt = sunInt;
    OUT.centered = pos.xy;

    [branch]
    if (sunInt > SUN_CLIP)
    {
        // Position along sun-to-center line
        float paramLen = length(LightParameters.xy);
        float2 lightDir = (paramLen > 0.001) ? LightParameters.xy / paramLen * pow(paramLen, 0.5) : 0.0;
        float2 offset = lightDir * -ghostOffset * ScreenSize.z * 0.75;

        // Rotate by aperture rotation
        float2 rotDir = GetDirVec(ui_ApertureRotation);
        float2x2 rotMat = float2x2(rotDir.x, -rotDir.y, rotDir.y, rotDir.x);

        float scale = ghostScale * 0.1;
        float2 rotPos = mul(rotMat, pos.xy);
        rotPos.y *= ScreenSize.z;
        OUT.centered = rotPos / max(scale, 0.001);

        OUT.pos = float4(rotPos * scale + offset, 0.5, 1.0);
    }
    else
    {
        OUT.pos = float4(-10.0, -10.0, 0.5, 1.0); // Off-screen
    }

    return OUT;
}

// Anamorphic VS
VS_OUT VS_Anam(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0,
               uniform float anamOffset)
{
    VS_OUT OUT;
    OUT.texcoord = txcoord;

    float3 sunCol;
    float sunInt = GetSunIntensity(SPRITE_INTENSITY * ui_AnamorphicIntensity, sunCol);
    OUT.sunCol = sunCol;
    OUT.sunInt = sunInt;
    OUT.centered = pos.xy;

    [branch]
    if (sunInt > SUN_CLIP)
    {
        float2 offset = LightParameters.xy * anamOffset;
        float2 scale = float2(ui_AnamorphicWidth, 0.05 * ScreenSize.z);

        OUT.pos = float4(pos.xy * scale + offset, 0.5, 1.0);
    }
    else
    {
        OUT.pos = float4(-10.0, -10.0, 0.5, 1.0);
    }

    return OUT;
}

// Starburst VS
VS_OUT VS_Starburst(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0)
{
    VS_OUT OUT;
    OUT.texcoord = txcoord;

    float3 sunCol;
    float sunInt = GetSunIntensity(SPRITE_INTENSITY * ui_StarburstIntensity, sunCol);
    OUT.sunCol = sunCol;
    OUT.sunInt = sunInt;
    OUT.centered = pos.xy;

    [branch]
    if (sunInt > SUN_CLIP)
    {
        float2 scaledPos = pos.xy * ui_StarburstScale;
        scaledPos.y *= ScreenSize.z;
        OUT.pos = float4(scaledPos + LightParameters.xy, 0.5, 1.0);
    }
    else
    {
        OUT.pos = float4(-10.0, -10.0, 0.5, 1.0);
    }

    return OUT;
}

// Sun glare VS
VS_OUT VS_SunGlare(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0)
{
    VS_OUT OUT;
    OUT.texcoord = txcoord;

    float3 sunCol;
    float sunInt = GetSunIntensity(SPRITE_INTENSITY * ui_SunGlareIntensity, sunCol);
    OUT.sunCol = sunCol;
    OUT.sunInt = sunInt;
    OUT.centered = pos.xy;

    [branch]
    if (sunInt > SUN_CLIP)
    {
        float2 scaledPos = pos.xy * float2(ui_SunGlareScale, ui_SunGlareScale * ScreenSize.z);
        OUT.pos = float4(scaledPos + LightParameters.xy, 0.5, 1.0);
    }
    else
    {
        OUT.pos = float4(-10.0, -10.0, 0.5, 1.0);
    }

    return OUT;
}

// Hoop VS
VS_OUT VS_Hoop(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0)
{
    VS_OUT OUT;
    OUT.texcoord = txcoord;

    float3 sunCol;
    float sunInt = GetSunIntensity(SPRITE_INTENSITY * ui_HoopIntensity, sunCol);
    OUT.sunCol = sunCol;
    OUT.sunInt = sunInt;

    [branch]
    if (sunInt > SUN_CLIP)
    {
        float sunDist = length(LightParameters.xy) * 1.5;
        float2 offset = LightParameters.xy * -0.7;

        float2 rotDir = GetDirVec(ui_ApertureRotation);
        float2x2 rotMat = float2x2(rotDir.x, -rotDir.y, rotDir.y, rotDir.x);

        float scale = ui_HoopScale * 0.1 * max(sunDist, 0.01);
        float2 rotPos = mul(rotMat, pos.xy);
        rotPos.y *= ScreenSize.z;

        OUT.centered = rotPos / max(scale, 0.001);
        OUT.pos = float4(rotPos * scale + offset, 0.5, 1.0);
    }
    else
    {
        OUT.centered = 0.0;
        OUT.pos = float4(-10.0, -10.0, 0.5, 1.0);
    }

    return OUT;
}


//=== PIXEL SHADERS ===//

// Ghost PS — N-gon aperture-shaped ghost with CA and edge highlight
float4 PS_Ghost(VS_OUT IN, uniform float ghostWeight) : SV_Target
{
    clip(IN.sunInt - SUN_CLIP);
    if (!ui_GhostEnable) return 0.0;

    float2 c = IN.centered;
    float dist = length(c);

    // N-gon aperture shape (replaces simple circle)
    float angle = atan2(c.y, c.x) + radians(ui_ApertureRotation);
    float sector = 2.0 * PI / (float)ui_ApertureBlades;
    float a = frac(angle / sector + 0.5) * sector - sector * 0.5;
    float polyRadius = cos(sector * 0.5) / cos(a);
    float effectiveRadius = lerp(polyRadius, 1.0, ui_ApertureRoundness);
    float normDist = dist / max(effectiveRadius, 0.001);

    float inside = 1.0 - smoothstep(0.85, 1.0, normDist);
    clip(inside - 0.001);

    // Edge ring highlight (bright rim at aperture edge — coating reflection)
    float edgeRing = smoothstep(0.6, 0.85, normDist) * (1.0 - smoothstep(0.85, 1.0, normDist));
    edgeRing *= 0.5;

    // Chromatic aberration with N-gon aware offset
    float2 caVec = (dist > 0.001) ? normalize(c) * ui_GhostCA : 0.0;
    float3 caShape;
    caShape.r = 1.0 - smoothstep(0.85, 1.0, length(c - caVec * 0.5) / max(effectiveRadius, 0.001));
    caShape.g = inside;
    caShape.b = 1.0 - smoothstep(0.85, 1.0, length(c + caVec * 0.5) / max(effectiveRadius, 0.001));

    // Feathering + weight
    float3 feather = smoothstep(0.0, 0.05, caShape);
    float3 weight = smoothstep(6.0 * pow(ghostWeight, 3.0), 0.0, 1.0 - caShape);

    float3 color = IN.sunInt * IN.sunCol * (feather * weight + edgeRing);
    return float4(max(color, 0.0), 1.0);
}

// Anamorphic PS — Gaussian cross-section with smooth exponential falloff
float4 PS_Anam(VS_OUT IN) : SV_Target
{
    clip(IN.sunInt - SUN_CLIP);
    if (!ui_AnamorphicEnable) return 0.0;

    // Exponential horizontal falloff (smoother than linear)
    float hFade = exp(-abs(IN.centered.x) * 1.5);
    // Gaussian vertical cross-section (more natural than pow3)
    float vShape = exp(-IN.centered.y * IN.centered.y * 20.0);

    // Subtle spectral dispersion: red extends wider, blue narrower
    float3 hSpectral;
    hSpectral.r = exp(-abs(IN.centered.x) * 1.3);
    hSpectral.g = hFade;
    hSpectral.b = exp(-abs(IN.centered.x) * 1.7);

    float3 color = hSpectral * vShape * IN.sunInt * IN.sunCol;
    return float4(max(color, 0.0), 1.0);
}

// Starburst PS
float4 PS_Starburst(VS_OUT IN) : SV_Target
{
    clip(IN.sunInt - SUN_CLIP);
    if (!ui_StarburstEnable) return 0.0;

    float2 c = IN.centered;
    float dist = length(c);
    clip(1.0 - dist);
    if (dist < 0.001) return 0.0;

    float2 dir = c / dist;

    // Find minimum angular distance to any aperture vertex
    float minVertDist = 1.0;
    int blades = ui_ApertureBlades;
    float rotRad = radians(ui_ApertureRotation);

    [unroll]
    for (int i = 0; i < 9; i++)
    {
        if (i >= blades) break;
        float vertAngle = rotRad + float(i) * 2.0 * PI / float(blades);
        float2 vertDir = float2(cos(vertAngle), sin(vertAngle));
        minVertDist = min(minVertDist, distance(dir, vertDir));
        minVertDist = min(minVertDist, distance(-dir, vertDir)); // Mirror
    }

    // Ray mask
    float burstWidth = pow(max(dist, 0.01), 0.7) * 200.0;
    float starMask = pow(saturate(1.0 - minVertDist), burstWidth * (1.0 - ui_ApertureRoundness));

    // Spectral radial falloff: red extends further (diffraction wavelength dependence)
    float3 spectralFalloff;
    spectralFalloff.r = pow(saturate(1.0 - dist * 0.9), ui_StarburstFalloff);
    spectralFalloff.g = pow(saturate(1.0 - dist), ui_StarburstFalloff);
    spectralFalloff.b = pow(saturate(1.0 - dist * 1.15), ui_StarburstFalloff);

    float3 color = starMask * spectralFalloff * IN.sunInt * IN.sunCol;
    return float4(max(color, 0.0), 1.0);
}

// Sun glare PS — with Hestroffer limb darkening (per-channel wavelength-dependent falloff)
// Solar limb appears redder because blue photons originate from higher, cooler layers.
float4 PS_SunGlare(VS_OUT IN) : SV_Target
{
    clip(IN.sunInt - SUN_CLIP);
    if (!ui_SunGlareEnable) return 0.0;

    float dist = length(IN.centered);
    float glow = smoothstep(1.0, 0.0, dist);
    glow *= glow;

    // Hestroffer limb darkening: cos(theta) = sqrt(1 - r^2) for unit sphere
    // Maps billboard distance to surface normal angle on a sphere
    float cosTheta = sqrt(max(1.0 - dist * dist, 0.0));
    float3 limbDark = HestrofferLimbDarkening(cosTheta);

    // Apply: blue falls off faster at edges, producing warm-tinted limb glow
    float3 color = glow * limbDark * IN.sunInt * IN.sunCol;
    return float4(max(color, 0.0), 1.0);
}

// Hoop PS
float4 PS_Hoop(VS_OUT IN) : SV_Target
{
    clip(IN.sunInt - SUN_CLIP);
    if (!ui_HoopEnable) return 0.0;

    float dist = length(IN.centered);
    float inside = 1.0 - saturate(dist);
    clip(inside - 0.001);

    // Ring: only near the edge of the shape
    float ringInner = smoothstep(0.0, 0.05, inside);
    float ringOuter = smoothstep(0.3, 0.0, inside);
    float ring = ringInner * ringOuter;

    float3 color = ring * IN.sunInt * IN.sunCol;
    return float4(max(color, 0.0), 1.0);
}


//=== TECHNIQUE MACROS ===//

#define GHOST_PASS(IDX, OFFSET, SCALE, WEIGHT) \
pass Ghost##IDX { \
    SetVertexShader(CompileShader(vs_5_0, VS_Ghost(OFFSET, SCALE))); \
    SetPixelShader(CompileShader(ps_5_0, PS_Ghost(WEIGHT))); }

#define ANAM_PASS(IDX, OFFSET) \
pass Anam##IDX { \
    SetVertexShader(CompileShader(vs_5_0, VS_Anam(OFFSET))); \
    SetPixelShader(CompileShader(ps_5_0, PS_Anam())); }


//=== TECHNIQUE ===//

technique11 EotE_Sunsprite <string UIName = "EotE: Sun Sprite";>
{
    // Atmospheric aureole (Mie forward-scatter)
    pass Aureole
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Aureole()));
    }

    // Ciliary corona (spectral diffraction rings)
    pass Corona
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Corona()));
    }

    // Ice crystal halos + parhelia
    pass IceHalos
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_IceHalos()));
    }

    // Anamorphic streaks
    ANAM_PASS(1, 1.0)
    ANAM_PASS(2, 0.5)
    ANAM_PASS(3, -0.8)

    // Sun glare (closest to sun)
    pass SunGlare
    {
        SetVertexShader(CompileShader(vs_5_0, VS_SunGlare()));
        SetPixelShader(CompileShader(ps_5_0, PS_SunGlare()));
    }

    // Starburst
    pass Starburst
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Starburst()));
        SetPixelShader(CompileShader(ps_5_0, PS_Starburst()));
    }

    // Ghost reflections
    GHOST_PASS(1, 0.8, 3.0, 0.8)
    GHOST_PASS(2, 1.2, 2.0, 0.6)
    GHOST_PASS(3, 1.8, 4.0, 0.7)
    GHOST_PASS(4, 2.5, 1.5, 0.9)
    GHOST_PASS(5, 3.2, 2.5, 0.5)
    GHOST_PASS(6, 0.5, 5.0, 0.4)

    // Hoop ring
    pass Hoop
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Hoop()));
        SetPixelShader(CompileShader(ps_5_0, PS_Hoop()));
    }

    // Heiligenschein (antisolar dew-drop retroreflection)
    pass Heiligenschein
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_Heiligenschein()));
    }

    // Procedural star field with scintillation
    pass StarField
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_StarField()));
    }

    // Light pillar (ice crystal vertical column)
    pass LightPillar
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_LightPillar()));
    }

    // Belt of Venus + Earth shadow (anti-twilight band)
    pass BeltOfVenus
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_BeltOfVenus()));
    }

    // Green flash (differential refraction at sunrise/sunset)
    pass GreenFlash
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_GreenFlash()));
    }

    // Atmospheric disc refraction (horizon flattening)
    pass DiscRefraction
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Basic()));
        SetPixelShader(CompileShader(ps_5_0, PS_DiscRefraction()));
    }
}
