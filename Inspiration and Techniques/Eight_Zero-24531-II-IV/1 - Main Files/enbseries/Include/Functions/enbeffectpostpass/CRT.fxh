////////////////////////////////////////////////////
// Simple CRT Overlay Shader                      //
// - TreyM                                        //
////////////////////////////////////////////////////

TEXTURE(SimpleHorz, "Include/Textures/CRT/simple_horizontal.png")
TEXTURE(SimpleVert, "Include/Textures/CRT/simple_vertical.png")
TEXTURE(Simple45, "Include/Textures/CRT/simple_45.png")
TEXTURE(RGBHorz, "Include/Textures/CRT/rgb_horizontal.png")
TEXTURE(RGBVert, "Include/Textures/CRT/rgb_vertical.png")
TEXTURE(RGBStacked, "Include/Textures/CRT/rgb_stacked.png")
TEXTURE(RGBArray1, "Include/Textures/CRT/rgb_array_1.png")
TEXTURE(RGBArray2, "Include/Textures/CRT/rgb_array_2.png")


#define BlendSoftLightf(base, blend) 	((blend < 0.5) ? (2.0 * base * blend + base * base * (1.0 - 2.0 * blend)) : (sqrt(base) * (2.0 * blend - 1.0) + 2.0 * base * (1.0 - blend)))
float4 PS_CRT(VS_OUTPUT_POST IN, float4 v0 : SV_Position0) : SV_Target
{
    float4 color;
    color = TextureColor.Sample(SamplerPoint, IN.txcoord0.xy);
    #if(ENB_QUALITY == 2)
        if( !ENABLE_CRT || ENABLE_NTSC) return color;
    #else
        if( !ENABLE_CRT) return color;
    #endif
    float4 mask;
    float4 mix;
    int2 tex_size;

    if( CRT_MODE == 1 )
    {
      SimpleHorz.GetDimensions(tex_size.x, tex_size.y);
      mask = SimpleHorz.Load(int3(IN.pos.xy % tex_size.xy, 0));
      mix = BlendSoftLightf(color, mask);
      color = pow(mix, 0.8);
    }

    if( CRT_MODE == 2 )
    {
      SimpleVert.GetDimensions(tex_size.x, tex_size.y);
      mask = SimpleVert.Load(int3(IN.pos.xy % tex_size.xy, 0));
      mix = BlendSoftLightf(color, mask);
      color = pow(mix, 0.8);
    }

    if( CRT_MODE == 3 )
    {
      Simple45.GetDimensions(tex_size.x, tex_size.y);
      mask = Simple45.Load(int3(IN.pos.xy % tex_size.xy, 0));
      mix = BlendSoftLightf(color, mask);
      color = pow(mix, 0.9);
    }

    if( CRT_MODE == 4 )
    {
      RGBHorz.GetDimensions(tex_size.x, tex_size.y);
      mask = RGBHorz.Load(int3(IN.pos.xy % tex_size.xy, 0));
      mix = BlendSoftLightf(color, mask);
      color = pow(mix, 1.1);
    }

    if( CRT_MODE == 5 )
    {
      RGBVert.GetDimensions(tex_size.x, tex_size.y);
      mask = RGBVert.Load(int3(IN.pos.xy % tex_size.xy, 0));
      mix = BlendSoftLightf(color, mask);
      color = pow(mix, 1.1);
    }

    if( CRT_MODE == 6 )
    {
      RGBArray1.GetDimensions(tex_size.x, tex_size.y);
      mask = RGBArray1.Load(int3(IN.pos.xy % tex_size.xy, 0));
      mix = BlendSoftLightf(color, mask);
      color = pow(mix, 0.9);
    }

    if( CRT_MODE == 7 )
    {
      RGBArray2.GetDimensions(tex_size.x, tex_size.y);
      mask = RGBArray2.Load(int3(IN.pos.xy % tex_size.xy, 0));
      mix = BlendSoftLightf(color, mask);
      color = mix;
    }

    if( CRT_MODE == 8 )
    {
      RGBStacked.GetDimensions(tex_size.x, tex_size.y);
      mask = RGBStacked.Load(int3(IN.pos.xy % tex_size.xy, 0));
      mix = BlendSoftLightf(color, mask);
      color = mix;
    }

    return color;
}
