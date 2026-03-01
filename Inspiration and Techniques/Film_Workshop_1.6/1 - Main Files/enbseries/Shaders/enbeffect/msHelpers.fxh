//+++++++++++++++++++++++++++++++++++++++++++++++++++++//
//       Contains helper functions and constants       //
//           used by the Modular Shader files          //
//-----------------------CREDITS-----------------------//
// JawZ: Author and developer of this file             //
// Erik Reinhard: Photographic Tone Reproduction       //
// Michael Stark: Photographic Tone Reproduction       //
// Peter Shirley: Photographic Tone Reproduction       //
// James Ferwerda: Photographic Tone Reproduction      //
// easyrgb.com: Example of the RGB>XYZ>Yxy color space //
// Charles Poynton: Color FAQ                          //
// Prod80: For code inspiration and general help       //
// CeeJay.dk: Split Screen                             //
// Matso: Texture atlas tiles sampling system          //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//


// This helper file can be included in any ENBSeries .fx and .txt shader file.


// ------------------- //
//   GUI ANNOTATIONS   //
// ------------------- //


// ------------------- //
//   HELPER CONSTANTS  //
// ------------------- //

// PI, required to calculate Gaussian weight
static const float PI = 3.1415926535897932384626433832795;

// TOD time settings, using Fallout 4 default time cycle
#define TOD_DAWN_DURATION   4
#define TOD_SUNRISE_TIME    7
#define TOD_DAY_TIME        13
#define TOD_SUNSET_TIME     19
#define TOD_DUSK_DURATION   4
#define TOD_NIGHT_TIME      1

//for float parameters
#define TOD1EI(parm)    TODEI1( TODfactors(),parm##_E_Sr, parm##_E_D, parm##_E_Ss, parm##_E_N, parm##_I_Sr, parm##_I_D, parm##_I_Ss, parm##_I_N)
//for float3 parameters
#define TOD3EI(parm)    TODEI3( TODfactors(),parm##_E_Sr, parm##_E_D, parm##_E_Ss, parm##_E_N, parm##_I_Sr, parm##_I_D, parm##_I_Ss, parm##_I_N)
//for float parameters
#define TOD1E(parm)    TODE1( TODfactors(),parm##_E_Sr, parm##_E_D, parm##_E_Ss, parm##_E_N)
//for float3 parameters
#define TOD3E(parm)    TODE3( TODfactors(),parm##_E_Sr, parm##_E_D, parm##_E_Ss, parm##_E_N)


// ------------------- //
//   HELPER FUNCTIONS  //
// ------------------- //

// Returns (TOD factor sunrise, TOD factor day, TOD factor sunset, TOD factor night)
float4 TODfactors()
{
  float4 weight = Weather.w;

    weight.xy -= TOD_SUNRISE_TIME;
    weight.zw -= TOD_SUNSET_TIME;
    weight    /= float4(-TOD_DAWN_DURATION, TOD_DAY_TIME - TOD_SUNRISE_TIME, TOD_DAY_TIME - TOD_SUNSET_TIME, TOD_DUSK_DURATION);
    weight     = saturate(weight);
    weight.yz  = sin(weight.yz * 1.57);// pi/2
    weight.xw  = 1 - weight.xw;

  return float4( weight.x*(1-weight.y), weight.y*weight.z, (1-weight.z)*weight.w, 1-weight.w*weight.x);
}

// TOD Exterior/Interior Factor
float TODEI1( float4 factors, float Sr_E, float D_E, float Ss_E, float N_E, float Sr_I, float D_I, float Ss_I, float N_I)
{
  return lerp( dot(factors, float4(Sr_E, D_E, Ss_E, N_E)), dot(factors, float4(Sr_I, D_I, Ss_I, N_I)), EInteriorFactor);
}

// TOD Exterior/Interior Factor
float3 TODEI3( float4 factors, float3 Sr_E, float3 D_E, float3 Ss_E, float3 N_E, float3 Sr_I, float3 D_I, float3 Ss_I, float3 N_I)
{
  return lerp( mul(factors, float4x3(Sr_E, D_E, Ss_E, N_E)), mul(factors, float4x3(Sr_I, D_I, Ss_I, N_I)), EInteriorFactor);
}

// TOD Exterior Factor
float TODE1( float4 factors, float Sr_E, float D_E, float Ss_E, float N_E)
{
  return dot(factors, float4(Sr_E, D_E, Ss_E, N_E));
}

// TOD Exterior Factor
float3 TODE3( float4 factors, float3 Sr_E, float3 D_E, float3 Ss_E, float3 N_E)
{
  return mul(factors, float4x3(Sr_E, D_E, Ss_E, N_E));
}

// DN-EI Separation, separates a value into night and day values and applies exterior and interior values to both
float3 DNEIFactor(float3 DAYEXT, float3 NIGHTEXT, float3 DAYINT, float3 NIGHTINT)
{
    float3 NDE = lerp(NIGHTEXT, DAYEXT, ENightDayFactor);
    float3 NDI = lerp(NIGHTINT, DAYINT, ENightDayFactor);
  return lerp(NDE, NDI, EInteriorFactor);
}

// DNI Separation, separates a value into Exterior Night, Day and Interior values
float3 DNIFactor(float3 DAY, float3 NIGHT, float3 INT)
{
  return lerp(lerp(NIGHT, DAY, ENightDayFactor), INT, EInteriorFactor);
}

// Compute the average of the 4 necessary samples
float4 GreyScale(uniform SamplerState inSampler, float2 inTexCoords)
{
  float average = 0.0f;
  float maximum = -1e20;  /// 1e20 = 10000000000000000000.0
  float4 lum    = 0.0f;

    lum = tex2D(inSampler, inTexCoords.xy);

    float GreyValue = max(max(lum.r, lum.g), lum.b);  /// Compute the luminance component as per the HSL colour space
    //float GreyValue = max(lum.r, max(lum.g, lum.b)); /// Take the maximum value of the incoming, same as computing the brightness/value for an HSV/HSB conversion
    //float GreyValue = 0.5f * (max(lum.r, max(lum.g, lum.b)) + min(lum.r, min(lum.g, lum.b))); /// Compute the luminance component as per the HSL colour space
    //float GreyValue = length(lum.rgb); /// Use the magnitude of the colour

    maximum = max( maximum, GreyValue );
    average += (0.25f * log( 1e-5 + GreyValue )); /// 1e-5 necessary to stop the singularity at GreyValue=0, 1e-5 = 0.00001
    average = exp( average );

  return float4( average, maximum, 0.0f, 1.0f ); /// Output the luminance to the render target
}

// Luma coefficient gray value for use with color perception effects. Multiple versions
float4 AvgLuma(float3 inColor)
{
  return float4(dot(inColor, float3(0.2125f, 0.7154f, 0.0721f)),                 /// Perform a weighted average
                max(inColor.r, max(inColor.g, inColor.b)),                       /// Take the maximum value of the incoming value
                max(max(inColor.x, inColor.y), inColor.z),                       /// Compute the luminance component as per the HSL colour space
                sqrt((inColor.x*inColor.x*0.2125f)+(inColor.y*inColor.y*0.7154f)+(inColor.z*inColor.z*0.0721f)));
}

// Calculate the log-average luminance of the scene
float LogLuma(float3 inColor)
{
    float ScreenX     = ScreenSize.y;
    float sHeight     = ScreenSize.x * ScreenSize.w;
    float ScreenY     = 1.0f / sHeight;
    float ScenePixels = ScreenX * ScreenY;

  return exp( ScenePixels + ( log(1e-5 + inColor.rgb)));
}

// RGB to XYZ conversion
float3 RGBtoXYZ(float3 inColor)
{
  static const float3x3 RGB2XYZ = {0.412453f, 0.357580f, 0.180423f,
                                   0.212671f,  0.715160f, 0.072169f,
                                   0.019334f, 0.119193f,  0.950227f};
  return mul(RGB2XYZ, inColor.rgb);
}

// XYZ to Yxy conversion
float3 XYZtoYxy(float3 inXYZ)
{
   float4 inYxy = 0.0f;

   inYxy.r = inXYZ.g;                                  /// Copy luminance Y
   inYxy.g = inXYZ.r / (inXYZ.r + inXYZ.g + inXYZ.b ); /// x = X / (X + Y + Z)
   inYxy.b = inXYZ.g / (inXYZ.r + inXYZ.g + inXYZ.b ); /// y = Y / (X + Y + Z)

  return inYxy.rgb;
}

// Yxy to XYZ conversion
float3 YxytoXYZ(float3 inXYZ, float3 inYxy)
{
    inXYZ.r = inYxy.r * inYxy.g / inYxy. b;                /// X = Y * x / y
    inXYZ.g = inYxy.r;                                     /// Copy luminance Y
    inXYZ.b = inYxy.r * (1 - inYxy.g - inYxy.b) / inYxy.b; /// Z = Y * (1-x-y) / y

  return inXYZ;
  return inYxy;
}

// XYZ to RGB conversion
float3 XYZtoRGB(float3 inXYZ)
{
  static const float3x3 XYZ2RGB  = {3.240479f, -1.537150f, -0.498535f,
                                    -0.969256f, 1.875992f, 0.041556f,
                                    0.055648f, -0.204043f, 1.057311f};
  return mul(XYZ2RGB, inXYZ);
}

// RGB to HSL conversion
float3 RGBToHSL(float3 color)
{
    float3 hsl; /// init to 0 to avoid warnings ? (and reverse if + remove first part)

    float fmin = min(min(color.r, color.g), color.b);
    float fmax = max(max(color.r, color.g), color.b);
    float delta = fmax - fmin;

    hsl.z = (fmax + fmin) / 2.0;

    if (delta == 0.0) /// No chroma
    {
        hsl.x = 0.0;  /// Hue
        hsl.y = 0.0;  /// Saturation
    }
    else /// Chromatic data
    {
        if (hsl.z < 0.5)
            hsl.y = delta / (fmax + fmin); /// Saturation
        else
            hsl.y = delta / (2.0 - fmax - fmin); /// Saturation

        float deltaR = (((fmax - color.r) / 6.0) + (delta / 2.0)) / delta;
        float deltaG = (((fmax - color.g) / 6.0) + (delta / 2.0)) / delta;
        float deltaB = (((fmax - color.b) / 6.0) + (delta / 2.0)) / delta;

        if (color.r == fmax )
            hsl.x = deltaB - deltaG; /// Hue
        else if (color.g == fmax)
            hsl.x = (1.0 / 3.0) + deltaR - deltaB; /// Hue
        else if (color.b == fmax)
            hsl.x = (2.0 / 3.0) + deltaG - deltaR; /// Hue

        if (hsl.x < 0.0)
            hsl.x += 1.0; /// Hue
        else if (hsl.x > 1.0)
            hsl.x -= 1.0; /// Hue
    }

  return hsl;
}

// HUE to RGB conversion
float HueToRGB(float f1, float f2, float hue)
{
    if (hue < 0.0)
        hue += 1.0;
    else if (hue > 1.0)
        hue -= 1.0;
    float res;
    if ((6.0 * hue) < 1.0)
        res = f1 + (f2 - f1) * 6.0 * hue;
    else if ((2.0 * hue) < 1.0)
        res = f2;
    else if ((3.0 * hue) < 2.0)
        res = f1 + (f2 - f1) * ((2.0 / 3.0) - hue) * 6.0;
    else
        res = f1;

  return res;
}

// HSL to RGB conversion
float3 HSLToRGB(float3 hsl)
{
    float3 rgb;

    if (hsl.y == 0.0)
        rgb = float3(hsl.z, hsl.z, hsl.z); // Luminance
    else
    {
        float f2;

        if (hsl.z < 0.5)
            f2 = hsl.z * (1.0 + hsl.y);
        else
        f2 = (hsl.z + hsl.y) - (hsl.y * hsl.z);

        float f1 = 2.0 * hsl.z - f2;

        rgb.r = HueToRGB(f1, f2, hsl.x + (1.0/3.0));
        rgb.g = HueToRGB(f1, f2, hsl.x);
        rgb.b= HueToRGB(f1, f2, hsl.x - (1.0/3.0));
    }

  return rgb;
}

// RGB to HSV conversion
float RGBCVtoHUE(in float3 RGB, in float C, in float V)
{
  float3 Delta = (V - RGB) / C;
    Delta.rgb -= Delta.brg;
    Delta.rgb += float3(2.0f, 4.0f, 6.0f);
    Delta.brg  = step(V, RGB) * Delta.brg;

  float H;
    H = max(Delta.r, max(Delta.g, Delta.b));

  return frac(H / 6.0f);
}

// RGB to HSV conversion
float3 RGBtoHSV(in float3 RGB)
{
  float3 HSV = 0.0f;
    HSV.z    = max(RGB.r, max(RGB.g, RGB.b));
    float M  = min(RGB.r, min(RGB.g, RGB.b));
    float C  = HSV.z - M;

  if (C != 0.0f)
  {
    HSV.x = RGBCVtoHUE(RGB, C, HSV.z);
    HSV.y = C / HSV.z;
  }

  return HSV;
}

// RGB to HSV conversion
float3 HUEtoRGBhsv(in float H)
{
    float R = abs(H * 6.0f - 3.0f) - 1.0f;
    float G = 2.0f - abs(H * 6.0f - 2.0f);
    float B = 2.0f - abs(H * 6.0f - 4.0f);

  return saturate(float3(R,G,B));
}

// RGB to HSV conversion
float3 HSVtoRGB(in float3 HSV)
{
    float3 RGB = HUEtoRGBhsv(HSV.x);

  return ((RGB - 1.0f) * HSV.y + 1.0f) * HSV.z;
}

// Luminance Blend
float3 BlendLuma(float3 base, float3 blend)
{
    float3 HSLBase  = RGBToHSL(base);
    float3 HSLBlend = RGBToHSL(blend);

  return HSLToRGB(float3(HSLBase.x, HSLBase.y, HSLBlend.z));
}

// Pseudo Random Number generator
float random(in float2 uv)
{
    float2 noise = (frac(sin(dot(uv , float2(12.9898,78.233) * 2.0)) * 43758.5453));

  return abs(noise.x + noise.y) * 0.5;
}

// Linear depth
float linearDepth(float nonLinDepth, float depthNearVar, float depthFarVar)
{
  return (2.0 * depthNearVar) / (depthFarVar + depthNearVar - nonLinDepth * (depthFarVar - depthNearVar));
}

float linearDepth2(float nonLinDepth, float depthNearVar, float depthFarVar)
{
  float LinDepth = 1.0 / max(1.0 - nonLinDepth, 0.0000000001);  /// Linearize depth

//  return -depthFarVar * depthNearVar / (nonLinDepth * (depthFarVar - depthNearVar) - depthFarVar);
  return LinDepth;
}

// ENB Bloom blur
float3 FuncBlur(Texture2D inputtex, float2 uvsrc, float srcsize, float destsize)
{
  const float scale=4.0;         /// Blurring range, samples count (performance) is factor of scale*scale
//  const float srcsize=1024.0;    /// In current example just blur input texture of 1024*1024 size
//  const float destsize=1024.0;   /// For last stage render target must be always 1024*1024

    float2 invtargetsize=scale/srcsize;
    invtargetsize.y*=ScreenSize.z; //correct by aspect ratio

    float2 fstepcount;
    fstepcount=srcsize;

    fstepcount*=invtargetsize;
    fstepcount=min(fstepcount, 16.0);
    fstepcount=max(fstepcount, 2.0);

    int stepcountX=(int)(fstepcount.x+0.4999);
    int stepcountY=(int)(fstepcount.y+0.4999);

    fstepcount=1.0/fstepcount;
    float4 curr=0.0;
    curr.w=0.000001;
    float2 pos;
    float2 halfstep=0.5*fstepcount.xy;
    pos.x=-0.5+halfstep.x;
    invtargetsize *= 2.0;
    for (int x=0; x<stepcountX; x++)
    {
      pos.y=-0.5+halfstep.y;
      for (int y=0; y<stepcountY; y++)
      {
        float2 coord=pos.xy * invtargetsize + uvsrc.xy;
        float3 tempcurr=inputtex.Sample(Sampler1, coord.xy).xyz;
        float  tempweight;
        float2 dpos=pos.xy*2.0;
        float  rangefactor=dot(dpos.xy, dpos.xy);
      // Loosing many pixels here, don't program such unefficient cycle yourself!
        tempweight=saturate(1001.0 - 1000.0*rangefactor);//arithmetic version to cut circle from square
        tempweight*=saturate(1.0 - rangefactor); //softness, without it bloom looks like bokeh dof
        curr.xyz+=tempcurr.xyz * tempweight;
        curr.w+=tempweight;

        pos.y+=fstepcount.y;
      }
      pos.x+=fstepcount.x;
    }
    curr.xyz/=curr.w;

//    curr.xyz=inputtex.Sample(Sampler1, uvsrc.xy);

  return curr.xyz;
}

// Split screen, show applied effects only on a specified area of the screen. ENBSeries before and user altered After
float4 SplitScreen(float4 inColor2, float4 inColor, float2 inTexCoords, float inVar)
{
    return (inTexCoords.x < inVar) ? inColor2 : inColor;
}

// Clip Mode. Show which pixels are over and under exposed.
float3 ClipMode(float3 inColor)
{
    if (inColor.x >= 0.99999 && inColor.y >= 0.99999 && inColor.z >= 0.99999) inColor.xyz = float3(1.0f, 0.0f, 0.0f);
    if (inColor.x <= 0.00001 && inColor.y <= 0.00001 && inColor.z <= 0.00001) inColor.xyz = float3(0.0f, 0.0f, 1.0f);

  return inColor;
}

// Visulize Depth. Shows the available depth buffer.
float3 ShowDepth(float3 inColor, Texture2D texDepth, float2 inCoords, float fDepthfromFar, float fDepthFromNear)
{
    float devDepth    = texDepth.Sample(Sampler0, inCoords.xy).x;
    float devLinDepth = linearDepth(devDepth, fDepthfromFar, fDepthFromNear);
    inColor.rgb       = float3(1, 0, 0) * devLinDepth;

  return inColor;
}
