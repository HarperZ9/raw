//------------------------SEPIA------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

float4 AgePass( float4 colorInput )
{
    float GreyPower = (((GRADE_AGE * 0.0255) * 0.09) * -1);
    float3 age_color[2] =
    {
        // Sepia
        float3(1.000, 0.945, 0.900),
        // Magenta
        float3(1.000, 0.920, 0.945)
    };

	// calculating amounts of input, grey and sepia colors to blend and combine
	float  grey   = dot(colorInput.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 sepia  = colorInput.rgb * age_color[AGE_MODE - 1];
    float3 blend2 = (grey * GreyPower) + (colorInput.rgb / (GreyPower + 1));
	return float4(lerp(blend2, sepia, (GRADE_AGE * 0.0255)), colorInput.a); 	// returning the final color
}
