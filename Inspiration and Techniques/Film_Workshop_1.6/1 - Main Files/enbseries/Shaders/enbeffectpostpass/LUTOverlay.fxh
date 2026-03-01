Texture2D  LutOverlay  <string ResourceName = "Textures/Helper/LUT.png";>;

float3 ShowLut( float3 color, float2 coord ) {
    coord   *= ScreenSize.x;
    coord.y *= ScreenSize.w;

    float2 LutSize;

    LutOverlay.GetDimensions(LutSize.x, LutSize.y);

    if(coord.x > LutSize.x || coord.y > LutSize.y) return color;
    else return LutOverlay.Sample(Sampler0, coord / LutSize);
}
