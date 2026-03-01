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
// Prod80: Author of Color Correction shaders          //
// CeeJay.dk: Author of Levels shader.				   //
// Martinsh, MTichenor: Author of original Film Grain. //
// L00 :  AGCC, Shader Setup, Presets and Settings,    //
//        Port and Modification of Shaders             //
//        and author of this file                      //
/////////////////////////////////////////////////////////
//            PHOTOREALISTIC TAMRIEL X           	   //
						 12.7
/////////////////////////////////////////////////////////
//               	  INTERFACE                        //
///////////////////////////////////////////////////////*/

bool 	bFilmList 		<string UIName="      ::SHOW AVAILABLE FILMS TO LOAD:: ->"; > = {true};
int		iFilm 			<string UIName="Film to load"; string UIWidget="dropdown"; int UIMin=0; int UIMax=50;string UIList="None,Agfa APX 25 BW,Agfa Precisa 100,Agfa Scala 200 BW,Agfa Superia 200,Agfa Superia Raela 100,Agfa Ultra Color 100,Agfa Vista 100D,Agfa Vista 200D,CrossPro Kodak Elite 100,Fuji 513,Fuji Astia 100,Fuji Eterna 250D,Fuji FP 100C,Fuji Neopan 1600 BW,Fuji Provia 100,Fuji Provia 100F,Fuji Sensia 100,Fuji Superia 400,Fuji Superia HG 1600,Fuji Velvia 100,Ilford Delta 400 BW,Ilford HP5 Plus 400 BW,Ilford XP2 BW,Kodak 2293,Kodak 5247,Kodak Ektachrome 100,Kodak Ektar 100,Kodak Elite Chrome 160,Kodak Elite Chrome 200,Kodak Elite Color 200,Kodak Elite Extracolor 100,Kodak HIE Filtered BW,Kodak Kodachrome 25,Kodak Kodachrome 64, Kodak Kodachrome 100,Kodak Kodachrome 100F,Kodak Portra 160VC,Kodak T-MAX 100 BW,Lomography CN100,Lomography Redscale 100,Lomography XProslide 200,Polaroid 667 BW,Polaroid 669,Polaroid Polachrome,Polaroid PX70,Polaroid PX680,Portra 400UC,Rollei R400 BW,Rollei Retro 80S BW,Technicolor 4";> = {0};

float 	Empty_Row2 		<string UIName="  ";   string UIWidget="spinner";  				float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};

float 	Title_Row1 		<string UIName="                        ::CAMERA CONTROL::";   	string UIWidget="spinner";float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};
float 	Sensor			<string UIName="Sensor: Size&Quality                                   %";string UIWidget="spinner";			float UIMin=25.0f;float UIMax=100.0f;float UIStep=5.0f;> = {100.0}; 
float 	LeakSlider		<string UIName="Sensor: Light Leak                                       %";string UIWidget="spinner";			float UIMin=0.0f;float UIMax=200.0f;float UIStep=5.0f;> = {0.0}; 
bool 	USE_ISONOISE 	<string UIName="Film: Use ISO noise"; > = {true};

float 	Title_Row2 		<string UIName="                           ::DAY: 8am - 7pm::             ";   	string UIWidget="spinner";float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};
float 	FilmAmount 		<string UIName="Film intensity Day                                              %"; string UIWidget="Spinner";    	float UIMin=-100.0;float UIMax=100.0;float UIStep=5.0f;> = {50.0};   
float	ISOD 			<string UIName="ISO Day";string UIWidget="spinner";					float UIMin=100.0;float UIMax=12800.0;float UIStep=100.0f;> = {100.0};
float	EVD 			<string UIName="Exposure Compensation Day                EV+/-";string UIWidget="spinner";float UIMin=-2.0f;float UIMax=2.0f;float UIStep=0.1f;> = {0.0f};
float	ShutterD 		<string UIName="Shutter Speed Day                                    1/";string UIWidget="spinner";	float UIMin=05.0;float UIMax=2000.0;float UIStep=25.0f;> = {250.0f};
float 	WBD				<string UIName="White Balance Day                                  K%";string UIWidget="spinner";		float UIMin=-100.0f;float UIMax=100.0f;float UIStep=5.0f;> = {0.0f};

float 	Title_Row3 		<string UIName="                          ::NIGHT: 7pm - 8am::             ";   	string UIWidget="spinner";float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};
float 	FilmAmount_N 		<string UIName="Film intensity Night                                         %"; string UIWidget="Spinner";    	float UIMin=-100.0;float UIMax=100.0;float UIStep=5.0f;> = {50.0};   
float	ISON 			<string UIName="ISO Night";string UIWidget="spinner";					float UIMin=100.0;float UIMax=12800.0;float UIStep=100.0f;> = {100.0};
float	EVN 			<string UIName="Exposure Compensation Night              EV+/-";string UIWidget="spinner";float UIMin=-2.0f;float UIMax=2.0f;float UIStep=0.1f;> = {0.0f};
float	ShutterN 		<string UIName="Shutter Speed Night                                  1/";string UIWidget="spinner";	float UIMin=05.0;float UIMax=2000.0;float UIStep=25.0f;> = {250.0f};
float 	WBN				<string UIName="White Balance Night                                  K%";string UIWidget="spinner";		float UIMin=-100.0f;float UIMax=100.0f;float UIStep=5.0f;> = {0.0f};

float 	Title_Row4 		<string UIName="                                 ";   	string UIWidget="spinner";float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};
float 	FilmAmount_I 		<string UIName="Film intensity Interior                                  %"; string UIWidget="Spinner";    	float UIMin=-100.0;float UIMax=100.0;float UIStep=5.0f;> = {50.0};   
float	ISOI 			<string UIName="ISO Interior";string UIWidget="spinner";					float UIMin=100.0;float UIMax=12800.0;float UIStep=100.0f;> = {100.0};
float	EVI 			<string UIName="Exposure Compensation Interior              EV+/-";string UIWidget="spinner";float UIMin=-2.0f;float UIMax=2.0f;float UIStep=0.1f;> = {0.0f};
float	ShutterI 		<string UIName="Shutter Speed Interior                                  1/";string UIWidget="spinner";	float UIMin=05.0;float UIMax=2000.0;float UIStep=25.0f;> = {250.0f};
float 	WBI				<string UIName="White Balance Interior                                  K%";string UIWidget="spinner";		float UIMin=-100.0f;float UIMax=100.0f;float UIStep=5.0f;> = {0.0f};

float 	Empty_Row3 		<string UIName="   ";   string UIWidget="spinner";  				float UIMin=0.0;float UIMax=0.0;float UIStep=1.0;> = {0.0};

float 	Title_Row5 		<string UIName="                    ::CAMERA OPTIONS MENU::";   string UIWidget="spinner";float UIMin=0.0;float UIMax=0.0;float UIStep=1.0f;> = {0.0};
float 	BlackPoint		<string UIName="Contrast";string UIWidget="spinner";			float UIMin=-50.0;float UIMax=50.0;float UIStep=5.0f;> = {0.0}; 
float 	camSat			<string UIName="Saturation";string UIWidget="spinner";			float UIMin=0.0;float UIMax=2.0;float UIStep=0.05f;> = {1.0}; 

/////////////////////////////////////////////////////////
//               	INTERNAL SETTINGS                  //
/////////////////////////////////////////////////////////
//LUTS
#define TuningColorLUTTileAmountX 	4096 
#define TuningColorLUTTileAmountY 	64 
#define TuningColorLUTNorm        	float2(1.0/float(TuningColorLUTTileAmountX),1.0/float(TuningColorLUTTileAmountY))
#define CameraCount       			7
#define FilmCount       			51

//SATURATION&CONTRAST
#define camSaturation 				(camSat-1.0)
#define Levels_white_point 			255
#define Levels_black_point 			BlackPoint

//SENSOR
#define ESensorAmount 				lerp(7.0, 0.5, Sensor*0.01)
#define ESensorRange  				lerp(4.0, 1.0, Sensor*0.01)

//LEAKS
	//Bloom
#define	fLeakSaturation				1.5
#define fLeakTextureIntensityDay	1.2
#define fLeakTextureIntensityNight	1.5
#define fLeakTextureIntensityInterior	1.5
	//Lens
#define fLensleakIntensityDay		0.45
#define fLensleakIntensityNight		0.65
#define fLensleakIntensityInterior	0.65

//LENS DISTORTION
#define LCAStrength					7.0
#define Shift    					15.0

//ENB PARAMETERS
#define LUM_709 float3(0.2125, 0.7154, 0.0721)
#define black_point_float ( Levels_black_point / 255.0 )
#define PixelSize 		 float2(ScreenSize.y, ScreenSize.y * ScreenSize.z)

	
////////////////////////////////////////////////////////////////////////////////////////////////////
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
float4 Params01[7];  /// MOD PARAMATER, DO NOT MODIFY!
float4 ENBParams01;  /// x - bloom amount; y - lens amount

Texture2D			TextureColor; //hdr color
Texture2D			TextureBloom; //vanilla or enb bloom
Texture2D			TextureLens; //enb lens fx
Texture2D			TextureDepth; //scene depth
Texture2D			TextureAdaptation; //vanilla or enb adaptation
Texture2D			TextureAperture; //this frame aperture 1*1 R32F hdr red channel only. computed in depth of field shader file

//CC
Texture2D TextureLUTCC 						<string ResourceName = "Textures/Colorgrades/NATENB_CC.png";>;

//NIGHT EYE
Texture2D TextureNELUT 						<string ResourceName = "Textures/Colorgrades/NightEye.png";>;

//WHITE BALANCE
Texture2D TextureWB 						<string ResourceName = "Textures/Cameras/BalanceWhite.png";>;

///CAMERASX6
Texture2D TextureCameras 					<string ResourceName = "Textures/Cameras/PRCameras.png";>;

///FILMS X50
Texture2D TextureFilmList 					<string ResourceName = "Textures/Overlays/List_Films.png";>;
Texture2D TextureFilms 						<string ResourceName = "Textures/Cameras/PRFilms.png";>;

///LensLeak
Texture2D TextureLeakD 						<string ResourceName = "Textures/Lenses/LeakD.jpg";>;
Texture2D TextureLeakN 						<string ResourceName = "Textures/Lenses/LeakN.jpg";>;
Texture2D TextureLeakI 						<string ResourceName = "Textures/Lenses/LeakI.jpg";>;
Texture2D TextureDefectPola 				<string ResourceName = "Textures/Lenses/DefectInsta.jpg";>;

Texture2D TextureIMG_debug 					<string ResourceName = "Textures/Overlays/NATENB_BadInstall.png";>;

/////////////////////////////////////////////////////////
//               	    SAMPLERS                       //
/////////////////////////////////////////////////////////

SamplerState SamplerPoint
{
  Filter=MIN_MAG_MIP_POINT;  AddressU=Clamp;  AddressV=Clamp;  // MIN_MAG_MIP_LINEAR;
};
SamplerState SamplerLinear
{
  Filter=MIN_MAG_MIP_LINEAR;  AddressU=Clamp;  AddressV=Clamp;
};
SamplerState SamplerLut
{
   Filter=MIN_MAG_MIP_LINEAR;  AddressU=Clamp;  AddressV=Clamp;
};
SamplerState SamplerNELut
{
   Filter=MIN_MAG_MIP_LINEAR;  AddressU=Clamp;  AddressV=Clamp;
};
SamplerState SamplerLeak
{
   Filter=MIN_MAG_MIP_LINEAR;  AddressU=Clamp;  AddressV=Clamp;
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


/////////////////////////////////////////////////////////
/// HELPER FUNCTIONS //
//CC
#define satLimit 0.1
// DITHER
#define remap(v, a, b) (((v) - (a)) / ((b) - (a)))

static const float3 tint_color 			= Params01[4].rgb; 
static const float  tint_weight 		= Params01[4].w;
static const float3 fade        		= Params01[5].xyz; 
static const float  fade_weight 		= min(Params01[5].w,0.1f);
static const bool   isHavingGameFx 		= fade_weight > 0.0f;
static const float  fadeWeightFactor 	= fade_weight*5.0;
static const float  fadeWeightFactorHalf = fade_weight*2.0;

static const float 	GameSaturation 		= 0.9 + Params01[3].x;
static const float 	gameBrightness 		= saturate(Params01[3].w);
static const float 	gameContrast 		= min(Params01[3].z - 0.25f,2.0f);
static const float 	gameCurve  			= Params01[3].z;
//static const float rBloomThreshold  = Params01[2].x;

static const bool isBlackreach 			= Weather.x == 41;
static const bool isSoulCairn 			= Weather.x == 39;
static const bool isBroken 				= Weather.x < 1;

//// TODIE HELPER MACRO
static const float timeweight() {
    return TimeOfDay1.x +
           TimeOfDay1.y +
           TimeOfDay1.z +
           TimeOfDay1.w +
           TimeOfDay2.x +
           TimeOfDay2.y;
}

#define TOD(a) \
  ((TimeOfDay1.x * a##_Dawn    + \
    TimeOfDay1.y * a##_Sunrise + \
    TimeOfDay1.z * a##_Day     + \
    TimeOfDay1.w * a##_Sunset  + \
    TimeOfDay2.x * a##_Dusk    + \
    TimeOfDay2.y * a##_Night) / timeweight())

#define TODIE(a) lerp( TOD(a), a##_Interior, EInteriorFactor )

///DEPTH
float linearDepth(float nonLinDepth, float depthNearVar, float depthFarVar)
	{
	  return (2.0 * depthNearVar) / (depthFarVar + depthNearVar - nonLinDepth * (depthFarVar - depthNearVar));
	}
	
///COLOR SPACES	
float grayValue(float3 gv)
{
   return dot( gv, float3(0.2125, 0.7154, 0.0721) );
}

float smootherstep(float edge0, float edge1, float x)
{
   x = clamp((x - edge0)/(edge1 - edge0), 0.0, 1.0);
   return x*x*x*(x*(x*6 - 15) + 10);
}

float Hue(float3 color)
{
   float hue = 0.0f;
   float fmin = min(min(color.r, color.g), color.b);
   float fmax = max(max(color.r, color.g), color.b);
   float delta = fmax - fmin;
   
   if (delta == 0.0)
      hue = 0.0;
   else
   {         
      float deltaR = (((fmax - color.r) / 6.0) + (delta / 2.0)) / delta;
      float deltaG = (((fmax - color.g) / 6.0) + (delta / 2.0)) / delta;
      float deltaB = (((fmax - color.b) / 6.0) + (delta / 2.0)) / delta;

      if (color.r == fmax )
         hue = deltaB - deltaG;
      else if (color.g == fmax)
         hue = (1.0 / 3.0) + deltaR - deltaB;
      else if (color.b == fmax)
         hue = (2.0 / 3.0) + deltaG - deltaR;
   }
      
   if (hue < 0.0)
      hue += 1.0f;
   else if (hue > 1.0)
      hue -= 1.0f;
   return hue;
}


float3 HUEToRGB( in float H )
{
    return saturate( float3( abs( H * 6.0f - 3.0f ) - 1.0f,
                                  2.0f - abs( H * 6.0f - 2.0f ),
                                  2.0f - abs( H * 6.0f - 4.0f )));
}

float3 RGBToHCV( in float3 RGB )
{
    // Based on work by Sam Hocevar and Emil Persson
    float4 P         = ( RGB.g < RGB.b ) ? float4( RGB.bg, -1.0f, 2.0f/3.0f ) : float4( RGB.gb, 0.0f, -1.0f/3.0f );
    float4 Q1        = ( RGB.r < P.x ) ? float4( P.xyw, RGB.r ) : float4( RGB.r, P.yzx );
    float C          = Q1.x - min( Q1.w, Q1.y );
    float H          = abs(( Q1.w - Q1.y ) / ( 6.0f * C + 0.000001f ) + Q1.z );
    return float3( H, C, Q1.x );
}

float3 RGBToHSL( in float3 RGB )
{
    RGB.xyz          = max( RGB.xyz, 0.000001f );
    float3 HCV       = RGBToHCV(RGB);
    float L          = HCV.z - HCV.y * 0.5f;
    float S          = HCV.y / ( 1.0f - abs( L * 2.0f - 1.0f ) + 0.000001f);
    return float3( HCV.x, S, L );
}

float3 HSLToRGB( in float3 HSL )
{
    float3 RGB       = HUEToRGB(HSL.x);
    float C          = (1.0f - abs(2.0f * HSL.z - 1.0f)) * HSL.y;
    return ( RGB - 0.5f ) * C + HSL.z;
}

///NOISE
float random(in float2 uv)
{
    float2 noise = (frac(sin(dot(uv , float2(12.9898,78.233) * 2.0)) * 43758.5453));
    return abs(noise.x + noise.y) * 0.5;
}

/// LENS C.A
   float2 barrelDistortion( float2 p, float2 amt )
{
    p = 2.0 * p - 1.0;
    float maxBarrelPower = sqrt(5.0);
    float radius = dot(p,p); //faster but doesn't match above accurately
    p *= pow(radius, maxBarrelPower * amt);
	

    return p * 0.5 + 0.5;
}

float2 brownConradyDistortion(float2 uv, float scalar)
{
    uv = (uv - 0.5 ) * 2.0;
    
    if( true )
    {
        float barrelDistortion1 = -0.02 * scalar; // K1 in text books
        float barrelDistortion2 = 0.0 * scalar; // K2 in text books

        float r2 = dot(uv,uv);
        uv *= 1.0 + barrelDistortion1 * r2 + barrelDistortion2 * r2 * r2;
       
    }
    
   return (uv / 2.0) + 0.5;
}

float3 LensCA( float2 uv )
{
	
    float maxDistort = fadeWeightFactor*4.0;

    float scalar = 1.0 * maxDistort;
    float4 colourScalar = float4(700.0, 560.0, 490.0, 1.0);
    colourScalar /= max(max(colourScalar.x, colourScalar.y), colourScalar.z);
    colourScalar *= 2.0;
    
    colourScalar *= scalar;

    const float numTaps = 8.0;
    
    float3 fragColor = 0.0;
    for( float tap = 0.0; tap < numTaps; tap += 1.0 )
    {
        fragColor.r += TextureColor.Sample(SamplerLinear, brownConradyDistortion(uv, colourScalar.r)).r;
        fragColor.g += TextureColor.Sample(SamplerLinear, brownConradyDistortion(uv, colourScalar.g)).g;
        fragColor.b += TextureColor.Sample(SamplerLinear, brownConradyDistortion(uv, colourScalar.b)).b;
     
        colourScalar *= 0.95;
    }
    
    fragColor /= numTaps;
  
    return fragColor;
}	

float3 ChromaticAberration(float3 colorInput, float2 inTexCoords)
	{
		float3 color;

		color.r = TextureColor.Sample(SamplerPoint, inTexCoords + (PixelSize * 1.5)).r;
		color.g = colorInput.g;
		color.b = TextureColor.Sample(SamplerPoint, inTexCoords - (PixelSize * 1.5)).b;

		return lerp(colorInput, color, fadeWeightFactor);
	}

//DITHERING BY SANDVICH MAKER
float rand21(float2 coord)
{
    float2 noise = frac(sin(dot(coord, float2(12.9898, 78.233) * 2.0)) * 43758.5453);
    return (noise.x + noise.y) * 0.5;
}

float rand11(float x)
{
    return frac(x * 0.024390243);
}

float permute(float x)
{
    return ((34.0 * x + 1.0) * x) % 289.0;
}

float3 Dither(float3 color, float2 uv, int depth)
{
    const float bitstep = exp2(depth) - 1.0; // pow(2.0, depth) - 1.0;
    const float lsb     = 1.0 / bitstep;
    const float lobit   = 0.5 / bitstep;
    const float hibit   = (bitstep - 0.5) / bitstep;

    float3 m   = float3(uv, rand21(uv + Timer.x)) + 1.0;
    float  h   = permute(permute(permute(m.x) + m.y) + m.z);

    float3 noise1, noise2;
    noise1.x   = rand11(h); h = permute(h);
    noise2.x   = rand11(h); h = permute(h);
    noise1.y   = rand11(h); h = permute(h);
    noise2.y   = rand11(h); h = permute(h);
    noise1.z   = rand11(h); h = permute(h);
    noise2.z   = rand11(h);

    float3 lo  = saturate(remap(color.xyz, 0.0, lobit));
    float3 hi  = saturate(remap(color.xyz, 1.0, hibit));
    float3 uni = noise1 - 0.5;
    float3 tri = noise1 - noise2;

    return float3(lerp(uni.x, tri.x, min(lo.x, hi.x)),
                  lerp(uni.y, tri.y, min(lo.y, hi.y)),
                  lerp(uni.z, tri.z, min(lo.z, hi.z))) * lsb;
}	

	
// 	Tetrahedral interpolated MultiLUT by kingeric (Nov 5th, 2022)

class Texture2DAtlas
{
    Texture2D tex;
    int slice;
    
    float4 Load(int3 pos, int3 size)
    {
        return tex.Load(int3(pos.x + size.x * pos.z, pos.y + size.y * slice, 0));
    }

    #define Texture2DAtlas(a,b) Texture2DAtlas::ctor((a),(b))
    static  Texture2DAtlas ctor(Texture2D t, int k)
    {
        Texture2DAtlas r = {t, k}; return r;
    }
};

#define _TETRATEX Texture2DAtlas
#define _TETRALOAD(a, b, c) (a).Load((b), (c))

float3 TetraLut(_TETRATEX texIn, float3 rgb, uint3 size)
{
    float3 d =  rgb * (size - 1);
    uint3  i =  d, p00, p11;
    uint2  j = {1,0};
    bool3  b = (d -= i) >= d.gbr;

    [flatten] // should flatten itself without annotation.
    if (b.x)  // x >= y
    {
        [flatten]
        if      (b.y) d = d.xyz, p00 = j.xyy, p11 = j.xxy; // xyz
        else if (b.z) d = d.zxy, p00 = j.yyx, p11 = j.xyx; // zxy
        else          d = d.xzy, p00 = j.xyy, p11 = j.xyx; // xzy
    }

    else // y > x
    {
        [flatten]
        if      (!b.y) d = d.zyx, p00 = j.yyx, p11 = j.yxx; // zyx
        else if (!b.z) d = d.yxz, p00 = j.yxy, p11 = j.xxy; // yxz
        else           d = d.yzx, p00 = j.yxy, p11 = j.yxx; // yzx
    }

    return mul(float4(1. - d.x, d.z, d.xy - d.yz),
               float4x3(_TETRALOAD(texIn, i + j.y, size).rgb,
                        _TETRALOAD(texIn, i + j.x, size).rgb,
                        _TETRALOAD(texIn, i + p00, size).rgb,
                        _TETRALOAD(texIn, i + p11, size).rgb));
}

// clean up
#undef _TETRATEX
#undef _TETRALOAD

float3 MultiLUT_T(float3 color, int index, int dni)
{
    class func_t
    {
        static float3 run(Texture2D t)
        {
            int2 tex_size;
            t.GetDimensions(tex_size.x, tex_size.y);

            return saturate(TetraLut(Texture2DAtlas(t, dni), color, sqrt(tex_size.x).xxx));
        }
    };

    switch(index)
    {
        case  0: return func_t::run(TextureLUTCC);
        default: return color;
    }
}


/////////////////////////////////////////////////////////

/////////////////////////////////////////////////////////
//               	  PIXEL SHADER                     //
/////////////////////////////////////////////////////////
VS_OUTPUT_POST	VS_Draw(VS_INPUT_POST IN)
{
	VS_OUTPUT_POST	OUT;
	float4	pos;
	pos.xyz=IN.pos.xyz;
	pos.w=1.0;
	OUT.pos=pos;
	OUT.txcoord0.xy=IN.txcoord.xy;
	return OUT;
}

float4 PS_PRT(VS_OUTPUT_POST IN, float4 v0 : SV_Position0, uniform int iCamera) : SV_Target
{

	#if (Levels_white_point == Levels_black_point)
		#define white_point_float ( 255.0 / 0.00025)
	#else
		#define white_point_float ( 255.0 / (Levels_white_point - Levels_black_point))
	#endif

////////////////////////////////////////////////////////////////////////////////
	float Gamma_Day = 1.4;
	float Gamma_Night = 1.2;
	float Gamma_Interior = 1.0;
	float Gamma_Dawn = (Gamma_Day+Gamma_Night)/2.0;
	float Gamma_Sunrise = Gamma_Day;
	float Gamma_Sunset = Gamma_Day;
	float Gamma_Dusk = (Gamma_Day+Gamma_Night)/2.0;
		
	float AdaptationMin_Day=0.001;
	float AdaptationMin_Night=0.002;
	float AdaptationMin_Interior=0.01;
	float AdaptationMin_Sunrise = (AdaptationMin_Day+AdaptationMin_Night)/2.0;//AdaptationMin_Day+0.0005;
	float AdaptationMin_Sunset = (AdaptationMin_Day+AdaptationMin_Night)/2.0;//AdaptationMin_Day+0.0005;
	float AdaptationMin_Dawn = AdaptationMin_Night;//(AdaptationMin_Sunrise+AdaptationMin_Night)/2.0;
	float AdaptationMin_Dusk = AdaptationMin_Night;//(AdaptationMin_Sunset+AdaptationMin_Night)/2.0;

	float Brightness_Day=1.0;
	float Brightness_Night=0.5;
	float Brightness_Interior=0.5;
	float Brightness_Dawn = (Brightness_Day+Brightness_Night)/2.0;
	float Brightness_Sunrise = Brightness_Day;
	float Brightness_Sunset = Brightness_Day;
	float Brightness_Dusk = (Brightness_Day+Brightness_Night)/2.0;
	if (isBlackreach) Brightness_Interior = 1.1;
	
	float Saturation_Day=2.6;
	float Saturation_Night=2.4;
	float Saturation_Interior= Saturation_Day;	
	float Saturation_Dawn = (Saturation_Day+Saturation_Night)/2.0;
	float Saturation_Sunrise = Saturation_Day;
	float Saturation_Sunset = Saturation_Day;
	float Saturation_Dusk = (Saturation_Day+Saturation_Night)/2.0;
	
/* 	float IntensityContrast_Day=1.25;
	float IntensityContrast_Night=1.05;
	float IntensityContrast_Interior=1.2;
	float IntensityContrast_Dawn = (IntensityContrast_Day+IntensityContrast_Night)/2.0;
	float IntensityContrast_Sunrise = IntensityContrast_Day;
	float IntensityContrast_Sunset = IntensityContrast_Day;
	float IntensityContrast_Dusk = (IntensityContrast_Day+IntensityContrast_Night)/2.0; */
	
	float ToneMappingCurve_Day=0.9;
	float ToneMappingCurve_Night=1.0;
	float ToneMappingCurve_Interior=0.8;
	float ToneMappingCurve_Sunrise = ToneMappingCurve_Day-0.05;
	float ToneMappingCurve_Sunset = ToneMappingCurve_Day-0.05;
	float ToneMappingCurve_Dawn = (ToneMappingCurve_Sunrise+ToneMappingCurve_Night)/2.0;
	float ToneMappingCurve_Dusk = (ToneMappingCurve_Sunset+ToneMappingCurve_Night)/2.0;
	
	float RedFilter_Day=1.0;
	float RedFilter_Night=1.0;
	float RedFilter_Interior=1.0;
	float RedFilter_Dawn = (RedFilter_Day+RedFilter_Night)/2.0;
	float RedFilter_Sunrise = RedFilter_Day;
	float RedFilter_Sunset = RedFilter_Day;
	float RedFilter_Dusk = (RedFilter_Day+RedFilter_Night)/2.0;

	float BlueFilter_Day=1.0;
	float BlueFilter_Night=0.980;
	float BlueFilter_Interior=1.0;
	float BlueFilter_Dawn = (BlueFilter_Day+BlueFilter_Night)/2.0;
	float BlueFilter_Sunrise = BlueFilter_Day;
	float BlueFilter_Sunset = BlueFilter_Day;
	float BlueFilter_Dusk = (BlueFilter_Day+BlueFilter_Night)/2.0;
	
	float DesatG_Day= 0.12;
	float DesatG_Night=0.1;
	float DesatG_Interior=0.15;
	float DesatG_Dawn = (DesatG_Day+DesatG_Night)/2.0;
	float DesatG_Sunrise = DesatG_Day;//0.12;
	float DesatG_Sunset = DesatG_Day;//0.2;
	float DesatG_Dusk = (DesatG_Day+DesatG_Night)/2.0;
		
	float DesatB_Day=0.7;
	float DesatB_Night=0.65;
	float DesatB_Interior=0.7;
	float DesatB_Dawn = (DesatB_Day+DesatB_Night)/2.0;
	float DesatB_Sunrise = 0.6;
	float DesatB_Sunset = 0.6;
	float DesatB_Dusk = (DesatB_Day+DesatB_Night)/2.0;
	
	float EV_Day=EVD;
	float EV_Night=EVN;
	float EV_Interior=EVI;
	float EV_Dawn = (EV_Day+EV_Night)/2.0;
	float EV_Sunrise = EV_Day;
	float EV_Sunset = EV_Day;
	float EV_Dusk = (EV_Day+EV_Night)/2.0;
	
	float ISO_Day=ISOD;
	float ISO_Night=ISON;
	float ISO_Interior=ISOI;
	float ISO_Dawn = (ISO_Day+ISO_Night)/2.0;
	float ISO_Sunrise = ISO_Day;
	float ISO_Sunset = ISO_Day;
	float ISO_Dusk = (ISO_Day+ISO_Night)/2.0;
	
	float Shutter_Day=ShutterD;
	float Shutter_Night=ShutterN;
	float Shutter_Interior=ShutterI;
	float Shutter_Dawn = (Shutter_Day+Shutter_Night)/2.0;
	float Shutter_Sunrise = Shutter_Day;
	float Shutter_Sunset = Shutter_Day;
	float Shutter_Dusk = (Shutter_Day+Shutter_Night)/2.0;

////////////////////////////////////////////////////////////////////////////////////////////
	
	float4	res;
	float4	color;
	
	color=  TextureColor.Sample(SamplerPoint, IN.txcoord0.xy);
	float3	bloom=TextureBloom.Sample(SamplerLinear, IN.txcoord0.xy);
	float3	lens=TextureLens.Sample(SamplerLinear, IN.txcoord0.xy).xyz;
	float4	Adaptation=TextureAdaptation.Sample(SamplerPoint, IN.txcoord0.xy).x;
	float2 	TexCoords= IN.txcoord0.xy;

// GAME TINT
	if (!isHavingGameFx) {
		color.a   = dot(color.rgb, LUM_709);
		float fTintPercent_Dawn=0.75;
		float fTintPercent_Sunrise=0.45;
		float fTintPercent_Day=0.1;
			if (isSoulCairn) fTintPercent_Day*=2.0;
		float fTintPercent_Sunset=0.65;
		float fTintPercent_Dusk=0.7;
		float fTintPercent_Night=0.25;
		float fTintPercent_Interior=0.25;

		color.rgb = lerp(color.rgb, tint_color * color.a , tint_weight * TODIE(fTintPercent));	
		color.a = 1.0;
	}

/// ENB Adaptation
	float	grayadaptation=max(max(Adaptation.x, Adaptation.y), Adaptation.z);
	float EV   			= TODIE(EV);
	float EVin=EV;
	EVin*=-1;

/// SENSOR
	float3 	SensorColor=0.0;

	float2	pixeloffset=ScreenSize.y;
	pixeloffset.y*=ScreenSize.z;

	float2	offsets[4]=
	{
		float2(-1.0,-1.0),
		float2(-1.0, 1.0),
		float2( 1.0,-1.0),
		float2( 1.0, 1.0),
	};
	
	for (int i=0; i<4; i++)
	{
		float2	coord=offsets[i].xy * pixeloffset.xy * ESensorRange + IN.txcoord0.xy;
		SensorColor.rgb+=TextureColor.Sample(SamplerLinear, coord.xy);
	}
	SensorColor.rgb *= 0.25;

	float	diffgray=saturate(dot((color.xyz-SensorColor.rgb), LUM_709));
	color.xyz=ESensorAmount * color.xyz*diffgray + color.xyz;

///BLOOM
	bloom.xyz=pow(bloom,0.9);
	bloom.xyz=max(bloom, 0.0);
	bloom*=ENBParams01.x+fade_weight+0.1f;
	color.xyz = color + bloom - color * bloom;

/// ISO NOISE
	float ISO  			= TODIE(ISO);
	if (USE_ISONOISE) {
		float3 	IsoNoiseColor = color;
		
		float 	fGrainIntensity = min(ISO*lerp(lerp(0.000005,0.00001,ENightDayFactor),0.000005,EInteriorFactor),0.02); //.035

		float	fGrainSaturation = 0.9f; //0.64
		
		float  	GrainTimerSeed    = Timer.x;
		float2 	GrainTexCoordSeed = TexCoords.xy * 1.0f;

		float2 	GrainSeed1  = GrainTexCoordSeed + float2( 0.0f, GrainTimerSeed );
		float2 	GrainSeed2  = GrainTexCoordSeed + float2( GrainTimerSeed, 0.0f );
		float2 	GrainSeed3  = GrainTexCoordSeed + float2( GrainTimerSeed, GrainTimerSeed );
		float  	GrainNoise1 = random( GrainSeed1 );
		float  	GrainNoise2 = random( GrainSeed2 );
		float  	GrainNoise3 = random( GrainSeed3 );
		float  	GrainNoise4 = ( GrainNoise1 + GrainNoise2 + GrainNoise3 ) * 0.333333333f;
		float3 	GrainNoise  = float3( GrainNoise4, GrainNoise4, GrainNoise4 );
		float3 	GrainColor  = float3( GrainNoise1, GrainNoise2, GrainNoise3 );

			
		IsoNoiseColor += ( lerp( GrainNoise, GrainColor, fGrainSaturation ) * fGrainIntensity ) - ( fGrainIntensity * 0.5f);
		color.rgb = IsoNoiseColor;
	}
	
/// RGB GAMMA	
	float 	greyscale = dot(color.xyz, float3(0.3, 0.59, 0.11));
	color.g = lerp(greyscale, color.g, TODIE(DesatG));
	color.b = lerp(greyscale, color.b, TODIE(DesatB));	
			
	color = pow(color, TODIE(Gamma)-(ISO*0.00005)); ////////////sat?
		
	color.r = pow(color.r, TODIE(RedFilter));
	color.b = pow(color.b, TODIE(BlueFilter));

/// 1rst TONEMAPPING & CAMERA STUFF pass
	//SHUTTER
	float Shutter   	= TODIE(Shutter);
	grayadaptation=max(grayadaptation, 0.0); //0.0
	grayadaptation=min(grayadaptation, 50.0); //50.0
	color.xyz        = color.xyz / (grayadaptation * (lerp(0.0001f, 0.0002f,ENightDayFactor)+(Shutter/1000)) + TODIE(AdaptationMin));
	
	//EV
	float fContrastModifier = BlackPoint*0.001;
	float IntensityContrast;
	
	if (BlackPoint >= 0.0) IntensityContrast = 1.025+fContrastModifier;
	else IntensityContrast = 1.025-fContrastModifier;
	
	if (EV>=0.0f)	color.xyz		*= pow(2.0f, EV*0.66);
	if (EV<0.0f){	
					color.xyz		*= pow(2.0f, EV*0.33);
					IntensityContrast -=EVin*0.2;
	}
	
	//ISO
	color.xyz*=(TODIE(Brightness)+(ISO*0.00015)*max(gameBrightness,0.5f))+fadeWeightFactor;
	color.xyz+=0.000001;
	float3 xncol=normalize(color.xyz);
	float3 scl=color.xyz/xncol.xyz;
	scl=pow(scl, IntensityContrast+(ISO*0.000005));

	xncol.xyz=pow(xncol.xyz, TODIE(Saturation)-fade_weight);
	color.xyz=scl*xncol.xyz;
    color.xyz=(color.xyz * (1.0 + color.xyz/333333))/(color.xyz + max(TODIE(ToneMappingCurve)-fadeWeightFactor,0.01)); ///Sat?

// LENS LEAK
	float 	LeakGamma_Day=4.0;
	float 	LeakGamma_Night=1.0; 
	float 	LeakGamma_Interior=1.5; 
	float 	LeakGamma_Sunrise=8.0;
	float 	LeakGamma_Sunset = LeakGamma_Sunrise;
	float 	LeakGamma_Dawn = 2.0;
	float 	LeakGamma_Dusk = LeakGamma_Dawn;

	lens.xyz=pow(lens.xyz,TODIE(LeakGamma));
	color.xyz += (lens.xyz * lerp(lerp(0.2,0.1,ENightDayFactor),0.2,EInteriorFactor)) * (LeakSlider*0.033);
	
	float	todfLensleakIntensity=lerp(lerp(fLensleakIntensityNight, fLensleakIntensityDay, ENightDayFactor), fLensleakIntensityInterior, EInteriorFactor);
	
	float3 dirttexD = TextureLeakD.Sample(SamplerLeak, IN.txcoord0.xy);
	float3 dirttexN = TextureLeakN.Sample(SamplerLeak, IN.txcoord0.xy);
	float3 dirttexI = TextureLeakI.Sample(SamplerLeak, IN.txcoord0.xy);
	
	float3 leaktex =lerp(lerp(dirttexN, dirttexD, ENightDayFactor), dirttexI, EInteriorFactor);
	float3 lensleak = leaktex.xyz*lens*(todfLensleakIntensity*(LeakSlider*0.01));
						
	float   tempnor2=(lens/(1.0+lens));
	tempnor2=pow(tempnor2, lens.x);
	lens*=tempnor2 * lens.z;

	lensleak = lerp(dot(lensleak.xyz,0.333), lensleak.xyz, 2.0);
	color.xyz = max(color.xyz, lensleak.xyz);   

/// AGCC TINT & FADE FOR SFX
	if (isHavingGameFx) {
		color.a   = dot(color.xyz, LUM_709);
		color.xyz = lerp(color.xyz, tint_color * color.a, tint_weight);
		color.xyz = lerp(color.xyz, fade, fade_weight);
		color.a = 1.0;
	}

/// PROD80 COLOR EQ	
   float3 fxcolor = saturate( color.xyz );
   float greyVal = grayValue( fxcolor.xyz );
   float colorHue = Hue( fxcolor.xyz );
   
   float colorSat = 0.0f;
   float minColor = min( min ( fxcolor.x, fxcolor.y ), fxcolor.z );
   float maxColor = max( max ( fxcolor.x, fxcolor.y ), fxcolor.z );
   float colorDelta = maxColor - minColor;
   float colorInt = ( maxColor + minColor ) * 0.5f;
   
   if ( colorDelta != 0.0f )
   {
      if ( colorInt < 0.5f )
         colorSat = colorDelta / ( maxColor + minColor );
      else
         colorSat = colorDelta / ( 2.0f - maxColor - minColor );
   }
   
	colorSat = 1.0f;
   
   float hueMin_1 = 0.0f;
   float hueMin_2 = 0.0f;
   float hueMax_1 = 0.0f;
   float hueMax_2 = 0.0f;
   
   if ( 0.5 > 0.45 )
   {
      hueMin_1 = 0.45 - 0.5;
      hueMin_2 = 1.0f + 0.45 - 0.5;
      hueMax_1 = 0.45 + 0.5;
      hueMax_2 = 1.0f + 0.45;
   
      if ( colorHue >= hueMin_1 && colorHue <= 0.45 )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_1, 0.45, colorHue ) * ( colorSat * satLimit ));
      else if ( colorHue > 0.45 && colorHue <= hueMax_1 )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, ( 1.0f - smootherstep( 0.45, hueMax_1, colorHue )) * ( colorSat * satLimit ));
      else if ( colorHue >= hueMin_2 && colorHue <= hueMax_2 )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_2, hueMax_2, colorHue ) * ( colorSat * satLimit ));
      else
         fxcolor.xyz = greyVal.xxx;
   
   }
   else if ( 0.45 + 0.5 > 1.0f )
   {
      hueMin_1 = 0.45 - 0.5;
      hueMin_2 = 0.0f - ( 1.0f - 0.45 );
      hueMax_1 = 0.45 + 0.5;
      hueMax_2 = 0.45 + 0.5 - 1.0f;
   
      if ( colorHue >= hueMin_1 && colorHue <= 0.45 )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_1, 0.45, colorHue ) * ( colorSat * satLimit ));
      else if ( colorHue > 0.45 && colorHue <= hueMax_1 )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, ( 1.0f - smootherstep( 0.45, hueMax_1, colorHue )) * ( colorSat * satLimit ));
      else if ( colorHue >= hueMin_2 && colorHue <= hueMax_2 )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_2, hueMax_2, colorHue) * ( colorSat * satLimit ));
      else
         fxcolor.xyz = greyVal.xxx;
      
   }
   else
   {
      hueMin_1 = 0.45 - 0.5;
      hueMax_1 = 0.45 + 0.5;
      
      if ( colorHue >= hueMin_1 && colorHue <= 0.45 )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, smootherstep( hueMin_1, 0.45, colorHue ) * ( colorSat * satLimit ));
      else if ( colorHue > 0.45 && colorHue <= hueMax_1 )
         fxcolor.xyz = lerp( greyVal.xxx, fxcolor.xyz, ( 1.0f - smootherstep( 0.45, hueMax_1, colorHue )) * ( colorSat * satLimit ));
      else
         fxcolor.xyz = greyVal.xxx;
   
   }
   
	color.xyz = lerp( color.xyz, fxcolor.xyz, 0.25 );

/// LEAK BLOOM	
	float3 BloomLeak=TextureBloom.Sample(SamplerLinear, IN.txcoord0.xy);
	float LeakBloomDiffuse=0.75;
	float LeakBloomPower=0.001;
	BloomLeak.xyz=BloomLeak-color;
	BloomLeak.xyz=max(BloomLeak.xyz, 0.0) / max(LeakBloomDiffuse, 0.00001);
	BloomLeak*=ENBParams01.x+(LeakSlider*LeakBloomPower);
	
	float	todfLeakTextureIntensity=lerp(lerp(fLeakTextureIntensityNight, fLeakTextureIntensityDay, ENightDayFactor), fLeakTextureIntensityInterior, EInteriorFactor);
						  
	float LeakTexturemult = BloomLeak;
	float3 LeakTextureD = 	TextureLeakD.Sample(SamplerLeak, IN.txcoord0.xy);
	float3 LeakTextureN = 	TextureLeakN.Sample(SamplerLeak, IN.txcoord0.xy);
	float3 LeakTextureI = 	TextureLeakI.Sample(SamplerLeak, IN.txcoord0.xy);

	float3 LeakTex=lerp(lerp(LeakTextureN, LeakTextureD, ENightDayFactor), LeakTextureI, EInteriorFactor);
		
	float3 DefectPola=	TextureDefectPola.Sample(SamplerLeak, IN.txcoord0.xy);					
	float3 Leak = 		LeakTex.xyz*BloomLeak*(todfLeakTextureIntensity*(LeakSlider*0.01));
	float3 PolaDef = 	DefectPola.xyz*BloomLeak*((todfLeakTextureIntensity*0.5)*(LeakSlider*0.01));
	
	float   tempnor=(BloomLeak/(1.0+BloomLeak));
	tempnor=pow(tempnor, BloomLeak.x);
	BloomLeak*=tempnor * BloomLeak.z;
	BloomLeak.xyz=saturate(BloomLeak.xyz);
				
	Leak = lerp(dot(Leak.xyz,0.333), Leak.xyz, fLeakSaturation);
	PolaDef = lerp(dot(PolaDef.xyz,0.333), PolaDef.xyz, fLeakSaturation);
	
	BloomLeak.xyz = 1-(1-BloomLeak.xyz)*(1-Leak.xyz)+PolaDef.xyz;

	color.xyz+=lerp(bloom,BloomLeak,LeakSlider*0.01);
	
//GOING LDR STARTING HERE  >>>
	color.xyz=saturate(color.xyz);

/// 1 COLOR CORRECTION LUT
/// COLOR CORRECTION NATURAL AND ATMOSPHERIC
	color.xyz = lerp(color.xyz, MultiLUT_T(color,0,0),lerp(lerp(0.5, 0.6, ENightDayFactor), 0.5, EInteriorFactor));

/// BLACK LEVELS
	color.rgb = color.rgb * white_point_float - (black_point_float *  white_point_float);

  #if (Levels_highlight_clipping == 1)

    float3 clipped_colors = any(color.rgb > saturate(color.rgb)) //any colors whiter than white?
                    ? float3(1.0, 0.0, 0.0)
                    : color.rgb;
                    
    clipped_colors = all(color.rgb > saturate(color.rgb)) //all colors whiter than white?
                    ? float3(1.0, 1.0, 0.0)
                    : clipped_colors;
                    
    clipped_colors = any(color.rgb < saturate(color.rgb)) //any colors blacker than black?
                    ? float3(0.0, 0.0, 1.0)
                    : clipped_colors;
                    
    clipped_colors = all(color.rgb < saturate(color.rgb)) //all colors blacker than black?
                    ? float3(0.0, 1.0, 1.0)
                    : clipped_colors;                    
                    
    color.rgb = clipped_colors;
    
  #endif

	color=saturate(color);

/// WHITE BALANCE
	float WB   			= lerp(lerp(WBN, WBD, ENightDayFactor), WBI, EInteriorFactor);
	float3 WBWColor=color.rgb;
    float4 ColorLUTWBWarm = float4((WBWColor.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,WBWColor.b*float(TuningColorLUTTileAmountY-1),1);
    ColorLUTWBWarm.x += trunc(ColorLUTWBWarm.z)*TuningColorLUTNorm.y;
	ColorLUTWBWarm = lerp(
      TextureWB.SampleLevel(SamplerLut, ColorLUTWBWarm.xy, 0),
      TextureWB.SampleLevel(SamplerLut, float2(ColorLUTWBWarm.x+TuningColorLUTNorm.y,ColorLUTWBWarm.y), 0),frac(ColorLUTWBWarm.z));
	color.xyz = saturate(lerp(WBWColor.xyz, ColorLUTWBWarm.xyz, WB*0.010));

/// NIGHT EYE LUT  
	if (isHavingGameFx) {
		float4 NELUTColor = float4((color.rg*float(TuningColorLUTTileAmountY-1)+0.5f)*TuningColorLUTNorm,color.b*float(TuningColorLUTTileAmountY-1),1);
		
		NELUTColor.x += trunc(NELUTColor.z)*TuningColorLUTNorm.y;
		NELUTColor = lerp(
		  TextureNELUT.SampleLevel(SamplerLut, NELUTColor.xy, 0),
		  TextureNELUT.SampleLevel(SamplerLut, float2(NELUTColor.x+TuningColorLUTNorm.y,NELUTColor.y), 0),frac(NELUTColor.z));
		
		color.xyz = saturate(lerp(color.xyz, NELUTColor.xyz, saturate(fade_weight*5.0)));
	}

/// CAMERA SATURATION 
	float3 cameSat = color.rgb;
	float3 middleg = dot(cameSat,(1.0/3.0));
	float3 diffcolor = cameSat - middleg;
	color.rgb = saturate((cameSat + diffcolor * camSaturation)/(1+(diffcolor*camSaturation)));

/// CAMERA PROFILES IN
	float3 CamColor=color.rgb;
	
	float4 ColorDSLRDst = float4( CamColor * ( TuningColorLUTTileAmountY - 1.0f ), 1.0f);
    ColorDSLRDst.xy = (ColorDSLRDst.xy + 0.5f) * TuningColorLUTNorm;
    ColorDSLRDst.x += trunc(ColorDSLRDst.z) * TuningColorLUTNorm.y;
    ColorDSLRDst.w  = ColorDSLRDst.x + TuningColorLUTNorm.y;
    ColorDSLRDst.y  = (ColorDSLRDst.y + iCamera) / float(CameraCount) ;
	
	///CAMERAS:
	 ColorDSLRDst = lerp(
      TextureCameras.SampleLevel(SamplerLut, ColorDSLRDst.xy, 0),
      TextureCameras.SampleLevel(SamplerLut, ColorDSLRDst.wy, 0),frac(ColorDSLRDst.z));

	color.rgb = saturate(lerp(CamColor.xyz, ColorDSLRDst.xyz, 1.0f));

/// FILMS
	float3 FilmColor=color.rgb;

	float4 ColorLUTF = float4( FilmColor * ( TuningColorLUTTileAmountY - 1.0f ), 1.0f);
    ColorLUTF.xy = (ColorLUTF.xy + 0.5f) * TuningColorLUTNorm;
    ColorLUTF.x += trunc(ColorLUTF.z) * TuningColorLUTNorm.y;
    ColorLUTF.w  = ColorLUTF.x + TuningColorLUTNorm.y;
    ColorLUTF.y  = (ColorLUTF.y + iFilm) / float(FilmCount) ;
	
	///FILMS:
	 ColorLUTF = lerp(
      TextureFilms.SampleLevel(SamplerLut, ColorLUTF.xy, 0),
      TextureFilms.SampleLevel(SamplerLut, ColorLUTF.wy, 0),frac(ColorLUTF.z));
	
	color.rgb = saturate(lerp(FilmColor.xyz, ColorLUTF.xyz, lerp(lerp(FilmAmount_N*0.01, FilmAmount*0.01, ENightDayFactor), FilmAmount_I*0.01, EInteriorFactor)));

/// FILM LIST VISUAL
if (bFilmList==1) {
 
		float4 FilmListColour;
		float3 FilmColor=color.rgb;
		const float phiValue = ((1.0 + sqrt(5.0))/2.0);
		float aspectRatio = ScreenSize.z;

		float screenWidth = ScreenSize.x;
		float screenHeight = ScreenSize.x * ScreenSize.w;

		float idealWidth =  screenHeight * phiValue;
		float idealHeight = screenWidth / phiValue;

		float4 sourceCoordFactor = float4(1.0, 1.0, 1.0, 1.0);

		float2 coords = float2( (TexCoords.x * sourceCoordFactor.x) - ((1.0-sourceCoordFactor.z)/2.0),
								(TexCoords.y * sourceCoordFactor.y) - ((1.0-sourceCoordFactor.w)/2.0));

		FilmListColour = TextureFilmList.Sample(SamplerLinear, coords);
		
		color.rgb= saturate(lerp(FilmColor, FilmListColour.xyz*float3(1.0, 1.0, 1.0), FilmListColour.w*1.0));
	}
	
/// FINAL OUTPUT
	/// Saturation Limit 
	color.xyz = RGBToHSL( color.xyz );
	color.y = min( color.y, 0.7 );
	color.xyz = HSLToRGB( color.xyz );
	
  res.xyz=saturate(color);
  
/// DEBUG MESSAGE
if (!EInteriorFactor && isBroken) {
 
		float4 ImageColour;

		const float phiValue = ((1.0 + sqrt(5.0))/2.0);
		float aspectRatio = ScreenSize.z;

		float screenWidth = ScreenSize.x;
		float screenHeight = ScreenSize.x * ScreenSize.w;

		float idealWidth =  screenHeight * phiValue;
		float idealHeight = screenWidth / phiValue;

		float4 sourceCoordFactor = float4(1.0, 1.0, 1.0, 1.0);

		float2 coords = float2( (IN.txcoord0.x * sourceCoordFactor.x) - ((1.0-sourceCoordFactor.z)/2.0),
								(IN.txcoord0.y * sourceCoordFactor.y) - ((1.0-sourceCoordFactor.w)/2.0));

		ImageColour = TextureIMG_debug.Sample(SamplerLinear, coords);
		
		res.rgb= saturate(lerp(res, ImageColour.xyz*float3(1.0, 1.0, 1.0), ImageColour.w*1.0));
	}  
  
  res.w=1.0;
  return float4(res + Dither(res, IN.txcoord0.xy, 10),1.0);

}

// VANILLA POST PROCESS, DO NOT MODIFY!
float4	PS_DrawOriginal(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	 float4	res;
	float4	color;

	float2	scaleduv=Params01[6].xy*IN.txcoord0.xy;
	scaleduv=max(scaleduv, 0.0);
	scaleduv=min(scaleduv, Params01[6].zy);

	color=TextureColor.Sample(SamplerPoint, IN.txcoord0.xy); //hdr scene color

	float4	r0, r1, r2, r3;
	r1.xy=scaleduv;
	r0.xyz = color.xyz;
	if (0.5<=Params01[0].x) r1.xy=IN.txcoord0.xy;
	r1.xyz = TextureBloom.Sample(SamplerLinear, r1.xy).xyz;
	r2.xy = TextureAdaptation.Sample(SamplerLinear, IN.txcoord0.xy).xy; //in skyrimse it two component

	r0.w=dot(float3(2.125000e-001, 7.154000e-001, 7.210000e-002), r0.xyz);
	r0.w=max(r0.w, 1.000000e-005);
	r1.w=r2.y/r2.x;
	r2.y=r0.w * r1.w;
	if (0.5<Params01[2].z) r2.z=0xffffffff; else r2.z=0;
	r3.xy=r1.w * r0.w + float2(-4.000000e-003, 1.000000e+000);
	r1.w=max(r3.x, 0.0);
	r3.xz=r1.w * 6.2 + float2(5.000000e-001, 1.700000e+000);
	r2.w=r1.w * r3.x;
	r1.w=r1.w * r3.z + 6.000000e-002;
	r1.w=r2.w / r1.w;
	r1.w=pow(r1.w, 2.2);
	r1.w=r1.w * Params01[2].y;
	r2.w=r2.y * Params01[2].y + 1.0;
	r2.y=r2.w * r2.y;
	r2.y=r2.y / r3.y;
	if (r2.z==0) r1.w=r2.y; else r1.w=r1.w;
	r0.w=r1.w / r0.w;
	r1.w=saturate(Params01[2].x - r1.w);
	r1.xyz=r1 * r1.w;
	r0.xyz=r0 * r0.w + r1;
	r1.x=dot(r0.xyz, float3(2.125000e-001, 7.154000e-001, 7.210000e-002));
	r0.w=1.0;
	r0=r0 - r1.x;
	r0=Params01[3].x * r0 + r1.x;
	r1=Params01[4] * r1.x - r0;
	r0=Params01[4].w * r1 + r0;
	r0=Params01[3].w * r0 - r2.x;
	r0=Params01[3].z * r0 + r2.x;
	r0.xyz=saturate(r0);
	r1.xyz=pow(r1.xyz, Params01[6].w);
	//active only in certain modes, like khajiit vision, otherwise Params01[5].w=0
	r1=Params01[5] - r0;
	res=Params01[5].w * r1 + r0;

	return res;
}

float4	PS_CA(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4	res=TextureColor.Sample(SamplerPoint, IN.txcoord0.xy);;
	
/// NIGHT EYE C.A
	if (isHavingGameFx) res.rgb = LensCA(IN.txcoord0.xy);
	if (isHavingGameFx) res.rgb = ChromaticAberration(res.rgb,IN.txcoord0.xy);

	res.w=1.0;
	return res;
}
/////////////////////////////////////////////////////////
//               	    TECHNIQUES                     //
/////////////////////////////////////////////////////////

technique11 Raw <string UIName="PR:RAW.CAMERA";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_PRT(0)));
  }
}
technique11 Raw1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
		SetPixelShader(CompileShader(ps_5_0, PS_CA()));
	}
}

technique11 Panasonic <string UIName="PR:PANASONIC.DMC-GH4";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_PRT(1)));
  }
}
technique11 Panasonic1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
		SetPixelShader(CompileShader(ps_5_0, PS_CA()));
	}
}

technique11 Canon <string UIName="PR:CANON.EOS-5D";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_PRT(2)));
  }
}
technique11 Canon1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
		SetPixelShader(CompileShader(ps_5_0, PS_CA()));
	}
}

technique11 Nikon <string UIName="PR:NIKON.D500";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_PRT(3)));
  }
}
technique11 Nikon1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
		SetPixelShader(CompileShader(ps_5_0, PS_CA()));
	}
}

technique11 Pola <string UIName="PR:POLAROID.1-STEP";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_PRT(4)));
  }
}
technique11 Pola1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
		SetPixelShader(CompileShader(ps_5_0, PS_CA()));
	}
}

technique11 Log <string UIName="PR:SONY.S-LOG";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_PRT(5)));
  }
}
technique11 Log1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
		SetPixelShader(CompileShader(ps_5_0, PS_CA()));
	}
}

technique11 Arri <string UIName="PR:ARRI.ALEXA-709";>
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_PRT(6)));
  }
}
technique11 Arri1
{
	pass p0
	{
		SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
		SetPixelShader(CompileShader(ps_5_0, PS_CA()));
	}
}
//////////////////////////////////////////////////////////

technique11 ORIGINALPOSTPROCESS <string UIName="Vanilla";> //do not modify this technique
{
  pass p0
  {
    SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
    SetPixelShader(CompileShader(ps_5_0, PS_DrawOriginal()));
  }
}