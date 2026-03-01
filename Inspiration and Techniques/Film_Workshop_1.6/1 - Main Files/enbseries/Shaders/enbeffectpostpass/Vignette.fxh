float4 VignettePass( float4 colorInput, float2 tex )
{
    //Set the center
    float2 distance_xy = tex - VignetteCenter;

    //Adjust the ratio
    {
          distance_xy *= float2((pixel.y / pixel.x),VignetteRatio);
     }

    //Calculate the distance
    #if(MONITOR_219 == 0)
    distance_xy /= VignetteRadius;
    #else
    distance_xy /= (VignetteRadius + 0.5);
    #endif
    float distance = dot(distance_xy,distance_xy);

    //Apply the vignette
    colorInput.rgb *= (1.0 + pow(distance, VignetteSlope * 0.5) * VignetteAmount); //pow - multiply

return colorInput;
}
