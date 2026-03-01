//==========================//
// Globals for every ENB.fx //
//==========================//

float4	Timer;           //x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4	ScreenSize;      //x = Width, y = 1/Width, z = Width/Height, w = Height/Width
float	AdaptiveQuality; //changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4	Weather;         //x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours.
float4	TimeOfDay1;      //x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay2;      //x = dusk, y = night. Interpolators range from 0..1
float	ENightDayFactor; //changes in range 0..1, 0 means that night time, 1 - day time
float	EInteriorFactor; //changes 0 or 1. 0 means that exterior, 1 - interior
#define PixelSize 		 float2(ScreenSize.y, ScreenSize.y * ScreenSize.z) // As in Reshade
#define Resolution       float2(ScreenSize.x, ScreenSize.x * ScreenSize.w) // Display Resolution
float4	tempF1, tempF2, tempF3; //0,1,2,3,4,5,6,7,8,9

float4	tempInfo1;
float4	tempInfo2; // xy = cursor position of previous left click, zw = cursor position of previous right click

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