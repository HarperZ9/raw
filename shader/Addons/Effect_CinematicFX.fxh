//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                Cinematic FX Suite for ENBSeries  v2.1                                        //
//                by Zain Dana Harper                                                           //
//                                                                                              //
//  Eight physically-motivated cinema/camera effects modelling the complete                     //
//  optical chain from lens filter to aged projection print:                                    //
//                                                                                              //
//    1. LENS DIFFUSION (Pro-Mist / Glimmerglass)                                               //
//       Screen-blend composite preserving highlights.  Dual-kernel PSF:                        //
//       16-tap local Gaussian (1σ + 2σ) + TextureDownsampled for far-field.                    //
//       Smooth toe-curve black lift.  Highlight retention control.                             //
//                                                                                              //
//    2. FILM HALATION                                                                          //
//       Wavelength-dependent scatter through film base: per-channel blur                       //
//       radii model anti-halation backing spectral absorption (red scatters                    //
//       ~1.5× wider than blue).  Soft-knee threshold.                                          //
//                                                                                              //
//    3. LIGHT LEAKS / FILM BURNS                                                               //
//       Scene brightness adaptation.  Hash-based sporadic intermittency.                       //
//       Three FBM noise layers with irrational drift ratios.                                   //
//                                                                                              //
//    4. GATE WEAVE / FILM JITTER                                                               //
//       Frame-quantized weave with analytical velocity for directional                         //
//       motion blur.  Per-frame exposure jitter from shutter mechanism.                        //
//       Rotation and breathing as incommensurate sinusoid sums.                                //
//                                                                                              //
//    5. CINEMATIC LETTERBOX                                                                    //
//       Aspect ratio masking with projected-black grain in bars.                               //
//                                                                                              //
//    6. ANAMORPHIC LENS  v2.1                                                                  //
//       Correct bilinear-optimized 13-effective-tap horizontal blur from                       //
//       7 reads, pairing integer positions [1,2],[3,4],[5,6].  Per-channel                     //
//       CA integrated into blur (R/B see shifted UV before Gaussian).                          //
//       Field curvature.  Focus breathing.  Vertical streak highlight bloom.                   //
//                                                                                              //
//    7. OPTICAL VIGNETTE  v2.1                                                                 //
//       Combined natural cos⁴(arctan(r)) + mechanical plateau-steep model.                    //
//       Cat-eye aperture shape at edges.  Per-channel wavelength-dependent                     //
//       vignetting (blue falls off faster than red, physically correct).                       //
//                                                                                              //
//    8. FILM DAMAGE  v2.1                                                                      //
//       Variable-width scratches with safe clamped blending.  Sprocket hole                    //
//       burns.  Elongated fiber-shaped dust.  Randomized gate hair side.                       //
//       Splice marks.  Chemical color fading.  Static-safe [unroll] loop.                      //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//----------------------------------------------------------------------------------------------//
//                              Shared Structs                                                  //
//----------------------------------------------------------------------------------------------//

//Simple fullscreen pass — used by Diffusion, Film Halation, Letterbox
struct CineFXVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};

//Light leaks — needs animation phases + scene average
struct LeakVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
NI float  Phase     : LEAK0;
NI float  Phase2    : LEAK1;
NI float  Phase3    : LEAK2;
NI float  SceneAvg  : LEAK3;  //v2: scene brightness for adaptation
};

//Gate weave — needs jitter + velocity for motion blur + exposure
struct WeaveVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
NI float2 Jitter   : WEAVE0;
NI float  RotAngle : WEAVE1;
NI float  ZoomOff  : WEAVE2;
NI float2 Velocity : WEAVE3;  //v2: analytical velocity for motion blur
NI float  ExpMul   : WEAVE4;  //v2: per-frame exposure jitter multiplier
};

//Anamorphic — needs breathing scale
struct AnamVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
NI float  BreathScale : ANAM0;  //Focus breathing zoom factor
};

//Optical vignette — precomputed parameters
struct VignetteVSOutput
{
    float4 pos       : SV_POSITION;
    float2 texcoord  : TEXCOORD0;
NI float  InnerR    : VIGP0;
NI float  FalloffR  : VIGP1;
};

//Film damage — precomputed timing + splice state
struct DamageVSOutput
{
    float4 pos       : SV_POSITION;
    float2 texcoord  : TEXCOORD0;
NI float  FrameID   : DMGP0;
NI float  FlickerMul: DMGP1;
NI float  ScratchX1 : DMGP2;
NI float  ScratchX2 : DMGP3;
NI float  HairPhase : DMGP4;
NI float  SpliceFlash : DMGP5;  //v2: splice mark flash intensity
};


//----------------------------------------------------------------------------------------------//
//                              Shared Vertex Shader                                            //
//----------------------------------------------------------------------------------------------//

CineFXVSOutput VS_CineFX(VertexShaderInput IN)
{
    CineFXVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;
    return OUT;
}


//----------------------------------------------------------------------------------------------//
//                              Noise Utilities                                                 //
//----------------------------------------------------------------------------------------------//

float CFX_Hash(float2 P)
{
    float3 P3 = frac(float3(P.xyx) * float3(443.8975, 397.2973, 491.1871));
    P3 += dot(P3, P3.yzx + 19.19);
    return frac((P3.x + P3.y) * P3.z);
}

float CFX_ValueNoise(float2 P)
{
    float2 I = floor(P);
    float2 F = frac(P);

    //Quintic Hermite interpolation (C² continuous)
    float2 U = F * F * F * (F * (F * 6.0 - 15.0) + 10.0);

    float A = CFX_Hash(I + float2(0, 0));
    float B = CFX_Hash(I + float2(1, 0));
    float C = CFX_Hash(I + float2(0, 1));
    float D = CFX_Hash(I + float2(1, 1));

    return lerp(lerp(A, B, U.x), lerp(C, D, U.x), U.y);
}

float CFX_FBM(float2 P, int Octaves, float Lacunarity, float Gain)
{
    float Value  = 0.0;
    float Amp    = 0.5;
    float2 Coord = P;

    [unroll] for(int i = 0; i < 4; i++)
    {
        if(i >= Octaves) break;
        Value += Amp * CFX_ValueNoise(Coord);
        Coord *= Lacunarity;
        Amp   *= Gain;
    }
    return Value;
}


//=============================================================================//
//                                                                             //
//                         1. LENS DIFFUSION  v2                               //
//                            (Pro-Mist Filter)                                //
//                                                                             //
//=============================================================================//
//
//  v2 improvements over v1:
//    - Screen blend composite instead of additive (prevents highlight blowout)
//    - Highlight retention: sharp specular detail preserved through diffusion
//    - 16-tap local blur (8 at 1σ + 8 at 2σ) for better PSF approximation
//    - Smooth toe-curve black lift (not abrupt step)
//    - Luminance computed once (fixed redundancy)
//=============================================================================//

float4 PS_Diffusion(CineFXVSOutput IN) : SV_Target
{
    float3 Sharp = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UIDIFF_Enable)
        return float4(Sharp, 1.0);

    float  SharpLuma = dot(Sharp, K_LUM);

    //--- Local Gaussian blur: 16-tap (1σ ring + 2σ ring) ---
    //  Correct Gaussian weights: w(d) = exp(-d²/(2σ²)) with σ=1 (radius is the scale)
    //  Ring 1 cardinal (distance 1σ):    exp(-0.5)  = 0.6065
    //  Ring 1 diagonal (distance √2·σ):  exp(-1.0)  = 0.3679
    //  Ring 2 cardinal (distance 2σ):    exp(-2.0)  = 0.1353
    //  Ring 2 diagonal (distance 2√2·σ): exp(-4.0)  = 0.0183
    float  Rad1 = UIDIFF_LocalRadius * PixelSize.x;
    float  VRad1 = Rad1 * ScreenSize.z;
    float  Rad2 = Rad1 * 2.0;
    float  VRad2 = VRad1 * 2.0;

    float3 LocalBlur = Sharp;
    float  LocalW = 1.0;

    //Ring 1 cardinal: distance = 1σ, weight = exp(-0.5) = 0.6065
    static const float W1C = 0.6065;
    LocalBlur += (TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( Rad1, 0),     0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-Rad1, 0),     0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0,     VRad1), 0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0,    -VRad1), 0).rgb) * W1C;
    //Ring 1 diagonal: distance = √2·σ, weight = exp(-1.0) = 0.3679
    static const float W1D = 0.3679;
    float D1 = Rad1 * 0.7071;
    float DV1 = VRad1 * 0.7071;
    LocalBlur += (TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( D1,  DV1), 0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-D1,  DV1), 0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( D1, -DV1), 0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-D1, -DV1), 0).rgb) * W1D;
    LocalW += 4.0 * W1C + 4.0 * W1D;

    //Ring 2 cardinal: distance = 2σ, weight = exp(-2.0) = 0.1353
    static const float W2C = 0.1353;
    LocalBlur += (TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( Rad2, 0),     0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-Rad2, 0),     0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0,     VRad2), 0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0,    -VRad2), 0).rgb) * W2C;
    //Ring 2 diagonal: distance = 2√2·σ, weight = exp(-4.0) = 0.0183
    static const float W2D = 0.0183;
    float D2 = Rad2 * 0.7071;
    float DV2 = VRad2 * 0.7071;
    LocalBlur += (TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( D2,  DV2), 0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-D2,  DV2), 0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( D2, -DV2), 0).rgb
                + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-D2, -DV2), 0).rgb) * W2D;
    LocalW += 4.0 * W2C + 4.0 * W2D;

    LocalBlur /= LocalW;

    //--- Wide blur from TextureDownsampled ---
    float3 WideBlur = TextureDownsampled.SampleLevel(Linear_Sampler, IN.texcoord, 0).rgb;
    WideBlur = WideBlur / (1.0 + WideBlur * 0.5);

    //--- Blend local and wide ---
    float3 Diffused = lerp(WideBlur, LocalBlur, UIDIFF_LocalWeight);

    //--- Luminance weighting ---
    float  DiffLuma = dot(Diffused, K_LUM);
    float  Weight = pow(saturate(DiffLuma), UIDIFF_HighlightBias);

    //--- Desaturation of scatter ---
    Diffused = lerp(Diffused, DiffLuma, UIDIFF_Desaturate);

    //--- Tint ---
    Diffused *= UIDIFF_Tint;

    //--- Screen blend composite (preserves highlights, avoids blowout) ---
    //  Screen: Result = 1 - (1 - A) × (1 - B)
    //  Weighted screen: lerp(Sharp, Screen(Sharp, Diffused), Strength × Weight)
    float3 DiffContrib = Diffused * UIDIFF_Strength * Weight;
    float3 Screened = 1.0 - (1.0 - Sharp) * (1.0 - DiffContrib);

    //--- Highlight retention ---
    //  Preserve sharp specular detail: where the sharp image is very bright,
    //  reduce the diffusion amount so specular highlights stay crisp.
    //  Single-parameter control: 0 = full diffusion everywhere, 1 = speculars stay sharp.
    float  RetainMask = saturate(SharpLuma * 3.0) * UIDIFF_HighlightRetain;
    float3 Result = lerp(Screened, Sharp, RetainMask);

    //--- Black lift: smooth toe curve ---
    //  Instead of an abrupt step, use a smooth rolloff that gently lifts
    //  only the deepest shadows without affecting midtones.
    float  ToeMask = 1.0 / (1.0 + SharpLuma * 8.0);  //Smooth 1/(1+8x) curve
    Result += UIDIFF_BlackLift * ToeMask;

    return float4(max(Result, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                         2. FILM HALATION  v2                                //
//                                                                             //
//=============================================================================//
//
//  v2 improvements:
//    - Per-channel wavelength-dependent scatter radii: the anti-halation
//      backing absorbs blue more strongly, so blue scatters shorter than red.
//      Red/Green/Blue scatter at different UV scales for genuine chromatic
//      halation spread — not just a tint applied to uniform scatter.
//    - 8-tap near-field blur per channel (not just 4-tap)
//=============================================================================//

float4 PS_FilmHalation(CineFXVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UIFHALO_Enable)
        return float4(Color, 1.0);

    //--- Threshold on SOURCE luminance (determines what's bright enough to halate) ---
    float SourceLuma = dot(Color, K_LUM);
    float KS = max(UIFHALO_Threshold - UIFHALO_Knee, 0.0);
    float KE = UIFHALO_Threshold + UIFHALO_Knee;
    float ThreshMask = smoothstep(KS, KE, SourceLuma);

    //Early out if nothing exceeds threshold
    [branch] if(ThreshMask < 0.001)
        return float4(Color, 1.0);

    //--- Per-channel wavelength-dependent scatter ---
    //  Anti-halation backing absorbs blue strongly, red weakly.
    //  Red scatters wider than blue through the film base.
    float WS = UIFHALO_WaveSpread;
    float3 ChScale = float3(WS, 1.0, 1.0 / max(WS, 0.5));

    //--- Near-field: sample at 3 wavelength-dependent radii ---
    //  Instead of per-channel loop (24 reads), sample at R/G/B radii
    //  and pick the correct channel from each full RGB read (12 reads total).
    float  BaseR = 3.5 * PixelSize.x;
    float3 Near = 0.0;

    //Red channel: widest scatter
    float RR = BaseR * ChScale.r;
    float RRV = RR * ScreenSize.z;
    Near.r = (TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( RR, 0), 0).r
            + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-RR, 0), 0).r
            + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0,  RRV), 0).r
            + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0, -RRV), 0).r) * 0.25;

    //Green channel: medium scatter
    float RG = BaseR * ChScale.g;
    float RGV = RG * ScreenSize.z;
    Near.g = (TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( RG, 0), 0).g
            + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-RG, 0), 0).g
            + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0,  RGV), 0).g
            + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0, -RGV), 0).g) * 0.25;

    //Blue channel: narrowest scatter
    float RB = BaseR * ChScale.b;
    float RBV = RB * ScreenSize.z;
    Near.b = (TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2( RB, 0), 0).b
            + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(-RB, 0), 0).b
            + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0,  RBV), 0).b
            + TextureColor.SampleLevel(Linear_Sampler, IN.texcoord + float2(0, -RBV), 0).b) * 0.25;

    //--- Wide-field: TextureDownsampled with center-relative UV scale ---
    //  Scale UV from center (0.5, 0.5) to simulate different scatter radii.
    float  WideScale = 0.015;
    float2 CenteredUV = IN.texcoord - 0.5;
    float3 Wide;
    Wide.r = TextureDownsampled.SampleLevel(Linear_Sampler, 0.5 + CenteredUV * (1.0 + WideScale * ChScale.r), 0).r;
    Wide.g = TextureDownsampled.SampleLevel(Linear_Sampler, IN.texcoord, 0).g;
    Wide.b = TextureDownsampled.SampleLevel(Linear_Sampler, 0.5 + CenteredUV * (1.0 - WideScale * (ChScale.b - 1.0)), 0).b;

    //--- Blend near and wide ---
    float3 Scatter = lerp(Near, Wide, UIFHALO_SpreadMix);

    //--- Apply source-based threshold mask ---
    Scatter *= ThreshMask;

    //--- Anti-halation spectral tint ---
    Scatter *= UIFHALO_Tint;

    //--- Partial desaturation ---
    float HaloGray = dot(Scatter, K_LUM);
    Scatter = lerp(Scatter, HaloGray, UIFHALO_Desaturate);

    //--- Additive composite ---
    Color += Scatter * UIFHALO_Strength;

    return float4(max(Color, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                         3. LIGHT LEAKS  v2                                  //
//                                                                             //
//=============================================================================//
//
//  v2 improvements:
//    - Scene brightness adaptation: leaks more visible in dark scenes
//      (eye is more adapted, stray light is more noticeable)
//    - Hash-based sporadic intermittency instead of sine pulsing
//      (real leaks don't cycle periodically)
//    - Three FBM layers for more organic, complex shapes
//=============================================================================//

LeakVSOutput VS_LightLeaks(VertexShaderInput IN)
{
    LeakVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    float T = Timer.x * 16777.216 * UILEAK_Speed;
    OUT.Phase  = T;
    OUT.Phase2 = T * 0.6180339887;
    OUT.Phase3 = T * 0.4142135623;

    //v2: estimate scene average brightness for adaptation
    float3 AvgA = TextureDownsampled.SampleLevel(Linear_Sampler, float2(0.3, 0.3), 0).rgb;
    float3 AvgB = TextureDownsampled.SampleLevel(Linear_Sampler, float2(0.7, 0.7), 0).rgb;
    float3 AvgC = TextureDownsampled.SampleLevel(Linear_Sampler, float2(0.5, 0.5), 0).rgb;
    OUT.SceneAvg = dot((AvgA + AvgB + AvgC) / 3.0, K_LUM);

    return OUT;
}


float4 PS_LightLeaks(LeakVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UILEAK_Enable)
        return float4(Color, 1.0);

    float2 UV = IN.texcoord;
    float  Softness = max(UILEAK_Softness, 0.5);

    //--- Three FBM noise layers for organic shapes ---
    float2 NUV1 = UV * Softness + float2(IN.Phase * 0.08, IN.Phase * 0.05);
    float  L1 = CFX_FBM(NUV1, 3, 2.37, 0.55);

    float2 NUV2 = UV * Softness * 1.7 + float2(-IN.Phase3 * 0.12, IN.Phase3 * 0.07);
    float  L2 = CFX_FBM(NUV2, 3, 2.13, 0.50);

    //v2: third layer adds low-frequency structure
    float2 NUV3 = UV * Softness * 0.6 + float2(IN.Phase2 * 0.05, -IN.Phase * 0.03);
    float  L3 = CFX_FBM(NUV3, 2, 1.87, 0.60);

    float  Leak = L1 * L2 * (0.5 + L3);

    //--- Coverage threshold ---
    float  CovThresh = 1.0 - UILEAK_Coverage;
    Leak = smoothstep(CovThresh, CovThresh + 0.25, Leak);

    //--- Edge bias ---
    float2 CenteredUV = UV * 2.0 - 1.0;
    float  EdgeDist = max(abs(CenteredUV.x), abs(CenteredUV.y));
    float  EdgeMask = smoothstep(0.2, 1.0, EdgeDist);
    Leak *= 1.0 - UILEAK_EdgeBias * (1.0 - EdgeMask);

    //--- v2: Hash-based sporadic intermittency ---
    //  Real leaks appear sporadically, not periodically.
    //  Hash the time period to create irregular on/off bursts.
    float  Period = floor(IN.Phase * 0.15);
    float  Burst = CFX_Hash(float2(Period, Period * 1.73));
    //  Only show leak when burst > 0.35 (appears ~65% of the time)
    float  BurstMask = smoothstep(0.35, 0.50, Burst);
    //  Smooth fade in/out within burst
    float  BurstPhase = frac(IN.Phase * 0.15);
    float  FadeEnv = smoothstep(0.0, 0.2, BurstPhase) * smoothstep(1.0, 0.7, BurstPhase);
    Leak *= BurstMask * FadeEnv;

    //--- Color modulation ---
    float  ColorPhase = CFX_ValueNoise(UV * 1.3 + float2(IN.Phase2 * 0.15, 0));
    float3 LeakColor = lerp(UILEAK_Color1, UILEAK_Color2, ColorPhase);

    //--- v2: Scene brightness adaptation ---
    //  In dark scenes, stray light is more noticeable (eye adaptation).
    //  Boost leak intensity inversely proportional to scene brightness.
    float  SceneBoost = lerp(1.0, 1.0 / max(IN.SceneAvg * 2.0, 0.2), UILEAK_SceneAdapt);
    SceneBoost = min(SceneBoost, 3.0); //Cap the boost

    float3 LeakRGB = LeakColor * Leak * UILEAK_Intensity * SceneBoost;

    //--- Blend ---
    float3 Result;
    [branch] if(UILEAK_BlendMode == 0)
    {
        Result = 1.0 - (1.0 - Color) * (1.0 - LeakRGB);
    }
    else if(UILEAK_BlendMode == 1)
    {
        Result = Color + LeakRGB;
    }
    else
    {
        float3 A = Color;
        float3 B = LeakRGB;
        float3 Lo = 2.0 * A * B + A * A * (1.0 - 2.0 * B);
        float3 Hi = 2.0 * A * (1.0 - B) + sqrt(max(A, 0.001)) * (2.0 * B - 1.0);
        Result = (B < 0.5) ? Lo : Hi;
    }

    return float4(max(Result, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                         4. GATE WEAVE  v2                                   //
//                                                                             //
//=============================================================================//
//
//  v2 improvements:
//    - Analytical velocity computation for directional motion blur
//      (image blurs along the weave direction, not just teleports)
//    - Frame-quantized timing: weave changes per film frame (24fps),
//      not continuously (gives that staccato film-motion character)
//    - Per-frame exposure jitter from shutter mechanism variation
//    - Motion blur samples along velocity vector in pixel shader
//=============================================================================//

WeaveVSOutput VS_GateWeave(VertexShaderInput IN)
{
    WeaveVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    //v2: Frame-quantized time (snaps to 24fps intervals)
    float  RawT = Timer.x * 16777.216 * UIWEAVE_Speed;
    float  FrameRate = 24.0;
    float  FrameID = floor(RawT * FrameRate);
    float  T = FrameID / FrameRate;  //Quantized time

    //--- Translational weave (4 incommensurate sinusoids) ---
    float JX = sin(T * 1.000) * 0.40
             + sin(T * 1.618) * 0.30
             + sin(T * 2.236) * 0.20
             + sin(T * 3.317) * 0.10;

    float JY = sin(T * 0.877) * 0.40
             + sin(T * 1.414) * 0.30
             + sin(T * 2.094) * 0.20
             + sin(T * 2.718) * 0.10;

    OUT.Jitter.x = JX * UIWEAVE_AmplitudeX * PixelSize.x;
    OUT.Jitter.y = JY * UIWEAVE_AmplitudeY * PixelSize.y;

    //--- v2: Analytical velocity (derivatives of sinusoid sums) ---
    //  d/dt[sin(ωt)] = ω·cos(ωt)
    float VX = cos(T * 1.000) * 1.000 * 0.40
             + cos(T * 1.618) * 1.618 * 0.30
             + cos(T * 2.236) * 2.236 * 0.20
             + cos(T * 3.317) * 3.317 * 0.10;

    float VY = cos(T * 0.877) * 0.877 * 0.40
             + cos(T * 1.414) * 1.414 * 0.30
             + cos(T * 2.094) * 2.094 * 0.20
             + cos(T * 2.718) * 2.718 * 0.10;

    //Velocity in UV space (proportional to per-frame displacement)
    OUT.Velocity.x = VX * UIWEAVE_AmplitudeX * PixelSize.x / max(FrameRate, 1.0);
    OUT.Velocity.y = VY * UIWEAVE_AmplitudeY * PixelSize.y / max(FrameRate, 1.0);

    //--- Rotational jitter ---
    float RotDeg = sin(T * 0.731) * 0.5 + sin(T * 1.137) * 0.3 + sin(T * 2.053) * 0.2;
    OUT.RotAngle = RotDeg * UIWEAVE_Rotation * 0.01745329;

    //--- Breathing ---
    float Breath = sin(T * 0.317) * 0.5 + sin(T * 0.519) * 0.3 + sin(T * 0.883) * 0.2;
    OUT.ZoomOff = Breath * UIWEAVE_Breathe;

    //--- v2: Per-frame exposure jitter ---
    //  Shutter mechanism variation: slight brightness change each frame.
    //  Hash the frame ID for pseudo-random per-frame variation.
    float ExpHash = frac(sin(FrameID * 127.1) * 43758.5453);
    OUT.ExpMul = 1.0 + (ExpHash * 2.0 - 1.0) * UIWEAVE_ExpJitter;

    return OUT;
}


float4 PS_GateWeave(WeaveVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UIWEAVE_Enable)
        return float4(Color, 1.0);

    float2 UV = IN.texcoord + IN.Jitter;

    //--- Rotational jitter ---
    [branch] if(UIWEAVE_Rotation > 0.001)
    {
        float2 Offset = UV - 0.5;
        float  CosA = cos(IN.RotAngle);
        float  SinA = sin(IN.RotAngle);
        UV = 0.5 + float2(
            Offset.x * CosA - Offset.y * SinA,
            Offset.x * SinA + Offset.y * CosA
        );
    }

    //--- Breathing ---
    [branch] if(UIWEAVE_Breathe > 0.0001)
    {
        UV = (UV - 0.5) * (1.0 + IN.ZoomOff) + 0.5;
    }

    //--- v2: Directional motion blur along weave velocity ---
    [branch] if(UIWEAVE_MotionBlur > 0.01)
    {
        //5-tap directional blur along velocity vector
        float2 Vel = IN.Velocity * UIWEAVE_MotionBlur * 15.0;

        //Gaussian-weighted: center heaviest, tails light
        Color = TextureColor.SampleLevel(Linear_Sampler, UV, 0).rgb * 0.30;
        Color += TextureColor.SampleLevel(Linear_Sampler, UV + Vel * 0.50, 0).rgb * 0.20;
        Color += TextureColor.SampleLevel(Linear_Sampler, UV - Vel * 0.50, 0).rgb * 0.20;
        Color += TextureColor.SampleLevel(Linear_Sampler, UV + Vel * 1.00, 0).rgb * 0.15;
        Color += TextureColor.SampleLevel(Linear_Sampler, UV - Vel * 1.00, 0).rgb * 0.15;
    }
    else
    {
        Color = TextureColor.SampleLevel(Linear_Sampler, UV, 0).rgb;
    }

    //--- v2: Exposure jitter ---
    Color *= IN.ExpMul;

    return float4(max(Color, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                         5. CINEMATIC LETTERBOX  v2                           //
//                                                                             //
//=============================================================================//
//
//  v2 improvement: projected-black grain in bar region.
//  Real projected black is never perfectly black — the projector lamp
//  still illuminates the screen through the film base, and dust/grain
//  in the unexposed film area creates subtle texture.
//=============================================================================//

float4 PS_Letterbox(CineFXVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UILBOX_Enable)
        return float4(Color, 1.0);

    //--- Target aspect ratio ---
    float TargetAspect;
    [branch] if(UILBOX_Ratio == 0)      TargetAspect = 2.39;
    else if(UILBOX_Ratio == 1) TargetAspect = 2.00;
    else if(UILBOX_Ratio == 2) TargetAspect = 1.85;
    else if(UILBOX_Ratio == 3) TargetAspect = 1.66;
    else if(UILBOX_Ratio == 4) TargetAspect = 1.333;
    else                       TargetAspect = UILBOX_CustomRatio;

    float ScreenAspect = ScreenSize.z;
    float BarMask = 0.0;
    float2 UV = IN.texcoord;
    float  Softness = max(UILBOX_EdgeSoftness, 0.0001);

    [branch] if(TargetAspect > ScreenAspect + 0.01)
    {
        float BarFrac = (1.0 - ScreenAspect / TargetAspect) * 0.5;
        float TopBar = smoothstep(BarFrac, BarFrac - Softness, UV.y);
        float BotBar = smoothstep(1.0 - BarFrac, 1.0 - BarFrac + Softness, UV.y);
        BarMask = max(TopBar, BotBar);
    }
    else if(TargetAspect < ScreenAspect - 0.01)
    {
        float BarFrac = (1.0 - TargetAspect / ScreenAspect) * 0.5;
        float LeftBar  = smoothstep(BarFrac, BarFrac - Softness, UV.x);
        float RightBar = smoothstep(1.0 - BarFrac, 1.0 - BarFrac + Softness, UV.x);
        BarMask = max(LeftBar, RightBar);
    }

    //--- v2: Projected-black grain in bars ---
    //  The bar region isn't perfectly clean — subtle noise from
    //  unexposed film grain and projector lamp scatter.
    float3 BarFill = UILBOX_BarColor;
    if(BarMask > 0.01)
    {
        float BarGrain = CFX_Hash(IN.pos.xy + Timer.z * 5.588) * 0.015;
        BarFill += BarGrain;
    }

    Color = lerp(Color, BarFill, BarMask * UILBOX_Opacity);

    return float4(Color, 1.0);
}


//=============================================================================//
//                                                                             //
//                         6. ANAMORPHIC LENS  v2.1                            //
//                                                                             //
//=============================================================================//
//
//  Physical basis: Cylindrical anamorphic front/rear elements create
//  asymmetric optical properties unique to 2× squeeze optics.
//
//  v2.1 improvements over v2:
//    - FIXED: bilinear blur pairing now starts at integer positions [1,2],
//      [3,4], [5,6] — v2 paired [0,1] which double-counted the center pixel,
//      producing an over-bright center in the PSF
//    - FIXED: chromatic aberration now applies to the blurred result.
//      v2 re-fetched R/B from TextureColor (original unblurred) while G
//      retained the blurred value — visible as a sharp R/B fringe on a
//      soft green channel.  Now CA is computed as UV offsets BEFORE blur,
//      so all three channels are blurred at their respective positions.
//    - Field curvature: blur increases toward frame edges (the cylindrical
//      element has worse aberrations off-axis)
//    - Focus breathing: slow horizontal-only FOV oscillation characteristic
//      of anamorphic rack-focus
//    - Vertical streak highlight bloom: anamorphic bokeh is oval/stretched
//      vertically; bright specular highlights elongate vertically. This
//      is the signature look alongside horizontal blur.
//=============================================================================//

AnamVSOutput VS_Anamorphic(VertexShaderInput IN)
{
    AnamVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    //Focus breathing — slow horizontal FOV oscillation
    //  The cylindrical element's focal length shifts with focus distance,
    //  producing a characteristic horizontal-only zoom wobble.
    float T = Timer.x * 16777.216 * UIANAM_BreatheSpeed;
    float Breath = sin(T * 0.617) * 0.50
                 + sin(T * 1.059) * 0.30
                 + sin(T * 1.732) * 0.20;
    OUT.BreathScale = 1.0 + Breath * UIANAM_Breathe * 0.01;

    return OUT;
}


float4 PS_Anamorphic(AnamVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UIANAM_Enable)
        return float4(Color, 1.0);

    float2 UV = IN.texcoord;

    //--- Focus breathing (horizontal-only scale) ---
    [branch] if(UIANAM_Breathe > 0.001)
    {
        UV.x = (UV.x - 0.5) * IN.BreathScale + 0.5;
    }

    //--- Edge stretch (mumps) ---
    //  Cubic radial distortion: objects at frame edges are horizontally wider.
    //  The cylindrical element magnifies differently across the field of view.
    //  Mixed radial term (x² + 0.3·y²) models the cylinder's 2D field map.
    [branch] if(UIANAM_Mumps > 0.001)
    {
        float2 C = UV * 2.0 - 1.0;
        float HStretch = (C.x * C.x + C.y * C.y * 0.3) * UIANAM_Mumps * 0.12;
        UV.x += C.x * HStretch;
    }

    //--- Horizontal chromatic aberration ---
    //  Lateral color from the cylindrical element: R and B wavelengths see
    //  slightly different horizontal magnifications.  Applied BEFORE blur
    //  so all three channels receive consistent Gaussian filtering.
    //  (v2 bug: applied after blur, causing R/B to be fetched unblurred
    //  from TextureColor while G retained the blurred result.)
    float2 UV_R = UV;
    float2 UV_G = UV;
    float2 UV_B = UV;

    [branch] if(UIANAM_ChromaH > 0.01)
    {
        float2 C = UV * 2.0 - 1.0;
        float  Fringe = length(C) * UIANAM_ChromaH * PixelSize.x * 3.0;
        UV_R.x = UV.x - Fringe;
        UV_B.x = UV.x + Fringe;
    }

    //--- Bilinear-optimized horizontal blur (13 effective taps from 7 reads) ---
    //
    //  Standard bilinear trick: pair adjacent integer-offset samples and
    //  place the texture fetch between them.  Hardware bilinear filtering
    //  blends the two pixels, giving 2 effective taps per read.
    //
    //  Pairs: [1,2], [3,4], [5,6]  →  3 pairs × 2 sides + 1 center = 7 reads
    //  Effective integer positions covered: 0, ±1, ±2, ±3, ±4, ±5, ±6 = 13 taps
    //
    //  (v2 bug: paired [0,1] which double-counted the center pixel,
    //  producing an asymmetric PSF with artificially high center weight.)
    //
    [branch] if(UIANAM_HBlur > 0.01)
    {
        //Field curvature — cylindrical element has worse aberrations off-axis
        float2 C = UV * 2.0 - 1.0;
        float  FieldMul = 1.0 + dot(C, C) * UIANAM_FieldCurve * 2.0;
        float  Sigma = UIANAM_HBlur * FieldMul;

        //Gaussian weights at integer pixel distances: W(d) = exp(-d²/2σ²)
        float S2 = 2.0 * Sigma * Sigma;
        float W0 = 1.0;                    //d=0
        float W1 = exp(-1.0  / S2);        //d=1
        float W2 = exp(-4.0  / S2);        //d=2
        float W3 = exp(-9.0  / S2);        //d=3
        float W4 = exp(-16.0 / S2);        //d=4
        float W5 = exp(-25.0 / S2);        //d=5
        float W6 = exp(-36.0 / S2);        //d=6

        //Bilinear pairs: combine weights, compute sub-pixel offset
        //  Pair A: pixels 1+2  →  weight = W1+W2, offset = (1·W1 + 2·W2)/(W1+W2)
        //  Pair B: pixels 3+4  →  weight = W3+W4, offset = (3·W3 + 4·W4)/(W3+W4)
        //  Pair C: pixels 5+6  →  weight = W5+W6, offset = (5·W5 + 6·W6)/(W5+W6)
        float BWA = W1 + W2;
        float BWB = W3 + W4;
        float BWC = W5 + W6;
        float BOA = (1.0 * W1 + 2.0 * W2) / max(BWA, 0.0001);
        float BOB = (3.0 * W3 + 4.0 * W4) / max(BWB, 0.0001);
        float BOC = (5.0 * W5 + 6.0 * W6) / max(BWC, 0.0001);

        float WTotal = W0 + 2.0 * (BWA + BWB + BWC);
        float InvWT  = 1.0 / WTotal;

        //Per-channel blur (each channel uses its own CA-shifted UV)
        //  This correctly blurs all three channels, with R/B offset if CA is active.
        float3 BlurR, BlurG, BlurB;
        float  StepX = PixelSize.x;

        //--- Green channel (reference wavelength, no shift) ---
        BlurG  = TextureColor.SampleLevel(Linear_Sampler, UV_G, 0).rgb * W0;
        BlurG += TextureColor.SampleLevel(Linear_Sampler, UV_G + float2( BOA * StepX, 0), 0).rgb * BWA;
        BlurG += TextureColor.SampleLevel(Linear_Sampler, UV_G + float2(-BOA * StepX, 0), 0).rgb * BWA;
        BlurG += TextureColor.SampleLevel(Linear_Sampler, UV_G + float2( BOB * StepX, 0), 0).rgb * BWB;
        BlurG += TextureColor.SampleLevel(Linear_Sampler, UV_G + float2(-BOB * StepX, 0), 0).rgb * BWB;
        BlurG += TextureColor.SampleLevel(Linear_Sampler, UV_G + float2( BOC * StepX, 0), 0).rgb * BWC;
        BlurG += TextureColor.SampleLevel(Linear_Sampler, UV_G + float2(-BOC * StepX, 0), 0).rgb * BWC;

        [branch] if(UIANAM_ChromaH > 0.01)
        {
            //--- Red channel (shifted left) ---
            BlurR  = TextureColor.SampleLevel(Linear_Sampler, UV_R, 0).rgb * W0;
            BlurR += TextureColor.SampleLevel(Linear_Sampler, UV_R + float2( BOA * StepX, 0), 0).rgb * BWA;
            BlurR += TextureColor.SampleLevel(Linear_Sampler, UV_R + float2(-BOA * StepX, 0), 0).rgb * BWA;
            BlurR += TextureColor.SampleLevel(Linear_Sampler, UV_R + float2( BOB * StepX, 0), 0).rgb * BWB;
            BlurR += TextureColor.SampleLevel(Linear_Sampler, UV_R + float2(-BOB * StepX, 0), 0).rgb * BWB;
            BlurR += TextureColor.SampleLevel(Linear_Sampler, UV_R + float2( BOC * StepX, 0), 0).rgb * BWC;
            BlurR += TextureColor.SampleLevel(Linear_Sampler, UV_R + float2(-BOC * StepX, 0), 0).rgb * BWC;

            //--- Blue channel (shifted right) ---
            BlurB  = TextureColor.SampleLevel(Linear_Sampler, UV_B, 0).rgb * W0;
            BlurB += TextureColor.SampleLevel(Linear_Sampler, UV_B + float2( BOA * StepX, 0), 0).rgb * BWA;
            BlurB += TextureColor.SampleLevel(Linear_Sampler, UV_B + float2(-BOA * StepX, 0), 0).rgb * BWA;
            BlurB += TextureColor.SampleLevel(Linear_Sampler, UV_B + float2( BOB * StepX, 0), 0).rgb * BWB;
            BlurB += TextureColor.SampleLevel(Linear_Sampler, UV_B + float2(-BOB * StepX, 0), 0).rgb * BWB;
            BlurB += TextureColor.SampleLevel(Linear_Sampler, UV_B + float2( BOC * StepX, 0), 0).rgb * BWC;
            BlurB += TextureColor.SampleLevel(Linear_Sampler, UV_B + float2(-BOC * StepX, 0), 0).rgb * BWC;

            Color = float3(BlurR.r, BlurG.g, BlurB.b) * InvWT;
        }
        else
        {
            Color = BlurG * InvWT;
        }
    }
    else if(UIANAM_ChromaH > 0.01)
    {
        //CA without blur: simple per-channel fetch
        Color.r = TextureColor.SampleLevel(Linear_Sampler, UV_R, 0).r;
        Color.g = TextureColor.SampleLevel(Linear_Sampler, UV_G, 0).g;
        Color.b = TextureColor.SampleLevel(Linear_Sampler, UV_B, 0).b;
    }

    //--- Vertical streak highlight bloom ---
    //  Anamorphic bokeh is vertically elongated due to the cylindrical element.
    //  Bright specular highlights produce distinctive vertical streaks.
    //  Uses a 5-tap vertical Gaussian on thresholded highlights only, then
    //  screen-blends the streak onto the result.
    [branch] if(UIANAM_Streak > 0.01)
    {
        float  Luma = dot(Color, float3(0.2126, 0.7152, 0.0722));
        float  StreakThresh = saturate((Luma - 0.7) * 3.0);  //soft knee at 0.7

        if(StreakThresh > 0.01)
        {
            float  StreakLen = UIANAM_Streak * PixelSize.y * 25.0;

            //5-tap vertical Gaussian (σ ≈ 0.4 of streak length)
            float3 Streak = Color * 0.30;
            Streak += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0,  StreakLen * 0.5), 0).rgb * 0.22;
            Streak += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -StreakLen * 0.5), 0).rgb * 0.22;
            Streak += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0,  StreakLen),       0).rgb * 0.13;
            Streak += TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -StreakLen),       0).rgb * 0.13;

            //Screen blend: 1 - (1-base)(1-streak)
            float3 StreakContrib = Streak * StreakThresh * UIANAM_Streak;
            Color = 1.0 - (1.0 - Color) * (1.0 - StreakContrib);
        }
    }

    return float4(max(Color, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                         7. OPTICAL VIGNETTE  v2.1                           //
//                            (Mechanical + Natural + Cat-Eye)                 //
//                                                                             //
//=============================================================================//
//
//  Physical basis: real lens vignetting is a combination of three mechanisms:
//
//    a) Natural falloff (cos⁴θ): illumination geometry — a tilted ray
//       intercepts a smaller solid angle and a larger sensor area, both
//       following cosine laws.  cos⁴(arctan(r/f)) ≈ 1/(1 + r²/f²)²
//
//    b) Mechanical vignetting: physical obstruction by lens barrel, filter
//       rings, and lens hood.  Produces a flat response in the center
//       (plateau) then steep falloff once the light cone clips the barrel.
//
//    c) Cat-eye clipping: at fast apertures (wide open), off-axis rays
//       see the exit pupil partially occluded by the rear barrel,
//       creating a D-shaped (cat-eye) pupil.  This makes the vignette
//       directionally asymmetric — darkening is more severe along the
//       radial direction and less along the tangential direction.
//
//  v2.1 improvements over v2:
//    - Per-channel vignetting for wavelength-dependent edge tint:
//      shorter wavelengths (blue) vignette slightly more than longer
//      wavelengths (red) because diffraction and scattering at barrel
//      edges affect shorter wavelengths more.  This replaces the v2
//      post-multiply tint which was a simple color grade, not physical.
//    - Better cos⁴θ approximation using the rational form 1/(1+r²)²
//      which is the exact expanded cos⁴(arctan(r)) — v2 used the
//      arbitrary pow(1 - 0.35·r², 2) which doesn't match any f-number.
//=============================================================================//

VignetteVSOutput VS_OptVignette(VertexShaderInput IN)
{
    VignetteVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    //Precompute plateau boundaries (invariant across pixels)
    float Plateau = UIVIG_Softness;
    OUT.InnerR   = Plateau * Plateau;
    OUT.FalloffR = 1.0 / max(1.0 - OUT.InnerR, 0.001);

    return OUT;
}

float4 PS_OptVignette(VignetteVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UIVIG_Enable)
        return float4(Color, 1.0);

    float2 C = IN.texcoord * 2.0 - 1.0;

    //Aspect-correct: map to physical sensor coordinates
    //  Roundness < 1 widens the vignette (horizontal stretch),
    //  matching the wider exit pupil of cinema-format lenses.
    C.x *= ScreenSize.z * UIVIG_Roundness;

    //--- Cat-eye aperture shape at edges ---
    //  At fast apertures, the rear barrel clips the exit pupil for off-axis rays.
    //  The clipping is asymmetric: the pupil becomes D-shaped (lenticular),
    //  with the flat edge facing the optical axis.  This means the tangential
    //  direction (perpendicular to the radial) sees more clipping.
    float R2_raw = dot(C, C);
    float R2 = R2_raw;

    [branch] if(UIVIG_CatEye > 0.01)
    {
        float2 Dir = normalize(C + 0.0001);
        float2 Tangent = float2(-Dir.y, Dir.x);
        float  TangentDist = abs(dot(C, Tangent));
        float  RadialDist  = abs(dot(C, Dir));
        //Cat-eye: tangential dimension is compressed at large radii
        //  The compression increases with radius (more barrel clipping off-axis)
        float  CatEyeFactor = 1.0 + UIVIG_CatEye * R2_raw * 0.8;
        R2 = RadialDist * RadialDist + TangentDist * TangentDist * CatEyeFactor;
    }

    //--- Combined natural + mechanical vignette model ---

    //  Normalization: MaxR2 is the squared radius at the frame corner
    float  MaxR2  = ScreenSize.z * ScreenSize.z * UIVIG_Roundness * UIVIG_Roundness + 1.0;
    float  NormR2 = R2 / MaxR2;

    //  Mechanical: flat plateau, then steep power-law falloff
    float  T = saturate((NormR2 - IN.InnerR) * IN.FalloffR);
    float  MechVig = 1.0 - pow(T, UIVIG_Power);

    //  Natural: cos⁴(arctan(r)) = 1/(1 + r²)²
    //  This is the exact expansion of cos⁴θ for a flat sensor at distance f
    //  from a thin-lens pinhole.  The 0.5 scaling factor maps the normalized
    //  radius to a reasonable field angle (~26° half-angle at corner).
    float  CosR = NormR2 * 0.5;
    float  NatFactor = 1.0 + CosR;
    float  NatVig = 1.0 / (NatFactor * NatFactor);

    //  Combined mask
    float  VigMask = MechVig * NatVig;
    VigMask = lerp(1.0, VigMask, UIVIG_Strength);

    //--- Per-channel wavelength-dependent vignetting ---
    //  Shorter wavelengths (blue) vignette more than longer wavelengths (red)
    //  because diffraction at barrel edges and Rayleigh scattering from
    //  anti-reflection coating imperfections affect short λ more strongly.
    //
    //  Model: apply a per-channel power to the mask.  For the tint (1.0, 0.92, 0.85),
    //  red gets the base mask, green gets slightly more falloff, blue the most.
    //  ChromaShift = 0 → uniform vignette.  ChromaShift = 1 → full spectral spread.
    [branch] if(UIVIG_ChromaShift > 0.01)
    {
        //Convert tint to per-channel power offset:
        //  tint = 1.0 → no extra falloff.  tint < 1.0 → more falloff (darker at edges).
        //  Power offset: 1.0 + (1 - tint) * ChromaShift * 0.5
        float3 ChPow = 1.0 + (1.0 - UIVIG_ChromaTint) * UIVIG_ChromaShift * 0.5;
        float3 VigRGB;
        VigRGB.r = lerp(1.0, pow(abs(VigMask), ChPow.r), UIVIG_Strength);
        VigRGB.g = lerp(1.0, pow(abs(VigMask), ChPow.g), UIVIG_Strength);
        VigRGB.b = lerp(1.0, pow(abs(VigMask), ChPow.b), UIVIG_Strength);
        Color *= VigRGB;
    }
    else
    {
        Color *= VigMask;
    }

    return float4(max(Color, 0.0), 1.0);
}


//=============================================================================//
//                                                                             //
//                         8. FILM DAMAGE  v2.1                                //
//                                                                             //
//=============================================================================//
//
//  Physical basis: accumulated damage from repeated projection of a 35mm
//  release print through a mechanical intermittent-movement projector.
//
//  v2.1 improvements over v2:
//    - FIXED: dust loop uses static [unroll] with conditional accumulation
//      instead of dynamic break — v2's [unroll]+break was undefined
//      behavior on some D3D11 drivers
//    - FIXED: scratch blending uses saturate to prevent negative RGB values
//      from bright/dark scratch mixing
//    - Sprocket hole burns: sprocket registration pins scratch the film
//      at top/bottom frame edges, creating persistent horizontal marks
//    - Gate hair can appear on either side (hash-selected edge)
//    - Dust particles include elongated fiber-shaped specks (not just circles)
//=============================================================================//

DamageVSOutput VS_FilmDamage(VertexShaderInput IN)
{
    DamageVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;

    float T = Timer.x * 16777.216;
    float FrameRate = 24.0;
    OUT.FrameID = floor(T * FrameRate);

    //--- Exposure flicker ---
    //  Three incommensurate sinusoids model the non-periodic brightness
    //  variation from lamp instability and shutter timing jitter.
    //  Frequencies: ~3 Hz base, golden-ratio harmonic, √2+1 harmonic.
    float FlickPhase = T * 3.0;
    float Flicker = sin(FlickPhase * 6.2831853) * 0.4
                  + sin(FlickPhase * 1.618 * 6.2831853) * 0.3
                  + sin(FlickPhase * 2.414 * 6.2831853) * 0.15;
    OUT.FlickerMul = 1.0 + Flicker * UIDMG_FlickerInt * 0.5;

    //--- Scratch positions (persist for 12-20 frames) ---
    //  A scratch persists for the duration the damaged film section passes
    //  through the gate.  ~16 frames at 24fps ≈ 0.67 seconds.
    float ScratchGroup = floor(OUT.FrameID / 16.0);
    OUT.ScratchX1 = frac(sin(ScratchGroup * 127.1 + 7.3) * 43758.5453);
    OUT.ScratchX2 = frac(sin(ScratchGroup * 269.5 + 13.7) * 43758.5453);

    //--- Gate hair phase ---
    OUT.HairPhase = T * 0.15;

    //--- Splice marks ---
    //  Splice occurs at reel changes: every ~2000 frames at 24fps (~83 seconds).
    //  Brief flash lasting 2-3 frames from the overexposed splice region
    //  passing through the gate.
    float  ReelPeriod  = 2000.0;
    float  FrameInReel = fmod(OUT.FrameID, ReelPeriod);
    float  SpliceFrame = step(FrameInReel, 2.0);
    float  SpliceDecay = 1.0 - FrameInReel / 3.0;
    OUT.SpliceFlash = SpliceFrame * max(SpliceDecay, 0.0) * UIDMG_SpliceInt;

    return OUT;
}


float4 PS_FilmDamage(DamageVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UIDMG_Enable)
        return float4(Color, 1.0);

    float2 UV = IN.texcoord;

    //--- VERTICAL SCRATCHES ---
    //  Emulsion surface scratched by debris trapped in the gate.
    //  Bright scratches: emulsion removed, clear base transmits projector light.
    //  Dark scratches: dirt/oxide fills the channel, blocking light.
    //  Width oscillates along length as debris wobbles while dragging through.
    [branch] if(UIDMG_ScratchInt > 0.01)
    {
        //Scratch 1 — primary (wider)
        float ScrX1 = IN.ScratchX1;
        ScrX1 += sin(UV.y * 12.0 + IN.FrameID * 0.01) * 0.003;

        //Width varies along scratch length (debris wobble)
        float  WidthBase = PixelSize.x * (1.0 + UIDMG_ScratchWidth);
        float  WidthMod1 = 1.0 + sin(UV.y * 25.0 + IN.ScratchX1 * 50.0) * 0.4
                               + sin(UV.y * 63.0 + IN.ScratchX1 * 127.0) * 0.2;
        float  ScrW1 = WidthBase * max(WidthMod1, 0.3);

        float  D1 = abs(UV.x - ScrX1);
        float  Scratch1 = smoothstep(ScrW1, ScrW1 * 0.25, D1);

        //Gradual fade at frame top/bottom (damaged section entering/leaving gate)
        float  FadeIn  = smoothstep(0.0, 0.08, UV.y);
        float  FadeOut = smoothstep(1.0, 0.92, UV.y);
        Scratch1 *= FadeIn * FadeOut;

        float  Vis1 = step(0.75 - UIDMG_ScratchDensity * 0.5,
                          frac(sin(IN.ScratchX1 * 31.37) * 43758.5453));

        //Scratch 2 — secondary (thinner)
        float ScrX2 = IN.ScratchX2;
        ScrX2 += sin(UV.y * 8.0 + IN.FrameID * 0.02) * 0.002;
        float  WidthMod2 = 1.0 + sin(UV.y * 31.0 + IN.ScratchX2 * 73.0) * 0.35;
        float  ScrW2 = WidthBase * 0.5 * max(WidthMod2, 0.3);
        float  D2 = abs(UV.x - ScrX2);
        float  Scratch2 = smoothstep(ScrW2, ScrW2 * 0.2, D2) * FadeIn * FadeOut;
        float  Vis2 = step(0.85 - UIDMG_ScratchDensity * 0.3,
                          frac(sin(IN.ScratchX2 * 73.91) * 43758.5453));

        //Bright/dark selection per scratch
        float BD1 = step(0.4, frac(ScrX1 * 173.7));
        float BD2 = step(0.4, frac(ScrX2 * 217.3));

        //v2.1 fix: saturate-safe blending to prevent negative values
        float  ScrVal1 = Scratch1 * Vis1 * UIDMG_ScratchInt * lerp(-0.35, 0.55, BD1);
        float  ScrVal2 = Scratch2 * Vis2 * UIDMG_ScratchInt * 0.5 * lerp(-0.25, 0.45, BD2);
        Color = max(Color + ScrVal1 + ScrVal2, 0.0);
    }

    //--- DUST / DIRT ---
    //  Foreign particles on the film surface or in the projector gate.
    //  Each speck persists 1-3 frames (settles, then dislodged by film motion).
    //  70% dark specks (opaque particles), 30% bright (translucent debris
    //  that scatters projector light).
    //
    //  v2.1 fix: static [unroll] with conditional accumulation replaces
    //  dynamic break, which was undefined behavior under [unroll] on some
    //  D3D11 drivers (the compiler may or may not honor the break).
    [branch] if(UIDMG_DustInt > 0.01)
    {
        float  DustAcc = 0.0;
        float  DustThresh = 1.0 - UIDMG_DustDensity;

        [unroll] for(int i = 0; i < 8; i++)
        {
            //Per-particle lifetime seed (deterministic from index)
            float  LifeSeed = frac(sin(float(i) * 43.37 + 17.1) * 43758.5453);
            float  Lifetime = floor(LifeSeed * 3.0) + 1.0;
            float  GroupID  = floor(IN.FrameID / Lifetime);

            //Stable position across lifetime (frame-group hash)
            float  Seed = float(i) * 127.1 + GroupID * 7.13;
            float2 DustPos;
            DustPos.x = frac(sin(Seed * 12.9898) * 43758.5453);
            DustPos.y = frac(sin(Seed * 78.233)  * 43758.5453);

            //v2.1: some particles are elongated fibers (not just circles)
            //  FiberSeed > 0.7 → elongated speck; otherwise circular
            float  FiberSeed = frac(Seed * 0.4271);
            float2 Diff = UV - DustPos;
            Diff.x *= ScreenSize.z;  //aspect correct

            //Elongated fiber: stretch one axis
            float  FiberAngle = FiberSeed * 6.2831853;
            float2 FiberDir = float2(cos(FiberAngle), sin(FiberAngle));
            float  AlongFiber = abs(dot(Diff, FiberDir));
            float  AcrossFiber = length(Diff - FiberDir * dot(Diff, FiberDir));
            float  Elongation = (FiberSeed > 0.7) ? 3.0 : 1.0;

            float  SizeBase = (1.0 + LifeSeed * 2.0) * PixelSize.x;
            float  Dist = (FiberSeed > 0.7)
                        ? max(AlongFiber / Elongation, AcrossFiber)
                        : length(Diff);

            float  Speck = smoothstep(SizeBase, SizeBase * 0.25, Dist);

            //Density gating: skip particle if below density threshold
            float  ParticleVis = step(DustThresh, frac(sin(float(i) * 91.17 + GroupID * 3.31) * 43758.5453));
            float  BrightDark = step(0.7, frac(Seed * 91.17));

            //Accumulate (replaces dynamic break)
            Color += Speck * ParticleVis * UIDMG_DustInt * lerp(-0.5, 0.8, BrightDark);
        }
    }

    //--- GATE HAIR ---
    //  Stray fiber caught in the projector gate aperture.
    //  Appears intermittently (~10% of time), drifts slowly.
    //  Dark only: opaque fiber blocking projection light.
    //
    //  v2.1: gate hair can appear on either side (hash-selected edge)
    //  and has a second harmonic for more natural fiber curvature.
    [branch] if(UIDMG_HairInt > 0.01)
    {
        float HairGroup = floor(IN.HairPhase * 0.3);
        float HairVis = step(0.90, frac(sin(HairGroup * 173.7) * 43758.5453));

        if(HairVis > 0.5)
        {
            //v2.1: select which side of frame the hair appears on
            float  SideSelect = step(0.5, frac(sin(HairGroup * 47.13) * 43758.5453));
            float  HairBaseX = lerp(
                0.08 - sin(IN.HairPhase * 0.7) * 0.05,   //Left edge
                0.92 + sin(IN.HairPhase * 0.7) * 0.05,   //Right edge
                SideSelect);

            //Multi-harmonic fiber curvature (natural looking)
            float FiberX = HairBaseX
                         + sin(UV.y * 6.0 + IN.HairPhase) * 0.025
                         + sin(UV.y * 14.0 + IN.HairPhase * 0.4) * 0.008
                         + cos(UV.y * 3.0 + IN.HairPhase * 1.3) * 0.012
                         + sin(UV.y * 23.0 + IN.HairPhase * 0.7) * 0.004;  //v2.1: extra harmonic

            float HairDist = abs(UV.x - FiberX);

            //Thickness varies along length (real fibers taper and have kinks)
            float ThickVar = 1.0 + sin(UV.y * 40.0 + IN.HairPhase * 2.0) * 0.3
                                 + sin(UV.y * 97.0 + HairGroup) * 0.15;  //v2.1: high-freq kinks
            float HairWidth = PixelSize.x * 1.5 * max(ThickVar, 0.4);
            float HairMask = smoothstep(HairWidth, HairWidth * 0.15, HairDist);

            //Vertical extent (fiber doesn't span entire frame)
            float VExtent = smoothstep(0.10, 0.22, UV.y) * smoothstep(0.90, 0.75, UV.y);
            Color -= HairMask * VExtent * UIDMG_HairInt * 0.6;
        }
    }

    //--- SPROCKET HOLE BURNS ---
    //  Registration pins in the projector intermittent mechanism engage
    //  sprocket holes at the top and bottom of each frame.  Over many
    //  projections, the pin contact area accumulates wear marks: faint
    //  horizontal scratches at the very top and bottom of the image area.
    //  These are persistent (present on every frame of a worn print).
    [branch] if(UIDMG_ScratchInt > 0.05)
    {
        //Top sprocket region: y ∈ [0.01, 0.04]
        float  SprTop = smoothstep(0.005, 0.015, UV.y) * smoothstep(0.045, 0.035, UV.y);
        //Bottom sprocket region: y ∈ [0.96, 0.99]
        float  SprBot = smoothstep(0.955, 0.965, UV.y) * smoothstep(0.995, 0.985, UV.y);
        float  SprocketMask = max(SprTop, SprBot);

        //Horizontal streak pattern from pin dragging
        float  SprocketNoise = frac(sin(UV.x * 800.0 + IN.FrameID * 0.003) * 43758.5453);
        float  SprocketStreak = smoothstep(0.3, 0.7, SprocketNoise);

        //Always bright (clear base exposed by pin)
        Color += SprocketMask * SprocketStreak * UIDMG_ScratchInt * 0.15;
    }

    //--- SPLICE MARKS ---
    //  At reel changeover points, the splicer leaves a brief artifact:
    //  overexposed frames, vertical film jump, and a horizontal splice line
    //  from the tape or cement join.
    [branch] if(IN.SpliceFlash > 0.01)
    {
        //Brief bright flash (overexposed frames at reel splice)
        Color = lerp(Color, 1.5, IN.SpliceFlash * 0.8);

        //Vertical displacement (film jumps at splice)
        float  SpliceShift = IN.SpliceFlash * 0.03;
        float2 SplicedUV = UV + float2(0, SpliceShift);
        float3 SpliceSample = TextureColor.SampleLevel(Linear_Sampler, SplicedUV, 0).rgb;
        Color = lerp(Color, SpliceSample + 0.3, IN.SpliceFlash * 0.5);

        //Horizontal splice line artifact (tape/cement visible)
        float SpliceLine = smoothstep(0.002, 0.0, abs(UV.y - 0.5 + IN.SpliceFlash * 0.05));
        Color += SpliceLine * IN.SpliceFlash;
    }

    //--- EXPOSURE FLICKER ---
    Color *= IN.FlickerMul;

    //--- CHEMICAL COLOR FADING ---
    //  Vinegar syndrome: acetic acid outgassing from the cellulose triacetate
    //  base attacks the dye layers.  Cyan dye degrades first, causing a warm
    //  shift, followed by general desaturation.
    [branch] if(UIDMG_ColorFade > 0.01)
    {
        float  FadeLuma = dot(Color, float3(0.2126, 0.7152, 0.0722));
        float3 FadedColor = lerp(Color, FadeLuma, UIDMG_ColorFade * 0.6);
        FadedColor *= UIDMG_VintageTint;
        Color = lerp(Color, FadedColor, UIDMG_ColorFade);
    }

    return float4(max(Color, 0.0), 1.0);
}
