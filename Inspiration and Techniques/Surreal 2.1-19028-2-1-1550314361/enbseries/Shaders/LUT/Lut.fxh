// 3DLut code by kingeric1992
// Edits and options by Adyss
#define AutoSize 1    // If 1 the Lut size will be detected automatically with a small performance dip. If 0 the size has the be in the function or pass 
#define Switcher 1    // If 1 youll need to setup an ingame menu. Switch by Numbers. If 2 switch by technique wich also needs a manual setup

Texture2D           Kodachrome              <string ResourceName="Shaders/LUT/Kodachrome.png"; >;  

float3 Lut( float3 colorIN, Texture2D lutTexIn, float2 lutSize ) {
    float2 CLut_pSize = 1.0 / lutSize;
    float4 CLut_UV;
    colorIN    = saturate(colorIN) * ( lutSize.y - 1.0);
    CLut_UV.w  = floor(colorIN.b);
    CLut_UV.xy = (colorIN.rg + 0.5) * CLut_pSize;
    CLut_UV.x += CLut_UV.w * CLut_pSize.y;
    CLut_UV.z  = CLut_UV.x + CLut_pSize.y;
    return       lerp (lutTexIn.SampleLevel(LinearSampler, CLut_UV.xy, 0).rgb, 
                       lutTexIn.SampleLevel(LinearSampler, CLut_UV.zy, 0).rgb, colorIN.b - CLut_UV.w);
}

#if AutoSize == 1
//function overload
float3 Lut( float3 colorIN, Texture2D lutTexIn) {
    float2 lutsize;
    lutTexIn.GetDimensions(lutsize.x, lutsize.y);
    return Lut(colorIN, lutTexIn, lutsize);
}
#endif

float3 LUTassembly(float3 Color)
{
    Color = Lut(Color, Kodachrome);
    return Color;
}
