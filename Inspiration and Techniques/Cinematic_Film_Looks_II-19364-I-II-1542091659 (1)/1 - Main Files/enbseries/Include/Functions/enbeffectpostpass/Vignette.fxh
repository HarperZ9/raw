/**
 * Vignette version 1.3
 * by Christian Cann Schuldt Jensen ~ CeeJay.dk
 *
 * Darkens the edges of the image to make it look more like it was shot with a camera lens.
 * May cause banding artifacts.
 */

float4 VignettePass( float4 colorInput, float2 tex ) {
    #define screen_size float2(ScreenSize.x,ScreenSize.x*ScreenSize.w)
    #define screen_ratio (screen_size.x / screen_size.y)
    #define pixel float2(ScreenSize.y,ScreenSize.y*ScreenSize.z)
    colorInput = TextureColor.Sample(Sampler0, tex.xy);
    float4 vignette = 0.5;
    //Set the center
    float2 distance_xy = tex - float2(0.500, 0.500);

    //Adjust the ratio
    distance_xy *= float2((pixel.y / pixel.x), lerp(ScreenSize.z, BORDER_RATIO, ENABLE_BORDER));

    //Calculate the distance
    distance_xy /= 3.0; //lerp(2.5, 2.25, (VIGNETTE_AMOUNT * 0.01));
    float distance = dot(distance_xy,distance_xy);

    // Apply the vignette to middle grey
    if(ENABLE_LENS != 0 && VIGNETTE_AMOUNT != 0) vignette.rgb *= (1.0 + pow(distance, lerp(1.0, 0.75, (VIGNETTE_WIDTH * 0.04))) * lerp(0, -3.8, (VIGNETTE_AMOUNT * 0.01))); //pow - multiply

    // Convert to sRGB from LOG
    colorInput.rgb = Log2Lin(colorInput.rgb);

    colorInput = pow(colorInput, CAM_GAMMA);

    // Blend the vignette with image using SoftLight
    if(ENABLE_LENS != 0 && VIGNETTE_AMOUNT != 0) colorInput.rgb = BlendSoftLightf(colorInput, vignette);

    // Convert back to LOG
    colorInput.rgb = Lin2Log(colorInput.rgb);

return colorInput;
}
