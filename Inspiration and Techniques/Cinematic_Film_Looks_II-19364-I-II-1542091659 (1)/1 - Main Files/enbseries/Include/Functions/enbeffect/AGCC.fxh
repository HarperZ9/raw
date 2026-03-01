    float4 AGCC(float4 inColor, float2 inCoords) {
        #define LUM_709 float3(0.2125, 0.7154, 0.0721)
        bool   scalebloom  = (0.5<=Params01[0].x);
        float2 scaleduv    = clamp(0.0, Params01[6].zy, Params01[6].xy * inCoords.xy);
        float4 bloom       = TextureBloom.Sample(Sampler1, (scalebloom)? inCoords.xy: scaleduv); //linear sampler
        float2 middlegray  = TextureAdaptation.Sample(Sampler1, inCoords.xy).xy; //.x == current, .y == previous
        middlegray.y = 1.0; //bypass for enbadaptation format
        float DELTA		   = max(0,0.00000001);

        bool   UseFilmic   = (0.5<Params01[2].z);

        float  saturation  = Params01[3].x;   // 0 == gray scale
        float  contrast    = Params01[3].z;   // 0 == no contrast
        float  brightness  = Params01[3].w;   // intensity
        float3 tint        = Params01[4].rgb; // tint color
        float  tint_weight = Params01[4].w;   // 0 == no tint
        float3 fade        = Params01[5].xyz; // fade current scene to specified color, mostly used in special effects
        float  fade_weight = Params01[5].w;   // 0 == no fade

        inColor.a   = dot(inColor.rgb, LUM_709);      /// Get luminance
        inColor.rgb = lerp(inColor.a, inColor.rgb, saturation);              /// Saturation
        inColor.rgb = lerp(inColor.rgb, inColor.a * tint.rgb, tint_weight);       /// Tint

        inColor.rgb = lerp(middlegray.x, brightness * inColor.rgb, contrast);
        inColor.rgb = pow(saturate(inColor.rgb), Params01[6].w); //this line is unused??
        inColor.rgb = lerp(inColor.rgb, fade, fade_weight);                   /// Fade current scene to specified color

        inColor.a = 1.0;
        inColor.rgb = saturate(inColor.rgb);

        return inColor;
    }
