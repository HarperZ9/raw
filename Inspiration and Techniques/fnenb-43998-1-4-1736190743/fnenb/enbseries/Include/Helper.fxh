

// Globals for all ENB.fx files
float4	Timer;           // x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4	ScreenSize;      // x = Width, y = 1/Width, z = Width/Height, w = Height/Width
float	AdaptiveQuality; // changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4	Weather;         // x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours.
float4	TimeOfDay1;      // x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay2;      // x = dusk, y = night. Interpolators range from 0..1
float	ENightDayFactor; // changes in range 0..1, 0 means that night time, 1 - day time
float	EInteriorFactor; // changes 0 or 1. 0 means that exterior, 1 - interior
float4  SunDirection;    // Prepass exclusive. Refrence here: https://cdn.discordapp.com/attachments/335788870849265675/532859588203249666/unknown.png
float4  Params01[7];     // skyrimse parameters
float4  ENBParams01;     // x - bloom amount; y - lens amount
static const float2 PixelSize  = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z); // As in Reshade
static const float2 Resolution = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w); // Display Resolution
float4	tempF1, tempF2, tempF3; //0,1,2,3,4,5,6,7,8,9

float4	tempInfo1;
float4	tempInfo2; // xy = cursor position of previous left click, zw = cursor position of previous right click


// All kinds of samplers
SamplerState		PointSampler
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState		LinearSampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState		WrapSampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Wrap;
	AddressV = Wrap;
};
SamplerState		MirrorSampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Mirror;
	AddressV = Mirror;
};
SamplerState		BorderSampler
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Border;
	AddressV = Border;
};

struct VS_INPUT
{
    float3 pos     : POSITION;
    float2 txcoord : TEXCOORD0;
};
struct VS_OUTPUT
{
    float4 pos     : SV_POSITION;
    float2 txcoord : TEXCOORD0;
};

VS_OUTPUT VS_Draw(VS_INPUT IN)
{
    VS_OUTPUT OUT;
    OUT.pos = float4(IN.pos.xyz, 1.0);
    OUT.txcoord.xy = IN.txcoord.xy;
    return OUT;
}

// Depth Linearization (Thanks to Marty and Trey)
float GetLinearizedDepth(float2 coord)
{
    float depth = TextureDepth.Sample(PointSampler, coord);
    depth *= rcp(mad(depth,-2999.0,3000.0));
    return depth;
}

// Get Luma function by TreyM
// Luma Coefficients
#define Rec709      0
#define Rec709_5    1
#define Rec601      2
#define Rec2020     3
#define Lum333      4

// Calculate perceived luminance color by using the ITU-R BT standards
float GetLuma(in float3 color, int btspec)
{
    static const float3 LumaCoeff[5] =
    {
        // 0: HD TV - Rec.709
        float3(0.2126, 0.7152f, 0.0722),
        // 1: HD TV - Rec.709-5
        float3(0.212395, 0.701049, 0.086556),
        // 2: CRT TV - Rec.601
        float3(0.299, 0.587, 0.114),
        // 3: HDR Spec - Rec.2020
        float3(0.2627, 0.6780, 0.0593),
        // 4: Incorrect Equal Weighting
        float3(0.3333, 0.3333, 0.3333)
    };

    //return sqrt(dot(color.rgb * color.rgb, LumaCoeff[btspec])); // Requires sRGB input
    return dot(color.rgb, LumaCoeff[btspec]);
}

float min3(float3 x)
{
    return min(x.x, min(x.y, x.z));
}

float max3(float3 x)
{
    return max(x.x, max(x.y, x.z));
}
