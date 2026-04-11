//----------------------------------------------------------------------------------------------//
//                                                                                              //
//         PrePass_Incandescence.fxh — Screen-Space Thermal Glow & Heat Shimmer                //
//         ENB of the Elders — Zain Dana Harper                                                 //
//                                                                                              //
//  Detects incandescent light sources (fire, lava, forge, torches, dragon breath) from         //
//  screen-space color heuristics and adds:                                                     //
//    1) Blackbody-tinted thermal glow (additive, naturally bloomed by downstream pipeline)     //
//    2) Heat shimmer distortion (animated UV refraction near hot sources)                      //
//                                                                                              //
//  Detection: A pixel is "incandescent" when it is bright AND warm-dominant                    //
//  (R > G > B, high luminance). The score drives glow intensity and shimmer amplitude.         //
//                                                                                              //
//  Host: enbeffectprepass.fx (HDR, float16). Runs before DOF/bloom/tonemap so glow            //
//  naturally gets bloom diffusion and DOF bokeh applied on top.                                //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#ifndef _PREPASS_INCANDESCENCE_
#define _PREPASS_INCANDESCENCE_
#define INCANDESCENCE_LOADED 1


//=============================================================================//
//                         UI PARAMETERS                                       //
//=============================================================================//

bool ui_IncanEnable
<
    string UIName = "INCANDESCENCE | Enable";
> = {false};

float ui_IncanIntensity
<
    string UIName = "INCANDESCENCE | Glow Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.1;
> = {1.0};

float ui_IncanThreshold
<
    string UIName = "INCANDESCENCE | Detection Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.05;
> = {0.8};

float ui_IncanWarmth
<
    string UIName = "INCANDESCENCE | Warmth Requirement (R-B gap)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.15};

float ui_IncanGlowRadius
<
    string UIName = "INCANDESCENCE | Glow Radius (px)";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 20.0; float UIStep = 0.5;
> = {6.0};

float3 ui_IncanTintLow
<
    string UIName = "INCANDESCENCE | Low Temp Tint (embers)";
    string UIWidget = "Color";
> = {1.0, 0.3, 0.05};

float3 ui_IncanTintHigh
<
    string UIName = "INCANDESCENCE | High Temp Tint (white hot)";
    string UIWidget = "Color";
> = {1.0, 0.85, 0.6};

bool ui_IncanShimmer
<
    string UIName = "INCANDESCENCE | Heat Shimmer";
> = {false};

float ui_IncanShimmerStr
<
    string UIName = "INCANDESCENCE | Shimmer Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_IncanShimmerSpeed
<
    string UIName = "INCANDESCENCE | Shimmer Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 5.0; float UIStep = 0.1;
> = {2.0};

float ui_IncanDepthFade
<
    string UIName = "INCANDESCENCE | Depth Fade (far=less)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};


//=============================================================================//
//                         INCANDESCENCE DETECTION                             //
//=============================================================================//
//  Score a pixel's "hotness" based on luminance and spectral warmth.
//  High R, moderate G, low B → hot. High luminance amplifies.
//  Returns [0, ~unbounded] incandescence score (HDR-safe).

float Incan_Score(float3 color, float threshold, float warmthReq)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));

    // Luminance gate: must be above threshold to be considered incandescent
    float lumaGate = smoothstep(threshold * 0.5, threshold, luma);

    // Warmth: how much does red dominate over blue?
    float warmth = saturate((color.r - color.b) / max(color.r + 0.001, 0.01));
    float warmGate = smoothstep(warmthReq * 0.5, warmthReq, warmth);

    // Red dominance: R > G > B chain (characteristic of blackbody emission)
    float redDom = saturate(color.r - color.g * 0.8) * 2.0;

    // Combined score: bright + warm + red-dominant
    return lumaGate * warmGate * (redDom + warmth) * luma;
}

// Approximate blackbody color for a given incandescence score
// Low score = deep red/orange (embers, ~1000K), high score = yellow-white (~3000K+)
float3 Incan_BlackbodyTint(float score, float3 tintLow, float3 tintHigh)
{
    float t = saturate(score * 0.5); // 0 = coolest hot, 1 = white hot
    return lerp(tintLow, tintHigh, t);
}


//=============================================================================//
//                         HEAT SHIMMER                                        //
//=============================================================================//
//  Animated UV distortion proportional to nearby incandescence.
//  Uses a multi-frequency sine pattern for natural-looking convection.

float2 Incan_ShimmerOffset(float2 uv, float score, float time, float strength)
{
    float phase1 = sin(uv.y * 200.0 + time * 7.0) * 0.6;
    float phase2 = sin(uv.y * 350.0 + time * 11.0) * 0.3;
    float phase3 = sin(uv.x * 180.0 + time * 5.0) * 0.1;

    float2 offset;
    offset.x = (phase1 + phase2) * strength * score * PixelSize.x * 3.0;
    offset.y = (phase3 + phase1 * 0.3) * strength * score * PixelSize.y * 2.0;

    return offset;
}


//=============================================================================//
//                         PIXEL SHADER                                        //
//=============================================================================//

float4 PS_Incandescence(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 original = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;

    [branch] if (!ui_IncanEnable)
        return float4(original, 1.0);

    float3 color = original;
    float time = Timer.x * ui_IncanShimmerSpeed;

    // Depth fade: reduce effect at distance
    float depth = GetLinearDepth(txcoord);
    float depthMask = 1.0;
    if (ui_IncanDepthFade > 0.01)
    {
        depthMask = 1.0 - smoothstep(0.0, 1.0, depth * ui_IncanDepthFade * 5.0);
    }

    // Heat shimmer: sample nearby pixels for incandescence to drive UV distortion
    // The shimmer should affect pixels NEAR hot sources, not just ON them
    if (ui_IncanShimmer && ui_IncanShimmerStr > 0.01)
    {
        // Sample incandescence score from a small neighborhood (above, below, left, right)
        float nearbyScore = 0.0;
        float2 shimStep = PixelSize * 4.0;

        float3 sU = TextureColor.SampleLevel(smpLinear, txcoord + float2(0, -shimStep.y), 0).rgb;
        float3 sD = TextureColor.SampleLevel(smpLinear, txcoord + float2(0,  shimStep.y), 0).rgb;
        float3 sL = TextureColor.SampleLevel(smpLinear, txcoord + float2(-shimStep.x, 0), 0).rgb;
        float3 sR = TextureColor.SampleLevel(smpLinear, txcoord + float2( shimStep.x, 0), 0).rgb;

        nearbyScore = max(max(Incan_Score(sU, ui_IncanThreshold, ui_IncanWarmth),
                              Incan_Score(sD, ui_IncanThreshold, ui_IncanWarmth)),
                         max(Incan_Score(sL, ui_IncanThreshold, ui_IncanWarmth),
                              Incan_Score(sR, ui_IncanThreshold, ui_IncanWarmth)));

        // Also check the center
        float centerScore = Incan_Score(original, ui_IncanThreshold, ui_IncanWarmth);
        nearbyScore = max(nearbyScore, centerScore * 0.5);

        if (nearbyScore > 0.01)
        {
            // Above-pixel shimmer is stronger (heat rises)
            float aboveBoost = smoothstep(0.0, 0.5, Incan_Score(sD, ui_IncanThreshold, ui_IncanWarmth));
            nearbyScore += aboveBoost * 0.5;

            float2 shimOffset = Incan_ShimmerOffset(txcoord, nearbyScore * depthMask, time,
                                                     ui_IncanShimmerStr);
            color = TextureColor.SampleLevel(smpLinear, txcoord + shimOffset, 0).rgb;
        }
    }

    // Thermal glow: additive blackbody-tinted light around incandescent pixels
    // Use a 5-tap cross pattern to detect and spread the glow
    if (ui_IncanIntensity > 0.01)
    {
        float glowR = ui_IncanGlowRadius * PixelSize.x;
        float glowRV = ui_IncanGlowRadius * PixelSize.y;

        // Center incandescence
        float centerScore = Incan_Score(color, ui_IncanThreshold, ui_IncanWarmth);

        // 4 cardinal neighbor incandescence (for glow spread)
        float3 c1 = TextureColor.SampleLevel(smpLinear, txcoord + float2( glowR, 0), 0).rgb;
        float3 c2 = TextureColor.SampleLevel(smpLinear, txcoord + float2(-glowR, 0), 0).rgb;
        float3 c3 = TextureColor.SampleLevel(smpLinear, txcoord + float2(0,  glowRV), 0).rgb;
        float3 c4 = TextureColor.SampleLevel(smpLinear, txcoord + float2(0, -glowRV), 0).rgb;

        float score1 = Incan_Score(c1, ui_IncanThreshold, ui_IncanWarmth);
        float score2 = Incan_Score(c2, ui_IncanThreshold, ui_IncanWarmth);
        float score3 = Incan_Score(c3, ui_IncanThreshold, ui_IncanWarmth);
        float score4 = Incan_Score(c4, ui_IncanThreshold, ui_IncanWarmth);

        // Weighted average (center strongest, neighbors contribute glow spread)
        float avgScore = centerScore * 0.4 + (score1 + score2 + score3 + score4) * 0.15;

        if (avgScore > 0.001)
        {
            // Blackbody tint based on how "hot" the pixel is
            float3 glowColor = Incan_BlackbodyTint(avgScore, ui_IncanTintLow, ui_IncanTintHigh);

            // Additive glow (HDR-safe, will be tonemapped downstream)
            color += glowColor * avgScore * ui_IncanIntensity * depthMask;
        }
    }

    return float4(color, 1.0);
}


#endif // _PREPASS_INCANDESCENCE_
