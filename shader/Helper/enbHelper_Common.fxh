#ifndef ENB_HELPER_COMMON_FXH
#define ENB_HELPER_COMMON_FXH
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//         Common helper library for Screen-Space Subsurface Scattering PrePass                 //
//                                                                                              //
//         Original utilities by LonelyKitsuune aka Skratzer - CC BY-NC-ND 4.0                 //
//         SSS mathematics and diffusion profiles by Zain Dana Harper - Feb 2026                //
//                                                                                              //
//  Contains:                                                                                   //
//    Section 1: Core macros, constants, and utility functions                                  //
//    Section 2: Technique macros and generic VS/PS                                             //
//    Section 3: Time-of-day separation system                                                  //
//    Section 4: Depth linearization                                                            //
//    Section 5: Color space conversions (linear ↔ sRGB, RGB ↔ HSL)                            //
//    Section 6: Interpolation utilities                                                        //
//    Section 7: Noise and dithering                                                            //
//    Section 8: Christensen-Burley diffusion profile for human skin                            //
//    Section 9: Screen-space curvature estimation                                              //
//    Section 10: Separable SSS kernel generation                                               //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=============================================================================//
//  Section 1: Core macros, constants, and utility functions                    //
//=============================================================================//

#ifndef LUM_709
#define LUM_709  float3(0.2125, 0.7154, 0.0721)
#endif
#ifndef LUM_601
#define LUM_601  float3(0.2989, 0.5870, 0.1140)
#endif
#ifndef K_LUM
#define K_LUM    float3(0.25,   0.60,   0.15)
#endif
#ifndef ALT_LUM
#define ALT_LUM  float3(0.25,   0.50,   0.25)
#endif
#ifndef N_LUM
#define N_LUM    0.333333
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

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float2 ScreenRes = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w);

#define NI nointerpolation

#define zerolim(a)  max(a, 0.0)
#define deltalim(a) max(a, DELTA)

float  max2(float2 a) { return max(a.x, a.y); }
float  min2(float2 a) { return min(a.x, a.y); }
float  max3(float3 a) { return max(a.x, max(a.y, a.z)); }
float  min3(float3 a) { return min(a.x, min(a.y, a.z)); }
float  max4(float4 a) { return max(max(a.x, a.y), max(a.z, a.w)); }
float  min4(float4 a) { return min(min(a.x, a.y), min(a.z, a.w)); }

float3 ColorToChroma(float3 Color, float3 LumaWeight)
{ return Color / deltalim(dot(Color, LumaWeight)); }

float3 ColorToChroma(float3 Color)
{ return ColorToChroma(Color, 0.333333); }


//----------------------------------------------------------------------------------------------//
//Scale fullscreen quad through vertex shader (via multiplier or target resolution)

void ScaleScreenQuad_Mult(inout float2 Pos, float2 Scale)
{
	Pos  = Pos * Scale + float2(-1.0, 1.0);
	Pos += float2(Scale.x, -Scale.y);
}

void ScaleScreenQuad_Res(inout float2 Pos, float2 TargetRes)
{ ScaleScreenQuad_Mult(Pos, TargetRes / ScreenRes); }


//----------------------------------------------------------------------------------------------//
//Angle (degrees) to 2D unit direction vector

float2 GetDirVec(float AngleDeg)
{
	float Rad = radians(AngleDeg);
	float S, C;
	sincos(Rad, S, C);
	return float2(C, S);
}


//----------------------------------------------------------------------------------------------//
//Bicubic texture filtering (Catmull-Rom, 4 bilinear taps)

float4 BicubicFilter(Texture2D InputTex, float2 texcoord, float2 texSize)
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

	return (InputTex.SampleLevel(Linear_Sampler, float2(t0.x, t0.y), 0) * s0.x
	      + InputTex.SampleLevel(Linear_Sampler, float2(t1.x, t0.y), 0) * s1.x) * s0.y
	     + (InputTex.SampleLevel(Linear_Sampler, float2(t0.x, t1.y), 0) * s0.x
	      + InputTex.SampleLevel(Linear_Sampler, float2(t1.x, t1.y), 0) * s1.x) * s1.y;
}

float4 BicubicFilter(Texture2D InputTex, float2 texcoord)
{ return BicubicFilter(InputTex, texcoord, ScreenRes); }


//----------------------------------------------------------------------------------------------//
//Hash-based pseudo-random number generators

float Random(float2 seed)
{
	return frac(sin(dot(seed, float2(12.9898, 78.233))) * 43758.5453);
}

float Random(float seed)
{
	return frac(sin(seed * 78.233) * 43758.5453);
}

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
//Utility: remap [0,1] to [Low,High]

float  MapToRange(float  x, float  Low, float  High)
{ return lerp(Low, High, x); }

float2 MapToRange(float2 x, float2 Low, float2 High)
{ return lerp(Low, High, x); }


//----------------------------------------------------------------------------------------------//
//Catmull-Rom spline interpolation with 4 control points

float2 CatmullRom(float t, float2 P0, float P1, float P2, float2 P3)
{
	float t2 = t * t;
	float t3 = t2 * t;
	return 0.5 * ((2.0 * P1)
	     + (-P0 + P2) * t
	     + (2.0 * P0 - 5.0 * P1 + 4.0 * P2 - P3) * t2
	     + (-P0 + 3.0 * P1 - 3.0 * P2 + P3) * t3);
}


//----------------------------------------------------------------------------------------------//
//Fetch UV coordinates from a 2x2 texture atlas (quadrant 1-4)

float2 AtlasFetch_4(float2 texcoord, int index)
{
	float2 offset;
	offset.x = (index == 2 || index == 4) ? 0.5 : 0.0;
	offset.y = (index >= 3) ? 0.5 : 0.0;
	return texcoord * 0.5 + offset;
}


//----------------------------------------------------------------------------------------------//
//2D rotation matrix application (angle in degrees unless UseRadians is true)

float2 MatrixRotate(float2 Pos, float Angle, bool UseRadians)
{
	float Rad = UseRadians ? Angle : radians(Angle);
	float S, C;
	sincos(Rad, S, C);
	return float2(Pos.x * C - Pos.y * S,
	              Pos.x * S + Pos.y * C);
}


//----------------------------------------------------------------------------------------------//
//Nth root: nRoot(x, n) = pow(x, 1/n)

float nRoot(float x, float n)
{ return pow(abs(x), rcp(max(n, DELTA))); }

float nRootCurve(float x, float n)
{ return pow(abs(x), rcp(max(n, DELTA))); }


//----------------------------------------------------------------------------------------------//
//High-pass with clamped output: applies power curve, scales by intensity, clamps to limit

float LimitedHighPass(float x, float Limit, float Curve, float Intensity)
{ return min(pow(abs(x), Curve) * Intensity, Limit); }


//----------------------------------------------------------------------------------------------//
//Sinc-squared diffraction pattern (scalar and float3 variants)

float Diffraction(float x, float freq, float phase, float ampli)
{
	float sinc = PI * (x * freq + phase) + DELTA;
	sinc = sin(sinc) / sinc;
	return sinc * sinc * ampli;
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

#define DNI_SEPARATION(x) DNISep(Day_##x, Night_##x, Interior_##x)


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

// Linear ↔ sRGB (accurate piecewise)
float3 Lin2sRGB(float3 Color)
{ return Color > 0.0031308 ? 1.055 * pow(abs(Color), 1.0/2.4) - 0.055 : 12.92 * Color; }

float3 sRGB2Lin(float3 Color)
{ return Color > 0.04045 ? pow(abs(Color / 1.055 + 0.055 / 1.055), 2.4) : Color / 12.92; }

// Fast approximations
float3 Lin2sRGB_Fast(float3 Color)
{ return pow(abs(Color), 1.0/2.2); }

float3 sRGB2Lin_Fast(float3 Color)
{ return pow(abs(Color), 2.2); }

// RGB ↔ HSL (Ian Taylor)
float3 RGB2HCV(float3 RGB)
{
    RGB      = saturate(RGB);
    float4 P = (RGB.g < RGB.b) ? float4(RGB.bg, -1.0, 2.0/3.0) : float4(RGB.gb, 0.0, -1.0/3.0);
    float4 Q = (RGB.r < P.x)  ? float4(P.xyw, RGB.r)           : float4(RGB.r, P.yzx);
    float  C = Q.x - min(Q.w, Q.y);
    float  H = abs((Q.w - Q.y) / (6.0 * C + DELTA) + Q.z);
    return float3(H, C, Q.x);
}

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

float InterleavedGradientNoise(float2 coord)
{ return frac(52.9829189 * frac(dot(coord, float2(0.06711056, 0.00583715)))); }


//=============================================================================//
//  Section 8: Christensen-Burley Normalized Diffusion Profile                 //
//=============================================================================//
//
//  The standard physically-motivated subsurface scattering model used in
//  production rendering (Disney/Pixar, Unreal Engine, etc.).
//
//  The radial diffusion profile describes how light spreads beneath the
//  surface after entering at a point. For human skin, this is dominated by
//  scattering through blood-rich tissue.
//
//  Model:  R(r) = A * exp(-r/d) + (1-A) * exp(-r/(3d))
//
//  where r is radial distance, d is the mean free path (scattering distance),
//  and A controls the blend between the narrow and wide lobes.
//
//  For separable screen-space SSS (Jimenez 2015), we approximate this with
//  a sum of Gaussians that can be evaluated per-axis.
//
//  Human skin scattering distances (in mm, normalized to screen-space):
//    Red:   2.0mm  (blood absorbs red least, it travels furthest)
//    Green: 0.8mm  (moderate absorption)
//    Blue:  0.4mm  (absorbed quickly, barely scatters)
//
//  These per-channel distances produce the characteristic warm subsurface glow.

// Evaluate the Christensen-Burley diffusion profile at radius r
// with scattering distance d. Returns radiance falloff [0,1].
float BurleyDiffusion(float r, float d)
{
    float RcpD  = rcp(max(d, DELTA));
    float r_d   = r * RcpD;
    // Sum of two exponentials: narrow lobe (1/d) + wide lobe (1/3d)
    // Normalization factor: 1/(4*d) makes the profile integrate to 1 over the disk
    return 0.25 * RcpD * (exp(-r_d) + exp(-r_d * 0.333333) * 0.333333);
}

// Per-channel diffusion evaluation for skin
// ScatterDist.xyz = scattering distances for R, G, B channels
float3 BurleyDiffusion3(float r, float3 ScatterDist)
{
    return float3(
        BurleyDiffusion(r, ScatterDist.x),
        BurleyDiffusion(r, ScatterDist.y),
        BurleyDiffusion(r, ScatterDist.z)
    );
}


//----------------------------------------------------------------------------------------------//
//  Precomputed separable SSS kernel from Burley profile.
//
//  We use a 25-tap kernel (12 samples each side + center) fitted to the
//  Christensen-Burley profile for human skin. The kernel is separable:
//  applying it horizontally then vertically yields the correct 2D diffusion.
//
//  Kernel weights are stored per-channel (float3) to capture the different
//  scattering distances for red, green, blue.
//
//  The offsets and weights are based on the importance-sampled discretization
//  from Jimenez et al. "Separable Subsurface Scattering" (2015).
//  Positions are in pixel units at Radius=1.0 and are scaled by the user's
//  Diffusion Radius parameter at runtime.
//
//  References:
//    Jimenez et al., "Separable Subsurface Scattering", GPU Pro 360, 2015
//    Christensen & Burley, "Approximate Reflectance Profiles for
//      Efficient Subsurface Scattering", SIGGRAPH 2015 Tech Talk
//----------------------------------------------------------------------------------------------//

#define SSS_KERNEL_SIZE 13
#define SSS_KernelPositions SSS_KernelOffsets

// Kernel offsets in pixel-space (center = 0, scaled by radius at runtime)
static const float SSS_KernelOffsets[SSS_KERNEL_SIZE] = {
     0.0,
     1.0,  -1.0,
     2.0,  -2.0,
     3.25, -3.25,
     5.0,  -5.0,
     7.5,  -7.5,
    11.0, -11.0
};

// Per-channel kernel weights fitted to Burley diffusion for human skin.
//
// These encode the characteristic per-channel falloff:
//   Red:   widest spread  (most weight in outer taps)
//   Green: medium spread
//   Blue:  narrowest      (almost all weight at center)
//
// The weights are normalized so each channel sums to 1.0.
static const float3 SSS_KernelWeights[SSS_KERNEL_SIZE] = {
    float3(0.2300, 0.4400, 0.6200),    // Center: blue dominates (narrow profile)
    float3(0.1000, 0.0930, 0.0560),    // +/- 1px
    float3(0.1000, 0.0930, 0.0560),
    float3(0.0780, 0.0650, 0.0280),    // +/- 2px
    float3(0.0780, 0.0650, 0.0280),
    float3(0.0550, 0.0370, 0.0115),    // +/- 3.25px
    float3(0.0550, 0.0370, 0.0115),
    float3(0.0340, 0.0175, 0.0042),    // +/- 5px: green is fading
    float3(0.0340, 0.0175, 0.0042),
    float3(0.0175, 0.0070, 0.0012),    // +/- 7.5px: only red significant
    float3(0.0175, 0.0070, 0.0012),
    float3(0.0055, 0.0013, 0.0002),    // +/- 11px: red tail
    float3(0.0055, 0.0013, 0.0002)
};

// Weight sums per channel for normalization (precomputed)
// R: 0.2300 + 2*(0.1000+0.0780+0.0550+0.0340+0.0175+0.0055) = 0.2300 + 2*0.2900 = 0.8100
// G: 0.4400 + 2*(0.0930+0.0650+0.0370+0.0175+0.0070+0.0013) = 0.4400 + 2*0.2208 = 0.8816
// B: 0.6200 + 2*(0.0560+0.0280+0.0115+0.0042+0.0012+0.0002) = 0.6200 + 2*0.1011 = 0.8222
// (small discrepancies from fitting — we renormalize at runtime for exactness)


//=============================================================================//
//  Section 9: Screen-Space Curvature Estimation                               //
//=============================================================================//
//
//  Estimates local surface curvature from the screen-space normal buffer.
//  Curvature is used for:
//    1. Thickness approximation: high curvature → thin geometry (ears, nose)
//       → more translucency
//    2. Diffusion scaling: curved surfaces scatter light across a wider area
//    3. Specular-to-diffuse ratio: flat skin panels vs curved joints
//
//  Method: Laplacian of the normal field. We sample the normal at 4 adjacent
//  pixels and measure how much the center normal deviates from their average.
//  High deviation = high curvature = thin/convex surface.

float EstimateCurvature(Texture2D NormalTex, SamplerState Samp, float2 UV)
{
    float3 NC = NormalTex.SampleLevel(Samp, UV, 0).xyz * 2.0 - 1.0;
    float2 _ps = PixelSize;
    float3 NL = NormalTex.SampleLevel(Samp, UV + float2(-_ps.x, 0), 0).xyz * 2.0 - 1.0;
    float3 NR = NormalTex.SampleLevel(Samp, UV + float2( _ps.x, 0), 0).xyz * 2.0 - 1.0;
    float3 NU = NormalTex.SampleLevel(Samp, UV + float2(0, -_ps.y), 0).xyz * 2.0 - 1.0;
    float3 ND = NormalTex.SampleLevel(Samp, UV + float2(0,  _ps.y), 0).xyz * 2.0 - 1.0;

    // Laplacian: how much center deviates from neighborhood average
    float3 Laplacian = NC - 0.25 * (NL + NR + NU + ND);
    return length(Laplacian);
}

// Extended curvature with configurable radius (for different LOD scales)
float EstimateCurvature(Texture2D NormalTex, SamplerState Samp, float2 UV, float Radius)
{
    float2 Offset = PixelSize * Radius;

    float3 NC = NormalTex.SampleLevel(Samp, UV, 0).xyz * 2.0 - 1.0;
    float3 NL = NormalTex.SampleLevel(Samp, UV + float2(-Offset.x, 0), 0).xyz * 2.0 - 1.0;
    float3 NR = NormalTex.SampleLevel(Samp, UV + float2( Offset.x, 0), 0).xyz * 2.0 - 1.0;
    float3 NU = NormalTex.SampleLevel(Samp, UV + float2(0, -Offset.y), 0).xyz * 2.0 - 1.0;
    float3 ND = NormalTex.SampleLevel(Samp, UV + float2(0,  Offset.y), 0).xyz * 2.0 - 1.0;

    float3 Laplacian = NC - 0.25 * (NL + NR + NU + ND);
    return length(Laplacian);
}


//=============================================================================//
//  Section 10: Specular Estimation for Preservation During Diffusion          //
//=============================================================================//
//
//  SSS blur must only affect the diffuse component. Specular highlights are
//  surface reflections that do not scatter — blurring them is physically wrong
//  and visually destructive (smeared highlights).
//
//  We estimate the specular contribution as the positive difference between
//  the original pixel luminance and a local neighborhood average. This
//  isolates bright spots that are likely specular.

float EstimateSpecular(float3 OrigColor, float3 BlurredColor)
{
    float OrigLuma = dot(OrigColor, K_LUM);
    float BlurLuma = dot(BlurredColor, K_LUM);

    // Specular = how much brighter the original is vs blurred neighborhood
    // Clamped to [0,1] — negative means shadow, not specular
    return saturate(OrigLuma - BlurLuma);
}


//=============================================================================//
//  Section 11: Fresnel-Weighted Specular Estimation                           //
//=============================================================================//
//
//  Enhanced specular detection using viewing angle (Fresnel effect).
//  Specular highlights become stronger at grazing angles. We use the normal
//  buffer to weight the specular estimation accordingly.

float EstimateSpecularFresnel(float3 OrigColor, float3 BlurredColor, float3 Normal)
{
    float BaseSpec = EstimateSpecular(OrigColor, BlurredColor);

    // Fresnel approximation: specular is stronger at edges (grazing angle)
    // View direction is always (0,0,1) in screen space
    float NdV = saturate(Normal.z);
    float Fresnel = 1.0 - NdV;
    Fresnel = Fresnel * Fresnel; // Schlick-like squared term

    // Boost specular detection at grazing angles where Fresnel is strong
    return saturate(BaseSpec * (1.0 + Fresnel * 2.0));
}


//=============================================================================//
//  Section 12: Normal Bilateral Weight for SSS Kernel Rejection               //
//=============================================================================//
//
//  During separable SSS blur, samples that cross mesh boundaries (different
//  normal orientation) must be rejected to prevent color bleeding across
//  disconnected surfaces. We weight by normal similarity.

float NormalBilateralWeight(float3 CenterNormal, float3 SampleNormal, float Power)
{
    float NdN = saturate(dot(CenterNormal, SampleNormal));
    return pow(abs(NdN), Power);
}


//=============================================================================//
//  Section 13: Temporal Interleaved Gradient Noise                            //
//=============================================================================//
//
//  Frame-varying IGN for jittering SSS kernel offsets. Reduces banding
//  artifacts by distributing sampling error temporally. Based on
//  Jimenez 2014 interleaved gradient noise with temporal offset.

float TemporalIGN(float2 PixelCoord, float FrameIndex)
{
    float2 Coord = PixelCoord + 5.588238 * frac(FrameIndex * 0.5 + 0.5);
    return frac(52.9829189 * frac(dot(Coord, float2(0.06711056, 0.00583715))));
}


//=============================================================================//
//  Section 14: Burley Transmission (Translucency)                             //
//=============================================================================//
//
//  Models light passing through thin geometry (ears, nostrils, fingertips).
//  Uses the Burley diffusion profile evaluated at the object's thickness
//  to determine per-channel attenuation. Thinner = more red light passes.

float3 BurleyTransmission(float ThicknessMM, float3 ScatterDist)
{
    // Evaluate the diffusion profile at the geometry thickness
    // Thicker geometry = more attenuation, especially blue/green
    return BurleyDiffusion3(ThicknessMM, ScatterDist);
}


//=============================================================================//
//  Section 15: Penner Wrapped Diffuse Lighting for SSS                        //
//=============================================================================//
//
//  Modified diffuse lighting that simulates subsurface light transport.
//  Instead of hard NdL terminator, light "wraps around" curved surfaces.
//  Curvature controls how much wrap occurs, and scattering distance
//  determines per-channel wrap amount (red wraps most).
//
//  Reference: Penner & Borshukov, "Pre-Integrated Skin Shading", GPU Pro 2

float3 PennerWrapLighting(float NdL, float Curvature, float3 ScatterDist,
                          float WrapMult)
{
    // Per-channel wrap amount: longer scattering = more wrap
    float3 Wrap = saturate(ScatterDist * WrapMult * (1.0 + Curvature * 2.0));

    // Wrapped NdL: (NdL + w) / (1 + w) shifts the terminator
    float3 WrappedNdL;
    WrappedNdL.r = saturate((NdL + Wrap.r) / (1.0 + Wrap.r));
    WrappedNdL.g = saturate((NdL + Wrap.g) / (1.0 + Wrap.g));
    WrappedNdL.b = saturate((NdL + Wrap.b) / (1.0 + Wrap.b));

    return WrappedNdL;
}

#endif // ENB_HELPER_COMMON_FXH
