float4 VignettePass( float4 colorInput, float2 tex )
{
    //Set the center
    float2 distance_xy = tex - VignetteCenter;

    //Adjust the ratio
    {
          distance_xy *= float2((pixel.y / pixel.x),VignetteRatio);
     }

    //Calculate the distance
    distance_xy /= VignetteRadius;
    float distance = dot(distance_xy,distance_xy);

    //Apply the vignette
    colorInput.rgb *= (1.0 + pow(distance, VignetteSlope * 0.5) * VignetteAmount); //pow - multiply

return colorInput;
}
