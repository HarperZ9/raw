#pragma once

#include "ReShadeUI.fxh"

#define hide true
#define hideDebug true
#define hideTech false

uniform int TonemapType <
    ui_type = "combo";
    hidden = true;
    ui_label = "Tonemap Type";
    ui_items = "ReinHard\0Filmic\0ACES\0None\0";
> = 1;

uniform float GammaCorrection <
    ui_type = "drag";
    ui_min = 0.5;
    ui_max = 3.0;
    ui_step = 0.01;
    hidden = true;
    ui_label = "Gamma";
> = 2.2f;

uniform float4x4 InverseView < hidden = hideDebug; >;
uniform float4x4 WorldView < hidden = hideDebug; >;
uniform float4x4 WorldInverseViewProjection < hidden = hideDebug; >;
uniform float4x4 WorldViewProjection < hidden = hideDebug; >;
uniform float4x4 Projection < hidden = hideDebug; >;
uniform float4x4 WorldViewPrev < hidden = hideDebug; >;
uniform float4x4 WorldViewProjectionPrev < hidden = hideDebug; >;
uniform float4x4 ProjectionPrev < hidden = hideDebug; >;
uniform float4x4 ViewProjectionPrev < hidden = hideDebug; >;
uniform bool FirstPerson < hidden = hideDebug; > = false;
uniform float2 Clips < hidden = hideDebug; > = float2(0.0f, 0.0f);

uniform float3 AzimuthWestColor <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 AzimuthEastColor <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 AzimuthTransitionColor <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 ZenithColor <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 ZenithTransitionColor <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float4 SkyPlaneColor <ui_type = "color"; hidden = hideDebug; > = float4(0.0f, 0.0f, 0.0f, 0.0f);

uniform float3 SunColor <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 SunColorHdr <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 SunDiscColorHdr <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 SmallCloudColorHdr <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 MoonColor <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);

uniform float3 CloudBaseMinusMidColor <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 CloudMidColor <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 CloudShadowMinusBaseColorTimesShadowStrength <ui_type = "color"; hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);

uniform float3 SunPosition < hidden = hideDebug; >;
uniform float3 SunDirection < hidden = hideDebug; >;
uniform float3 MoonPosition < hidden = hideDebug; >;
uniform float3 MoonDirection < hidden = hideDebug; >;
uniform float3 GameTime < hidden = hideDebug; >;
uniform float3 WeatherData < hidden = hideDebug; >;
uniform float3 WindData < hidden = hideDebug; >;
uniform float3 NOffsetA < hidden = hideDebug; >;
uniform float3 NOffsetB < hidden = hideDebug; >;
uniform float3 NOffsetC < hidden = hideDebug; >;
uniform float3 cloudShift < hidden = hideDebug; >;
uniform float3 nsOffsetA < hidden = hideDebug; >;
uniform float3 nsOffsetB < hidden = hideDebug; >;
uniform float3 nsOffsetC < hidden = hideDebug; >;
uniform float3 nsOffsetD < hidden = hideDebug; >;
uniform float3 nsOffsetE < hidden = hideDebug; >;
uniform float3 clampBright <    ui_type = "color";ui_min = 0.0f;ui_max = 1.0f; hidden=hideDebug;> = float3(0.0f,0.0f,0.0f);
uniform float3 ColorAtSun <     ui_type = "color";ui_min = 0.0f;ui_max = 1.0f; hidden=hideDebug;> = float3(0.0f,0.0f,0.0f);
uniform float3 ColorBase <      ui_type = "color";ui_min = 0.0f;ui_max = 1.0f; hidden=hideDebug;> = float3(0.0f,0.0f,0.0f);
uniform float topLayerMultiplier < hidden = hideDebug; > = 0.0f;
uniform float3 L_CenterRandomRange < hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);

uniform float AuroraVisibility < hidden = hideDebug; > = 0.0f;


uniform float4x4 cs1 < hidden = hideDebug; >;
uniform float4x4 cs2 < hidden = hideDebug; >;
uniform float4x4 cs3 < hidden = hideDebug; >;
uniform float4x4 cs4 < hidden = hideDebug; >;
uniform float4x4 cs5 < hidden = hideDebug; >;
uniform float4x4 cs6 < hidden = hideDebug; >;
uniform float4x4 cs7 < hidden = hideDebug; >;
uniform float4x4 cs8 < hidden = hideDebug; >;
uniform float4x4 cs9 < hidden = hideDebug; >;
uniform float4x4 cs10 < hidden = hideDebug; >;
uniform float4x4 cs11 < hidden = hideDebug; >;

uniform float2 BackBufferRes < hidden = hideDebug; >;
uniform float ReflectionRes < hidden = hideDebug; >;

uniform float4x4 future < hidden = hideDebug; >;
#define TransitionPercent (future[2][0])

#define InScatterExp (cs4[2][2])
#define InScatterMul (cs4[2][3])

#define L_Probability (cs7[1][1])
#define L_Frequency (cs7[1][3])
#define L_StrobeSpeed (cs7[2][3])
#define L_StrobeMul (cs7[3][3])
#define L_Count (cs8[3][2])
#define L_Curve (cs9[1][2])
#define L_Size (cs9[1][3])

#define L_CenterX (cs9[0][0])
#define L_CenterY (cs9[0][1])
#define L_CenterZ (cs9[0][2])

#define BottomDetailMul (cs9[0][3])
#define BottomDetailLow (cs9[1][0])
#define BottomDetailHigh (cs9[1][1])

#define PowderExp (cs6[2][1])
#define PowderStrength (cs6[2][0])

uniform bool SEPARATOR0 < __UNIFORM_INPUT_BOOL1 ui_label = "Density and Coverage"; hidden = hide;> = false;
#define CloudyCoverage (cs1[0][0])
#define coverageBottom (cs1[0][1])
#define cloudMapStrength (cs1[0][2])
#define cloudmapScale (cs2[0][1])
#define cloudmapZScale (cs2[0][2])
#define cloudmapOffset (cs2[0][3])
#define detailDensA (cs1[0][3])
#define detailDensB (cs1[1][0])
#define altitudeOffsetB (cs3[3][1])
#define densExp (cs6[2][2])
#define detailDensC (cs1[1][1])
#define C_Smooth (cs6[2][3])
#define C_Smooth2 (cs6[3][0])
#define C_Smooth3 (cs6[3][1])
#define C_Contrast (cs6[3][2])
#define densExp2 (cs6[3][3])
#define detailScaleA (cs1[1][2])
#define detailScaleB (cs1[1][3])
#define detailScaleC (cs1[2][0])
#define detailZScaleA (cs3[2][2])
#define detailZScaleB (cs3[2][3])
#define detailZScaleC (cs3[3][0])
#define FutureDetailScaleA (future[0][2])
#define FutureDetailScaleAZ (future[0][3])
#define FutureDetailScaleB (future[1][0])
#define FutureDetailScaleBZ (future[1][1])
#define FutureDetailScaleC (future[1][2])
#define FutureDetailScaleCZ (future[1][3])

uniform bool SEPARATOR11 < __UNIFORM_INPUT_BOOL1 ui_label = "Scale"; hidden = hide; > = false;
#define densCA (cs3[0][2])
#define densCB (cs3[0][3])
#define scaleX (cs3[1][2])
#define scaleY (cs3[1][3])
#define FutureScaleX (future[0][0])
#define FutureScaleY (future[0][1])
#define vertS (cs3[2][0])
#define FutureScaleXZ (future[2][1])
#define chunkBZScale (cs3[2][1])
#define FutureScaleYZ (future[2][2])

uniform bool SEPARATOR1 < __UNIFORM_INPUT_BOOL1 ui_label = "Steps Configuration"; hidden = hide; > = false;
#define maxStepCount (int(cs1[2][3]))
#define stepMult (int(cs1[3][0]))
#define minStep (int(cs1[3][1]))
#define maxStep (int(cs1[3][2]))
#define mainNoise (cs5[2][1])
#define alphaCut (cs1[3][3])
#define densCut (cs5[1][3])
#define fadeDist (cs2[0][0])
#define dfLimit (cs5[2][0])
#define RMMask1 (cs5[2][3])
#define RMMask2 (cs5[3][0])
#define MaskDistOffset (cs5[3][1])

uniform bool SEPARATOR2 < __UNIFORM_INPUT_BOOL1 ui_label = "Volume Configuration"; hidden = hide; > = false;
#define cloudHeight (cs2[1][0])
#define volumeBoxX (cs2[1][1])
#define volumeBoxY (cs2[1][2])
#define volumeShapeX (cs2[1][3])
#define volumeShapeY (cs2[2][0])
#define volumeShapeZ (cs2[2][1])
#define volumeShapeThic (cs2[2][2])
#define rangeTotal (cs7[0][0])
#define rangeBottom (cs7[0][1])
#define rangeTop (cs7[0][2])
#define softMul (cs7[0][3])
#define solidness (cs3[1][0])
#define solidnessBottom (cs3[1][1])

uniform bool SEPARATOR3 < __UNIFORM_INPUT_BOOL1 ui_label = "Shadow Configuration"; hidden = hide; > = false;
#define shadowStepLength (cs2[2][3])
#define shadowSteps (cs7[1][0])
#define shadowNoise (cs5[2][2])
#define shadowExpand (cs2[3][0])
#define shadowStrength (cs2[3][1])
#define shadowDetailStrength (cs2[3][2])
#define shadowThreshold (cs7[1][2])
#define shadowLimit (cs2[3][3])
#define shadowEarlyExit (cs3[0][0])
#define shadowEarlyExitApprox (cs3[0][1])

uniform bool SEPARATOR4 < __UNIFORM_INPUT_BOOL1 ui_label = "Distortion Configuration"; hidden = hide; > = false;
#define distMaxAngle (cs3[3][2])
#define distStrength (cs3[3][3])
#define distBumpStrength (cs4[0][0])
#define distSmallBumpStrength (cs4[0][1])

uniform bool SEPARATOR41 < __UNIFORM_INPUT_BOOL1 ui_label = "Earth Configuration"; hidden = hide; > = false;
#define earthShadVal1 (cs4[1][3])
#define earthShadVal2 (cs4[2][0])
#define earthShadVal3 (cs4[2][1])
#define earthShadVal4 (cs4[0][3])
#define earthShadVal5 (cs4[1][0])
#define earthShadVal6 (cs4[1][1])
#define earthShadVal7 (cs4[1][2])
#define cBallRad (cs6[1][1])
#define cBallClipBegin (cs6[1][2])
#define cBallClipEnd (cs6[1][3])
#define atmoConvDist (cs6[0][2])
#define atmDensityConfig (cs6[0][3])
uniform float atmHeight <    ui_type = "slider";ui_min = 0.0f;ui_max = 100000.0f; hidden=hide;> = 0.6f;

uniform bool SEPARATOR5 < __UNIFORM_INPUT_BOOL1 ui_label = "Blending Configuration"; hidden = hide; > = false;
#define atmoDistance2 (cs4[2][2])
#define atmoMaxBlend2 (cs4[2][3])
#define AtmoDistanceExp (cs7[1][3])
#define sunScale (cs4[3][2])
#define sunScaleExp (cs4[3][3])
#define depthMul (cs5[0][3])
uniform float depthFill <           ui_type = "slider";ui_min = 0.0f;ui_max = 1.0f; hidden=hide;> = 1.0f;
#define borderRadius (cs6[0][0])
#define borderRadius2 (cs6[0][1])

uniform bool SEPARATOR6 < __UNIFORM_INPUT_BOOL1 ui_label = "Shading Configuration"; hidden = hide; > = false;
#define cloudBrightness (cs4[3][1])

#define sunStrength (cs7[2][0])
#define mieStrengthConfig (cs1[2][1])

#define HGClose (cs5[0][0])
#define HGMid (cs8[3][2])
#define HGFar (cs8[3][3])
#define HGStrengthTop (cs5[0][1])
#define HGMu (cs5[0][2])

#define blur (cs5[1][0])
#define timeBoost (cs5[1][1])
#define timeSkip (cs5[1][2])

uniform int usePowder <             ui_type = "slider";ui_min = 0;ui_max = 2; hidden=hide;> = 1;
uniform float pwd <                 ui_type = "slider";ui_min = 0.0f;ui_max = 10.0f; hidden=hide;> = 0.0f;
uniform float pwd2 <                ui_type = "slider";ui_min = 0.0f;ui_max = 1.0f; hidden=hide;> = 0.0f;

uniform bool SEPARATOR7 < __UNIFORM_INPUT_BOOL1 ui_label = "Other Configuration"; hidden = hide; > = false;
#define deNoiseLevel (cs5[3][2])
#define cloudsAberration (cs5[3][3])
#define cloudsSharpen (cs1[2][2])

#define cableShit (cs6[1][0])
#define AlphaBlend (cs7[2][1])

#define shadowMul (cs7[2][2])

#define GR_Intensity (cs7[3][0])
#define GR_ScatterG (cs7[3][1])
#define GR_StepMul (cs7[3][2])

#define groundAtmoStartDistance (cs8[0][0])
#define groundAtmoBlendClose (cs8[0][1])
#define groundAtmoBlendFar (cs8[0][2])
#define groundAtmoBlendCurve (cs8[0][3])
#define groundAtmoBlendMax (cs8[1][0])
#define skyAtmoStartDistance (cs8[1][1])
#define skyAtmoBlendClose (cs8[1][2])
#define skyAtmoBlendFar (cs8[1][3])
#define skyAtmoBlendCurve (cs8[2][0])
#define skyAtmoBlendMax (cs8[2][1])
#define shadowOffset (cs8[2][2])
#define csBottomOffset (cs8[2][3])
#define atmoRaySize (cs8[3][0])
#define brightProtect (cs8[3][1])
#define redShift (cs4[0][2])


uniform float3 AmbientEast < hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float3 AmbientWest < hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);
uniform float2 AmbientExtra < hidden = hideDebug; > = float2(0.0f, 0.0f);
#define ambientSplit (AmbientExtra.x)
#define ambientMul (AmbientExtra.y)

uniform float2 GroundShadows < hidden = hideDebug; > = float2(0.0f, 0.0f);
#define gsShapeAdjust (GroundShadows.x)
#define gsSoftness (GroundShadows.y)

uniform float3 AmbientSkyColor < hidden = hideDebug; > = float3(0.0f, 0.0f, 0.0f);

#define windSpeed (cs9[2][0])
#define ambient_power (cs9[2][1])
#define ambient_altitude_bottom (cs9[2][2])
#define ambient_altitude_top (cs9[2][3])
#define GR_FogBloomBoost (cs9[3][0])
#define GR_AlphaScale (cs9[3][1])
#define pad0 (cs9[3][2])
#define pad1 (cs9[3][3])


#define lightMapIntensity (cs10[0][0])
#define lightMapSaturation (cs10[0][1])

#define GR_SigmaS (cs10[0][2])
#define GR_SigmaT (cs10[0][3])
#define GR_ColorX (cs10[1][0])
#define GR_ColorY (cs10[1][1])
#define GR_ColorZ (cs10[1][2])
#define GR_HeightFalloff (cs10[1][3])

#define chunkPostDensityCorrectionLow (cs10[2][0])
#define chunkPostDensityCorrectionMid (cs10[2][1])
#define chunkPostDensityCorrectionHigh (cs10[2][2])
#define chunkPostDensityCorrectionMidCurve (cs10[2][3])

#define ambientCurve (cs10[3][0])

#define HorizonBlendHeight (cs10[3][1])
#define HorizonBlendCurve (cs10[3][2])
#define HorizonBlendMul (cs10[3][3])

#define HorizonCutoffMul (cs11[0][0])
#define HorizonCutoffRadius (cs11[0][1])
#define TopCoverageGradient (cs11[0][2])
#define TopCoverageGradientBottom (cs11[0][3])
#define TopCoverageGradientTop (cs11[1][0])
#define TopCoverageGradientScale (cs11[1][1])
#define TopCoverageGradientVertScale (cs11[1][2])

#define DensityRatio (cs11[1][3])
#define ShadowFaceOffset (cs11[2][0])

float GetLinearizedDepth(float2 texcoord, float addMult)
{      
    float nearPlane = Clips.x;
    float farPlane = Clips.y;
    // Depth is reversed
	float depth = (1.0-tex2Dlod(ReShade::DepthBuffer, float4(texcoord, 0, 0)).x) * 1.0f;
	depth = nearPlane * farPlane / (farPlane + depth * (nearPlane - farPlane));
    if(depth > 7000.0f)
        depth = 99999.0f;
	return depth;	
}

float3 ViewRay(float2 texCoord)
{
    float x = texCoord.x * 2 - 1;
    x*=-1;
    float y = (1-texCoord.y) * 2 - 1;
    float3 cameraSpaceRay = float3(x / Projection[0].x,(y / Projection[1].y)*-1, 1.0);
    float3 ray = normalize(mul(cameraSpaceRay, transpose(float3x3(WorldView[0].xyz,WorldView[1].xyz,WorldView[2].xyz))));
    return ray;
}
float4 DepthToViewPos( float depth, float2 texCoord )
{
    float  x = texCoord.x * 2 - 1;
    x*=-1;
    float  y = ( texCoord.y ) * 2 - 1;
    float2 screenSpaceRay =
        float2( (x / Projection[0].x), (y / Projection[1].y) );
    float4 pos = float4( screenSpaceRay * depth, depth, 1.0 );
    return pos;
}
float3 DepthToWorldPos( float depth, float2 texCoord )
{
    float4 pos = DepthToViewPos( depth, texCoord );
    pos        = mul( pos, InverseView );
    return pos.xyz;
}

float3 SphericalRay(float2 uv)
{
    float2 p = uv * 2.0 - 1.0;
    float r = saturate(length(p));
    float theta = atan2(p.y, p.x);
    float phi = r * (0.5 * 3.14159265);  
    float sinPhi = sin(phi);
    float cosPhi = cos(phi);
    float3 localDir;
    localDir.x = sinPhi * cos(theta);
    localDir.y = sinPhi * sin(theta);
    localDir.z = cosPhi;
    return normalize(localDir);
}

float3 ViewRay_UpsideDown(float2 texCoord)
{
    float x = texCoord.x * 2.0 - 1.0;
    float y = (1.0-texCoord.y) * 2.0 - 1.0;
    float3 cameraSpaceRay = float3(x / Projection[0].x, (y / Projection[1].y) * -1.0, 1.0);
    cameraSpaceRay.x = -cameraSpaceRay.x;
    float3 ray = normalize(mul(cameraSpaceRay, transpose(float3x3(
        -WorldView[0].xyz,
        -WorldView[1].xyz,
        WorldView[2].xyz
    ))));
    
    return ray;
}

float3 GetScreenSpaceNormal(float2 texcoord)
{
    // Sample depth at the center and four cardinal neighbors.
    float depthC = GetLinearizedDepth(texcoord, 0);
    float depthR = GetLinearizedDepth(texcoord + float2(BUFFER_PIXEL_SIZE.x, 0), 0);
    float depthL = GetLinearizedDepth(texcoord + float2(-BUFFER_PIXEL_SIZE.x, 0), 0);
    float depthU = GetLinearizedDepth(texcoord + float2(0, -BUFFER_PIXEL_SIZE.y), 0);
    float depthD = GetLinearizedDepth(texcoord + float2(0, BUFFER_PIXEL_SIZE.y), 0);

    // Reconstruct view-space positions.
    // texcoord is remapped by subtracting 0.5 to center the coordinate system.
    float3 posC = float3(texcoord - 0.5, 1.0) * depthC;
    float3 posR = float3(texcoord + float2(BUFFER_PIXEL_SIZE.x, 0) - 0.5, 1.0) * depthR;
    float3 posL = float3(texcoord + float2(-BUFFER_PIXEL_SIZE.x, 0) - 0.5, 1.0) * depthL;
    float3 posU = float3(texcoord + float2(0, -BUFFER_PIXEL_SIZE.y) - 0.5, 1.0) * depthU;
    float3 posD = float3(texcoord + float2(0, BUFFER_PIXEL_SIZE.y) - 0.5, 1.0) * depthD;

    // Compute central differences.
    // Averaging left/right and up/down differences provides a smoother gradient.
    float3 dPosdx = (posR - posL) * 0.5;
    float3 dPosdy = (posD - posU) * 0.5;
    
    // Compute the normal from the cross product.
    float3 normal = normalize(cross(dPosdy, dPosdx));
    
    // Remap from [-1,1] to [0,1] if needed.
    return normal * 0.5 + 0.5;
}

float3 ViewRay_UpsideDown_Offset(float2 texCoord, float Smoothness, float NormalOffset)
{
    // Reconstruct view-space ray from texCoord.
    float x = texCoord.x * 2.0 - 1.0;
    float y = (1.0 - texCoord.y) * 2.0 - 1.0;
    float3 cameraSpaceRay = float3(x / Projection[0].x, (y / Projection[1].y) * -1.0, 1.0);
    cameraSpaceRay.x = -cameraSpaceRay.x;
    
    float3 ray = normalize(mul(cameraSpaceRay, transpose(float3x3(
        -WorldView[0].xyz,
        -WorldView[1].xyz,
         WorldView[2].xyz
    ))));
    
    // Get the screen-space smoothed normal (offset remapped from [0,1] back to [-1,1]).
    float3 normal = GetScreenSpaceNormal(texCoord) * 2.0 - 1.0;
    
    // Offset the ray direction with the smoothed normal.
    // Adjust NormalOffset to control the influence of the normal.
    ray = normalize(ray + normal * NormalOffset);
    
    return ray;
}