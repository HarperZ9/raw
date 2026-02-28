//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                Atmospheric Fog Shader Addon for ENBSeries  v3.3                              //
//                by Zain Dana Harper                                                           //
//                                                                                              //
//  v3.3 Fixes:                                                                                 //
//    - FIXED: Removed all #define aliases that redirected to enbeffectprepass.fx variable       //
//      names (UIFOG_MaxOpacity, UIFOG_MaxDist, etc.) — these don't exist when the addon        //
//      is compiled from enblens.fx.  All params now come from enbUI_Fog.fxh directly.          //
//    - FIXED: Removed static const defaults that conflicted with UI declarations               //
//      (HLSL redefinition error when both a UI variable and a static const share a name)       //
//    - FIXED: Added DNI_SEPARATION() for TODIE params (Density, Intensity, Tint, etc.)         //
//      so Day/Night/Interior values interpolate correctly at runtime                            //
//    - FIXED: enbUI_Fog.fxh now declares all params the addon needs — Density, Enable,         //
//      MaxFog, HeightFalloff, HeightBase, SkySampleY, SkySpread, SkyDesaturate, Scatter        //
//                                                                                              //
//  v3.1 Fixes:                                                                                 //
//    - FIXED: FOG0-FOG6 semantics renamed to FOGP0-FOGP6 to avoid collision                   //
//      with legacy D3D9 FOG semantic (FXC silently miscompiles with FOG)                       //
//    - FIXED: Removed FastLinDepth(raw, 2999) which crushed all depth to                       //
//      near-zero making fog invisible (v2.0 bug)                                               //
//    - Uses raw depth buffer directly with configurable power curve                            //
//    - Depth-validated sky color sampling with fallback                                        //
//    - Aerial perspective desaturation                                                         //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//----------------------------------------------------------------------------------------------//
//                       Parameter Resolution                                                   //
//                                                                                              //
//  All fog parameters are now declared in enbUI_Fog.fxh:                                       //
//    SHADERGROUP 12: Global params (bare names) — Enable, distances, height, sky, bloom         //
//    SHADERGROUP 13: TODIE params (Day_/Night_/Interior_ via SEPARATE_VAR) — Density,           //
//                    Intensity, Tint, TintWt, Bright, Scatter, HeightBase                       //
//                                                                                              //
//  TODIE params are interpolated via DNI_SEPARATION() in the vertex shader and passed           //
//  to the pixel shader through interpolants.  Global params are used directly in PS.            //
//                                                                                              //
//  v3.3: Removed all #define aliases (redirected to enbeffectprepass.fx variables that           //
//        don't exist in the enblens.fx compilation unit) and all static const defaults           //
//        (conflicted with enbUI_Fog.fxh declarations).  Everything is now self-contained.       //
//----------------------------------------------------------------------------------------------//


//----------------------------------------------------------------------------------------------//
//                                Fog Vertex/Pixel Structs                                      //
//----------------------------------------------------------------------------------------------//
//
//  IMPORTANT: Do NOT use FOG as a semantic prefix.
//  FOG is a legacy D3D9 semantic and FXC may miscompile or silently drop
//  interpolants using FOG0, FOG1, etc. Use FOGP (Fog Parameter) instead.
//----------------------------------------------------------------------------------------------//

struct FogVSOutput
{
   float4 pos            : SV_POSITION;
   float2 texcoord       : TEXCOORD0;
NI float  FogDensity     : FOGP0;
NI float  FogIntensity   : FOGP1;
NI float3 FogTint        : FOGP2;
NI float  FogTintWeight  : FOGP3;
NI float  FogBrightness  : FOGP4;
NI float  FogHeightMix   : FOGP5;
NI float  FogScatter     : FOGP6;
};


//----------------------------------------------------------------------------------------------//
//                                Sky Color Estimation                                          //
//----------------------------------------------------------------------------------------------//

static const float2 SkyPoissonDisc[8] = {
    float2(-0.613,  0.617), float2( 0.170, -0.040),
    float2(-0.299, -0.248), float2( 0.685,  0.422),
    float2(-0.799, -0.073), float2( 0.425, -0.639),
    float2( 0.036,  0.455), float2(-0.210,  0.720)
};

float3 EstimateSkyColor(float2 Center, float Spread, float Desat, float SkyThresh)
{
    float3 Accum = 0.0;
    float  ValidCount = 0.0;

    [unroll] for(int i = 0; i < 8; i++)
    {
        float2 UV = saturate(Center + SkyPoissonDisc[i] * Spread);
        float  Depth = TextureDepth.SampleLevel(Point_Sampler, UV, 0).x;

        //Only accept actual sky pixels
        float IsSky = step(SkyThresh, Depth);

        float3 S = TextureColor.SampleLevel(Linear_Sampler, UV, 0).rgb;

        //Reinhard soft clamp to prevent sun blowout
        S = S / (1.0 + S);

        Accum      += S * IsSky;
        ValidCount += IsSky;
    }

    [branch] if(ValidCount > 0.5)
    {
        Accum /= ValidCount;
    }
    else
    {
        //Fallback: use downsampled scene for interiors/enclosed areas
        Accum = TextureDownsampled.SampleLevel(Linear_Sampler, float2(0.5, 0.3), 0).rgb;
        Accum = Accum / (1.0 + Accum);
    }

    float Luma = dot(Accum, K_LUM);
    return lerp(Accum, Luma, Desat);
}


//----------------------------------------------------------------------------------------------//
//                              Bloom Color Sampling                                            //
//----------------------------------------------------------------------------------------------//

float3 EstimateBloomColor(float2 UV, float Desat)
{
    float3 Local  = RenderTarget128.SampleLevel(Linear_Sampler, UV, 0).rgb;
    float3 Global = RenderTarget128.SampleLevel(Linear_Sampler, float2(0.5, 0.5), 0).rgb;

    float3 Bloom = lerp(Global, Local, 0.4);
    Bloom = Bloom / (1.0 + Bloom);

    float Luma = dot(Bloom, K_LUM);
    return lerp(Bloom, Luma, Desat);
}


//----------------------------------------------------------------------------------------------//
//                              Height Fog Estimation                                           //
//----------------------------------------------------------------------------------------------//

float EstimateHeightFog(float2 UV, float FogDepth, float Curve, float Baseline, float Influence)
{
    float ScreenY = UV.y;
    float HeightFactor = saturate((ScreenY - (1.0 - Baseline)) / max(Baseline, DELTA));
    HeightFactor = pow(abs(HeightFactor), Curve);

    float DepthMod = smoothstep(0.0, 0.4, FogDepth);

    return lerp(1.0, saturate(HeightFactor + (1.0 - DepthMod) * 0.3), Influence);
}


//----------------------------------------------------------------------------------------------//
//                                Fog Vertex Shader                                             //
//----------------------------------------------------------------------------------------------//

FogVSOutput VS_Fog(VertexShaderInput IN)
{
    FogVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    //TODIE params: interpolate Day/Night/Interior values based on game state
    OUT.FogDensity    = DNI_SEPARATION(UIFOG_Density);
    OUT.FogIntensity  = DNI_SEPARATION(UIFOG_Intensity);
    OUT.FogTint       = DNI_SEPARATION(UIFOG_Tint);
    OUT.FogTintWeight = DNI_SEPARATION(UIFOG_TintWt);
    OUT.FogBrightness = DNI_SEPARATION(UIFOG_Bright);
    OUT.FogHeightMix  = UIFOG_HeightMix;  //global param (SG12)
    OUT.FogScatter    = DNI_SEPARATION(UIFOG_Scatter);

    return OUT;
}


//----------------------------------------------------------------------------------------------//
//                                Fog Pixel Shader                                              //
//----------------------------------------------------------------------------------------------//
//
//  Depth handling (v3.1):
//
//  ENB TextureDepth: perspective Z, 0 = camera, ~1.0 = far plane, 1.0 = sky
//  Skyrim SE: near ~10 units, far ~30000 units
//
//  v2.0 BUG:  FastLinDepth(raw, 2999.0) linearized depth, but this crushed
//             all scene geometry to values < 0.03.  Fog at 500 game units
//             received 0.08% opacity — completely invisible.
//
//  v3.x FIX:  Use raw depth directly. The perspective Z-buffer naturally
//             concentrates values near 1.0 for distant geometry, giving
//             a useful fog distribution. A power curve parameter shapes
//             the falloff artistically.
//
//  Raw depth → game distance reference:
//    0.50 → ~20 units     0.95 → ~200 units    0.998 → ~4300 units
//    0.80 → ~50 units     0.99 → ~970 units    0.999 → ~7500 units
//
//----------------------------------------------------------------------------------------------//

float4 PS_Fog(FogVSOutput IN) : SV_Target
{
    float3 SceneColor = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UIFOG_Enable || IN.FogIntensity < DELTA)
        return float4(SceneColor, 1.0);


    // ---- DEPTH ---- //

    float RawDepth = TextureDepth.SampleLevel(Point_Sampler, IN.texcoord, 0).x;

    //Skip sky pixels
    [branch] if(RawDepth > UIFOG_SkyThreshold)
        return float4(SceneColor, 1.0);

    //Artistic depth shaping
    float ShapedDepth = pow(abs(RawDepth), UIFOG_DepthPower);


    // ---- FOG FACTOR ---- //

    float DepthRange = saturate((ShapedDepth - UIFOG_StartDist) / max(UIFOG_EndDist - UIFOG_StartDist, DELTA));

    float FogAmount = 1.0 - exp(-IN.FogDensity * pow(abs(DepthRange), UIFOG_Curve));
    FogAmount = saturate(FogAmount * IN.FogIntensity);
    FogAmount = min(FogAmount, UIFOG_MaxFog);


    // ---- HEIGHT FOG ---- //

    [branch] if(UIFOG_HeightEnable)
    {
        float HeightMod = EstimateHeightFog(IN.texcoord, DepthRange,
            UIFOG_HeightFalloff, DNI_SEPARATION(UIFOG_HeightBase), IN.FogHeightMix);
        FogAmount *= HeightMod;
    }

    [branch] if(FogAmount < 0.002)
        return float4(SceneColor, 1.0);


    // ---- FOG COLOR ---- //

    float3 SkyColor = EstimateSkyColor(
        float2(0.5, UIFOG_SkySampleY), UIFOG_SkySpread, UIFOG_SkyDesaturate, UIFOG_SkyThreshold);

    float3 BloomColor = EstimateBloomColor(IN.texcoord, UIFOG_BloomDesat);

    float ScatterMix = saturate(UIFOG_BloomMix + DepthRange * IN.FogScatter);
    float3 FogColor  = lerp(SkyColor, BloomColor, ScatterMix);

    //--- SKYRIMBRIDGE: blend in actual game fog color ---
    //  The screen-space estimation is good but can drift from the game's intent
    //  (wrong color in certain weathers, interiors). Game fog color anchors it.
#ifdef SKYRIMBRIDGE_FXH
    [branch] if (SB_IsActive() && !EInteriorFactor)
    {
        float3 GameFogColor = lerp(SB_Fog_NearColor.rgb, SB_Fog_FarColor.rgb, DepthRange);
        //Tonemap to match our [0,1] working range
        GameFogColor = GameFogColor / (GameFogColor + 1.0);
        //Blend: 40% game data, 60% screen estimate — preserves artistic control
        FogColor = lerp(FogColor, GameFogColor, 0.4);
    }
#endif

    FogColor  = lerp(FogColor, FogColor * IN.FogTint, IN.FogTintWeight);
    FogColor *= IN.FogBrightness;

    //--- SKYRIMBRIDGE: lightning flash illumination ---
#ifdef SKYRIMBRIDGE_FXH
    [branch] if (SB_IsActive() && SB_Lightning.z > 0.01)
    {
        //Lightning briefly illuminates fog — cool white flash
        float3 FlashColor = float3(0.8, 0.85, 1.0) * SB_Lightning.z;
        FogColor += FlashColor * FogAmount * 0.5;
    }
#endif

    //Inverse Reinhard: restore HDR for correct scene blending
    FogColor = FogColor / max(1.0 - FogColor, DELTA);


    // ---- AERIAL PERSPECTIVE ---- //

    float SceneLuma   = dot(SceneColor, K_LUM);
    float3 DesatScene = lerp(SceneColor, SceneLuma, FogAmount * 0.15);

    //--- SKYRIMBRIDGE: surface wetness darkening ---
    //  Wet surfaces absorb more light. Subtle effect that connects
    //  rain/precipitation to the overall scene appearance.
#ifdef SKYRIMBRIDGE_FXH
    [branch] if (SB_IsActive() && SB_Precip_Surface.x > 0.01)
    {
        float WetDarken = SB_Precip_Surface.x * 0.08; // max 8% darkening
        //Only darken non-sky, nearby geometry
        WetDarken *= saturate(1.0 - DepthRange * 3.0);
        DesatScene *= (1.0 - WetDarken);
    }
#endif


    // ---- COMPOSITE ---- //

    float3 Result = lerp(DesatScene, FogColor, FogAmount);
    return float4(Result, 1.0);
}
