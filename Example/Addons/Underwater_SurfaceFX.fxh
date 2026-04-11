//=============================================================================
//  Underwater_SurfaceFX.fxh — Underwater Surface Effects
//
//  2 new sub-techniques for enbunderwater.fx:
//    Snell's Window / Total Internal Reflection (full Fresnel equations)
//    Wet Lens (gravity-driven droplet flow + streaks)
//
//  All features disabled by default.
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef UNDERWATER_SURFACEFX_FXH
#define UNDERWATER_SURFACEFX_FXH


//=== UI PARAMETERS ===//

// --- Snell's Window / TIR ---

bool ui_SnellEnable
<
    string UIName = "UW SURF | Snell's Window Enable";
> = {false};

float ui_SnellDarkness
<
    string UIName = "UW SURF | TIR Darkness";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_SnellCritAngle
<
    string UIName = "UW SURF | Critical Angle Adjust";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 1.5; float UIStep = 0.01;
> = {1.0};

float ui_SnellFresnel
<
    string UIName = "UW SURF | Fresnel Edge Glow";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.5};

float ui_SnellRipple
<
    string UIName = "UW SURF | Ripple Distortion";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_SnellChroma
<
    string UIName = "UW SURF | Chromatic TIR Shift";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.1; float UIStep = 0.001;
> = {0.02};

// --- Wet Lens ---

bool ui_WetLensEnable
<
    string UIName = "UW SURF | Wet Lens Enable";
> = {false};

float ui_WetLensIntensity
<
    string UIName = "UW SURF | Wet Lens Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

float ui_WetDropDensity
<
    string UIName = "UW SURF | Droplet Density";
    string UIWidget = "Spinner";
    float UIMin = 0.2; float UIMax = 3.0; float UIStep = 0.1;
> = {1.0};

float ui_WetDropSize
<
    string UIName = "UW SURF | Droplet Size";
    string UIWidget = "Spinner";
    float UIMin = 0.3; float UIMax = 3.0; float UIStep = 0.1;
> = {1.0};

float ui_WetRefraction
<
    string UIName = "UW SURF | Droplet Refraction";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.5; float UIStep = 0.01;
> = {0.5};

float ui_WetStreaks
<
    string UIName = "UW SURF | Water Streaks";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_WetDrySpeed
<
    string UIName = "UW SURF | Dry Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 3.0; float UIStep = 0.1;
> = {1.0};


//=== PIXEL SHADERS ===//

// Snell's Window — full Fresnel equations with wavelength-dependent IOR
float4 PS_SnellWindow(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;
    float mask = RenderTargetR16F.SampleLevel(smpLinear, txcoord, 0).x;
    if (!ui_SnellEnable || mask < 0.01) return float4(color, 1.0);

    float2 centered = txcoord * 2.0 - 1.0;
    centered.x *= ScreenSize.z; // aspect correct

    // Animated water surface normals — FBM noise distortion
    float2 noiseUV = txcoord * 4.0;
    float t = Timer.x * 1677.7216;
    float ripple1 = sin(noiseUV.x * 8.0 + t * 0.4) * cos(noiseUV.y * 6.0 + t * 0.3);
    float ripple2 = sin(noiseUV.y * 10.0 - t * 0.5) * cos(noiseUV.x * 7.0 + t * 0.2);
    float2 surfaceNormal = float2(ripple1, ripple2) * ui_SnellRipple * 0.1;
    centered += surfaceNormal;

    float viewAngle = length(centered) * 0.8;

    // Per-wavelength critical angle (Cauchy: n_water varies with wavelength)
    // R=1.3240, G=1.3330, B=1.3450
    float3 n_water = float3(1.3240, 1.3330, 1.3450);
    float3 critAngle = asin(1.0 / n_water) * ui_SnellCritAngle;

    // Full Fresnel reflectance (average of s and p polarization)
    float3 reflectance;
    [unroll] for (int ch = 0; ch < 3; ch++)
    {
        float sinI = sin(min(viewAngle, critAngle[ch] - 0.001));
        float cosI = cos(min(viewAngle, critAngle[ch] - 0.001));
        float sinT = sinI / n_water[ch]; // Snell's law

        if (sinT >= 1.0)
        {
            reflectance[ch] = 1.0; // Total internal reflection
        }
        else
        {
            float cosT = sqrt(1.0 - sinT * sinT);
            float Rs = (1.0 * cosI - n_water[ch] * cosT) / (1.0 * cosI + n_water[ch] * cosT);
            float Rp = (1.0 * cosT - n_water[ch] * cosI) / (1.0 * cosT + n_water[ch] * cosI);
            reflectance[ch] = 0.5 * (Rs * Rs + Rp * Rp);
        }
    }

    // Fresnel edge glow (bright caustic-like ring at critical angle boundary)
    float3 edgeDist = abs(float3(viewAngle, viewAngle, viewAngle) - critAngle);
    float3 fresnelGlow = exp(-edgeDist * edgeDist * 200.0) * ui_SnellFresnel;

    // Composite: outside Snell's window = darker (TIR), inside = brighter
    float3 tinted = color * (1.0 - reflectance * ui_SnellDarkness) + fresnelGlow * color;

    return float4(lerp(color, tinted, mask), 1.0);
}

// Wet Lens — gravity-driven droplet flow with hemispherical refraction
float4 PS_WetLens(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;
    float mask = RenderTargetR16F.SampleLevel(smpLinear, txcoord, 0).x;
    if (!ui_WetLensEnable || mask < 0.01) return float4(color, 1.0);

    float3 result = color;
    float t = Timer.x * 1677.7216;

    // --- Droplet field (cell-noise with gravity drift) ---
    float2 gridUV = txcoord * 15.0 * ui_WetDropDensity;
    gridUV.y += t * ui_WetDrySpeed * 0.5; // Gravity drift

    int2 cell = (int2)floor(gridUV);
    float2 f = frac(gridUV);

    [loop] for (int cy = -1; cy <= 1; cy++)
    [loop] for (int cx = -1; cx <= 1; cx++)
    {
        float2 cid = float2(cell + int2(cx, cy));
        float h = frac(sin(dot(cid, float2(127.1, 311.7))) * 43758.5453);
        if (h > 0.35 * ui_WetDropDensity) continue;

        // Droplet position with gravity-driven downward acceleration
        float2 dropPos = float2(
            frac(sin(dot(cid, float2(269.5, 183.3))) * 43758.5453),
            frac(sin(dot(cid, float2(419.7, 371.9))) * 43758.5453)
        );
        dropPos.y += frac(t * ui_WetDrySpeed * 0.2 * (0.5 + h));

        float radius = (0.1 + h * 0.2) * ui_WetDropSize * 0.15;
        float2 d = f - float2(cx, cy) - dropPos;
        float dist = length(d);
        float normDist = dist / max(radius, 0.001);

        if (normDist < 1.0)
        {
            // Hemispherical normal for refraction (Snell's law through water hemisphere)
            float nz = sqrt(max(1.0 - normDist * normDist, 0.0));

            // Refraction UV offset
            float2 refractOff = (d / max(dist, 0.001)) * (1.0 - nz)
                              * ui_WetRefraction * radius;

            result = TextureColor.SampleLevel(smpLinear,
                txcoord + refractOff * PixelSize * 15.0, 0).rgb;

            // Edge darkening (meniscus shadow)
            result *= (1.0 - smoothstep(0.5, 1.0, normDist) * 0.3);

            // Specular highlight
            result += pow(max(nz, 0.0), 8.0) * 0.4;
        }
    }

    // --- Water streaks (vertical trails from draining droplets) ---
    float streakNoise = frac(sin(floor(txcoord.x * 60.0) * 127.1) * 43758.5453);
    float streak = pow(saturate(streakNoise), 4.0)
                 * (sin(txcoord.y * 40.0 - t * ui_WetDrySpeed * 2.0) * 0.5 + 0.5);
    streak *= ui_WetStreaks * smoothstep(0.0, 0.3, 1.0 - txcoord.y);

    if (streak > 0.3)
    {
        float2 streakRefract = float2(0, streak * 0.003);
        result = lerp(result,
            TextureColor.SampleLevel(smpLinear, txcoord + streakRefract, 0).rgb,
            streak * 0.4);
    }

    return float4(lerp(color, result, ui_WetLensIntensity * mask), 1.0);
}


#endif // UNDERWATER_SURFACEFX_FXH
