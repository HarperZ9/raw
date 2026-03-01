// LETTERBOX / PILLARBOX SHADER ////////////////////
////////////////////////////////////////////////////
// - By Marty McFly                               //
////////////////////////////////////////////////////

// FUNCTION //////////////////////////////////////
    float4 BorderPass( float4 color, float2 txcoord )
    {
    	float2 ntex = txcoord * 2.0 - 1.0;

        [flatten]
            if(BORDER_RATIO < ScreenSize.z) ntex.x *= ScreenSize.z / BORDER_RATIO;
            else ntex.y /= ScreenSize.z / BORDER_RATIO;

        float letterbox = !all(saturate(1.0 - ntex * ntex));
        float3 letterbox_color = lerp(0.0, 1.0, (BORDER_LUMA * 0.01));

        color.rgb = lerp(color.rgb, letterbox_color, BORDER_OPACITY * 0.01 * letterbox);
        return color;
    }
