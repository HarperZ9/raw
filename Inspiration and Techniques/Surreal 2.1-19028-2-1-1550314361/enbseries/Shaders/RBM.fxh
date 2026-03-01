// Reflective Bumpmapping "RBM" 3.0 beta by Marty McFly. 
// Copyright � 2008-2016 Marty McFly


#define iRBM_SampleCount=32.000000
#define fRBM_LowerThreshold=0.000000
#define fRBM_BlurWidthPixels=93.000008
#define fRBM_ColorMask_Orange=1.000000
#define fRBM_ReliefHeight=0.160000
#define fRBM_FresnelMult=0.500000
#define fRBM_FresnelReflectance=0.160000
#define fRBM_UpperThreshold=0.000000
#define fRBM_ColorMask_Red=1.000000
#define fRBM_ColorMask_Yellow=1.000000
#define fRBM_ColorMask_Green=1.000000
#define fRBM_ColorMask_Cyan=1.000000
#define fRBM_ColorMask_Blue=1.000000
#define fRBM_ColorMask_Magenta=1.000000

float GetLinearDepth(float2 coords)
{
	return GetLinearizedDepth(coords);
}

float3 GetPosition(float2 coords)
{
	float EyeDepth = GetLinearDepth(coords.xy) * 1000.0;
	return float3((coords.xy * 2.0 - 1.0)*EyeDepth,EyeDepth);
}

float3 GetNormalFromDepth(float2 coords) 
{
	float3 centerPos = GetPosition(coords.xy);
	float2 offs = PixelSize.xy*1.0;
	float3 ddx1 = GetPosition(coords.xy + float2(offs.x, 0)) - centerPos;
	float3 ddx2 = centerPos - GetPosition(coords.xy + float2(-offs.x, 0));

	float3 ddy1 = GetPosition(coords.xy + float2(0, offs.y)) - centerPos;
	float3 ddy2 = centerPos - GetPosition(coords.xy + float2(0, -offs.y));

	ddx1 = lerp(ddx1, ddx2, abs(ddx1.z) > abs(ddx2.z));
	ddy1 = lerp(ddy1, ddy2, abs(ddy1.z) > abs(ddy2.z));

	float3 normal = cross(ddy1, ddx1);
	
	return normalize(normal);
}

float3 GetNormalFromColor(float2 coords, float2 offset, float scale, float sharpness)
{
	const float3 lumCoeff = float3(0.299,0.587,0.114);

    	float hpx = dot(TextureColor.Sample(LinearSampler, float4(coords + float2(offset.x,0.0),0,0)).xyz,lumCoeff) * scale;
    	float hmx = dot(TextureColor.Sample(LinearSampler, float4(coords - float2(offset.x,0.0),0,0)).xyz,lumCoeff) * scale;
    	float hpy = dot(TextureColor.Sample(LinearSampler, float4(coords + float2(0.0,offset.y),0,0)).xyz,lumCoeff) * scale;
    	float hmy = dot(TextureColor.Sample(LinearSampler, float4(coords - float2(0.0,offset.y),0,0)).xyz,lumCoeff) * scale;

    	float dpx = GetLinearizedDepth(coords + float2(offset.x,0.0));
    	float dmx = GetLinearizedDepth(coords - float2(offset.x,0.0));
    	float dpy = GetLinearizedDepth(coords + float2(0.0,offset.y));
    	float dmy = GetLinearizedDepth(coords - float2(0.0,offset.y));

	float2 xymult = float2(abs(dmx - dpx), abs(dmy - dpy)) * sharpness; 
	xymult = max(0.0, 1.0 - xymult);
    	
    	float ddx = (hmx - hpx) / (2.0 * offset.x) * xymult.x;
    	float ddy = (hmy - hpy) / (2.0 * offset.y) * xymult.y;
    
    	return normalize(float3(ddx, ddy, 1.0));
}

float3 GetBlendedNormals(float3 n1, float3 n2)
{
	n1 += float3( 0, 0, 1); 
	n2 *= float3(-1, -1, 1); 
	return n1*dot(n1, n2)/n1.z - n2;
}

float3 RGB2HSV(float3 RGB)
{
    	float4 K = float4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    	float4 p = RGB.g < RGB.b ? float4(RGB.bg, K.wz) : float4(RGB.gb, K.xy);
    	float4 q = RGB.r < p.x ? float4(p.xyw, RGB.r) : float4(RGB.r, p.yzx);

    	float d = q.x - min(q.w, q.y);
    	float e = 1.0e-10;
    	return float3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}

float3 HSV2RGB(float3 HSV)
{
    	float4 K = float4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    	float3 p = abs(frac(HSV.xxx + K.xyz) * 6.0 - K.www);
    	return HSV.z * lerp(K.xxx, saturate(p - K.xxx), HSV.y); //HDR capable
}

// unexpected = ? makes no fuckin sense at all
float GetHueMask(in float H)	
{
	float SMod = 0.0;
	SMod += fRBM_ColorMask_Red * ( 1.0 - min( 1.0, abs( H / 0.08333333 ) ) );
	SMod += fRBM_ColorMask_Orange * ( 1.0 - min( 1.0, abs( ( 0.08333333 - H ) / ( - 0.08333333 ) ) ) );
	SMod += fRBM_ColorMask_Yellow * ( 1.0 - min( 1.0, abs( ( 0.16666667 - H ) / ( - 0.16666667 ) ) ) );
	SMod += fRBM_ColorMask_Green * ( 1.0 - min( 1.0, abs( ( 0.33333333 - H ) / 0.16666667 ) ) );
	SMod += fRBM_ColorMask_Cyan * ( 1.0 - min( 1.0, abs( ( 0.5 - H ) / 0.16666667 ) ) );
	SMod += fRBM_ColorMask_Blue * ( 1.0 - min( 1.0, abs( ( 0.66666667 - H ) / 0.16666667 ) ) );
	SMod += fRBM_ColorMask_Magenta * ( 1.0 - min( 1.0, abs( ( 0.83333333 - H ) / 0.16666667 ) ) );
	SMod += fRBM_ColorMask_Red * ( 1.0 - min( 1.0, abs( ( 1.0 - H ) / 0.16666667 ) ) );
	return SMod;
} 
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4 PS_RBM_Gen(VS_OUTPUT IN) : SV_Target
{
	float scenedepth 		    = GetLinearizedDepth(IN.txcoord.xy);
	float3 SurfaceNormals 		= GetNormalFromDepth(IN.txcoord.xy).xyz;
	float3 TextureNormals 		= GetNormalFromColor(IN.txcoord.xy, 0.01 * PixelSize.xy / scenedepth, 0.0002 / scenedepth + 0.1, 1000.0);
	float3 SceneNormals		    = GetBlendedNormals(SurfaceNormals, TextureNormals);
	SceneNormals 			    = normalize(lerp(SurfaceNormals,SceneNormals,fRBM_ReliefHeight));
	float3 ScreenSpacePosition 	= GetPosition(IN.txcoord.xy);
	float3 ViewDirection 		= normalize(ScreenSpacePosition.xyz);

	float4 color = TextureColor.Sample(LinearSampler, IN.txcoord.xy);
	float3 bump = 0.0;

	for(float i=0; i<=iRBM_SampleCount; i++)
	{
		float2 currentOffset 	= IN.txcoord.xy + SceneNormals.xy * PixelSize.xy * i/(float)iRBM_SampleCount * fRBM_BlurWidthPixels;
		float4 texelSample   	= TextureColor.SampleLevel(LinearSampler, float4(currentOffset,0,0));	
		
		float depthDiff 	    = smoothstep(0.005,0.0,scenedepth-GetLinearizedDepth(currentOffset));
		float colorWeight 	    = smoothstep(fRBM_LowerThreshold,fRBM_UpperThreshold+0.00001,dot(texelSample.xyz,float3(0.299,0.587,0.114)));
		bump                   += lerp(color.xyz,texelSample.xyz,depthDiff*colorWeight);
	}

	bump /= iRBM_SampleCount;

	float cosphi               = dot(-ViewDirection, SceneNormals);

	float SchlickReflectance   = lerp(pow(1.0-cosphi,5.0), 1.0, fRBM_FresnelReflectance);
	SchlickReflectance         = saturate(SchlickReflectance)*fRBM_FresnelMult; // *should* be 0~1 but isn't for some pixels.

	float3 hsvcol              = RGB2HSV(color.xyz);
	float colorMask            = GetHueMask(hsvcol.x);
	colorMask                  = lerp(1.0,colorMask, smoothstep(0.0,0.2,hsvcol.y) * smoothstep(0.0,0.1,hsvcol.z));
	color.xyz                  = lerp(color.xyz,bump.xyz,SchlickReflectance*colorMask);

	return color;
}