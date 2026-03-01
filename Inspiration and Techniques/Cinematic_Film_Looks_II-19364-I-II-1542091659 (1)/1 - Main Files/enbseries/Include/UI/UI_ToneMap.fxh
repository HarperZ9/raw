// LENS DIRT ///////////////////////////////////////
    //UI_BOOL(ENABLE_DIRT, "Lens Dirt", true)
    UI_DIVIDER(1)
    
// DAY PARAMETERS //////////////////////////////////
    UI_MSG(1, "DAY")
    UI_FLOAT(BrightnessDay, "   Brightness Day", 0.0, 10.0, 1.0)
    UI_FLOAT(IntensityContrastDay, "   Contrast Day", 0.0, 10.0, 1.0)
    UI_FLOAT(SaturationDay, "   Saturation Day", 0.0, 10.0, 1.0)
    UI_FLOAT(ToneMappingOversaturationDay, "   Oversaturation Day", 0.0, 1000.0, 180.0)
    UI_FLOAT(ToneMappingCurveDay, "   Tonemapping Curve Day", 0.0, 50.0, 8.0)
    UI_DIVIDER(2)

// NIGHT PARAMETERS ////////////////////////////////
    UI_MSG(2, "NIGHT")
    UI_FLOAT(BrightnessNight, "   Brightness Night", 0.0, 10.0, 1.0)
    UI_FLOAT(IntensityContrastNight, "   Contrast Night", 0.0, 10.0, 1.0)
    UI_FLOAT(SaturationNight, "   Saturation Night", 0.0, 10.0, 1.0)
    UI_FLOAT(ToneMappingOversaturationNight, "   Oversaturation Night", 0.0, 1000.0, 180.0)
    UI_FLOAT(ToneMappingCurveNight, "   Tonemapping Curve Night", 0.0, 50.0, 8.0)
    UI_DIVIDER(3)

// INTERIOR PARAMETERS /////////////////////////////
    UI_MSG(3, "INTERIOR")
    UI_FLOAT(BrightnessInterior, "   Brightness Interior", 0.0, 10.0, 1.0)
    UI_FLOAT(IntensityContrastInterior, "   Contrast Interior", 0.0, 10.0, 1.0)
    UI_FLOAT(SaturationInterior, "   Saturation Interior", 0.0, 10.0, 1.0)
    UI_FLOAT(ToneMappingOversaturationInterior, "   Oversaturation Interior", 0.0, 1000.0, 180.0)
    UI_FLOAT(ToneMappingCurveInterior, "   Tonemapping Curve Interior", 0.0, 50.0, 8.0)
