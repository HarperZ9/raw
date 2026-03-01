/////////////////////////////\\\\\\\\\\\\\\\\\\\\\\\\\\\\
//                      MSL HELPER                     //
/////////////////////////////////////////////////////////

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

// ------------------- //
//   HELPER CONSTANTS  //
// ------------------- //

// PI, required to calculate Gaussian weight
static const float PI = 3.1415926535897932384626433832795;



// ------------------- //
//   HELPER FUNCTIONS  //
// ------------------- //

// Compute the average of the 4 necessary samples
float4 GreyScale(uniform SamplerState inSampler, float2 inTexCoords)
{
    float average = 0.0f;
    float maximum = -1e20;  /// 1e20 = 10000000000000000000.0
    float4 lum    = 0.0f;

        lum = tex2D(inSampler, inTexCoords.xy);

        float GreyValue = max(max(lum.r, lum.g), lum.b);  /// Compute the luminance component as per the HSL color space
        //float GreyValue = max(lum.r, max(lum.g, lum.b)); /// Take the maximum value of the incoming, same as computing the brightness/value for an HSV/HSB conversion
        //float GreyValue = 0.5f * (max(lum.r, max(lum.g, lum.b)) + min(lum.r, min(lum.g, lum.b))); /// Compute the luminance component as per the HSL color space
        //float GreyValue = length(lum.rgb); /// Use the magnitude of the color

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
                  max(max(inColor.x, inColor.y), inColor.z),                       /// Compute the luminance component as per the HSL color space
                  sqrt((inColor.x*inColor.x*0.2125f)+(inColor.y*inColor.y*0.7154f)+(inColor.z*inColor.z*0.0721f)));
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
    float3 HSLBase 	= RGBToHSL(base);
    float3 HSLBlend	= RGBToHSL(blend);
    return HSLToRGB(float3(HSLBase.x, HSLBase.y, HSLBlend.z));
}

// Pseudo Random Number generator.
float random(in float2 uv)
{
    float2 noise = (frac(sin(dot(uv , float2(12.9898,78.233) * 2.0)) * 43758.5453));
    return abs(noise.x + noise.y) * 0.5;
}

// Linear depth
float linearDepth(float d, float n, float f)
{
    return (2.0 * n)/(f + n - d * (f - n));
}
float linearDepth2(float nonLinDepth, float fZNear, float fZFar)
{
  float LinDepth = 1.0/max(1.0-nonLinDepth, 0.0000000001);
    LinDepth = -fZFar * fZNear / (LinDepth * (fZFar - fZNear) - fZFar);

  return LinDepth;
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


//////////////////////END OF MSL FUNCTION //////////////
