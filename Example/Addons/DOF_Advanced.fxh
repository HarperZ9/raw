//=============================================================================
//  DOF_Advanced.fxh — Advanced DOF Modes
//
//  2 new techniques for enbdepthoffield.fx:
//    DOF8: Tilt-Shift (Scheimpflug plane, rotatable axis, CoC-weighted blur)
//    DOF9: Cat's Eye Vignetting (entrance/exit pupil optical model)
//
//  All features disabled by default.
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef DOF_ADVANCED_FXH
#define DOF_ADVANCED_FXH


//=== UI PARAMETERS ===//

// --- Tilt-Shift ---

bool ui_TiltEnable
<
    string UIName = "DOF ADV | Tilt-Shift Enable";
> = {false};

float ui_TiltCenter
<
    string UIName = "DOF ADV | Tilt-Shift Center Y";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 0.9; float UIStep = 0.01;
> = {0.5};

float ui_TiltRange
<
    string UIName = "DOF ADV | Tilt-Shift Focus Range";
    string UIWidget = "Spinner";
    float UIMin = 0.02; float UIMax = 0.5; float UIStep = 0.01;
> = {0.15};

float ui_TiltAngle
<
    string UIName = "DOF ADV | Tilt-Shift Angle (deg)";
    string UIWidget = "Spinner";
    float UIMin = -90.0; float UIMax = 90.0; float UIStep = 1.0;
> = {0.0};

float ui_TiltBlur
<
    string UIName = "DOF ADV | Tilt-Shift Blur Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 8.0; float UIStep = 0.1;
> = {2.0};

float ui_TiltFalloffPow
<
    string UIName = "DOF ADV | Tilt-Shift Falloff Power";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 4.0; float UIStep = 0.1;
> = {1.5};

int ui_TiltQuality
<
    string UIName = "DOF ADV | Tilt-Shift Quality (0=9tap 1=17tap)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 1;
> = {0};

float ui_TiltSatBoost
<
    string UIName = "DOF ADV | Miniature Saturation Boost";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.0};

// --- Cat's Eye Vignetting ---

bool ui_CatEyeVigEnable
<
    string UIName = "DOF ADV | Cat's Eye Enable";
> = {false};

float ui_CatEyeRadius
<
    string UIName = "DOF ADV | Cat's Eye Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.3; float UIMax = 1.5; float UIStep = 0.01;
> = {0.8};

float ui_CatEyePower
<
    string UIName = "DOF ADV | Cat's Eye Power";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 5.0; float UIStep = 0.1;
> = {2.0};

float ui_CatEyeSqueeze
<
    string UIName = "DOF ADV | Cat's Eye Squeeze";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_CatEyeDarken
<
    string UIName = "DOF ADV | Cat's Eye Darken";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01;
> = {0.15};


//=== PIXEL SHADERS ===//

// DOF8: Tilt-Shift — Scheimpflug plane with rotatable axis
float4 PS_TiltShift(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpPoint, txcoord, 0).rgb;
    if (!ui_TiltEnable) return float4(color, 1.0);

    // Rotatable focus axis (Scheimpflug plane projection to screen)
    float angle = ui_TiltAngle * 0.01745329;
    float2 axisDir = float2(cos(angle), sin(angle));
    float2 perpDir = float2(-axisDir.y, axisDir.x);

    // Distance from focus plane (perpendicular to axis through center)
    float2 center = float2(0.5, ui_TiltCenter);
    float dist = abs(dot(txcoord - center, perpDir));

    // CoC from distance to focus plane (power-curve falloff)
    float coc = pow(saturate(dist / ui_TiltRange - 1.0), ui_TiltFalloffPow);
    coc *= ui_TiltBlur;

    if (coc < 0.01) return float4(color, 1.0);

    // CoC-weighted Gaussian blur
    int halfTaps = (ui_TiltQuality == 0) ? 4 : 8;
    float3 accum = 0;
    float totalW = 0;
    float2 blurPixel = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);

    [loop] for (int i = -8; i <= 8; i++)
    {
        if (abs(i) > halfTaps) continue;
        float t = (float)i / (float)halfTaps;
        float w = exp(-t * t * 2.0);

        float2 offset = float2(i, 0) * blurPixel * coc;
        float3 s = TextureColor.SampleLevel(smpLinear, txcoord + offset, 0).rgb;
        accum += s * w;
        totalW += w;
    }
    float3 blurred = accum / totalW;

    // Optional miniature saturation boost (diorama effect)
    if (ui_TiltSatBoost > 0.001)
    {
        float luma = dot(blurred, float3(0.2126, 0.7152, 0.0722));
        blurred = lerp(luma, blurred, 1.0 + ui_TiltSatBoost * coc);
    }

    return float4(lerp(color, blurred, saturate(coc)), 1.0);
}

// DOF9: Cat's Eye — entrance/exit pupil optical vignetting + directional squeeze
float4 PS_CatEye(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpPoint, txcoord, 0).rgb;
    if (!ui_CatEyeVigEnable) return float4(color, 1.0);

    // Distance from optical center
    float2 fromCenter = txcoord - 0.5;
    fromCenter.x *= ScreenSize.z; // aspect correct
    float edgeDist = length(fromCenter);

    // Optical vignetting: entrance pupil / exit pupil intersection
    float vigAmount = pow(edgeDist / max(ui_CatEyeRadius, 0.01), ui_CatEyePower);
    vigAmount = saturate(vigAmount);

    if (vigAmount < 0.001) return float4(color, 1.0);

    // Luminance vignetting (darkening toward edges)
    float3 vignetted = color * (1.0 - vigAmount * ui_CatEyeDarken);

    // Directional squeeze (bokeh elongation perpendicular to radial direction)
    if (ui_CatEyeSqueeze > 0.01 && vigAmount > 0.1)
    {
        float2 radialDir = normalize(fromCenter + 0.001);
        float2 perpDir = float2(-radialDir.y, radialDir.x);
        float blurAmount = vigAmount * ui_CatEyeSqueeze * 2.0;

        float2 blurPixel = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
        float3 s1 = TextureColor.SampleLevel(smpLinear,
            txcoord + perpDir * blurPixel * blurAmount, 0).rgb;
        float3 s2 = TextureColor.SampleLevel(smpLinear,
            txcoord - perpDir * blurPixel * blurAmount, 0).rgb;

        vignetted = (vignetted + s1 + s2) / 3.0;
        vignetted *= (1.0 - vigAmount * ui_CatEyeDarken);
    }

    return float4(vignetted, 1.0);
}


#endif // DOF_ADVANCED_FXH
