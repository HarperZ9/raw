//----------------------VIBRANCE-----------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

    float4 VibrancePass( float4 colorInput ) {
        #define Vibrance_RGB_balance float3(1.00,1.00,1.00)
        #define Vibrance_coeff float3(Vibrance_RGB_balance * (VibranceAmt * 0.01))

        float4 color = colorInput; //original input color
        float3 lumCoeff = float3(0.212656, 0.715158, 0.072186);  //Values to calculate luma with

        float luma = dot(lumCoeff, color.rgb); //calculate luma (grey)

        float max_color = max(colorInput.r, max(colorInput.g,colorInput.b)); //Find the strongest color
        float min_color = min(colorInput.r, min(colorInput.g,colorInput.b)); //Find the weakest color

        float color_saturation = max_color - min_color; //The difference between the two is the saturation

        //color.rgb = lerp(luma, color.rgb, (1.0 + (Vibrance * (1.0 - (sign(Vibrance) * color_saturation))))); //extrapolate between luma and original by 1 + (1-saturation) - current
        color.rgb = lerp(luma, color.rgb, (1.0 + (Vibrance_coeff * (1.0 - (sign(Vibrance_coeff) * color_saturation))))); //extrapolate between luma and original by 1 + (1-saturation) - current

        return color; //return the result
    }
