    float4 AvgLuma(float3 inColor) {
        return float4(dot(inColor, float3(0.2125f, 0.7154f, 0.0721f)),   /// Perform a weighted average
        max(inColor.r, max(inColor.g, inColor.b)),                       /// Take the maximum value of the incoming value
        max(max(inColor.x, inColor.y), inColor.z),                       /// Compute the luminance component as per the HSL colour space
        sqrt((inColor.x*inColor.x*0.2125f)+(inColor.y*inColor.y*0.7154f)+(inColor.z*inColor.z*0.0721f)));
    }

    float3 PP2(float3 inColor, float3 inAdaptation) {
        float EAdaptationMinV2   = 0.05;
        float EAdaptationMaxV2   = 0.05;
        float EBrightnessV2   = lerp(lerp(BrightnessNight, BrightnessDay, ENightDayFactor), BrightnessInterior, EInteriorFactor);
        float EIntensityContrastV2   = lerp(lerp(IntensityContrastNight, IntensityContrastDay, ENightDayFactor), IntensityContrastInterior, EInteriorFactor);
        float EColorSaturationV2   = lerp(lerp(SaturationNight, SaturationDay, ENightDayFactor), SaturationInterior, EInteriorFactor);
        float EToneMappingOversaturationV2   = lerp(lerp(ToneMappingOversaturationNight, ToneMappingOversaturationDay, ENightDayFactor), ToneMappingOversaturationInterior, EInteriorFactor);
        float EToneMappingCurveV2   = lerp(lerp(ToneMappingCurveNight, ToneMappingCurveDay, ENightDayFactor), ToneMappingCurveInterior, EInteriorFactor);

        float grayadaptation = AvgLuma(inAdaptation.xyz).y;
        grayadaptation     = max(grayadaptation, 0.0);
        grayadaptation     = min(grayadaptation, 50.0);
        inColor.xyz        = inColor.xyz / (grayadaptation * EAdaptationMaxV2 + EAdaptationMinV2);

        inColor.xyz  *= EBrightnessV2;
        inColor.xyz  += 0.000001;
        float3 xncol  = normalize(inColor.xyz);
        float3 scl    = inColor.xyz / xncol.xyz;
        scl           = pow(scl, EIntensityContrastV2);
        xncol.xyz     = pow(xncol.xyz, EColorSaturationV2);
        inColor.xyz   = scl * xncol.xyz;
        float lumamax = EToneMappingOversaturationV2;
        inColor.xyz   = (inColor.xyz * (1.0 + inColor.xyz / lumamax)) / (inColor.xyz + EToneMappingCurveV2);


      return inColor;
    }
