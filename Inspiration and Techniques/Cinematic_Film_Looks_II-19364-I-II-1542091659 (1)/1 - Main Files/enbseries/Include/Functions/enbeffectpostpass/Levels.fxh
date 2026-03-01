//-----------------------LEVELS------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

    #define Levels_black_point LIFT_BLACK * -1
    #define Levels_white_point 235 + (255 - LOWER_WHITE)
    #define black_point_float ( Levels_black_point / 255.0 )
    #define white_point_float ( 255.0 / (Levels_white_point - Levels_black_point))
    #define cal_white 255 - CLIP_WHITE
    #define cal_black_point_float (CLIP_BLACK/ 255.0 )
    #define cal_white_point_float ( 255.0 / (cal_white - CLIP_BLACK))

    float4 LevelsPass( float4 colorInput ) {
      colorInput.rgb = colorInput.rgb * cal_white_point_float - (cal_black_point_float *  cal_white_point_float);

      return colorInput;
    }

    float4 LevelsPass2( float4 colorInput ) {
      colorInput.rgb = colorInput.rgb * white_point_float - (black_point_float *  white_point_float);

      return colorInput;
    }
