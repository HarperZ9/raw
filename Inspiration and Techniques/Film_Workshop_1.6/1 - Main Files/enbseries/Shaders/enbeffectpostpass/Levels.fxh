//-----------------------LEVELS------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

   #define black_point_float ( Levels_black_point / 255.0 )

#if (Levels_white_point == Levels_black_point) //avoid division by zero if the white and black point are the same
  #define white_point_float ( 255.0 / 0.00025)
#else
  #define white_point_float ( 255.0 / (Levels_white_point - Levels_black_point))
#endif

#define cal_black_point_float ( (cal_black + 7) / 255.0 )
#define cal_white_point_float ( 255.0 / (cal_white - cal_black))


float4 LevelsPass( float4 colorInput )
{
  colorInput.rgb = colorInput.rgb * white_point_float - (black_point_float *  white_point_float);

  return colorInput;
}

float4 LevelsPass2( float4 colorInput )
{
  colorInput.rgb = colorInput.rgb * cal_white_point_float - (cal_black_point_float *  cal_white_point_float);

  return colorInput;
}
