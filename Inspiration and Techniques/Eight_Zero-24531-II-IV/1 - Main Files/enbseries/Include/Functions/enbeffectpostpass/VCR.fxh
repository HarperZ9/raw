////////////////////////////////////////////////////
// Simple Image Overlay Shader                    //
// - TreyM                                        //
////////////////////////////////////////////////////

TEXTURE(VCRPlay, "Include/Textures/VCR/VCRPlay.png")
TEXTURE(VCRRW, "Include/Textures/VCR/VCRRewind.png")
TEXTURE(VCRFF, "Include/Textures/VCR/VCRFF.png")
TEXTURE(VCRCal, "Include/Textures/VCR/VCRCalibrate.png")
TEXTURE(VCRStop, "Include/Textures/VCR/VCRStop.png")
TEXTURE(VCRFBI, "Include/Textures/VCR/VCRFBI.png")

TEXTURE(VCRPlay219, "Include/Textures/VCR/VCRPlay219.png")
TEXTURE(VCRRW219, "Include/Textures/VCR/VCRRewind219.png")
TEXTURE(VCRFF219, "Include/Textures/VCR/VCRFF219.png")
TEXTURE(VCRCal219, "Include/Textures/VCR/VCRCalibrate219.png")
TEXTURE(VCRStop219, "Include/Textures/VCR/VCRStop219.png")
TEXTURE(VCRFBI219, "Include/Textures/VCR/VCRFBI219.png")

TEXTURE(TutWelcome, "Include/Textures/Tutorial/welcome.png")
TEXTURE(TutContents, "Include/Textures/Tutorial/contents.png")
TEXTURE(TutPreset, "Include/Textures/Tutorial/preset.png")
TEXTURE(TutLens, "Include/Textures/Tutorial/lens.png")
TEXTURE(TutVCR1, "Include/Textures/Tutorial/vcr_1.png")
TEXTURE(TutVCR2, "Include/Textures/Tutorial/vcr_2.png")
TEXTURE(TutTV, "Include/Textures/Tutorial/tv.png")
TEXTURE(TutFormat, "Include/Textures/Tutorial/format.png")

TEXTURE(TutWelcome219, "Include/Textures/Tutorial/welcome219.png")
TEXTURE(TutContents219, "Include/Textures/Tutorial/contents219.png")
TEXTURE(TutPreset219, "Include/Textures/Tutorial/preset219.png")
TEXTURE(TutLens219, "Include/Textures/Tutorial/lens219.png")
TEXTURE(TutVCR1219, "Include/Textures/Tutorial/vcr219_1.png")
TEXTURE(TutVCR2219, "Include/Textures/Tutorial/vcr219_2.png")
TEXTURE(TutTV219, "Include/Textures/Tutorial/tv219.png")
TEXTURE(TutFormat219, "Include/Textures/Tutorial/format219.png")

TEXTURE(TutBG, "Include/Textures/Tutorial/blue.png")

#define BlendScreenf(base, blend) 		(1.0 - ((1.0 - base) * (1.0 - blend)))

float3 VCROSDPass(float3 Color, float2 txCoord)
{
    float4 orig = TextureColor.Sample(SamplerPoint, txCoord);
    float4 overlay;
    float4 sourceCoordFactor = 1;
    float2 coords = (txCoord.xy * sourceCoordFactor.xy) - (1.0-sourceCoordFactor.zw) * 0.5;

    if(ScreenSize.z < 2.0)
    {
        if( VCR_SELECT == 1 ) overlay = VCRPlay.Sample(SamplerPoint, coords);
        if( VCR_SELECT == 2 ) overlay = VCRRW.Sample(SamplerPoint, coords);
        if( VCR_SELECT == 3 ) overlay = VCRFF.Sample(SamplerPoint, coords);
        if( VCR_SELECT == 4 ) overlay = VCRCal.Sample(SamplerPoint, coords);
        if( VCR_SELECT == 5 )
        {
          overlay = VCRStop.Sample(SamplerPoint, coords);
          return overlay;
        }
        if( VCR_SELECT == 6 )
        {
          overlay = VCRFBI.Sample(SamplerPoint, coords);
          return overlay;
        }
    }
    else
    {
        if( VCR_SELECT == 1 ) overlay = VCRPlay219.Sample(SamplerPoint, coords);
        if( VCR_SELECT == 2 ) overlay = VCRRW219.Sample(SamplerPoint, coords);
        if( VCR_SELECT == 3 ) overlay = VCRFF219.Sample(SamplerPoint, coords);
        if( VCR_SELECT == 4 ) overlay = VCRCal219.Sample(SamplerPoint, coords);
        if( VCR_SELECT == 5 )
        {
          overlay = VCRStop219.Sample(SamplerPoint, coords);
          return overlay;
        }
        if( VCR_SELECT == 6 )
        {
          overlay = VCRFBI219.Sample(SamplerPoint, coords);
          return overlay;
        }
    }


    return BlendScreenf(orig, overlay);
}

float3 TutorialPass(float3 Color, float2 txCoord)
{
    float4 orig = TextureColor.Sample(SamplerPoint, txCoord);
    float4 overlay;
    float4 sourceCoordFactor = 1;
    float2 coords = (txCoord.xy * sourceCoordFactor.xy) - (1.0-sourceCoordFactor.zw) * 0.5;

    orig.rgb = dot(float3(0.21, 0.72, 0.07), orig.rgb);
    orig = lerp(0.0, orig, 0.25);

    overlay = TutBG.Sample(SamplerPoint, coords);

    orig = lerp(overlay, orig, 0.4);

    if(ScreenSize.z < 2.0)
    {
        if( TUTORIAL_PAGE == 1 ) overlay = TutContents.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 2 ) overlay = TutPreset.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 3 ) overlay = TutLens.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 4 ) overlay = TutVCR1.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 5 ) overlay = TutVCR2.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 6 ) overlay = TutTV.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 7 ) overlay = TutFormat.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 8 ) overlay = TutWelcome.Sample(SamplerPoint, coords);
    }
    else
    {
        if( TUTORIAL_PAGE == 1 ) overlay = TutContents219.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 2 ) overlay = TutPreset219.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 3 ) overlay = TutLens219.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 4 ) overlay = TutVCR1219.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 5 ) overlay = TutVCR2219.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 6 ) overlay = TutTV219.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 7 ) overlay = TutFormat219.Sample(SamplerPoint, coords);
        if( TUTORIAL_PAGE == 8 ) overlay = TutWelcome219.Sample(SamplerPoint, coords);
    }

    orig = BlendScreenf(orig, overlay);

    return orig;
}
