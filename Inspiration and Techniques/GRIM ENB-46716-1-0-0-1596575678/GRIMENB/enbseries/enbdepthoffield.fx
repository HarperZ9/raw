/* //////////////////////////////////////////////////////
//                ENBSeries effect file                //
//         visit http://enbdev.com for updates         //
//       Copyright (c) 2007-2019 Boris Vorontsov       //
//----------------------ENB PRESET---------------------//
			
						GRIM  ENB
				
//-----------------------CREDITS-----------------------//
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++
		ENBSeries Skyrim SE dx11 sm5 effect file
	visit github.com/martymcmodding for news/updates
//+++++++++++++++++++++++++++++++++++++++++++++++++++++++
// Advanced Depth of Field & SSAO 2.1f by Marty McFly  //
// 		  Do not redistribute without credits!         //
// 		  Copyright (c) 2008-2019 Marty McFly		   //
//													   //
// McFly: Original author of the DOF    			   //
// Matso: Original author of the CA and Noise  		   //
// L00 :  Shader Setup, Presets and Settings,          //
//        Port and Modification of  Shaders            //
///////////////////////////////////////////////////////*/

int		iQuality			<string UIName="Quality";string UIWidget="quality";int UIMin=-1;int UIMax=4;> = {1};
bool	bMFocus				<string UIName="Manual Focus (Click on Focus Point)";> = {false};	

float	DofAperture			<string UIName="Aperture                                                     f/";string UIWidget="Spinner";float UIStep=0.1;float UIMin=1.40;float UIMax=12.0;> = {2.0};
float	fBokehAperture		<string UIName="Scale";string UIWidget="Spinner";float UIStep=1.0f;float UIMin=0.1;float UIMax=50.0;> = {6.0};
float	fBokehCurve			<string UIName="Bokeh";string UIWidget="Spinner";float UIStep=1.0;float UIMin=0.0f;float UIMax=5.0;> = {1.0};
float	fGrainScale			<string UIName="Grain";string UIWidget="Spinner";float UIStep=0.05;float UIMin=0.00;float UIMax=2.0;> = {0.1};
float	fChromaPower		<string UIName="Chromatic Aberration";string UIWidget="Spinner";float UIStep=1.0;float UIMin=0.00;float UIMax=10.00;> = {3.0};

/////////////////////////////////////////////////////////
//               	INTERNAL SETTINGS                  //
/////////////////////////////////////////////////////////

//DOF
#define bADOF_AutofocusEnable 		1
#define fADOF_AutofocusCenter 		float2(0.5,0.5)
#define	iADOF_AutofocusSamples 		5
#define	fADOF_AutofocusRadius 		0.02
#define	fADOF_ManualfocusDepth 		0.05
#define bADOF_ShapeWeightEnable 	1

#define	fADOF_NearBlurMult 			-1.0f+fADOF_FarBlurCurve //1.0
#define	fADOF_FarBlurMult 			1.0f*fADOF_FarBlurCurve
#define	fADOF_NearBlurCurve 		1.0f+fADOF_FarBlurCurve

#define	fADOF_InfiniteFocus 		0.03
#define	iADOF_ShapeVertices 		7

#define	fADOF_ShapeCurvatureAmount	0.65
#define	fADOF_ShapeRotation 128
#define	fADOF_ShapeAnamorphRatio	0.65

#define	fADOF_SmootheningAmount		0.35
#define	fADOF_ShapeWeightCurve		10.0
#define	fADOF_ShapeWeightAmount		0.88

#define fADOF_FarBlurCurve 			fLensAperture
#define fADOF_ShapeRadius 			fBokehAperture/(fLensAperture+0.65)

#define fLensAperture 				DofAperture-1.25f
#define fADOF_BokehCurve			fBokehCurve+0.1f

float4	Timer; 			//x = generic timer in range 0..1, period of 16777216 ms (4.6 hours), y = average fps, w = frame time elapsed (in seconds)
float4	ScreenSize; 		//x = Width, y = 1/Width, z = aspect, w = 1/aspect, aspect is Width/Height
float	AdaptiveQuality;	//changes in range 0..1, 0 means full quality, 1 lowest dynamic quality (0.33, 0.66 are limits for quality levels)
float4	Weather;		//x = current weather index, y = outgoing weather index, z = weather transition, w = time of the day in 24 standart hours. Weather index is value from weather ini file, for example WEATHER002 means index==2, but index==0 means that weather not captured.
float4	TimeOfDay1;		//x = dawn, y = sunrise, z = day, w = sunset. Interpolators range from 0..1
float4	TimeOfDay2;		//x = dusk, y = night. Interpolators range from 0..1
float	ENightDayFactor;	//changes in range 0..1, 0 means that night time, 1 - day time
float	EInteriorFactor;	//changes 0 or 1. 0 means that exterior, 1 - interior

#define PixelSize 		float2(ScreenSize.y,ScreenSize.y*ScreenSize.z)
uniform float4 DepthParameters  = float4(1.0,3000.0,-2999.0f,0.0);//x = near plane, y = far plane, z = -(y-x), w = unused
extern float fWaterLevel = 1.0;
#define DOF(sd,sf)		fADOF_ShapeRadius * smoothstep(fADOF_FarBlurMult * tempF1.y, fADOF_NearBlurMult * tempF1.z, abs(sd - sf))
// Chromatic aberration parameters
float3 fvChroma = float3(0.9995, 1.000, 1.0005);// displacement scales of red, green and blue respectively
#define fBaseRadius 0.9							// below this radius the effect is less visible
#define fFalloffRadius 1.8						// over this radius the effect is max
#define CHROMA_POW		64.0								// the bigger the value, the more visible chomatic aberration effect in DoF
#define SEED			Timer.x //Timer.x
#define PI				3.1415926535897932384626433832795


//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
//external enb debugging parameters for shader programmers, do not modify
//++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4	tempF1; 		//0,1,2,3
float4	tempF2; 		//5,6,7,8
float4	tempF3; 		//9,0
float4	tempInfo1; 		//float4(cursorpos.xy 0~1,isshaderwindowopen, mouse buttons)
float4	tempInfo2;		//float4(cursorpos.xy prev left mouse button click, cursorpos.xy prev right mouse button click)

/////////////////////////////////////////////////////////
//               		TEXTURES                       //
/////////////////////////////////////////////////////////

float4			DofParameters;		//z = ApertureTime multiplied by time elapsed, w = FocusingTime multiplied by time elapsed
Texture2D		TextureCurrent; 	//current frame focus depth or aperture. unused in dof computation
Texture2D		TexturePrevious; 	//previous frame focus depth or aperture. unused in dof computation
Texture2D		TextureOriginal; 	//color R16B16G16A16 64 bit hdr format
Texture2D		TextureColor; 		//color which is output of previous technique (except when drawed to temporary render target), R16B16G16A16 64 bit hdr format
Texture2D		TextureDepth; 		//scene depth R32F 32 bit hdr format
Texture2D		TextureFocus; 		//this frame focus 1*1 R32F hdr red channel only. computed in PS_Focus
Texture2D		TextureAperture; 	//this frame aperture 1*1 R32F hdr red channel only. computed in PS_Aperture
Texture2D		TextureAdaptation;	//previous frame vanilla or enb adaptation 1*1 R32F hdr red channel only. adaptation computed after depth of field and it's kinda "average" brightness of screen!!!
Texture2D		texNoise;

//temporary textures which can be set as render target for techniques via annotations like <string RenderTarget="RenderTargetRGBA32";>
Texture2D		RenderTargetRGBA32; 	//R8G8B8A8 32 bit ldr format
Texture2D		RenderTargetRGBA64; 	//R16B16G16A16 64 bit ldr format
Texture2D		RenderTargetRGBA64F; 	//R16B16G16A16F 64 bit hdr format
Texture2D		RenderTargetR16F; 	//R16F 16 bit hdr format with red channel only
Texture2D		RenderTargetR32F; 	//R32F 32 bit hdr format with red channel only
Texture2D		RenderTargetRGB32F; 	//32 bit hdr format without alpha

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

SamplerState		Sampler2
{
	Filter = MIN_MAG_MIP_POINT;
	AddressU = Wrap;
	AddressV = Wrap;
};

SamplerState		SamplerNoise
{
	Filter = MIN_MAG_MIP_LINEAR;
	AddressU = Wrap;
	AddressV = Wrap;
};

/////////////////////////////////////////////////////////
//               	   FUNCTIONS                 	   //
/////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////
/**
 * Pseudo-random number generator - returns a number generated according to the provided vector.
 */
float Random(float2 co)
{
    return frac(sin(dot(co.xy, float2(12.9898, 78.233))) * 43758.5453);
}

// CHANGE WITH ASPECT RATIO HERE
float2 Random2(float2 coord)
{
	float width  = ScreenSize.x; // Would #define be better?
    float height = ScreenSize.x/ScreenSize.z;
	
	float noiseX = ((frac(1.0-coord.x*(width/2.0))*0.25)+(frac(coord.y*(height/2.0))*0.75))*2.0-1.0;
	float noiseY = ((frac(1.0-coord.x*(width/2.0))*0.75)+(frac(coord.y*(height/2.0))*0.25))*2.0-1.0;
	
	noiseX = clamp(frac(sin(dot(coord ,float2(12.9898,78.233))) * 43758.5453),0.0,1.0)*2.0-1.0;
	noiseY = clamp(frac(sin(dot(coord ,float2(12.9898,78.233)*2.0)) * 43758.5453),0.0,1.0)*2.0-1.0;
	
	return float2(noiseX, noiseY);
}

/**
 * Movie grain function - returns a random, time scaled value for the given pixel coordinate.
 */
float Grain(float3 tex)
{
	float fMulti;
	if (iQuality==2) { 
		fMulti = 0.33;
	}
	if (iQuality==1) {
		fMulti = 0.5;
	}
	if (iQuality==0) {
		fMulti = 0.75;
	}
	if (iQuality==-1) {
		fMulti = 1.0;
	}
	
	float r = Random(tex.xy);
	float grain = sin(PI * tex.z * r * Timer.x*16777216) * (fGrainScale*fMulti) * r;
	return grain;
}
/////////////////////////////////////////////////////////
//               	  DEPTH OF FIELD               	   //
/////////////////////////////////////////////////////////

void VS_DOF(in float3 inpos : POSITION, inout float2 txcoord0 : TEXCOORD0, out float4 outpos : SV_POSITION)
{
	outpos = float4(inpos.xyz,1.0);
}

float GetLinearDepth(float2 coords)
{
	float depth = TextureDepth.SampleLevel(Sampler1, coords.xy,0).x;
	depth *= rcp(DepthParameters.y + depth * DepthParameters.z);
	return depth;
}

float3 GetPosition(float2 coords)
{
        return float3(coords.xy*2.0-1.0,1.0)*GetLinearDepth(coords.xy)*DepthParameters.y;
}

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

float GetCoC(float2 texcoord)
{
	float	scenedepth	= GetLinearDepth(texcoord.xy);
	float	scenefocus	= TextureFocus.Sample(Sampler0, texcoord.xy).x;
	float   scenecoc 	= 0.0;

	scenefocus = smoothstep(0.0,fADOF_InfiniteFocus,scenefocus);
	scenedepth = smoothstep(0.0,fADOF_InfiniteFocus,scenedepth);

	float farBlurDepth = scenefocus*pow(4.0,fADOF_FarBlurCurve);

	if(scenedepth < scenefocus)
	{
		scenecoc = (scenedepth - scenefocus) / scenefocus;
		scenecoc *= fADOF_NearBlurMult;
	}
	else
	{
		scenecoc=(scenedepth - scenefocus)/(farBlurDepth - scenefocus);
		scenecoc *= fADOF_FarBlurMult;
		scenecoc=saturate(scenecoc);
	}

	scenecoc = (scenedepth < 0.00000001) ? 0.0 : scenecoc; //first person models, that epsilon is handpicked, do not change
	scenecoc = saturate(scenecoc * 0.5 + 0.5);
	return scenecoc;
}


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

        
                float2 discRadiusScaled	= discRadius*float2(fADOF_ShapeAnamorphRatio,ScreenSize.z)*0.0006*ringIncrement;
   

        float2 currentVertex,nextVertex,matrixVector;
	sincos(radians(fADOF_ShapeRotation),currentVertex.y,currentVertex.x);
	sincos(6.2831853 / nVertices,matrixVector.x,matrixVector.y);

	float2x2 rotMatrix = float2x2(matrixVector.y,-matrixVector.x,matrixVector.x,matrixVector.y);

        #if(bADOF_ShapeWeightEnable != 0)
               res.w *= saturate(1.0f-fADOF_ShapeWeightAmount*nQuality);
               res.xyz *= res.w;
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

				
				        float4 tap = colortex.SampleLevel(Sampler1, coords.xy + (sampleOffset.xy * discRadiusScaled) * iRings,0);
				
						tap.w = (centerDepth > 0.5) ? saturate(abs(tap.w*2.0-1.0)-iRings*ringIncrement*abs(centerDepth*2.0-1.0)) : 1.0; //I believe this is almost perfect.
   
                             


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

float4 ChromaticAberration(float2 tex)
{
	float d = distance(tex, float2(0.5, 0.5));
	float f = smoothstep(fBaseRadius, fFalloffRadius, d);
	float3 chroma = pow(f + fvChroma, fChromaPower);
	
	float2 tr = ((2.0 * tex - 1.0) * chroma.r) * 0.5 + 0.5;
	float2 tg = ((2.0 * tex - 1.0) * chroma.g) * 0.5 + 0.5;
	float2 tb = ((2.0 * tex - 1.0) * chroma.b) * 0.5 + 0.5;
	
	float3 color = float3(TextureColor.Sample(Sampler0, tr).r, TextureColor.Sample(Sampler0, tg).g, TextureColor.Sample(Sampler0, tb).b) * (1.0 - f);
	
	return float4(color, 1.0);
}


float4 ChromaticAberration(float2 tex, float outOfFocus)
{
	float d = distance(tex, float2(0.5, 0.5));
	float f = smoothstep(fBaseRadius, fFalloffRadius, d);
	float3 chroma = pow(f + fvChroma, CHROMA_POW * outOfFocus * fChromaPower);

	float2 tr = ((2.0 * tex - 1.0) * chroma.r) * 0.5 + 0.5;
	float2 tg = ((2.0 * tex - 1.0) * chroma.g) * 0.5 + 0.5;
	float2 tb = ((2.0 * tex - 1.0) * chroma.b) * 0.5 + 0.5;
	
	float3 color = float3(TextureColor.Sample(Sampler0, tr).r, TextureColor.Sample(Sampler0, tg).g, TextureColor.Sample(Sampler0, tb).b) * (1.0 - outOfFocus);
	
	return float4(color, 1.0);
}



/////////////////////////////////////////////////////////
//               	  PIXEL SHADER                     //
/////////////////////////////////////////////////////////

/// DEPTH OF FIELD

//? -> 1x1 R32F
float4	PS_Aperture(float2 texcoord : TEXCOORD0) : SV_Target
{
	//as I don't use aperture and deleting the technique causes weird things to happen, don't waste resources :v
	return 1;
}

//fullres -> 16x16 R32F //Slight modification from Looping to adapt Wolrajh focus point.
float4	PS_ReadFocus(float2 texcoord : TEXCOORD0) : SV_Target
{
	float scenefocus 	= fADOF_ManualfocusDepth;
	float2 coords 		= 0.0;

	if(bADOF_AutofocusEnable != 0)
	{
		scenefocus = GetLinearDepth(fADOF_AutofocusCenter.xy);
		float2 offsetVector = float2(1.0,0.0) * fADOF_AutofocusRadius;
		float Alpha = 6.2831853 / iADOF_AutofocusSamples;
		float2x2 rotMatrix = float2x2(cos(Alpha),-sin(Alpha),sin(Alpha),cos(Alpha));

		for(int i=0; i<iADOF_AutofocusSamples; i++)
		{
			float2 currentOffset = fADOF_AutofocusCenter + offsetVector.xy;
			scenefocus += GetLinearDepth(currentOffset);
			offsetVector = mul(offsetVector,rotMatrix);
		}

		scenefocus /= iADOF_AutofocusSamples;
	}
	if (bMFocus == true)
	{
		
		scenefocus = GetLinearDepth(tempInfo2.xy);
	
	}
	scenefocus = saturate(scenefocus);
	return scenefocus;
}

//16x16 -> 1x1 R32F  ///Modified to always interpolate
float4	PS_Focus(float2 texcoord : TEXCOORD0) : SV_Target
{
	float prevFocus = TexturePrevious.Sample(Sampler1, texcoord.xy).x;
	float currFocus = TextureCurrent.Sample(Sampler1, texcoord.xy).x;
	
	float res = 0.0f;
	res = lerp(prevFocus, currFocus, DofParameters.w);
	if(prevFocus < currFocus) res = lerp(prevFocus,currFocus,DofParameters.w*lerp(0.03f,0.35f,saturate(currFocus/prevFocus)) / pow(2.5f,fADOF_FarBlurCurve));
	res = saturate(res);
	return res;
	return 0.0f;
}

float4	PS_CoC(float2 texcoord : TEXCOORD0) : SV_Target
{
	float4 res 		= TextureColor.Sample(Sampler1, texcoord.xy);
	float scenecoc 		= GetCoC(texcoord.xy);
	res.w = scenecoc;
	return res;
}

float4	PS_DoF_Main(float2 texcoord : TEXCOORD0) : SV_Target
{
	
	float fQualityMulti;
	float fADOF_RenderResolutionMult;
	float iADOF_ShapeQuality;
	
	if (iQuality==2) { 
		fADOF_RenderResolutionMult = 0.33;
		iADOF_ShapeQuality = 4;
	}
	if (iQuality==1) {
		fADOF_RenderResolutionMult = 0.5;
		iADOF_ShapeQuality = 5;
	}
	if (iQuality==0) {
		fADOF_RenderResolutionMult = 0.75;
		iADOF_ShapeQuality = 6;
	}
	if (iQuality==-1) {
		fADOF_RenderResolutionMult = 1.0;
		iADOF_ShapeQuality = 7;
	}
	
	float2 scaledcoord = texcoord.xy / fADOF_RenderResolutionMult;
        clip(!all(saturate(-scaledcoord * scaledcoord + scaledcoord + 0.01)) ? -1:1); //0.01 epsilon to prevent border issues with AO.

	float4 scenecolor =  TextureColor.Sample(Sampler1, scaledcoord.xy);

	float centerDepth = scenecolor.w;
	float blurAmount = abs(centerDepth * 2.0 - 1.0);
	float discRadius = blurAmount * fADOF_ShapeRadius;

       
	       discRadius*=(centerDepth < 0.5) ? (1.0 / max(fADOF_NearBlurCurve * 2.0, 1.0)) : 1.0;
           clip((discRadius<1.0)?-1:1);

	
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

float4	PS_DoF_Combine(float2 texcoord : TEXCOORD0) : SV_Target
{
	float fQualityMulti;
	float fADOF_RenderResolutionMult;
	if (iQuality==2) fADOF_RenderResolutionMult = 0.33;
	if (iQuality==1) fADOF_RenderResolutionMult = 0.5;
	if (iQuality==0) fADOF_RenderResolutionMult = 0.75;
	if (iQuality==-1) fADOF_RenderResolutionMult = 1.0;
	
	float4 blurredcolor 		= TextureColor.Sample(Sampler1, texcoord.xy*fADOF_RenderResolutionMult);
	float4 unblurredcolor		= TextureOriginal.Sample(Sampler1, texcoord.xy);
	float4 scenecolor			= ChromaticAberration(texcoord.xy);

	float centerDepth			= GetCoC(texcoord.xy);

	float discRadius = abs(centerDepth * 2.0 - 1.0) * fADOF_ShapeRadius;
	discRadius*=(centerDepth < 0.5) ? (1.0 / max(fADOF_NearBlurCurve * 2.0, 1.0)) : 1.0;

	//1.0 + 0.05 epsilon because discard at 1.0 in PS_DoF_Main
	scenecolor.xyz = lerp(blurredcolor.xyz, unblurredcolor.xyz,smoothstep(4.0,1.05,discRadius));

	scenecolor.w = centerDepth;
	return scenecolor;
}

float4	PS_DoF_Smoothen(float2 texcoord : TEXCOORD0) : SV_Target
{
	float4 scenecolor 		= TextureColor.Sample(Sampler1, texcoord.xy);

	float centerDepth = scenecolor.w;
	float blurAmount = abs(centerDepth * 2.0 - 1.0);
	blurAmount = smoothstep(0.0,0.15,blurAmount)*fADOF_SmootheningAmount;

	scenecolor = 0.0;


	float offsets[5] = {-3.2307692308, -1.3846153846, 0.0, 1.3846153846, 3.2307692308};
	float weights[3] = {0.2270270270, 0.3162162162, 0.0702702703};

	float chromaamount = 1.3;

	for(int x=-2; x<=2; x++)
	for(int y=-2; y<=2; y++)
	{
		float2 coord = float2(x,y);
		float2 actualoffset = float2(offsets[x+2],offsets[abs(y+2)])*blurAmount*PixelSize.xy;
		float weight = weights[abs(x)] * weights[abs(y)];
		scenecolor.xyz += TextureColor.SampleLevel(Sampler1, texcoord.xy + actualoffset,0).xyz * weight;
		scenecolor.w += weight;
	}
	scenecolor.xyz /= scenecolor.w;
	return scenecolor;

}

//CA
float4 PS_ProcessPass_Chroma(float2 texcoord : TEXCOORD0) : SV_Target
{
	float2 coord = texcoord.xy;
	float4 scenecolor = ChromaticAberration(coord.xy);
	scenecolor.a = 1.0;
	return scenecolor;
}

//GRAIN
float4 PS_ProcessPass_ImageGrain(float2 texcoord : TEXCOORD0) : SV_Target

{
	float4 res=TextureColor.Sample(Sampler1, texcoord.xy);
	float4 Noise=TextureColor.Sample(SamplerNoise, texcoord.xy);

	if (fGrainScale>0.0f) 
	{
		res.rgb += Noise.rgb * Grain(float3(texcoord.xy, SEED));
		res.a = 1.0;
	}
	return res;
}
/////////////////////////////////////////////////////////
//               	    TECHNIQUES                     //
/////////////////////////////////////////////////////////

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

//DOF
technique11 DOF <string UIName="GRIM.ENB: DOF";> 
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_ProcessPass_Chroma()));
	}
}

technique11 DOF1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_CoC()));
	}
}

technique11 DOF2
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_Main()));
	}
}

technique11 DOF3
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_ProcessPass_ImageGrain()));
	}
}

technique11 DOF4
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_Combine()));
	}
}

technique11 DOF5
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_DOF()));
		SetPixelShader(CompileShader(ps_5_0, PS_DoF_Smoothen()));
	}
}
