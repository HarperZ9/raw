#ifndef EOTE_COMMON_FXH
#define EOTE_COMMON_FXH
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//         EotE_Common.fxh - Core helper library for ENB of the Elders                          //
//                                                                                              //
//         Math utilities derived from LonelyKitsuune - CC BY-NC-ND 4.0                        //
//         Color science and tonemapping additions by Zain Dana Harper                          //
//                                                                                              //
//  Contains:                                                                                   //
//    Section 1: Core macros, constants, and utility functions                                  //
//    Section 2: Technique macros and generic VS/PS                                             //
//    Section 3: Time-of-day separation system                                                  //
//    Section 4: Depth linearization                                                            //
//    Section 5: Color space conversions (linear/sRGB, RGB/HSL, RGB/HSV)                       //
//    Section 6: Interpolation utilities                                                        //
//    Section 7: Noise and dithering                                                            //
//    Section 8: Color temperature (Planckian locus)                                            //
//    Section 9: ACES color transforms                                                          //
//    Section 10: Bloom/threshold utilities                                                     //
//    Section 11: Bicubic sampling                                                              //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=============================================================================//
//  Section 1: Core macros, constants, and utility functions                    //
//=============================================================================//

#ifndef LUM_709
#define LUM_709  float3(0.2126, 0.7152, 0.0722)
#endif
#ifndef LUM_601
#define LUM_601  float3(0.2989, 0.5870, 0.1140)
#endif
#ifndef K_LUM
#define K_LUM    float3(0.25, 0.60, 0.15)
#endif
#ifndef DELTA
#define DELTA    1e-6
#endif
#ifndef PI
#define PI       3.1415926535897932384626433832795
#endif
#ifndef TWO_PI
#define TWO_PI   6.2831853071795864769252867665590
#endif
#ifndef INV_PI
#define INV_PI   0.3183098861837906715377675267450
#endif
#ifndef HALF_PI
#define HALF_PI  1.5707963267948966192313216916398
#endif

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float2 ScreenRes = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);

#define zerolim(a)  max(a, 0.0)
#define deltalim(a) max(a, DELTA)

float  max2(float2 a) { return max(a.x, a.y); }
float  min2(float2 a) { return min(a.x, a.y); }
float  max3(float3 a) { return max(a.x, max(a.y, a.z)); }
float  min3(float3 a) { return min(a.x, min(a.y, a.z)); }
float  max4(float4 a) { return max(max(a.x, a.y), max(a.z, a.w)); }
float  min4(float4 a) { return min(min(a.x, a.y), min(a.z, a.w)); }

float Luminance(float3 c) { return dot(c, LUM_709); }

float3 ColorToChroma(float3 Color, float3 LumaWeight)
{ return Color / deltalim(dot(Color, LumaWeight)); }

float3 ColorToChroma(float3 Color)
{ return ColorToChroma(Color, 0.333333); }


//----------------------------------------------------------------------------------------------//
// Scale fullscreen quad through vertex shader

void ScaleScreenQuad_Mult(inout float2 Pos, float2 Scale)
{
    Pos  = Pos * Scale + float2(-1.0, 1.0);
    Pos += float2(Scale.x, -Scale.y);
}

void ScaleScreenQuad_Res(inout float2 Pos, float2 TargetRes)
{ ScaleScreenQuad_Mult(Pos, TargetRes / ScreenRes); }


//----------------------------------------------------------------------------------------------//
// Angle (degrees) to 2D unit direction vector

float2 GetDirVec(float AngleDeg)
{
    float Rad = radians(AngleDeg);
    float S, C;
    sincos(Rad, S, C);
    return float2(C, S);
}


//----------------------------------------------------------------------------------------------//
// 2D rotation matrix

float2 MatrixRotate(float2 Pos, float Angle, bool UseRadians)
{
    float Rad = UseRadians ? Angle : radians(Angle);
    float S, C;
    sincos(Rad, S, C);
    return float2(Pos.x * C - Pos.y * S,
                  Pos.x * S + Pos.y * C);
}


//----------------------------------------------------------------------------------------------//
// Utility functions

float  MapToRange(float  x, float  Low, float  High) { return lerp(Low, High, x); }
float2 MapToRange(float2 x, float2 Low, float2 High) { return lerp(Low, High, x); }

float nRoot(float x, float n)
{ return pow(abs(x), rcp(max(n, DELTA))); }

float LimitedHighPass(float x, float Limit, float Curve, float Intensity)
{ return min(pow(abs(x), Curve) * Intensity, Limit); }


//----------------------------------------------------------------------------------------------//
// Hash-based pseudo-random number generators

float Random(float2 seed)
{ return frac(sin(dot(seed, float2(12.9898, 78.233))) * 43758.5453); }

float Random(float seed)
{ return frac(sin(seed * 78.233) * 43758.5453); }

float RandomGauss(float2 seed)
{
    float r1 = Random(seed);
    float r2 = Random(seed + 0.5);
    return sqrt(-2.0 * log(max(r1, DELTA))) * cos(TWO_PI * r2);
}

float4 RandomF4(float4 seed)
{
    return frac(sin(float4(
        dot(seed.xy, float2(12.9898, 78.233)),
        dot(seed.yz, float2(39.3460, 11.135)),
        dot(seed.zw, float2(73.1560, 52.742)),
        dot(seed.wx, float2(27.6190, 93.418))
    )) * 43758.5453);
}


//----------------------------------------------------------------------------------------------//
// UV atlas fetch (2x2 quadrant)

float2 AtlasFetch_4(float2 texcoord, int index)
{
    float2 offset;
    offset.x = (index == 2 || index == 4) ? 0.5 : 0.0;
    offset.y = (index >= 3) ? 0.5 : 0.0;
    return texcoord * 0.5 + offset;
}


//=============================================================================//
//  Section 2: Technique macros and generic VS/PS                              //
//=============================================================================//

#define TECH11(NAME, VS, PS) \
technique11 NAME {pass p0 {SetVertexShader(CompileShader(vs_5_0, VS));\
                           SetPixelShader (CompileShader(ps_5_0, PS));}}

#define TWOPASSTECH11(NAME, VS1, PS1, VS2, PS2) \
technique11 NAME {pass p0 {SetVertexShader(CompileShader(vs_5_0, VS1));\
                           SetPixelShader (CompileShader(ps_5_0, PS1));}\
                  pass p1 {SetVertexShader(CompileShader(vs_5_0, VS2));\
                           SetPixelShader (CompileShader(ps_5_0, PS2));}}

void   VS_Basic(inout float4 pos : SV_POSITION, inout float4 txcoord : TEXCOORD0) { pos.w = 1.0; }
float4 PS_Blank(float4 pos : SV_POSITION, float4 txcoord : TEXCOORD0) : SV_Target { return 0.0; }


//=============================================================================//
//  Section 3: Time-of-day separation system                                   //
//=============================================================================//

// DNI: Day / Night / Interior interpolation
float  DNISep(float  Day, float  Night, float  Interior)
{ return lerp(lerp(Night, Day, ENightDayFactor), Interior, EInteriorFactor); }

float3 DNISep(float3 Day, float3 Night, float3 Interior)
{ return lerp(lerp(Night, Day, ENightDayFactor), Interior, EInteriorFactor); }

#define DNI(x) DNISep(Day_##x, Night_##x, Interior_##x)

// 7-way TOD: Dawn / Sunrise / Day / Sunset / Dusk / Night / Interior
float TOD7Sep(float Dawn, float Sunrise, float Day, float Sunset,
              float Dusk, float Night, float Interior)
{
    float ext = Dawn      * TimeOfDay1.x
              + Sunrise   * TimeOfDay1.y
              + Day       * TimeOfDay1.z
              + Sunset    * TimeOfDay1.w
              + Dusk      * TimeOfDay2.x
              + Night     * TimeOfDay2.y;
    return lerp(ext, Interior, EInteriorFactor);
}


//=============================================================================//
//  Section 4: Depth linearization                                             //
//=============================================================================//

float  FastLinDepth(float  Depth, float Far)
{ return Depth / mad(-Depth, Far, Far + 1.0); }

float2 FastLinDepth(float2 Depth, float Far)
{ return Depth / mad(-Depth, Far, Far + 1.0); }

float4 FastLinDepth(float4 Depth, float Far)
{ return Depth / mad(-Depth, Far, Far + 1.0); }


//=============================================================================//
//  Section 5: Color space conversions                                         //
//=============================================================================//

// Linear <-> sRGB (accurate piecewise)
float3 Lin2sRGB(float3 Color)
{ return Color > 0.0031308 ? 1.055 * pow(abs(Color), 1.0/2.4) - 0.055 : 12.92 * Color; }

float3 sRGB2Lin(float3 Color)
{ return Color > 0.04045 ? pow(abs(Color / 1.055 + 0.055 / 1.055), 2.4) : Color / 12.92; }

// Fast approximations
float3 Lin2sRGB_Fast(float3 Color) { return pow(abs(Color), 1.0/2.2); }
float3 sRGB2Lin_Fast(float3 Color) { return pow(abs(Color), 2.2); }

// RGB <-> HCV (Ian Taylor)
float3 RGB2HCV(float3 RGB)
{
    RGB      = saturate(RGB);
    float4 P = (RGB.g < RGB.b) ? float4(RGB.bg, -1.0, 2.0/3.0) : float4(RGB.gb, 0.0, -1.0/3.0);
    float4 Q = (RGB.r < P.x)  ? float4(P.xyw, RGB.r)           : float4(RGB.r, P.yzx);
    float  C = Q.x - min(Q.w, Q.y);
    float  H = abs((Q.w - Q.y) / (6.0 * C + DELTA) + Q.z);
    return float3(H, C, Q.x);
}

// RGB <-> HSL
float3 RGB2HSL(float3 RGB)
{
    float3 HCV = RGB2HCV(RGB);
    float  L   = HCV.z - HCV.y * 0.5;
    float  S   = HCV.y / ((1.0 + DELTA) - abs(L * 2.0 - 1.0));
    return float3(HCV.x, S, L);
}

float3 HSL2RGB(float3 HSL)
{
    HSL = saturate(HSL);
    float3 RGB = saturate(float3(abs(HSL.x * 6.0 - 3.0) - 1.0,
                           2.0 - abs(HSL.x * 6.0 - 2.0),
                           2.0 - abs(HSL.x * 6.0 - 4.0)));
    float C = (1.0 - abs(2.0 * HSL.z - 1.0)) * HSL.y;
    return (RGB - 0.5) * C + HSL.z;
}

// RGB <-> HSV
float3 RGB2HSV(float3 RGB)
{
    float3 HCV = RGB2HCV(RGB);
    float S = HCV.y / (HCV.z + DELTA);
    return float3(HCV.x, S, HCV.z);
}

float3 HSV2RGB(float3 HSV)
{
    float3 RGB = saturate(float3(abs(HSV.x * 6.0 - 3.0) - 1.0,
                           2.0 - abs(HSV.x * 6.0 - 2.0),
                           2.0 - abs(HSV.x * 6.0 - 4.0)));
    return ((RGB - 1.0) * HSV.y + 1.0) * HSV.z;
}


//=============================================================================//
//  Section 6: Interpolation utilities                                         //
//=============================================================================//

float  LinearStep(float  Low, float  Up, float  x)
{ return saturate((x - Low) / (Up - Low)); }

float2 LinearStep(float2 Low, float2 Up, float2 x)
{ return saturate((x - Low) / (Up - Low)); }

float smootherstep(float Low, float Up, float x)
{
    x = LinearStep(Low, Up, x);
    return x * x * x * (x * (x * 6.0 - 15.0) + 10.0);
}


//=============================================================================//
//  Section 7: Noise and dithering                                             //
//=============================================================================//

// Interleaved Gradient Noise (Jorge Jimenez 2014)
float InterleavedGradientNoise(float2 coord)
{ return frac(52.9829189 * frac(dot(coord, float2(0.06711056, 0.00583715)))); }

// Temporal IGN (frame-varying)
float TemporalIGN(float2 PixelCoord, float FrameIndex)
{
    float2 Coord = PixelCoord + 5.588238 * frac(FrameIndex * 0.5 + 0.5);
    return frac(52.9829189 * frac(dot(Coord, float2(0.06711056, 0.00583715))));
}

// Valve/Iestyn RGB dither (final 8-bit quantization anti-banding)
float3 DitherOutput(float3 color, float2 screenPos)
{
    float3 noise = float3(
        InterleavedGradientNoise(screenPos),
        InterleavedGradientNoise(screenPos + 72.0),
        InterleavedGradientNoise(screenPos + 144.0)
    );
    return color + (noise - 0.5) / 255.0;
}


//=============================================================================//
//  Section 8: Color temperature (Planckian locus)                             //
//=============================================================================//

// Attempt to approximate a blackbody Kelvin temperature as an RGB color.
// Valid for ~1000K to ~40000K range.
// Reference: Tanner Helland, "How to Convert a Temperature in Kelvin to RGB"
float3 KelvinToRGB(float K)
{
    float temp = K * 0.01;
    float3 rgb;

    // Red channel
    if (temp <= 66.0)
        rgb.r = 1.0;
    else
        rgb.r = saturate(1.292936 * pow(abs(temp - 60.0), -0.1332047));

    // Green channel
    if (temp <= 66.0)
        rgb.g = saturate(0.390082 * log(max(temp, 1.0)) - 0.631889);
    else
        rgb.g = saturate(1.129891 * pow(abs(temp - 60.0), -0.0755148));

    // Blue channel
    if (temp >= 66.0)
        rgb.b = 1.0;
    else if (temp <= 19.0)
        rgb.b = 0.0;
    else
        rgb.b = saturate(0.543207 * log(max(temp - 10.0, 1.0)) - 1.19625);

    return rgb;
}

// Color temperature offset: warm (+K bias) or cool (-K bias)
// Multiplies input color by the blackbody temperature ratio
float3 ApplyColorTemp(float3 color, float tempOffset)
{
    float3 warm = KelvinToRGB(6500.0 + tempOffset);
    float3 neutral = KelvinToRGB(6500.0);
    return color * (warm / deltalim(neutral));
}


//=============================================================================//
//  Section 9: ACES color transforms                                           //
//=============================================================================//

// sRGB (linear) -> ACEScg
static const float3x3 sRGB_to_ACEScg = float3x3(
    0.613097, 0.339523, 0.047379,
    0.070194, 0.916354, 0.013452,
    0.020616, 0.109570, 0.869815
);

// ACEScg -> sRGB (linear)
static const float3x3 ACEScg_to_sRGB = float3x3(
     1.704859, -0.621715, -0.083299,
    -0.130078,  1.140734, -0.010560,
    -0.023964, -0.128975,  1.153013
);

float3 ToACEScg(float3 color) { return mul(sRGB_to_ACEScg, color); }
float3 FromACEScg(float3 color) { return mul(ACEScg_to_sRGB, color); }


//=============================================================================//
//  Section 10: Bloom/threshold utilities                                      //
//=============================================================================//

// Karis anti-firefly soft-knee threshold
// Smoothly ramps bloom contribution from (threshold - knee) to (threshold + knee)
float3 SoftThreshold(float3 color, float threshold, float knee)
{
    float luma = Luminance(color);
    float soft = luma - threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + DELTA);
    float contribution = max(soft, luma - threshold) / max(luma, DELTA);
    return color * max(contribution, 0.0);
}


//=============================================================================//
//  Section 11: Bicubic sampling (Catmull-Rom, 4 bilinear taps)                //
//=============================================================================//

float4 BicubicFilter(Texture2D InputTex, SamplerState smp, float2 texcoord, float2 texSize)
{
    float2 invSize = 1.0 / texSize;
    texcoord = texcoord * texSize - 0.5;

    float2 fxy = frac(texcoord);
    texcoord -= fxy;

    float2 fxy2 = fxy * fxy;
    float2 fxy3 = fxy2 * fxy;

    float2 w0 = -0.5 * fxy3 + fxy2 - 0.5 * fxy;
    float2 w1 =  1.5 * fxy3 - 2.5 * fxy2 + 1.0;
    float2 w2 = -1.5 * fxy3 + 2.0 * fxy2 + 0.5 * fxy;
    float2 w3 =  0.5 * fxy3 - 0.5 * fxy2;

    float2 s0 = w0 + w1;
    float2 s1 = w2 + w3;
    float2 f0 = w1 / (w0 + w1);
    float2 f1 = w3 / (w2 + w3);

    float2 t0 = (texcoord - 1.0 + f0) * invSize;
    float2 t1 = (texcoord + 1.0 + f1) * invSize;

    return (InputTex.SampleLevel(smp, float2(t0.x, t0.y), 0) * s0.x
          + InputTex.SampleLevel(smp, float2(t1.x, t0.y), 0) * s1.x) * s0.y
         + (InputTex.SampleLevel(smp, float2(t0.x, t1.y), 0) * s0.x
          + InputTex.SampleLevel(smp, float2(t1.x, t1.y), 0) * s1.x) * s1.y;
}


#endif // EOTE_COMMON_FXH
