//=============================================================================
//  PrePass_SnowCover.fxh — Screen-Space Snow Accumulation v1.0
//
//  Adds a snow layer to upward-facing surfaces using depth-reconstructed
//  normals.  Runs as a sub-technique pass in enbeffectprepass.fx after
//  SSS and Stylization passes.
//
//  Snow coverage is determined by surface normal Y component (world up),
//  with smooth threshold transitions, distance fade, and optional sparkle
//  noise for specular highlights.
//
//  SkyrimBridge integration:
//    - SB_Precip_Surface.z  (snow accumulation amount)
//    - SB_Atmos_Ambient.rgb (ambient light for snow illumination)
//    - SB_Weather_Flags.w   (isSnowy flag)
//
//  Placed in prepass so snow-covered scene receives natural DOF, bloom,
//  and tonemapping — accumulation with physically correct lighting on top.
//
//  Disabled by default (ui_SnowEnable = false).
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef _PREPASS_SNOWCOVER_
#define _PREPASS_SNOWCOVER_

#define SNOW_LOADED 1


//=== UI PARAMETERS ===//

bool ui_SnowEnable
<
    string UIName = "06 SNOW ── Screen-Space Snow Accumulation";
> = {false};

float ui_SnowIntensity
<
    string UIName = "06 SNOW | Overall Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.7};

float ui_SnowThreshold
<
    string UIName = "06 SNOW | Normal Y Threshold (upward facing)";
    string UIWidget = "Spinner";
    float UIMin = 0.3;
    float UIMax = 0.9;
    float UIStep = 0.01;
> = {0.55};

float ui_SnowSoftness
<
    string UIName = "06 SNOW | Edge Softness";
    string UIWidget = "Spinner";
    float UIMin = 0.01;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

float ui_SnowDepthFade
<
    string UIName = "06 SNOW | Distance Fade Start (depth)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.6};

float3 ui_SnowColor
<
    string UIName = "06 SNOW | Snow Color Tint";
    string UIWidget = "Color";
> = {0.92, 0.94, 1.0};

float ui_SnowSparkle
<
    string UIName = "06 SNOW | Sparkle Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.25};

float ui_SnowRoughness
<
    string UIName = "06 SNOW | Surface Roughness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.6};

bool ui_SnowUseSkyrimBridge
<
    string UIName = "06 SNOW | Use SkyrimBridge Precipitation Data";
> = {true};

float ui_SnowSBScale
<
    string UIName = "06 SNOW | SB Snow Amount Scale";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 3.0;
    float UIStep = 0.05;
> = {1.5};


//=== NOISE FUNCTIONS ===//

// Hash-based pseudo-random for sparkle generation.
// Uses a well-distributed 2D → 1D hash (Hugo Elias / Dave Hoskins variant).
float SnowHash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// 2D value noise for snow surface micro-texture
float SnowValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    // Hermite smoothing
    f = f * f * (3.0 - 2.0 * f);

    float a = SnowHash(i);
    float b = SnowHash(i + float2(1.0, 0.0));
    float c = SnowHash(i + float2(0.0, 1.0));
    float d = SnowHash(i + float2(1.0, 1.0));

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// FBM for snow surface variation (2 octaves, cheap)
float SnowFBM(float2 p)
{
    return SnowValueNoise(p) * 0.667 + SnowValueNoise(p * 2.13 + 17.5) * 0.333;
}


//=== NORMAL RECONSTRUCTION ===//
//
// Reconstructs view-space normal from depth buffer using the Bavoil-Sainz
// smallest-derivative method (same as host NormalFromDepth, but inlined
// here to be self-contained as an addon).
//
// Uses 4 depth taps around the center pixel, selects the pair with the
// smallest depth difference for more robust normals at depth discontinuities.

float3 SnowReconstructNormal(float2 uv)
{
    float2 texel = PixelSize;

    float cZ = GetLinearDepth(uv);
    float rZ = GetLinearDepth(uv + float2(texel.x, 0.0));
    float lZ = GetLinearDepth(uv - float2(texel.x, 0.0));
    float dZ = GetLinearDepth(uv + float2(0.0, texel.y));
    float uZ = GetLinearDepth(uv - float2(0.0, texel.y));

    float3 cP = ViewPosFromUV(uv, cZ);

    // Select smallest delta for X axis
    float3 ddxR = ViewPosFromUV(uv + float2(texel.x, 0.0), rZ) - cP;
    float3 ddxL = cP - ViewPosFromUV(uv - float2(texel.x, 0.0), lZ);
    float3 dx = (abs(ddxR.z) < abs(ddxL.z)) ? ddxR : ddxL;

    // Select smallest delta for Y axis
    float3 ddyD = ViewPosFromUV(uv + float2(0.0, texel.y), dZ) - cP;
    float3 ddyU = cP - ViewPosFromUV(uv - float2(0.0, texel.y), uZ);
    float3 dy = (abs(ddyD.z) < abs(ddyU.z)) ? ddyD : ddyU;

    return normalize(cross(dy, dx));
}


//=== SNOW ACCUMULATION ===//

// Compute snow coverage mask for a given surface normal and depth.
// Returns [0, 1] snow amount.
float ComputeSnowMask(float3 viewNormal, float depth, float2 uv)
{
    // --- Normal-based upward facing check ---
    // View-space normal: in ENB's coordinate system, Y-up means
    // viewNormal.y > 0 is upward-facing (camera looking -Z, Y is up).
    // Use the Y component to determine how upward the surface faces.
    float upFacing = viewNormal.y;

    // Smoothstep transition around the threshold.
    // Higher softness = wider transition band.
    float halfSoft = ui_SnowSoftness * 0.5;
    float lo = ui_SnowThreshold - halfSoft;
    float hi = ui_SnowThreshold + halfSoft;
    float normalMask = smoothstep(lo, hi, upFacing);

    // --- Depth fade ---
    // Fade snow at distance to avoid far-field artifacts.
    // ui_SnowDepthFade controls where fade begins (0 = immediate, 1 = never).
    float fadeStart = ui_SnowDepthFade;
    float fadeEnd = saturate(fadeStart + 0.15);
    float depthFade = 1.0 - smoothstep(fadeStart, fadeEnd, depth);

    // --- Snow surface micro-variation ---
    // Adds natural unevenness to the snow boundary using world-scaled noise.
    float2 screenRes = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);
    float2 noiseUV = uv * screenRes * 0.15;
    float surfaceNoise = SnowFBM(noiseUV);
    // Modulate the normal mask with subtle noise to break up uniform edges
    float noiseMod = lerp(0.85, 1.15, surfaceNoise);
    normalMask *= noiseMod;
    normalMask = saturate(normalMask);

    return normalMask * depthFade;
}


// Compute sparkle highlights for snow surface.
// Returns HDR sparkle intensity (can exceed 1.0 for bloom pickup).
float ComputeSnowSparkle(float2 uv, float snowMask)
{
    if (ui_SnowSparkle < 0.001) return 0.0;

    // High-frequency hash grid for sparkle points
    float2 screenRes = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);
    float2 sparkleUV = uv * screenRes;

    // Animate sparkle pattern slowly with Timer
    float timePhase = Timer.x * 0.0003;
    float sparkleRaw = SnowHash(sparkleUV + float2(timePhase, timePhase * 0.7));

    // Only the brightest hash values become sparkle points
    // Roughness controls the sparkle density: low roughness = more sparkles
    float sparkleThresh = lerp(0.97, 0.993, ui_SnowRoughness);
    float sparkle = saturate((sparkleRaw - sparkleThresh) / (1.0 - sparkleThresh));

    // Square for sharper highlights
    sparkle *= sparkle;

    // HDR sparkle intensity (allow values > 1 so bloom picks up the sparkles)
    float sparkleStrength = ui_SnowSparkle * 3.0;

    return sparkle * sparkleStrength * snowMask;
}


//=== PIXEL SHADER ===//

float4 PS_SnowCover(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 original = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;

    // Early out if disabled
    if (!ui_SnowEnable)
        return float4(original, 1.0);

    float depth = GetLinearDepth(txcoord);

    // Sky pixels: no snow
    if (depth > 0.99)
        return float4(original, 1.0);

    // --- Compute intensity with SkyrimBridge modulation ---
    float intensity = ui_SnowIntensity;
    float3 snowAmbient = float3(1.0, 1.0, 1.0);

    // SkyrimBridge integration: scale by game precipitation data
    [branch] if (ui_SnowUseSkyrimBridge && SB_IsActive())
    {
        // SB_Precip_Surface.z = snow accumulation [0,1]
        // SB_Weather_Flags.w  = isSnowy (0/1)
        float sbSnow = SB_Precip_Surface.z;
        float isSnowy = SB_Weather_Flags.w;

        // Scale intensity: if the game says no snow, reduce dramatically
        // but don't zero out completely (allow artistic override via intensity)
        float sbFactor = lerp(0.15, 1.0, saturate(sbSnow * ui_SnowSBScale));
        sbFactor *= lerp(0.3, 1.0, isSnowy);
        intensity *= sbFactor;

        // Use game ambient color to tint the snow illumination
        // Snow reflects ambient light — brighter ambient = brighter snow
        float ambIntensity = SB_Atmos_Ambient.a;
        float3 ambColor = SB_Atmos_Ambient.rgb;
        float ambLum = dot(ambColor, float3(0.2126, 0.7152, 0.0722));
        // Normalize ambient color to prevent over-darkening, then scale
        snowAmbient = (ambLum > 0.01) ? (ambColor / ambLum) : float3(1.0, 1.0, 1.0);
        // Blend between pure white and ambient-tinted based on ambient strength
        snowAmbient = lerp(float3(1.0, 1.0, 1.0), snowAmbient, saturate(ambIntensity * 0.5));
    }

    // Nothing to do if intensity is zero
    if (intensity < 0.001)
        return float4(original, 1.0);

    // --- Reconstruct surface normal ---
    float3 viewNormal = SnowReconstructNormal(txcoord);

    // --- Compute snow coverage ---
    float snowMask = ComputeSnowMask(viewNormal, depth, txcoord);
    snowMask *= intensity;

    // Nothing to blend
    if (snowMask < 0.001)
        return float4(original, 1.0);

    // --- Snow surface color ---
    // Base snow color modulated by ambient lighting
    float3 snowCol = ui_SnowColor * snowAmbient;

    // Add micro-texture variation for roughness appearance
    float2 roughUV = txcoord * float2(ScreenSize.x, ScreenSize.x * ScreenSize.w) * 0.4;
    float roughNoise = SnowFBM(roughUV + 42.0);
    // Roughness modulates snow brightness subtly
    float roughMod = lerp(0.9, 1.1, roughNoise * ui_SnowRoughness);
    snowCol *= roughMod;

    // --- Preserve scene luminance relationship ---
    // In HDR, the scene color may be much brighter than 1.0.
    // Scale snow brightness relative to original scene luminance so it
    // integrates naturally rather than clamping bright areas to white.
    float origLum = dot(original, float3(0.2126, 0.7152, 0.0722));
    // Snow is reflective: base brightness is related to scene exposure.
    // Use a mild boost (snow is brighter than most surfaces) but preserve HDR range.
    float snowLum = max(origLum * 1.3, 0.15);
    snowCol *= snowLum;

    // --- Sparkle highlights ---
    float sparkle = ComputeSnowSparkle(txcoord, snowMask);
    // Sparkles are additive and pure white (specular highlight)
    float3 sparkleColor = float3(1.0, 1.0, 1.0) * sparkle * snowLum;

    // --- Final blend ---
    // Lerp between original and snow color, then add sparkle on top
    float3 result = lerp(original, snowCol, snowMask);
    result += sparkleColor;

    return float4(result, 1.0);
}


#endif // _PREPASS_SNOWCOVER_
