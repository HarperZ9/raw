//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// ENBSeries Skyrim SE dx11 sm5 effect file
// visit facebook.com/MartyMcModding for news/updates
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Advanced Depth of Field & SSAO 2.1f by Marty McFly
// Do not redistribute without credits!
// Private for testing.
// Configuration by:
// Copyright (c) 2008-2017 Marty McFly
// DO NOT REMOVE/REPLACE THIS HEADER.
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//As long as ENBSeries for SSE does not have Boris' SSAO/SSIL, this feature should
//serve as a replacer for the ingame AO.
// If you intend to use AO + DOF or AO only, set it to 1.
//If you don't use AO, set it to 0, to save some compilation time (ENB must compile all 3 techniques)
#define ENABLE_AO_TECHNIQUES            0                 //[0 or 1]

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//Non-UI vars. Some could be made changeable in realtime, at the cost
//of performance.
//Use APPLY EFFECTS in enbseries.ini window if changes do not apply.
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//Enables bokeh shape weight bias and shifts color to the shape borders.
#define bADOF_ShapeWeightEnable 	1	          //[0 or 1]
//------------------------------------------------------------------
//Enables chromatic aberration at bokeh shape borders.
#define bADOF_ShapeChromaEnable 	1	          //[0 or 1]
//------------------------------------------------------------------
//Switches through the possible R G B shifts.
#define iADOF_ShapeChromaOrder 		5	          //[1 to 6]
//------------------------------------------------------------------
//0 : per bokeh sample (very slow. high quality)
//1 : derivative based (fast, maybe artifacts)
#define iADOF_ShapeChromaMode           0                 //[0 or 1]
//------------------------------------------------------------------

//------------------------------------------------------------------
//Blur masking prevents focus pixels from polluting bokeh with their own color.
//0: AMD modified | 1: Marty McFly '16 | 2 : Marty McFly '17
#define iADOF_MaskMode                  2                //[0 to 2]
//------------------------------------------------------------------
//0: blur radius factor of screen size
//1: blur radius factor of pixel size, this means the perceived blur
//   radius differs in diffferent DSR resolutions because the pixel size
//   of these resolutions doesn't match the pixel size of your screen.
#define bADOF_BlurDependingOnPixelSize  0                 //[0 or 1]
//------------------------------------------------------------------
//Because of hysteresis, AF adjusts faster from near>far than far>near.
//This tries to compensate that behaviour but can't be correct for reasons.
#define bADOF_AutofocusSmoothingEnable  1                 //[0 or 1]
//------------------------------------------------------------------
//0 : bokeh blur is smoothed by post gaussian, blurry
//1 : bokeh blur runs a second time with quality 2, looks like using
//    3x Shape Quality. Less performance than gaussian BUT allows using
//    a much lower shape quality for the main bokeh, gaining fps again.
//    May have artifacts under some circumstances.
#define iADOF_PostBlurMode              0                 //[0 or 1]
//------------------------------------------------------------------

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//UI vars, nothing to edit for standard users below this point
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#define bADOF_AutofocusEnable 1
#define fADOF_AutofocusCenter float2(0.5,0.455)
#define	iADOF_AutofocusSamples 5
#define	fADOF_AutofocusRadius 0.01
#define	fADOF_ManualfocusDepth 0.05

#define fADOF_NearBlurCurve bAperture/2
#define fADOF_FarBlurCurve bAperture
#define	fADOF_NearBlurMult 1.0
#define	fADOF_FarBlurMult 1.0
#define	fADOF_InfiniteFocus 0.03
#define bADOF_ShapePreviewWindowEnable false
#define	iADOF_ShapeVertices 6
#define	fADOF_ShapeCurvatureAmount	0.65
#define	fADOF_ShapeRotation 0
#define	fADOF_ShapeWeightCurve		10.0
#define	fADOF_ShapeWeightAmount		0.985
#define	fADOF_ShapeChromaAmount		0.05

#if(ENABLE_AO_TECHNIQUES != 0)
 float	fMXAOSampleRadius	        < string UIName="AO: Sample Radius";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=10.0;	> = {2.5};
 int	iMXAOSampleCount		< string UIName="AO: Sample Count";		        string UIWidget="spinner";				int UIMin=4;		int UIMax=128;		> = {16};
 float	fMXAONormalBias		        < string UIName="AO: Normal Map Bias";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.2};
 float	fMXAOAmbientOcclusionPower	< string UIName="AO: Curve";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.05;	float UIMax=5.0;	> = {1.0};
 float	fMXAOAmbientOcclusionAmount	< string UIName="AO: Amount";		                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {2.0};
 float	fMXAOAmbientOcclusionLuminance	< string UIName="AO: Highlight Preservation";	        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=2.0;	> = {1.0};
 int	iMXAOBlurSteps		        < string UIName="AO: Blur Steps";		        string UIWidget="spinner";				int UIMin=0;		int UIMax=5;		> = {2};
 float	fMXAOBlurSharpness	        < string UIName="AO: Blur Sharpness";		        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=5.0;	> = {1.0};
 float	fMXAOFadeStart	                < string UIName="AO: Fade Start";	                string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.05};
 float	fMXAOFadeEnd	                < string UIName="AO: Fade End";	                        string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.25};
 bool   bMXAOShowAO 		        < string UIName="AO: Show raw AO (use >AO only< technique)";													> = {false};
#endif

#define	fPrepass_SharpScale		0.0

bool	bAutoFocus			<string UIName="Manual Focus (Click on Focus Point)";> = {false};
float bQuality <string UIName="DOF Quality (Resolution Scale)";string UIWidget="Spinner";float UIStep=0.05;float UIMin=0.25;float UIMax=1.00;> = {0.50};
#define fADOF_RenderResolutionMult bQuality
#define iADOF_ShapeQuality ((bQuality * 4) + 4)
#define fADOF_SmootheningAmount ((bQuality * 0.25) - 0.25)
float bRatio <string UIName="Lens Type";string UIWidget="Spinner";float UIStep=1.0;float UIMin=1.0;float UIMax=3.0;> = {3.0};
float	LensInfo1		< string UIName="   1 = Anamorphic";	string UIWidget="Spinner";	float UIStep=0.0;	float UIMin=-0.0;	float UIMax=0.0;	> = {0.0};
float	LensInfo2		< string UIName="   2 = Anamorphic Wider";	string UIWidget="Spinner";	float UIStep=0.0;	float UIMin=-0.0;	float UIMax=0.0;	> = {0.0};
float	LensInfo3		< string UIName="   3 = Standard Hexagonal";	string UIWidget="Spinner";	float UIStep=0.0;	float UIMin=-0.0;	float UIMax=0.0;	> = {0.0};
float	bAperture_Set  < string UIName="Aperture                                                     f/ ";  string UIWidget="Spinner";  float UIStep=0.1;  float UIMin=0.7; float UIMax=3.6; > = {2.8};
#define bAperture ((bAperture_Set + 1) * 0.45)
float	fADOF_ShapeRadius_set	<string UIName="Fine Tune Bokeh Size";string UIWidget="Spinner";float UIStep=0.1;float UIMin=-10.0;float UIMax=10.0;> = {0.0};
#define fADOF_ShapeRadius ((fADOF_ShapeRadius_set + 12.0) - ((bAperture_Set * bAperture_Set) * 1))
float	fADOF_BokehCurve	<string UIName="Bokeh Intensity";string UIWidget="Spinner";float UIStep=0.1;float UIMin=0.1;float UIMax=12.0;> = {3.0};
float	THICC_START		< string UIName="+++++++++++++++++++++++++++++++";	string UIWidget="Spinner";	float UIStep=0.0;	float UIMin=-0.0;	float UIMax=0.0;	> = {0.0};
bool    THICC 		< string UIName="THICC MODE";															> = {false};
 int	HOWTHICC		        < string UIName="HOW THICC YOU WANT IT?";		        string UIWidget="spinner";				int UIMin=2;		int UIMax=20;		> = {2};
#define THICCHELPER (THICC * HOWTHICC)
#define fADOF_ShapeAnamorphRatio (((bRatio - 1) + THICCHELPER) * 0.33) + 0.34

float	DOF_Empty1		< string UIName="+++++++++++++++++++++++++++++";	string UIWidget="Spinner";	float UIStep=0.0;	float UIMin=-0.0;	float UIMax=0.0;	> = {0.0};
bool    iADOF_FocusMode 		< string UIName="Tilt Shift Mode";															> = {false};
float	fADOF_TiltShiftPosition		< string UIName="Axis Position";	string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=-1.0;	float UIMax=1.0;	> = {0.0};
float	fADOF_TiltShiftWidth		< string UIName="Focus Width";		string UIWidget="Spinner";	float UIStep=0.01;	float UIMin=0.0;	float UIMax=1.0;	> = {0.0};
float	fADOF_TiltShiftRotation		< string UIName="Lens Rotation (\xB0)"; string UIWidget="Spinner";	float UIStep=1.00;	float UIMin=0.0;	float UIMax=180.0;	> = {0.0};
#define fADOF_TiltShiftBlurCurve (bAperture - 0.4)

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//redefine disabled UI vars
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#if (bADOF_ShapeChromaEnable == 0)
 #define fADOF_ShapeChromaAmount 0.0
#endif

#if(iADOF_PostBlurMode == 1)
 #define fADOF_SmootheningAmount 0.0
#endif

#if(ENABLE_AO_TECHNIQUES == 0)
 #define fMXAOSampleRadius 3.0
 #define iMXAOSampleCount 3
 #define fMXAONormalBias 0.2
 #define fMXAOAmbientOcclusionPower 0.0
 #define fMXAOAmbientOcclusionAmount 0.0
 #define fMXAOAmbientOcclusionLuminance 0.0
 #define iMXAOBlurSteps 2
 #define fMXAOBlurSharpness 1.0
 #define fMXAOFadeStart 0.0
 #define fMXAOFadeEnd 0.001
 #define bMXAOShowAO 0
#endif

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//global shader file parameters, do not modify
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

#define PixelSize 		float2(ScreenSize.y,ScreenSize.y*ScreenSize.z)
uniform float4 DepthParameters  = float4(1.0,3000.0,-2999.0f,0.0);//x = near plane, y = far plane, z = -(y-x), w = unused

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//external enb parameters, do not modify
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4	Timer; 			//x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4	ScreenSize; 		//x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
float	AdaptiveQuality;	//changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4	Weather;		//x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours. Weather index is value from weather ini file, for example WEATHER002 means index==2, but index==0 means that weather not captured.
float4	TimeOfDay1;		//x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay2;		//x = dusk, y = night. Interpolators range from 0..1
float	ENightDayFactor;	//changes in range 0..1, 0 means that night time, 1 - day time
float	EInteriorFactor;	//changes 0 or 1. 0 means that exterior, 1 - interior

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//external enb debugging parameters for shader programmers, do not modify
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4	tempF1; 		//0,1,2,3
float4  tempF2;                 //5,6,7,8
float4	tempF3; 		//9,0
float4	tempInfo1; 		//float4(cursorpos.xy 0~1,isshaderwindowopen, mouse buttons)
float4	tempInfo2;		//float4(cursorpos.xy prev left mouse button click, cursorpos.xy prev right mouse button click)

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//mod parameters, do not modify
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4			DofParameters;		//z = ApertureTime multiplied by time elapsed, w = FocusingTime multiplied by time elapsed
Texture2D		TextureCurrent; 	//current frame focus depth or aperture. unused in dof computation
Texture2D		TexturePrevious; 	//previous frame focus depth or aperture. unused in dof computation
Texture2D		TextureOriginal; 	//color R16B16G16A16 64 bit hdr format
Texture2D		TextureColor; 		//color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format
Texture2D		TextureDepth; 		//scene depth R32F 32 bit hdr format
Texture2D		TextureFocus; 		//this frame focus 1*1 R32F hdr red channel only. computed in PS_Focus
Texture2D		TextureAperture; 	//this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture
Texture2D		TextureAdaptation;	//previous frame vanilla or enb adaptation 1*1 R32F hdr red channel only. adaptation computed after depth of field and it's kinda "average" brightness of screen!!!
//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D		RenderTargetRGBA32; 	//R8G8B8A8 32 bit ldr format
Texture2D		RenderTargetRGBA64; 	//R16B16G16A16 64 bit ldr format
Texture2D		RenderTargetRGBA64F; 	//R16B16G16A16F 64 bit hdr format
Texture2D		RenderTargetR16F; 	//R16F 16 bit hdr format with red channel only
Texture2D		RenderTargetR32F; 	//R32F 32 bit hdr format with red channel only
Texture2D		RenderTargetRGB32F; 	//32 bit hdr format without alpha

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

SamplerState		Sampler2
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Wrap;
	AddressV = Wrap;
};

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Vertex Shader
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

void VS_DOF(in float3 inpos : POSITION, inout float2 txcoord0 : TEXCOORD0, out float4 outpos : SV_POSITION)
{
	outpos = float4(inpos.xyz,1.0);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Functions
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float GetLinearDepth(float2 coords)
{
	float depth = TextureDepth.SampleLevel(Sampler1, coords.xy,0).x;
	depth *= rcp(DepthParameters.y + depth * DepthParameters.z);
	return depth;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float3 RemoveFireflies(Texture2D inputtex, float2 texcoord)
{
	float3 blockTL = 0, blockTR = 0, blockBR = 0, blockBL = 0, blockCC = 0;
	float3 tex;

	//unrolled for parallelization. Looks ugly, runs fastest.
	tex = inputtex.Sample(Sampler1, texcoord.xy + float2(-1, -1) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockTL += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2( 0, -1) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockTL += tex; blockTR += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2( 1, -1) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockTR += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2(-1,  0) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockTL += tex; blockBL += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2( 0,  0) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockTL += tex; blockTR += tex; blockBR += tex; blockBL += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2( 1,  0) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockTR += tex; blockBR += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2(-1,  1) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockBL += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2( 0,  1) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockBL += tex; blockBR += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2( 1,  1) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockBR += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2(-0.5, -0.5) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockCC += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2( 0.5, -0.5) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockCC += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2( 0.5,  0.5) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockCC += tex;

	tex = inputtex.Sample(Sampler1, texcoord.xy + float2(-0.5,  0.5) * PixelSize).rgb;
	tex /= 1.0 + max(tex.x,max(tex.y,tex.z));
	blockCC += tex;

	blockTL /= 4.0; blockTR /= 4.0; blockBR /= 4.0; blockBL /= 4.0; blockCC /= 4.0;

	blockTL /= (1 - max(blockTL.x,max(blockTL.y,blockTL.z)));
	blockTR /= (1 - max(blockTR.x,max(blockTR.y,blockTR.z)));
	blockBR /= (1 - max(blockBR.x,max(blockBR.y,blockBR.z)));
	blockBL /= (1 - max(blockBL.x,max(blockBL.y,blockBL.z)));
	blockCC /= (1 - max(blockCC.x,max(blockCC.y,blockCC.z)));

	return 0.5 * blockCC + 0.125 * (blockTL + blockTR + blockBR + blockBL);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float GetCoC(float2 texcoord)
{
        float   scenecoc 	= 0.0;

        if(iADOF_FocusMode == false)
        {
                float	scenedepth	= GetLinearDepth(texcoord.xy);
                float   scenedepthsample[4];
                static const float   bleedbias = 0.66; //this greatly improves leak reduction
                scenedepthsample[0] = GetLinearDepth(texcoord.xy + float2(PixelSize.x*bleedbias,0.0));
                scenedepthsample[1] = GetLinearDepth(texcoord.xy - float2(PixelSize.x*bleedbias,0.0));
                scenedepthsample[2] = GetLinearDepth(texcoord.xy + float2(0.0,PixelSize.y*bleedbias));
                scenedepthsample[3] = GetLinearDepth(texcoord.xy - float2(0.0,PixelSize.y*bleedbias));
                scenedepth = min(scenedepth,scenedepthsample[0]);
                scenedepth = min(scenedepth,scenedepthsample[1]);
                scenedepth = min(scenedepth,scenedepthsample[2]);
                scenedepth = min(scenedepth,scenedepthsample[3]);

        	float	scenefocus	= TextureFocus.Sample(Sampler0, texcoord.xy).x;

                //scenefocus = saturate(scenefocus/fADOF_InfiniteFocus); //done in focus pass
                scenedepth = saturate(scenedepth/fADOF_InfiniteFocus);

	        float farBlurDepth = scenefocus*pow(4.0,fADOF_FarBlurCurve);

                if(scenedepth < scenefocus)
	        {
                        scenecoc = scenedepth*rcp(scenefocus) + (-1.0);
                        // Disable near blur when using autofocus
                        if(bAutoFocus == false)
                        {
                    		  scenecoc *= 0.0;
                        }
                        else
                        {
                          scenecoc *= fADOF_NearBlurMult;
                        }
	        }
	        else
	        {
                        scenecoc=scenedepth*(1.0/(farBlurDepth-scenefocus)) + (-scenefocus/(farBlurDepth-scenefocus));
	        	scenecoc *= fADOF_FarBlurMult;
	        	scenecoc=saturate(scenecoc);
	        }
	        scenecoc = (scenedepth < 0.00000001) ? 0.0 : scenecoc; //first person models, that epsilon is handpicked, do not change
          }
          else
          {
                float2 ncoord = texcoord.xy * 2.0 - 1.0;
                float2 matrixVector;
                sincos(fADOF_TiltShiftRotation*0.0174533,matrixVector.x,matrixVector.y);
                float2x2 axisrot = float2x2(matrixVector.y,-matrixVector.x,matrixVector.xy);

                ncoord.xy = mul(ncoord.xy,axisrot);
                ncoord.y += fADOF_TiltShiftPosition;

                scenecoc = abs(ncoord.y);
                scenecoc = saturate((scenecoc - fADOF_TiltShiftWidth) / (1.00001 - fADOF_TiltShiftWidth));
                scenecoc = pow(scenecoc,fADOF_TiltShiftBlurCurve);
        }

	scenecoc = saturate(scenecoc * 0.5 + 0.5);
	return scenecoc;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4 colortexSampleChroma(Texture2D colortex, float2 sourcecoord, float2 offsetcoord, float3 chromaoffsets) //new function
{
	float4 res 		= 0.0;
	float2 Rsample 		= colortex.SampleLevel(Sampler1, sourcecoord.xy + offsetcoord.xy * chromaoffsets.x,0).xw;
	float2 Gsample 		= colortex.SampleLevel(Sampler1, sourcecoord.xy + offsetcoord.xy * chromaoffsets.y,0).yw;
	float2 Bsample 		= colortex.SampleLevel(Sampler1, sourcecoord.xy + offsetcoord.xy * chromaoffsets.z,0).zw;
	res.xyz 		= float3(Rsample.x,Gsample.x,Bsample.x);
	res.w 			= min(Rsample.y,min(Gsample.y,Bsample.y)); //best for masking
	return res;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float3 GetChromaticAberrationOffsets()
{
        float3 chromaoffsets = float3(1.0 - fADOF_ShapeChromaAmount, 1.0, 1.0 + fADOF_ShapeChromaAmount);
        #if(  iADOF_ShapeChromaOrder == 2)
               chromaoffsets.xyz = chromaoffsets.zxy;
        #elif(iADOF_ShapeChromaOrder == 3)
               chromaoffsets.xyz = chromaoffsets.yzx;
        #elif(iADOF_ShapeChromaOrder == 4)
               chromaoffsets.xyz = chromaoffsets.xzy;
        #elif(iADOF_ShapeChromaOrder == 5)
               chromaoffsets.xyz = chromaoffsets.yxz;
        #elif(iADOF_ShapeChromaOrder == 6)
               chromaoffsets.xyz = chromaoffsets.zyx;
        #else
               chromaoffsets.xyz = chromaoffsets.xyz;
        #endif
        return chromaoffsets;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float3 BokehBlur(Texture2D colortex,
                 float2 coords,
                 float discRadius,
                 float centerDepth,
                 int nQuality,
                 int nVertices,
                 float BokehCurve)
{
	float4 res 			= float4(pow(colortex.Sample(Sampler1, coords.xy).xyz,BokehCurve),1.0);
	float ringIncrement		= rcp(nQuality);

        #if(bADOF_BlurDependingOnPixelSize == 1)
                float2 discRadiusScaled	= discRadius*(PixelSize.xy*float2(fADOF_ShapeAnamorphRatio,1.0f))*ringIncrement;
        #else
                float2 discRadiusScaled	= discRadius*float2(fADOF_ShapeAnamorphRatio,ScreenSize.z)*0.0006*ringIncrement;
        #endif

        float2 currentVertex,nextVertex,matrixVector;
	sincos(radians(fADOF_ShapeRotation),currentVertex.y,currentVertex.x);
	sincos(6.2831853 / nVertices,matrixVector.x,matrixVector.y);

	float2x2 rotMatrix = float2x2(matrixVector.y,-matrixVector.x,matrixVector.x,matrixVector.y);

        #if(bADOF_ShapeWeightEnable != 0)
               res.w *= saturate(1.0f-fADOF_ShapeWeightAmount*nQuality);
               res.xyz *= res.w;
	#endif

        #if(bADOF_ShapeChromaEnable != 0 && iADOF_ShapeChromaMode == 0)
                float3 chromaoffsets = GetChromaticAberrationOffsets();
	#endif

	[fastopt]
	for(float iRings = 1; iRings <= nQuality && iRings <= 25; iRings++)
	{
		[fastopt]
		for (int iVertices = 1; iVertices <= nVertices && iVertices <= 9; iVertices++)
		{
			nextVertex = mul(currentVertex.xy, rotMatrix);

			[fastopt]
			for (float iSamplesPerRing = 0; iSamplesPerRing < iRings && iSamplesPerRing < 25; iSamplesPerRing++)
			{
				float2 sampleOffset = lerp(currentVertex,nextVertex,iSamplesPerRing/iRings);
				sampleOffset *= lerp(1.0,rsqrt(dot(sampleOffset,sampleOffset)),fADOF_ShapeCurvatureAmount);

				#if(bADOF_ShapeChromaEnable != 0 && iADOF_ShapeChromaMode == 0)
				        float4 tap = colortexSampleChroma(colortex, coords.xy, (sampleOffset.xy * discRadiusScaled) * iRings, chromaoffsets);
				#else
				        float4 tap = colortex.SampleLevel(Sampler1, coords.xy + (sampleOffset.xy * discRadiusScaled) * iRings,0);
				#endif

                                if(iADOF_FocusMode == false)
                                {
                                        #if(iADOF_MaskMode == 0)
				                float tapcoc = tap.w * 2.0 - 1.0;
				                tap.w = (tap.w >= centerDepth * 0.99) ? 1.0 : (tapcoc*tapcoc)*(tapcoc*tapcoc); //why the brackets, you ask?
                                        #elif(iADOF_MaskMode == 1)
                                                float tapcoc = abs(tap.w*2.0-1.0);
                                                tap.w = (tapcoc*4.0*fADOF_ShapeRadius >= discRadius || tap.w < 0.5) ? 1.0 : 0.0;
                                        #elif(iADOF_MaskMode == 2)
                                                tap.w = (centerDepth > 0.5) ? saturate(abs(tap.w*2.0-1.0)-iRings*ringIncrement*abs(centerDepth*2.0-1.0)) : 1.0; //I believe this is almost perfect.
                                        #endif
                                }
                                else
                                {
                                        tap.w = 1.0;
                                        }


				#if(bADOF_ShapeWeightEnable != 0)
				tap.w *= lerp(1.0,pow(iRings*ringIncrement,fADOF_ShapeWeightCurve),fADOF_ShapeWeightAmount);
				#endif

				res.xyz += pow(tap.xyz,BokehCurve)*tap.w;
				res.w += tap.w;
			}

			currentVertex = nextVertex;
		}
	}
        res.xyz = pow(max(0.0,res.xyz/res.w),rcp(BokehCurve));
	return res.xyz;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//modified version for the post blurring, using max(total,tap) instead of avg
float3 BokehBlurPost(Texture2D colortex,
                 float2 coords,
                 float discRadius,
                 float centerDepth,
                 int nQuality,
                 int nVertices)
{
	float4 res 			= float4(colortex.Sample(Sampler1, coords.xy).xyz,1.0);
	float ringIncrement		= rcp(nQuality);

        #if(bADOF_BlurDependingOnPixelSize == 1)
                float2 discRadiusScaled	= discRadius*(PixelSize.xy*float2(fADOF_ShapeAnamorphRatio,1.0f))*ringIncrement;
        #else
                float2 discRadiusScaled	= discRadius*float2(fADOF_ShapeAnamorphRatio,ScreenSize.z)*0.0006*ringIncrement;
        #endif

        float2 currentVertex,nextVertex,matrixVector;
	sincos(radians(fADOF_ShapeRotation),currentVertex.y,currentVertex.x);
	sincos(6.2831853 / nVertices,matrixVector.x,matrixVector.y);

	float2x2 rotMatrix = float2x2(matrixVector.y,-matrixVector.x,matrixVector.x,matrixVector.y);

	[fastopt]
	for(float iRings = 1; iRings <= nQuality && iRings <= 25; iRings++)
	{
		[fastopt]
		for (int iVertices = 1; iVertices <= nVertices && iVertices <= 9; iVertices++)
		{
			nextVertex = mul(currentVertex.xy, rotMatrix);

			[fastopt]
			for (float iSamplesPerRing = 0; iSamplesPerRing < iRings && iSamplesPerRing < 25; iSamplesPerRing++)
			{
				float2 sampleOffset = lerp(currentVertex,nextVertex,iSamplesPerRing/iRings);
				sampleOffset *= lerp(1.0,rsqrt(dot(sampleOffset,sampleOffset)),fADOF_ShapeCurvatureAmount);

				float4 tap = colortex.SampleLevel(Sampler1, coords.xy + (sampleOffset.xy * discRadiusScaled) * iRings,0);

                                if(iADOF_FocusMode == false)
                                {
                                        float tapcoc = abs(tap.w*2.0-1.0);
                                        tap.xyz *= (tapcoc*2.0*fADOF_ShapeRadius/iADOF_ShapeQuality*0.666 >= discRadius || tap.w < 0.5) ? 1.0 : 0.0; //move out of loop
                                }

				res.xyz = max(res.xyz,tap.xyz);

			}
			currentVertex = nextVertex;
		}
	}
	return res.xyz;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float drawdot(float2 coord, float2 center)
{
        return smoothstep(0.0025,0.000,length((coord-center)*float2(1.0,ScreenSize.w)));
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//modified version for preview window.
float3 BokehBlurPreview(Texture2D colortex, float2 coords, float discRadius, float2 centerofshape)
{
        float4 res 			= float4(drawdot(coords.xy,centerofshape).xxx,1.0); //float4(colortex.Sample(Sampler1, coords.xy).xyz, 1.0f);
	int ringCount			= iADOF_ShapeQuality;
	float ringIncrement		= rcp(ringCount);
	float2 discRadiusInPixels	= discRadius*(PixelSize.xy*float2(fADOF_ShapeAnamorphRatio,1.0f));
  float Alpha                     = 6.2831853 / iADOF_ShapeVertices;

        float2 currentVertex,nextVertex,matrixVector;
	sincos(fADOF_ShapeRotation*0.0174533,currentVertex.y,currentVertex.x);
	sincos(Alpha,matrixVector.x,matrixVector.y);

	float2x2 rotMatrix = float2x2(matrixVector.y,-matrixVector.x,matrixVector.x,matrixVector.y);

        #if(bADOF_ShapeWeightEnable != 0)
               res.w = saturate(1.0f-fADOF_ShapeWeightAmount);
               res.xyz *= res.w;
	#endif

        #if(bADOF_ShapeChromaEnable != 0 && iADOF_ShapeChromaMode == 0)
                float3 chromaoffsets = GetChromaticAberrationOffsets();
                chromaoffsets = clamp(chromaoffsets,0.85,1.15); //let's not fuck up the window
	#endif

	[fastopt]
	for(float iRings = 1; iRings <= ringCount && iRings <= 25; iRings++)
	{
		float radiusCoeff = iRings*ringIncrement;

		[fastopt]
		for (int iVertices = 1; iVertices <= iADOF_ShapeVertices && iVertices <= 9; iVertices++)
		{
			nextVertex = mul(currentVertex.xy, rotMatrix);

			[fastopt]
			for (float iSamplesPerRing = 0; iSamplesPerRing < iRings && iSamplesPerRing < 25; iSamplesPerRing++)
			{
				float2 sampleOffset = lerp(currentVertex,nextVertex,iSamplesPerRing/iRings);
				sampleOffset *= lerp(1.0,rsqrt(dot(sampleOffset,sampleOffset)),fADOF_ShapeCurvatureAmount);

                                float2 sampleCoord = centerofshape + (sampleOffset.xy * discRadiusInPixels) * radiusCoeff;

                                float3 tap = 0.0;

                                #if(bADOF_ShapeChromaEnable != 0  && iADOF_ShapeChromaMode == 0)
                                        tap.x = drawdot(coords.xy,centerofshape + (sampleOffset.xy * discRadiusInPixels) * radiusCoeff*chromaoffsets.x);
                                        tap.y = drawdot(coords.xy,centerofshape + (sampleOffset.xy * discRadiusInPixels) * radiusCoeff*chromaoffsets.y);
                                        tap.z = drawdot(coords.xy,centerofshape + (sampleOffset.xy * discRadiusInPixels) * radiusCoeff*chromaoffsets.z);
                                #else
                                        tap.xyz = drawdot(coords.xy,sampleCoord);
                                #endif

                                #if(bADOF_ShapeWeightEnable != 0)
                                	tap *= lerp(1.0,pow(radiusCoeff,fADOF_ShapeWeightCurve),fADOF_ShapeWeightAmount);
                                #endif

                                res.xyz += tap.xyz;
			}

			currentVertex = nextVertex;
		}
	}
	return saturate(res.xyz);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float3 GetPosition(float2 coords)
{
        return float3(coords.xy*2.0-1.0,1.0)*GetLinearDepth(coords.xy)*DepthParameters.y;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float GetBayerFromCoordLevel(float2 pixelpos, int maxLevel)
{
	float finalBayer = 0.0;

	for(float i = 1-maxLevel; i<= 0; i++)
	{
		float bayerSize = exp2(i);
	        float2 bayerCoord = floor(pixelpos * bayerSize) % 2.0;
		float bayer = 2.0 * bayerCoord.x - 4.0 * bayerCoord.x * bayerCoord.y + 3.0 * bayerCoord.y;
		finalBayer += exp2(2.0*(i+maxLevel))* bayer;
	}

	float finalDivisor = 4.0 * exp2(2.0 * maxLevel)- 4.0;
	return finalBayer/ finalDivisor + 1.0/exp2(2.0 * maxLevel);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float3 GetNormalFromDepth(float2 coords)
{
	float3 offs = float3(PixelSize.xy,0);

	float3 f 	 =       GetPosition(coords.xy);
	float3 d_dx1 	 = - f + GetPosition(coords.xy + offs.xz);
	float3 d_dx2 	 =   f - GetPosition(coords.xy - offs.xz);
	float3 d_dy1 	 = - f + GetPosition(coords.xy + offs.zy);
	float3 d_dy2 	 =   f - GetPosition(coords.xy - offs.zy);

	d_dx1 = lerp(d_dx1, d_dx2, abs(d_dx1.z) > abs(d_dx2.z));
	d_dy1 = lerp(d_dy1, d_dy2, abs(d_dy1.z) > abs(d_dy2.z));

	return normalize(cross(d_dy1,d_dx1));
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float GetBlurWeight(float O, float z, float z0)
{
        float DeltaZ = abs(z-z0) * DepthParameters.y * fMXAOBlurSharpness;
        float DeltaO = O/(iMXAOBlurSteps+1.0);

        return exp2(-0.5*DeltaO*DeltaO-DeltaZ*DeltaZ);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float3 CombineAO(float3 color, float mxao, float2 texcoord, bool outputAO)
{
        mxao    = saturate(1.0 - mxao);
        mxao    = pow(mxao,fMXAOAmbientOcclusionPower);
        mxao    = lerp(mxao, 1.0, (1 - outputAO) * pow(saturate(dot(color.xyz,fMXAOAmbientOcclusionLuminance)),2.0));
        mxao    = lerp(mxao,1.0,smoothstep(fMXAOFadeStart,fMXAOFadeEnd,GetLinearDepth(texcoord.xy)));

        color.xyz = (outputAO) ? mxao : color.xyz * mxao;
        return color;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Pixel Shaders
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4 PS_AO_NormalTexture(float2 texcoord : TEXCOORD0, float4 vpos : SV_POSITION) : SV_Target
{
        int level = round((8*1.4142) * pow(iMXAOSampleCount,-0.7071) + 1.4142);
        float AOjitter = GetBayerFromCoordLevel(vpos.xy,level);
        float3 normal = GetNormalFromDepth(texcoord.xy);
        return float4(normal*0.5+0.5,AOjitter);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float GetMXAO( float2 scaledcoord,
               float3 normal,
               float3 position,
               float nSamples,
               float2 currentVector,
               float fNegInvR2,
               float radiusJitter,
               float sampleRadius)
{
	float AO = 0.0;
        float2 currentOffset = 0.0;

	[loop]
	for(int iSample=0; iSample < nSamples; iSample++)
	{
                currentOffset.xy = scaledcoord.xy + currentVector.xy * (iSample + radiusJitter);
                currentOffset.y *= ScreenSize.z;

		float3 occlVec 		= -position + GetPosition(currentOffset);
		float  occlDistanceRcp 	= rsqrt(dot(occlVec,occlVec));

		AO += saturate(1.0 + fNegInvR2/occlDistanceRcp)  * saturate(dot(occlVec, normal)*occlDistanceRcp - fMXAONormalBias);
                currentVector = mul(currentVector.xy, float2x2(0.575,0.81815,-0.81815,0.575));
	}

	return saturate(fMXAOAmbientOcclusionAmount * AO/(0.15*(1.0-fMXAONormalBias)*nSamples*sqrt(sampleRadius)));
}

float4	PS_AO_Gen(float2 texcoord : TEXCOORD0, float4 vpos : SV_POSITION) : SV_Target
{
        float4 normalSample = RenderTargetRGBA32.Sample(Sampler1,texcoord.xy);
        float3 ScreenSpaceNormals = normalSample.xyz * 2.0 - 1.0;
        float3 ScreenSpacePosition = GetPosition(texcoord.xy);

        float scenedepth = ScreenSpacePosition.z / DepthParameters.y;

        clip(fMXAOFadeEnd-scenedepth);
        ScreenSpacePosition += ScreenSpaceNormals * scenedepth;

        float SampleRadiusScaled  = 0.2*fMXAOSampleRadius*fMXAOSampleRadius / (iMXAOSampleCount * ScreenSpacePosition.z);
        float mipFactor = SampleRadiusScaled * 300.0 * sqrt(iMXAOSampleCount);

        float2 currentVector;
        sincos(2.0*3.14159274*normalSample.w, currentVector.y, currentVector.x);
        static const float fNegInvR2 = -1.0/(fMXAOSampleRadius*fMXAOSampleRadius);
        currentVector *= SampleRadiusScaled;

        texcoord.y /= ScreenSize.z;

        float MXAO =  GetMXAO(texcoord,
                      ScreenSpaceNormals,
                      ScreenSpacePosition,
                      iMXAOSampleCount,
                      currentVector,
                      fNegInvR2,
                      normalSample.w,
                      fMXAOSampleRadius);

        MXAO -= ddx(MXAO)*(fmod(vpos.x-0.5,2.0)-0.5) * 0.666;
        MXAO -= ddy(MXAO)*(fmod(vpos.y-0.5,2.0)-0.5) * 0.666;

        return float4(scenedepth.xxx,MXAO);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4 GetBlurredAO( float2 texcoord, float2 axis, int nSteps)
{
        float4 centersample,tempsample;
        float centerkey,tempkey;
        float2 blurcoord = 0.0;
        float totalweight = 0.5,tempweight;

        centersample = TextureColor.Sample(Sampler1, texcoord.xy);
        centerkey = centersample.x;
        centersample.w *= totalweight;

        [loop]
	for(int orientation=-1;orientation<=1; orientation+=2)
	{
		[loop]
		for(float iStep = 1.0; iStep <= nSteps; iStep++)
		{
			blurcoord.xy 	= (2.0 * iStep - 0.5) * orientation * axis * PixelSize.xy + texcoord.xy;
                        tempsample = TextureColor.SampleLevel(Sampler1, blurcoord.xy,0);
                        float tempkey = tempsample.x;

                        tempweight = GetBlurWeight(iStep, tempkey, centerkey);
                        centersample.w += tempsample.w * tempweight;
                        totalweight += tempweight;
                }
        }

        centersample.w /= totalweight;
        return centersample;
}

float4 PS_AO_Blur1(float2 texcoord : TEXCOORD0) : SV_Target
{
        return GetBlurredAO(texcoord.xy, float2(1.0,0.0), iMXAOBlurSteps);
}

float4 PS_AO_Blur2(float2 texcoord : TEXCOORD0) : SV_Target
{
        return GetBlurredAO(texcoord.xy, float2(0.0,1.0), iMXAOBlurSteps);
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4 PS_AO_Combine(float2 texcoord : TEXCOORD0, uniform bool showAO) : SV_Target
{
	float ao 		= RenderTargetRGBA32.Sample(Sampler1, texcoord.xy).w;
        float4 color            = TextureOriginal.Sample(Sampler1, texcoord.xy);

        color.xyz = CombineAO(color.xyz, ao, texcoord, showAO);

        return color;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//? -> 1x1 R32F
float4	PS_Aperture(float2 texcoord : TEXCOORD0) : SV_Target
{
	//as I don't use aperture and deleting the technique causes weird things to happen, don't waste resources :v
	return 1;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//fullres -> 16x16 R32F
float4	PS_ReadFocus(float2 texcoord : TEXCOORD0) : SV_Target
{
if(iADOF_FocusMode == false)
{
	float scenefocus 	= fADOF_ManualfocusDepth;
	float2 coords 		= 0.0;

	if(bADOF_AutofocusEnable != 0)
	{
		scenefocus =  saturate(GetLinearDepth(fADOF_AutofocusCenter.xy)/fADOF_InfiniteFocus);
		float2 offsetVector = float2(1.0,0.0) * fADOF_AutofocusRadius;
		float Alpha = 6.2831853 / iADOF_AutofocusSamples;
		float2x2 rotMatrix = float2x2(cos(Alpha),-sin(Alpha),sin(Alpha),cos(Alpha));

		for(int i=0; i<iADOF_AutofocusSamples; i++)
		{
			float2 currentOffset = fADOF_AutofocusCenter + offsetVector.xy;
			scenefocus += saturate(GetLinearDepth(currentOffset)/fADOF_InfiniteFocus);
			offsetVector = mul(offsetVector,rotMatrix);
		}

		scenefocus /= iADOF_AutofocusSamples;
	}
  if (bAutoFocus == true)
	{

		scenefocus = GetLinearDepth(tempInfo2.xy);
	scenefocus = saturate(scenefocus)/fADOF_InfiniteFocus;
	}
  else
  {
  	scenefocus = saturate(scenefocus);
  }

	return scenefocus;
}
        return 0.0;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//16x16 -> 1x1 R32F
float4	PS_Focus(float2 texcoord : TEXCOORD0) : SV_Target
{
        if(iADOF_FocusMode == false)
        {
	       float prevFocus = TexturePrevious.Sample(Sampler1, texcoord.xy).x;
	       float currFocus = TextureCurrent.Sample(Sampler1, texcoord.xy).x;

                float res = 0.0f;
                res = lerp(prevFocus,currFocus,DofParameters.w);
                #if(bADOF_AutofocusSmoothingEnable != 0)
                        if(prevFocus < currFocus) res = lerp(prevFocus,currFocus,DofParameters.w*lerp(0.03f,0.35f,saturate(currFocus/prevFocus)) / pow(2.5f,fADOF_FarBlurCurve));
                #endif
	       res = saturate(res);
	       return res;
        }
        return 0.0f;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4	PS_CoC(float2 texcoord : TEXCOORD0, uniform bool applyAO) : SV_Target
{
        float3 color = 1.0;
        #if(ENABLE_AO_TECHNIQUES != 0)
                //color = (applyAO) ? CombineAOIL(RemoveFireflies(TextureOriginal, texcoord).xyz, RenderTargetRGBA32.Sample(Sampler1, texcoord.xy), texcoord.xy, 0)
                //                  : RemoveFireflies(TextureColor, texcoord).xyz;
                color = RemoveFireflies(TextureOriginal, texcoord).xyz;
                color = CombineAO(color, RenderTargetRGBA32.Sample(Sampler1, texcoord.xy).w, texcoord.xy, 0);
        #else
                color = RemoveFireflies(TextureColor, texcoord).xyz;
        #endif

        return float4(color.xyz,GetCoC(texcoord.xy));
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4	PS_DoF_Main(float2 texcoord : TEXCOORD0) : SV_Target
{
	float2 scaledcoord = texcoord.xy / fADOF_RenderResolutionMult;
        clip(!all(saturate(-scaledcoord * scaledcoord + scaledcoord + 0.01)) ? -1:1); //0.01 epsilon to prevent border issues with AO.

	float4 scenecolor = TextureColor.Sample(Sampler1, scaledcoord.xy);

	float centerDepth = scenecolor.w;
	float blurAmount = abs(centerDepth * 2.0 - 1.0);
	float discRadius = blurAmount * fADOF_ShapeRadius;

        if(iADOF_FocusMode == false)
        {
	       discRadius*=(centerDepth < 0.5) ? (1.0 / max(fADOF_NearBlurCurve * 2.0, 1.0)) : 1.0;
        }

        #if(ENABLE_AO_TECHNIQUES == 0)
                //AO remains in those clipped parts and due to TAA (I suppose)
                //it bleeds into color at the end, even though this shouldn't be possible.
                //Visible at night, scattered white pixels at blur-focus transition
                clip((discRadius<1.0)?-1:1);
        #endif

        #if(iADOF_PostBlurMode == 0)
                int ringCount = round(lerp(1.0,(float)iADOF_ShapeQuality,blurAmount));
        #elif(iADOF_PostBlurMode == 1)
                //new, less obvious quality jumps. blur is heavier now but the 2nd bokeh pass instead of Gaussian
                //makes low qualities look higher quality so the user can lower the quality, gaining fps again.
                int ringCount = lerp(1.0, iADOF_ShapeQuality, saturate(0.333 * discRadius / iADOF_ShapeQuality));
        #endif

	scenecolor.xyz = BokehBlur(TextureColor,scaledcoord.xy, discRadius, centerDepth, ringCount, iADOF_ShapeVertices, fADOF_BokehCurve);
	scenecolor.w = centerDepth;

	return scenecolor;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4	PS_DoF_Combine(float2 texcoord : TEXCOORD0, uniform bool applyAO) : SV_Target
{
	float4 blurredcolor 		= TextureColor.Sample(Sampler1, texcoord.xy*fADOF_RenderResolutionMult);
	float4 unblurredcolor		= TextureOriginal.Sample(Sampler1, texcoord.xy);

        //1:1 matso
        unblurredcolor.xyz *= 9.0;
        unblurredcolor.xyz -= TextureOriginal.Sample(Sampler1, texcoord.xy + float2(-PixelSize.x, PixelSize.y) * fPrepass_SharpScale).xyz;
	unblurredcolor.xyz -= TextureOriginal.Sample(Sampler1, texcoord.xy + float2(0.0, PixelSize.y) * fPrepass_SharpScale).xyz;
	unblurredcolor.xyz -= TextureOriginal.Sample(Sampler1, texcoord.xy + float2(PixelSize.x, PixelSize.y) * fPrepass_SharpScale).xyz;
	unblurredcolor.xyz -= TextureOriginal.Sample(Sampler1, texcoord.xy + float2(PixelSize.x, 0.0) * fPrepass_SharpScale).xyz;
	unblurredcolor.xyz -= TextureOriginal.Sample(Sampler1, texcoord.xy + float2(PixelSize.x, -PixelSize.y) * fPrepass_SharpScale).xyz;
	unblurredcolor.xyz -= TextureOriginal.Sample(Sampler1, texcoord.xy + float2(0.0, -PixelSize.y) * fPrepass_SharpScale).xyz;
	unblurredcolor.xyz -= TextureOriginal.Sample(Sampler1, texcoord.xy + float2(-PixelSize.x, -PixelSize.y) * fPrepass_SharpScale).xyz;
	unblurredcolor.xyz -= TextureOriginal.Sample(Sampler1, texcoord.xy + float2(-PixelSize.x, 0.0) * fPrepass_SharpScale).xyz;

	float4 scenecolor		= 0.0;
	float centerDepth		= GetCoC(texcoord.xy);

        #if(ENABLE_AO_TECHNIQUES != 0)
                unblurredcolor.xyz = (applyAO) ? CombineAO(unblurredcolor.xyz,  RenderTargetRGBA32.Sample(Sampler1, texcoord.xy).w, texcoord.xy, 0) : unblurredcolor.xyz;
        #endif

	float discRadius = abs(centerDepth * 2.0 - 1.0) * fADOF_ShapeRadius;

        if(iADOF_FocusMode == false)
        {
	       discRadius*=(centerDepth < 0.5) ? (1.0 / max(fADOF_NearBlurCurve * 2.0, 1.0)) : 1.0;
        }
	//1.0 + 0.05 epsilon because discard at 1.0 in PS_DoF_Main
	scenecolor.xyz = lerp(blurredcolor.xyz, unblurredcolor.xyz,smoothstep(2.0,1.05,discRadius));
        scenecolor.w = centerDepth;

	return scenecolor;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4	PS_DoF_Smoothen(float2 texcoord : TEXCOORD0) : SV_Target
{
	float4 scenecolor 		= TextureColor.Sample(Sampler1, texcoord.xy);

	float centerDepth = scenecolor.w;
	float blurAmount  = abs(centerDepth * 2.0 - 1.0);
        float discRadius  = blurAmount * fADOF_ShapeRadius;

        if(iADOF_FocusMode == false)
        {
                discRadius*=(centerDepth < 0.5) ? (1.0 / max(fADOF_NearBlurCurve * 2.0, 1.0)) : 1.0;
        }

        #if(iADOF_PostBlurMode == 0)
        	float blurFactor = smoothstep(0.0,0.15,blurAmount)*fADOF_SmootheningAmount;
        	scenecolor = 0.0;

        	float offsets[5] = {-3.2307692308, -1.3846153846, 0.0, 1.3846153846, 3.2307692308};
        	float weights[3] = {0.2270270270, 0.3162162162, 0.0702702703};

        	for(int x=-2; x<=2; x++)
        	for(int y=-2; y<=2; y++)
        	{
        		float2 coord = float2(x,y);
        		float2 actualoffset = float2(offsets[x+2],offsets[abs(y+2)])*blurFactor*PixelSize.xy;
        		float weight = weights[abs(x)] * weights[abs(y)];

                        float4 tap = TextureColor.SampleLevel(Sampler1, texcoord.xy + actualoffset,0);
                        float tapblurAmount = abs(tap.w * 2.0 - 1.0);
                        if(iADOF_FocusMode == false)
                        {
                                weight *= smoothstep(-0.0001,0.0,tapblurAmount-blurAmount);
                        }
                        scenecolor.xyz += tap.xyz * weight;
                        scenecolor.w += weight;
        	}
        	scenecolor.xyz /= scenecolor.w;
        #elif(iADOF_PostBlurMode == 1)
                float3 bokehcolor = BokehBlurPost(TextureColor,texcoord.xy, discRadius/iADOF_ShapeQuality*0.666, centerDepth, 2, iADOF_ShapeVertices);
                scenecolor.xyz = lerp(scenecolor.xyz,bokehcolor.xyz,saturate(discRadius/iADOF_ShapeQuality*0.666));
        #endif

        #if(bADOF_ShapeChromaEnable == 0 || iADOF_ShapeChromaMode == 0)
                if(bADOF_ShapePreviewWindowEnable)
                {
                        float2 newcoord = 1.0 - texcoord.xy;
                        newcoord.x *= ScreenSize.z;
                        scenecolor.xyz *= max(smoothstep(0.2,0.21,newcoord.x),smoothstep(0.2,0.21,newcoord.y));
                        scenecolor.xyz += BokehBlurPreview(TextureColor, texcoord.xy, 0.023*ScreenSize.x*ScreenSize.z, 1.0 - float2(0.1*ScreenSize.w,0.1));
                }
        #endif

        scenecolor.w = centerDepth;
	return scenecolor;
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4	PS_DoF_PostChroma(float2 texcoord : TEXCOORD0) : SV_Target
{
	float4 scenecolor 		= TextureColor.Sample(Sampler1, texcoord.xy);
        float centerDepth               = scenecolor.w;
        float discRadius                = abs(centerDepth * 2.0 - 1.0) * fADOF_ShapeRadius;

        if(iADOF_FocusMode == false)
        {
               discRadius*=(centerDepth < 0.5) ? (1.0 / max(fADOF_NearBlurCurve * 2.0, 1.0)) : 1.0;
        }

        //this basically calculates the gradient of the luma height field
        //to determine where big changes in luma are (bokeh shape borders)
        //and shifts colors perpendiculary to these borders.
        //First blur, then chroma, otherwise separated samples of bokeh filter
        //might screw up sample directions.

        float sampleL,sampleT,sampleC;
        float3 sampleOffset = float3(PixelSize.xy,0.0);

        sampleL = dot(0.333,TextureColor.Sample(Sampler1,texcoord.xy - sampleOffset.xz).xyz);
        sampleT = dot(0.333,TextureColor.Sample(Sampler1,texcoord.xy - sampleOffset.zy).xyz);
        sampleC = dot(0.333,scenecolor.xyz);

        float2 derivativeXY = float2(sampleC-sampleL,sampleC-sampleT) / sampleOffset.xy;

        float normalScale = saturate(4.0 * sampleC)*0.7;
        float3 embossNormal = normalize(float3(derivativeXY,rcp(normalScale)));

        float2 gradientVector = normalize(embossNormal.xy); //actually -res but who cares...

        float4 chromasample[2] = {TextureColor.Sample(Sampler1, texcoord.xy + gradientVector.xy * PixelSize.xy * discRadius * fADOF_ShapeChromaAmount * 0.5), //match mode 0
                                  TextureColor.Sample(Sampler1, texcoord.xy - gradientVector.xy * PixelSize.xy * discRadius * fADOF_ShapeChromaAmount * 0.5)};

        chromasample[0].xyz = lerp(scenecolor.xyz,chromasample[0].xyz,saturate(abs(chromasample[0].w*4.0-2.0)));
        chromasample[1].xyz = lerp(scenecolor.xyz,chromasample[1].xyz,saturate(abs(chromasample[1].w*4.0-2.0)));

        //todo: restructure order to match mode 0 definition
        #if(  iADOF_ShapeChromaOrder == 2)
                scenecolor.xyz = float3(scenecolor.x,chromasample[0].z,chromasample[1].y).xzy;
        #elif(iADOF_ShapeChromaOrder == 3)
                scenecolor.xyz = float3(scenecolor.y,chromasample[0].x,chromasample[1].z).yxz;
        #elif(iADOF_ShapeChromaOrder == 4)
                scenecolor.xyz = float3(scenecolor.z,chromasample[0].y,chromasample[1].x).zyx;
        #elif(iADOF_ShapeChromaOrder == 5)
                scenecolor.xyz = float3(scenecolor.z,chromasample[0].x,chromasample[1].y).yzx;
        #elif(iADOF_ShapeChromaOrder == 6)
                scenecolor.xyz = float3(scenecolor.y,chromasample[0].z,chromasample[1].x).zxy;
        #else
                scenecolor.xyz = float3(scenecolor.x,chromasample[0].y,chromasample[1].z).xyz;
        #endif

        if(bADOF_ShapePreviewWindowEnable)
        {
                float2 newcoord = 1.0 - texcoord.xy;
                newcoord.x *= ScreenSize.z;
                scenecolor.xyz *= max(smoothstep(0.2,0.21,newcoord.x),smoothstep(0.2,0.21,newcoord.y));
                scenecolor.xyz += BokehBlurPreview(TextureColor, texcoord.xy, 0.023*ScreenSize.x*ScreenSize.z, 1.0 - float2(0.1*ScreenSize.w,0.1));
        }

	return scenecolor;
}



//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Techniques
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//write aperture with time factor, this is always first technique
technique11 Aperture
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_Aperture()));
	}
}

//compute focus from depth of screen and may be brightness, this is always second technique
technique11 ReadFocus
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_ReadFocus()));
	}
}

//write focus with time factor, this is always third technique
technique11 Focus
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_Focus()));
	}
}

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

technique11 DOF <string UIName="Accurate DOF";>
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_CoC(0)));
	}
}

technique11 DOF1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_Main()));
	}
}

technique11 DOF2
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_Combine(0)));
	}
}

technique11 DOF3
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_Smoothen()));
	}
}
#if(bADOF_ShapeChromaEnable != 0 && iADOF_ShapeChromaMode == 1)
technique11 DOF4
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_PostChroma()));
	}
}
#endif

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
#if(ENABLE_AO_TECHNIQUES != 0)
technique11 DOFAO <string UIName="Accurate DOF"; string RenderTarget="RenderTargetRGBA32";>
{
        pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_AO_NormalTexture()));
	}
}

technique11 DOFAO1
{
        pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_AO_Gen()));
	}
}

technique11 DOFAO2
{
        pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_AO_Blur1()));
	}
}

technique11 DOFAO3 <string RenderTarget="RenderTargetRGBA32";>
{
        pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_AO_Blur2()));
	}
}

technique11 DOFAO4
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_CoC(1)));
	}
}

technique11 DOFAO5
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_Main()));
	}
}

technique11 DOFAO6
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_Combine(1)));
	}
}

technique11 DOFAO7
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_Smoothen()));
	}
}

#if(bADOF_ShapeChromaEnable != 0 && iADOF_ShapeChromaMode == 1)
technique11 DOFAO8
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_PostChroma()));
	}
}
#endif

//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

technique11 AO <string UIName="Marty McFly's AO"; string RenderTarget="RenderTargetRGBA32";>
{
        pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_AO_NormalTexture()));
	}
}

technique11 AO1
{
        pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_AO_Gen()));
	}
}

technique11 AO2
{
        pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_AO_Blur1()));
	}
}

technique11 AO3 <string RenderTarget="RenderTargetRGBA32";>
{
        pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_AO_Blur2()));
	}
}

technique11 AO4
{
        pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_AO_Combine(bMXAOShowAO )));
	}
}
#endif
