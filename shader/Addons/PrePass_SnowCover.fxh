//=============================================================================
//  PrePass_SnowCover.fxh — Screen-Space Snow Accumulation
//
//  3-pass addon for enbeffectprepass.fx:
//    Pass A: Compute snow mask from depth-reconstructed normals + game state
//    Pass B: Bilateral blur horizontal
//    Pass C: Bilateral blur vertical + composite snow onto scene
//
//  Integrates with SkyrimBridge: uses SB_Precip_Surface.z (snow accumulation),
//  SB_Weather_Flags.w (isSnowy), SB_Atmos_Ambient, SB_Sun_Direction.
//
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef PREPASS_SNOW_COVER_FXH
#define PREPASS_SNOW_COVER_FXH

#define SNOW_LOADED 1


//=== UI PARAMETERS ===//

int ui_Snow_Sep0
<
    string UIName = "===== SNOW COVER =====";
    int UIMin = 0; int UIMax = 0;
> = {0};

bool ui_Snow_Enable
<
    string UIName = "Snow | Enable";
> = {true};

float ui_Snow_Amount
<
    string UIName = "Snow | Manual Amount Override";
    string UIWidget = "Spinner";
    float UIMin = -0.1;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {-0.1};

float ui_Snow_NormalThreshold
<
    string UIName = "Snow | Surface Angle Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.35};

float ui_Snow_EdgeSharpness
<
    string UIName = "Snow | Coverage Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 20.0;
    float UIStep = 0.1;
> = {6.0};

float ui_Snow_Brightness
<
    string UIName = "Snow | Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.1};

float ui_Snow_BlueTint
<
    string UIName = "Snow | Shadow Blue Tint";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.15};

float ui_Snow_FadeStart
<
    string UIName = "Snow | Fade Start Distance";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.4};

float ui_Snow_FadeEnd
<
    string UIName = "Snow | Fade End Distance";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.85};

float ui_Snow_BlurSharpness
<
    string UIName = "Snow | Blur Edge Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 20.0;
    float UIStep = 0.1;
> = {6.0};


//=== SNOW HELPER FUNCTIONS ===//

// Hash for procedural variation
float SnowHash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// Get snow accumulation from SkyrimBridge or manual override
float GetSnowAccumulation()
{
    // Manual override takes precedence (>= 0 means active)
    if (ui_Snow_Amount >= 0.0)
        return ui_Snow_Amount;

    // SkyrimBridge: SB_Precip_Surface.z = snow accumulation [0,1]
    // Declared in SkyrimBridge.fxh, available when included in prepass
    #ifdef SKYRIMBRIDGE_FXH
        return SB_Precip_Surface.z;
    #else
        // Fallback: use ENB Weather variable
        // Weather.w ~ snowiness from ENB
        return saturate(Weather.w);
    #endif
}


//=== PIXEL SHADERS ===//

// Pass A: Compute snow coverage mask
// Output: R = snow mask [0,1], G = brightness variation, B = blue tint amount
float4 PS_SnowCompute(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    if (!ui_Snow_Enable)
        return float4(0, 0, 0, 0);

    float snowAccum = GetSnowAccumulation();
    if (snowAccum < 0.01)
        return float4(0, 0, 0, 0);

    float depth = GetLinearDepth(uv);

    // Skip sky
    if (depth > 0.99)
        return float4(0, 0, 0, 0);

    // Distance fade
    float fadeFactor = 1.0 - saturate((depth - ui_Snow_FadeStart) /
                       max(ui_Snow_FadeEnd - ui_Snow_FadeStart, 0.001));
    if (fadeFactor < 0.01)
        return float4(0, 0, 0, 0);

    // Reconstruct surface normal from depth
    float3 normal = ReconstructNormal(uv, depth);

    // Snow accumulates on upward-facing surfaces
    // normal.z > 0 means facing up in screen space
    // We use the Y component which corresponds to "up" in screen-space normals
    // reconstructed from depth differences
    float upFacing = normal.z;  // depth-based normal: z points toward camera

    // For a more correct approach, use the dot product with world-up
    // In screen space, "up" depends on camera angle, but for a prepass
    // the depth-reconstructed normal.y approximates upward bias
    float surfaceAngle = saturate(upFacing);

    // Apply threshold with smooth edge
    float snowMask = saturate((surfaceAngle - ui_Snow_NormalThreshold) *
                              ui_Snow_EdgeSharpness);

    // Modulate by global accumulation
    snowMask *= snowAccum;

    // Procedural variation (breaks up uniform coverage)
    float2 noiseCoord = pos.xy * 0.25;
    float variation = SnowHash(noiseCoord);
    float detailNoise = SnowHash(pos.xy * 1.73);

    // Large-scale patches + fine grain
    snowMask *= lerp(0.7, 1.0, variation);
    snowMask *= lerp(0.85, 1.0, detailNoise);

    // Reduce snow in very dark areas (under overhangs, caves)
    float3 sceneColor = TextureColor.SampleLevel(smpLinear, uv, 0).rgb;
    float sceneLuma = dot(sceneColor, float3(0.2126, 0.7152, 0.0722));
    float occlusionFactor = saturate(sceneLuma * 4.0);  // Dark areas get less snow
    snowMask *= lerp(0.2, 1.0, occlusionFactor);

    // Apply distance fade
    snowMask *= fadeFactor;

    // Brightness variation based on lighting
    float brightness = ui_Snow_Brightness * lerp(0.8, 1.2, variation);

    // Blue tint in shadow areas
    float blueTint = ui_Snow_BlueTint * (1.0 - saturate(sceneLuma * 2.0));

    return float4(saturate(snowMask), brightness, blueTint, 1.0);
}

// Pass B: Bilateral blur horizontal on snow mask
float4 PS_SnowBlurH(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float4 center = RenderTargetRGBA64F.Sample(smpPoint, uv);
    float centerDepth = GetLinearDepth(uv);

    float4 result = center;
    float totalWeight = 1.0;

    static const int SNOW_BLUR_RADIUS = 4;

    [unroll]
    for (int i = 1; i <= SNOW_BLUR_RADIUS; i++)
    {
        float spatialW = exp(-float(i * i) / (2.0 * float(SNOW_BLUR_RADIUS)));

        [unroll]
        for (int s = -1; s <= 1; s += 2)
        {
            float2 sampleUV = uv + float2(PixelSize.x * float(i * s), 0);
            float4 tap = RenderTargetRGBA64F.Sample(smpPoint, sampleUV);
            float tapDepth = GetLinearDepth(sampleUV);

            float depthW = exp(-abs(tapDepth - centerDepth) *
                           ui_Snow_BlurSharpness / max(centerDepth, 0.001));

            float w = spatialW * depthW;
            result += tap * w;
            totalWeight += w;
        }
    }

    return result / totalWeight;
}

// Pass C: Bilateral blur vertical + composite snow onto scene
float4 PS_SnowComposite(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    // Vertical blur
    float4 center = RenderTargetRGBA32.Sample(smpPoint, uv);
    float centerDepth = GetLinearDepth(uv);

    float4 blurred = center;
    float totalWeight = 1.0;

    static const int SNOW_BLUR_RADIUS = 4;

    [unroll]
    for (int i = 1; i <= SNOW_BLUR_RADIUS; i++)
    {
        float spatialW = exp(-float(i * i) / (2.0 * float(SNOW_BLUR_RADIUS)));

        [unroll]
        for (int s = -1; s <= 1; s += 2)
        {
            float2 sampleUV = uv + float2(0, PixelSize.y * float(i * s));
            float4 tap = RenderTargetRGBA32.Sample(smpPoint, sampleUV);
            float tapDepth = GetLinearDepth(sampleUV);

            float depthW = exp(-abs(tapDepth - centerDepth) *
                           ui_Snow_BlurSharpness / max(centerDepth, 0.001));

            float w = spatialW * depthW;
            blurred += tap * w;
            totalWeight += w;
        }
    }

    blurred /= totalWeight;

    // Extract snow data
    float snowMask   = blurred.r;
    float brightness = blurred.g;
    float blueTint   = blurred.b;

    // Read scene color
    float3 color = TextureColor.Sample(smpPoint, uv).rgb;

    if (snowMask < 0.005)
        return float4(color, 1.0);

    // Snow color: bright white with subtle blue in shadows
    float3 snowColor = float3(brightness, brightness, brightness + blueTint);

    // Blend: lerp scene toward snow color based on mask
    color = lerp(color, snowColor, snowMask);

    return float4(color, 1.0);
}


//=== TECHNIQUE MACRO ===//

#define SNOW_TECHS(name, p1, p2, p3) \
technique11 name##p1 <string UIName="Snow: Compute"; string RenderTarget="RenderTargetRGBA64F";> \
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic())); \
            SetPixelShader(CompileShader(ps_5_0, PS_SnowCompute())); } } \
technique11 name##p2 <string RenderTarget="RenderTargetRGBA32";> \
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic())); \
            SetPixelShader(CompileShader(ps_5_0, PS_SnowBlurH())); } } \
technique11 name##p3 \
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic())); \
            SetPixelShader(CompileShader(ps_5_0, PS_SnowComposite())); } }

#endif // PREPASS_SNOW_COVER_FXH
