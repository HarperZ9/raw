//////////////////////////////////////////////////////////////////////
//                                                                  //
//    ██████╗ ███████╗ █████╗  ██████╗████████╗ ██████╗ ██████╗     //
//    ██╔══██╗██╔════╝██╔══██╗██╔════╝╚══██╔══╝██╔═══██╗██╔══██╗    //
//    ██████╔╝█████╗  ███████║██║        ██║   ██║   ██║██████╔╝    //
//    ██╔══██╗██╔══╝  ██╔══██║██║        ██║   ██║   ██║██╔══██╗    //
//    ██║  ██║███████╗██║  ██║╚██████╗   ██║   ╚██████╔╝██║  ██║    //
//    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝    //
//                                                                  //
//                         A FALLOUT4 ENB                           //
//                                                                  //
///// MOD PAGE ///////////////////////////////////////////////////////
//                                                                  //
//    https://www.nexusmods.com/fallout4/mods/57321                 //
//                                                                  //
//////////////////////////////////////////////////////////////////////
//                                                                  //
//    ENBSeries Fallout 4 hlsl DX11 format                          //
//    visit http://enbdev.com for updates                           //
//    Copyright (c) Boris Vorontsov                                 //
//                                                                  //
///// CREDITS ////////////////////////////////////////////////////////
//                                                                  //
//  - Additional shaders, setup,                                    //
//    modifications, tweaks and                                     //
//    author of this file:          Sevenence                       //
//                                                                  //
//  - Advanced Depth of Field 3.0:  Marty McFly / Pascal Gilcher    //
//                                                                  //
//  - Reforged code and                                             //
//    Raindrops FX Shader:          The Sandvich Maker              //
//                                                                  //
//  - Weather System based on code                                  //
//    from Weather FX Shader:       Kingeric1992                    //
//                                                                  //
///// PLEASE DO NOT REDISTRIBUTE WITHOUT CREDITS /////////////////////


  
///// INCLUDE ////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

#include "Include/Helper.fxh"
#include "Include/ReforgedUI.fxh"
#include "Include/ReforgedGlobals.fxh"
#include "Setup.ini"

///// GUI ////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

UI_WHITESPACE(1)
UI_MESSAGE(w1, MERGE(                       "R E A C T O R   E N B   ",                             VERSION_NUMBER))

UI_WHITESPACE(2)
UI_WHITESPACE(3)

#define UI_CATEGORY DOF
UI_SEPARATOR_CUSTOM                         ("D E P T H   O F   F I E L D")
UI_LINESPACE(1)
UI_BOOL(bADOF_AutofocusEnable,              "|- DOF: Enable Autofocus",                             true)
float2  fADOF_AutofocusCenter               < string UIName="|- DOF: Autofocus sample center";      string UIWidget="Spinner";  float UIStep=0.01;  float UIMin=0.00;   float UIMax=1.00;   > = {0.5,0.5};
UI_INT(iADOF_AutofocusSamples,              "|- DOF: Autofocus sample count",                       0,    10,   6)
UI_FLOAT(fADOF_AutofocusRadius,             "|- DOF: Autofocus sample radius",                      0.01, 1.00, 0.05)
UI_FLOAT_FINE(fADOF_ManualfocusDepth,       "|- DOF: Manual focus depth",                           0.00, 1.00, 0.05,  0.001)
UI_LINESPACE(2)
UI_FLOAT(fADOF_NearBlurCurve,               "|- DOF: Near blur curve",                              0.01, 20.0, 1.00)
UI_FLOAT_DNI(fADOF_FarBlurCurve,            "|- DOF: Far blur curve",                               0.01, 20.0, 1.40)
UI_LINESPACE(3)
UI_FLOAT_FINE(fADOF_HyperFocus,             "|- DOF: Hyperfocal depth distance",                    0.00, 1.00, 0.015, 0.001)
UI_FLOAT(fADOF_RenderResolutionMult,        "|- DOF: Blur render res mult",                         0.50, 1.00, 0.50)
UI_LINESPACE(4)
UI_FLOAT(fADOF_BokehIntensity,              "|- DOF: Bokeh Intensity",                              0.00, 1.00, 0.50)
UI_FLOAT_FINE(fADOF_ShapeRadius,            "|- DOF: Bokeh shape max size",                         0.00, 100.0,15.0,  0.100)
UI_INT(iADOF_ShapeVertices,                 "|- DOF: Bokeh shape vertices",                         3,    9,    6)
UI_INT(iADOF_ShapeQuality,                  "|- DOF: Bokeh shape quality",                          2,    25,   5)
UI_FLOAT(fADOF_ShapeCurvatureAmount,        "|- DOF: Bokeh shape roundness",                       -1.00, 1.00, 1.00)
UI_FLOAT_FINE(fADOF_ShapeRotation,          "|- DOF: Bokeh shape rotation (\xB0)",                  0.00, 360.0,15.0,  1.000)
UI_FLOAT(fADOF_ShapeAnamorphRatio,          "|- DOF: Bokeh shape aspect ratio",                     0.00, 1.00, 1.00)
UI_LINESPACE(5)

#if E_OPTICAL_VIGNETTE
    UI_FLOAT(fADOF_ShapeVignetteCurve,      "|- DOF: Bokeh shape vignette curve",                   0.50, 2.50, 0.75)
    UI_FLOAT(fADOF_ShapeVignetteAmount,     "|- DOF: Bokeh shape vignette amount",                  0.00, 2.00, 1.00)
    UI_LINESPACE(6)
#endif

#if E_CHROMATIC_ABERRATION
    UI_FLOAT(fADOF_ShapeChromaAmount,       "|- DOF: Shape chromatic aberration amount",           -1.00, 1.00, -1.00)
    UI_INT(iADOF_ShapeChromaMode,           "|- DOF: Shape chromatic aberration type",              0,    2,    2)
    UI_LINESPACE(7)
#endif

UI_FLOAT_FINE(fADOF_SmootheningAmount,      "|- DOF: Gaussian postblur width",                      0.00, 20.0, 4.00,  0.010)

UI_WHITESPACE(4)
UI_WHITESPACE(5)

#define UI_CATEGORY RAIN
UI_SEPARATOR_CUSTOM                         ("R A I N D R O P S   O N   L E N S")
UI_LINESPACE(10)
UI_FLOAT(UI_Rain_Range,                     "|- Rain: Master Range",                                0.0, 128.0, 7.0)
UI_LINESPACE(11)
UI_MESSAGE(w2,                              "|---------------- RAIN LEVEL 1 ------------------")
UI_FLOAT(UI_Rain_Size_Lvl1,                 "|- Rain: Lvl1 - Size",                                 1.0, 64.0, 15.0)
UI_FLOAT(UI_Rain_Fade_Lvl1,                 "|- Rain: Lvl1 - Fade",                                 0.01,1.0, 0.05)
UI_FLOAT_FINE(UI_Rain_Chance_Lvl1,          "|- Rain: Lvl1 - Chance",                               0.0, 0.001, 0.0001, 0.00001)
UI_FLOAT(UI_Rain_Slide_Lvl1,                "|- Rain: Lvl1 - Slide",                                0.0, 1.0, 0.15)
UI_FLOAT(UI_Rain_Dispersion_Lvl1,           "|- Rain: Lvl1 - Dispersion",                           0.0, 1.0, 0.35)
UI_LINESPACE(12)
UI_MESSAGE(w3,                              "|---------------- RAIN LEVEL 2 ------------------")
UI_FLOAT(UI_Rain_Size_Lvl2,                 "|- Rain: Lvl2 - Size",                                 1.0, 64.0, 15.0)
UI_FLOAT(UI_Rain_Fade_Lvl2,                 "|- Rain: Lvl2 - Fade",                                 0.01,1.0, 0.1)
UI_FLOAT_FINE(UI_Rain_Chance_Lvl2,          "|- Rain: Lvl2 - Chance",                               0.0, 0.001, 0.0005, 0.00001)
UI_FLOAT(UI_Rain_Slide_Lvl2,                "|- Rain: Lvl2 - Slide",                                0.0, 1.0, 0.25)
UI_FLOAT(UI_Rain_Dispersion_Lvl2,           "|- Rain: Lvl2 - Dispersion",                           0.0, 1.0, 0.35)

UI_WHITESPACE(20)

///////////////////////////////////////////////////////////////////////////
// Semi-hardcoded parameters, DO NOT MODIFY unless you know what you do. //
// But what am I saying, you're gonna do it anyways. //////////////////////
///////////////////////////////////////////////////////////////////////////

#define DISCRADIUS_RESOLUTION_BOUNDARY_LOWER    0.25        // 1.0      // used for blending blurred scene.
#define DISCRADIUS_RESOLUTION_BOUNDARY_UPPER    1.0         // 6.0      // used for blending blurred scene.
#define DISCRADIUS_RESOLUTION_BOUNDARY_CURVE    0.5         // used for blending blurred scene.
#define FPS_HAND_BLUR_CUTOFF_DIST               0.3353      // fps hand depth (x10.000), change if you perceive blurred fps weapons.
#define FPS_HAND_BLUR_CUTOFF_CHECK              0           // blur = max if depth > hand depth, else 0, useful for tweaking above param
#define GAUSSIAN_BUILDUP_MULT                   4.0         // value of x -> gaussian reaches max radius at |CoC| == 1/x

#define linearstep(a,b,x) saturate((x-a)/(b-a))
#define LinearizeDepth(x) x *= rcp(mad(x,-2999.0,3000.0))

///// VERTEX SHADERS /////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

VS_OUTPUT_DOF  VS_DoF(VS_INPUT_POST IN, uniform float scale)
{
    VS_OUTPUT_DOF   OUT;
    OUT.pos     = float4(IN.pos.xyz, 1.0);
    OUT.txcoord.xy  = IN.txcoord.xy / scale;

    [unroll]
    for(int i=0; i<10; i++)
    {
        sincos(i * 6.2831853 / iADOF_ShapeVertices + radians(fADOF_ShapeRotation),OUT.vertices[i].y,OUT.vertices[i].x);
    }

    return OUT;
}

///// FUNCTIONS //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

float GetLinearDepth(float2 texcoord)
{
    float depth = TextureDepth.SampleLevel(LinearSampler, texcoord.xy,0).x;
    LinearizeDepth(depth);
    return depth;
}

float CircleOfConfusion(float2 texcoord, bool aggressiveLeakReduction)
{
    float2 depthdata; //x - linear scene depth, y - linear scene focus
    float scenecoc;   //blur value, signed by position relative to focus plane

    [branch] if(aggressiveLeakReduction)
    {
        float4 depthsGather[2] = {TextureDepth.Gather(PointSampler, texcoord.xy - PixelSize.xy * 0.5  ),
                      TextureDepth.Gather(PointSampler, texcoord.xy - PixelSize.xy * 0.5, 1)};

        LinearizeDepth(depthsGather[0]);
        LinearizeDepth(depthsGather[1]);

        depthdata.x = min(min(min(depthsGather[0].x,depthsGather[0].z),min(depthsGather[1].x,depthsGather[1].z)),depthsGather[0].y);
        depthdata.x = lerp(depthdata.x, depthsGather[0].y, 0.001);
    }
    else
    {
        depthdata.x = TextureDepth.Sample(PointSampler,texcoord.xy).x;
        LinearizeDepth(depthdata.x);
    }

    depthdata.y = TextureFocus.Sample(PointSampler, texcoord.xy).x;
    float handdepth = depthdata.x;

    depthdata.xy = saturate(depthdata.xy / fADOF_HyperFocus); //hyperfocal distance

    [branch] if(depthdata.x < depthdata.y)
    {
        scenecoc = depthdata.x / depthdata.y - 1.0;
        scenecoc = ldexp(scenecoc, -0.5*fADOF_NearBlurCurve*fADOF_NearBlurCurve);
    }
    else
    {
        scenecoc = (depthdata.x - depthdata.y)/(ldexp(depthdata.y, fADOF_FarBlurCurve*fADOF_FarBlurCurve) - depthdata.y);
            scenecoc = saturate(scenecoc);
    }

#if FPS_HAND_BLUR_CUTOFF_CHECK
    scenecoc = (handdepth < FPS_HAND_BLUR_CUTOFF_DIST * 1e-4) ? 0.0 : 1.0;
#else //FPS_HAND_BLUR_CUTOFF_CHECK
    scenecoc = (handdepth < FPS_HAND_BLUR_CUTOFF_DIST * 1e-4) ? 0.0 : scenecoc;
#endif //FPS_HAND_BLUR_CUTOFF_CHECK

    return scenecoc;
}

float3 integerHash3(uint3 x)
{
    static const uint K = 1103515245U;  // GLIB C
    x = ((x >> 8U) ^ x.yzx) * K;
    x = ((x >> 8U) ^ x.yzx) * K;
    x = ((x >> 8U) ^ x.yzx) * K;

    return x * rcp(0xffffffffU);
}

///// PIXEL SHADERS //////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

///// DOF ////////////////////////////////////////////////////////////

// fullres -> 16x16 R32F
float4  PS_ReadFocus(VS_OUTPUT_POST IN) : SV_Target
{
    float scenefocus = 0.0;

    [branch] if(bADOF_AutofocusEnable)
    {
        float samples = 10.0;
        float weightsum = 1e-6;

        for(float xcoord = 0.0; xcoord < samples; xcoord++)
        for(float ycoord = 0.0; ycoord < samples; ycoord++)
        {
            float2 sampleOffset = (float2(xcoord,ycoord) + 0.5) / samples;
            sampleOffset = sampleOffset * 2.0 - 1.0;
            sampleOffset *= fADOF_AutofocusRadius;
            sampleOffset += (fADOF_AutofocusCenter - 0.5);

            float sampleWeight = saturate(1.2 * exp2(-dot(sampleOffset,sampleOffset)*4.0));

            float tempfocus = GetLinearDepth(sampleOffset * 0.5 + 0.5);
            sampleWeight *= rcp(tempfocus + 0.001);

            sampleWeight *= saturate(tempfocus > FPS_HAND_BLUR_CUTOFF_DIST * 1e-4); //remove fps hands from focus calculations

            scenefocus += tempfocus * sampleWeight;
            weightsum += sampleWeight;
        }
        scenefocus /= weightsum;
    }
    else
    {
        scenefocus = fADOF_ManualfocusDepth;
    }

    return scenefocus;
}

// 16x16 -> 1x1 R32F
float4  PS_Focus(VS_OUTPUT_POST IN) : SV_Target
{
        float prevFocus = 0.0;
    float currFocus = 0.0;

    for(float x=0.0; x<16.0; x++)
    for(float y=0.0; y<16.0; y++)
    {
        prevFocus += TexturePrevious.SampleLevel(PointSampler, float2(x,y) / 16.0, 0).x;
        currFocus += TextureCurrent.SampleLevel(PointSampler, float2(x,y) / 16.0, 0).x;
    }

    return (bADOF_AutofocusEnable) ? 0.00390625 * lerp(prevFocus,currFocus,DofParameters.w) : currFocus * 0.00390625;
}

float4  PS_CoC(VS_OUTPUT_POST IN) : SV_Target
{
    float4 color = TextureColor.Sample(LinearSampler, IN.txcoord.xy);

    static const float2 sampleOffsets[4] = {float2( 1.5, 0.5) * PixelSize.xy,
                                            float2( 0.5,-1.5) * PixelSize.xy,
                                            float2(-1.5,-0.5) * PixelSize.xy,
                                            float2(-0.5, 1.5) * PixelSize.xy};

    float4 compColor = 0.0;
    float centerDepth = TextureDepth.Sample(LinearSampler, IN.txcoord.xy).x;
    LinearizeDepth(centerDepth);

    [loop]
    for(int i=0; i<4; i++)
    {
        float2 sampleCoord = IN.txcoord.xy + sampleOffsets[i];

        float3 sampleColor = TextureColor.Sample(LinearSampler, sampleCoord).rgb;
        float4 sampleDepths = TextureDepth.Gather(LinearSampler, sampleCoord);

        sampleColor /= 1.0 + max(max(sampleColor.r, sampleColor.g), sampleColor.b);

        float sampleDepthMin = min(min(sampleDepths.x,sampleDepths.y),min(sampleDepths.z,sampleDepths.w));
        LinearizeDepth(sampleDepthMin);

        float sampleWeight = saturate(sampleDepthMin * rcp(centerDepth) + 1e-3);
        compColor += float4(sampleColor.rgb * sampleWeight, sampleWeight);
    }

    compColor.rgb /= compColor.a;
    compColor.rgb /= 1.0 - max(compColor.r, max(compColor.g, compColor.b));

    color.rgb = lerp(color.rgb, compColor.rgb, saturate(compColor.w * 8.0));
    color.w = CircleOfConfusion(IN.txcoord.xy, 1);

    return color;
}

void ShapeRoundness(inout float2 sampleOffset, in float roundness)
{
    sampleOffset *= (1.0-roundness) + rsqrt(dot(sampleOffset,sampleOffset))*roundness;
}

void OpticalVignette(in float2 sampleOffset, in float2 centerVec, inout float sampleWeight)
{
    sampleOffset -= centerVec; // scaled by vignette intensity
    sampleWeight *= saturate(3.33 - dot(sampleOffset,sampleOffset) * 1.666); // notsosmoothstep to avoid aliasing
}

float2 CoC2BlurRadius(float CoC)
{
    return float2(fADOF_ShapeAnamorphRatio,ScreenSize.z) * CoC * fADOF_ShapeRadius * 6e-4;
}

float4  PS_DoF_Main(VS_OUTPUT_DOF IN, float4 vPos : SV_POSITION) : SV_Target
{
    clip(1.01-max(IN.txcoord.x,IN.txcoord.y));

    float4 BokehSum, BokehMax;
    BokehMax = BokehSum     = TextureColor.Sample(LinearSampler, IN.txcoord.xy);
    float weightSum         = 1.0;
    float CoC               = abs(BokehSum.w);
    float2 bokehRadiusScaled    = CoC2BlurRadius(CoC);
    float nRings            = lerp(1.0,iADOF_ShapeQuality,saturate(CoC)) + fmod(dot(vPos.xy,1),2)*0.5;

    if(bokehRadiusScaled.x < DISCRADIUS_RESOLUTION_BOUNDARY_LOWER * ScreenSize.y) return BokehSum;

    bokehRadiusScaled /= nRings;
    CoC /= nRings;

#if E_OPTICAL_VIGNETTE
    float2 centerVec = IN.txcoord.xy - 0.5;
    float centerDist = sqrt(dot(centerVec,centerVec));
    float vignette = pow(centerDist, fADOF_ShapeVignetteCurve) * fADOF_ShapeVignetteAmount;
    centerVec = centerVec / centerDist * vignette;
    weightSum *= saturate(3.33 - vignette * 2.0);
    BokehSum *= weightSum;
    BokehMax *= weightSum;
#endif

    [loop]for (int iVertices = 0; iVertices < iADOF_ShapeVertices; iVertices++)
    [loop]for(float iRings = 1; iRings <= nRings; iRings++)
    [loop]for(float iSamplesPerRing = 0; iSamplesPerRing < iRings; iSamplesPerRing++)
    {
        float2 sampleOffset = lerp(IN.vertices[iVertices],IN.vertices[iVertices+1],iSamplesPerRing/iRings);
        ShapeRoundness(sampleOffset,fADOF_ShapeCurvatureAmount);

        float4 sampleBokeh  = TextureColor.SampleLevel(LinearSampler, IN.txcoord.xy + sampleOffset.xy * (bokehRadiusScaled * iRings),0);
        float sampleWeight  = saturate(1e6 * (abs(sampleBokeh.a) - CoC * (float)iRings) + 1.0);
        // float sampleWeight = saturate((abs(sampleBokeh.a) + CoC * (2.0 - iRings + abs(sampleBokeh.a)))/(4.0*CoC)); //mcfly '17 v2, smooth transition between quality steps

#if E_OPTICAL_VIGNETTE
        OpticalVignette(sampleOffset.xy * iRings/nRings, centerVec, sampleWeight);
#endif
        sampleBokeh.rgb *= sampleWeight;
        weightSum       += sampleWeight;
        BokehSum        += sampleBokeh;
        BokehMax         = max(BokehMax,sampleBokeh);
    }

    return lerp(BokehSum / weightSum, BokehMax, fADOF_BokehIntensity);
}

float4  PS_DoF_Combine(VS_OUTPUT_POST IN) : SV_Target
{

    float4 blurredColor     = TextureColor.Sample(LinearSampler,  IN.txcoord.xy * fADOF_RenderResolutionMult); //Median3x3Upscale(TextureColor, IN.txcoord.xy, fADOF_RenderResolutionMult);
    float4 originalColor    = TextureOriginal.Sample(PointSampler,  IN.txcoord.xy);

    float CoC               = CircleOfConfusion(IN.txcoord.xy, 0);
    float CoCblurred        = blurredColor.a;

    float bokehRadiusPixels = abs(CoC2BlurRadius(CoC).x * ScreenSize.x);

    float blendWeight       = linearstep(DISCRADIUS_RESOLUTION_BOUNDARY_LOWER, DISCRADIUS_RESOLUTION_BOUNDARY_UPPER, bokehRadiusPixels);
          blendWeight       = pow(blendWeight,DISCRADIUS_RESOLUTION_BOUNDARY_CURVE);

    float4 BokehSum;
    BokehSum.rgb            = lerp(originalColor.rgb, blurredColor.rgb, blendWeight);
    BokehSum.a              = saturate(abs(lerp(CoC,CoCblurred,blendWeight*0)) * GAUSSIAN_BUILDUP_MULT);

    return BokehSum;
}

float4  PS_DoF_Gauss(uniform float2 axis, uniform bool overrideAlpha, VS_OUTPUT_POST IN) : SV_Target
{
    float4 centerTap = TextureColor.Sample(LinearSampler, IN.txcoord.xy);

    float nSteps        = floor(centerTap.a * (fADOF_SmootheningAmount + 3.0));
    float expCoeff      = -2.0 * rcp(nSteps * nSteps + 1e-3); // sigma adjusted for blur width
    float2 blurAxisScaled   = axis * PixelSize.xy;

    float4 gaussianSum = 0.0;
    float  gaussianSumWeight = 1e-3;

    for(float iStep = -nSteps; iStep <= nSteps; iStep++)
    {
        float currentWeight = exp(iStep * iStep * expCoeff);
        float currentOffset = 2.0 * iStep - 0.5; // Sample between texels to double blur width at no cost

        float4 currentTap = TextureColor.SampleLevel(LinearSampler, IN.txcoord.xy + blurAxisScaled.xy * currentOffset, 0);

        currentWeight *= saturate(currentTap.a - centerTap.a * 0.25);

        gaussianSum += currentTap * currentWeight;
        gaussianSumWeight += currentWeight;
    }

    gaussianSum /= gaussianSumWeight;

    float4 BokehSum = lerp(centerTap, gaussianSum, saturate(gaussianSumWeight));
#if E_CHROMATIC_ABERRATION
    if(overrideAlpha == true) BokehSum.a = CircleOfConfusion(IN.txcoord.xy, 0);
#else
    if(overrideAlpha == true) BokehSum.a = 1;    // fix potential bugs in enbeffect due to wrong code
#endif
    return BokehSum;
}

#if E_CHROMATIC_ABERRATION
float4  PS_DoF_ChromaticAberration(VS_OUTPUT_POST IN, float4 vPos : SV_POSITION) : SV_Target
{
    float4 colorVals[5];

    colorVals[0] = TextureColor.Load(int3(vPos.x, vPos.y, 0));
    colorVals[1] = TextureColor.Load(int3(vPos.x - 1, vPos.y, 0)); //L
    colorVals[2] = TextureColor.Load(int3(vPos.x, vPos.y - 1, 0)); //T
    colorVals[3] = TextureColor.Load(int3(vPos.x + 1, vPos.y, 0)); //R
    colorVals[4] = TextureColor.Load(int3(vPos.x, vPos.y + 1, 0)); //B

    float CoC           = abs(colorVals[0].a);
    float2 bokehRadiusScaled    = CoC2BlurRadius(CoC);

    float4 vGradTwosided = float4(dot(colorVals[0].rgb - colorVals[1].rgb, 1),   //C - L
                                  dot(colorVals[0].rgb - colorVals[2].rgb, 1),   //C - T
                                  dot(colorVals[3].rgb - colorVals[0].rgb, 1),   //R - C
                                  dot(colorVals[4].rgb - colorVals[0].rgb, 1));      //B - C

    float2 vGrad = min(vGradTwosided.xy, vGradTwosided.zw);

    float vGradLen = sqrt(dot(vGrad,vGrad)) + 1e-6;
    vGrad = vGrad / vGradLen * saturate(vGradLen * 32.0) * bokehRadiusScaled * 0.125 * fADOF_ShapeChromaAmount;

    float4 chromaVals[3];

    chromaVals[0] = colorVals[0];
    chromaVals[1] = TextureColor.Sample(LinearSampler, IN.txcoord.xy + vGrad);
    chromaVals[2] = TextureColor.Sample(LinearSampler, IN.txcoord.xy - vGrad);

    chromaVals[1].rgb = lerp(chromaVals[0].rgb, chromaVals[1].rgb, saturate(4.0 * abs(chromaVals[1].w)));
    chromaVals[2].rgb = lerp(chromaVals[0].rgb, chromaVals[2].rgb, saturate(4.0 * abs(chromaVals[2].w)));

    uint3 chromaMode = (uint3(0,1,2) + iADOF_ShapeChromaMode.xxx) % 3;

    float4 BokehSum;
    BokehSum.rgb = float3(chromaVals[chromaMode.x].r,
                   chromaVals[chromaMode.y].g,
                   chromaVals[chromaMode.z].b);
    BokehSum.a = 1.0;

    return BokehSum;
}
#endif

///// RAINDROPS FX ///////////////////////////////////////////////////

float4  PS_Copy(VS_OUTPUT_POST IN, float4 v0 : SV_Position0, uniform Texture2D tex) : SV_Target
{
    return tex.Load(int3(v0.xy, 0));
}

float4  PS_RainGeneration(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    ///// WEATHER SYSTEM /////////////////////////////////////////////
   
    float  F_Rain_Size;
    float  F_Rain_Fade;
    float  F_Rain_Chance;
//  float  F_Rain_Range; // used in another Pixal Shader
    float  F_Rain_Slide;
    float  F_Rain_Dispersion;

    float2 rainstrength;
    float2 rainlvl;
    float  EffectLevel;
    float  EffectStrength;
    float  tStep4 = step(Weather.z, 0.0);
    float  tStep6 = step(Weather.z, 1.0);

    ///// CURRENT WEATHER INDEX
   
    // Rain Level 1
    if(Weather.x > (RAIN_START - 0.2) && Weather.x < (RAIN_ENDS + 0.2)) 
        rainstrength.x = 1, rainlvl.x = 0;

    // Rain Level 2
    else if(Weather.x > (STORM_START - 0.2) && Weather.x < (STORM_ENDS + 0.2)) 
        rainstrength.x = 1, rainlvl.x = 1;
 
    // Rain Level 0
    else rainstrength.x = 0;
    
    ///// OUTGOING WEATHER INDEX
    
    // Rain Level 1
    if(Weather.y > (RAIN_START - 0.2) && Weather.y < (RAIN_ENDS + 0.2)) 
        rainstrength.y = 1, rainlvl.y = 0;

    // Rain Level 2
    else if(Weather.y > (STORM_START - 0.2) && Weather.y < (STORM_ENDS + 0.2)) 
        rainstrength.y = 1, rainlvl.y = 1;
    
    // Rain Level 0
    else rainstrength.y = 0;
    
    ///// PARAMETERS
    
    EffectStrength      = lerp(rainstrength.x, rainstrength.y, lerp(tStep6, tStep4, rainstrength.y));
    EffectLevel         = lerp(rainlvl.y, rainlvl.x, Weather.z); //step(0.5, Weather.z));

    F_Rain_Size         = lerp(UI_Rain_Size_Lvl1, UI_Rain_Size_Lvl2, EffectLevel);
    F_Rain_Chance       = lerp(UI_Rain_Chance_Lvl1, UI_Rain_Chance_Lvl2, EffectLevel) * EffectStrength;
    F_Rain_Fade         = lerp(UI_Rain_Fade_Lvl1, UI_Rain_Fade_Lvl2, EffectLevel) * EffectStrength + 0.01;
    F_Rain_Slide        = lerp(UI_Rain_Slide_Lvl1, UI_Rain_Slide_Lvl2, EffectLevel);
    F_Rain_Dispersion   = lerp(UI_Rain_Dispersion_Lvl1, UI_Rain_Dispersion_Lvl2, EffectLevel);

    F_Rain_Chance      *= (1 - EInteriorFactor);
        
    ///// RAIN SHADER ////////////////////////////////////////////////

    float4 res;
    float2 uv        = IN.txcoord.xy;

    float2 center    = round(v0.xy / F_Rain_Size) * F_Rain_Size;
    float3 rand      = integerHash3(int3(center, Timer.x * 16677216.0));

    float size       = F_Rain_Size * (0.5 + rand.y);
    res.xyz          = rand.x > (1.0 - F_Rain_Chance) ? 1.0 : 0.0;
    res             *= saturate(1.0 - length(center - v0.xy) / (size * 0.5));
    res.xy          *= clamp(((center - v0.xy) / (size * 0.5)), -1.0, 1.0);

    float4 prev      = filter4x4(RenderTargetR32F, float2(uv.x + (rand.x - 0.5) * F_Rain_Dispersion * 0.005, uv.y - F_Rain_Slide * 0.01 + F_Rain_Dispersion * (rand.z - 0.5) * 0.01), PixelSize);// RT(RGBA64F).Load(int3(v0.x, v0.y - F_Rain_Slide, 0));
    res              = lerp(prev, res, F_Rain_Fade);

    res.w            = 1.0;
    return res;
}

float4  PS_DrawRain(VS_OUTPUT IN, float4 v0 : SV_Position0) : SV_Target
{
    return RenderTargetRGBA64F.Sample(LinearSampler, IN.txcoord.xy + filter4x4(RenderTargetR32F, IN.txcoord.xy, PixelSize) * UI_Rain_Range * 10.0);
}

///// TECHNIQUES /////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

#define TECH11(NAME, VERTEXSHADER, PIXELSHADER)\
technique11 NAME { pass p0 { SetVertexShader(CompileShader(vs_5_0, VERTEXSHADER)); SetPixelShader(CompileShader(ps_5_0, PIXELSHADER)); }}

///// DOF ////////////////////////////////////////////////////////////

TECH11(ReadFocus,                                               VS_Draw(),                              PS_ReadFocus())
TECH11(Focus,                                                   VS_Draw(),                              PS_Focus())
TECH11(DOF <string UIName="REACTOR DOF";>,                      VS_Draw(),                              PS_CoC())
TECH11(DOF1,                                                    VS_DoF(fADOF_RenderResolutionMult),     PS_DoF_Main())
TECH11(DOF2,                                                    VS_Draw(),                              PS_DoF_Combine())
TECH11(DOF3,                                                    VS_Draw(),                              PS_DoF_Gauss(float2(0,1),0))
TECH11(DOF4,                                                    VS_Draw(),                              PS_DoF_Gauss(float2(1,0),1))
#if E_CHROMATIC_ABERRATION
TECH11(DOF5,                                                    VS_Draw(),                              PS_DoF_ChromaticAberration())
#endif

///// RAIN ///////////////////////////////////////////////////////////

TECH11(RAIN <string UIName="REACTOR RAIN"; 
string RenderTarget="RenderTargetRGBA64F";>,                    VS_Draw(),                              PS_Copy(TextureColor))
TECH11(RAIN1,                                                   VS_Draw(),                              PS_RainGeneration())
TECH11(RAIN2<string RenderTarget="RenderTargetR32F";>,          VS_Draw(),                              PS_Copy(TextureColor))
TECH11(RAIN3,                                                   VS_Draw(),                              PS_DrawRain())

///// DOF + RAIN /////////////////////////////////////////////////////

TECH11(DOF_RAIN <string UIName="REACTOR DOF+RAIN";>,            VS_Draw(),                              PS_CoC())
TECH11(DOF_RAIN1,                                               VS_DoF(fADOF_RenderResolutionMult),     PS_DoF_Main())
TECH11(DOF_RAIN2,                                               VS_Draw(),                              PS_DoF_Combine())
TECH11(DOF_RAIN3,                                               VS_Draw(),                              PS_DoF_Gauss(float2(0,1),0))
TECH11(DOF_RAIN4,                                               VS_Draw(),                              PS_DoF_Gauss(float2(1,0),1))
#if E_CHROMATIC_ABERRATION
TECH11(DOF_RAIN5,                                               VS_Draw(),                              PS_DoF_ChromaticAberration())
TECH11(DOF_RAIN6 <string RenderTarget="RenderTargetRGBA64F";>,  VS_Draw(),                              PS_Copy(TextureColor))
TECH11(DOF_RAIN7,                                               VS_Draw(),                              PS_RainGeneration())
TECH11(DOF_RAIN8 <string RenderTarget="RenderTargetR32F";>,     VS_Draw(),                              PS_Copy(TextureColor))
TECH11(DOF_RAIN9,                                               VS_Draw(),                              PS_DrawRain())
#else
TECH11(DOF_RAIN5 <string RenderTarget="RenderTargetRGBA64F";>,  VS_Draw(),                              PS_Copy(TextureColor))
TECH11(DOF_RAIN6,                                               VS_Draw(),                              PS_RainGeneration())
TECH11(DOF_RAIN7 <string RenderTarget="RenderTargetR32F";>,     VS_Draw(),                              PS_Copy(TextureColor))
TECH11(DOF_RAIN8,                                               VS_Draw(),                              PS_DrawRain())
#endif