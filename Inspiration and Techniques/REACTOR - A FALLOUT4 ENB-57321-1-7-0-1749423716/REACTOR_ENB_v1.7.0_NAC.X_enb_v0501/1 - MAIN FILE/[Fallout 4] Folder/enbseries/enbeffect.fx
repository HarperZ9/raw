//////////////////////////////////////////////////////////////////////
//                                                                  //
//    ██████╗ ███████╗ █████╗  ██████╗████████╗ ██████╗ ██████╗     //
//    ██╔══██╗██╔════╝██╔══██╗██╔════╝╚══██╔══╝██╔═══██╗██╔══██╗    //
//    ██████╔╝█████╗  ███████║██║        ██║   ██║   ██║██████╔╝    //
//    ██╔══██╗██╔══╝  ██╔══██║██║        ██║   ██║   ██║██╔══██╗    //
//    ██║  ██║███████╗██║  ██║╚██████╗   ██║   ╚██████╔╝██║  ██║    //
//    ╚═╝  ╚═╝╚══════╝╚═╝  ╚═╝ ╚═════╝   ╚═╝    ╚═════╝ ╚═╝  ╚═╝    //
//                                                                  //
//                         A FALLOUT4 ENB                           //
//                                                                  //
///// MOD PAGE ///////////////////////////////////////////////////////
//                                                                  //
//    https://www.nexusmods.com/fallout4/mods/57321                 //
//                                                                  //
//////////////////////////////////////////////////////////////////////
//                                                                  //
//    ENBSeries Fallout 4 hlsl DX11 format                          //
//    visit http://enbdev.com for updates                           //
//    Copyright (c) Boris Vorontsov                                 //
//                                                                  //
///// CREDITS ////////////////////////////////////////////////////////
//                                                                  //
//  - Additional shaders, setup,                                    //
//    modifications, tweaks and                                     //
//    author of this file:          Sevenence                       //
//                                                                  //
//  - Reforged code:                The Sandvich Maker              //
//                                                                  //
//  - Film Grain:                   Sevenence			            //
//                                                                  //
//  - Adaptation Level                                              //
//    Visualizer and Lut code:      Kingeric1992                    //
//                                                                  //
//  - Uncharted II Tonemapping                                      //
//    and Inspiraton:               John Hable                      //
//                                                                  //
//  - ACES Tonemapping:             Stephen Hill                    //
//                                  Krzysztof Narkowicz             //
//                                                                  //
///// PLEASE DO NOT REDISTRIBUTE WITHOUT CREDITS /////////////////////



///// INCLUDE ////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

#include "Include/Helper.fxh"
#include "Include/ReforgedGlobals.fxh"
#include "Include/ReforgedUI.fxh"
#include "Include/Graphing.fxh"
#include "Setup.ini"
#include "Include/Colorspace.fxh"

///// TEXTURES ///////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

Texture2D   TexOverlay1   <string ResourceName="Include/Textures/NVOverlay1.png"; >;
Texture2D   TexOverlay2   <string ResourceName="Include/Textures/NVOverlay2.png"; >;
Texture2D   TexOverlay3   <string ResourceName="Include/Textures/NVOverlay3.png"; >;
Texture2D   TexDirt1      <string ResourceName="Include/Textures/NVDirt1.png"; >;
Texture2D   LUT           <string ResourceName="Include/Textures/LUT_Color_correction.png"; >;

///// GUI ////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

UI_WHITESPACE(1)
UI_MESSAGE(w1, MERGE(                           "R E A C T O R   E N B   ",                             VERSION_NUMBER))

UI_WHITESPACE(2)
UI_WHITESPACE(3)

#define UI_CATEGORY Colorspace
UI_SEPARATOR_CUSTOM                             ("C O L O R   S P A C E")
UI_LINESPACE(30)
UI_MESSAGE(w20, MERGE(                          "|  PRIMARIES: ",										TO_STRING(COLORSPACE_TYPE)))
UI_LINESPACE(31)
UI_BOOL(UI_EnableColorCorrection,				"|- Correction: Enable",								true)
UI_FLOAT_DNI(UI_ColorCorrectionStrength,		"|- Correction: Amount",								0.00, 1.00, 1.00)
	
UI_WHITESPACE(4)
UI_WHITESPACE(5)

#define UI_CATEGORY Tonemapping
UI_SEPARATOR_CUSTOM                             ("T O N E   M A P P I N G")
UI_LINESPACE(18)

#if E_TONEMAPPER==1
    UI_MESSAGE(w8,                              "|  TM1: Uncharted II")
    UI_LINESPACE(19)
	UI_BOOL(UI_ShowGraph,                       "|- TM1: Show Graph",                                   false)
	UI_LINESPACE(20)
    UI_FLOAT_DNI(UI_TM_ExposureBias,            "|- TM1: Exposure Bias",                                0.10, 10.0, 0.50)    
	UI_LINESPACE(21)
    UI_FLOAT_DNI(UI_LinearWhite,                "|- TM1: Linear White Point",                           1.00, 11.2, 6.00)
    UI_LINESPACE(22)
    UI_FLOAT_DNI(UI_ShoulderStrength,           "|- TM1: Shoulder Strength",                            0.01, 1.00, 0.15)
    UI_LINESPACE(23)
    UI_FLOAT_DNI(UI_LinearStrength,             "|- TM1: Linear Strength",                              0.01, 1.00, 0.05)
    UI_LINESPACE(24)
    UI_FLOAT_DNI(UI_LinearAngle,                "|- TM1: Linear Angle",                                 0.01, 1.00, 0.15)
	UI_LINESPACE(25)
	#if   E_TONEMAPPER_MODE==0
		UI_FLOAT(UI_ToeNumerator,				"|- TM1: Toe Strength is Imagespace controlled",        0.00, 0.00, 0.00)
	#elif E_TONEMAPPER_MODE==1
		UI_FLOAT_DNI(UI_ToeStrength,			"|- TM1: Toe Strength",                                 0.01, 1.00, 0.00)
	#endif
	UI_LINESPACE(26)
    UI_FLOAT_DNI(UI_ToeDenominator,             "|- TM1: Toe Denominator",                              0.01, 1.00, 0.50)
   
#elif E_TONEMAPPER==2
    UI_MESSAGE(w8,                              "|  TM2: ACES Fitted")
    UI_LINESPACE(19)
	UI_BOOL(UI_ShowGraph,                       "|- TM2: Show Graph",                                   false)
    UI_LINESPACE(20)    
	UI_FLOAT_DNI(UI_TM_ExposureBias,            "|- TM2: Exposure Bias",                                0.10, 10.0, 1.00)
	UI_LINESPACE(21)
    UI_FLOAT_DNI(UI_LinearWhite,                "|- TM2: Linear White Point",                           1.00, 11.2, 6.00)
	UI_LINESPACE(22)
    UI_FLOAT_DNI(UI_ACES_C,                     "|- TM2: Shoulder",                                     0.00, 20.0, 0.30)
	UI_LINESPACE(23)
    UI_FLOAT_DNI(UI_ACES_B,                     "|- TM2: Linear",                                       0.00, 20.0, 0.22)
	UI_LINESPACE(24)
    UI_FLOAT_DNI(UI_ACES_A,                     "|- TM2: Toe",                                          0.00, 20.0, 0.01)
	UI_LINESPACE(25)
	#if   E_TONEMAPPER_MODE==0
		UI_FLOAT(UI_ACES_D,						"|- TM2: D is Imagespace controlled",                   0.00, 0.00, 0.00)
	#elif E_TONEMAPPER_MODE==1
		UI_FLOAT_FINE_DNI(UI_ACES_D,			"|- TM2: D",                                            0.00, 20.0, 0.03, 0.001)
	#endif
    
#elif E_TONEMAPPER==3
    UI_MESSAGE(w8,                              "|  TM3: FALLOUT 4 VANILLA")
    UI_LINESPACE(19)
    UI_BOOL(UI_ShowGraph,                       "|- TM3: Show Graph",                                   false)
    UI_LINESPACE(20)
    UI_FLOAT_DNI(UI_TM_ExposureBias,            "|- TM3: Exposure Bias",                                0.10, 10.0, 1.00)
	UI_LINESPACE(21)
    UI_FLOAT_DNI(UI_LinearWhite,                "|- TM3: Linear White Point",                           1.00, 11.2, 6.00)
    UI_LINESPACE(22)
    
#endif

UI_WHITESPACE(8)
UI_WHITESPACE(9)

#define UI_CATEGORY ImageSpaceControl
UI_SEPARATOR_CUSTOM                             ("I M A G E   S P A C E   C O N T R O L")
UI_LINESPACE(1)
UI_MESSAGE(w2,                                  "|---------------- Params01[6].x -----------------")
UI_FLOAT_DNI(UI_ISC_BloomThresMin,              "|- ISC: Bloom Threshold min",                          0.00, 1.00, 0.00)
UI_FLOAT_DNI(UI_ISC_BloomThresMax,              "|- ISC: Bloom Threshold max",                          0.00, 1.00, 1.00)
UI_LINESPACE(2)
UI_MESSAGE(w10,                                 "|---------------- Params01[6].y -----------------")
UI_FLOAT_DNI(UI_ISC_BloomScaleMin,              "|- ISC: Bloom Scale min",                              0.00, 1.00, 0.00)
UI_FLOAT_DNI(UI_ISC_BloomScaleMax,              "|- ISC: Bloom Scale max",                              0.00, 1.00, 1.00)
UI_LINESPACE(3)
UI_BOOL_EI(UI_SwitchBloomISC,                   "|- ISC: Bloom switch .y <-> .x",                       false)
UI_LIST(UI_ShowBloomTex,                        "|- ISC: Bloom visualization",                          0,    2,    0,          "Disabled, Before mixing, After mixing")
UI_LINESPACE(4)
UI_MESSAGE(w3,                                  "|---------------- Params01[2].w -----------------")
UI_FLOAT_DNI(UI_ISC_BrightnessMin,              "|- ISC: Brightness min",                               0.00, 1.00, 0.00)
UI_FLOAT_DNI(UI_ISC_BrightnessMax,              "|- ISC: Brightness max",                               1.00, 10.0, 2.00)
UI_FLOAT_DNI(UI_ISC_BrightnessMul,              "|- ISC: Brightness Multiplier",                        0.00, 20.0, 1.00)
UI_LINESPACE(5)
UI_MESSAGE(w4,                                  "|---------------- Params01[2].z -----------------")
UI_FLOAT_DNI(UI_ISC_ContrastMin,                "|- ISC: Contrast min",                                 0.00, 1.00, 0.00)
UI_FLOAT_DNI(UI_ISC_ContrastMax,                "|- ISC: Contrast max",                                 1.00, 2.00, 2.00)
UI_FLOAT_DNI(UI_ISC_ContrastMul,                "|- ISC: Contrast Multiplier",                          0.00, 2.00, 1.00)
UI_LINESPACE(6)
UI_MESSAGE(w5,                                  "|---------------- Params01[2].x -----------------")
UI_FLOAT_DNI(UI_ISC_SaturationtMin,             "|- ISC: Saturation min",                               0.00, 1.00, 0.00)
UI_FLOAT_DNI(UI_ISC_SaturationtMax,             "|- ISC: Saturation max",                               1.00, 2.00, 2.00)
UI_FLOAT_DNI(UI_ISC_SaturationMul,              "|- ISC: Saturation Multiplier",                        0.00, 2.00, 1.00)
UI_LINESPACE(7)
UI_MESSAGE(w6,                                  "|---------------- Params01[3].w -----------------")
UI_FLOAT_DNI(UI_ISC_TintMin,                    "|- ISC: Tint min",                                     0.00, 1.00, 0.00)
UI_FLOAT_DNI(UI_ISC_TintMax,                    "|- ISC: Tint max",                                     0.00, 2.00, 2.00)
UI_FLOAT_DNI(UI_ISC_TintMul,                    "|- ISC: Tint Multiplier",                              0.00, 2.00, 1.00)

UI_WHITESPACE(6)
UI_WHITESPACE(7)

#define UI_CATEGORY NightEyeAmp
UI_SEPARATOR_CUSTOM                             ("N I G H T   E Y E   A M P L I F I E R")
UI_LINESPACE(40)
UI_BOOL(UI_enable_NightEye,                     "|- NEA: Enable",                                       true)
UI_FLOAT_FINE(UI_EyeLoValue,                    "|- NEA: Detect [5].w low ",                            0.00, 3.00, 0.00784, 0.00001)
UI_FLOAT_FINE(UI_EyeHiValue,                    "|- NEA: Detect [5].w high",                            0.00, 3.00, 0.00785, 0.00001)
UI_LINESPACE(41)
UI_BOOL(UI_EyeDepthMode,                        "|- NEA: Vision Range Enable",                          false)
UI_FLOAT(UI_EyeRange,                           "|- NEA: Vision Range",                                 0.01, 1.00, 0.50)
UI_LINESPACE(42)
UI_FLOAT_DNI(UI_EyeGain,                        "|- NEA: Gain",                                         1.00, 5.00, 1.00)
UI_LINESPACE(43)
UI_FLOAT3_DNI(UI_EyeColor,                      "|- NEA: Tint Color",                                   0.00, 1.00, 0.00)
UI_FLOAT_DNI(UI_EyeTint,                        "|- NEA: Tint Amount",                                  0.00, 1.00, 0.00)
UI_LINESPACE(44)
UI_FLOAT_DNI(UI_EyeSaturation,                  "|- NEA: Saturation",                                   0.00, 1.00, 1.00)
UI_LINESPACE(45)
UI_BOOL(UI_Enable_Grain,                        "|- NEA: Noise Enable",                                 true)
UI_FLOAT_DNI(UI_GrainIntensity,                 "|- NEA: Noise Intensity",                              0.00, 1.00, 0.20)
UI_LINESPACE(46)
UI_INT(UI_EyeVisorType,                         "|- NEA: Visor Type",                                   0.00, 3.00, 1.00)
UI_FLOAT(UI_EyeDirtIntensity,                   "|- NEA: Visor Dirt",                                   0.00, 1.00, 0.25)

UI_WHITESPACE(20)

///// IMAGE SPACE CONSTANTS //////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

static float  ISC_Auto_Exp_Max    = Params01[1].x;
static float  ISC_Auto_Exp_Min    = Params01[1].y;
static float  ISC_Middle_Gray     = Params01[1].z;
static float  ISC_Tonemap_E       = Params01[1].w;
static float  ISC_Saturation      = Params01[2].x;
static float  ISC_Contrast        = Params01[2].z;
static float  ISC_Brightness      = Params01[2].w;
static float3 ISC_Tint_RGB        = Params01[3].xyz;
static float  ISC_Tint_Amount     = Params01[3].w;
static float3 ISC_SFX_RGB         = Params01[5].xyz;
static float  ISC_SFX_Amount      = Params01[5].w;
static float  ISC_Bloom_Threshold = Params01[6].x;
static float  ISC_Bloom_Scale     = Params01[6].y;

///// FUNCTIONS //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

#if E_ADAPTTOOL
    #include "Include/AdaptTool.fxh"
#endif

///// LUT ////////////////////////////////////////////////////////////

float3 Lut(float3 colorIN, Texture2D lutTexIn, float2 lutSize) 
{
    float2  CLut_pSize = 1.0 / lutSize;
    float4  CLut_UV;
            colorIN    = saturate(colorIN) * (lutSize.y - 1.0);
            CLut_UV.w  = floor(colorIN.b);
            CLut_UV.xy = (colorIN.rg + 0.5) * CLut_pSize;
            CLut_UV.x += CLut_UV.w * CLut_pSize.y;
            CLut_UV.z  = CLut_UV.x + CLut_pSize.y;
    
    return  lerp(lutTexIn.SampleLevel(LinearSampler, CLut_UV.xy, 0).rgb, 
                 lutTexIn.SampleLevel(LinearSampler, CLut_UV.zy, 0).rgb, colorIN.b - CLut_UV.w);
}

float3 Lut(float3 colorIN, Texture2D lutTexIn) // function overload
{
    float2  lutsize;
            lutTexIn.GetDimensions(lutsize.x, lutsize.y);
    
    return  Lut(colorIN, lutTexIn, lutsize);
}

///// SIGMOID CURVE //////////////////////////////////////////////////

float Sigmoid(float x)
{
    return 1.0 / (1.0 + exp(-x));
}

///// ADAPTATION PRIMER //////////////////////////////////////////////

float adaptPrimer(float adapt)
{
    #if   E_ADAPTATION==1 // ENB ADAPTATION
        
        adapt = 1.0 / (0.001 + adapt);
    
    #elif E_ADAPTATION==2 // VANILLA ADAPTATION    
        
        adapt = ISC_Middle_Gray / (0.001 + adapt);
    
    #elif E_ADAPTATION==3 // VANILLA AUTO EXPOSURE
        
        float2 VAE;
        
        ISC_Auto_Exp_Max = max(Params01[1].x, Params01[1].y); // Set ISC_Auto_Exp_Max to the larger of .x and .y
        ISC_Auto_Exp_Min = min(Params01[1].x, Params01[1].y); // Set ISC_Auto_Exp_Min to the smaller of .x and .y
  
        VAE.x = ISC_Middle_Gray / (0.001 + adapt);
        VAE.y = VAE.x < ISC_Auto_Exp_Min;
        VAE.x = VAE.y ? ISC_Auto_Exp_Min : VAE.x;
        VAE.y = ISC_Auto_Exp_Max < VAE.x;
        adapt = VAE.y ? ISC_Auto_Exp_Max : VAE.x;
        
    #endif
    
    return zerolim(adapt);
}

///// UNCHARTED 2 TMO ////////////////////////////////////////////////

#if E_TONEMAPPER==1 

    float3 TMO(float3 x)
    {
        float a = UI_ShoulderStrength;
        float b = UI_LinearStrength;
        float c = UI_LinearAngle;
		
		#if   E_TONEMAPPER_MODE==0
			float d = ISC_Tonemap_E;
		#elif E_TONEMAPPER_MODE==1
			float d = UI_ToeStrength;
		#endif
		
		float e = 0.0;
		float f = UI_ToeDenominator;
		
        return deltalim((x * (a * x + c * b) + d * e) / deltalim((x * (a * x + b) + d * f)) - e / f);
    }

///// ACES FITTED TMO (Parameterized) ////////////////////////////////

#elif E_TONEMAPPER==2
    
    float3 TMO(float3 x)
    {
        float  a = UI_ACES_A;
        float  b = UI_ACES_B;
        float  c = UI_ACES_C;
        
		#if   E_TONEMAPPER_MODE==0
			float  d = ISC_Tonemap_E;
		#elif E_TONEMAPPER_MODE==1
			float  d = UI_ACES_D;
		#endif
		
        return zerolim( (x * (x + a)) / (x * (b * x + c) + d) );
    }

///// FALLOUT 4 VANILLA TMO //////////////////////////////////////////

#elif E_TONEMAPPER==3
    
    float3 TMO(float3 x)
    {
        float3 r0, r1, r2, r3;
        
        r0.xyz = x.xyz;
        r1.xyz = r0.xyz + r0.xyz;
        r2.xyz = r0.xyz * 0.3 + 0.05;
        r3.xy  = float2(0.2, 3.333333) * ISC_Tonemap_E;
        r2.xyz = r1.xyz * r2.xyz + r3.x;
        r0.xyz = r0.xyz * 0.3 + 0.5;
        r0.xyz = r1.xyz * r0.xyz + 0.06;
        r0.xyz = r2.xyz / r0.xyz;
        r0.xyz = -ISC_Tonemap_E * 3.333333 + r0.xyz;
        r1.x   = ISC_Tonemap_E * 0.2 + 19.376;
        r1.x   = r1.x * 0.0408564 - r3.y;
    
        return r0.xyz / r1.x;
    }

#endif

///// TONEMAPPING ////////////////////////////////////////////////////

float3 Tonemapping(float3 x)
{
	float   e = exp2(UI_TM_ExposureBias) - 1.0;
	float   w = UI_LinearWhite;
	float3  A = TMO(x * e);
	float3  B = TMO(w * e);    

	x = (A / B);
	x = mul(ColorOutputMat, x);
	x = Lin2sRGB_fast(saturate(x));
   
    return saturate(x);
}    

///// FILM GRAIN /////////////////////////////////////////////////////

float randomAnalog(float2 uv)
{
    float dot1  = dot(uv, float2(127.1, 311.7));
    float dot2  = dot(uv, float2(269.5, 183.3));
    float noise = sin(dot1) * 43758.5453 + sin(dot2) * 12414.2341;

    return frac(noise);
}

float3 FilmGrain(float3 colorIN, float2 uv)
{
    [branch] if (UI_Enable_Grain)
    {
		float  grainSize = 2.0;
        
		float2 resolution = float2(ScreenSize.x, ScreenSize.x / ScreenSize.z);
        float2 grainCoord = floor((uv * resolution) / grainSize);
        
		float  time = frac(Timer.x * 15.0);
			   grainCoord += float2(time * 23.1, time * 12.7);
        
		float  grain = randomAnalog(grainCoord);
			   grain = grain.xxx * (UI_GrainIntensity * 0.1);

		colorIN += grain;
    }

    return colorIN;
}

// NIGHT EYE AMPLIFIER //////////////////////////////////////////////

float3 NightEye(float3 colorIN, float2 coordIN)
{
    [branch] if(UI_enable_NightEye && Params01[5].w > UI_EyeLoValue && Params01[5].w < UI_EyeHiValue) 
    { 
        float  luma;
        float  depth = TextureDepth.Sample(PointSampler, coordIN);
               depth = linearDepth(depth, 0.1, pow(10000.0, UI_EyeRange));
               depth = 1.0 - depth;
        
        if(!UI_EyeDepthMode) depth = 1.0;
		
		colorIN  = FilmGrain(colorIN, coordIN); 
		
		colorIN  = lerp(colorIN, colorIN * (exp2(UI_EyeGain) - 1.0), depth);

        luma     = calculateLuma(colorIN);
        colorIN  = UI_EyeTint * (luma * UI_EyeColor - colorIN) + colorIN;
        colorIN  = zerolim(UI_EyeSaturation * (colorIN - luma) + luma);  
		
		colorIN /= 1.0 + max3(colorIN);      
		
		luma     = calculateLuma(colorIN);
        colorIN  = lerp(colorIN, colorIN + (TexDirt1.Sample(PointSampler, coordIN) * luma), UI_EyeDirtIntensity);
	  
        if(UI_EyeVisorType==1) colorIN *= pow(TexOverlay1.Sample(PointSampler, coordIN), 2.2);
        if(UI_EyeVisorType==2) colorIN *= pow(TexOverlay2.Sample(PointSampler, coordIN), 2.2);
        if(UI_EyeVisorType==3) colorIN *= pow(TexOverlay3.Sample(PointSampler, coordIN), 2.2);
    }
    
    return zerolim(colorIN);
}

///// PIXEL SHADERS //////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

///// REACTOR POST PROCESS ///////////////////////////////////////////

float4  PS_Draw(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float   luma  = 0.0;
    float2  coord = IN.txcoord.xy;
  
    ///// IMAGE SPACE CONTROLL ///////////////////////////////////////

    [branch] if(UI_SwitchBloomISC)
    {
        ISC_Bloom_Threshold = Params01[6].y;
        ISC_Bloom_Scale     = Params01[6].x;
    }
    
    ISC_Bloom_Scale     = clamp(ISC_Bloom_Scale,     UI_ISC_BloomScaleMin,  UI_ISC_BloomScaleMax);
    ISC_Bloom_Threshold = clamp(ISC_Bloom_Threshold, UI_ISC_BloomThresMin,  UI_ISC_BloomThresMax);
    ISC_Saturation      = clamp(ISC_Saturation,      UI_ISC_SaturationtMin, UI_ISC_SaturationtMax) * UI_ISC_SaturationMul;
    ISC_Contrast        = clamp(ISC_Contrast,        UI_ISC_ContrastMin,    UI_ISC_ContrastMax)    * UI_ISC_ContrastMul;
    ISC_Brightness      = clamp(ISC_Brightness,      UI_ISC_BrightnessMin,  UI_ISC_BrightnessMax)  * UI_ISC_BrightnessMul;
    ISC_Tint_Amount     = clamp(ISC_Tint_Amount,     UI_ISC_TintMin,        UI_ISC_TintMax)        * UI_ISC_TintMul;
    
    ///// LOAD TEXTURES //////////////////////////////////////////////

    float3  color       = TextureColor.Sample(PointSampler, coord).xyz;
    float3  lens        = TextureLens.Sample(PointSampler, coord).xyz;
    float3  bloom       = TextureBloom.Sample(LinearSampler, Params01[4].zw * coord).xyz;
    float   adaptation  = TextureAdaptation.Sample(PointSampler, coord).x;
            
    ///// MIX LENS ///////////////////////////////////////////////////

    color += lens * ENBParams01.y;
    
    ///// MIX BLOOM //////////////////////////////////////////////////
    
    float3 rawBloom = bloom;
   
    bloom *= Sigmoid((max3(bloom) - ISC_Bloom_Threshold) / 0.5); // 0.5 = threshold softness
    bloom *= ISC_Bloom_Scale * ENBParams01.x;
    color += bloom / (1.0 + color);
    
    switch(UI_ShowBloomTex)
    {
        case 1: // Before mixing
            color = rawBloom;
            break;
        case 2: // After mixing
            color = bloom;
            break;
    }

    ///// ADAPTATION /////////////////////////////////////////////////
    
    if(adaptation == 0.0) adaptation = 0.05;
    color *= adaptPrimer(adaptation);
    
    ///// PRE TM SATURATION AND TINT /////////////////////////////////
    
    #if E_COLORSPACE != 0 || E_COLORSPACE != 1

        luma   = calculateLuma(color);
        color  = zerolim(ISC_Saturation * (color - luma) + luma);
        color  = ISC_Tint_Amount * (luma * ISC_Tint_RGB - color) + color;
        
    #endif
    
    ///// COLORSPACE /////////////////////////////////////////////////

    color = mul(ColorInputMat, color);

    ///// EXPOSURE AND CONTRAST //////////////////////////////////////
    
    float    logMidpoint = log2(adaptation);
    color *= exp2(ISC_Brightness) - 1.0;
    color  = log2(color + DELTA);
    color  = ISC_Contrast * (color - logMidpoint ) + logMidpoint;
    color  = zerolim(exp2(color) - DELTA);
    
    ///// TONE MAPPING & GAMMA ///////////////////////////////////////
   
    color  = Tonemapping(color);
	
    ///// COLOR CORRECTION ///////////////////////////////////////////
    
	[branch] if(UI_EnableColorCorrection)
    {
		color  = lerp(color, Lut(color, LUT), UI_ColorCorrectionStrength);
	}
	
    ///// POST TM SATURATION AND TINT ////////////////////////////////
    
    #if E_COLORSPACE == 0 || E_COLORSPACE == 1

        luma   = calculateLuma(color);
        color  = zerolim(ISC_Saturation * (color - luma) + luma);
        color  = ISC_Tint_Amount * (luma * ISC_Tint_RGB - color) + color;

    #endif

    ///// FX COLOR FILTER ////////////////////////////////////////////

    color  = lerp(color, ISC_SFX_RGB, ISC_SFX_Amount);    
	
	///// NIGHT EYE AMPLIFIER ////////////////////////////////////////
   
    color  = NightEye(color, coord);
	
	///// TM CURVE GRAPH /////////////////////////////////////////////
    
    [branch] if(UI_ShowGraph)
    {
        #define graphSize 512
        #define graphThickness 6
        float4 tmpcolor = float4(color, 1.0);
        GraphStruct g 	= graphNew(float2(Resolution.x - graphSize, 3), float2(graphSize, graphSize), v0.xy, float2(6, 6));
        g.drop_shadow 	= 0.5;
        g.roundness 	= 5.0;
        graphAddPlot(g, sRGB2Lin(Tonemapping(g.uv.x)), graphThickness);
        graphDraw(g, tmpcolor);
        color = tmpcolor.rgb;
    }
    
    ///// OUTPUT /////////////////////////////////////////////////////
	
    #if E_DITHERING
        
        color += chromaTriDither(color, coord, Timer.x, 10);
    
    #endif
    
    return float4(saturate(color), 1.0);
}
    
///// VANILLA POST PROCESS ///////////////////////////////////////////

float4	PS_DrawOriginal(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float4 res;
    float4 color = TextureColor.Sample(Sampler0, IN.txcoord.xy);  /// HDR scene color

    ///// BLOOM AND ADAPTATION ///////////////////////////////////////

    float4 r0, r1, r2, r3;
    r0.xyz = color.xyz;
    r1.xy  = Params01[4].zw * IN.txcoord.xy;
    r1.xyz = TextureBloom.Sample(Sampler1, r1.xy).xyz * ISC_Bloom_Scale;
    r0.w   = TextureAdaptation.Sample(Sampler0, IN.txcoord.xy).x;
    r1.w   = ISC_Middle_Gray / (0.001 + r0.w);
    r2.x   = r1.w < ISC_Auto_Exp_Min;
    r1.w   = r2.x ? ISC_Auto_Exp_Min : r1.w;
    r2.x   = ISC_Auto_Exp_Max < r1.w;
    r1.w   = r2.x ? ISC_Auto_Exp_Max : r1.w;
    r0.xyz = r1.xyz + r0.xyz;
    r0.xyz = r0.xyz * r1.w;

    ///// FILMIC TONE MAPPING ////////////////////////////////////////

    r1.xyz = r0.xyz + r0.xyz;
    r2.xyz = r0.xyz * 0.3 + 0.05;
    r3.xy  = float2(0.2, 3.333333) * ISC_Tonemap_E;
    r2.xyz = r1.xyz * r2.xyz + r3.x;
    r0.xyz = r0.xyz * 0.3 + 0.5;
    r0.xyz = r1.xyz * r0.xyz + 0.06;
    r0.xyz = r2.xyz / r0.xyz;
    r0.xyz = -ISC_Tonemap_E * 3.333333 + r0.xyz;
    r1.x   = ISC_Tonemap_E * 0.2 + 19.376;
    r1.x   = r1.x * 0.0408564 - r3.y;
    r1.xyz = r0.xyz / r1.x;
    
    ///// POST PROCESS ///////////////////////////////////////////////

    r0.x    = dot(r1.xyz, float3(0.2125, 0.7154, 0.0721));
    r1.xyz  = r1.xyz - r0.x;
    r1.xyz  = ISC_Saturation * r1.xyz + r0.x;
    r2.xyz  = r0.x * ISC_Tint_RGB - r1.xyz;
    r1.xyz  = ISC_Tint_Amount * r2.xyz + r1.xyz;
    r1.xyz  = ISC_Brightness * r1.xyz - r0.w;
    r0.xyz  = ISC_Contrast * r1.xyz + r0.w;
    res.xyz = lerp(r0.xyz, ISC_SFX_RGB, ISC_SFX_Amount);

    res.xyz = pow(res.xyz, 1.0 / 2.2);
	res.w   = 1.0;
    return res;
}

///// TECHNIQUES /////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

technique11 Draw <string UIName="REACTOR";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Draw())); }

#if E_ADAPTTOOL
    pass ADAPT_TOOL_PASS
#endif
}

technique11 ORIGINALPOSTPROCESS <string UIName="VANILLA";> 
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_DrawOriginal())); }

#if E_ADAPTTOOL
    pass ADAPT_TOOL_PASS
#endif
}