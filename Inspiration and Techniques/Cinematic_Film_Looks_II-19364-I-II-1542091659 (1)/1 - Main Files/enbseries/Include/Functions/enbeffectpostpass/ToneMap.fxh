    float4 TonemapPass(float4 colorInput) {
    	float3 color = colorInput.rgb;

        // Exsposure Adjustments
    	color *= pow(2.0f, (CAM_EXPOSURE + (CAM_TEMP * 0.001) + (CAM_TINT * 0.001) * 5) - 1.0);

        // Color Temperature and Tint
    	color = saturate(color - (CAM_TEMP * 0.001) * float3(0.85,1.70,2.55)   // Color Temperature
                               - (CAM_TINT * 0.001) * float3(2.55,0.85,1.70)); // Color Tint

    	return float4(color, 1.0);
    }
