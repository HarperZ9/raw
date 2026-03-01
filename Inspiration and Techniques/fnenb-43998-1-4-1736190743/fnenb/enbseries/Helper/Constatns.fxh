//----------------------------------------------------------------------------------------------//
//												//
//			Common helper library by LonelyKitsune aka Skratzer			//
//				 for ENB (DirectX 11 Shader Model 5)				//
//												//
//		Copyright (c) 2019-2020 LonelyKitsune / T.Thanner - CC BY-NC-ND 4.0		//
//												//
//----------------------------------------------------------------------------------------------//

#define LUM_709	float3(0.2125, 0.7154, 0.0721)
#define LUM_601	float3(0.2989, 0.5870, 0.1140)
#define K_LUM	float3(0.25,   0.60,   0.15)
#define ALT_LUM	float3(0.25,   0.50,   0.25)
#define N_LUM	0.333333
#define DELTA	1e-6//1e-8
#define PI	3.1415926535897932384626433832795

static const float MC_WeightFactor = 1.0 / (0.399 * sqrt(2.0 * PI));
static const float timeweight() {
    return TimeOfDay1.x +
           TimeOfDay1.y +
           TimeOfDay1.z +
           TimeOfDay1.w +
           TimeOfDay2.x +
           TimeOfDay2.y;}
        
#define TODIE(a) lerp( TOD(a), a##_Interior, EInteriorFactor )

#define TOD(a) \
  ((TimeOfDay1.x * a##_Dawn    + \
    TimeOfDay1.y * a##_Sunrise + \
    TimeOfDay1.z * a##_Day     + \
    TimeOfDay1.w * a##_Sunset  + \
    TimeOfDay2.x * a##_Dusk    + \
    TimeOfDay2.y * a##_Night) / timeweight())
        
static const float4 node[9]= {
    float4(TODIE(HUEcolor0), 0.0),//red
    float4(TODIE(HUEcolor1), 30.0),
    float4(TODIE(HUEcolor2), 60.0),
    float4(TODIE(HUEcolor3), 120),
    float4(TODIE(HUEcolor4), 180),
    float4(TODIE(HUEcolor5), 240),
    float4(TODIE(HUEcolor6), 270),
    float4(TODIE(HUEcolor7), 300),
    float4(TODIE(HUEcolor0), 360)//red
};

#define TODA(a,b) \
  ((TimeOfDay1.x * a   + \
    TimeOfDay1.y * a + \
    TimeOfDay1.z * a    + \
    TimeOfDay1.w * a + \
    TimeOfDay2.x * b  + \
    TimeOfDay2.y * b) / timeweight())

#define TODAT(a,b,c,d,e,f) \
  ((TimeOfDay1.x * a   + \
    TimeOfDay1.y * b + \
    TimeOfDay1.z * c    + \
    TimeOfDay1.w * d + \
    TimeOfDay2.x * e  + \
    TimeOfDay2.y * f) / timeweight())

// a = dawn ,b = sunrise , c day, d sunset, e dusk f night
float3 RGB_to_HSL(float3 color) {
    float3 HSL = 0.0f;
    float  M   = max(color.r, max(color.g, color.b));
    float  C   = M - min(color.r, min(color.g, color.b));
    HSL.z = M - 0.5 * C;

    if (C != 0.0f) {
        float3 Delta = (color.brg - color.rgb) / C + float3(2.0f, 4.0f, 6.0f);
        Delta *= step(M, color.gbr); //if max = rgb
        HSL.x = frac(max(Delta.r, max(Delta.g, Delta.b)) / 6.0);
        HSL.y = (HSL.z == 1)? 0.0: C/ (1 - abs( 2 * HSL.z - 1));
        }

        return HSL;
}

float3 Hue_to_RGB( float h) {
    return saturate(float3( abs(h * 6.0f - 3.0f) - 1.0f,
                            2.0f - abs(h * 6.0f - 2.0f),
                            2.0f - abs(h * 6.0f - 4.0f)));
}

float3 HSL_to_RGB( float3 HSL ) {
    return (Hue_to_RGB(HSL.x) - 0.5) * (1.0 - abs(2.0 * HSL.z - 1)) * HSL.y + HSL.z;
}

float LoC( float L0, float L1, float angle) {
    return sqrt(L0*L0+L1*L1-2.0*L0*L1*cos(angle));
}

static const float HSL_Threshold_Base  = 0.05;
static const float HSL_Threshold_Curve = 1.0;




