/*/////////////////////////////////////////////////////////
//                ENBSeries effect file                //
//         visit http://enbdev.com for updates         //
//       Copyright (c) 2007-2018 Boris Vorontsov       //
//----------------------ENB PRESET---------------------//
			MM"""""""`YM MM"""""""`MM M""""""""M 
			MM  mmmmm  M MM  mmmm,  M Mmmm  mmmM 
			M'        .M M'        .M MMMM  MMMM 
			MM  MMMMMMMM MM  MMMb. "M MMMM  MMMM 
			MM  MMMMMMMM MM  MMMMM  M MMMM  MMMM 
			MM  MMMMMMMM MM  MMMMM  M MMMM  MMMM 
			MMMMMMMMMMMM MMMMMMMMMMMM MMMMMMMMMM 
https://www.nexusmods.com/skyrimspecialedition/mods/4743
//-----------------------CREDITS-----------------------//
//     Please do not redistribute without credits      //
// Boris: For ENBSeries and his knowledge and codes    //
// L00 :  Shader Setup, Presets and Settings,          //
//        Port and Modification of Shaders     	       //
//        and author of this file                      //
////////////////////////////////////////////////////////*

/* int		iLensdirtMixmode 			< string UIName="Dirt:: Mixmode"; string UIWidget="spinner"; int UIMin=1; int UIMax=4;> = {1};
float	fLensdirtSaturation			<string UIName="Dirt:: saturation";string UIWidget="spinner";float UIMin=0.0;float UIMax=2.0;> = {2.0};	
float	fLensdirtIntensityDay 		< string UIName="Dirt:: intensity Day"; string UIWidget="spinner"; float UIMin=0.0; float UIMax=20.0;> = {2.0};
float	fLensdirtIntensityNight 		< string UIName="Dirt:: intensity Night"; string UIWidget="spinner"; float UIMin=0.0; float UIMax=20.0;> = {2.0};
float	fLensdirtIntensityInterior 		< string UIName="Dirt:: intensity Interior"; string UIWidget="spinner"; float UIMin=0.0; float UIMax=2.0;> = {2.0};



float	ELensReflectionIntensityDay
<
	string UIName="Lens Leak: Day";
	string UIWidget="spinner";
	float UIMin=0.0;
	float UIMax=1000.0;
> = {1.0};

float	ELensReflectionIntensityNight
<
	string UIName="Lens Leak Night";
	string UIWidget="spinner";
	float UIMin=0.0;
	float UIMax=1000.0;
> = {1.0};

float	ELensReflectionIntensityInt
<
	string UIName="Lens Leak Interior";
	string UIWidget="spinner";
	float UIMin=0.0;
	float UIMax=1000.0;
> = {1.0}; */



/////////////////////////////////////////////////////////
//               	INTERNAL SETTINGS                  //
/////////////////////////////////////////////////////////

#define USE_DEBAND 1 //[Deband] //-Applies debanding to minimize banding artifacts
#define DEBAND_THRESHOLD 1.2
#define DEBAND_RADIUS 32.0 //[0.0:1024.0] //-Sampling radius, higher values will reduce further banding but might also reduce details
#define DEBAND_SAMPLE_COUNT 8 //[1:8] //-Sample count, higher values are better
#define DEBAND_OFFSET_MODE 3 //[1:3] //-1 = cross (axis aligned, fast), 2 = diagonal (45 degrees, slower), 3 = box (fully random, much slower)
#define DEBAND_DITHERING 0 //[0:3] //-Additional dithering options to smoothen the output. 0 = No dithering 1 = Ordered dithering, 2 = Random dithering, 3 = Iestyn's RGB dither (Valve)
#define DEBAND_SKIP_THRESHOLD_TEST 0 //[0:1] //-1 = Skip threshold to see the unfiltered sampling pattern
#define DEBAND_OUTPUT_BOOST 1.0 //[-2.0:2.0] //-Default = 1.0. Any value other than the default activates debug mode. When fine-tuning the values you might use both these settings to boost luminance, which should make it easier to see banding artifacts.
#define DEBAND_OUTPUT_OFFSET 0.0 //[-1.0:3.0] //-Default = 0.0. Any value other than the default activates debug mode. When fine-tuning the values you might use both these settings to boost luminance, which should make it easier to see banding artifacts.

#define	EColorFilter (0.0471, 0.00784, 1.0)
#define	EContrast 9.0
#define	ELensReflectionPowerA 100
#define	ELensDirtIntensityA 1000
#define	ELensDirtPowerA 0.5

#define iLensdirtMixmode				4
#define fLensdirtSaturation				1.8
#define fLensdirtIntensityDay			1.5
#define fLensdirtIntensityNight			10.0
#define fLensdirtIntensityInterior		3.0

#define ELensReflectionIntensityDay		65.0
#define ELensReflectionIntensityNight	120.0
#define ELensReflectionIntensityInt		75.0

float4	Timer;				//x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4	ScreenSize; 		//x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
float	AdaptiveQuality; 	//changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4	Weather; 			//x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours. Weather index is value from weather ini file, for example WEATHER002 means index==2, but index==0 means that weather not captured.
float4	TimeOfDay1; 		//x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay2; 		//x = dusk, y = night. Interpolators range from 0..1
float	ENightDayFactor; 	//changes in range 0..1, 0 means that night time, 1 - day time
float	EInteriorFactor; 	//changes 0 or 1. 0 means that exterior, 1 - interior

//keyboard controlled temporary variables. Press and hold key 1,2,3...8 together with PageUp or PageDown to modify. By default all set to 1.0
float4	tempF1; //0,1,2,3
float4	tempF2; //5,6,7,8
float4	tempF3; //9,0
// xy = cursor position in range 0..1 of screen;
// z = is shader editor window active;
// w = mouse buttons with values 0..7 as follows:
//    0 = none
//    1 = left
//    2 = right
//    3 = left+right
//    4 = middle
//    5 = left+middle
//    6 = right+middle
//    7 = left+right+middle (or rather cat is sitting on your mouse)
float4	tempInfo1;
// xy = cursor position of previous left mouse button click
// zw = cursor position of previous right mouse button click
float4	tempInfo2;

/////////////////////////////////////////////////////////
//               		TEXTURES                       //
/////////////////////////////////////////////////////////

Texture2D			TextureDownsampled; //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format. 1024*1024 size
Texture2D			TextureColor; //color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format. screen size

Texture2D			TextureOriginal; //color R16B16G16A16 64 bit or R11G11B10 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D			TextureDepth; //scene depth R32F 32 bit hdr format, screen size. PLEASE AVOID USING IT BECAUSE OF ALIASING ARTIFACTS, UNLESS YOU FIX THEM
Texture2D			TextureAperture; //this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture of enbdepthoffield.fx
Texture2D TextureBloom; 
//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D			RenderTarget1024; //R16B16G16A16F 64 bit hdr format, 1024*1024 size
Texture2D			RenderTarget512; //R16B16G16A16F 64 bit hdr format, 512*512 size
Texture2D			RenderTarget256; //R16B16G16A16F 64 bit hdr format, 256*256 size
Texture2D			RenderTarget128; //R16B16G16A16F 64 bit hdr format, 128*128 size
Texture2D			RenderTarget64; //R16B16G16A16F 64 bit hdr format, 64*64 size
Texture2D			RenderTarget32; //R16B16G16A16F 64 bit hdr format, 32*32 size
Texture2D			RenderTarget16; //R16B16G16A16F 64 bit hdr format, 16*16 size
Texture2D			RenderTargetRGBA32; //R8G8B8A8 32 bit ldr format, screen size
Texture2D			RenderTargetRGBA64F; //R16B16G16A16F 64 bit hdr format, screen size

Texture2D LensMaskTextureD 		<string ResourceName = "/Textures/Lenses/LensLeakD.jpg";>;
Texture2D LensMaskTextureN 		<string ResourceName = "/Textures/Lenses/LensLeakN.jpg";>;
Texture2D LensMaskTextureI 		<string ResourceName = "/Textures/Lenses/LensLeakI.jpg";>;

Texture2D TextureDirtD 			<string ResourceName = "Textures/Lenses/LeakD.jpg";>;
Texture2D TextureDirtN 			<string ResourceName = "Textures/Lenses/LeakN.jpg";>;
Texture2D TextureDirtI 			<string ResourceName = "Textures/Lenses/LeakI.jpg";>;

/////////////////////////////////////////////////////////
//               	    SAMPLERS                       //
/////////////////////////////////////////////////////////

SamplerState		Sampler0
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState		Sampler1
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Clamp;
	AddressV = Clamp;
};
SamplerState 		SamplerDirt
{
   Filter = MIN_MAG_MIP_LINEAR;  
   AddressU=Clamp;  
   AddressV=Clamp;
};

/////////////////////////////////////////////////////////
//               	   FUNCTIONS                 	   //
/////////////////////////////////////////////////////////

struct VS_INPUT_POST
{
	float3 pos		: POSITION;
	float2 txcoord	: TEXCOORD0;
};
struct VS_OUTPUT_POST
{
	float4 pos		: SV_POSITION;
	float2 txcoord0	: TEXCOORD0;
};

#if (USE_DEBAND == 1)

float rand(float2 pos)
{
	return frac(sin(dot(pos, float2(12.9898, 78.233))) * 43758.5453);
}

bool is_within_threshold(float3 original, float3 other)
{
		
		
	return !any(max(abs(original - other) - DEBAND_THRESHOLD, float3(0.0, 0.0, 0.0))).x;
}

float4 Debandpass(float4 inColor, float2 texcoord)
{
	float RFX_ScreenSize=ScreenSize.x * ScreenSize.w;
    float2 step = ScreenSize.y * DEBAND_RADIUS;
    float2 halfstep = step * 0.5;

    //Compute additional sample positions
    float2 seed = texcoord + Timer.w;
	#if (DEBAND_OFFSET_MODE == 1)
		float2 offset = float2(rand(seed), 0.0);
	#elif (DEBAND_OFFSET_MODE == 2)
		float2 offset = float2(rand(seed).xx);
	#elif (DEBAND_OFFSET_MODE == 3)
		float2 offset = float2(rand(seed), rand(seed + float2(0.1, 0.2)));
	#endif

    float2 on[8] = {
        float2( offset.x,  offset.y) * step,
        float2( offset.y, -offset.x) * step,
        float2(-offset.x, -offset.y) * step,
        float2(-offset.y,  offset.x) * step,
        float2( offset.x,  offset.y) * halfstep,
        float2( offset.y, -offset.x) * halfstep,
        float2(-offset.x, -offset.y) * halfstep,
        float2(-offset.y,  offset.x) * halfstep,
        };

    float3 col0 = TextureColor.Sample(Sampler1, texcoord).rgb;
    float4 accu = float4(col0, 1.0);

    for (int i = 0; i < DEBAND_SAMPLE_COUNT; i++)
    {
        float4 cn = float4(TextureColor.Sample(Sampler1, texcoord + on[i]).rgb, 1.0);
		#if (DEBAND_SKIP_THRESHOLD_TEST == 0)
			if (is_within_threshold(col0, cn.rgb))
		#endif
		accu += cn;
    }

    accu.rgb /= accu.a;

    //Boost to make it easier to inspect the effect's output
    if (DEBAND_OUTPUT_OFFSET != 0.0 || DEBAND_OUTPUT_BOOST != 1.0)
	{
		accu.rgb -= DEBAND_OUTPUT_OFFSET;
		accu.rgb *= DEBAND_OUTPUT_BOOST;
	}
	
	//Additional dithering
	#if (DEBAND_DITHERING == 1)
		//Ordered dithering
		float dither_bit  = 8.0;
		float grid_position = frac( dot(texcoord,(RFX_ScreenSize * float2(1.0/16.0,10.0/36.0))) + 0.25 );
		float dither_shift = (0.25) * (1.0 / (pow(2,dither_bit) - 1.0));
		float3 dither_shift_RGB = float3(dither_shift, -dither_shift, dither_shift);
		dither_shift_RGB = lerp(2.0 * dither_shift_RGB, -2.0 * dither_shift_RGB, grid_position);
		accu.rgb += dither_shift_RGB;
	#elif (DEBAND_DITHERING == 2)
		//Random dithering
		float dither_bit  = 8.0;
		float sine = sin(dot(texcoord, float2(12.9898,78.233)));
		float noise = frac(sine * 43758.5453 + texcoord.x);
		float dither_shift = (1.0 / (pow(2,dither_bit) - 1.0));
		float dither_shift_half = (dither_shift * 0.5);
		dither_shift = dither_shift * noise - dither_shift_half;
		accu.rgb += float3(-dither_shift, dither_shift, -dither_shift);
	#elif (DEBAND_DITHERING == 3)
		//Iestyn's RGB dither (7 asm instructions) from Portal 2 X360, slightly modified for VR
		//float3 vDither = dot(float2(171.0, 231.0), texcoord * RFX_ScreenSize + RFX_Timer).xxx; //Dynamic dither pattern
		float3 vDither = dot(float2(171.0, 231.0), texcoord * RFX_ScreenSize).xxx;
		vDither.rgb = frac( vDither.rgb / float3( 103.0, 71.0, 97.0 ) ) - float3(0.5, 0.5, 0.5);
		accu.rgb += (vDither.rgb / 255.0);
	#endif
	
	return saturate(accu);
}
#endif

float3	FuncBlur(Texture2D inputtex, float2 uvsrc, float srcsize, float destsize)
{
	const float	scale=4.0; //blurring range, samples count (performance) is factor of scale*scale
	//const float	srcsize=1024.0; //in current example just blur input texture of 1024*1024 size
	//const float	destsize=1024.0; //for last stage render target must be always 1024*1024

	float2	invtargetsize=scale/srcsize;
	invtargetsize.y*=ScreenSize.z; //correct by aspect ratio


	float2	fstepcount;
	fstepcount=srcsize;

	fstepcount*=invtargetsize;
	fstepcount=min(fstepcount, 16.0);
	fstepcount=max(fstepcount, 2.0);

	int	stepcountX=(int)(fstepcount.x+0.4999);
	int	stepcountY=(int)(fstepcount.y+0.4999);

	fstepcount=1.0/fstepcount;
	float4	curr=0.0;
	curr.w=0.000001;
	float2	pos;
	float2	halfstep=0.5*fstepcount.xy;
	pos.x=-0.5+halfstep.x;
	invtargetsize *= 2.0;
	for (int x=0; x<stepcountX; x++)
	{
		pos.y=-0.5+halfstep.y;
		for (int y=0; y<stepcountY; y++)
		{
			float2	coord=pos.xy * invtargetsize + uvsrc.xy;
			float3	tempcurr=inputtex.Sample(Sampler1, coord.xy).xyz;
			float	tempweight;
			float2	dpos=pos.xy*2.0;
			float	rangefactor=dot(dpos.xy, dpos.xy);
			//loosing many pixels here, don't program such unefficient cycle yourself!
			tempweight=saturate(1001.0 - 1000.0*rangefactor);//arithmetic version to cut circle from square
			tempweight*=saturate(1.0 - rangefactor); //softness, without it bloom looks like bokeh dof
			curr.xyz+=tempcurr.xyz * tempweight;
			curr.w+=tempweight;

			pos.y+=fstepcount.y;
		}
		pos.x+=fstepcount.x;
	}
	curr.xyz/=curr.w;

	//curr.xyz=inputtex.Sample(Sampler1, uvsrc.xy);

	return curr.xyz;
}

/////////////////////////////////////////////////////////
//               	  PIXEL SHADER                     //
/////////////////////////////////////////////////////////

VS_OUTPUT_POST	VS_Quad(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST	OUT;
	float4	pos;
	pos.xyz=IN.pos.xyz;
	pos.w=1.0;
	OUT.pos=pos;
	OUT.txcoord0.xy=IN.txcoord.xy;
	return OUT;
}

//draw in several passes to different render targets
float4	PS_Resize(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
	uniform Texture2D inputtex, uniform float srcsize, uniform float destsize) : SV_Target
{
	float4	res;

	res.xyz=FuncBlur(inputtex, IN.txcoord0.xy, srcsize, destsize);

	res=max(res, 0.0);
	res=min(res, 16384.0);

	res.w=1.0;
	return res;
}


float4	PS_ComputeLens1(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
	uniform Texture2D inputtex, uniform float srcsize) : SV_Target
{
	float4	res;
	float4	color;
	float2	coord;

	color=0.0;

	float	weight=0.000001;
	float	scale=0.375;
	float	step=1.0/16.0;
	float2	pos;
	pos.y=0.0;
	pos.x=-1.0;
	for (int i=0; i<33; i++)
	{
		float	tempweight;
		coord.xy=pos.xy*scale;
		coord.xy+=IN.txcoord0.xy;
		float3	tempcolor=inputtex.Sample(Sampler1, coord.xy);
		tempweight=1.05-abs(pos.x);
		tempweight*=1.0-saturate(abs(coord.x*32.0-16.0)-16.0);//clamp outside of screen
		tempweight*=tempweight;
		color.xyz+=tempcolor.xyz * tempweight;
		weight+=tempweight;

		pos.x+=step;
	}
	color.xyz/=weight;

	res.xyz=color;

	res.w=1.0;
	return res;
}


float4	PS_ComputeLens2(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
	uniform Texture2D inputtex, uniform float srcsize) : SV_Target
{
	float4	res;
	float4	color;
	float2	coord;

	color=0.0;

	float	weight=0.000001;
	float	scale=1.0/96.0;
	float	step=1.0/4.0;
	float2	pos;
	pos.y=0.0;
	pos.x=-1.0;
	for (int i=0; i<9; i++)
	{
		float	tempweight;
		coord.xy=pos.xy*scale;
		coord.xy+=IN.txcoord0.xy;
		float3	tempcolor=inputtex.Sample(Sampler1, coord.xy);
		tempweight=1.0;
		//tempweight=1.05-abs(pos.x);
		color.xyz+=tempcolor.xyz * tempweight;
		weight+=tempweight;

		pos.x+=step;
	}
	color.xyz/=weight;

	res.xyz=color;

	res.w=1.0;
	return res;
}

//output is screen size
float4	PS_LensMix(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4	res;
	float4	color;

	color=RenderTarget512.Sample(Sampler1, IN.txcoord0.xy);

	float3	colorfilter=EColorFilter;
	float	intensity=dot(color.xyz, colorfilter);
	intensity=pow(intensity, EContrast);

	res.xyz=intensity * EColorFilter;

	res=max(res, 0.0);
	res=min(res, 16384.0);

	res.w=1.0;
	return res;
}

//version from skyrim sample lens file
float4	PS_DrawSkyrimLens(VS_OUTPUT_POST IN, float4 v0 : SV_Position0,
	uniform Texture2D inputtex, uniform float srcsize, uniform float destsize) : SV_Target
{
	float4	LensParameters;
	LensParameters.x= lerp(lerp(ELensReflectionIntensityNight, ELensReflectionIntensityDay, ENightDayFactor), ELensReflectionIntensityInt, EInteriorFactor);
	LensParameters.y=ELensReflectionPowerA;
	LensParameters.z=ELensDirtIntensityA;
	LensParameters.w=ELensDirtPowerA;

	float   timeweight=0.000001;
	float   timevalue=0.0;
	
	float4	res=0.0;

	float2	coord;
	//deepness, curvature, inverse size
	const float3 offset[4]=
	{
		float3(1.6, 4.0, 1.0),
		float3(0.7, 0.25, 2.0),
		float3(0.3, 1.5, 0.5),
		float3(-0.5, 1.0, 1.0)
	};
	//color filter per reflection
	const float3 factors[4]=
	{
		float3(0.3, 0.4, 0.4),
		float3(0.2, 0.4, 0.5),
		float3(0.5, 0.3, 0.7),
		float3(0.1, 0.2, 0.7)
	};

	
	
	for (int i=0; i<4; i++)
	{
		float2	distfact=(IN.txcoord0.xy-0.5);
		coord.xy=offset[i].x*distfact;
		coord.xy*=pow(2.0*length(float2(distfact.x*ScreenSize.z,distfact.y)), offset[i].y);
		coord.xy*=offset[i].z;
		coord.xy=0.5-coord.xy;//v1
		coord.xy=IN.txcoord0.xy-coord.xy;//v2
		float3	templens=inputtex.Sample(Sampler1, coord.xy);
		float4  maskD=LensMaskTextureD.Sample(Sampler1, coord.xy);
		float4  maskN=LensMaskTextureN.Sample(Sampler1, coord.xy);
		float4  maskI=LensMaskTextureI.Sample(Sampler1, coord.xy);
	
	float4  mask=lerp(lerp(maskN, maskD, ENightDayFactor), maskI, EInteriorFactor);
		templens=templens*factors[i];

        res.xyz=saturate(res.xyz);
        res.xyz+=mask.xyz * saturate(templens.xyz);
		distfact=(coord.xy-0.5);
		distfact*=2.0;
		templens*=saturate(1.0-dot(distfact,distfact));//limit by uv 0..1
		float	maxlens=max(templens.x, max(templens.y, templens.z));
		float	tempnor=(maxlens/(1.0+maxlens));
		tempnor=pow(tempnor, LensParameters.y);
		templens.xyz*=tempnor* LensParameters.z;;

		res.xyz+=templens;
	}
	res.xyz*=0.25*LensParameters.x;


	//add mask
	{
		coord=IN.txcoord0.xy;
//      coord.y*=ScreenSize.w;//remove stretching of image
		float4  maskD=LensMaskTextureD.Sample(Sampler1, coord.xy);
		float4  maskN=LensMaskTextureN.Sample(Sampler1, coord.xy);
		float4  maskI=LensMaskTextureI.Sample(Sampler1, coord.xy);
	
	float4  mask=lerp(lerp(maskN, maskD, ENightDayFactor), maskI, EInteriorFactor);
      float3   templens=RenderTarget128.Sample(Sampler1, IN.txcoord0.xy);
      float   maxlens=max(templens.x, max(templens.y, templens.z));
      float   tempnor=(maxlens/(1.0+maxlens));
      tempnor=pow(tempnor, LensParameters.w);
      templens.xyz*=tempnor * LensParameters.z;
      res.xyz=saturate(res.xyz);
      res.xyz+=mask.xyz * saturate(templens.xyz);
	}
	
	return res;
}


float4	PS_MixSkyrimLens(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4	res;
	float4	color;

	res=RenderTarget512.Sample(Sampler1, IN.txcoord0.xy);
	
	res=max(res, 0.0);
	res=min(res, 16384.0);

	
	res.w=1.0;
	return res;
}

float4 PS_LightLeak(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float4 res;
	
	float   timeweight=0.000001;
	float   timevalue=0.0;
	
	res = TextureColor.Sample(Sampler1, IN.txcoord0.xy);
	float3	bloom=TextureBloom.Sample(Sampler1, IN.txcoord0.xy);
	
	res = Debandpass(res, IN.txcoord0.xy);
	
	//fLensdirtIntensity
	
	float	todfLensdirtIntensity=lerp(lerp(fLensdirtIntensityNight, fLensdirtIntensityDay, ENightDayFactor), fLensdirtIntensityInterior, EInteriorFactor);
						  
	float lensdirtmult = res;
	float3 dirttexD = TextureDirtD.Sample(SamplerDirt, IN.txcoord0.xy);
	float3 dirttexN = TextureDirtN.Sample(SamplerDirt, IN.txcoord0.xy);
	float3 dirttexI = TextureDirtI.Sample(SamplerDirt, IN.txcoord0.xy);
		
	//Texture TOD interpolation
	
	timevalue+=TimeOfDay1.x * dirttexD;
	timevalue+=TimeOfDay1.y * dirttexI;
	timevalue+=TimeOfDay1.z * dirttexD;
	timevalue+=TimeOfDay1.w * dirttexI;
	timevalue+=TimeOfDay2.x * dirttexN;
	timevalue+=TimeOfDay2.y * dirttexN;

	timeweight+=TimeOfDay1.x;
	timeweight+=TimeOfDay1.y;
	timeweight+=TimeOfDay1.z;
	timeweight+=TimeOfDay1.w;
	timeweight+=TimeOfDay2.x;
	timeweight+=TimeOfDay2.y;
	
	float3 dirttex=lerp( (timevalue / timeweight), dirttexI, EInteriorFactor );		
						
	//float3 dirttex =lerp(lerp(dirttexN, dirttexD, ENightDayFactor), dirttexI, EInteriorFactor);
	float3 lensdirt = (dirttex.xyz*lensdirtmult)*todfLensdirtIntensity;
						
	float   tempnor=(lensdirtmult/(1.0+lensdirtmult));
	tempnor=pow(tempnor, res.x);
	lensdirtmult*=tempnor * res.z;
	res.xyz=saturate(res.xyz);
						
	lensdirt = lerp(dot(lensdirt.xyz,0.333), lensdirt.xyz, fLensdirtSaturation);
	
	if(iLensdirtMixmode == 1) res.xyz = res.xyz + lensdirt.xyz;
	if(iLensdirtMixmode == 2) res.xyz = 1-(1-res.xyz)*(1-lensdirt.xyz);
	if(iLensdirtMixmode == 3) res.xyz = max(0.0f,max(res.xyz,lerp(res.xyz,(1.0f - (1.0f - saturate(lensdirt.xyz)) *(1.0f - saturate(lensdirt.xyz * 1.0))),1.0)));
	if(iLensdirtMixmode == 4) res.xyz = max(res.xyz, lensdirt.xyz);   

	return res;
}

/////////////////////////////////////////////////////////
//               	    TECHNIQUES                     //
/////////////////////////////////////////////////////////

technique11 SkyrimLens <string UIName="PRT.X: Light Leak"; string RenderTarget="RenderTarget512";>
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
		SetPixelShader(CompileShader(ps_5_0, PS_Resize(TextureDownsampled, 1024.0, 512.0)));
	}
}

technique11 SkyrimLens1 <string RenderTarget="RenderTarget256";>
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
		SetPixelShader(CompileShader(ps_5_0, PS_Resize(RenderTarget512, 512.0, 256.0)));
	}
}

technique11 SkyrimLens2 <string RenderTarget="RenderTarget128";>
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
		SetPixelShader(CompileShader(ps_5_0, PS_Resize(RenderTarget256, 256.0, 128.0)));
	}
}

technique11 SkyrimLens3 <string RenderTarget="RenderTarget512";>
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
		SetPixelShader(CompileShader(ps_5_0, PS_DrawSkyrimLens(RenderTarget256, 256.0, 512.0)));
	}
}

technique11 SkyrimLens4
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
		SetPixelShader(CompileShader(ps_5_0, PS_MixSkyrimLens()));
	}
}

technique11 SkyrimLens5
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Quad()));
		SetPixelShader(CompileShader(ps_5_0, PS_LightLeak()));
	}
}
