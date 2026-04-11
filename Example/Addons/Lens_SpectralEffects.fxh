//=============================================================================
//  Lens_SpectralEffects.fxh — Lens Spectral Effects Suite
//
//  1 new technique (Draw5) for enblens.fx:
//    Diffraction Starburst (N-blade aperture with spectral decomposition)
//    Veil Glare (optical scatter 1/r^2 PSF with Rayleigh wavelength dependence)
//
//  All features disabled by default.
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef LENS_SPECTRAL_EFFECTS_FXH
#define LENS_SPECTRAL_EFFECTS_FXH


//=== UI PARAMETERS ===//

// --- Diffraction Starburst ---

bool ui_LensStarburstEnable
<
    string UIName = "LENS FX | Starburst Enable";
> = {false};

float ui_LensStarburstIntensity
<
    string UIName = "LENS FX | Starburst Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.2};

int ui_LensStarburstBlades
<
    string UIName = "LENS FX | Starburst Blade Count";
    string UIWidget = "Spinner";
    int UIMin = 4; int UIMax = 8;
> = {6};

float ui_LensStarburstRotation
<
    string UIName = "LENS FX | Starburst Rotation (deg)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 360.0; float UIStep = 1.0;
> = {0.0};

float ui_LensStarburstSharpness
<
    string UIName = "LENS FX | Starburst Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 100.0; float UIMax = 1200.0; float UIStep = 10.0;
> = {600.0};

float ui_LensStarburstSpectral
<
    string UIName = "LENS FX | Starburst Spectral Color";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_LensStarburstLength
<
    string UIName = "LENS FX | Starburst Length";
    string UIWidget = "Spinner";
    float UIMin = 0.05; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_LensStarburstVariation
<
    string UIName = "LENS FX | Starburst Blade Variation";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.0};

// --- Veil Glare ---

bool ui_VeilEnable
<
    string UIName = "LENS FX | Veil Glare Enable";
> = {false};

float ui_VeilIntensity
<
    string UIName = "LENS FX | Veil Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.3};

float ui_VeilFalloff
<
    string UIName = "LENS FX | Veil Falloff";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 10.0; float UIStep = 0.1;
> = {3.0};

float ui_VeilWarmth
<
    string UIName = "LENS FX | Veil Warmth";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.1};


//=== STARBURST HELPERS ===//

// Smooth Lorentzian-squared approximation to sinc^2(x)
// Avoids secondary lobe ringing that causes aliasing at finite resolution.
// k controls spike sharpness: 800 = sharp modern, 200 = soft vintage.
// Ref: Komrska 1982 (Fraunhofer diffraction of polygonal apertures)
float SincSquaredApprox(float x, float k)
{
    float kxx = k * x * x;
    return 1.0 / ((1.0 + kxx) * (1.0 + kxx));
}

// Per-blade reflectivity variation (manufacturing imperfections)
// Produces asymmetric starbursts where some spikes are brighter than others
static const float BLADE_VARIATION[9] =
{
    1.00, 1.08, 0.95, 1.12, 0.88, 1.05, 0.92, 1.10, 0.97
};

// Spectral color gradient along a diffraction spike
// Blue falls off first (shortest wavelength diffracts least),
// red persists longest (longest wavelength diffracts most)
float3 DiffractionSpectralColor(float t)
{
    float red   = exp(-t * 3.0);
    float green = exp(-t * 5.0);
    float blue  = exp(-t * 8.0);
    return float3(red, green, blue);
}


//=== PIXEL SHADER ===//

// Combined starburst + veil glare pass
float4 PS_LensSpectral(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(Sampler0, txcoord, 0).rgb;

    float intensity = lerp(lerp(ui_LensIntensityNight, ui_LensIntensityDay, ENightDayFactor),
                           ui_LensIntensityInterior, EInteriorFactor);
    if (intensity < 0.001) return float4(color, 1.0);

    float3 result = color;

    // --- Diffraction Starburst (sinc² approximation with spectral color) ---
    if (ui_LensStarburstEnable)
    {
        // Read bright pixel data from low-res mip (existing lens pyramid)
        float3 brightMip = RenderTarget128.SampleLevel(Sampler1, txcoord, 0).rgb;
        float brightness = dot(brightMip, N_LUM);

        if (brightness > 0.01)
        {
            int bladeCount = ui_LensStarburstBlades;
            // Even blades = N spikes, odd blades = 2N spikes (Komrska)
            int spikeCount = (bladeCount % 2 == 0) ? bladeCount : bladeCount * 2;

            float2 fromCenter = txcoord - 0.5;
            float angle = atan2(fromCenter.y, fromCenter.x + 0.0001);
            float dist = length(fromCenter);

            // Temporal rotation (slow drift for realism)
            float rotation = ui_LensStarburstRotation * 0.01745329 + Timer.x * 0.01;

            // Accumulate spike intensity across all spikes
            float starburstVal = 0.0;
            bool isEven = (bladeCount % 2 == 0);

            [loop]
            for (int i = 0; i < spikeCount; i++)
            {
                // Angle of this spike
                float spikeAngle = (float(i) / float(spikeCount)) * 6.28318530718;

                // Angular distance from current pixel to this spike
                float angleDiff = angle - spikeAngle - rotation;
                // Wrap to [-PI, PI]
                angleDiff = fmod(angleDiff + 3.14159265359 + 62831.853, 6.28318530718) - 3.14159265359;

                // Spike profile: Lorentzian² approximation of sinc²
                float spikeProfile = SincSquaredApprox(angleDiff, ui_LensStarburstSharpness);

                // Per-blade brightness variation (asymmetric starburst)
                float bladeVar = 1.0;
                if (ui_LensStarburstVariation > 0.001)
                {
                    int bladeIndex = isEven ? i : (i / 2);
                    bladeVar = lerp(1.0, BLADE_VARIATION[bladeIndex % 9], ui_LensStarburstVariation);
                }

                starburstVal += spikeProfile * bladeVar;
            }

            // Radial falloff
            float radialFalloff = 1.0 / (1.0 + dist * dist * 50.0 / max(ui_LensStarburstLength * ui_LensStarburstLength, 0.001));

            starburstVal *= radialFalloff * 0.1;

            // Spectral color gradient along spikes (wavelength-dependent diffraction)
            float t = saturate(dist * 3.0 / max(ui_LensStarburstLength, 0.01));
            float3 spectralColor = lerp(1.0, DiffractionSpectralColor(t), ui_LensStarburstSpectral);

            result += spectralColor * starburstVal * brightness * ui_LensStarburstIntensity * intensity;
        }
    }

    // --- Veil Glare ---
    if (ui_VeilEnable)
    {
        // Optical scatter PSF: 1/r^2 falloff from bright areas
        float3 veilMip = RenderTarget128.SampleLevel(Sampler1, txcoord, 0).rgb;

        float2 fromCenter = txcoord - 0.5;
        float dist = length(fromCenter);

        // 1/(1+r^2*falloff) scatter pattern
        float scatter = 1.0 / (1.0 + dist * dist * ui_VeilFalloff * 100.0);

        // Wavelength-dependent scatter (blue scatters more via Rayleigh in glass)
        float3 wavelengthScatter = float3(0.85, 0.95, 1.0);

        // Warmth tint toward edges (chromatic aberration of scattered light)
        float3 warmEdge = lerp(float3(1, 1, 1), float3(1.1, 1.0, 0.9),
                               dist * ui_VeilWarmth);

        result += veilMip * scatter * wavelengthScatter * warmEdge
                * ui_VeilIntensity * intensity;
    }

    return float4(max(result, 0.0), 1.0);
}


#endif // LENS_SPECTRAL_EFFECTS_FXH
