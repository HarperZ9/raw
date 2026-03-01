//==================================//
// Based on BakingLab by            //
// Matt Pettineo's and Stephen Hill //
//==================================//

/*
UI_SEPARATOR_CUSTOM("\xB6 Adaptation \xB6")
UI_FLOAT_FINE_DNI(minAdapt,        "Adaptation: Min",          0.0, 10.0, 0.025, 0.001)
UI_FLOAT_FINE_DNI(maxAdapt,        "Adaptation: Max",          0.0, 10.0, 0.011, 0.001) 

#define minAdapt 0.09
#define maxAdapt 0.1 */

// Calculate perceived luminance color by using the ITU-R BT.709-5 standard
float pcvLum(in float3 color)
{
    return sqrt((color.x*color.x*0.212395f)+(color.y*color.y*0.701049f)+(color.z*color.z*0.086556f));

/// Luma Coefficient Standards
//  Ultra-HD TV > ITU-R Rec. BT.2020  - sqrt((color.x*color.x*0.2627f)+(color.y*color.y*0.6780f)+(color.z*color.z*0.0593f));
//  HD TV       > ITU-R Rec. BT.709-5 - sqrt((color.x*color.x*0.212395f)+(color.y*color.y*0.701049f)+(color.z*color.z*0.086556f));
//  HD TV       > ITU-R Rec. BT.709   - sqrt((color.x*color.x*0.2126f)+(color.y*color.y*0.7152f)+(color.z*color.z*0.0722f));
//  CRT TV      > ITU-R Rec. BT.601   - sqrt((color.x*color.x*0.299f)+(color.y*color.y*0.587f)+(color.z*color.z*0.114f));
}

// Determines the color based on exposure settings, and applies the threshold to the exposure value.
float3 CalcExposedColor(float3 color, float avgLuminance)
{
    avgLuminance = max(avgLuminance, 0.001f);
    float linearExposure = (KeyValue / avgLuminance);
    float exposure = log2(max(linearExposure, 0.0001f));
    exposure -= ThresholdEXP;
    return exp2(exposure) * color;
}