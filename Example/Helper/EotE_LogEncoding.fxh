#ifndef EOTE_LOG_ENCODING_FXH
#define EOTE_LOG_ENCODING_FXH
//----------------------------------------------------------------------------------------------//
//  EotE_LogEncoding.fxh — Log-domain encoding utilities
//
//  ARRI LogC3 (EI 800): Linear ↔ Log conversion for log-domain grading.
//  Working in log space ensures that grading operations (contrast, curves,
//  color balance) affect perceptually uniform stops rather than linear values.
//
//  Reference: ARRI LogC Curve, "ALEXA Log C Curve — Usage in VFX", 2017
//----------------------------------------------------------------------------------------------//


// ARRI LogC3 EI800 constants
static const float LOGC3_CUT    = 0.010591;
static const float LOGC3_A      = 5.555556;
static const float LOGC3_B      = 0.052272;
static const float LOGC3_C      = 0.247190;
static const float LOGC3_D      = 0.385537;
static const float LOGC3_E      = 5.367655;
static const float LOGC3_F      = 0.092809;


// Linear scene-referred → ARRI LogC3 [0,1]
float3 LinearToLogC3(float3 x)
{
    float3 logPart = LOGC3_C * log10(max(LOGC3_A * x + LOGC3_B, 1e-10)) + LOGC3_D;
    float3 linPart = LOGC3_E * x + LOGC3_F;
    return (x > LOGC3_CUT) ? logPart : linPart;
}

// ARRI LogC3 [0,1] → Linear scene-referred
float3 LogC3ToLinear(float3 t)
{
    float3 logPart = (pow(10.0, (t - LOGC3_D) / LOGC3_C) - LOGC3_B) / LOGC3_A;
    float3 linPart = (t - LOGC3_F) / LOGC3_E;
    float  cutLog  = LOGC3_E * LOGC3_CUT + LOGC3_F;
    return (t > cutLog) ? logPart : linPart;
}


#endif // EOTE_LOG_ENCODING_FXH
