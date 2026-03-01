//+++++++++++++++++++++++++++++++++++++++++++++++++++++//
//            Contains ENB Post-Process v2             //
//           used by the Modular Shader files          //
//-----------------------CREDITS-----------------------//
// JawZ: Author and developer of this file             //
// Boris: Initial author of ENB post-process method    //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//


// This helper file is specifically only for use in the enbeffect.fx!
// The below list is only viable if the mshelpers.fxh is loaded/included into the enbeffect.fx file!


/***List of available fetches**********************************************
 * - PI // value of PI                                                    *
 * - TODEI1(TODfactors(), Var_ESr, Var_ED, Var_ESs, Var_EN, Var_ISr, Var_ID, Var_ISs, Var_IN)
 * - TODEI3(TODfactors(), Var_ESr, Var_ED, Var_ESs, Var_EN, Var_ISr, Var_ID, Var_ISs, Var_IN)
 * - TODE1(TODfactors(), Var_ESr, Var_ED, Var_ESs, Var_EN)                *
 * - TODE3(TODfactors(), Var_ESr, Var_ED, Var_ESs, Var_EN)                *
 * - DNEIFactor(Var_ED, Var_EN, Var_ID, Var_IN)                           *
 * - DNIFactor(Var_D, Var_N, Var_I)                                       *
 * - GreyScale(_s4, IN.txcoord0.xy)                                       *
 * - AvgLuma(color.rgb).x  // or .y or .z or .w, never ever .xyzw!        *
 * - LogLuma(color.rgb)                                                   *
 * - random(uv.xy)                                                        *
 * - RGBtoXYZ(color.rgb)                                                  *
 * - XYZtoYxy(XYZ.xyz)                                                    *
 * - YxytoXYZ(XYZ.xyz, Yxy.rgb)                                           *
 * - XYZtoRGB(XYZ.xyz)                                                    *
 * - RGBToHSL(color.rgb)                                                  *
 * - HSLToRGB(hsl.rgb)                                                    *
 * - RGBtoHSV(color.rgb)                                                  *
 * - HSVtoRGB(hsv.rgb)                                                    *
 * - BlendLuma(hslbase.rgb, hslblend.rgb)                                 *
 * - linearDepth(Depth, fFromFarDepth, fFromNearDepth)                    *
 * - FuncBlur(inputtex, IN.txcoord0.xy, srcsize, destsize)                *
 * - SplitScreen(_s0, color, IN.txcoord0.xy, fSplitscreenPos)             *
 * - ClipMode(color.rgb)                                                  *
 * - ShowDepth(color.rgb, IN.txcoord0.xy, fFromFarDepth, fFromNearDepth   *
 **************************************************************************/
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// EXTERNAL PARAMETERS BEGINS HERE, SHOULD NOT BE MODIFIED UNLESS YOU KNOW WHAT YOU ARE DOING
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++



// ------------------- //
//   GUI ANNOTATIONS   //
// ------------------- //
  #define ENABLE_PP2 true
//{
	// Exterior controls
// Day
float INFO2 <string UIName="++DAY++++++++++++++++++++++++++++++++++++++";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};
    float AdaptationMinDay <
        string UIName="PP2: Adaptation Min Day";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;
    > = {0.05};
    float AdaptationMaxDay <
        string UIName="PP2: Adaptation Max Day";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;
    > = {0.05};
	float BrightnessDay <
        string UIName="PP2: Brightness Day";                  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;
    > = {1.0};
    float IntensityContrastDay <
        string UIName="PP2: Intensity Contrast Day";          string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;
    > = {1.0};
    float SaturationDay <
        string UIName="PP2: Color Saturation Day";            string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;
    > = {1.0};
    float ToneMappingOversaturationDay <
        string UIName="PP2: Tonemapping Oversaturation Day";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1000.0;
    > = {180.0};
    float ToneMappingCurveDay <
        string UIName="PP2: Tonemapping Curve Day";           string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=50.0;
    > = {8.0};

	float INFO3 <string UIName="++NIGHT++++++++++++++++++++++++++++++++++++++";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};
// Night
	float AdaptationMinNight <
        string UIName="PP2: Adaptation Min Night";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;
    > = {0.05};
    float AdaptationMaxNight <
        string UIName="PP2: Adaptation Max Night";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;
    > = {0.05};
	float BrightnessNight <
        string UIName="PP2: Brightness Night";                  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;
    > = {1.0};
    float IntensityContrastNight <
        string UIName="PP2: Intensity Contrast Night";          string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;
    > = {1.0};
    float SaturationNight <
        string UIName="PP2: Color Saturation Night";            string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;
    > = {1.0};
    float ToneMappingOversaturationNight <
        string UIName="PP2: Tonemapping Oversaturation Night";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1000.0;
    > = {180.0};
    float ToneMappingCurveNight <
        string UIName="PP2: Tonemapping Curve Night";           string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=50.0;
    > = {8.0};

	float INFO4 <string UIName="++INTERIOR++++++++++++++++++++++++++++++++++++++";  string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;> = {0.0};
	// Interior controls
	float AdaptationMinInterior <
        string UIName="PP2: Adaptation Min Interior";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;
    > = {0.05};
    float AdaptationMaxInterior <
        string UIName="PP2: Adaptation Max Interior";              string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=100.0;  float UIStep=0.001;
    > = {0.05};
    float BrightnessInterior <
        string UIName="PP2: Brightness Interior";                  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;
    > = {1.0};
    float IntensityContrastInterior <
        string UIName="PP2: Intensity Contrast Interior";          string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;
    > = {1.0};
    float SaturationInterior <
        string UIName="PP2: Color Saturation Interior";            string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=10.0;
    > = {1.0};
    float ToneMappingOversaturationInterior <
        string UIName="PP2: Tonemapping Oversaturation Interior";  string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=1000.0;
    > = {180.0};
    float ToneMappingCurveInterior <
        string UIName="PP2: Tonemapping Curve Interior";           string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=50.0;
    > = {8.0};
//}


// ------------------- //
//   HELPER CONSTANTS  //
// ------------------- //




// ------------------- //
//   HELPER FUNCTIONS  //
// ------------------- //

float3 enbPP(float3 inColor, float3 inAdaptation)
{
	/// Time and Location interpolators ++++++++++++++++++++++++++++++++++++++++++++++++++++++///

  float EAdaptationMinV2   = lerp(lerp(AdaptationMinNight, AdaptationMinDay, ENightDayFactor), AdaptationMinInterior, EInteriorFactor);
  float EAdaptationMaxV2   = lerp(lerp(AdaptationMaxNight, AdaptationMaxDay, ENightDayFactor), AdaptationMaxInterior, EInteriorFactor);

  float EBrightnessV2   = lerp(lerp(BrightnessNight, BrightnessDay, ENightDayFactor), BrightnessInterior, EInteriorFactor);
  float EIntensityContrastV2   = lerp(lerp(IntensityContrastNight, IntensityContrastDay, ENightDayFactor), IntensityContrastInterior, EInteriorFactor);
  float EColorSaturationV2   = lerp(lerp(SaturationNight, SaturationDay, ENightDayFactor), SaturationInterior, EInteriorFactor);

  float EToneMappingOversaturationV2   = lerp(lerp(ToneMappingOversaturationNight, ToneMappingOversaturationDay, ENightDayFactor), ToneMappingOversaturationInterior, EInteriorFactor);
  float EToneMappingCurveV2   = lerp(lerp(ToneMappingCurveNight, ToneMappingCurveDay, ENightDayFactor), ToneMappingCurveInterior, EInteriorFactor);

///+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++///
  if (ENABLE_PP2==true)
  {
    float grayadaptation = AvgLuma(inAdaptation.xyz).y;
      grayadaptation     = max(grayadaptation, 0.0);
      grayadaptation     = min(grayadaptation, 50.0);
      inColor.xyz        = inColor.xyz / (grayadaptation * EAdaptationMaxV2 + EAdaptationMinV2);

    inColor.xyz  *= EBrightnessV2;
    inColor.xyz  += 0.000001;
    float3 xncol  = normalize(inColor.xyz);
    float3 scl    = inColor.xyz / xncol.xyz;
    scl           = pow(scl, EIntensityContrastV2);
    xncol.xyz     = pow(xncol.xyz, EColorSaturationV2);
    inColor.xyz   = scl * xncol.xyz;
    float lumamax = EToneMappingOversaturationV2;
    inColor.xyz   = (inColor.xyz * (1.0 + inColor.xyz / lumamax)) / (inColor.xyz + EToneMappingCurveV2);
  }

  return inColor;
}
