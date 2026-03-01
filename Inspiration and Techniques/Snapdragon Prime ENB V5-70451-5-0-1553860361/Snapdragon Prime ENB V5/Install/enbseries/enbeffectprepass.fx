//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// ENBSeries effect file
// visit http://enbdev.com for updates
// Copyright (c) 2007-2017 Boris Vorontsov
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//basic example of using skinned objects mask to make edge detection


//+++++++++++++++++++++++++++++
//internal parameters, can be modified
//+++++++++++++++++++++++++++++
int	EOutlineType
<
	string UIName="OutlineType";
	string UIWidget="Spinner";
	int UIMin=0;
	int UIMax=2;
> = {0};

float	EOutlineThickness
<
	string UIName="OutlineThickness";
	string UIWidget="Spinner";
	float UIMin=0.0;
	float UIMax=2.0;
> = {1.0};

float	EOutlineFadeDistance
<
	string UIName="OutlineFadeDistance";
	string UIWidget="Spinner";
	float UIMin=0.0;
	float UIMax=3.0;
> = {1.0};

float	EOutlineTransparency
<
	string UIName="OutlineTransparency";
	string UIWidget="Spinner";
	float UIMin=0.0;
	float UIMax=1.0;
> = {0.0};

float3	EOutlineColor <
	string UIName="OutlineColor";
	string UIWidget="Color";
> = {0.0, 0.0, 0.0};

float	EOutlineModulateIntensity
<
	string UIName="OutlineModulateIntensity";
	string UIWidget="Spinner";
	float UIMin=0.0;
	float UIMax=10.0;
> = {1.0};

float3	EOutlineModulateColor <
	string UIName="OutlineModulateColor";
	string UIWidget="Color";
> = {0.0, 0.0, 0.0};


//+++++++++++++++++++++++++++++
//external parameters, do not modify
//+++++++++++++++++++++++++++++
//keyboard controlled temporary variables (in some versions exists in the config file). Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4	tempF1; //0,1,2,3
float4	tempF2; //5,6,7,8
float4	tempF3; //9,0
//x=Width, y=1/Width, z=ScreenScaleY, w=1/ScreenScaleY
float4	ScreenSize;
//x=generic timer in range 0..1, period of 16777216 ms (4.6 hours), w=frame time elapsed (in seconds)
float4	Timer;
//changes in range 0..1, 0 means that night time, 1 - day time
float	ENightDayFactor;
//changes 0 or 1. 0 means that exterior, 1 - interior
float	EInteriorFactor;
//adaptation delta time for focusing
float	FadeFactor;
//fov in degrees
float	FieldOfView;



//textures
texture2D texOriginal;
texture2D texMask; //alpha channel is mask for skinned objects (less than 1) and amount of sss
texture2D texColor;
texture2D texDepth;
texture2D texNoise;
texture2D texJitter;
texture2D texPalette;
//texture2D texFocus; //computed focusing depth
//texture2D texCurr; //4*4 texture for focusing
//texture2D texPrev; //4*4 texture for focusing

sampler2D SamplerOriginal = sampler_state
{
	Texture   = <texOriginal>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerMask = sampler_state
{
	Texture   = <texMask>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerColor = sampler_state
{
	Texture   = <texColor>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerDepth = sampler_state
{
	Texture   = <texDepth>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerNoise = sampler_state
{
	Texture   = <texNoise>;
	MinFilter = POINT;
	MagFilter = POINT;
	MipFilter = NONE;//NONE;
	AddressU  = Wrap;
	AddressV  = Wrap;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerJitter = sampler_state
{
	Texture   = <texJitter>;
	MinFilter = POINT;
	MagFilter = POINT;
	MipFilter = NONE;//NONE;
	AddressU  = Wrap;
	AddressV  = Wrap;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerPalette = sampler_state
{
	Texture   = <texPalette>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};
/*
//for focus computation
sampler2D SamplerCurr = sampler_state
{
	Texture   = <texCurr>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = LINEAR;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

//for focus computation
sampler2D SamplerPrev = sampler_state
{
	Texture   = <texPrev>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};
//for dof only in PostProcess techniques
sampler2D SamplerFocus = sampler_state
{
	Texture   = <texFocus>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};
*/
struct VS_OUTPUT_POST
{
	float4 vpos  : POSITION;
	float2 txcoord : TEXCOORD0;
};

struct VS_INPUT_POST
{
	float3 pos  : POSITION;
	float2 txcoord : TEXCOORD0;
};



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
VS_OUTPUT_POST VS_PostProcess(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST OUT;

	float4 pos=float4(IN.pos.x,IN.pos.y,IN.pos.z,1.0);

	OUT.vpos=pos;
	OUT.txcoord.xy=IN.txcoord.xy;

	return OUT;
}


//generate edges
float4 PS_ProcessPass1(VS_OUTPUT_POST IN, float2 vPos : VPOS) : COLOR
{
	float4	res;
	float4	coord;
	coord.zw=0.0;
	coord.xy=IN.txcoord.xy;

	float2	invscreensize=ScreenSize.y;
	invscreensize.y*=ScreenSize.z;

	float4	mask=tex2D(SamplerMask, coord.xy);
	//if (mask.w==1.0) mask=0.0; else mask=1.0;
	mask=(mask.w == 1.0) ? 0.0 : 1.0;

	//clip((254.0/255.0) - mask.w); //works properly only with one pass to increase speed, otherwise make one more pass

	float	depth=tex2D(SamplerDepth, coord.xy).x;
	float	origdepth=depth;

	depth=1.0/(1.0-depth);

	float	fadefact=saturate(1.0-depth*0.005*EOutlineFadeDistance);

	invscreensize*=fadefact;
	invscreensize*=EOutlineThickness;

	float2 offset[16]=
	{
	 float2(1.0, 1.0),
	 float2(-1.0, -1.0),
	 float2(-1.0, 1.0),
	 float2(1.0, -1.0),

	 float2(1.0, 0.0),
	 float2(-1.0, 0.0),
	 float2(0.0, 1.0),
	 float2(0.0, -1.0),

	 float2(1.41, 0.0),
	 float2(-1.41, 0.0),
	 float2(0.0, 1.41),
	 float2(0.0, -1.41),

	 float2(1.41, 1.41),
	 float2(-1.41, -1.41),
	 float2(-1.41, 1.41),
	 float2(1.41, -1.41)
	};

	float	weight=1.0;
	float	edges=0.0;
	float	averagedepth=depth;
	for (int i=0; i<16; i++)
	{
		float	tempdepth;
		coord.xy=IN.txcoord.xy + offset[i].xy*invscreensize;
		tempdepth=tex2Dlod(SamplerDepth, coord).x;

		tempdepth=1.0/(1.0-tempdepth);
		float	depthdiff=depth-tempdepth;

		float4	tempmask=tex2Dlod(SamplerMask, coord);
		tempmask=(tempmask.w == 1.0) ? 0.0 : 1.0;
		//if (depthdiff<0.0) tempmask.w=0.0; //remove from overlayed objects
		tempmask.w*=saturate(1.0+depthdiff*5.0); //remove from overlayed objects
		mask.w=max(tempmask.w, mask.w);
		//mask.x=min(tempmask.w, mask.x);

		//float	tempweight=1.0;
		//not works because of linear textures filtering for mask and depth
		//tempweight=((depthdiff>0.3) && (tempmask.x==0.0)) ? 0.0 : 1.0; //remove from overlayed objects

		//edges+=saturate( depthdiff*4.0 );
		edges+=saturate( abs(depthdiff*4.0) );
		averagedepth+=tempdepth;
	//	float	tempweight;
	//	tempweight=saturate( -(depthdiff) );
	//	averagedepth+=tempdepth * tempweight;
	//	weight+=tempweight;
	}
	edges=max(edges-2.0, 0.0); //skip 2 pixels to reduce bugs
	edges=saturate(edges);
	averagedepth*=1.0/17.0;
	//averagedepth/=weight;

	float	diff=(depth-averagedepth)*8.0;
	diff/=max(EOutlineThickness, 0.1); //correction for high scaling
	//edges*=saturate( diff ); //outer edges
	//edges*=saturate( -diff ); //inner edges
	//edges*=saturate( abs(diff) ); //both edges
	if (EOutlineType==1) diff=-diff;
	if (EOutlineType==2) diff=abs(diff);
	edges=saturate( diff );

	edges=lerp(0.0, edges, fadefact);
	res.w=saturate(edges);

	res.xyz=mask.w;
//	res.y=saturate(mask.w-mask.x);

	return res;
}


//blur previously generated edges and mix with colors
float4 PS_ProcessPass2(VS_OUTPUT_POST IN, float2 vPos : VPOS) : COLOR
{
	float4	res;
	float4	coord;
	coord.zw=0.0;
	coord.xy=IN.txcoord.xy;

	float2	invscreensize=ScreenSize.y;
	invscreensize.y*=ScreenSize.z;

	invscreensize*=0.5;

//	float4	mask=tex2D(SamplerMask, coord.xy);

	float4	origcolor=tex2D(SamplerOriginal, coord.xy);
	float4	color=tex2D(SamplerColor, coord.xy);

	float2 offset[16]=
	{
	 float2(1.0, 1.0),
	 float2(-1.0, -1.0),
	 float2(-1.0, 1.0),
	 float2(1.0, -1.0),

	 float2(1.0, 0.0),
	 float2(-1.0, 0.0),
	 float2(0.0, 1.0),
	 float2(0.0, -1.0),

	 float2(1.41, 0.0),
	 float2(-1.41, 0.0),
	 float2(0.0, 1.41),
	 float2(0.0, -1.41),

	 float2(1.41, 1.41),
	 float2(-1.41, -1.41),
	 float2(-1.41, 1.41),
	 float2(1.41, -1.41)
	};

	for (int i=0; i<8; i++)
	{
		float4	tempcolor;
		coord.xy=IN.txcoord.xy + offset[i].xy*invscreensize;
		tempcolor=tex2Dlod(SamplerColor, coord);

		color+=tempcolor;
	}
	color*=1.0/9.0;

	color.w*=color.x;
	color.w=saturate(1.0-color.w);

	res.xyz=lerp(EOutlineColor.xyz, origcolor.xyz, saturate(color.w+EOutlineTransparency));
	color.xyz=lerp(EOutlineModulateColor, 0.0, color.w);
	res.xyz+=color * origcolor * EOutlineModulateIntensity;

	res.w=1.0;
	return res;
}



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
technique PostProcess
{
	pass P0
	{

		VertexShader = compile vs_3_0 VS_PostProcess();
		PixelShader  = compile ps_3_0 PS_ProcessPass1();

		ZEnable=FALSE;
		CullMode=NONE;
		ALPHATESTENABLE=FALSE;
		SEPARATEALPHABLENDENABLE=FALSE;
		AlphaBlendEnable=FALSE;
		SRGBWRITEENABLE=FALSE;
	}
}

technique PostProcess2
{
	pass P0
	{

		VertexShader = compile vs_3_0 VS_PostProcess();
		PixelShader  = compile ps_3_0 PS_ProcessPass2();

		ZEnable=FALSE;
		CullMode=NONE;
		ALPHATESTENABLE=FALSE;
		SEPARATEALPHABLENDENABLE=FALSE;
		AlphaBlendEnable=FALSE;
		SRGBWRITEENABLE=FALSE;
	}
}


