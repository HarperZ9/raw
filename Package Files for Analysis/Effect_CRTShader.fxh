#ifndef EFFECT_CRTSHADER_FXH
#define EFFECT_CRTSHADER_FXH

//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                CRT Display Simulation Addon for ENBSeries  v1.0                              //
//                by LonelyKitsuune / Extended by Zain Dana Harper                              //
//                                                                                              //
//  Physically-motivated CRT monitor simulation modelling the electron-beam                     //
//  display chain: phosphor mask → scanline structure → screen curvature →                      //
//  phosphor bloom → corner shadow.                                                             //
//                                                                                              //
//  Uses its own CRTVSOutput struct (no dependency on CinematicFX).                            //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//----------------------------------------------------------------------------------------------//
//                              CRT Structs                                                     //
//----------------------------------------------------------------------------------------------//

struct CRTVSOutput
{
    float4 pos      : SV_POSITION;
    float2 texcoord : TEXCOORD0;
};


//----------------------------------------------------------------------------------------------//
//                              CRT Vertex Shader                                               //
//----------------------------------------------------------------------------------------------//

CRTVSOutput VS_CRT(VertexShaderInput IN)
{
    CRTVSOutput OUT;
    OUT.pos      = float4(IN.pos.xyz, 1.0);
    OUT.texcoord = IN.txcoord.xy;
    return OUT;
}


//----------------------------------------------------------------------------------------------//
//                              CRT Pixel Shader                                                //
//----------------------------------------------------------------------------------------------//

float4 PS_CRT(CRTVSOutput IN) : SV_Target
{
    float3 Color = TextureColor.Sample(Linear_Sampler, IN.texcoord).rgb;

    [branch] if(!UICRT_Enable)
        return float4(Color, 1.0);

    float2 UV = IN.texcoord;


    // ---- SCREEN CURVATURE ---- //
    //  CRT screens are slightly convex.  Map flat UV to curved surface
    //  using a barrel distortion model: UV' = UV + UV·r²·k
    [branch] if(UICRT_Curvature > 0.001)
    {
        float2 C = UV * 2.0 - 1.0;
        float  R2 = dot(C, C);
        C *= 1.0 + R2 * UICRT_Curvature;
        UV = C * 0.5 + 0.5;
    }

    // ---- OVERSCAN ---- //
    //  CRT displays slightly overscan: the raster extends past the visible
    //  bezel, cutting off ~1-2% of the image at each edge.
    UV = (UV - 0.5) * (1.0 + UICRT_Overscan * 2.0) + 0.5;

    //Out-of-bounds check (curved + overscanned UV may exit [0,1])
    float2 EdgeDist = UV * (1.0 - UV);
    float  InBounds = step(0.0, min(EdgeDist.x, EdgeDist.y));

    [branch] if(InBounds < 0.5)
        return float4(0.0, 0.0, 0.0, 1.0);

    //Re-sample scene at distorted UV
    Color = TextureColor.SampleLevel(Linear_Sampler, UV, 0).rgb;


    // ---- BRIGHTNESS / CONTRAST / SATURATION ---- //
    //  CRT phosphors have non-linear response.  Simple artist controls
    //  simulate the characteristic vivid, slightly crushed look.
    Color *= UICRT_Brightness;
    Color = (Color - 0.5) * UICRT_Contrast + 0.5;
    Color = max(Color, 0.0);

    float Luma = dot(Color, K_LUM);
    Color = lerp(Luma, Color, UICRT_Saturation);
    Color = max(Color, 0.0);


    // ---- SCANLINES ---- //
    //  The electron beam paints horizontal lines with gaps between them.
    //  On bright pixels the beam blooms wider, filling the gap — so scanline
    //  darkness is inversely proportional to luminance (bright boost).
    [branch] if(UICRT_ScanIntensity > 0.01)
    {
        float ScreenY = UV.y * ScreenSize.y;
        float ScanPhase = frac(ScreenY / UICRT_ScanWidth);

        //Sinusoidal scanline profile: dark at gap center (phase 0.5)
        float ScanMask = 0.5 + 0.5 * cos(ScanPhase * 6.2831853);

        //Bright boost: luminous pixels widen the beam, reducing the dark gap
        float BrightMask = 1.0 - UICRT_ScanBrightBoost * Luma;
        ScanMask = lerp(1.0, ScanMask, UICRT_ScanIntensity * BrightMask);

        Color *= ScanMask;
    }


    // ---- PHOSPHOR MASK ---- //
    //  RGB phosphor triads or slots modulate each channel spatially.
    //  Three common mask types: aperture grille, slot mask, shadow mask.
    [branch] if(UICRT_MaskIntensity > 0.01)
    {
        float2 PixelPos = UV * ScreenSize.xy / UICRT_MaskScale;
        float3 MaskRGB;

        [branch] if(UICRT_MaskType == 0)
        {
            //Aperture grille (Trinitron): vertical RGB stripes
            float Phase = frac(PixelPos.x / 3.0) * 3.0;
            MaskRGB.r = (Phase < 1.0) ? 1.0 : (1.0 - UICRT_MaskIntensity);
            MaskRGB.g = (Phase >= 1.0 && Phase < 2.0) ? 1.0 : (1.0 - UICRT_MaskIntensity);
            MaskRGB.b = (Phase >= 2.0) ? 1.0 : (1.0 - UICRT_MaskIntensity);
        }
        else if(UICRT_MaskType == 1)
        {
            //Slot mask: staggered RGB blocks with horizontal offset every other row
            float RowOff = floor(fmod(PixelPos.y, 2.0)) * 1.5;
            float Phase = frac((PixelPos.x + RowOff) / 3.0) * 3.0;
            MaskRGB.r = (Phase < 1.0) ? 1.0 : (1.0 - UICRT_MaskIntensity * 0.8);
            MaskRGB.g = (Phase >= 1.0 && Phase < 2.0) ? 1.0 : (1.0 - UICRT_MaskIntensity * 0.8);
            MaskRGB.b = (Phase >= 2.0) ? 1.0 : (1.0 - UICRT_MaskIntensity * 0.8);
        }
        else
        {
            //Shadow mask: hexagonal dot pattern
            float2 Hex = PixelPos * float2(1.0, 0.866);
            float RowOff2 = floor(fmod(Hex.y, 2.0)) * 0.5;
            float Phase = frac((Hex.x + RowOff2) / 3.0) * 3.0;
            MaskRGB.r = (Phase < 1.0) ? 1.0 : (1.0 - UICRT_MaskIntensity * 0.6);
            MaskRGB.g = (Phase >= 1.0 && Phase < 2.0) ? 1.0 : (1.0 - UICRT_MaskIntensity * 0.6);
            MaskRGB.b = (Phase >= 2.0) ? 1.0 : (1.0 - UICRT_MaskIntensity * 0.6);
        }

        Color *= MaskRGB;
    }


    // ---- PHOSPHOR BLOOM ---- //
    //  CRT phosphor glow bleeds light into neighboring pixels.
    //  Simple 4-tap cross blur approximation.
    [branch] if(UICRT_Bloom > 0.01)
    {
        float BloomR = 1.5 * PixelSize.x;
        float BloomRV = BloomR * ScreenSize.z;

        float3 Bloom = TextureColor.SampleLevel(Linear_Sampler, UV + float2( BloomR, 0), 0).rgb
                      + TextureColor.SampleLevel(Linear_Sampler, UV + float2(-BloomR, 0), 0).rgb
                      + TextureColor.SampleLevel(Linear_Sampler, UV + float2(0,  BloomRV), 0).rgb
                      + TextureColor.SampleLevel(Linear_Sampler, UV + float2(0, -BloomRV), 0).rgb;
        Bloom *= 0.25;

        Color = lerp(Color, max(Color, Bloom), UICRT_Bloom);
    }


    // ---- CORNER SHADOW ---- //
    //  CRT tubes have rounded corners where the phosphor coating thins out
    //  and the electron beam weakens, creating dark rounded corners.
    [branch] if(UICRT_CornerRadius > 0.001)
    {
        float2 Abs = abs(UV * 2.0 - 1.0);
        float2 CornerDist = max(Abs - (1.0 - UICRT_CornerRadius), 0.0) / UICRT_CornerRadius;
        float  CornerMask = 1.0 - smoothstep(0.8, 1.0, length(CornerDist));
        Color *= CornerMask;
    }

    return float4(max(Color, 0.0), 1.0);
}

#endif // EFFECT_CRTSHADER_FXH
