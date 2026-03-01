//+++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                 Contains HDR Combo                  //
//           used by the Modular Shader files          //
//-----------------------CREDITS-----------------------//
// JawZ: Author and developer of this file             //
// Erik Reinhard: Photographic Tone Reproduction       //
// Michael Stark: Photographic Tone Reproduction       //
// Peter Shirley: Photographic Tone Reproduction       //
// James Ferwerda: Photographic Tone Reproduction      //
// John Hable: Filmic Uncharted2D                      //
// Charles Poynton: Color FAQ                          //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

// This helper file is specifically only for use in the enbeffect.fx shader file!
// The below list is only viable if the mshelpers.fxh is loaded/included into the enbeffect.fx file!


/***List of available fetches**********************************************
 * - PI // value of PI                                                    *
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
 * - SplitScreen(_s0, color, IN.txcoord0.xy, fSplitscreenPos)             *
 * - ClipMode(color.rgb)                                                  *
 **************************************************************************/


// ------------------- //
//   GUI ANNOTATIONS   //
// ------------------- //
float EmptyHDR1 <
      string UIName="------------------------------------";                 string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;
  > = {0.0};
  bool ENABLE_HDRU2D <
    string UIName = "TONEMAPING";
  > = {true};
#define ADAPT_HDRU2D false
//{
float EmptyHDR2 <
      string UIName="--DAY-------------------------------";                 string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;
  > = {0.0};
    float fKeyValueD <
        string UIName="   Exposure Day";           string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.001;
    > = {1.0};
    float ShoulderStrengthD <
        string UIName="   Shoulder Strength Day";  string UIWidget="Spinner";  float UIMin=0.001;  float UIMax=2.0;
    > = {0.16};
    float LinearStrengthD <
        string UIName="   Linear Strength Day";    string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=5.0;
    > = {0.2};
    float LinearAngleD <
        string UIName="   Linear Angle Day";       string UIWidget="Spinner";  float UIMin=0.001;  float UIMax=1.0;
    > = {0.1};
    float ToeStrengthD <
        string UIName="   Toe Strength Day";       string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=2.0;
    > = {0.2};
    float ToeNumeratorD <
        string UIName="   Toe Numerator Day";      string UIWidget="Spinner";  float UIMin=0.001;  float UIMax=0.5;  float UIStep=0.001;
    > = {0.001};
    float ToeDenominatorD <
        string UIName="   Toe Denominator Day";    string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=2.0;
    > = {0.2};
    float LinearWhiteD <
        string UIName="   Linear White Day";       string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=50.0;
    > = {1.0};

    float EmptyHDR3 <
          string UIName="--NIGHT-----------------------------";                 string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;
      > = {0.0};
	float fKeyValueN <
        string UIName="   Exposure Night";           string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.001;
    > = {1.0};
	float ShoulderStrengthN <
        string UIName="   Shoulder Strength Night";  string UIWidget="Spinner";  float UIMin=0.001;  float UIMax=2.0;
    > = {0.16};
	float LinearStrengthN <
        string UIName="   Linear Strength Night";    string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=5.0;
    > = {0.2};
	float LinearAngleN <
        string UIName="   Linear Angle Night";       string UIWidget="Spinner";  float UIMin=0.001;  float UIMax=1.0;
    > = {0.1};
	float ToeStrengthN <
        string UIName="   Toe Strength Night";       string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=2.0;
    > = {0.2};
	float ToeNumeratorN <
        string UIName="   Toe Numerator Night";      string UIWidget="Spinner";  float UIMin=0.001;  float UIMax=0.5;  float UIStep=0.001;
    > = {0.001};
	float ToeDenominatorN <
        string UIName="   Toe Denominator Night";    string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=2.0;
    > = {0.2};
	float LinearWhiteN <
        string UIName="   Linear White Night";       string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=50.0;
    > = {1.0};

  float EmptyHDR4 <
        string UIName="--INTERIOR-------------------------";                 string UIWidget="spinner";  float UIMin=0.0;  float UIMax=0.0;
    > = {0.0};
  float fKeyValueI <
        string UIName="   Exposure Int";           string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=5.0;  float UIStep=0.001;
    > = {1.0};
  float ShoulderStrengthI <
        string UIName="   Shoulder Strength Int";  string UIWidget="Spinner";  float UIMin=0.001;  float UIMax=2.0;
    > = {0.16};
  float LinearStrengthI <
        string UIName="   Linear Strength Int";    string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=5.0;
    > = {0.2};
  float LinearAngleI <
        string UIName="   Linear Angle Int";       string UIWidget="Spinner";  float UIMin=0.001;  float UIMax=1.0;
    > = {0.1};
  float ToeStrengthI <
        string UIName="   Toe Strength Int";       string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=2.0;
    > = {0.2};
  float ToeNumeratorI <
        string UIName="   Toe Numerator Int";      string UIWidget="Spinner";  float UIMin=0.001;  float UIMax=0.5;  float UIStep=0.001;
    > = {0.001};
  float ToeDenominatorI <
        string UIName="   Toe Denominator Int";    string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=2.0;
    > = {0.2};
  float LinearWhiteI <
        string UIName="   Linear White Int";       string UIWidget="Spinner";  float UIMin=0.0;  float UIMax=50.0;
    > = {1.0};

//}


// ------------------- //
//   HELPER CONSTANTS  //
// ------------------- //




// ------------------- //
//   HELPER FUNCTIONS  //
// ------------------- //

float3 msHDRTonemap(float3 inColor, float3 inBlur, float3 inAdaptation)
{
	/// Time and Location interpolators ++++++++++++++++++++++++++++++++++++++++++++++++++++++///
  float fKeyValue   = lerp(lerp(fKeyValueN, fKeyValueD, ENightDayFactor), fKeyValueI, EInteriorFactor);
  float ShoulderStrength   = lerp(lerp(ShoulderStrengthN, ShoulderStrengthD, ENightDayFactor), ShoulderStrengthI, EInteriorFactor);
  float LinearStrength   = lerp(lerp(LinearStrengthN, LinearStrengthD, ENightDayFactor), LinearStrengthI, EInteriorFactor);

  float LinearAngle   = lerp(lerp(LinearAngleN, LinearAngleD, ENightDayFactor), LinearAngleI, EInteriorFactor);
  float ToeStrength   = lerp(lerp(ToeStrengthN, ToeStrengthD, ENightDayFactor), ToeStrengthI, EInteriorFactor);
  float ToeNumerator   = lerp(lerp(ToeNumeratorN, ToeNumeratorD, ENightDayFactor), ToeNumeratorI, EInteriorFactor);
  float ToeDenominator   = lerp(lerp(ToeDenominatorN, ToeDenominatorD, ENightDayFactor), ToeDenominatorI, EInteriorFactor);
  float LinearWhite   = lerp(lerp(LinearWhiteN, LinearWhiteD, ENightDayFactor), LinearWhiteI, EInteriorFactor);

///++++++++++++++++++++++++++++++///
  float A = ShoulderStrength;    ///
  float B = LinearStrength;      ///
  float C = LinearAngle;         ///
  float D = ToeStrength;         ///
  float E = ToeNumerator;        ///
  float F = ToeDenominator;      ///
  float W = LinearWhite;         ///
///++++++++++++++++++++++++++++++///



  /// Color Space Conversion STARTS
    float3 XYZ = RGBtoXYZ(inColor.rgb);  /// Color space conversion, from RGB to XYZ
    float3 Yxy = XYZtoYxy(XYZ.xyz);      /// Color space conversion, from XYZ to Yxy

  /// Tonemapping and Exposure
    Yxy.r *= fKeyValue;
      Yxy.r  = ((Yxy.r * (A * Yxy.r + C * B) + D * E) / (Yxy.r * (A * Yxy.r + B) + D * F)) - E / F;
      Yxy.r /= ((W * (A * W + C * B) + D * E) / (W * (A * W + B) + D * F)) - E / F;


  /// Color Space Conversion ENDS
    XYZ.xyz      = YxytoXYZ(XYZ.xyz, Yxy.xyz);  /// Color space conversion, from Yxy to XYZ
    inColor.rgb  = XYZtoRGB(XYZ.xyz);           /// Color space conversion, from XYZ to RGB


  return inColor;
}
