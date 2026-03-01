//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  ENBSeries effect file                                               //
//  visit http://enbdev.com for updates                                 //
//  Copyright (c) 2007-2013 Boris Vorontsov                             //
//                                                                      //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//  by kingeric1992, based on gp65cj04's work                           //
//      more info at                                                    //
//          http://enbseries.enbdev.com/forum/viewtopic.php?f=7&t=3224  //
//  update: Dec.13.14                                                   //
//                                                                      //
//////////////////////////////////////////////////////////////////////////
//                                                                      //
//  internal parameters, can be modified                                //
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++//
//                                                                      //
//  place "//" before "#define" to disable specific feature entirely,   //
//  equivalent to setting effect intensity 0, but save some performance //
//  by skipping computation.                                            //
//                                                                      //
//  example:                                                            //
//      //#define example                                               //
//                                                                      //
//////////////////////////////////////////////////////////////////////////

#define CHROMATIC_ABERRATION 	//Axial & Transverse chromatic aberration
#define VIGNETTE				//Optical vignette
#define NOISE					//Noise grain

bool 	MF_MODE			<string UIName="MF mode";					> = {false};
bool 	AFsettings 		<string UIName="------AUTO FOCUS------";	> = {false};
bool 	AFcursor 		<string UIName="Display Cursor";			> = {false};
float2 	FP				<string UIName="AF pos";					float UIMin=0.00;	float UIMax=1.00;	float UIStep=0.001;	> = {0.5, 0.5};	//(0, 0) at top left corner.
float 	NearBlurCurve 	<string UIName="AF Near Blur Curve";		float UIMin=1;		float UIMax=50.00;	> = {12.00};
float	FarBlurCurve 	<string UIName="AF Far Blur Curve";			float UIMin=0.00;	float UIMax=100.00;	> = {2.00};
bool 	MFsettings 		<string UIName="------MANUAL FOCUS------";	> = {false};
float	FocalPlaneDepth <string UIName="MF Focus Plane";			float UIMin=0.00;	float UIMax=1000.00;> = {2};
float 	FarBlurDepth 	<string UIName="MF Far Blur Plane";			float UIMin=0.00;	float UIMax=1000.00;> = {10};
bool 	TSsettings 		<string UIName="-------TILT-SHIFT-------";	> = {false};
int 	TS_Axis 		<string UIName="Tilt Shift Axis(\xB0)";		int   UIMin=0;		int   UIMax=90;		> = {0};	//Rotate tilt shift axis
float 	TiltShiftAngle 	<string UIName="Tilt Shift Angle";			float UIMin=-80.00;	float UIMax=80.00;	> = {0.00};	//0 == no tilt shift
bool 	DoFsettings 	<string UIName="-------BOKEH BLUR-------";	> = {false};
int 	quality			<string UIName="DoF Quality";				int   UIMin=1;		int   UIMax=7;		> = {4};	//DoF quality, 7 == max.
int 	POLYGON 		<string UIName="Aperture Shape";			int   UIMin=0;		int   UIMax=3;		> = {0};	//Aperture Shape, 0 == circle, 1 == pentagon, 2 == hexagon, 3 == octagon
int 	BOKEH_ANGLE 	<string UIName="Aperture Angle(\xB0)";		int	  UIMin=0;		int   UIMax=72;		> = {0};	//Base aperture angle.
float 	Highlight		<string UIName="Bokeh Highlight";			float UIMin=0;		float UIMax=20;		> = {3};	//	> 1 to increace highlight, < 1 to decreace.
float 	BokehBias 		<string UIName="Bokeh Bias";				float UIMin=0.00;	float UIMax=1;		> = {0.5};	//Brightness of center point
float 	BokehBiasCurve 	<string UIName="Bokeh Bias Curve";			float UIMin=0.00;	float UIMax=10.00;	> = {0.5};	//brightness curve from center to edge
float 	RadiusMultiplier<string UIName="Bokeh Radius";				float UIMin=0.00;	float UIMax=50.00;	> = {35};	//CoC Multiplier.
float 	GuassianColor 	<string UIName="Guassian Radius";			float UIMin=0;		float UIMax=5;		> = {0.75};	//Guassian Size.
#ifdef 	VIGNETTE	
bool 	OVsettings 		<string UIName="---OPTICAL VIGNETTE---";	> = {false};
float 	VigBias 		<string UIName="Vignette Bias";				float UIMin=0;		float UIMax=1;		> = {0};	//0 == no vignette.
float 	VigScaleOffset 	<string UIName="Vignette Radius";			float UIMin=0;		float UIMax=10;		> = {0};	//vignette radius, minimum == max CoC.
#endif
#ifdef CHROMATIC_ABERRATION
bool 	CAsettings 		<string UIName="--CHROMATIC ABERRATION--";	> = {false};										
float 	CA_axial 		<string UIName="Axial CA";					float UIMin=0.00;	float UIMax=0.50;	> = {0};	//Axial(longitudinal) & Transverse(lateral) CA
float 	CA_trans 		<string UIName="Trans CA";					float UIMin=0.00;	float UIMax=0.005;	float UIStep=0.0001;	> = {0};
#endif
#ifdef NOISE
bool 	noisesettings 	<string UIName="----------NOISE----------";	> = {false};
float 	NoiseAmount 	<string UIName="Noise Amount";				float UIMin=0.00;	float UIMax=100.00;	> = {0.01};
float 	NoiseCurve 		<string UIName="Noise Curve";				float UIMin=0.00;	float UIMax=1.00;	> = {0.95};
#endif

//////////////////////////////////////////////////////////////////////
//	external parameters, do not modify                              //
//////////////////////////////////////////////////////////////////////

//keyboard controlled temporary variables (in some versions exists in the config file). 
//Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4 	tempF1; 	//1,2,3,4
float4 	tempF2; 	//5,6,7,8
float4 	tempF3; 	//9,0
float4 	ScreenSize;	//x=Width, y=1/Width, z=ScreenScaleY, w=1/ScreenScaleY
float4 	Timer;		//x=generic timer in range 0..1, period of 16777216 ms (4.6 hours), w=frame time elapsed (in seconds)
float 	FadeFactor;	//adaptation delta time for focusing

//textures
texture2D texColor;
texture2D texDepth;
texture2D texNoise;
texture2D texPalette;
texture2D texFocus;	//computed focusing depth
texture2D texCurr; 	//4*4 texture for focusing
texture2D texPrev; 	//4*4 texture for focusing

sampler2D SamplerColor = sampler_state
{
	Texture = <texColor>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU = Clamp;
	AddressV = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerDepth = sampler_state
{
	Texture = <texDepth>;
	MinFilter = POINT;
	MagFilter = POINT;
	MipFilter = NONE;
	AddressU = Clamp;
	AddressV = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerNoise = sampler_state
{
	Texture = <texNoise>;
	MinFilter = POINT;
	MagFilter = POINT;
	MipFilter = NONE;//NONE;
	AddressU = Wrap;
	AddressV = Wrap;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerPalette = sampler_state
{
	Texture = <texPalette>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU = Clamp;
	AddressV = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

//for focus computation
sampler2D SamplerCurr = sampler_state
{
	Texture = <texCurr>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = LINEAR;//NONE;
	AddressU = Clamp;
	AddressV = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

//for focus computation
sampler2D SamplerPrev = sampler_state
{
	Texture = <texPrev>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;
	AddressU = Clamp;
	AddressV = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

//for dof only in PostProcess techniques
sampler2D SamplerFocus = sampler_state
{
	Texture = <texFocus>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;
	AddressU = Clamp;
	AddressV = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

//////////////////////////////////////////////////////////////////////
// Functions                                                        //
//////////////////////////////////////////////////////////////////////

struct VS_OUTPUT_POST
{
	float4 vpos : POSITION;
	float2 txcoord : TEXCOORD0;
};

struct VS_INPUT_POST
{
	float3 pos : POSITION;
	float2 txcoord : TEXCOORD0;
};

//squre lens distortion
float2 LensDistortion( float2 tex, float s)
{	
    float r2 = (tex.x - 0.5) * (tex.x - 0.5) + (tex.y - 0.5) * (tex.y - 0.5) * ScreenSize.z * ScreenSize.z;
    float f = 1 + r2 * s;
    float2 uv = f * (tex - 0.5) + 0.5;	
	return uv;
}

float linearizeDepth(float nonlinearDepth)
{
	float2 dofProj=float2(0.15, 3000);
	float2 dofDist=float2(0.0, 0.15);
	float4 depth=nonlinearDepth;
	
	depth.y=-dofProj.x + dofProj.y;
	depth.y=1.0/depth.y;
	depth.z=depth.y * dofProj.y;
	depth.z=depth.z * -dofProj.x;
	depth.x=dofProj.y * -depth.y + depth.x;
	depth.x=1.0/depth.x;
	depth.y=depth.z * depth.x;
	depth.x=depth.z * depth.x - dofDist.y;
	depth.x+=dofDist.x * -0.5;
	depth.x=max(depth.x, 0.0);
	return depth.x;
}

//////////////////////////////////////////////////////////////////////
//	begin focusing code                                             //
//////////////////////////////////////////////////////////////////////

VS_OUTPUT_POST VS_Focus(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST OUT;
	float4 pos=float4(IN.pos.x,IN.pos.y,IN.pos.z,1.0);

	OUT.vpos=pos;
	OUT.txcoord.xy=IN.txcoord.xy;
	return OUT;
}

//SRCpass1X=ScreenWidth;
//SRCpass1Y=ScreenHeight;
//DESTpass2X=4;
//DESTpass2Y=4;
float4 PS_ReadFocus(VS_OUTPUT_POST IN) : COLOR
{
	float2 uvsrc;
	uvsrc.x = FP.x;
	uvsrc.y = FP.y;

	float2 pixelSize=ScreenSize.y;
	pixelSize.y*=ScreenSize.z;

	const float2 offset[4]=
	{
		float2(0.0, 1.0),
		float2(0.0, -1.0),
		float2(1.0, 0.0),
		float2(-1.0, 0.0)
	};

	float res=linearizeDepth(tex2D(SamplerDepth, uvsrc.xy).x);
	for (int i=0; i<4; i++)
	{
		uvsrc.xy=uvsrc.xy;
		uvsrc.xy+=offset[i] * pixelSize.xy * 0.5;
		res+=linearizeDepth(tex2D(SamplerDepth, uvsrc).x);
	}
	res*=0.2;
	return res;
}

//SRCpass1X=4;
//SRCpass1Y=4;
//DESTpass2X=4;
//DESTpass2Y=4;
float4 PS_WriteFocus(VS_OUTPUT_POST IN) : COLOR
{
	float2 uvsrc;
	uvsrc.x = FP.x;
	uvsrc.y = FP.y;

	float res=0.0;
	float curr=tex2D(SamplerCurr, uvsrc.xy).x;
	float prev=tex2D(SamplerPrev, uvsrc.xy).x;
	res=lerp(prev, curr, saturate(FadeFactor));//time elapsed factor
	return res;
}

//////////////////////////////////////////////////////////////////////
//	Focus pass                                                      //
//////////////////////////////////////////////////////////////////////

technique ReadFocus
{
	pass P0
	{
		VertexShader = compile vs_3_0 VS_Focus();
		PixelShader = compile ps_3_0 PS_ReadFocus();

		ZEnable=FALSE;
		CullMode=NONE;
		ALPHATESTENABLE=FALSE;
		SEPARATEALPHABLENDENABLE=FALSE;
		AlphaBlendEnable=FALSE;
		FogEnable=FALSE;
		SRGBWRITEENABLE=FALSE;
	}
}

technique WriteFocus
{
	pass P0
	{
		VertexShader = compile vs_3_0 VS_Focus();
		PixelShader = compile ps_3_0 PS_WriteFocus();

		ZEnable=FALSE;
		CullMode=NONE;
		ALPHATESTENABLE=FALSE;
		SEPARATEALPHABLENDENABLE=FALSE;
		AlphaBlendEnable=FALSE;
		FogEnable=FALSE;
		SRGBWRITEENABLE=FALSE;
	}
}

//////////////////////////////////////////////////////////////////////
//	end focusing, starting DoF                                      //
//////////////////////////////////////////////////////////////////////

VS_OUTPUT_POST VS_PostProcess(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST OUT;
	float4 pos=float4(IN.pos.x,IN.pos.y,IN.pos.z,1.0);

	OUT.vpos=pos;
	OUT.txcoord.xy=IN.txcoord.xy;

	return OUT;
}

//Calculate CoC & Tilt_Shift
float4 PS_CoCtoAlpha(VS_OUTPUT_POST IN, float2 vPos : VPOS) : COLOR
{	
	float2 coord = IN.txcoord.xy;
	float4 res = tex2D(SamplerColor, coord.xy);;
	float scenefocus = tex2D(SamplerFocus, 0.5).x;
	float depth = linearizeDepth(tex2D(SamplerDepth, IN.txcoord.xy).x);
	
	int MF = (MF_MODE == true)? 1 : 0;
	float focalPlaneDepth = lerp(scenefocus, FocalPlaneDepth, MF);
	float farBlurDepth = lerp(scenefocus*pow(4.0, FarBlurCurve), FarBlurDepth, MF);

	float shiftAngle = (frac(TiltShiftAngle / 90.0) == 0) ? 0.0 : TiltShiftAngle;
	float2 othogonal = float2(tan(TS_Axis * 0.0174533), -ScreenSize.z);
	float TS_Dist = dot(coord - 0.5, othogonal) / length(othogonal);
	float depthShift = 1 + TS_Dist * tan(-shiftAngle * 0.017453292);		

	focalPlaneDepth *= max(depthShift, 0);
	farBlurDepth *= max(depthShift, 0.001);
		
	if(depth < focalPlaneDepth)
		res.w=(depth - focalPlaneDepth)/focalPlaneDepth;
	else
	{
		res.w=(depth - focalPlaneDepth)/(farBlurDepth - focalPlaneDepth);
		res.w=saturate(res.w);
	}
	
	res.w=res.w * 0.5 + 0.5;
	return res;
}

//Dof pass
float4 PS_BokehCircle(VS_OUTPUT_POST IN, float2 vPos : VPOS) : COLOR
{
	float2 coord=IN.txcoord.xy;
	float4 origcolor=tex2D(SamplerColor, coord.xy);
	float centerDepth=origcolor.w;
	float2 pixelSize=ScreenSize.y;
	pixelSize.y*=ScreenSize.z;

	float blurAmount=abs(centerDepth * 2.0 - 1.0);
	float discRadius=blurAmount * RadiusMultiplier;
	float AFnear = (centerDepth < 0.5) ? (1.0 / NearBlurCurve) : 1.0;
	discRadius *= (MF_MODE == true)? 1.0 : AFnear;
	
#ifdef VIGNETTE	
	float vigradius = (RadiusMultiplier + VigScaleOffset) * ScreenSize.y * 1.1;
	float2 vigcenter = LensDistortion(coord, lerp(0, vigradius, VigBias) * 1.82) - coord;
#endif

	float4 res;
	float3 weight = 1;
	res.xyz = origcolor.xyz;
	res.xyz *= lerp(1.0, 0, BokehBias);	
	res.xyz = pow(res.xyz, Highlight) * weight;
	res.w = weight.x;

	float sampleCycleCounter;
	float sampleCounterInCycle;
	float2 sampleOffset;
	int dofTaps = quality * (quality + 1) * 4;

	for(int i=0; i < 224 && i < dofTaps; i++)
	{
		if((sampleCounterInCycle % (sampleCycleCounter * 8)) == 0)
		{
			sampleCounterInCycle = 0;
			sampleCycleCounter++;
		}
		
		float sampleAngle = 0.78539816 * ( sampleCounterInCycle / sampleCycleCounter);
		sampleCounterInCycle++;
		sincos(sampleAngle, sampleOffset.y, sampleOffset.x);
		sampleOffset *= pixelSize * discRadius * sampleCycleCounter / quality;

#ifdef CHROMATIC_ABERRATION		
		float4 tap;
		tap.ra = tex2Dlod(SamplerColor, float4(LensDistortion(coord + sampleOffset * ( 1 + CA_axial), discRadius * CA_trans), 0, 0)).ra;
		weight.x = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);
		tap.ga = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0)).ga;
		weight.y = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);		
		tap.ba = tex2Dlod(SamplerColor, float4(LensDistortion(coord + sampleOffset * ( 1 - CA_axial), discRadius * -CA_trans), 0, 0)).ba;
		weight.z = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);
#else		
		float4 tap = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0));
		weight.xyz = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);	
#endif		
		tap.xyz *= lerp(1.0, pow(sampleCycleCounter/ quality, BokehBiasCurve), BokehBias);
		
#ifdef VIGNETTE	
		weight *= (length(float2(sampleOffset.x, sampleOffset.y * ScreenSize.w) - vigcenter) > vigradius)? 0 : 1;
#endif		
		res.xyz += pow(tap.xyz * weight.xyz, Highlight);
		res.w += pow(weight.y, Highlight);
	}
	res.xyz = pow( res.xyz / res.w, 1 / Highlight);
	res.w = centerDepth;

	return res;
}

float4 PS_BokehP5(VS_OUTPUT_POST IN, float2 vPos : VPOS) : COLOR
{
	float2 coord = IN.txcoord.xy;
	float4 origcolor = tex2D(SamplerColor, coord.xy);
	float centerDepth = origcolor.w;
	float2 pixelSize = ScreenSize.y;
	pixelSize.y *= ScreenSize.z;

	float blurAmount = abs(centerDepth * 2.0 - 1.0);
	float discRadius = blurAmount * RadiusMultiplier;
	float AFnear = (centerDepth < 0.5) ? (1.0 / NearBlurCurve) : 1.0;
	discRadius *= (MF_MODE == true)? 1.0 : AFnear;
	
#ifdef VIGNETTE	
	float vigradius = (RadiusMultiplier + VigScaleOffset) * ScreenSize.y * 1.1;
	float2 vigcenter = LensDistortion(coord, lerp(0, vigradius, VigBias) * 1.82) - coord;
#endif
	
	float4 res;
	float3 weight = 1;
	res.xyz = origcolor.xyz;
	res.xyz *= lerp(1.0, 0, BokehBias);	
	res.xyz = pow(res.xyz, Highlight) * weight;
	res.w = weight.x;
	
	float sampleCycleCounter;
	float sampleCounterInCycle;	
	
	float2 currentVertex;	
	float2 nextVertex;
	sincos(BOKEH_ANGLE * 0.017453292, nextVertex.y, nextVertex.x);
	
	float BaseAngle = 72;
	float2 rotation;// .y == sin, .x == cos (rotation matrix, can precompute to increace performance)
	sincos(BaseAngle * 0.017453292, rotation.y, rotation.x);

	int dofTaps = (quality + 2) * (quality + 3) * 2.5;
	
	for(int i=0; i < 225 && i < dofTaps; i++)
	{
		if(sampleCounterInCycle % (sampleCycleCounter * 5) == 0)
		{
			sampleCounterInCycle = 0;
			sampleCycleCounter++;
		}		
		float sideOffset = frac(sampleCounterInCycle/sampleCycleCounter);
		
		if(sideOffset < 0.1)
		{
			currentVertex = nextVertex;
			nextVertex.x = dot(currentVertex, float2(rotation.x, -rotation.y));
			nextVertex.y = dot(currentVertex, float2(rotation.y, rotation.x));
		}		
		float2 sampleOffset=lerp(currentVertex, nextVertex, sideOffset);
		sampleOffset *= pixelSize * discRadius * sampleCycleCounter / (quality + 2);		
		sampleCounterInCycle++;		
		
#ifdef CHROMATIC_ABERRATION		
		float4 tap;
		tap.ra = tex2Dlod(SamplerColor, float4(LensDistortion(coord + sampleOffset * ( 1 + CA_axial), discRadius * CA_trans), 0, 0)).ra;
		weight.x = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);
		tap.ga = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0)).ga;
		weight.y = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);		
		tap.ba = tex2Dlod(SamplerColor, float4(LensDistortion(coord + sampleOffset * ( 1 - CA_axial), discRadius * -CA_trans), 0, 0)).ba;
		weight.z = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);
#else		
		float4 tap = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0));
		weight.xyz = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);	
#endif
		tap.xyz *= lerp(1.0, pow(sampleCycleCounter/ quality, BokehBiasCurve), BokehBias);		

#ifdef VIGNETTE	
		weight *= (length(float2(sampleOffset.x, sampleOffset.y * ScreenSize.w) - vigcenter) > vigradius)? 0 : 1;
#endif	
		res.xyz += pow(tap.xyz * weight.xyz, Highlight);
		res.w += pow(weight.y, Highlight);
	}
	res.xyz = pow( res.xyz / res.w, 1 / Highlight);
	res.w = centerDepth;

	return res;
}

float4 PS_BokehP6(VS_OUTPUT_POST IN, float2 vPos : VPOS) : COLOR
{
	float2 coord = IN.txcoord.xy;
	float4 origcolor = tex2D(SamplerColor, coord.xy);
	float centerDepth = origcolor.w;
	float2 pixelSize = ScreenSize.y;
	pixelSize.y *= ScreenSize.z;

	float blurAmount = abs(centerDepth * 2.0 - 1.0);
	float discRadius = blurAmount * RadiusMultiplier;
	float AFnear = (centerDepth < 0.5) ? (1.0 / NearBlurCurve) : 1.0;
	discRadius *= (MF_MODE == true)? 1.0 : AFnear;

#ifdef VIGNETTE	
	float vigradius = (RadiusMultiplier + VigScaleOffset) * ScreenSize.y * 1.1;
	float2 vigcenter = LensDistortion(coord, lerp(0, vigradius, VigBias) * 1.82) - coord;
#endif
	
	float4 res;
	float3 weight = 1;
	res.xyz = origcolor.xyz;
	res.xyz *= lerp(1.0, 0, BokehBias);	
	res.xyz = pow(res.xyz, Highlight) * weight;
	res.w = weight.x;
	
	float sampleCycleCounter;//layer # == tap per side
	float sampleCounterInCycle;//tap # in layer		
	
	float2 currentVertex;	
	float2 nextVertex;
	sincos(BOKEH_ANGLE * 0.017453292, nextVertex.y, nextVertex.x);
	
	float BaseAngle = 60;
	float2 rotation;// .y == sin, .x == cos (rotation matrix, can precompute to increace performance)
	sincos(BaseAngle * 0.017453292, rotation.y, rotation.x);

	int dofTaps = (quality + 1) * (quality + 2) * 3;
	
	for(int i=0; i < 216 && i < dofTaps; i++)
	{
		if(sampleCounterInCycle % (sampleCycleCounter * 6) == 0)
		{
			sampleCounterInCycle = 0;
			sampleCycleCounter++;
		}		
		float sideOffset = frac(sampleCounterInCycle/sampleCycleCounter);
		
		if(sideOffset < 0.1)
		{
			currentVertex = nextVertex;
			nextVertex.x = dot(currentVertex, float2(rotation.x, -rotation.y));
			nextVertex.y = dot(currentVertex, float2(rotation.y, rotation.x));
		}		
		float2 sampleOffset=lerp(currentVertex, nextVertex, sideOffset);
		sampleOffset *= pixelSize * discRadius * sampleCycleCounter / (quality + 1);		
		sampleCounterInCycle++;		
				
#ifdef CHROMATIC_ABERRATION		
		float4 tap;
		tap.ra = tex2Dlod(SamplerColor, float4(LensDistortion(coord + sampleOffset * ( 1 + CA_axial), discRadius * CA_trans), 0, 0)).ra;
		weight.x = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);
		tap.ga = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0)).ga;
		weight.y = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);		
		tap.ba = tex2Dlod(SamplerColor, float4(LensDistortion(coord + sampleOffset * ( 1 - CA_axial), discRadius * -CA_trans), 0, 0)).ba;
		weight.z = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);
#else		
		float4 tap = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0));
		weight.xyz = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);	
#endif		
		tap.xyz *= lerp(1.0, pow(sampleCycleCounter/ quality, BokehBiasCurve), BokehBias);			
		
#ifdef VIGNETTE	
		weight *= (length(float2(sampleOffset.x, sampleOffset.y * ScreenSize.w) - vigcenter) > vigradius)? 0 : 1;
#endif	
		res.xyz += pow(tap.xyz * weight.xyz, Highlight);
		res.w += pow(weight.y, Highlight);
	}
	res.xyz = pow( res.xyz / res.w, 1 / Highlight);
	res.w = centerDepth;

	return res;
}

float4 PS_BokehP8(VS_OUTPUT_POST IN, float2 vPos : VPOS) : COLOR
{
	float2 coord = IN.txcoord.xy;
	float4 origcolor = tex2D(SamplerColor, coord.xy);
	float centerDepth = origcolor.w;
	float2 pixelSize = ScreenSize.y;
	pixelSize.y *= ScreenSize.z;

	float blurAmount = abs(centerDepth * 2.0 - 1.0);
	float discRadius = blurAmount * RadiusMultiplier;
	float AFnear = (centerDepth < 0.5) ? (1.0 / NearBlurCurve) : 1.0;
	discRadius *= (MF_MODE == true)? 1.0 : AFnear;

#ifdef VIGNETTE	
	float vigradius = (RadiusMultiplier + VigScaleOffset) * ScreenSize.y * 1.1;
	float2 vigcenter = LensDistortion(coord, lerp(0, vigradius, VigBias) * 1.82) - coord;
#endif
	
	float4 res;
	float3 weight = 1;
	res.xyz = origcolor.xyz;
	res.xyz *= lerp(1.0, 0, BokehBias);	
	res.xyz = pow(res.xyz, Highlight) * weight;
	res.w = weight.x;

	float sampleCycleCounter;//layer # == tap per side
	float sampleCounterInCycle;//tap # in layer		
	
	float2 currentVertex;	
	float2 nextVertex;
	sincos(BOKEH_ANGLE * 0.017453292, nextVertex.y, nextVertex.x);
	
	float BaseAngle = 45;
	float2 rotation;// .y == sin, .x == cos (rotation matrix, can precompute to increace performance)
	sincos(BaseAngle * 0.017453292, rotation.y, rotation.x);
	
	int dofTaps = quality * (quality + 1) * 4;

	for(int i=0; i < 224 && i < dofTaps; i++)
	{
		if(sampleCounterInCycle % (sampleCycleCounter * 8) == 0)
		{
			sampleCounterInCycle = 0;
			sampleCycleCounter++;
		}		
		float sideOffset = frac( sampleCounterInCycle / sampleCycleCounter);
		
		if(sideOffset < 0.1)
		{
			currentVertex = nextVertex;
			nextVertex.x = dot(currentVertex, float2(rotation.x, -rotation.y));
			nextVertex.y = dot(currentVertex, float2(rotation.y, rotation.x));
		}
		float2 sampleOffset=lerp(currentVertex, nextVertex, sideOffset);
		sampleOffset *= pixelSize * discRadius * sampleCycleCounter / quality;		
		sampleCounterInCycle++;	
		
#ifdef CHROMATIC_ABERRATION		
		float4 tap;
		tap.ra = tex2Dlod(SamplerColor, float4(LensDistortion(coord + sampleOffset * ( 1 + CA_axial), discRadius * CA_trans), 0, 0)).ra;
		weight.x = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);
		tap.ga = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0)).ga;
		weight.y = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);		
		tap.ba = tex2Dlod(SamplerColor, float4(LensDistortion(coord + sampleOffset * ( 1 - CA_axial), discRadius * -CA_trans), 0, 0)).ba;
		weight.z = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);
#else		
		float4 tap = tex2Dlod(SamplerColor, float4(coord + sampleOffset, 0, 0));
		weight.xyz = (tap.a >= centerDepth)? 1.0 : abs(tap.a * 2.0 - 1.0);	
#endif		
		tap.xyz *= lerp(1.0, pow(sampleCycleCounter/ quality, BokehBiasCurve), BokehBias);
		
#ifdef VIGNETTE	
		weight *= (length(float2(sampleOffset.x, sampleOffset.y * ScreenSize.w) - vigcenter) > vigradius)? 0 : 1;
#endif	
		res.xyz += pow(tap.xyz * weight.xyz, Highlight);
		res.w += pow(weight.y, Highlight);
	}
	res.xyz = pow( res.xyz / res.w, 1 / Highlight);
	res.w = centerDepth;

	return res;
}

float4 PS_GuassianH(VS_OUTPUT_POST IN, float2 vPos : VPOS, uniform float BlurStrength) : COLOR
{
	float2 coord = IN.txcoord.xy;
	float2 pixelSize = ScreenSize.y;
	pixelSize.y *= ScreenSize.z;
	float4 origcolor = tex2D(SamplerColor, coord.xy);
	
	float blurAmount = abs(origcolor.w * 2.0 - 1.0) * RadiusMultiplier;
	blurAmount /= 10 * (quality + ((3 - POLYGON) % 3));
	float AFnear = (origcolor.w < 0.5) ? (1.0 / NearBlurCurve) : 1.0;
	blurAmount *= (MF_MODE == true)? 1.0 : AFnear;

	float weight[11] = {0.082607, 0.080977, 0.076276, 0.069041, 0.060049, 0.050187, 0.040306, 0.031105, 0.023066, 0.016436, 0.011254};
	float4 res=origcolor * weight[0];

	for(int i=1; i < 11; i++)
	{
		res	+= tex2D(SamplerColor, coord + float2(i * pixelSize.x * blurAmount * BlurStrength, 0)) * weight[i];
		res	+= tex2D(SamplerColor, coord - float2(i * pixelSize.x * blurAmount * BlurStrength, 0)) * weight[i];
	}
	return res;
}

float4 PS_GuassianV(VS_OUTPUT_POST IN, float2 vPos : VPOS, uniform float BlurStrength) : COLOR
{
	float2 coord = IN.txcoord.xy;
	float2 pixelSize = ScreenSize.y;
	pixelSize.y *= ScreenSize.z;
	float4 origcolor = tex2D(SamplerColor, coord.xy);
	
	float blurAmount = abs(origcolor.w * 2.0 - 1.0) * RadiusMultiplier;
	blurAmount /= 10 * (quality + ((3 - POLYGON) % 3));
	float AFnear = (origcolor.w < 0.5)? (1.0 / NearBlurCurve) : 1.0;
	blurAmount *= (MF_MODE == true)? 1.0 : AFnear;

	float weight[11] = {0.082607, 0.080977, 0.076276, 0.069041, 0.060049, 0.050187, 0.040306, 0.031105, 0.023066, 0.016436, 0.011254};
	float4 res=origcolor * weight[0];

	for(int i=1; i < 11; i++)
	{
		res	+= tex2D(SamplerColor, coord + float2(0, i * pixelSize.x * blurAmount * BlurStrength)) * weight[i];
		res	+= tex2D(SamplerColor, coord - float2(0, i * pixelSize.x * blurAmount * BlurStrength)) * weight[i];
	}
	return res;
}

//Noise + AF cursor
float4 PS_PostProcess(VS_OUTPUT_POST IN, float2 vPos : VPOS) : COLOR
{
	float2 coord = IN.txcoord.xy;
	float4 res = tex2D(SamplerColor, coord);
	
#ifdef NOISE
	float blurAmount = abs(res.a * 2.0 - 1.0) * RadiusMultiplier;
	float AFnear = (res.a < 0.5)? (1.0 / NearBlurCurve) : 1.0;
	blurAmount *= (MF_MODE == true)? 1.0 : AFnear;

	float origgray = dot(res.xyz, 0.3333);
	origgray /= origgray + 1.0;
	float4 cnoi = tex2D(SamplerNoise, coord * 16.0 + origgray);
	float noiseAmount = NoiseAmount * pow(blurAmount, NoiseCurve);
	res *= lerp( 1, (cnoi.x+0.5), noiseAmount * saturate( 1.0 - origgray * 1.8));
#endif

	if(AFcursor == true)
	{
		float2 pixelSize = ScreenSize.y;
		pixelSize.y *= ScreenSize.z;
		if( ( abs(coord.x - FP.x) < 5 * pixelSize.x) && ( abs(coord.y - FP.y) < 5 * pixelSize.y))
			res.rgb = float3(2.0, 0, 0);
	}	
	return res;
}

//////////////////////////////////////////////////////////////////////
//	DoF Pass                                                        //
//////////////////////////////////////////////////////////////////////
technique PostProcess
{
	pass P0
	{

		VertexShader = compile vs_3_0 VS_PostProcess();
		PixelShader = compile ps_3_0 PS_CoCtoAlpha();

		DitherEnable=FALSE;
		ZEnable=FALSE;
		CullMode=NONE;
		ALPHATESTENABLE=FALSE;
		SEPARATEALPHABLENDENABLE=FALSE;
		AlphaBlendEnable=FALSE;
		StencilEnable=FALSE;
		FogEnable=FALSE;
		SRGBWRITEENABLE=FALSE;
	}
}

PixelShader pixelShaders[4] = 
{
    compile ps_3_0 PS_BokehCircle(),
    compile ps_3_0 PS_BokehP5(),
	compile ps_3_0 PS_BokehP6(),
	compile ps_3_0 PS_BokehP8(),
};

technique PostProcess2
{
	pass P0
	{
		VertexShader = compile vs_3_0 VS_PostProcess();
		PixelShader = pixelShaders[POLYGON];

		DitherEnable=FALSE;
		ZEnable=FALSE;
		CullMode=NONE;
		ALPHATESTENABLE=FALSE;
		SEPARATEALPHABLENDENABLE=FALSE;
		AlphaBlendEnable=FALSE;
		StencilEnable=FALSE;
		FogEnable=FALSE;
		SRGBWRITEENABLE=FALSE;
	}
}

technique PostProcess3
{
	pass P0
	{
		VertexShader = compile vs_3_0 VS_PostProcess();
		PixelShader = compile ps_3_0 PS_GuassianH(GuassianColor);

		DitherEnable=FALSE;
		ZEnable=FALSE;
		CullMode=NONE;
		ALPHATESTENABLE=FALSE;
		SEPARATEALPHABLENDENABLE=FALSE;
		AlphaBlendEnable=FALSE;
		StencilEnable=FALSE;
		FogEnable=FALSE;
		SRGBWRITEENABLE=FALSE;
	}
}

technique PostProcess4
{
	pass P0
	{
		VertexShader = compile vs_3_0 VS_PostProcess();
		PixelShader = compile ps_3_0 PS_GuassianV(GuassianColor);

		DitherEnable=FALSE;
		ZEnable=FALSE;
		CullMode=NONE;
		ALPHATESTENABLE=FALSE;
		SEPARATEALPHABLENDENABLE=FALSE;
		AlphaBlendEnable=FALSE;
		StencilEnable=FALSE;
		FogEnable=FALSE;
		SRGBWRITEENABLE=FALSE;
	}
}


technique PostProcess5
{
	pass P0
	{
		VertexShader = compile vs_3_0 VS_PostProcess();
		PixelShader = compile ps_3_0 PS_PostProcess();

		DitherEnable=FALSE;
		ZEnable=FALSE;
		CullMode=NONE;
		ALPHATESTENABLE=FALSE;
		SEPARATEALPHABLENDENABLE=FALSE;
		AlphaBlendEnable=FALSE;
		StencilEnable=FALSE;
		FogEnable=FALSE;
		SRGBWRITEENABLE=FALSE;
	}
}