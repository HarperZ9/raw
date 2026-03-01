#define Red_Green_Blue float3

void ColorCorrection(int ppfx_preset                        ,
                    inout float  Levels_Input_Black         ,
                    inout float  Levels_Input_White         ,
                    inout float  Levels_Output_Black        ,
                    inout float  Levels_Output_White        ,
                    inout float3 Color_Balance_Shadows      ,
                    inout float3 Color_Balance_Highlights   ,
                    inout float3 ChannelMixer_Red           ,
                    inout float3 ChannelMixer_Green         ,
                    inout float3 ChannelMixer_Blue          ,
                    inout float  Color_Temperature_Strength ,
                    inout float  Color_Temperature_Kelvin   ,
                    inout float  Gamma                      ,
                    inout float  Brightness                 ,
                    inout float  Contrast                   ,
                    inout float  Saturation                 ,
                    inout float  Chromatic_Aberration       ,
                    inout float  Bleach_Bypass              )
{
    switch (ppfx_preset)
    {
    case 1:
		// Color Boost Filter
        Levels_Input_Black        = 0;
        Levels_Input_White        = 1;
        Levels_Output_Black       = 0;
        Levels_Output_White       = 1;
        Color_Balance_Shadows     = Red_Green_Blue(0.500,0.500,0.500);
        Color_Balance_Highlights  = Red_Green_Blue(0.500,0.500,0.500);
        ChannelMixer_Red          = Red_Green_Blue(1,0,0);
        ChannelMixer_Green        = Red_Green_Blue(0,1,0);
        ChannelMixer_Blue         = Red_Green_Blue(0,0,1);
        Color_Temperature_Strength= 1.00;
        Color_Temperature_Kelvin  = 5800;
        Gamma                     = 1.00;
        Brightness                = 1.00;
        Contrast                  = 0.95;
        Saturation                = 1.25;
        Chromatic_Aberration      = 0.00;
        Bleach_Bypass             = 0.30;
        break;
    case 2:
        // Warm Filter
        Levels_Input_Black        = 0;
        Levels_Input_White        = 1;
        Levels_Output_Black       = 0;
        Levels_Output_White       = 1;
        Color_Balance_Shadows     = Red_Green_Blue(0.498,0.498,0.525);
        Color_Balance_Highlights  = Red_Green_Blue(0.522,0.525,0.498);
        ChannelMixer_Red          = Red_Green_Blue(1,0,0);
        ChannelMixer_Green        = Red_Green_Blue(0,1,0);
        ChannelMixer_Blue         = Red_Green_Blue(0,0,1);
        Color_Temperature_Strength= 0.95;
        Color_Temperature_Kelvin  = 3500;
        Gamma                     = 1;
        Brightness                = 0.98;
        Contrast                  = 0.90;
        Saturation                = 1.20;
        Chromatic_Aberration      = 0.00;
        Bleach_Bypass             = 0.30;
        break;
    case 3:
        // Vintage Filter
        Levels_Input_Black        = 0;
        Levels_Input_White        = 1;
        Levels_Output_Black       = 0;
        Levels_Output_White       = 1;
        Color_Balance_Shadows     = Red_Green_Blue(0.388,0.428,0.508);
        Color_Balance_Highlights  = Red_Green_Blue(0.658,0.455,0.415);
        ChannelMixer_Red          = Red_Green_Blue(1,0,0);
        ChannelMixer_Green        = Red_Green_Blue(0,1,0.3);
        ChannelMixer_Blue         = Red_Green_Blue(0,0,1);
        Color_Temperature_Strength= 1.00;
        Color_Temperature_Kelvin  = 4000;
        Gamma                     = 1;
        Brightness                = 1.00;
        Contrast                  = 0.80;
        Saturation                = 0.85;
        Chromatic_Aberration      = 0.00;
        Bleach_Bypass             = 0.00;
        break;
    case 4:
		// Retro Filter
        Levels_Input_Black        = 0;
        Levels_Input_White        = 1;
        Levels_Output_Black       = 0;
        Levels_Output_White       = 1;
        Color_Balance_Shadows     = Red_Green_Blue(0.500,0.510,0.550);
        Color_Balance_Highlights  = Red_Green_Blue(0.600,0.500,0.500);
        ChannelMixer_Red          = Red_Green_Blue(1,0,0);
        ChannelMixer_Green        = Red_Green_Blue(0,1,0.25);
        ChannelMixer_Blue         = Red_Green_Blue(0,0,1);
        Color_Temperature_Strength= 1.00;
        Color_Temperature_Kelvin  = 4500;
        Gamma                     = 1.00;
        Brightness                = 0.90;
        Contrast                  = 0.95;
        Saturation                = 0.80;
        Chromatic_Aberration      = 0.00;
        Bleach_Bypass             = 0.00;
        break;
    case 5:
		// VHS Filter
        Levels_Input_Black        = 0;
        Levels_Input_White        = 1;
        Levels_Output_Black       = 0;
        Levels_Output_White       = 1;
        Color_Balance_Shadows     = Red_Green_Blue(0.510,0.460,0.510);
        Color_Balance_Highlights  = Red_Green_Blue(0.500,0.500,0.500);
        ChannelMixer_Red          = Red_Green_Blue(1,0,0);
        ChannelMixer_Green        = Red_Green_Blue(0,1,0);
        ChannelMixer_Blue         = Red_Green_Blue(0,0,1);
        Color_Temperature_Strength= 1.00;
        Color_Temperature_Kelvin  = 6000;
        Gamma                     = 1.00;
        Brightness                = 0.90;
        Contrast                  = 0.70;
        Saturation                = 1.50;
        Chromatic_Aberration      = 0.80;
        Bleach_Bypass             = 0.70;
        break;
        case 6:
		// Old Camera Filter
        Levels_Input_Black        = 0;
        Levels_Input_White        = 1;
        Levels_Output_Black       = 0;
        Levels_Output_White       = 1;
        Color_Balance_Shadows     = Red_Green_Blue(0.510,0.460,0.510);
        Color_Balance_Highlights  = Red_Green_Blue(0.500,0.500,0.500);
        ChannelMixer_Red          = Red_Green_Blue(1,0,0);
        ChannelMixer_Green        = Red_Green_Blue(0,1,0);
        ChannelMixer_Blue         = Red_Green_Blue(0,0,1);
        Color_Temperature_Strength= 1.00;
        Color_Temperature_Kelvin  = 6000;
        Gamma                     = 1.00;
        Brightness                = 0.90;
        Contrast                  = 0.70;
        Saturation                = 1.50;
        Chromatic_Aberration      = 0.95;
        Bleach_Bypass             = 0.70;
        break;
    case 7:
		// Matrix Filter
        Levels_Input_Black        = 0;
        Levels_Input_White        = 1;
        Levels_Output_Black       = 0;
        Levels_Output_White       = 1;
        Color_Balance_Shadows     = Red_Green_Blue(0.500,0.500,0.500);
        Color_Balance_Highlights  = Red_Green_Blue(0.450,0.450,0.450);
        ChannelMixer_Red          = Red_Green_Blue(1,0,0);
        ChannelMixer_Green        = Red_Green_Blue(0,1.2,0.15);
        ChannelMixer_Blue         = Red_Green_Blue(0,0.05,1);
        Color_Temperature_Strength= 1.00;
        Color_Temperature_Kelvin  = 6500;
        Gamma                     = 1.00;
        Brightness                = 0.85;
        Contrast                  = 1.00;
        Saturation                = 0.50;
        Chromatic_Aberration      = 0.00;
        Bleach_Bypass             = 0.15;
        break;
    case 8:
		// Noir Filter
        Levels_Input_Black        = 0;
        Levels_Input_White        = 1;
        Levels_Output_Black       = 0;
        Levels_Output_White       = 1;
        Color_Balance_Shadows     = Red_Green_Blue(0.500,0.500,0.500);
        Color_Balance_Highlights  = Red_Green_Blue(0.455,0.455,0.455);
        ChannelMixer_Red          = Red_Green_Blue(1,0,0);
        ChannelMixer_Green        = Red_Green_Blue(0,1,0);
        ChannelMixer_Blue         = Red_Green_Blue(0,0,1);
        Color_Temperature_Strength= 0.00;
        Color_Temperature_Kelvin  = 6500;
        Gamma                     = 1.00;
        Brightness                = 1.00;
        Contrast                  = 1.00;
        Saturation                = 0.00;
        Chromatic_Aberration      = 0.00;
        Bleach_Bypass             = 0.30;
        break;
    default:
        break;
    }
    
}