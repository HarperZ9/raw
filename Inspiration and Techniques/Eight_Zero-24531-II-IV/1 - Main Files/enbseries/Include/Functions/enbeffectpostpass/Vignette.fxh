/**
 * Vignette version 1.3
 * by Christian Cann Schuldt Jensen ~ CeeJay.dk
 *
 * Darkens the edges of the image to make it look more like it was shot with a camera lens.
 * May cause banding artifacts.
 */

 #define BlendSoftLightf(base, blend) 	((blend < 0.5) ? (2.0 * base * blend + base * base * (1.0 - 2.0 * blend)) : (sqrt(base) * (2.0 * blend - 1.0) + 2.0 * base * (1.0 - blend)))

float4 VignettePass( float4 colorInput, float2 tex ) {
    colorInput = TextureColor.Sample(SamplerPoint, tex.xy);
    float4 vignette = 0.5;

    //Set the center
    float2 distance_xy = tex - float2(0.500, 0.500);

    //Adjust the ratio
    distance_xy *= float2((pixel.y / pixel.x), ENABLE_BORDER == true && TV_OVERLAY == false ? lerp(ScreenSize.z, BORDER_RATIO, ENABLE_BORDER) : lerp(ScreenSize.z, 1.33, TV_OVERLAY));

    //Calculate the distance
    distance_xy /= 3.0; //lerp(2.5, 2.25, (VIGNETTE_AMOUNT * 0.01));
    float distance = dot(distance_xy,distance_xy);

    // Apply the vignette to middle grey
    vignette.rgb *= (1.0 + pow(distance, lerp(1.0, 0.75, (VIGNETTE_WIDTH * 0.04))) * lerp(0, -3.8, (VIGNETTE_AMOUNT * 0.01))); //pow - multiply

    // Blend the vignette with image using SoftLight
    colorInput.rgb = BlendSoftLightf(colorInput, vignette);

return colorInput;
}
