//-----------------------LEVELS------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

   #define black_point_float ( Levels_black_point / 255.0 )

#if (Levels_white_point == Levels_black_point) //avoid division by zero if the white and black point are the same
  #define white_point_float ( 255.0 / 0.00025)
#else
  #define white_point_float ( 255.0 / (Levels_white_point - Levels_black_point))
#endif

#define cal_black_point_float ( cal_black / 255.0 )
#define cal_white_point_float ( 255.0 / (cal_white - cal_black))


float4 CalibrationPass( float4 colorInput )
{
  colorInput.rgb = colorInput.rgb * cal_white_point_float - (cal_black_point_float *  cal_white_point_float);

  #if (Levels_highlight_clipping == 1)

    float3 clipped_colors = any(colorInput.rgb > saturate(colorInput.rgb)) //any colors whiter than white?
                    ? float3(1.0, 0.0, 0.0)
                    : colorInput.rgb;

    clipped_colors = all(colorInput.rgb > saturate(colorInput.rgb)) //all colors whiter than white?
                    ? float3(1.0, 1.0, 0.0)
                    : clipped_colors;

    clipped_colors = any(colorInput.rgb < saturate(colorInput.rgb)) //any colors blacker than black?
                    ? float3(0.0, 0.0, 1.0)
                    : clipped_colors;

    clipped_colors = all(colorInput.rgb < saturate(colorInput.rgb)) //all colors blacker than black?
                    ? float3(0.0, 1.0, 1.0)
                    : clipped_colors;

    colorInput.rgb = clipped_colors;

  #endif

  return colorInput;
}
