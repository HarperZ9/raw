/* Serenity ENB HQ Bloom FX file
 * File Author: prod80
 * File Version: GEN7 ( October 2014 )
 * Based on work by Ken Turkowski ( Adobe Systems )
 * Article describing the technique: http://http.developer.nvidia.com/GPUGems3/gpugems3_ch40.html

 * Features
 * - Prepass bloom texture controls
 * - Advanced dynamic blur radius based
 *   on depth information
 * - 3 Bloom tinting methods linked to BBS
 * - Options for SuperSampling/DownSampling (GEN5 update)
 * - Controls to select which bloom textures to mix for final result (GEN5 update)
 * - Everything now ported to GUI, including QUALITY (GEN5 update)
 * - Major changes to Gaussian passes and optimizations, recoded depth testing (GEN5 update)
 * - No additional performance costs due to depth testing (GEN5 update)
 * - Redone math behind SuperSampling to support the odd resolutions (GEN7 update)

 
 * ATTENTION!!!!!!!!!!!!!
 * THIS BLOOM SHADER REQUIRES ENB BINARY 254+ RELEASED ON THIS DATE OR LATER: MAY 2 2014
 * WITHOUT THIS BINARY THIS BLOOM SHADER WILL NOT WORK AT ALL!!!!!
 *
 * THIS IS BECAUSE BINARY 254 BEFORE THIS DATE DOES NOT HAVE SAMPLERDEPTH ADDED.
 * THIS SAMPLER IS ADDED ON MY SPECIAL REQUEST AND THIS IS NOT IN THE ENB RELEASE NOTES!

 
 * ENBSeries
 * visit http://enbdev.com for updates
 * Copyright (c) 2007-2014 Boris Vorontsov
 */

// THERE IS NOTHING TO EDIT BELOW THIS POINT ++++++++++++++++++++++++++++++++++++++++++++
// GUI ELEMENTS +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	bool   Section_Options <
		string UIName =  "------Options & Settings----";
	> = {false};
	float QUALITYLEVEL <
		string UIName="Bloom Quality";
		string UIWidget="Quality";
		float UIMin=-2;
		float UIMax=2;
		float UIStep=1;
	> = {0}; //0 best compromise between quality and performance
	float2 BASERESOLUTION <
		string UIName="Base Resolution";
		string UIWidget="Spinner";
		float UIMin=0;
		float UIMax=10240;
		float UIStep=1;
	> = {1920, 1080};
	bool USE_SS <
		string UIName="Enable SuperSampling";
	> = {false};
	float2 SSRESOLUTION <
		string UIName="SuperSample Resolution";
		string UIWidget="Spinner";
		float UIMin=0;
		float UIMax=10240;
		float UIStep=1;
	> = {3200, 1800};
	bool   Section_Depth <
		string UIName =  "------Depth-----------------";
	> = {false};
	bool show_depth <
		string UIName="Show Depth";
	> = {false};
	float zfar <
		string UIName="Far Depth";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=100000.0;
		float UIStep=0.1;
	> = {50.0};
	float minDepthE <
		string UIName="Exterior: Min Depth";
		string UIWidget="Spinner";
		float UIMin=0;
		float UIMax=1;
		float UIStep=0.001;
	> = {0.67};
	float minDepthI <
		string UIName="Interior: Min Depth";
		string UIWidget="Spinner";
		float UIMin=0;
		float UIMax=1;
		float UIStep=0.001;
	> = {0.67};
	bool   Section_Tint <
		string UIName =  "------Tinting---------------";
	> = {false};
	bool use_tinting <
		string UIName="Tinting: Enable Bloom Tinting";
	> = {true};
	float tint_type <
		string UIName="Tinting: Tint Method";
		string UIWidget="Spinner";
		float UIMin=1;
		float UIMax=3;
		float UIStep=1;
	> = {1};
	float3 tint_colorD <
		string UIName="Tinting: Day Tint RGB Color";
		string UIWidget="Color";
	> = {0.6, 0.4, 1.0};
	float tint_levelD <
		string UIName="Tinting: Day Tint Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.44};
	float tint_mixD <
		string UIName="Tinting: (method 3) Day Tint Mix";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.22};
	float3 tint_colorN <
		string UIName="Tinting: Night Tint RGB Color";
		string UIWidget="Color";
	> = {0.6, 0.4, 1.0};
	float tint_levelN <
		string UIName="Tinting: Night Tint Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.44};
	float tint_mixN <
		string UIName="Tinting: (method 3) Night Tint Mix";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.22};
	float3 tint_colorI <
		string UIName="Tinting: Interior Tint RGB Color";
		string UIWidget="Color";
	> = {0.6, 0.4, 1.0};
	float tint_levelI <
		string UIName="Tinting: Interior Tint Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.44};
	float tint_mixI <
		string UIName="Tinting: (method 3) Interior Tint Mix";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.22};
	bool   Section_BDay <
		string UIName =  "------Bloom Day-------------";
	> = {false};
	float	BinBlackD <
		string UIName="Day Bloom: Black Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float	BinGammaD <
		string UIName="Day Bloom: Gamma Curve";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=5.0;
		float UIStep=0.001;
	> = {1.0};
	float	BinWhiteD <
		string UIName="Day Bloom: White Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float BoutBlackD <
		string UIName="Day Bloom: Black Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float BoutWhiteD <
		string UIName="Day Bloom: White Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float bsatD <
		string UIName="Day Bloom: Saturation";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float BloomSigmaD <
		string UIName="Day Bloom: Sigma (Blur width)";
		string UIWidget="Spinner";
		float UIMin=1.2;
		float UIMax=64.0;
		float UIStep=0.001;
	> = {6.0};
	float hIntensityD <
		string UIName="Day Bloom: X Intensity";
		string UIWidget="Spinner";
		float UIMin=1.0;
		float UIMax=10.0;
		float UIStep=0.001;
	> = {1.0};
	float vIntensityD <
		string UIName="Day Bloom: X+Y Intensity";
		string UIWidget="Spinner";
		float UIMin=1.0;
		float UIMax=10.0;
		float UIStep=0.001;
	> = {1.0};
	bool   Section_BNight <
		string UIName =  "------Bloom Night-----------";
	> = {false};
	float	BinBlackN <
		string UIName="Night Bloom: Black Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float	BinGammaN <
		string UIName="Night Bloom: Gamma Curve";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=5.0;
		float UIStep=0.001;
	> = {1.0};
	float	BinWhiteN <
		string UIName="Night Bloom: White Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float BoutBlackN <
		string UIName="Night Bloom: Black Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float BoutWhiteN <
		string UIName="Night Bloom: White Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float bsatN <
		string UIName="Night Bloom: Saturation";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float BloomSigmaN <
		string UIName="Night Bloom: Sigma (Blur width)";
		string UIWidget="Spinner";
		float UIMin=1.2;
		float UIMax=64.0;
		float UIStep=0.001;
	> = {6.0};
	float hIntensityN <
		string UIName="Night Bloom: X Intensity";
		string UIWidget="Spinner";
		float UIMin=1.0;
		float UIMax=10.0;
		float UIStep=0.001;
	> = {1.0};
	float vIntensityN <
		string UIName="Night Bloom: X+Y Intensity";
		string UIWidget="Spinner";
		float UIMin=1.0;
		float UIMax=10.0;
		float UIStep=0.001;
	> = {1.0};
	bool   Section_BInterior <
		string UIName =  "------Bloom Interior--------";
	> = {false};
	float	BinBlackI <
		string UIName="Interior Bloom: Black Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float	BinGammaI <
		string UIName="Interior Bloom: Gamma Curve";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=5.0;
		float UIStep=0.001;
	> = {1.0};
	float	BinWhiteI <
		string UIName="Interior Bloom: White Level";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float BoutBlackI <
		string UIName="Interior Bloom: Black Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.0};
	float BoutWhiteI <
		string UIName="Interior Bloom: White Cutoff";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float bsatI <
		string UIName="Interior Bloom: Saturation";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	float BloomSigmaI <
		string UIName="Interior Bloom: Sigma (Blur width)";
		string UIWidget="Spinner";
		float UIMin=1.2;
		float UIMax=64.0;
		float UIStep=0.001;
	> = {6.0};
	float hIntensityI <
		string UIName="Interior Bloom: X Intensity";
		string UIWidget="Spinner";
		float UIMin=1.0;
		float UIMax=10.0;
		float UIStep=0.001;
	> = {1.0};
	float vIntensityI <
		string UIName="Interior Bloom: X+Y Intensity";
		string UIWidget="Spinner";
		float UIMin=1.0;
		float UIMax=10.0;
		float UIStep=0.001;
	> = {1.0};
	bool   Section_Textures <
		string UIName =  "------Texture Selection-----";
	> = {false};
	bool tex_select1 <
		string UIName="Enable Tex 1: FullScreen";
	> = {true};
	float tex_select1_int <
		string UIName="Texture 1: Intensity";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.4};
	bool tex_select2 <
		string UIName="Enable Tex 2: DownSampled x2";
	> = {true};
	float tex_select2_int <
		string UIName="Texture 2: Intensity";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	bool tex_select3 <
		string UIName="Enable Tex 3: DownSampled x4";
	> = {true};
	float tex_select3_int <
		string UIName="Texture 3: Intensity";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	bool tex_select4 <
		string UIName="Enable Tex 4: DownSampled x8";
	> = {true};
	float tex_select4_int <
		string UIName="Texture 4: Intensity";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.85};
	bool tex_select5 <
		string UIName="Enable Tex 5: DownSampled x16";
	> = {true};
	float tex_select5_int <
		string UIName="Texture 5: Intensity";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.65};
	bool tex_select6 <
		string UIName="Enable Tex 6: DownSampled x32";
	> = {true};
	float tex_select6_int <
		string UIName="Texture 6: Intensity";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {0.45};
	bool tex_select7 <
		string UIName="Enable Tex 7: Prepass Result";
	> = {false};
	float tex_select7_int <
		string UIName="Texture 7: Intensity";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	bool tex_select8 <
		string UIName="Enable Tex 8: Original Input";
	> = {false};
	float tex_select8_int <
		string UIName="Texture 8: Intensity";
		string UIWidget="Spinner";
		float UIMin=0.0;
		float UIMax=1.0;
		float UIStep=0.001;
	> = {1.0};
	
	
// SOME STANDARD VARIABLES ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

//float4	tempF1; 			// Not used //0,1,2,3
//float4	tempF2; 			// Not used //5,6,7,8
//float4	tempF3; 			// Not used //9,0
float4	ScreenSize; 			// x=Width, y=1/Width, z=ScreenScaleY, w=1/ScreenScaleY
//float4	Timer;				// Not used
float4	TempParameters; 		// Used to correct DIRECT3D half pixel offset correction (.xy), Unknown screenfactor (.z), Passnumber (.w)?
//float4	LenzParameters;		// Not used
float4	BloomParameters;		// Not used: BloomRadius1 (GTA4) (.x), BloomRadius2(GTA4) (.y) //Used: BloomBlueShiftAmount (.z), BloomContrast (.w) 
float	ENightDayFactor;		// 0=Night, 1=Day
float	EInteriorFactor;		// 0=Exterior, 1=Interior
	
// TEXTURES AND SAMPLERS ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

texture2D texDepth;
texture2D texBloom1;
texture2D texBloom2;
texture2D texBloom3;
texture2D texBloom4;
texture2D texBloom5;
texture2D texBloom6;
texture2D texBloom7;//additional bloom tex
texture2D texBloom8;//additional bloom tex

sampler2D SamplerDepth = sampler_state
{
	Texture   = <texDepth>;
	MinFilter = POINT;
	MagFilter = POINT;
	MipFilter = NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerBloom1 = sampler_state
{
    Texture   = <texBloom1>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerBloom2 = sampler_state
{
    Texture   = <texBloom2>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerBloom3 = sampler_state
{
    Texture   = <texBloom3>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerBloom4 = sampler_state
{
    Texture   = <texBloom4>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerBloom5 = sampler_state
{
    Texture   = <texBloom5>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerBloom6 = sampler_state
{
    Texture   = <texBloom6>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerBloom7 = sampler_state
{
    Texture   = <texBloom7>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

sampler2D SamplerBloom8 = sampler_state
{
    Texture   = <texBloom8>;
	MinFilter = LINEAR;
	MagFilter = LINEAR;
	MipFilter = NONE;//NONE;
	AddressU  = Clamp;
	AddressV  = Clamp;
	SRGBTexture=FALSE;
	MaxMipLevel=0;
	MipMapLodBias=0;
};

// STRUCTURES +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

struct VS_OUTPUT_POST
{
	float4 vpos  : POSITION;
	float2 txcoord0 : TEXCOORD0;
};

struct VS_INPUT_POST
{
	float3 pos  : POSITION;
	float2 txcoord0 : TEXCOORD0;
};

// VERTEX SHADER ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

VS_OUTPUT_POST VS_Bloom(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST OUT;
	OUT.vpos = float4(IN.pos.x, IN.pos.y, IN.pos.z, 1.0);
	OUT.txcoord0.xy = IN.txcoord0.xy + TempParameters.xy;

	return OUT;
};

// HELPER FUNCTIONS AND CONSTANTS +++++++++++++++++++++++++++++++++++++++++++++++++++++++

//Set loopcount limit - DO NOT CHANGE, EVER.
#define LOOPCOUNT	150

// PI, required to calculate weight
#define PI			3.1415926535897932384626433832795

float linearDepth(float d, float n, float f)
{
	return (2.0 * n)/(f + n - d * (f - n));
}

// SHADERS ++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

float4 PS_BloomPrePass(VS_OUTPUT_POST IN) : COLOR
{
	// --------------------------- //
	//   PREPASS EFFECT TO BLOOM   //
	// --------------------------- //

	// NOTES [prod80]
	// Initial image preparation for bloom
	// Using level controls here to manipulate input texture. Ie. This determines where bloom is applied and base texture intensity
	// Writes to texBloom1 which is used in Texture1 and Texture2 passes below that do the actual blooming
	
	float4 bC		= 0.0f;
	bC				= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy, 0, 0 ));
	
	// Pre corrections on bloom texture based on level controls
	float BinBlack	= lerp(	lerp(	BinBlackN,	BinBlackD,	ENightDayFactor	),	BinBlackI,	EInteriorFactor ); 
	float BinGamma	= lerp(	lerp(	BinGammaN,	BinGammaD,	ENightDayFactor	),	BinGammaI,	EInteriorFactor ); 
	float BinWhite	= lerp(	lerp(	BinWhiteN,	BinWhiteD,	ENightDayFactor	),	BinWhiteI,	EInteriorFactor );
	float BoutWhite	= lerp( lerp(	BoutWhiteN,	BoutWhiteD,	ENightDayFactor ),	BoutWhiteI,	EInteriorFactor );
	float BoutBlack	= lerp( lerp(	BoutBlackN,	BoutBlackD,	ENightDayFactor ),	BoutBlackI,	EInteriorFactor );
	
	bC.xyz			= pow( max( bC.xyz - BinBlack , 0.0f ) / max( BinWhite - BinBlack, 0.0001f ), BinGamma ) * max( BoutWhite - BoutBlack, 0.0001f ) + BoutBlack;
	
	bC.xyz			= saturate( bC.xyz );
	bC.w			= 1.0f;
	return bC;
}

// NOTES [ prod80 ]
/* PS_BloomTexture1 & PS_BloomTexture2 passes below here are ran twice.
 * First scale is 512px then target is swapped and run again to 256px, 128px, 64px and 32px textures

 * Input texture is texBloom1
 * Target texture of Texture1 Pass1 is a Temporary texture
 * Input texture on Texture1 Pass2 is the Temporary texture and output goes to texBloom1
 * Input texture on Texture2 Pass1 is texBloom1 and does same with Temporary texture
 * Final output goes to all textures in different scale
 
 * At this point still not sure if should use texBloom7, texBloom8 as quality is extremely low due to resolution.
 * This is most notable in situations where environment is very dark with bright source and bloom cause banding artefacts in shadows
 */

float4 PS_BloomTexture1(VS_OUTPUT_POST IN) : COLOR
{
	// --------------------------- //
	//    FIRST GAUSSIAN PASSES    //
	//   BLOOM TINT HAPPENS HERE   //
	// --------------------------- //
	
	float DSMULTI		= 1.0f;
	if ( USE_SS == true ) {
		float scaleX	= SSRESOLUTION.x / BASERESOLUTION.x;
		float scaleY	= SSRESOLUTION.y / BASERESOLUTION.y;
		DSMULTI			= min( scaleX, scaleY );
		}
	
	float Q				= 0.0f;
	if ( QUALITYLEVEL == 2 )
		Q = 0.6f;
	else if ( QUALITYLEVEL == 1 ) 
		Q = 0.8f;
	else if ( QUALITYLEVEL == 0 )
		Q = 0.985f;
	else if ( QUALITYLEVEL == -1 )
		Q = 0.9995f;
	else if (QUALITYLEVEL == -2 )
		Q = 0.999999997f;
	
	float hIntensity	= lerp( lerp( hIntensityN, hIntensityD, ENightDayFactor ), hIntensityI, EInteriorFactor );
	float vIntensity	= lerp( lerp( vIntensityN, vIntensityD, ENightDayFactor ), vIntensityI, EInteriorFactor );
	float BloomSigma	= lerp( lerp( BloomSigmaN, BloomSigmaD, ENightDayFactor ), BloomSigmaI, EInteriorFactor ) * DSMULTI;
	float minDepth		= lerp( minDepthE, minDepthI, EInteriorFactor );
	float px 			= ScreenSize.y;
	float sHeight		= ScreenSize.x * ScreenSize.w;
	float py 			= 1.0f / sHeight;
	
	// Get Depth info and apply to Sigma
	float Depth			= tex2Dlod( SamplerDepth, float4( IN.txcoord0.xy, 0, 0 )).x;
	float SigmaDepth	= min( max( linearDepth( Depth, 0.5f, zfar ), minDepth ), 1.0f );
	BloomSigma			= max( BloomSigma * SigmaDepth, 0.6f ); //limited to not give weird result
	
	// Declare all needed variables
	float SigmaSum;
	float3 Sigma;
	float sampleOffset;
	float4 bloom;
	float4 bloom1;
	float4 bloom2;
	float4 srcbloom;
	float d;
	float d1;
	float d2;
	
	// Get bloom
	bloom				= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy, 0, 0 ));
	srcbloom			= bloom;
	d					= linearDepth( Depth, 0.5f, 3500.0f );
	
	// PASS 1 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	if( TempParameters.w < 1.1 ) {
	
	SigmaSum			= 0.0f;
	Sigma.x				= 1.0f / ( sqrt( 2.0f * PI ) * BloomSigma );
	Sigma.y				= exp( -0.5f / ( BloomSigma * BloomSigma ));
	Sigma.z				= Sigma.y * Sigma.y;
	
	sampleOffset		= 1.0f;
	bloom				*= Sigma.x;
	SigmaSum			+= Sigma.x;
	Sigma.xy			*= Sigma.yz;
	
	for(int i = 1; i < LOOPCOUNT && SigmaSum < Q; ++i) {
		bloom1 			= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy + float2(sampleOffset*px, 0.0), 0, 0 )) * Sigma.x;
		bloom2 			= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy - float2(sampleOffset*px, 0.0), 0, 0 )) * Sigma.x;
		
		d1				= tex2Dlod(SamplerDepth, float4( IN.txcoord0.xy + float2(sampleOffset*px, 0.0), 0, 0 )).x;
		d1				= linearDepth( d1, 0.5f, 3500.0f );
		d2				= tex2Dlod(SamplerDepth, float4( IN.txcoord0.xy - float2(sampleOffset*px, 0.0), 0, 0 )).x;
		d2				= linearDepth( d2, 0.5f, 3500.0f );

		bloom			+= lerp( srcbloom * Sigma.x, bloom1, smoothstep( d-d1, d, d1 ));		
		bloom			+= lerp( srcbloom * Sigma.x, bloom2, smoothstep( d-d2, d, d2 ));

		SigmaSum		+= ( 2.0f * Sigma.x );
		sampleOffset	+= 1.0f;
		Sigma.xy		*= Sigma.yz;
	}
	bloom.xyz			/= SigmaSum;
	bloom.xyz 			*= hIntensity;
	}
	
	// PASS 2 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	if( TempParameters.w > 1.1 ) {
	
	SigmaSum			= 0.0f;
	Sigma.x				= 1.0f / ( sqrt( 2.0f * PI ) * BloomSigma );
	Sigma.y				= exp( -0.5f / ( BloomSigma * BloomSigma ));
	Sigma.z				= Sigma.y * Sigma.y;
	
	sampleOffset		= 1.0f;
	bloom				*= Sigma.x;
	SigmaSum			+= Sigma.x;
	Sigma.xy			*= Sigma.yz;
	
	for(int i = 1; i < LOOPCOUNT && SigmaSum < Q; ++i) {
		bloom1 			= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy + float2(0.0, sampleOffset*py), 0, 0 )) * Sigma.x;
		bloom2 			= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy - float2(0.0, sampleOffset*py), 0, 0 )) * Sigma.x;
		
		d1				= tex2Dlod(SamplerDepth, float4( IN.txcoord0.xy + float2(0.0, sampleOffset*py), 0, 0 )).x;
		d1				= linearDepth( d1, 0.5f, 3500.0f );
		d2				= tex2Dlod(SamplerDepth, float4( IN.txcoord0.xy - float2(0.0, sampleOffset*py), 0, 0 )).x;
		d2				= linearDepth( d2, 0.5f, 3500.0f );

		bloom			+= lerp( srcbloom * Sigma.x, bloom1, smoothstep( d-d1, d, d1 ));		
		bloom			+= lerp( srcbloom * Sigma.x, bloom2, smoothstep( d-d2, d, d2 ));		

		SigmaSum		+= ( 2.0f * Sigma.x );
		sampleOffset	+= 1.0f;
		Sigma.xy		*= Sigma.yz;
	}
	bloom.xyz			/= SigmaSum;
	bloom.xyz 			*= vIntensity;
	}
	
	//Bloom Tinting
	if (use_tinting==true) {
			
		float3 tint_color	= lerp( lerp( tint_colorN, tint_colorD, ENightDayFactor ), tint_colorI, EInteriorFactor );
		float tint_level	= lerp( lerp( tint_levelN, tint_levelD, ENightDayFactor ), tint_levelI, EInteriorFactor );
		float tint_mix		= lerp( lerp( tint_mixN, tint_mixD, ENightDayFactor ), tint_mixI, EInteriorFactor );
		
		float3 tintColor	= 0.0f;
		
		if (tint_type==1)
			tintColor = tint_color * tint_level;
		if (tint_type==2)
			tintColor = tex2Dlod(SamplerBloom6, float4( IN.txcoord0.xy, 0, 0 )).xyz * tint_level;
		if (tint_type==3) {
			float3 tcol1 	= tint_color;
			float3 tcol2 	= tex2Dlod(SamplerBloom6, float4( IN.txcoord0.xy, 0, 0 )).xyz;
			tintColor		= lerp( tcol1.xyz, tcol2.xyz, tint_mix ) * tint_level;
			}
		
		float ttt		= max( dot( bloom.xyz, 0.33333332f ) - dot( srcbloom.xyz, 0.33333332f ), 0.0f );
		float gray		= BloomParameters.z * ttt * 10.0f;
		float mixfact	= gray / ( gray + 1.0f );
		mixfact			*= 1.0f - saturate( ( TempParameters.w - 1.0f ) * 0.2f );
		tintColor.xy	+= 1.0f - saturate( ( TempParameters.w - 1.0f ) * 0.3f );
		tintColor.xy	= saturate( tintColor.xy );
		
		bloom.xyz		*= lerp( 1.0f, tintColor.xyz, mixfact );
	}
	bloom.xyz			= saturate( bloom.xyz );
	bloom.w				= 1.0f;
	
	return bloom;
}

float4 PS_BloomTexture2(VS_OUTPUT_POST IN) : COLOR
{
	// --------------------------- //
	//   SECOND GAUSSIAN PASSES    //
	// --------------------------- //
	
	float DSMULTI		= 1.0f;
	if ( USE_SS == true ) {
		float scaleX	= SSRESOLUTION.x / BASERESOLUTION.x;
		float scaleY	= SSRESOLUTION.y / BASERESOLUTION.y;
		DSMULTI			= min( scaleX, scaleY );
		}
		
	float Q				= 0.0f;
	if ( QUALITYLEVEL == 2 )
		Q = 0.6f;
	else if ( QUALITYLEVEL == 1 ) 
		Q = 0.8f;
	else if ( QUALITYLEVEL == 0 )
		Q = 0.985f;
	else if ( QUALITYLEVEL == -1 )
		Q = 0.9995f;
	else if (QUALITYLEVEL == -2 )
		Q = 0.999999997f;
	
	float hIntensity	= lerp( lerp( hIntensityN, hIntensityD, ENightDayFactor ), hIntensityI, EInteriorFactor );
	float vIntensity	= lerp( lerp( vIntensityN, vIntensityD, ENightDayFactor ), vIntensityI, EInteriorFactor );
	float BloomSigma	= lerp( lerp( BloomSigmaN, BloomSigmaD, ENightDayFactor ), BloomSigmaI, EInteriorFactor ) * DSMULTI;
	float minDepth		= lerp( minDepthE, minDepthI, EInteriorFactor );
	float px 			= ScreenSize.y;
	float sHeight		= ScreenSize.x * ScreenSize.w;
	float py 			= 1.0f / sHeight;
	
	// Get Depth info and apply to Sigma
	float Depth			= tex2Dlod( SamplerDepth, float4( IN.txcoord0.xy, 0, 0 )).x;
	float SigmaDepth	= min( max( linearDepth( Depth, 0.5f, zfar ), minDepth ), 1.0f );
	BloomSigma			= max( BloomSigma * SigmaDepth, 0.6f ); //limited to not give weird result
	
	// Declare all needed variables
	float SigmaSum;
	float3 Sigma;
	float sampleOffset;
	float4 bloom;
	float4 bloom1;
	float4 bloom2;
	float4 srcbloom;
	float d;
	float d1;
	float d2;
	
	// Get bloom
	bloom				= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy, 0, 0 ));
	srcbloom			= bloom;
	d					= linearDepth( Depth, 0.5f, 3500.0f );
	
	// Reverse of Texture1 pass
	// PASS 1 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	if( TempParameters.w > 1.1 ) {
	
	SigmaSum			= 0.0f;
	Sigma.x				= 1.0f / ( sqrt( 2.0f * PI ) * BloomSigma );
	Sigma.y				= exp( -0.5f / ( BloomSigma * BloomSigma ));
	Sigma.z				= Sigma.y * Sigma.y;
	
	sampleOffset		= 1.0f;
	bloom				*= Sigma.x;
	SigmaSum			+= Sigma.x;
	Sigma.xy			*= Sigma.yz;
	
	for(int i = 1; i < LOOPCOUNT && SigmaSum < Q; ++i) {
		bloom1 			= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy + float2(sampleOffset*px, 0.0), 0, 0 )) * Sigma.x;
		bloom2 			= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy - float2(sampleOffset*px, 0.0), 0, 0 )) * Sigma.x;
		
		d1				= tex2Dlod(SamplerDepth, float4( IN.txcoord0.xy + float2(sampleOffset*px, 0.0), 0, 0 )).x;
		d1				= linearDepth( d1, 0.5f, 3500.0f );
		d2				= tex2Dlod(SamplerDepth, float4( IN.txcoord0.xy - float2(sampleOffset*px, 0.0), 0, 0 )).x;
		d2				= linearDepth( d2, 0.5f, 3500.0f );

		bloom			+= lerp( srcbloom * Sigma.x, bloom1, smoothstep( d-d1, d, d1 ));	
		bloom			+= lerp( srcbloom * Sigma.x, bloom2, smoothstep( d-d2, d, d2 ));
		
		SigmaSum		+= ( 2.0f * Sigma.x );
		sampleOffset	+= 1.0f;
		Sigma.xy		*= Sigma.yz;
	}
	bloom.xyz			/= SigmaSum;
	bloom.xyz 			*= hIntensity;
	}
	
	// PASS 2 +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
	if( TempParameters.w < 1.1 ) {
	
	SigmaSum			= 0.0f;
	Sigma.x				= 1.0f / ( sqrt( 2.0f * PI ) * BloomSigma );
	Sigma.y				= exp( -0.5f / ( BloomSigma * BloomSigma ));
	Sigma.z				= Sigma.y * Sigma.y;
	
	sampleOffset		= 1.0f;
	bloom				*= Sigma.x;
	SigmaSum			+= Sigma.x;
	Sigma.xy			*= Sigma.yz;
	
	for(int i = 1; i < LOOPCOUNT && SigmaSum < Q; ++i) {
		bloom1 			= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy + float2(0.0, sampleOffset*py), 0, 0 )) * Sigma.x;
		bloom2 			= tex2Dlod(SamplerBloom1, float4( IN.txcoord0.xy - float2(0.0, sampleOffset*py), 0, 0 )) * Sigma.x;
		
		d1				= tex2Dlod(SamplerDepth, float4( IN.txcoord0.xy + float2(0.0, sampleOffset*py), 0, 0 )).x;
		d1				= linearDepth( d1, 0.5f, 3500.0f );
		d2				= tex2Dlod(SamplerDepth, float4( IN.txcoord0.xy - float2(0.0, sampleOffset*py), 0, 0 )).x;
		d2				= linearDepth( d2, 0.5f, 3500.0f );

		bloom			+= lerp( srcbloom * Sigma.x, bloom1, smoothstep( d-d1, d, d1 ));		
		bloom			+= lerp( srcbloom * Sigma.x, bloom2, smoothstep( d-d2, d, d2 ));		

		SigmaSum		+= ( 2.0f * Sigma.x );
		sampleOffset	+= 1.0f;
		Sigma.xy		*= Sigma.yz;
	}
	bloom.xyz			/= SigmaSum;
	bloom.xyz 			*= vIntensity;
	}
	bloom.xyz			= saturate( bloom.xyz );
	bloom.w				= 1.0f;
	
	return bloom;
}

float4 PS_BloomPostPass(VS_OUTPUT_POST IN) : COLOR
{
	// --------------------------- //
	//   POSTPASS TO COMBINE TEX   //
	// --------------------------- //

	// NOTES [prod80]
	// Mix and match some textures
	// Doing final saturation control to please the general crowd
	
	float4 bloom	= 0.0f;
	
	// Texture Select
	bloom += lerp( 0, ( tex2Dlod(SamplerBloom1, float4(IN.txcoord0.xy,0,0)) * tex_select1_int ), tex_select1 ); //Original ENB blurred input; pre-blurred
	bloom += lerp( 0, ( tex2Dlod(SamplerBloom2, float4(IN.txcoord0.xy,0,0)) * tex_select2_int ), tex_select2 ); //Downsampled to 512
	bloom += lerp( 0, ( tex2Dlod(SamplerBloom3, float4(IN.txcoord0.xy,0,0)) * tex_select3_int ), tex_select3 ); //Downsampled to 256
	bloom += lerp( 0, ( tex2Dlod(SamplerBloom4, float4(IN.txcoord0.xy,0,0)) * tex_select4_int ), tex_select4 ); //Downsampled to 128
	bloom += lerp( 0, ( tex2Dlod(SamplerBloom7, float4(IN.txcoord0.xy,0,0)) * tex_select5_int ), tex_select5 ); //Downsampled to 64
	bloom += lerp( 0, ( tex2Dlod(SamplerBloom8, float4(IN.txcoord0.xy,0,0)) * tex_select6_int ), tex_select6 ); //Downsampled to 32
	bloom += lerp( 0, ( tex2Dlod(SamplerBloom5, float4(IN.txcoord0.xy,0,0)) * tex_select7_int ), tex_select7 ); //Prepass result only @ 512
	bloom += lerp( 0, ( tex2Dlod(SamplerBloom6, float4(IN.txcoord0.xy,0,0)) * tex_select8_int ), tex_select8 ); //Original texture @ 512
	
	//Some mega elaborate calculation, does work fine though
	float divider		= tex_select1 + tex_select2 + tex_select3 + tex_select4 + tex_select5 + tex_select6 + tex_select7 + tex_select8;
	float int_sum		= ( tex_select1 * tex_select1_int ) + ( tex_select2 * tex_select2_int ) + ( tex_select3 * tex_select3_int ) + ( tex_select4 * tex_select4_int ) + ( tex_select5 * tex_select5_int ) + ( tex_select6 * tex_select6_int ) + ( tex_select7 * tex_select7_int ) + ( tex_select8 * tex_select8_int );
	float multiplier	= divider / int_sum;
	bloom.xyz			*= ( 1.0f / divider );
	bloom.xyz			*= multiplier;
	
	// Correction for saturation, required or not depends on at what point bloom is added in enbeffect.fx ( before or after game color corrections )
	float bsat		= lerp( lerp( bsatN, bsatD, ENightDayFactor ), bsatI, EInteriorFactor );
	float grbloom	= dot( bloom.xyz, float3( 0.2125, 0.7154, 0.0721 ));
	bloom.xyz		= lerp( grbloom, bloom.xyz, bsat );
	
	// Render depth to screen, for good visualization add to enbeffect.fx option to render only bloom to screen with return statement
	if (show_depth==true) {
		float minDepth	= lerp( minDepthE, minDepthI, EInteriorFactor );
		float depthTest = min( linearDepth( tex2Dlod( SamplerDepth, float4(IN.txcoord0.xy,0,0)).x, 0.5f, zfar ), 1.0f );
		bloom.xyz		= lerp( float3(1.0, 1.0, 1.0), float3(0.0, 0.0, 0.0), depthTest );
		if (depthTest < minDepth) bloom.xyz = ( bloom.xyz + float3(1.0, 0.0, 0.0) ) / 2.0f;
		}
	
	bloom.xyz		= saturate( bloom.xyz );	// clamp to 0..1 range	
	bloom.w			= 1.0f;						// alpha 1.0 or weirdness will hunt you down	

	//Final output of shader is 512x512 texture
	return bloom;
	
	// enbeffect.fx should process final bloom intensity using EBloomAmount
	// I have a bloody headache now, still have after many changes to this file, bloody file, bloody shite
}

// TECHNIQUES +++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

technique BloomPrePass
{
    pass p0
    {
		VertexShader = compile vs_3_0 VS_Bloom();
		PixelShader  = compile ps_3_0 PS_BloomPrePass();

		ColorWriteEnable = ALPHA|RED|GREEN|BLUE;
		CullMode = NONE;
		AlphaBlendEnable = FALSE;
		AlphaTestEnable = FALSE;
		SEPARATEALPHABLENDENABLE = FALSE;
		FogEnable = FALSE;
		SRGBWRITEENABLE = FALSE;
	}
}

technique BloomTexture1
{
    pass p0
    {
		VertexShader = compile vs_3_0 VS_Bloom();
		PixelShader  = compile ps_3_0 PS_BloomTexture1();

		ColorWriteEnable = ALPHA|RED|GREEN|BLUE;
		CullMode = NONE;
		AlphaBlendEnable = FALSE;
		AlphaTestEnable = FALSE;
		SEPARATEALPHABLENDENABLE = FALSE;
		FogEnable = FALSE;
		SRGBWRITEENABLE = FALSE;
	}
}

technique BloomTexture2
{
    pass p0
    {
		VertexShader = compile vs_3_0 VS_Bloom();
		PixelShader  = compile ps_3_0 PS_BloomTexture2();

		ColorWriteEnable = ALPHA|RED|GREEN|BLUE;
		CullMode = NONE;
		AlphaBlendEnable = FALSE;
		AlphaTestEnable = FALSE;
		SEPARATEALPHABLENDENABLE = FALSE;
		FogEnable = FALSE;
		SRGBWRITEENABLE = FALSE;
	}
}

technique BloomPostPass
{
    pass p0
    {
		VertexShader = compile vs_3_0 VS_Bloom();
		PixelShader  = compile ps_3_0 PS_BloomPostPass();

		ColorWriteEnable = ALPHA|RED|GREEN|BLUE;
		CullMode = NONE;
		AlphaBlendEnable = FALSE;
		AlphaTestEnable = FALSE;
		SEPARATEALPHABLENDENABLE = FALSE;
		FogEnable = FALSE;
		SRGBWRITEENABLE = FALSE;
	}
}
