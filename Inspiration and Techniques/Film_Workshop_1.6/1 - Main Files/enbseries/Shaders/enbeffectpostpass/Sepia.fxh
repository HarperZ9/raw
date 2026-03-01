//------------------------SEPIA------------------------//
// by Christian Cann Schuldt Jensen ~ CeeJay.dk        //
//+++++++++++++++++++++++++++++++++++++++++++++++++++++//

float4 SepiaPass( float4 colorInput )
{
	// calculating amounts of input, grey and sepia colors to blend and combine
	float  grey   = dot(colorInput.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 sepia  = colorInput.rgb * ColorToneSepia;
    float3 blend2 = (grey * GreyPower) + (colorInput.rgb / (GreyPower + 1));
	return float4(lerp(blend2, sepia, SepiaPower), colorInput.a); 	// returning the final color
}

float4 MagentaPass( float4 colorInput )
{
	// calculating amounts of input, grey and sepia colors to blend and combine
	float  grey   = dot(colorInput.rgb, float3(0.2126, 0.7152, 0.0722));
    float3 sepia  = colorInput.rgb * ColorToneMagenta;
    float3 blend2 = (grey * GreyPower) + (colorInput.rgb / (GreyPower + 1));
	return float4(lerp(blend2, sepia, SepiaPower), colorInput.a); 	// returning the final color
}
