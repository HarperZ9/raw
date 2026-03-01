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
//  - Some code, support,                                           //
//    help and inspiration:         Adyss                           //
//                                                                  //
//  - Reforged code:                The Sandvich Maker              //
//                                                                  //
//  - FXAA:                         geeks3d                         //
//                                                                  //
//  - SMAA and lut code:            kingeric1992                    //
//                                                                  //
//  - Film grain:                   MTichenor/IndigoNeko            //
//                                                                  //
//  - Crop assistant:               Wolrajh                         //
//                                                                  //
//  - Vibrance                      CeeJay.dk                       //
//                                                                  //
///// PLEASE DO NOT REDISTRIBUTE WITHOUT CREDITS /////////////////////



///// INCLUDE ////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

#include "Include/Helper.fxh"
#include "Include/ReforgedUI.fxh"
#include "Include/ReforgedGlobals.fxh"
#include "Setup.ini"

///// TEXTURES ///////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

Texture2D       LUT_0       <string ResourceName="Include/Textures/LUT-0-Normalized-SSE.png"; >;
Texture2D       LUT_1       <string ResourceName="Include/Textures/LUT-0-Tungsten-to-Daylight-1a.png"; >;
Texture2D       LUT_2       <string ResourceName="Include/Textures/LUT-1-Tungsten-to-Daylight-1b.png"; >;
Texture2D       LUT_3       <string ResourceName="Include/Textures/LUT-4-Kodak-5205-Fuji-3510.png"; >;
Texture2D       LUT_4       <string ResourceName="Include/Textures/LUT-5-HorrorBlue.png"; >;
Texture2D       LUT_5       <string ResourceName="Include/Textures/LUT-6-Teal-and-Orange-Blue-Tint.png"; >;
Texture2D       LUT_6       <string ResourceName="Include/Textures/LUT-9-eisVogelV2-1.2.png"; >;
Texture2D       LUT_7       <string ResourceName="Include/Textures/LUT-10-eisVogel-mix.png"; >;
Texture2D       LUT_8       <string ResourceName="Include/Textures/LUT-11-eisVogel-mix-2.png"; >;
Texture2D       LUT_9       <string ResourceName="Include/Textures/LUT-12-Fake-HDR.png"; >;

#define LUT_LIST_REACTOR "Normalized, Tungsten to day light A, Tungsten to day light B, Kodak5205 Fuji3510 mix, Horror Blue, Teal and orange blue tint, eisVogelV2 1.2, eisVogel mix A, eisVogel mix B, Fake HDR,"

///// CUSTOM LUTS ////////////////////////////////////////////////////

#if E_CUSTOM_LUTS
    #include "CustomLUTs/_CustomLUTs.fxh"
    #define LUTnumber 17
    #define LUT_LIST LUT_LIST_REACTOR LUT_LIST_CUSTOM
#else
    #define LUTnumber 9
    #define L_CUSTOM Disabled
    #define LUT_LIST LUT_LIST_REACTOR
#endif

///// CALIBRATION CHART //////////////////////////////////////////////

#define CustomTEX_W 1920
#define CustomTEX_H 1080

Texture2D       CCHART      <string ResourceName="Include/Textures/gamma.png"; >;

///// GUI ////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

UI_WHITESPACE(1)
UI_MESSAGE(w1, MERGE(                   "R E A C T O R   E N B   ",             VERSION_NUMBER))

UI_WHITESPACE(2)
UI_WHITESPACE(3)

#define UI_CATEGORY UCC
UI_SEPARATOR_CUSTOM                     ("C A L I B R A T I O N")
UI_LINESPACE(1)
UI_BOOL(UI_CChart,                      "|- GAMMA: Show Calibration Chart",     false)
UI_FLOAT_FINE(UI_GammaCorrection,       "|- GAMMA: Correction",                 1.4, 2.8, 2.8, 0.001)

UI_WHITESPACE(4)
UI_WHITESPACE(5)

#define UI_CATEGORY LUT
UI_SEPARATOR_CUSTOM                     ("L O O K   U P   T A B L E")
UI_LINESPACE(2)
UI_BOOL(UI_LUT_Select,                  "|- LUT: Enable",                       true)
UI_LIST(UI_LUT_MIXING_MODE,             "|- LUT: Blend Mode",                   0, 1, 1, "Normal, Color")

UI_LIST(UI_LUT_File_Day,                "|- LUT: Preset Day",                   0, LUTnumber, 1, LUT_LIST)
UI_LIST(UI_LUT_File_Night,              "|- LUT: Preset Night",                 0, LUTnumber, 1, LUT_LIST)
UI_LIST(UI_LUT_File_Interior,           "|- LUT: Preset Interior",              0, LUTnumber, 1, LUT_LIST)

UI_FLOAT_DNI(UI_LUT_Amount,             "|- LUT: Amount",                       0.0, 1.0, 1.0)
UI_FLOAT_DNI(UI_LUT_White,              "|- LUT: Exclude Shadows",              0.0, 1.0, 0.25)

UI_WHITESPACE(6)
UI_WHITESPACE(7)

#define UI_CATEGORY TEMPERATURE
UI_SEPARATOR_CUSTOM                     ("C O L O R   T E M P E R A T U R E")
UI_LINESPACE(40)
UI_BOOL(UI_Enable_Temperature,          "|- Temperature: Enable",               true)
UI_FLOAT(UI_Temperature,				"|- Temperature: Cold <-> Warm",	   -1.0, 1.0, 0.0)

UI_WHITESPACE(40)
UI_WHITESPACE(41)

#define UI_CATEGORY VIBRANCE
UI_SEPARATOR_CUSTOM                     ("V I B R A N C E")
UI_LINESPACE(4)
UI_BOOL(UI_Enable_Vibrance,             "|- Vibrance: Enable",                  true)
UI_FLOAT_DNI(UI_VibranceStrenght,       "|- Vibrance: Intensity",              -1.00, 1.00, 0.00)
UI_FLOAT3(UI_VibranceBalance,           "|- Vibrance: RGB Balance",             1.00, 1.00, 1.00)

UI_WHITESPACE(8)
UI_WHITESPACE(9)

#define UI_CATEGORY SHARP
UI_SEPARATOR_CUSTOM                     ("S H A R P E N I N G")
UI_LINESPACE(5)
UI_MESSAGE(w5,                          "|  AMD FidelityFX")
UI_MESSAGE(w6,                          "|  Contrast Adaptive Sharpening")
UI_LINESPACE(6)
UI_BOOL(UI_Enable_Sharpening,           "|- Sharpening: Enable",                true)
UI_BOOL_EI(UI_Enable_Depthsharp,        "|- Sharpening: From Depth",            true)
UI_FLOAT(casContrast,                   "|- Sharpening: Contrast",              0.0, 1.0, 0.0)
UI_FLOAT(casSharpening,                 "|- Sharpening: Amount",                0.0, 3.0, 1.0)

UI_WHITESPACE(10)
UI_WHITESPACE(11)

#define UI_CATEGORY GRAIN
UI_SEPARATOR_CUSTOM                     ("F I L M   G R A I N")
UI_LINESPACE(7)
UI_BOOL(UI_Enable_Grain,                "|- Grain: Enable",                     true)
UI_FLOAT_DNI(UI_Grain_Intensity,        "|- Grain: Intensity",                  0.0, 1.0, 0.25)
#if !E_ADVANCED_GRAIN
    #define UI_Depth_Grain true
    #define UI_Shadows_Grain true
    #define UI_Enable_Mask false
    #define UI_White_Level 0.95
    #define UI_Black_Level 0.65
#elif E_ADVANCED_GRAIN
    UI_LINESPACE(8)
    UI_BOOL(UI_Depth_Grain,             "|- Mask: Draw Depth",                  true)
    UI_BOOL(UI_Shadows_Grain,           "|- Mask: Draw Shadows",                true)
    UI_BOOL(UI_Enable_Mask,             "|- Mask: Visualize Mask",              false)
    UI_FLOAT(UI_White_Level,            "|- Mask: White Level",                 0.0, 1.0, 0.95)
    UI_FLOAT(UI_Black_Level,            "|- Mask: Black Level",                 0.0, 1.0, 0.65)
#endif

UI_WHITESPACE(12)
UI_WHITESPACE(13)

#define UI_CATEGORY CA
UI_SEPARATOR_CUSTOM                     ("C H R O M A T I C   A B E R R A T I O N")
UI_LINESPACE(10)
UI_BOOL(UI_Enable_CA,                   "|- CA: Enable",                        true)
UI_FLOAT(UI_CA_LightsThres,             "|- CA: Lights Threshold",              0.0, 1.0,  0.50)
UI_FLOAT(UI_CA_Radius,                  "|- CA: Screen Edge Radius",            1.0, 3.0,  2.00)
UI_FLOAT(UI_CA_RadiusThres,             "|- CA: Screen Edge Threshold",         0.0, 1.0,  1.00)
UI_FLOAT_FINE(UI_CA_OffsetMul,          "|- CA: Pixel Offset Multiplier",       0.5, 4.0,  0.50, 0.5)
#if !E_ADVANCED_CA
    #define UI_CA_Rx  1.00
    #define UI_CA_Ry  1.00
    #define UI_CA_Gx -1.00
    #define UI_CA_Gy -1.00
    #define UI_CA_Bx -1.00
    #define UI_CA_By -1.00
#elif E_ADVANCED_CA
    UI_FLOAT(UI_CA_Rx,                  "|- CA: R.x Channel Offset",           -4.0, 4.0,  1.00)
    UI_FLOAT(UI_CA_Ry,                  "|- CA: R.y Channel Offset",           -4.0, 4.0,  1.00)
    UI_FLOAT(UI_CA_Gx,                  "|- CA: G.x Channel Offset",           -4.0, 4.0, -1.00)
    UI_FLOAT(UI_CA_Gy,                  "|- CA: G.y Channel Offset",           -4.0, 4.0, -1.00)
    UI_FLOAT(UI_CA_Bx,                  "|- CA: B.x Channel Offset",           -4.0, 4.0, -1.00)
    UI_FLOAT(UI_CA_By,                  "|- CA: B.y Channel Offset",           -4.0, 4.0, -1.00)
#endif

UI_WHITESPACE(14)
UI_WHITESPACE(15)

#define UI_CATEGORY VIGNETTE
UI_SEPARATOR_CUSTOM                     ("V I G N E T T E")
UI_LINESPACE(11)
UI_BOOL(UI_Enable_Vignette,             "|- Vignette: Enable",                  true)
UI_FLOAT_DNI(UI_Vignette_Amount,        "|- Vignette: Amount",                  0.0, 2.0, 1.0)

UI_WHITESPACE(16)
UI_WHITESPACE(17)

#define UI_CATEGORY GLITCH
UI_SEPARATOR_CUSTOM                     ("R G B   G L I T C H")
UI_LINESPACE(12)
UI_BOOL(UI_Enable_Glitch,               "|- Glitch: Enable",                    true)
UI_FLOAT_DNI(UI_Glitch_Amount,          "|- Glitch: Amount",                    0.0, 1.0,  0.3)
UI_FLOAT_FINE(UI_Glitch_OffsetMul,      "|- Glitch: Pixel Offset Multiplier",   0.5, 4.0,  0.50, 0.5)
#if !E_ADVANCED_GLITCH
    #define UI_GlitchRx  1.00
    #define UI_GlitchRy  0.00
    #define UI_GlitchGx  0.00
    #define UI_GlitchGy  0.00
    #define UI_GlitchBx  0.00
    #define UI_GlitchBy  0.00
#elif E_ADVANCED_GLITCH
    UI_FLOAT(UI_GlitchRx,               "|- Glitch: R.x Channel Offset",       -4.0, 4.0,  1.00)
    UI_FLOAT(UI_GlitchRy,               "|- Glitch: R.y Channel Offset",       -4.0, 4.0,  0.00)
    UI_FLOAT(UI_GlitchGx,               "|- Glitch: G.x Channel Offset",       -4.0, 4.0,  0.00)
    UI_FLOAT(UI_GlitchGy,               "|- Glitch: G.y Channel Offset",       -4.0, 4.0,  0.00)
    UI_FLOAT(UI_GlitchBx,               "|- Glitch: B.x Channel Offset",       -4.0, 4.0,  0.00)
    UI_FLOAT(UI_GlitchBy,               "|- Glitch: B.y Channel Offset",       -4.0, 4.0,  0.00)
#endif

UI_WHITESPACE(18)
UI_WHITESPACE(19)

#define UI_CATEGORY LETTERBOX
UI_SEPARATOR_CUSTOM                     ("L E T T E R B O X")
UI_LINESPACE(13)
UI_BOOL(UI_Enable_Croppreview,          "|- Letterbox: Enable",                 true)
UI_FLOAT(UI_Wratio,                     "|- Letterbox: Size",                   0.0, 30.0, 2.1)

UI_WHITESPACE(30)

///// FUNCTIONS //////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

///// VIBRANCE ///////////////////////////////////////////////////////

float3 Vibrance(float3 colorIN)
{
    [branch]if (UI_Enable_Vibrance) 
    {
        float luma = calculateLuma(colorIN);

        float max_color = max(colorIN.r, max(colorIN.g, colorIN.b)); // Find the strongest color
        float min_color = min(colorIN.r, min(colorIN.g, colorIN.b)); // Find the weakest color

        float color_saturation = max_color - min_color; // The difference between the two is the saturation

        // Extrapolate between luma and original by 1 + (1-saturation) - current
        float3 coeffVibrance = float3(UI_VibranceBalance * UI_VibranceStrenght);
        colorIN = lerp(luma, colorIN, 1.0 + (coeffVibrance * (1.0 - (sign(coeffVibrance) * color_saturation))));
    }
    
	return colorIN;
}

///// TEMPERATURE ////////////////////////////////////////////////////

float3 ColorTemperature(float3 colorIN)
{
    [branch]if (UI_Enable_Temperature)
    {
        float  luma   = calculateLuma(colorIN);
        float3 chroma = colorIN / max(luma, DELTA);

        float3 neutral   = float3(1.0, 1.0, 1.0);
        float3 warmShift = float3(1.0, 0.9, 0.8);
        float3 coolShift = float3(0.8, 0.9, 1.0);

        float3 shift = lerp(neutral, (UI_Temperature >= 0.0) ? warmShift : coolShift, abs(UI_Temperature));
        chroma *= shift;

        float  newLuma = calculateLuma(chroma);

        colorIN = saturate(chroma * (luma / max(newLuma, DELTA)));
    }

    return colorIN;
}

///// FILM GRAIN /////////////////////////////////////////////////////

#define grain_saturation 0.2
#define grain_motion 0.1

float3 FilmGrain(float3 colorIN, float2 coord)
{
    [branch] if(UI_Enable_Grain)
    {
        float grain_timer_seed = Timer.x * grain_motion;
        float2 grain_tex_coord_seed = coord.xy * 5.0;

        // Generate grain seeds ////////////
        float2 grain_seed_1 = grain_tex_coord_seed + float2 (0.0, grain_timer_seed);
        float2 grain_seed_2 = grain_tex_coord_seed + float2 (grain_timer_seed, 0.0);
        float2 grain_seed_3 = grain_tex_coord_seed + float2 (grain_timer_seed, grain_timer_seed);

        // Generate pseudo random noise ////
        float grain_noise_1 = random (grain_seed_1);
        float grain_noise_2 = random (grain_seed_2);
        float grain_noise_3 = random (grain_seed_3);
        float grain_noise_4 = (grain_noise_1 + grain_noise_2 + grain_noise_3) * 0.333333333;

        // Combine results /////////////////
        float3 grain_noise  = float3 (grain_noise_4, grain_noise_4, grain_noise_4);
        float3 grain_color  = float3 (grain_noise_1, grain_noise_2, grain_noise_3);

        // Add noise to color //////////////
        float grain_intensity = UI_Grain_Intensity * 0.1;
        colorIN += (lerp (grain_noise, grain_color, grain_saturation) * grain_intensity) - (grain_intensity * 0.5);
    }
    
    return saturate(colorIN);
}

///// LUT ////////////////////////////////////////////////////////////

float3 Lut( float3 colorIN, Texture2D lutTexIn, float2 lutSize ) 
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
    
    return  saturate(Lut(colorIN, lutTexIn, lutsize));
}

///// LUT SWITCH /////////////////////////////////////////////////////

float3 lutTexture(float3 colorIN, int ui_lut_file)
{
    switch (ui_lut_file)
    {
        case 0:
            return Lut(colorIN, LUT_0);
        case 1:
            return Lut(colorIN, LUT_1);
        case 2:
            return Lut(colorIN, LUT_2);
        case 3:
            return Lut(colorIN, LUT_3);
        case 4:
            return Lut(colorIN, LUT_4);
        case 5:
            return Lut(colorIN, LUT_5);
        case 6:
            return Lut(colorIN, LUT_6);
        case 7:
            return Lut(colorIN, LUT_7);
        case 8:
            return Lut(colorIN, LUT_8);
        case 9:
            return Lut(colorIN, LUT_9);

        #if E_CUSTOM_LUTS
            case 10:
                return Lut(colorIN, C_LUT_1);
            case 11:
                return Lut(colorIN, C_LUT_2);
            case 12:
                return Lut(colorIN, C_LUT_3);
            case 13:
                return Lut(colorIN, C_LUT_4);
            case 14:
                return Lut(colorIN, C_LUT_5);
            case 15:
                return Lut(colorIN, C_LUT_6);
            case 16:
                return Lut(colorIN, C_LUT_7);
            case 17:
                return Lut(colorIN, C_LUT_8);            
        #endif   
        
        default:
            return colorIN;
    }
}

float3 lutTextureDay(float3 colorIN)
{
    return lutTexture(colorIN, UI_LUT_File_Day);
}

float3 lutTextureNight(float3 colorIN)
{
    return lutTexture(colorIN, UI_LUT_File_Night);
}

float3 lutTextureInterior(float3 colorIN)
{
    return lutTexture(colorIN, UI_LUT_File_Interior);
}

///// GLITCH FX //////////////////////////////////////////////////////

float3 Glitch(float3 colorIN, float2 coordIN)
{   
    [branch] if(UI_Enable_Glitch)
    {   
        float2  rPosition;
                rPosition.x  = coordIN.x + (UI_GlitchRx / 1000 * UI_Glitch_OffsetMul);
                rPosition.y  = coordIN.y + (UI_GlitchRy / 1000 * UI_Glitch_OffsetMul);
        
        float2  gPosition;
                gPosition.x  = coordIN.x + (UI_GlitchGx / 1000 * UI_Glitch_OffsetMul);
                gPosition.y  = coordIN.y + (UI_GlitchGy / 1000 * UI_Glitch_OffsetMul);
        
        float2  bPosition;
                bPosition.x  = coordIN.x + (UI_GlitchBx / 1000 * UI_Glitch_OffsetMul);
                bPosition.y  = coordIN.y + (UI_GlitchBy / 1000 * UI_Glitch_OffsetMul);
        
                colorIN.x    = TextureColor.Sample(PointSampler,rPosition.xy).r;
                colorIN.y    = TextureColor.Sample(PointSampler,gPosition.xy).g;
                colorIN.z    = TextureColor.Sample(PointSampler,bPosition.xy).b;
    }
    
    return colorIN;
}

///// PIXEL SHADERS //////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

///// ANTIALIASING ///////////////////////////////////////////////////

#include "Include/SMAA/enbsmaa.fx"
#include "Include/FXAA.fxh"

///// SHARPENING /////////////////////////////////////////////////////

#include "Include/CAS.fxh"

float3 PS_CAS(VS_OUTPUT IN) : SV_Target
{
    float3 color = TextureColor.Sample(PointSampler, IN.txcoord.xy);
    
    [branch] if(UI_Enable_Sharpening)
    {
        float Depth = 1.0;
        if(UI_Enable_Depthsharp) Depth = 1.0 - linearDepth(TextureDepth.Sample(PointSampler, IN.txcoord.xy ).x, 0.5, 300.00);
        color = lerp(color, CASsharpening(IN.txcoord.xy), saturate(Depth));
    }
    
    return float4(color, 1.0);
}

///// CHROMATIC ABERRATION ///////////////////////////////////////////

float4 PS_CA(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{   
    float2  coordIN  = IN.txcoord.xy;
    float3  colorIN  = TextureColor.Sample(PointSampler, coordIN).xyz;
    
    [branch] if(UI_Enable_CA)
    {   
        float3  Origcolor    = colorIN;
        float3  Threshold    = calculateLuma(colorIN);
                Threshold    = pow(Threshold, 1.0 + (4.0 * UI_CA_LightsThres));
                Threshold    = lerp(1.0, Threshold, UI_CA_LightsThres);

        float2  uv           = (coordIN -0.5) * UI_CA_Radius;
        float   vignette     = saturate(dot(uv.xy, uv.xy));
                vignette     = pow(vignette, 2.0);
        
        float2  rPosition;
                rPosition.x  = coordIN.x + (UI_CA_Rx / 1000 * UI_CA_OffsetMul);
                rPosition.y  = coordIN.y + (UI_CA_Ry / 1000 * UI_CA_OffsetMul);
        
        float2  gPosition;
                gPosition.x  = coordIN.x + (UI_CA_Gx / 1000 * UI_CA_OffsetMul);
                gPosition.y  = coordIN.y + (UI_CA_Gy / 1000 * UI_CA_OffsetMul);
        
        float2  bPosition;
                bPosition.x  = coordIN.x + (UI_CA_Bx / 1000 * UI_CA_OffsetMul);
                bPosition.y  = coordIN.y + (UI_CA_By / 1000 * UI_CA_OffsetMul);
        
                colorIN.x    = TextureColor.Sample(PointSampler,rPosition.xy).r;
                colorIN.y    = TextureColor.Sample(PointSampler,gPosition.xy).g;
                colorIN.z    = TextureColor.Sample(PointSampler,bPosition.xy).b;
        
                colorIN      = lerp(Origcolor, colorIN, Threshold);
                colorIN      = lerp(Origcolor, colorIN, lerp(1.0, vignette, UI_CA_RadiusThres));
    }

    return float4(colorIN, 1.0);
}

///// VIGNETTE ///////////////////////////////////////////////////////

float4 PS_Vignette(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float2  coordIN  = IN.txcoord.xy;
    float3  colorIN  = TextureColor.Sample(PointSampler, coordIN).xyz;
    
	[branch]if (UI_Enable_Vignette) 
    {
        float2  uv       = coordIN - 0.5;
        float   vignette = saturate(dot(uv.xy, uv.xy) * UI_Vignette_Amount);
        float3  vigCol   = pow(colorIN, 10.0);
                colorIN  = lerp(colorIN, vigCol, vignette);
    }
    
    return float4(colorIN, 1.0);
}

///// LETTERBOX //////////////////////////////////////////////////////

#define Hratio 1
#define cropOffset 0

float4 PS_wolCropPreview(float4 position : SV_Position, VS_OUTPUT_POST IN) : SV_Target
{
    float4 res;
    float fCropHeight  = 0.0;   // defaulting
    float fCropOffsetH = 0.0;   // defaulting
    float fCropWidth   = 0.0;   // defaulting
    float fCropOffsetW = 0.0;   // defaulting
    
    [branch] if(UI_Enable_Croppreview)
    {
        if(UI_Wratio > Hratio && (UI_Wratio/Hratio) > ScreenSize.z) // Basically, detects if portrait or landscape, or if input aspect ratio leads to something narrower than current output aspect ratio
        {
            fCropOffsetH = cropOffset;
            fCropHeight  = (1-(ScreenSize.z/UI_Wratio)*Hratio)*0.5; // Landscape
        } 
        else 
        {
            fCropOffsetW = cropOffset;
            fCropWidth   = ((ScreenSize.z - (UI_Wratio/Hratio)) * 0.5) / ScreenSize.z; // Portrait
        }
    
        if(IN.txcoord.y > 1.0f - fCropHeight + fCropOffsetH || IN.txcoord.y  < fCropHeight  + fCropOffsetH|| IN.txcoord.x > 1.0f - fCropWidth + fCropOffsetW || IN.txcoord.x  < fCropWidth  + fCropOffsetW ) // Detects if pixel in border
        {
            res = float4(0.0f, 0.0f, 0.0f, 0.0f); // Turns pixel Black...
        }   
        else 
        {
            res.xyz = TextureColor.Sample(LinearSampler, IN.txcoord.xy).rgb; // ... Or keeps it identical.
        }
    } 
    else 
    {   // If Crop Assistant not enabled, keeps the pixel the way it was.
        res.xyz = TextureColor.Sample(PointSampler, IN.txcoord.xy).rgb;
    }
    
    return float4(res.xyz, 1.0);
}

///// DITHERING //////////////////////////////////////////////////////

float4 PS_Dithering(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
	float2  coordIN  = IN.txcoord.xy;
    float3  colorIN  = TextureColor.Sample(PointSampler, coordIN).xyz;
    
	#if E_DITHERING
        
        colorIN += chromaTriDither(colorIN, coordIN, Timer.x, 8);
    
    #endif

    return float4(colorIN, 1.0);
}

///// POST PASS //////////////////////////////////////////////////////

float4 PS_PostFX(VS_OUTPUT_POST IN) : SV_Target
{
    float2  coord = IN.txcoord.xy;
    float3  color = TextureColor.Sample(PointSampler, coord).xyz;
    float   luma;
    
    ///// GLITCH FX //////////////////////////////////////////////////
    
    color  = lerp(color, Glitch(color, coord), UI_Glitch_Amount);

    ///// LUT ////////////////////////////////////////////////////////
    
    [branch] if(UI_LUT_Select) 
    {
        float3 LUT_Color = lerp(color, lerp(lerp(lutTextureNight(color), lutTextureDay(color), ENightDayFactor), lutTextureInterior(color), EInteriorFactor), UI_LUT_Amount);

        float  lumaM = inLevels(calculateLuma(color), 0.0, UI_LUT_White);
        
        if(UI_LUT_White==0) lumaM = 1.0;
        
        if(UI_LUT_MIXING_MODE==0) // Normal blend mode
        { 
            color  = lerp(color, LUT_Color, lumaM); 
        } 
        else // Color blend mode
        {
            luma   = calculateLuma(color);
            color  = lerp(color, LUT_Color, lumaM);
            color /= calculateLuma(color);
            color *= luma;
        }
    }

    ///// FILM GRAIN /////////////////////////////////////////////////
  
    float  depth  = TextureDepth.Sample(PointSampler, coord);
           depth *= rcp(mad(depth, -2999.0, 3000.0));
           luma   = 0.0;
           luma   = inLevels(saturate(1.0 - calculateLuma(color)), UI_Black_Level, UI_White_Level);
    float  mask   = 1.0;

    if(UI_Shadows_Grain==true  && UI_Depth_Grain==false) mask = luma;
    if(UI_Shadows_Grain==false && UI_Depth_Grain==true)  mask = depth;
    if(UI_Shadows_Grain==true  && UI_Depth_Grain==true)  mask = luma + depth;

    color = lerp(color, FilmGrain(color, coord), mask);
        
    if(UI_Enable_Mask) color = mask;
  
    ///// VIBRANCE ///////////////////////////////////////////////////

    color = Vibrance(color);
	
	color = ColorTemperature(color);

    ///// GAMMA CORRECTION ///////////////////////////////////////////

    [branch] if(UI_CChart)
    {
        float2  uv;
                uv.x  = Resolution.x / CustomTEX_W;
                uv.y  = Resolution.y / CustomTEX_H;
                coord = (coord - 0.5) * uv + 0.5;
                color = CCHART.Sample(LinearSampler, coord);
    }
    
    if(UI_GammaCorrection!=2.2) color = pow(color, UI_GammaCorrection - 1.2);
 
    return float4(saturate(color), 1.0);
}

///// TECHNIQUES /////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////

///// NO AA //////////////////////////////////////////////////////////

technique11 noAA_Pass <string UIName="REACTOR - no AA";>
{ pass p0 {	SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_PostFX())); } }
			
technique11 noAA_Pass1
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_CAS())); } }

technique11 noAA_Pass2
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_CA())); } }
			
technique11 noAA_Pass3
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Vignette())); } }
			
technique11 noAA_Pass4
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_wolCropPreview())); } }
			
technique11 noAA_Pass5
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Dithering())); } }			

///// FXAA ///////////////////////////////////////////////////////////

technique11 FXAA_Pass <string UIName="REACTOR - FXAA";>
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_FXAA())); } }

technique11 FXAA_Pass1
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_PostFX())); } }
			
technique11 FXAA_Pass2
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_CAS())); } }

technique11 FXAA_Pass3
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_CA())); } }
			
technique11 FXAA_Pass4
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Vignette())); } }

technique11 FXAA_Pass5
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_wolCropPreview())); } }
			
technique11 FXAA_Pass6
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Dithering())); } }			

///// SMAA ///////////////////////////////////////////////////////////

technique11 SMAA_Pass <string RenderTarget= SMAA_STRING(SMAA_EDGE_TEX); string UIName= "REACTOR - SMAA";>
{ pass Clear { SetVertexShader(CompileShader(vs_5_0, VS_SMAAClear()));
               SetPixelShader (CompileShader(ps_5_0, PS_SMAAClear())); }

    pass EdgeDetection 
    { SetVertexShader(CompileShader(vs_5_0, VS_SMAAEdgeDetection()));
      SetPixelShader (CompileShader(ps_5_0, PS_SMAAEdgeDetection())); } }

technique11 SMAA_Pass1 <string RenderTarget=SMAA_STRING(SMAA_BLEND_TEX);>
{ pass Clear { SetVertexShader(CompileShader(vs_5_0, VS_SMAAClear()));
               SetPixelShader (CompileShader(ps_5_0, PS_SMAAClear())); }
    
    pass BlendingWeightCalculation 
    { SetVertexShader(CompileShader(vs_5_0, VS_SMAABlendingWeightCalculation()));
      SetPixelShader (CompileShader(ps_5_0, PS_SMAABlendingWeightCalculation())); } }

technique11 SMAA_Pass2
{ pass NeighborhoodBlending
          { SetVertexShader(CompileShader(vs_5_0, VS_SMAANeighborhoodBlending()));
            SetPixelShader (CompileShader(ps_5_0, PS_SMAANeighborhoodBlending())); } }

technique11 SMAA_Pass3
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_PostFX())); } }
			
technique11 SMAA_Pass4
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_CAS())); } }

technique11 SMAA_Pass5
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_CA())); } }
			
technique11 SMAA_Pass6
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Vignette())); } }

technique11 SMAA_Pass7
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_wolCropPreview())); } }
			
technique11 SMAA_Pass8
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Draw()));
            SetPixelShader (CompileShader(ps_5_0, PS_Dithering())); } }			