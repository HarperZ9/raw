//-----------------------CURVES------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

    float4 CurvesPassLuma( float4 colorInput ) {
        float3 lumCoeff = float3(0.2126, 0.7152, 0.0722);  //Values to calculate luma with
        float Curves_contrast_blend = (CURVES_CONTRAST * 0.01);

        #ifndef PI
        #define PI 3.1415927
        #endif

        //calculate luma (grey)
        float luma = dot(lumCoeff, colorInput.rgb);

        //calculate chroma
        float3 chroma = colorInput.rgb - luma;

        float x = luma; //if the curve should be applied to Luma

        x = x - 0.5;
        x = ( x / (0.5 + abs(x)) ) + 0.5;

        /*-----------------------------------------------------------.
        /                 Joining of Luma and Chroma                  /
        '-----------------------------------------------------------*/

        x = lerp(luma, x, Curves_contrast_blend); //Blend by Curves_contrast_luma
        colorInput.rgb = x + chroma; //Luma + Chroma

        return colorInput;
    }

    float4 CurvesPassChroma( float4 colorInput ) {
        float3 lumCoeff = float3(0.2126, 0.7152, 0.0722);  //Values to calculate luma with
        float Curves_contrast_blend = ((CURVES_SATURATION -100) * 0.01);

        #ifndef PI
         #define PI 3.1415927
        #endif

        //calculate luma (grey)
        float luma = dot(lumCoeff, colorInput.rgb);

        //calculate chroma
        float3 chroma = colorInput.rgb - luma;

         float3 x = chroma; //if the curve should be applied to Chroma
         x = x * 0.5 + 0.5; //adjust range of Chroma from -1 -> 1 to 0 -> 1


         x = x - 0.5;
         x = ( x / (0.5 + abs(x)) ) + 0.5;

         //x = ( (x - 0.5) / (0.5 + abs(x-0.5)) ) + 0.5;

        /*-----------------------------------------------------------.
        /                 Joining of Luma and Chroma                  /
        '-----------------------------------------------------------*/

        x = x * 2.0 - 1.0; //adjust the Chroma range back to -1 -> 1
        float3 color = luma + x; //Luma + Chroma
        colorInput.rgb = lerp(colorInput.rgb, color, Curves_contrast_blend);

        return colorInput;
    }
