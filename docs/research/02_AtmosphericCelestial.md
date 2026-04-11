# Atmospheric & Celestial Light Source Rendering

**Target:** `enbsunsprite.fx` + `Addons/Sunsprite_AtmosphericOptics.fxh` + `enbeffectprepass.fx`
**Pipeline:** DX11 SM5.0, FXC compiler, pixel shaders only, ENBSeries render target pool
**Constraint:** 128 technique limit per .fx file, no compute shaders, no UAV, no geometry/tessellation stages
**Author:** Zain Dana Harper
**Date:** 2026-03-07
**Sources:** SkyrimBridge Vol. 7 Agent 2 Research, ENB Compatibility Analysis, "Physics That Games Forgot"

---

## Table of Contents

1. [Solar Disc Physics & Wavelength-Dependent Limb Darkening](#1-solar-disc-physics--wavelength-dependent-limb-darkening)
2. [Atmospheric Extinction Model](#2-atmospheric-extinction-model)
3. [Multi-Component Corona & Aureole](#3-multi-component-corona--aureole)
4. [Moon Rendering & Opposition Surge](#4-moon-rendering--opposition-surge)
5. [God Rays / Volumetric Light Shafts](#5-god-rays--volumetric-light-shafts)
6. [Hillaire Atmosphere LUT Pipeline](#6-hillaire-atmosphere-lut-pipeline)
7. [Physics That Games Forgot -- New Techniques](#7-physics-that-games-forgot----new-techniques)
8. [Existing Codebase vs. What Needs Adding](#8-existing-codebase-vs-what-needs-adding)
9. [ENB Compatibility Verdicts](#9-enb-compatibility-verdicts)
10. [Performance Budget & Pass Allocation](#10-performance-budget--pass-allocation)
11. [Advanced Diffraction Spikes](#11-advanced-diffraction-spikes)
12. [Multi-Source Light Flare](#12-multi-source-light-flare)
13. [Academic References](#13-academic-references)

---

## 1. Solar Disc Physics & Wavelength-Dependent Limb Darkening

### 1.1 Physical Background

The Sun subtends an angular diameter of approximately 0.53 degrees from Earth's surface (perihelion 0.545 deg, aphelion 0.524 deg). For real-time rendering we treat this as constant at 0.53 deg, giving a half-angle of 0.265 deg.

Screen-space fraction depends on camera FOV:

```
// Angular radius of the sun disc in screen-space fraction
// sunHalfAngle = 0.265 degrees = 0.004625 radians
// tan(0.265 deg) = 0.004625 (small angle: tan(x) ~ x)
// screenFraction = tan(sunHalfAngle) / tan(FOV/2)
//
// At 60 deg FOV: screenFraction = 0.004625 / 0.5774 = 0.00801
// At 90 deg FOV: screenFraction = 0.004625 / 1.0    = 0.004625
// At 40 deg FOV: screenFraction = 0.004625 / 0.3640 = 0.01271
//
// The sun appears LARGER at narrow FOV (telephoto) and SMALLER at wide FOV.
```

At 60 deg FOV the sun disc covers approximately 0.005% of screen area. This means all per-pixel disc computations are essentially free -- the early-out test rejects 99.995% of pixels.

### 1.2 Limb Darkening -- The Physics

Solar limb darkening occurs because the photosphere is a gaseous layer with a temperature gradient. At disc center we see deeper, hotter layers; at the limb our line of sight skims cooler, shallower layers.

**Standard Minnaert (linear) model:**

```
I(r) = I_center * (1 - u * (1 - cos(theta)))

where:
  r     = fractional distance from disc center (0=center, 1=limb)
  theta = angle between surface normal and line of sight
  cos(theta) = sqrt(1 - r^2)  [geometric relationship for a sphere]
  u     = limb darkening coefficient (broadband visible: u ~ 0.6)
```

### 1.3 Wavelength-Dependent Coefficients (Neckel & Labs 1994)

The critical improvement: coefficient `u` varies with wavelength because H-minus opacity (which dominates in the solar atmosphere) is wavelength-dependent. Blue photons originate from higher, cooler layers than red.

| Channel | Central Wavelength | Minnaert u | Hestroffer alpha |
|---------|-------------------|------------|-----------------|
| Red     | 620 nm            | 0.50       | 0.429           |
| Green   | 540 nm            | 0.60       | 0.530           |
| Blue    | 460 nm            | 0.75       | 0.621           |

The solar limb therefore appears redder than the center -- physically correct and observationally verified.

### 1.4 Hestroffer Power-Law Model (Research-Validated Upgrade)

The Hestroffer power-law model matches Neckel & Labs measurements to within 1% at all limb positions. The linear Minnaert overestimates limb brightness by 10-12% near the edge. The correction is a single-line change with identical cost (3 `pow()` instructions):

```hlsl
// LINEAR MINNAERT (original):
float3 limbDarkening = 1.0 - LIMB_U * SUN_LimbStrength * (1.0 - cosTheta);

// HESTROFFER POWER-LAW (corrected):
float3 limbDarkening = pow(max(cosTheta, 1e-4), float3(0.429, 0.530, 0.621));
```

### 1.5 Quadratic Limb Darkening (Kopal 1950, Claret 2000)

For even higher fidelity when the disc is large (narrow FOV or size multiplier > 2.0):

```hlsl
// Quadratic limb darkening (Kopal 1950):
// I(r) = I_center * (1 - a*(1 - mu) - b*(1 - mu)^2)
// where mu = cos(theta) = sqrt(1 - r^2)
//
// Per-channel coefficients (Claret 2000, Claret & Bloemen 2011):
//   a_r = 0.35, b_r = 0.20  (red,   620nm)
//   a_g = 0.45, b_g = 0.22  (green, 540nm)
//   a_b = 0.55, b_b = 0.28  (blue,  460nm)

float3 QuadraticLimbDarkening(float cosTheta) {
    static const float3 a_ld = float3(0.35, 0.45, 0.55);
    static const float3 b_ld = float3(0.20, 0.22, 0.28);
    float mu = cosTheta;
    float oneMinusMu = 1.0 - mu;
    return 1.0 - a_ld * oneMinusMu - b_ld * oneMinusMu * oneMinusMu;
}
```

Cost: 3 extra MAD instructions over Hestroffer. Worth enabling only when disc occupies enough pixels for the edge gradient to be visible.

### 1.6 Sun Screen-Space Projection

```hlsl
float2 ProjectSunToScreen(float3 sunDir) {
    // Place the sun at a very large distance along its direction
    float4 sunClip = mul(float4(-sunDir * 50000.0, 1.0), ViewProjectionMatrix);

    // Behind camera check
    if (sunClip.w <= 0.0)
        return float2(-10.0, -10.0); // Off-screen sentinel

    // Perspective divide + NDC to UV
    float2 sunUV = (sunClip.xy / sunClip.w) * float2(0.5, -0.5) + 0.5;
    return sunUV;
}

float AngularDistanceFromSun(float2 uv, float2 sunUV) {
    float2 delta = uv - sunUV;
    delta.x *= ScreenSize.x / ScreenSize.y; // Aspect ratio correction
    return length(delta);
}
```

### 1.7 Complete HLSL -- Per-Channel Limb-Darkened Sun Disc

```hlsl
// ===================================================================
//  SB_SolarDisc.fxh -- Wavelength-Dependent Limb-Darkened Sun Disc
//  SkyrimBridge Vol. 7
// ===================================================================

// --- User Parameters (ENB UI) ---
float  SUN_SizeMultiplier  < string UIName = "Sun Size Multiplier";
    float UIMin = 0.5; float UIMax = 5.0; > = 1.0;
float  SUN_Intensity       < string UIName = "Sun Disc Intensity";
    float UIMin = 0.0; float UIMax = 10.0; > = 1.0;
float  SUN_LimbStrength    < string UIName = "Limb Darkening Strength";
    float UIMin = 0.0; float UIMax = 1.5; > = 1.0;
float  SUN_EdgeSoftness    < string UIName = "Disc Edge Softness";
    float UIMin = 0.001; float UIMax = 0.1; > = 0.02;

// --- Per-Channel Limb Darkening Coefficients ---
// Neckel & Labs (1994), Pierce (2000)
static const float3 LIMB_U = float3(0.50, 0.60, 0.75);

// --- Physical Constants ---
static const float SUN_HALF_ANGLE_RAD = 0.004625; // tan(0.265 deg)

struct SolarDiscResult {
    float3 color;       // Final disc color (HDR)
    float  mask;        // 1.0 inside disc, 0.0 outside
    float  limbRadius;  // Fractional radius from disc center [0,1]
};

SolarDiscResult ComputeSolarDisc(
    float2 uv,
    float2 sunUV,
    float3 sunColor,
    float  fovDegrees,
    float  rawDepth
) {
    SolarDiscResult result = (SolarDiscResult)0;

    // --- Sky-only depth test ---
    if (rawDepth < 0.9998)
        return result;

    // --- Compute angular distance from sun center ---
    float2 delta = uv - sunUV;
    delta.x *= ScreenSize.x / ScreenSize.y;
    float angularDist = length(delta);

    // --- Compute disc radius in screen-space ---
    float halfFOV_rad = fovDegrees * 0.5 * 3.14159265 / 180.0;
    float discRadius  = SUN_HALF_ANGLE_RAD / tan(halfFOV_rad) * SUN_SizeMultiplier;

    // --- Fractional radius within disc ---
    float limbR = angularDist / max(discRadius, 0.00001);

    // Early out: well outside disc
    if (limbR > 1.5)
        return result;

    // --- Limb darkening computation ---
    float cosTheta = sqrt(max(0.0, 1.0 - limbR * limbR));

    // Per-channel Hestroffer power-law (research-validated)
    float3 limbDarkening = pow(max(cosTheta, 1e-4), float3(0.429, 0.530, 0.621));
    limbDarkening = lerp(float3(1,1,1), limbDarkening, SUN_LimbStrength);

    // --- Disc edge mask ---
    float edgeMask = smoothstep(1.0, 1.0 - SUN_EdgeSoftness, limbR);

    // --- Assemble result ---
    result.color      = sunColor * limbDarkening * edgeMask * SUN_Intensity;
    result.mask       = edgeMask;
    result.limbRadius = limbR;

    return result;
}

// ===================================================================
//  Integration wrapper with SkyrimBridge / ENB fallback
// ===================================================================
SolarDiscResult GetSolarDisc(float2 uv) {
    float3 sunDir;
    float  fov;

    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        sunDir = SB_Celest_SunPosition.xyz;
        fov    = SB_Camera_Position.w;
    } else
    #endif
    {
        sunDir = -SunDirection.xyz;
        fov    = FieldOfView;
    }

    float2 sunUV = ProjectSunToScreen(sunDir);

    float3 sunCol;
    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        sunCol = SB_Celest_SunColor.rgb * SB_Celest_SunColor.a;
    } else
    #endif
    {
        sunCol = SunColor.rgb * ENightDayFactor;
    }

    float rawDepth = TextureDepth.SampleLevel(PointClampSampler, uv, 0).x;
    return ComputeSolarDisc(uv, sunUV, sunCol, fov, rawDepth);
}
```

### 1.8 Depth Test Considerations

The threshold `rawDepth > 0.9998` relies on Skyrim's reversed-Z depth buffer where sky pixels approach 1.0. Edge cases:

- **Cloud meshes**: Skyrim renders some cloud layers as geometry with depth < 1.0. Use SB_Weather_Flags.y (isOvercast) to suppress the disc during heavy overcast.
- **Distant mountains**: At extreme view distances, LOD terrain can approach the sky threshold. The 0.9998 value is conservative; tighten to 0.99995 if artifacts appear.

```hlsl
// Enhanced depth test with cloud awareness
float SunDepthTest(float2 uv) {
    float rawDepth = TextureDepth.SampleLevel(PointClampSampler, uv, 0).x;
    float isSky = step(0.9998, rawDepth);

    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        float overcast = SB_Weather_Flags.y;
        isSky *= 1.0 - overcast * 0.95; // Keep 5% for silver lining

        float precip = max(SB_Precip_Intensity.x, SB_Precip_Intensity.y);
        isSky *= 1.0 - precip * 0.8;
    }
    #endif

    return isSky;
}
```

### 1.9 Performance

| Operation | Cost (1440p) | Notes |
|-----------|-------------|-------|
| Screen projection | ~0 | 1 matrix multiply, once per frame |
| Disc rendering | ~0.01ms | 0.005% pixel coverage, early-out |
| Hestroffer vs Minnaert | identical | 3 pow() either way |
| Quadratic upgrade | +3 MAD | Only when disc > ~50 pixels |

---

## 2. Atmospheric Extinction Model

### 2.1 Rayleigh Scattering

Rayleigh scattering occurs when photons interact with particles much smaller than the wavelength (N2/O2 molecules ~0.3nm vs visible light 400-700nm). The cross-section follows an inverse fourth-power wavelength dependence:

```
sigma_R(lambda) ~ 1 / lambda^4

Normalizing to red = 1.0:
  Red   (620nm): 1.00
  Green (540nm): 1.73
  Blue  (460nm): 3.29

Standard atmosphere Rayleigh scattering coefficients at sea level:
  beta_R = float3(5.802e-6, 13.558e-6, 33.1e-6) per meter
  (Bruneton & Neyret 2008, Hillaire 2020)

Normalized ratio: float3(0.175, 0.410, 1.000)
```

**Research correction**: The original document used `float3(0.06, 0.14, 0.30)` which has the correct ratio shape but magnitude too low, causing sunset extinction to kick in too late (sun stays white until very low elevation). The corrected Bruneton/Hillaire values should be used with appropriate per-scene scaling.

### 2.2 Mie Scattering

Mie scattering occurs with particles comparable in size to wavelength (aerosols, water droplets, dust, 0.1-10um). Key properties:

- Nearly wavelength-independent (all channels attenuated equally)
- Strongly forward-peaked (Henyey-Greenstein g = 0.75 to 0.85)
- Increases with aerosol density (haze, fog, pollution)

```
Mie extinction coefficient at sea level:
  Clear day:  beta_M ~ 2.0e-5 /m
  Hazy:       beta_M ~ 8.0e-5 /m
  Foggy:      beta_M ~ 2.0e-4 /m
```

### 2.3 Beer-Lambert Extinction Law

```
I_transmitted = I_source * exp(-tau)

tau = tau_zenith * airmass(elevation)
```

### 2.4 Kasten-Young Airmass Formula (Research-Validated Upgrade)

The original used `airmass = 1/max(sin(elevation), 0.01)` which diverges to 100 at 0.57 deg elevation, causing a black sun near the horizon. The Kasten & Young (1989) formula handles near-horizon correctly:

```hlsl
// BEFORE (original):
float airmass = 1.0 / max(sin(elevation), 0.01);
// At 0 degrees: airmass = 100 (too high, causes black sun)

// AFTER (Kasten & Young 1989):
float elevDeg = elevation * 57.29578; // Convert to degrees
float airmass = 1.0 / (sin(elevation) +
    0.50572 * pow(max(elevDeg + 6.07995, 0.001), -1.6364));
// At 0 degrees: airmass = 37.9 (matches observations)
// At -0.5 degrees (just below horizon): airmass = 40.4
// Single pow() instruction; negligible cost increase.
```

### 2.5 Complete Atmospheric Extinction HLSL

```hlsl
// ===================================================================
//  Atmospheric Extinction Vector
//  Rayleigh: strong wavelength dependence (removes blue first)
//  Mie: wavelength-independent (uniform dimming)
// ===================================================================

static const float3 TAU_RAYLEIGH_ZENITH = float3(0.06, 0.14, 0.30);
static const float  TAU_MIE_ZENITH = 0.02;

float3 AtmosphericExtinction(float sunElevation, float mieMultiplier) {
    float sinElev = max(sin(sunElevation), 0.006);

    // Kasten-Young airmass
    float elevDeg = sunElevation * 57.29578;
    float airmass = 1.0 / (sinElev +
        0.50572 * pow(max(elevDeg + 6.07995, 0.001), -1.6364));

    float3 tau_rayleigh = TAU_RAYLEIGH_ZENITH * airmass;
    float  tau_mie = TAU_MIE_ZENITH * airmass * mieMultiplier;
    float3 tau_total = tau_rayleigh + float3(tau_mie, tau_mie, tau_mie);

    return exp(-tau_total);
}
```

### 2.6 Weather-Reactive Mie Multiplier

```hlsl
float GetMieMultiplier() {
    float mie = 1.0; // Baseline: clear atmosphere

    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        float hazeDensity = SB_Atmos_FogDensity.z;
        mie += hazeDensity * 3.0;

        float rain = SB_Precip_Intensity.x;
        mie += rain * 5.0;

        float snow = SB_Precip_Intensity.y;
        mie += snow * 4.0;

        float overcast = SB_Weather_Flags.y;
        mie += overcast * 6.0;

        float storm = SB_Weather_Flags.z;
        mie += storm * 10.0;
    } else
    #endif
    {
        mie += EWeatherParam.w * 4.0;
        mie += EWeatherParam.y * 3.0;
    }

    return mie;
}
```

### 2.7 Complete Pipeline with Warm Emission

```hlsl
float3 GetSunExtinction(float3 sunDir) {
    float elevation;
    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        elevation = SB_Celest_SunPosition.w;
    } else
    #endif
    {
        elevation = asin(max(sunDir.z, 0.0));
    }

    float mie = GetMieMultiplier();
    return AtmosphericExtinction(elevation, mie);
}

float3 ApplyAtmosphericExtinction(float3 rawSunColor, float3 sunDir) {
    float3 transmittance = GetSunExtinction(sunDir);
    float3 attenuatedColor = rawSunColor * transmittance;

    float elevation;
    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        elevation = SB_Celest_SunPosition.w;
    } else
    #endif
    {
        elevation = asin(max(sunDir.z, 0.0));
    }

    // At low elevation, scattered Mie light adds warm emission
    float horizonFactor = smoothstep(0.15, 0.0, elevation);
    float3 warmEmission = rawSunColor * float3(0.5, 0.2, 0.05) * horizonFactor * 0.3;
    attenuatedColor += warmEmission;

    return attenuatedColor;
}
```

### 2.8 Rayleigh Phase Function

```hlsl
// Angular distribution of scattered Rayleigh light:
// P_R(theta) = (3 / 16*pi) * (1 + cos^2(theta))
// Forward and backward peaks are equal (symmetric scattering).
// Contributes to sky brightening within ~20 degrees of sun.

float RayleighPhase(float cosTheta) {
    return (3.0 / (16.0 * 3.14159265)) * (1.0 + cosTheta * cosTheta);
}
```

### 2.9 Fallback Behavior

| Data | SkyrimBridge | Standalone ENB |
|------|-------------|----------------|
| Elevation | SB_Celest_SunPosition.w (radians, precise) | asin(SunDirection.z) (less accurate near horizon) |
| Mie multiplier | SB_Atmos_FogDensity, SB_Precip_Intensity, SB_Weather_Flags | EWeatherParam.w and .y |
| Sun color | SB_Celest_SunColor.rgb * .a | SunColor.rgb * ENightDayFactor |
| FOV | SB_Camera_Position.w | FieldOfView |

### 2.10 Performance

Atmospheric extinction is computed only for pixels that pass the sun-proximity test (within the corona/aureole region). For the majority of screen pixels, extinction is applied as a uniform color correction to the entire sun sprite composite. Cost: negligible.

---

## 3. Multi-Component Corona & Aureole

### 3.1 Physical Components

The visible glow surrounding the sun is the superposition of three distinct physical processes:

**Component 1: Forward Mie Aureole** -- The brightest component, concentrated within 2-5 deg of sun center. Caused by forward scattering from aerosols (water droplets, dust, pollen). Described by Henyey-Greenstein phase function with g=0.75-0.85.

```
At theta=0 (directly toward sun), g=0.8:
  P_HG(0) ~ 3.58 (vs 0.0597 for Rayleigh)
  This is ~60x brighter than surrounding sky.
```

**Component 2: Rayleigh Sky Brightening** -- Broader, dimmer glow extending 10-20 deg from sun. Blue-biased in clear conditions, suppressed by haze.

```
Key difference from Mie aureole:
  Mie:     sharp peak, white/warm, increases with haze
  Rayleigh: broad peak, bluish, decreases with haze
```

**Component 3: Bishop's Ring (Diffraction Corona)** -- Colored concentric rings from diffraction around uniformly-sized water droplets. Ring radius inversely proportional to droplet diameter. Rare but striking.

```
ring_radius(degrees) ~ 1.22 * lambda / d_droplet
  For d=10um: ring at ~3.4 deg (red) to ~2.5 deg (blue)
  For d=20um: ring at ~1.7 deg to ~1.25 deg
```

### 3.2 Cornette-Shanks Phase Function

Research-validated as the correct Mie phase function for atmospheric scattering. Unlike standard HG which becomes isotropic at g=0, Cornette-Shanks correctly reduces to the Rayleigh phase function at g=0. This matters when transitioning between clear (Rayleigh-dominated) and hazy (Mie-dominated) conditions. Cost difference: 1 multiply and 1 addition over standard HG.

```hlsl
float CornetteShanks(float cosTheta, float g) {
    float g2 = g * g;
    float num = 3.0 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = 8.0 * 3.14159265 * (2.0 + g2) *
                  pow(abs(1.0 + g2 - 2.0 * g * cosTheta), 1.5);
    return num / max(denom, 0.0001);
}
```

### 3.3 Buie Sunshape Model for Aureole (Research-Validated)

The Buie (2003) two-piece analytical aureole model uses a single parameter (Circumsolar Ratio, CSR) that maps directly to SkyrimBridge atmospheric haze data:

```hlsl
// Buie sunshape: disc region + aureole region
// theta = angular distance from sun center in milliradians
// CSR = circumsolar ratio
float BuieSunshape(float theta_mrad, float CSR) {
    if (theta_mrad <= 4.65) {
        // Disc region
        return cos(0.326 * theta_mrad) / max(cos(0.308 * theta_mrad), 0.01);
    } else {
        // Aureole region
        float kappa = 2.2 * log(max(0.52 * CSR, 0.001)) + 0.1;
        float gamma = -0.3 + 0.9 * log(max(13.5 * CSR, 0.001));
        return exp(kappa) * pow(theta_mrad, gamma);
    }
}

// Map SkyrimBridge haze to CSR:
// Clear (haze=0.0) -> CSR=0.03 (minimal aureole)
// Hazy (haze=0.3)  -> CSR=0.15 (moderate aureole)
// Foggy (haze=0.8) -> CSR=0.40 (strong aureole)
float GetCSR() {
    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        return lerp(0.03, 0.40, saturate(SB_Atmos_FogDensity.z));
    }
    #endif
    return 0.05;
}
```

### 3.4 Draine Phase Function -- Pre-Fitted Parameter Sets

The Draine phase function (Jendersie & d'Eon, SIGGRAPH 2023) requires per-droplet-size parameters. For ENB we pre-fit for representative atmospheric conditions and interpolate at runtime:

```hlsl
struct DraineParams {
    float g;      // HG asymmetry
    float alpha;  // Draine angular dependence
    float weight; // Blend weight with HG
};

static const DraineParams DRAINE_CLEAR   = { 0.76, 0.15, 0.3 };
static const DraineParams DRAINE_HAZY    = { 0.82, 0.25, 0.4 };
static const DraineParams DRAINE_FOGGY   = { 0.88, 0.45, 0.6 };
static const DraineParams DRAINE_RAIN    = { 0.92, 0.60, 0.7 };
static const DraineParams DRAINE_STORM   = { 0.95, 0.70, 0.8 };

DraineParams GetDraineForWeather(float mieMultiplier) {
    DraineParams result;
    if (mieMultiplier < 2.0) {
        float t = mieMultiplier / 2.0;
        result.g      = lerp(DRAINE_CLEAR.g,      DRAINE_HAZY.g,      t);
        result.alpha   = lerp(DRAINE_CLEAR.alpha,  DRAINE_HAZY.alpha,  t);
        result.weight  = lerp(DRAINE_CLEAR.weight, DRAINE_HAZY.weight, t);
    } else if (mieMultiplier < 6.0) {
        float t = (mieMultiplier - 2.0) / 4.0;
        result.g      = lerp(DRAINE_HAZY.g,      DRAINE_FOGGY.g,      t);
        result.alpha   = lerp(DRAINE_HAZY.alpha,  DRAINE_FOGGY.alpha,  t);
        result.weight  = lerp(DRAINE_HAZY.weight, DRAINE_FOGGY.weight, t);
    } else {
        float t = saturate((mieMultiplier - 6.0) / 4.0);
        result.g      = lerp(DRAINE_FOGGY.g,      DRAINE_RAIN.g,      t);
        result.alpha   = lerp(DRAINE_FOGGY.alpha,  DRAINE_RAIN.alpha,  t);
        result.weight  = lerp(DRAINE_FOGGY.weight, DRAINE_RAIN.weight, t);
    }
    return result;
}

float DrainePhase(float cosTheta, DraineParams dp) {
    float g2 = dp.g * dp.g;
    float denom = pow(abs(1.0 + g2 - 2.0 * dp.g * cosTheta), 1.5);
    float hg = (1.0 - g2) / (4.0 * 3.14159265 * denom);
    float draine_factor = (1.0 + dp.alpha * cosTheta * cosTheta) /
                          (1.0 + dp.alpha * (1.0 + 2.0 * g2) / 3.0);
    float draine = hg * draine_factor;
    return lerp(hg, draine, dp.weight);
}
```

### 3.5 Complete Multi-Component Corona HLSL

```hlsl
// ===================================================================
//  SB_Corona.fxh -- Multi-Component Corona & Aureole
//  Two primary components with optional third (Bishop's Ring).
//  Weather-reactive via SkyrimBridge atmospheric data.
// ===================================================================

float CORONA_MieIntensity     < string UIName = "Mie Aureole Intensity";
    float UIMin = 0.0; float UIMax = 5.0; > = 1.0;
float CORONA_MieG             < string UIName = "Mie Asymmetry (g)";
    float UIMin = 0.5; float UIMax = 0.95; > = 0.80;
float CORONA_RayleighIntensity < string UIName = "Rayleigh Glow Intensity";
    float UIMin = 0.0; float UIMax = 3.0; > = 0.5;
float CORONA_RayleighExtent    < string UIName = "Rayleigh Glow Extent (deg)";
    float UIMin = 5.0; float UIMax = 30.0; > = 15.0;
float CORONA_Saturation        < string UIName = "Corona Color Saturation";
    float UIMin = 0.0; float UIMax = 1.5; > = 0.8;

struct CoronaResult {
    float3 mieAureole;
    float3 rayleighGlow;
    float3 bishopRing;
    float3 combined;
};

CoronaResult ComputeCorona(
    float  angularDist,
    float3 sunColor,
    float3 skyColor,
    float  mieMultiplier,
    float  precipIntensity
) {
    CoronaResult result = (CoronaResult)0;
    float angularDistDeg = angularDist * (180.0 / 3.14159265);

    // --- Component 1: Mie Aureole ---
    float cosAngle = cos(angularDist);
    float g = lerp(CORONA_MieG, min(CORONA_MieG + 0.10, 0.95),
                   saturate(mieMultiplier / 10.0));
    float miePhase = CornetteShanks(cosAngle, g);
    float mieForward = CornetteShanks(1.0, g);
    float mieNorm = miePhase / max(mieForward, 0.001);

    float3 mieColor = lerp(float3(1.0, 1.0, 1.0), sunColor,
                           CORONA_Saturation * 0.5);
    float mieStrength = CORONA_MieIntensity * (1.0 + mieMultiplier * 0.3);
    result.mieAureole = mieColor * mieNorm * mieStrength;

    // --- Component 2: Rayleigh Sky Brightening ---
    float rayleighPhase = (3.0 / (16.0 * 3.14159265)) *
                          (1.0 + cosAngle * cosAngle);
    float rayleighNorm = rayleighPhase / 0.1194;
    float rayleighFalloff = smoothstep(CORONA_RayleighExtent, 0.0,
                                        angularDistDeg);

    float3 rayleighColor = lerp(skyColor, float3(0.7, 0.8, 1.0), 0.5);
    rayleighColor = lerp(rayleighColor, float3(0.9, 0.9, 0.9),
                         saturate(mieMultiplier / 8.0));

    float rayleighStrength = CORONA_RayleighIntensity *
                              rayleighNorm * rayleighFalloff;
    rayleighStrength *= 1.0 / (1.0 + mieMultiplier * 0.2);
    result.rayleighGlow = rayleighColor * rayleighStrength;

    // --- Component 3: Bishop's Ring (optional) ---
    #if defined(SB_BISHOPS_RING_ENABLE)
    {
        float bishopTrigger = smoothstep(0.02, 0.08, precipIntensity)
                            * smoothstep(0.30, 0.15, precipIntensity);

        if (bishopTrigger > 0.01) {
            float ringRadius = 3.5;
            float ringWidth  = 0.8;
            float ringDist = abs(angularDistDeg - ringRadius);
            float ringMask = exp(-ringDist * ringDist /
                                 (ringWidth * ringWidth));

            float chromaFactor = (angularDistDeg - ringRadius) / ringWidth;
            float3 ringColor = float3(
                smoothstep(-1.0, 1.0, chromaFactor),
                1.0 - abs(chromaFactor) * 0.5,
                smoothstep(1.0, -1.0, chromaFactor)
            );

            result.bishopRing = ringColor * ringMask * bishopTrigger *
                                sunColor * 0.15;
        }
    }
    #endif

    // --- Combine ---
    result.combined = result.mieAureole + result.rayleighGlow +
                      result.bishopRing;
    result.combined = min(result.combined, sunColor * 5.0);

    return result;
}
```

### 3.6 Disc + Corona Compositing

```hlsl
float3 CompositeSunAndCorona(float2 uv) {
    SolarDiscResult disc = GetSolarDisc(uv);

    float2 sunUV = ProjectSunToScreen(GetSunDirection());
    float2 delta = uv - sunUV;
    delta.x *= ScreenSize.x / ScreenSize.y;
    float angularDist = length(delta);

    float halfFOV_rad = GetFOV() * 0.5 * 3.14159265 / 180.0;
    float angularRad = atan(angularDist * tan(halfFOV_rad));

    // Early out: beyond corona extent (25 degrees)
    float maxExtent = 25.0 * 3.14159265 / 180.0;
    if (angularRad > maxExtent && disc.mask < 0.001)
        return float3(0, 0, 0);

    float3 sunColor = GetExtinctionCorrectedSunColor();
    float3 skyColor = GetSkyColor();
    float  mie = GetMieMultiplier();
    float  precip = GetPrecipIntensity();

    CoronaResult corona = ComputeCorona(angularRad, sunColor,
                                        skyColor, mie, precip);

    // Disc overrides corona inside disc radius
    float outsideDisc = 1.0 - disc.mask;
    float3 result = disc.color + corona.combined * outsideDisc;
    result *= SunDepthTest(uv);

    return result;
}
```

### 3.7 Weather Response Matrix

| Weather State | Mie g | Mie Intensity | Rayleigh Visible | Bishop's Ring | Overall Character |
|---------------|-------|---------------|------------------|---------------|-------------------|
| Clear         | 0.76  | Low           | Strong (blue)    | No            | Blue halo, subtle aureole |
| Hazy          | 0.82  | Medium        | Suppressed       | No            | White aureole, broad |
| Light rain    | 0.88  | High          | Hidden           | Possible      | Large white glow |
| Heavy rain    | 0.92  | Very high     | Hidden           | No            | Massive diffuse glow |
| Storm         | 0.95  | Extreme       | Hidden           | No            | Sun barely visible |

### 3.8 Performance

Phase function evaluation uses only ALU operations (no texture fetches). The Schlick approximation can replace Cornette-Shanks for ~30% ALU reduction at slight accuracy cost.

---

## 4. Moon Rendering & Opposition Surge

### 4.1 Skyrim's Dual Moon System

Skyrim features two moons: Masser (larger, reddish) and Secunda (smaller, whitish-blue). Masser follows a 24-game-day cycle (8 phases, 3 days each); Secunda is offset by half a cycle.

SkyrimBridge exposes moon data through `SB_Celest_MoonPosition`: `.xyz` = world-space direction, `.w` = phase float from 0.0 (new moon) through 0.5 (full moon) to 1.0 (back to new).

### 4.2 Opposition Surge (Hapke Model)

The most visually distinctive feature of real full-moon brightness, absent from virtually all game implementations. The moon exhibits **limb brightening** near full phase, not limb darkening. This is the opposition effect caused by shadow hiding and coherent backscatter enhancement in lunar regolith.

```hlsl
// Opposition surge term (Hapke model, simplified)
// alpha = phase angle in radians (0 = full moon facing sun)
float OppositionSurge(float alpha) {
    // B0 = amplitude, h = angular width
    static const float B0 = 1.6; // Typical lunar regolith
    static const float h  = 0.06; // ~3.4 degrees half-width

    // Hapke: B(alpha) = B0 / (1 + tan(alpha/2) / h)
    float tanHalfAlpha = tan(alpha * 0.5);
    return B0 / (1.0 + tanHalfAlpha / h);
}

// At full moon (alpha ~ 0): surge ~ B0 = 1.6 (60% brightness boost)
// At quarter (alpha ~ 90 deg): surge ~ 0.03 (negligible)
// Cost: 1 tan() + 1 divide = ~2 ALU ops.
```

### 4.3 Moon Disc with Phase Illumination

```hlsl
// ===================================================================
//  Moon Disc Rendering with Phase-Aware Illumination
//  Renders Masser and Secunda with:
//  - Phase-dependent illumination geometry
//  - Atmospheric extinction at low elevation
//  - Earthshine on the dark limb
//  - Hapke opposition surge at full moon
// ===================================================================

float MOON_Intensity    < string UIName = "Moon Brightness";
    float UIMin = 0.0; float UIMax = 5.0; > = 1.0;
float MOON_EarthShine   < string UIName = "Earthshine Intensity";
    float UIMin = 0.0; float UIMax = 0.3; > = 0.05;
float MOON_Size         < string UIName = "Moon Size Multiplier";
    float UIMin = 0.5; float UIMax = 5.0; > = 2.0;

// Moon angular diameters (game-world, enlarged for visibility)
static const float MASSER_HALF_ANGLE  = 0.0131; // tan(0.75 deg)
static const float SECUNDA_HALF_ANGLE = 0.0070; // tan(0.4 deg)

struct MoonResult {
    float3 color;
    float  mask;
};

MoonResult ComputeMoonDisc(
    float2 uv,
    float3 moonDir,
    float  moonPhase,
    float  discHalfAngle,
    float3 moonBaseColor,
    float  fovDegrees,
    float  rawDepth
) {
    MoonResult result = (MoonResult)0;

    if (rawDepth < 0.9998) return result;

    float2 moonUV = ProjectSunToScreen(moonDir);
    if (moonUV.x < -5.0) return result;

    float2 delta = uv - moonUV;
    delta.x *= ScreenSize.x / ScreenSize.y;
    float dist = length(delta);

    float halfFOV_rad = fovDegrees * 0.5 * 3.14159265 / 180.0;
    float discRadius = discHalfAngle / tan(halfFOV_rad) * MOON_Size;

    float limbR = dist / max(discRadius, 0.00001);
    if (limbR > 1.2) return result;

    // --- Phase illumination ---
    float phaseAngle = moonPhase * 2.0 * 3.14159265;
    float localX = delta.x / max(discRadius, 0.00001);
    float terminatorX = cos(phaseAngle);
    float illumination = smoothstep(terminatorX - 0.1,
                                     terminatorX + 0.1, -localX);
    float phaseBrightness = abs(sin(moonPhase * 3.14159265));

    // --- Opposition surge at full moon ---
    float phaseAngleRad = abs(moonPhase - 0.5) * 3.14159265;
    float surge = OppositionSurge(phaseAngleRad);

    // --- Moon color with limb treatment ---
    float cosTheta = sqrt(max(0.0, 1.0 - limbR * limbR));
    // Full moon: limb BRIGHTENING (opposition surge dominates)
    // Other phases: gentle limb darkening
    float limbFactor = lerp(0.6 + 0.4 * cosTheta,  // darkening
                            1.0 + 0.3 * (1.0 - cosTheta), // brightening
                            phaseBrightness * 0.5);

    float3 litColor = moonBaseColor * limbFactor * illumination *
                      phaseBrightness * surge * MOON_Intensity;

    // --- Earthshine ---
    float darkPortion = 1.0 - illumination;
    float3 earthshineColor = float3(0.4, 0.5, 0.7) * MOON_EarthShine;
    float3 darkColor = earthshineColor * darkPortion * cosTheta *
                       (1.0 - phaseBrightness * 0.5);

    // --- Atmospheric extinction ---
    float moonElevation = asin(max(moonDir.z, 0.0));
    float3 moonExtinction = AtmosphericExtinction(moonElevation, 1.0);

    // --- Disc edge ---
    float edgeMask = smoothstep(1.0, 0.96, limbR);

    result.color = (litColor + darkColor) * moonExtinction * edgeMask;
    result.mask = edgeMask;

    return result;
}
```

### 4.4 Dual Moon Integration

```hlsl
float3 RenderMoons(float2 uv) {
    float rawDepth = TextureDepth.SampleLevel(PointClampSampler, uv, 0).x;
    float fov;

    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        fov = SB_Camera_Position.w;
    } else
    #endif
    {
        fov = FieldOfView;
    }

    float3 moonAccum = float3(0, 0, 0);

    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        // Masser (reddish, larger)
        MoonResult masser = ComputeMoonDisc(
            uv, SB_Celest_MoonPosition.xyz,
            SB_Celest_MoonPosition.w,
            MASSER_HALF_ANGLE,
            float3(0.95, 0.85, 0.75), fov, rawDepth
        );
        moonAccum += masser.color;

        // Secunda (bluish-white, smaller)
        float3 secundaDir = normalize(SB_Celest_MoonPosition.xyz +
                            float3(0.1, 0.05, 0.0));
        float secundaPhase = frac(SB_Celest_MoonPosition.w + 0.5);
        MoonResult secunda = ComputeMoonDisc(
            uv, secundaDir, secundaPhase,
            SECUNDA_HALF_ANGLE,
            float3(0.8, 0.85, 0.95), fov, rawDepth
        );
        moonAccum += secunda.color;
    }
    #endif

    moonAccum *= 1.0 - ENightDayFactor;
    return moonAccum;
}
```

### 4.5 Procedural Star Field

```hlsl
float StarHash(float2 p) {
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float3 RenderStarField(float2 uv, float3 viewDir, float rawDepth) {
    if (rawDepth < 0.9998) return float3(0, 0, 0);
    float nightFactor = 1.0 - ENightDayFactor;
    if (nightFactor < 0.01) return float3(0, 0, 0);

    float2 sphereUV = float2(
        atan2(viewDir.x, viewDir.z) / (2.0 * 3.14159265) + 0.5,
        asin(viewDir.y) / 3.14159265 + 0.5
    );

    float2 gridUV = sphereUV * 200.0;
    float2 gridID = floor(gridUV);
    float2 gridFrac = frac(gridUV);

    float3 starColor = float3(0, 0, 0);

    [unroll] for (int dx = -1; dx <= 1; dx++) {
        [unroll] for (int dy = -1; dy <= 1; dy++) {
            float2 neighbor = gridID + float2(dx, dy);
            float hash = StarHash(neighbor);

            if (hash > 0.985) {
                float2 starPos = float2(
                    StarHash(neighbor + float2(7.13, 3.71)),
                    StarHash(neighbor + float2(13.37, 7.19))
                );

                float dist = length(gridFrac - float2(dx, dy) - starPos);
                float star = exp(-dist * dist * 800.0);

                float temp = StarHash(neighbor + float2(19.23, 5.67));
                float3 color;
                if (temp < 0.2)       color = float3(0.7, 0.8, 1.0);
                else if (temp < 0.6)  color = float3(1.0, 1.0, 0.95);
                else if (temp < 0.85) color = float3(1.0, 0.9, 0.7);
                else                  color = float3(1.0, 0.7, 0.5);

                float brightness = StarHash(neighbor + float2(23.45, 9.01));
                brightness = brightness * brightness * 3.0;

                float twinkle = sin(hash * 100.0 +
                    Timer.x * (0.5 + hash * 2.0)) * 0.3 + 0.7;

                starColor += color * star * brightness * twinkle;
            }
        }
    }

    float elevation = asin(max(viewDir.y, 0.0));
    float3 starExtinction = AtmosphericExtinction(elevation, 0.5);
    return starColor * nightFactor * starExtinction * 0.5;
}
```

### 4.6 Performance

| Component | Cost (1440p) | Notes |
|-----------|-------------|-------|
| Moon disc (per moon) | ~0.01ms | Tiny pixel coverage + early-out |
| Opposition surge | ~0 | 2 ALU ops within disc pixels only |
| Earthshine | ~0 | Additive within same disc loop |
| Star field | ~0.05ms | Hash-only, no texture |
| Atmospheric extinction | ~0 | Shared with solar pipeline |

---

## 5. God Rays / Volumetric Light Shafts

### 5.1 Overview

Three primary approaches exist for screen-space volumetric light shafts:

| Method | Samples | Passes | Quality | Cost (1440p) |
|--------|---------|--------|---------|-------------|
| Mitchell radial blur | 64 | 1 | Good | ~0.8ms full, ~0.2ms half |
| Crytek cascaded | 48 (3x16) | 3 | Excellent | ~0.45ms |
| Epipolar sampling | varies | 2 | Best | ~0.3ms (complex setup) |

### 5.2 Mitchell Radial Blur (GPU Gems 3)

Single-pass approach. For each pixel, sample along a radial line from pixel toward projected sun position, accumulating sky brightness and rejecting geometry.

```hlsl
// ===================================================================
//  God Rays -- Mitchell Radial Blur (Single Pass, 64 Samples)
//  Reference: Mitchell, GPU Gems 3, Chapter 13 (2007)
//  Cost: ~0.8ms at full-res 1440p, ~0.2ms at half-res
// ===================================================================

float GR_Density     < string UIName = "God Ray Density";
    float UIMin = 0.1; float UIMax = 2.0; > = 0.8;
float GR_Decay       < string UIName = "God Ray Decay";
    float UIMin = 0.9; float UIMax = 1.0; > = 0.97;
float GR_Exposure    < string UIName = "God Ray Exposure";
    float UIMin = 0.0; float UIMax = 2.0; > = 0.5;
float GR_Threshold   < string UIName = "Sky Brightness Threshold";
    float UIMin = 0.5; float UIMax = 1.0; > = 0.9998;

#define GR_NUM_SAMPLES 64

float3 GodRays_Mitchell(float2 uv, float2 sunUV) {
    float2 deltaUV = (uv - sunUV) * GR_Density / float(GR_NUM_SAMPLES);

    // Temporal dithering to prevent concentric ring artifacts
    float dither = frac(52.9829189 * frac(dot(uv * ScreenSize.xy,
                        float2(0.06711056, 0.00583715)))
                        + Timer.x * 0.2345);
    float2 sampleUV = uv - deltaUV * dither;

    float3 accum = 0;
    float weight = 1.0;

    [loop] for (int i = 0; i < GR_NUM_SAMPLES; i++) {
        sampleUV -= deltaUV;

        if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) {
            weight *= GR_Decay;
            continue;
        }

        float depth = TextureDepth.SampleLevel(PointClampSampler, sampleUV, 0).x;
        float isSky = smoothstep(GR_Threshold - 0.0002, GR_Threshold, depth);

        float3 sampleColor = TextureColor.SampleLevel(
                              LinearClampSampler, sampleUV, 0).rgb;
        float brightness = dot(sampleColor, float3(0.299, 0.587, 0.114));

        accum += isSky * brightness * weight;
        weight *= GR_Decay;
    }

    float sunDist = length(uv - sunUV);
    float distFalloff = 1.0 / (1.0 + sunDist * sunDist * 2.0);

    float3 rayColor;
    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        rayColor = SB_Celest_SunColor.rgb;
    } else
    #endif
    {
        rayColor = SunColor.rgb;
    }

    return accum * GR_Exposure * distFalloff * rayColor /
           float(GR_NUM_SAMPLES);
}
```

### 5.3 Crytek Cascaded Radial Blur (Recommended)

Chains 3 passes with decreasing step sizes. 3 passes of 16 samples produce an effective filter equivalent to 4096 samples at only 48 actual fetches. Dramatically smoother than single-pass.

```hlsl
// ===================================================================
//  God Rays -- Crytek Cascaded Radial Blur (3 Passes, 16 Samples Each)
//
//  Pass 1: Full step, reads from masked scene
//  Pass 2: Half step, refines from Pass 1 output
//  Pass 3: Quarter step, final refinement
//
//  Total: 48 texture fetches, effective reach of ~4096 samples.
//  Cost: ~0.15ms per pass at 1440p = ~0.45ms total
//
//  ENB mapping: 3 technique passes, ping-ponging RenderTargetRGBA32
// ===================================================================

#define CASCADE_SAMPLES 16

float3 CascadePass1(float2 uv, float2 sunUV) {
    float2 delta = (uv - sunUV) * GR_Density / float(CASCADE_SAMPLES);
    float3 result = 0;
    float2 sampleUV = uv;

    [unroll] for (int i = 0; i < CASCADE_SAMPLES; i++) {
        sampleUV -= delta;
        float depth = TextureDepth.SampleLevel(PointClampSampler,
                      saturate(sampleUV), 0).x;
        float isSky = step(GR_Threshold, depth);
        float3 col = TextureColor.SampleLevel(LinearClampSampler,
                     saturate(sampleUV), 0).rgb;
        result += col * isSky;
    }
    return result / float(CASCADE_SAMPLES);
}

float3 CascadePass2(float2 uv, float2 sunUV) {
    float2 delta = (uv - sunUV) * GR_Density * 0.5 / float(CASCADE_SAMPLES);
    float3 result = 0;
    float2 sampleUV = uv;

    [unroll] for (int i = 0; i < CASCADE_SAMPLES; i++) {
        sampleUV -= delta;
        result += RenderTargetRGBA32.SampleLevel(LinearClampSampler,
                  saturate(sampleUV), 0).rgb;
    }
    return result / float(CASCADE_SAMPLES);
}

float3 CascadePass3(float2 uv, float2 sunUV) {
    float2 delta = (uv - sunUV) * GR_Density * 0.25 / float(CASCADE_SAMPLES);
    float3 result = 0;
    float2 sampleUV = uv;

    [unroll] for (int i = 0; i < CASCADE_SAMPLES; i++) {
        sampleUV -= delta;
        result += RenderTargetRGBA32.SampleLevel(LinearClampSampler,
                  saturate(sampleUV), 0).rgb;
    }
    return result / float(CASCADE_SAMPLES);
}

// ENB Technique declarations:
// Technique_GR_Blur1: RenderTarget = RenderTargetRGBA32
// Technique_GR_Blur2: RenderTarget = RenderTargetRGBA32
// Technique_GR_Blur3: RenderTarget = (final composite)
```

### 5.4 Fire/Torch Rejection

```hlsl
// Strategy 1: Directional test -- only sun-direction rays
float SunProximityMask(float2 sampleUV, float2 sunUV) {
    float dist = length(sampleUV - sunUV);
    return smoothstep(0.35, 0.20, dist);
}

// Strategy 2: Combined depth + luminance
float IsGodRaySource(float2 sampleUV, float2 sunUV) {
    float depth = TextureDepth.SampleLevel(PointClampSampler, sampleUV, 0).x;
    float isSky = step(GR_Threshold, depth);

    float3 col = TextureColor.SampleLevel(LinearClampSampler, sampleUV, 0).rgb;
    float luma = dot(col, float3(0.299, 0.587, 0.114));
    float isBright = smoothstep(2.0, 4.0, luma);
    float nearSun = SunProximityMask(sampleUV, sunUV);

    return max(isSky, isBright * nearSun);
}
```

### 5.5 Weather Integration

```hlsl
float GetGodRayWeatherFactor() {
    float factor = 1.0;

    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        float fogDensity = SB_Atmos_FogDensity.z;
        factor = 0.3 + fogDensity * 3.0;

        factor += SB_Precip_Intensity.x * 1.5;
        factor += SB_Precip_Intensity.y * 0.8;
        factor *= 1.0 - SB_Weather_Flags.z * 0.7;
        factor *= 1.0 - EInteriorFactor;
    } else
    #endif
    {
        factor = 0.5 + EWeatherParam.w * 2.0;
        factor *= 1.0 - EInteriorFactor;
    }

    return saturate(factor);
}
```

### 5.6 ENB Pass Budget

```
Pass allocation in enbeffectprepass.fx:

Technique_GR_Mask:     Generate sun occlusion mask -> RenderTargetR16F
Technique_GR_Blur1:    Cascade pass 1 (full step) -> RenderTargetRGBA32
Technique_GR_Blur2:    Cascade pass 2 (half step) -> RenderTargetRGBA32
Technique_GR_Blur3:    Cascade pass 3 (quarter)   -> RenderTargetRGBA32

Total: 4 passes for cascaded method (2 for Mitchell)

RT lifecycle: RenderTargetRGBA32 is free after GTAO composites.
No overlap with existing RT consumers.
```

---

## 6. Hillaire Atmosphere LUT Pipeline

### 6.1 The Problem

Hillaire's UE5 atmosphere system generates four LUTs via compute shaders:

| LUT | Size | Content |
|-----|------|---------|
| Transmittance | 256x64 | Optical depth integral |
| Multi-scattering | 32x32 | 2nd+ order scatter contribution |
| Sky-view | 192x108 | Hemisphere sky radiance |
| Aerial Perspective | 32x32x32 (2D atlas) | Distance-dependent fog color |

ENB has no compute shaders. However, LUT generation is fundamentally a parallel per-texel operation -- identical to what a pixel shader does.

### 6.2 Pixel Shader LUT Generation into Fixed-Size RTs

Each LUT maps to a fixed-size ENB render target:

| LUT | ENB RT | Valid Region | Cost |
|-----|--------|-------------|------|
| Transmittance | RenderTarget256 | 256x64 of 256x256 | ~0.04ms |
| Multi-scatter | RenderTarget32 | 32x32 | ~0.01ms |
| Sky-view | RenderTarget256 | 192x108 of 256x256 | ~0.10ms |
| Aerial Persp. | RenderTarget1024 | 32x32x32 as 2D atlas | ~0.04ms |
| **Total** | | | **~0.19ms** |

### 6.3 Transmittance LUT Pixel Shader

```hlsl
// ===================================================================
//  Transmittance LUT Generation in Pixel Shader
//  Renders into RenderTarget256; only 256x64 region is valid.
//  U axis = cos(zenith angle) remapped from [-1, 1]
//  V axis = height from ground to atmosphere top
// ===================================================================

static const float R_GROUND = 6360000.0;  // meters
static const float R_ATMOS  = 6460000.0;
static const float H_R = 8000.0;          // Rayleigh scale height
static const float H_M = 1200.0;          // Mie scale height
static const float3 BETA_R = float3(5.802e-6, 13.558e-6, 33.1e-6);
static const float  BETA_M_EXT = 8.396e-6;

float4 PS_TransmittanceLUT(float2 uv : TEXCOORD) : SV_Target {
    // Only use 256x64 of 256x256
    if (uv.y > 64.0 / 256.0) return float4(1, 1, 1, 1);

    float v = uv.y * (256.0 / 64.0);
    float height = lerp(R_GROUND, R_ATMOS, v) - R_GROUND;
    float cosAngle = uv.x * 2.0 - 1.0;

    float3 pos = float3(0, R_GROUND + height, 0);
    float3 dir = float3(sqrt(max(0, 1.0 - cosAngle * cosAngle)), cosAngle, 0);

    float3 opticalDepth = float3(0, 0, 0);
    int steps = 32; // Reduced from 40 for ENB budget
    float rayLen = RayAtmosphereIntersect(pos, dir, R_ATMOS);
    float ds = rayLen / float(steps);

    [loop] for (int i = 0; i < steps; i++) {
        float t = (float(i) + 0.5) * ds;
        float3 sPos = pos + dir * t;
        float h = length(sPos) - R_GROUND;

        float3 rayleigh = BETA_R * exp(-h / H_R);
        float  mie = BETA_M_EXT * exp(-h / H_M);
        opticalDepth += (rayleigh + float3(mie, mie, mie)) * ds;
    }

    return float4(exp(-opticalDepth), 1.0);
}

// Technique declaration:
// technique Technique_TransmittanceLUT
// <string RenderTarget = "RenderTarget256";>
// { pass { PixelShader = compile ps_5_0 PS_TransmittanceLUT(); } }
```

### 6.4 LUT Caching Strategy

The transmittance LUT depends only on atmospheric composition (constant) and can be generated ONCE. The sky-view LUT depends on sun position and should regenerate when the sun moves more than ~0.5 degrees. ENB has no mechanism to conditionally skip a technique pass, so the pragmatic approach is to accept the per-frame cost (~0.19ms for the full pipeline).

### 6.5 Cross-File RT Sharing

LUT render targets are written in enbeffectprepass.fx and read by enbsunsprite.fx (which executes later in the pipeline). This cross-file RT sharing is a standard ENB pattern -- the RTs persist across shader files within a frame.

### 6.6 Quarter-Resolution Proxy for Expensive Effects

Multiple techniques (god rays, volumetric fog) recommend quarter-resolution rendering. ENB cannot create arbitrary render targets but provides fixed-size square targets:

```hlsl
// At 1920x1080: quarter = 480x270, fits in RenderTarget512
// At 2560x1440: quarter = 640x360, fits in RenderTarget1024
// At 3840x2160: quarter = 960x540, fits in RenderTarget1024

// WRITE PASS: Render effect into fixed-size RT
float2 ScreenToFixedRT(float2 screenUV) {
    float screenW = ScreenSize.x;
    float screenH = screenW * ScreenSize.w;
    float quarterW = screenW * 0.25;
    float quarterH = screenH * 0.25;
    float rtSize = QUARTER_RT_SIZE;

    float2 rtUV;
    rtUV.x = screenUV.x * (quarterW / rtSize);
    rtUV.y = screenUV.y * (quarterH / rtSize);
    return rtUV;
}

// READ PASS: Sample from fixed-size RT back to screen space
float2 FixedRTToScreen(float2 screenUV) {
    float screenW = ScreenSize.x;
    float screenH = screenW * ScreenSize.w;
    float quarterW = screenW * 0.25;
    float quarterH = screenH * 0.25;
    float rtSize = QUARTER_RT_SIZE;

    float2 rtUV;
    rtUV.x = screenUV.x * (rtSize / quarterW);
    rtUV.y = screenUV.y * (rtSize / quarterH);
    rtUV = min(rtUV, float2(quarterW / rtSize, quarterH / rtSize));
    return rtUV;
}
```

Resolution tier selection (compile-time):

```hlsl
#define SB_RESOLUTION_TIER 2  // 1=1080p, 2=1440p, 3=4K

#if SB_RESOLUTION_TIER == 1
    #define QUARTER_RT     RenderTarget512
    #define QUARTER_RT_SIZE 512.0
#elif SB_RESOLUTION_TIER == 2
    #define QUARTER_RT     RenderTarget1024
    #define QUARTER_RT_SIZE 1024.0
#else
    #define QUARTER_RT     RenderTarget1024
    #define QUARTER_RT_SIZE 1024.0
#endif
```

---

## 7. Physics That Games Forgot -- New Techniques

This section draws from the "Physics That Games Forgot" research document, identifying atmospheric and optical phenomena achievable within ENB's DX11 pixel shader constraints.

### 7.1 Corona Diffraction (Airy Disk Pattern)

Corona diffraction produces colored concentric rings around the sun or moon when viewed through thin clouds. Caused by Fraunhofer diffraction from near-monodisperse cloud droplets. **Fundamentally different from lens flare** (a camera artifact) -- this is a real atmospheric phenomenon. No game has ever distinguished between the two.

```
I(theta) = I_0 * [2*J1(x)/x]^2
where x = (2*pi*R * sin(theta)) / lambda

For R=10um droplets, the first red ring appears at ~4 degrees from the source.
```

The Airy function `2*J1(x)/x` requires a Bessel function evaluation. Efficient polynomial approximation for pixel shaders:

```hlsl
// ===================================================================
//  Corona Diffraction -- Airy Disk Pattern
//  Physically distinct from lens flare: real atmospheric phenomenon
//  caused by cloud droplet diffraction.
//  Cost: ~0.05ms at 1440p (applied only to near-sun sky pixels)
// ===================================================================

// Polynomial approximation of J1(x) (Abramowitz & Stegun 9.4.4)
float BesselJ1(float x) {
    float ax = abs(x);
    if (ax < 3.0) {
        float y = x * x / 9.0;
        return x * (0.5 - y * (0.56249985 - y * (0.21093573 -
               y * (0.03954289 - y * 0.00443319))));
    } else {
        float y = 3.0 / ax;
        float p = 1.0 + y * y * (-0.00000077 + y * y *
                  (-0.00552740 + y * y * (-0.00009512 + y * y * 0.00137237)));
        float q = -0.04166397 + y * y *
                  (-0.00003954 + y * y * (0.00262573 + y * y *
                  (-0.00054125 - y * y * 0.00029333)));
        float phase = ax - 2.356194491; // ax - 3*pi/4
        return sqrt(0.636619772 / ax) * (p * cos(phase) - y * q * sin(phase))
               * sign(x);
    }
}

// Airy intensity pattern
float AiryDisk(float x) {
    if (abs(x) < 0.001) return 1.0;
    float j1 = BesselJ1(x);
    float val = 2.0 * j1 / x;
    return val * val;
}

float3 CoronaDiffraction(float angularDist, float dropletRadius_um) {
    // Per-channel for chromatic rings
    // lambda in micrometers: R=0.620, G=0.540, B=0.460
    static const float3 lambda_um = float3(0.620, 0.540, 0.460);
    float R_um = dropletRadius_um;

    float3 result;
    float sinTheta = sin(angularDist);

    result.r = AiryDisk(2.0 * 3.14159265 * R_um * sinTheta / lambda_um.r);
    result.g = AiryDisk(2.0 * 3.14159265 * R_um * sinTheta / lambda_um.g);
    result.b = AiryDisk(2.0 * 3.14159265 * R_um * sinTheta / lambda_um.b);

    return result;
}
```

**Trigger conditions**: Thin uniform cloud cover (SB_Weather_Flags.y in [0.2, 0.5]), light precipitation (SB_Precip_Intensity.x in [0.05, 0.2]).

### 7.2 Spectrally Varying God Rays

Current god ray implementations are universally monochromatic despite Rayleigh scattering's lambda^-4 wavelength dependence. Rays should transition from warm white near the sun to progressively blue-purple at greater angular distances.

```hlsl
// ===================================================================
//  Spectrally Varying God Rays
//  Wavelength-dependent extinction along each ray sample.
//  Blue scatters MORE into the ray at large angles from sun,
//  but is also attenuated MORE along the direct path.
// ===================================================================

float3 SpectralGodRayColor(float2 sampleUV, float2 sunUV) {
    float2 delta = sampleUV - sunUV;
    delta.x *= ScreenSize.x / ScreenSize.y;
    float angularDist = length(delta);

    // Near sun: warm (transmitted sun color, Rayleigh-extincted)
    // Far from sun: cool (scattered skylight entering ray path)
    float3 warmColor = float3(1.0, 0.85, 0.6);
    float3 coolColor = float3(0.6, 0.75, 1.0);

    float scatterFraction = smoothstep(0.0, 0.4, angularDist);
    return lerp(warmColor, coolColor, scatterFraction * 0.5);
}
```

Cost: effectively zero -- replaces a constant multiplication with a position-dependent one.

### 7.3 Green Flash Easter Egg

At extremely low sun elevation (< 0.5 deg above geometric horizon), differential refraction produces a brief green flash at the upper rim of the solar disc. Real atmospheric phenomenon caused by the atmosphere acting as a weak prism (Peck-Reeder formula: differential refraction ~0.5 arcminutes at horizon).

```hlsl
// ===================================================================
//  Green Flash -- Optional Easter Egg
//  Triggered at very low sun elevation (last ~0.3 deg before sunset).
//  Enable with preprocessor: #define SB_GREEN_FLASH_ENABLE 1
// ===================================================================

#if SB_GREEN_FLASH_ENABLE
float3 ApplyGreenFlash(float3 discColor, float sunElevation,
                        float limbR, float2 delta) {
    // Only active in the last 0.3 degrees before sunset
    float flashZone = smoothstep(0.006, 0.0, sunElevation);

    // Only the upper rim of the disc
    float upperRim = smoothstep(0.0, -0.3, delta.y / max(length(delta), 0.0001));

    // Only the outer edge of the disc
    float rimZone = smoothstep(0.6, 1.0, limbR);

    float flashIntensity = flashZone * upperRim * rimZone;

    float3 greenShift = float3(-0.3, 0.5, -0.2);
    discColor += greenShift * flashIntensity * 2.0;

    return max(discColor, 0.0);
}
#endif
```

### 7.4 Heiligenschein (Future Consideration)

Bright retroreflective glow around observer's shadow on dew-covered grass. Spherical dew drops (n=4/3) act as cat's-eye retroreflectors with enhancement factor increasing by two orders of magnitude as contact angle increases from 90 to 140 degrees (Fraser 1994). Requires antisolar point computation + grass material identification. **BLOCKED** in ENB (needs G-buffer access); candidate for SkyrimBridge SceneObserver or Community Shaders. Estimated cost if feasible: ~0.2ms.

### 7.5 Fogbow (Future Consideration)

Ghostly white bow in fog where droplets (~10-100um) are too small for geometric optics. Pre-computed Mie scattering lookup table makes this ~0.2ms. Requires antisolar point and fog particle size distribution. Lower priority than core atmospheric effects.

### 7.6 Additional Phenomena from "Physics That Games Forgot"

Several other phenomena documented in the research are relevant but lower priority:

- **Brocken spectre + glory**: Observer's magnified shadow on clouds with Mie backscattering rings. Pre-tabulated Mie patterns make the glory ~0.1ms, but Brocken spectre needs volumetric shadow projection.
- **Bishop's Ring (volcanic)**: Already partially covered in Section 3.5 as optional Bishop's Ring component. Volcanic aerosol version uses larger angular radius.
- **Subsun**: Reflected sun image below horizon from horizontally-oriented ice crystals. Trivial implementation but only relevant in flight/mountain scenarios.
- **Hunt Effect** (colorfulness increasing with luminance): `M = C * F_L^0.25`. Cost: 2-3 ALU. Could be applied as a post-process in enbeffect.fx rather than sunsprite.
- **Helmholtz-Kohlrausch effect**: Saturated colors appearing brighter. Relevant for HDR sun rendering. `L*_EAL = L* + f1(h_ab) * f2(C*_ab)`.

---

## 8. Existing Codebase vs. What Needs Adding

This is the critical section for implementation planning. It catalogs what already exists in the Truth ENB / ENB of the Elders codebase versus what the research proposes.

### 8.1 ALREADY EXISTS in `enbsunsprite.fx`

| Feature | Location | Status | Quality |
|---------|----------|--------|---------|
| Sun screen position | `LightParameters.xy` via ENB | Working | Good -- ENB-native |
| Sun visibility/occlusion | `TextureMask.Load(0,0,0).x * LightParameters.w` | Working | Good |
| Sun color sampling | `GetSunIntensity()` 4-tap cross sample | Working | Good |
| N-gon aperture ghost chain | `PS_Ghost()` + `VS_Ghost()`, 6 ghosts | Working | Good -- blade-aware CA |
| Anamorphic streaks | `PS_Anam()` + `VS_Anam()`, 3 passes | Working | Good -- spectral dispersion |
| Starburst | `PS_Starburst()` + `VS_Starburst()` | Working | Good -- vertex-aligned to blades |
| Sun glare | `PS_SunGlare()` + `VS_SunGlare()` | Working | Basic smoothstep glow |
| Hoop ring | `PS_Hoop()` + `VS_Hoop()` | Working | Basic ring shape |
| Aperture blade system | `ui_ApertureBlades` (4-9), rotation, roundness | Working | Feature-complete |
| Global intensity + theme | `SPRITE_INTENSITY` macro, theme-aware | Working | Good |

### 8.2 ALREADY EXISTS in `Sunsprite_AtmosphericOptics.fxh`

| Feature | Function | Status | Quality vs. Research |
|---------|----------|--------|---------------------|
| Atmospheric aureole | `PS_Aureole()` | Working | **Partial** -- uses raw HG (g=0.76), not Cornette-Shanks. Has dual-layer falloff (core+haze), scintillation, breathing, horizon boost. |
| Ciliary corona | `PS_Corona()` | Working | **Lens effect, not atmospheric** -- renders spectral diffraction rings with fiber detail. This is a ciliary/ocular corona (eyelash diffraction), not atmospheric corona diffraction. |
| Ice crystal halos | `PS_IceHalos()` | Working | **Good** -- 22-deg and 46-deg halos with per-channel Cauchy IOR dispersion. Physically correct minimum deviation angles. |
| Parhelia (sundogs) | `PS_IceHalos()` | Working | **Good** -- Gaussian spots at +/-22 deg with chromatic dispersion. Horizon-dependent intensity. |
| Spectral-to-RGB helper | `AtmosSpectralToRGB()` | Working | Simplified CIE approximation -- adequate |
| IGN noise | `AtmosIGN()` | Working | Standard interleaved gradient noise |
| Moon intensity multiplier | `ui_MoonIntensityMult` | Working | Applied as flat multiplier at night |

### 8.3 WHAT NEEDS ADDING OR UPGRADING

| Feature | Priority | Effort | Notes |
|---------|----------|--------|-------|
| **Wavelength-dependent limb darkening** | HIGH | Low | Replace single-coefficient with Hestroffer power-law. 1-line change. |
| **Kasten-Young airmass** | HIGH | Low | Replace `1/sin(elev)` with K-Y formula. 1-line change. |
| **Cornette-Shanks phase function** | HIGH | Low | Replace raw HG in `PS_Aureole()`. Drop-in replacement. |
| **Multi-component corona** | MEDIUM | Medium | Add Rayleigh sky brightening alongside existing Mie aureole. Bishop's Ring as optional third. |
| **Buie CSR aureole model** | MEDIUM | Medium | Replace dual-exponential with Buie two-piece model driven by SB fog density. |
| **Draine phase function** | LOW | Low | Pre-fitted parameter interpolation. Matters only for large-particle scattering accuracy. |
| **SkyrimBridge integration** | HIGH | Medium | Add `#ifdef SKYRIMBRIDGE_FXH` paths. Currently all ENB-native in atmospheric addon. |
| **Atmospheric extinction pipeline** | HIGH | Medium | Full Beer-Lambert with weather-reactive Mie. Currently simplified `exp(-float3(0.06,0.12,0.30) * airMass)`. |
| **Moon disc rendering** | MEDIUM | Medium | Phase-aware illumination, earthshine, opposition surge. Currently no procedural moon disc. |
| **God rays (cascaded)** | HIGH | High | Full Crytek 3-pass cascade in enbeffectprepass.fx. |
| **Corona diffraction (Airy)** | MEDIUM | Medium | Physically-based atmospheric diffraction. Completely new. |
| **Spectrally varying god rays** | LOW | Low | Trivial modification to any god ray implementation. |
| **Green flash** | LOW | Low | Easter egg. Preprocessor-gated. |
| **Hillaire LUT pipeline** | LOW | High | 4 pixel shader passes. Major architectural addition. |
| **Star field** | LOW | Low | Only if replacing Skyrim's native star rendering. |
| **Opposition surge (moon)** | MEDIUM | Low | Hapke B(alpha). 2 ALU ops. |
| **Procedural diffraction spikes** | MEDIUM | Medium | Upgrade from current starburst. |
| **Multi-source flare** | LOW | High | Requires SkyrimBridge light data or compute-free source finding. |

### 8.4 Key Architectural Gap

The existing `Sunsprite_AtmosphericOptics.fxh` uses **ENB-native data only**:
- Sun position from `LightParameters.xy`
- Sun visibility from `TextureMask`
- Sun elevation approximated from UV: `sinElev = abs(1.0 - sunUV.y * 2.0)`
- No SkyrimBridge integration at all

The research assumes SkyrimBridge provides precise elevation (radians), FOV, weather flags, fog density, precipitation, and moon phase. The `#ifdef SKYRIMBRIDGE_FXH` pattern is not yet present.

### 8.5 What Does NOT Need Changing

- The N-gon ghost chain in `enbsunsprite.fx` is solid -- ABCD matrix ghosts belong in `enblens.fx`.
- The aperture blade system (blades, rotation, roundness) is feature-complete.
- The anamorphic streak system works well.
- The ice crystal halo and parhelia implementations are physically correct.
- The ciliary corona (eyelash diffraction) is a valid perceptual effect and should remain alongside any new atmospheric corona diffraction.

---

## 9. ENB Compatibility Verdicts

Every technique audited against seven hard ENB constraints: no compute shaders, pixel shaders only (full-screen quad), fixed render target pool, 128 technique limit per .fx file, ~4ms total budget at 1440p, SM 5.0 only (FXC/DXBC), and the parameter dead-stripping problem.

**Verdict key:**
- **NATIVE**: Works as-published within ENB architecture, zero modifications.
- **WORKAROUND**: Requires specific adaptation but achieves equivalent visual quality.
- **BLOCKED**: Cannot be implemented in ENB pipeline; alternative provided.

| Technique | Verdict | Notes |
|-----------|---------|-------|
| Wavelength-dependent limb darkening | **NATIVE** | Pure ALU, zero architectural changes |
| Hestroffer power-law | **NATIVE** | `pow()` is SM 5.0 native |
| Kasten-Young airmass | **NATIVE** | Single `pow()` instruction |
| Atmospheric extinction (Beer-Lambert) | **NATIVE** | Pure ALU + `exp()` |
| Cornette-Shanks phase function | **NATIVE** | Drop-in HG replacement |
| Buie CSR aureole | **NATIVE** | Pure ALU with branch |
| Draine phase function | **NATIVE** | Pre-fitted parameters, pure ALU |
| Multi-component corona | **NATIVE** | ALU only, no extra passes |
| Moon disc + opposition surge | **NATIVE** | ALU, tiny pixel coverage |
| God rays (Mitchell) | **NATIVE** | 1 technique, 64 tex fetches |
| God rays (Crytek cascaded) | **WORKAROUND** | 3 techniques ping-ponging RenderTargetRGBA32 |
| Hillaire Transmittance LUT | **WORKAROUND** | PS into RenderTarget256, 32-step march |
| Hillaire Sky-View LUT | **WORKAROUND** | PS into RenderTarget256, runs every frame |
| Hillaire Multi-Scatter LUT | **WORKAROUND** | PS into RenderTarget32 |
| Hillaire Aerial Perspective | **WORKAROUND** | 3D LUT as 2D atlas into RenderTarget1024 |
| Corona diffraction (Airy) | **NATIVE** | Bessel J1 polynomial, pure ALU |
| Spectrally varying god rays | **NATIVE** | Position-dependent color, zero extra cost |
| Green flash | **NATIVE** | Pure ALU, preprocessor-gated |
| Procedural star field | **NATIVE** | Hash-based, no textures |
| Multi-source flare (screen-space) | **WORKAROUND** | Downsample chain, limited to 4 sources |
| Multi-source flare (SB light data) | **NATIVE** | SkyrimBridge provides positions directly |
| Diffraction spikes (procedural) | **NATIVE** | Sum-of-sinc^2 loop |
| Diffraction spikes (atlas) | **NATIVE** | Single texture lookup |
| Heiligenschein | **BLOCKED** | Requires G-buffer grass material ID |
| Fogbow (full Mie LUT) | **WORKAROUND** | Pre-computed 1D Mie texture via TextureCustom |
| Quarter-resolution proxy | **WORKAROUND** | RenderTarget512/1024 with UV scaling |

### 9.1 SB_LITE Mode for Parameter Management

```hlsl
#define SB_LITE 1

#ifdef SKYRIMBRIDGE_FXH
    float4 SB_Celest_SunPosition;
    float4 SB_Celest_MoonPosition;
    float4 SB_Celest_SunColor;
    float4 SB_Atmos_SkyColor;
    float4 SB_Atmos_FogDensity;
    float4 SB_Precip_Intensity;
    float4 SB_Weather_Flags;
    float4 SB_Weather_TransProgress;
    float4 SB_Camera_Direction;
    float4 SB_Camera_Position;
    float4 SB_Render_Frame;
    float4 SB_Shadow_Direction;

    float SB_LiteKeepAlive(float value) {
        return (Timer.x < -99999.0) ?
            (SB_Celest_SunPosition.x + SB_Celest_MoonPosition.x +
             SB_Celest_SunColor.x + SB_Atmos_SkyColor.x +
             SB_Atmos_FogDensity.x + SB_Precip_Intensity.x +
             SB_Weather_Flags.x + SB_Weather_TransProgress.x +
             SB_Camera_Direction.x + SB_Camera_Position.x +
             SB_Render_Frame.x + SB_Shadow_Direction.x) : value;
    }
#endif
```

### 9.2 UI Parameter Consolidation

```hlsl
// 50+ individual floats -> 8 float4s
float4 UI_SunDisc   < string UIName = "Sun: Size|Intensity|LimbStr|EdgeSoft"; >;
float4 UI_Corona    < string UIName = "Corona: MieInt|MieG|RaylInt|RaylExt"; >;
float4 UI_Spikes    < string UIName = "Spikes: Blades|Intensity|Length|Width"; >;
float4 UI_SpikesAdv < string UIName = "Spikes: Asymm|Curve|Chroma|0"; >;
float4 UI_GodRays   < string UIName = "GodRays: Density|Decay|Exposure|Thresh"; >;
float4 UI_Moon      < string UIName = "Moon: Intensity|Earthshine|Size|0"; >;
float4 UI_Stars     < string UIName = "Stars: Density|Intensity|Twinkle|0"; >;
float4 UI_Flare     < string UIName = "Flare: Intensity|GhostCount|Threshold|0"; >;
```

---

## 10. Performance Budget & Pass Allocation

### 10.1 Per-Technique Cost Table

All costs at 1440p on RTX 3060-class GPU:

| Technique | Cost (1080p) | Cost (1440p) | Passes | Shader File |
|-----------|-------------|-------------|--------|-------------|
| Solar disc + limb darkening | ~0.01ms | ~0.01ms | 0 (inline) | enbsunsprite.fx |
| Atmospheric extinction | ~0ms | ~0ms | 0 (inline) | shared |
| Multi-component corona | ~0.04ms | ~0.06ms | 1 | enbsunsprite.fx |
| Diffraction spikes (6 blade) | ~0.05ms | ~0.08ms | 1 | enbsunsprite.fx |
| Moon rendering (2 moons) | ~0.01ms | ~0.02ms | 1 | enbsunsprite.fx |
| Star field | ~0.03ms | ~0.05ms | 1 | enbsunsprite.fx |
| God rays (cascade, half-res) | ~0.12ms | ~0.19ms | 4 | enbeffectprepass.fx |
| Hillaire LUTs | ~0.12ms | ~0.19ms | 4 | enbeffectprepass.fx |
| Bilateral upsample | ~0.03ms | ~0.05ms | 1 | enbeffectprepass.fx |
| **Total** | **~0.41ms** | **~0.65ms** | **13** | |

Budget: 0.65ms at 1440p within 0.8ms allocation. ~0.15ms headroom for Bishop's Ring, aurora, or extra sources.

### 10.2 Technique Pass Budget

| Shader File | Existing | New | Total | Budget | Utilization |
|-------------|---------|-----|-------|--------|-------------|
| enbsunsprite.fx | 15 | 5 | 20 | 128 | 16% |
| enbeffectprepass.fx | ~44 | 9 | 53 | 128 | 41% |

### 10.3 Render Target Lifecycle

```
RT              | Pass Lifecycle
----------------+----------------------------------------------
R16F            | Depth (persistent read-only)
RGB32F          | Normals (passes 1-5 only)
RGBA64F         | AO -> SSR -> Fog (recycled)
R32F            | Blur -> SSR filter
RGBA32          | AO blur -> GodRay cascade (recycled, no overlap)
RGBA64          | SSS only
RenderTarget256 | Transmittance LUT (persist after write)
RenderTarget32  | MultiScatter LUT (persist after write)

No conflicts. LUTs persist across shader files within a frame.
```

---

## 11. Advanced Diffraction Spikes

### 11.1 Physics Background

Diffraction spikes are caused by Fraunhofer diffraction at the camera aperture:

```
N blades produce 2N diffraction orders:
  N even: opposite blades parallel, spikes overlap -> N visible spikes
  N odd:  no parallel pairs -> 2N visible spikes

  5 blades -> 10 spikes    6 blades -> 6 spikes
  7 blades -> 14 spikes    8 blades -> 8 spikes
```

### 11.2 Sinc^2 Diffraction Pattern

```hlsl
float sinc(float x) {
    if (abs(x) < 0.0001) return 1.0;
    float px = 3.14159265 * x;
    return sin(px) / px;
}

float sinc2(float x) {
    float s = sinc(x);
    return s * s;
}
```

### 11.3 Blade Configuration Arrays

```hlsl
struct BladeConfig {
    float angle;
    float brightness;
    float width;
};

static const BladeConfig BLADES_5[5] = {
    { 0.0000, 1.00, 1.00 }, { 1.2566, 0.92, 1.05 },
    { 2.5133, 1.05, 0.95 }, { 3.7699, 0.88, 1.08 },
    { 5.0265, 0.97, 0.98 },
};

static const BladeConfig BLADES_6[6] = {
    { 0.0000, 1.00, 1.00 }, { 1.0472, 0.95, 1.02 },
    { 2.0944, 1.03, 0.97 }, { 3.1416, 0.98, 1.04 },
    { 4.1888, 0.93, 1.01 }, { 5.2360, 1.02, 0.96 },
};

static const BladeConfig BLADES_7[7] = {
    { 0.0000, 1.00, 1.00 }, { 0.8976, 0.94, 1.03 },
    { 1.7952, 1.04, 0.96 }, { 2.6928, 0.91, 1.06 },
    { 3.5904, 1.02, 0.98 }, { 4.4880, 0.96, 1.02 },
    { 5.3856, 0.99, 0.99 },
};

static const BladeConfig BLADES_8[8] = {
    { 0.0000, 1.00, 1.00 }, { 0.7854, 0.96, 1.02 },
    { 1.5708, 1.03, 0.97 }, { 2.3562, 0.94, 1.04 },
    { 3.1416, 0.99, 1.01 }, { 3.9270, 0.95, 1.03 },
    { 4.7124, 1.02, 0.98 }, { 5.4978, 0.97, 1.00 },
};
```

### 11.4 Procedural Sum-of-Sinc^2 with Spectral Sampling

```hlsl
// 4 spectral samples with CIE color matching
static const float4 LAMBDA     = float4(450.0, 530.0, 590.0, 640.0);
static const float3 CIE_XYZ_0 = float3(0.336, 0.038, 1.772); // 450nm blue
static const float3 CIE_XYZ_1 = float3(0.165, 0.862, 0.042); // 530nm green
static const float3 CIE_XYZ_2 = float3(1.026, 0.757, 0.002); // 590nm orange
static const float3 CIE_XYZ_3 = float3(0.283, 0.107, 0.000); // 640nm red

float3 XYZtoLinearRGB(float3 xyz) {
    return float3(
        dot(xyz, float3( 3.2406, -1.5372, -0.4986)),
        dot(xyz, float3(-0.9689,  1.8758,  0.0415)),
        dot(xyz, float3( 0.0557, -0.2040,  1.0570))
    );
}

float3 ComputeDiffractionSpikes_Procedural(
    float2 pixelOffset, float sourceBright, float cameraRoll
) {
    float dist = length(pixelOffset);
    if (dist < 0.001 || dist > SPIKE_Length * 2.0)
        return float3(0, 0, 0);

    float2 dir = pixelOffset / dist;
    int nBlades = clamp(SPIKE_BladeCount, 5, 8);

    float3 totalXYZ = float3(0, 0, 0);
    float  totalWeight = 0.0;

    [loop] for (int b = 0; b < 8; b++) {
        if (b >= nBlades) break;

        float bladeAngle, bladeBright, bladeWidth;

        [branch] if (nBlades == 5) {
            bladeAngle = BLADES_5[b].angle;
            bladeBright = BLADES_5[b].brightness;
            bladeWidth = BLADES_5[b].width;
        } else if (nBlades == 6) {
            bladeAngle = BLADES_6[b].angle;
            bladeBright = BLADES_6[b].brightness;
            bladeWidth = BLADES_6[b].width;
        } else if (nBlades == 7) {
            bladeAngle = BLADES_7[b].angle;
            bladeBright = BLADES_7[b].brightness;
            bladeWidth = BLADES_7[b].width;
        } else {
            bladeAngle = BLADES_8[b].angle;
            bladeBright = BLADES_8[b].brightness;
            bladeWidth = BLADES_8[b].width;
        }

        bladeAngle += cameraRoll;
        bladeBright = lerp(1.0, bladeBright, SPIKE_Asymmetry / 0.05);

        float spikeAngle = bladeAngle + 1.5707963;
        float2 spikeDir = float2(cos(spikeAngle), sin(spikeAngle));
        float2 bladeDir = float2(cos(bladeAngle), sin(bladeAngle));

        float alongSpike = dot(pixelOffset, spikeDir);
        float acrossBlade = abs(dot(pixelOffset, bladeDir));

        float spikeEnvelope = exp(-abs(alongSpike) /
                              max(SPIKE_Length * sourceBright, 0.001));

        float widthFactor = SPIKE_Width * bladeWidth;
        widthFactor *= (1.0 + SPIKE_Curvature * 2.0);

        [unroll] for (int w = 0; w < 4; w++) {
            float lambdaScale = LAMBDA[w] / 550.0;
            float crossArg = acrossBlade * 80.0 /
                             (widthFactor * lambdaScale);
            float crossPattern = sinc2(crossArg);

            float chromaShift = (lambdaScale - 1.0) * SPIKE_ChromaStrength *
                                dist * 5.0;
            float shiftedAlong = abs(alongSpike) + chromaShift;
            float shiftedEnvelope = exp(-shiftedAlong /
                                    max(SPIKE_Length * sourceBright, 0.001));

            float intensity = crossPattern * shiftedEnvelope * bladeBright;

            float3 cie;
            if (w == 0) cie = CIE_XYZ_0;
            else if (w == 1) cie = CIE_XYZ_1;
            else if (w == 2) cie = CIE_XYZ_2;
            else cie = CIE_XYZ_3;

            totalXYZ += cie * intensity;
            totalWeight += intensity;
        }
    }

    float3 spikeColor = XYZtoLinearRGB(totalXYZ);
    spikeColor = max(spikeColor, 0.0);

    float3 sunTint = float3(1.0, 0.95, 0.85);
    spikeColor = lerp(sunTint * (totalWeight / max(float(nBlades) * 4.0, 1.0)),
                      spikeColor,
                      SPIKE_ChromaStrength);

    return spikeColor * SPIKE_Intensity;
}
```

### 11.5 Precomputed Aperture Atlas Alternative

```hlsl
// 512x512 texture: 4 quadrants for 5/6/7/8 blade patterns
// Constant per-pixel cost regardless of blade count.

float3 ComputeDiffractionSpikes_Atlas(
    float2 pixelOffset, float sourceBright,
    float cameraRoll, int bladeCount
) {
    float dist = length(pixelOffset);
    if (dist < 0.001 || dist > SPIKE_Length * 2.0)
        return float3(0, 0, 0);

    float angle = atan2(pixelOffset.y, pixelOffset.x) - cameraRoll;
    angle = frac(angle / (2.0 * 3.14159265));
    float logDist = saturate(log(1.0 + dist * 10.0) / 3.0);

    float2 quadOffset;
    if (bladeCount == 5)      quadOffset = float2(0.0, 0.0);
    else if (bladeCount == 6) quadOffset = float2(0.5, 0.0);
    else if (bladeCount == 7) quadOffset = float2(0.0, 0.5);
    else                       quadOffset = float2(0.5, 0.5);

    float2 atlasUV = quadOffset + float2(angle, logDist) * 0.5;
    float3 pattern = TextureCustom01.SampleLevel(sLinear, atlasUV, 0).rgb;
    float envelope = exp(-dist / max(SPIKE_Length * sourceBright, 0.001));

    return pattern * envelope * SPIKE_Intensity * sourceBright;
}

// Procedural (8 blades, 4 wavelengths): ~0.08ms at 1440p
// Atlas lookup: ~0.03ms at 1440p (constant)
// Use atlas for >= 7 blades, procedural for <= 6
```

### 11.6 Camera Roll & Blade Curvature

```hlsl
float GetCameraRoll() {
    #ifdef SKYRIMBRIDGE_FXH
    if (SB_IsActive()) {
        return SB_Camera_Direction.w;
    }
    #endif
    return 0.0;
}

// Curved blades soften spikes: 0=straight (sinc^2), 1=circular (no spikes)
float CurvedBladeDiffraction(float crossDist, float widthFactor, float curvature) {
    float sincResult = sinc2(crossDist * 80.0 / widthFactor);
    float gaussResult = exp(-crossDist * crossDist * 5000.0 /
                            (widthFactor * widthFactor));
    return lerp(sincResult, gaussResult, curvature);
}
```

---

## 12. Multi-Source Light Flare

### 12.1 Problem

The sun is not the only bright source. Torches, campfires, magical effects, and forges can warrant flare. Constraints: identify sources efficiently, limit to 3-5 for performance, depth-test each, generate per-source ghost chains in a single pass.

### 12.2 SkyrimBridge Light Data (Preferred)

```hlsl
struct LightFlareSource {
    float2 screenUV;
    float3 color;
    float  intensity;
    float  distance;
    bool   occluded;
    bool   valid;
};
// SkyrimBridge Light Tracker provides world-space position and color
// for nearest point lights, eliminating screen-space source finding.
```

### 12.3 Per-Source Flare Chain

```hlsl
float3 PerSourceFlare(
    float2 uv, float2 sourceUV, float3 sourceColor,
    float sourceIntensity, float sourceDistance
) {
    float3 result = float3(0, 0, 0);
    float distAtten = 1.0 / (1.0 + sourceDistance * sourceDistance * 0.001);
    float2 flareAxis = sourceUV - float2(0.5, 0.5);

    // Halo
    float2 delta = uv - sourceUV;
    delta.x *= ScreenSize.x / ScreenSize.y;
    float haloDist = length(delta);
    result += sourceColor * exp(-haloDist * 20.0) * 0.3 * distAtten * sourceIntensity;

    // 3 ghost reflections
    static const float GHOST_OFFSETS[3] = { -0.3, 0.5, 1.2 };
    static const float GHOST_SCALES[3]  = { 0.5, 0.3, 0.15 };
    static const float3 GHOST_TINTS[3]  = {
        float3(1.0, 0.8, 0.6), float3(0.6, 0.8, 1.0), float3(0.8, 1.0, 0.7),
    };

    [unroll] for (int g = 0; g < 3; g++) {
        float2 ghostUV = float2(0.5, 0.5) + flareAxis * GHOST_OFFSETS[g];
        float2 ghostDelta = uv - ghostUV;
        ghostDelta.x *= ScreenSize.x / ScreenSize.y;
        float ghostDist = length(ghostDelta);
        float ghostMask = exp(-ghostDist * ghostDist /
                              (GHOST_SCALES[g] * GHOST_SCALES[g] * 0.01));
        result += sourceColor * GHOST_TINTS[g] * ghostMask *
                  distAtten * sourceIntensity * 0.1;
    }

    // 4-spike starburst
    float2 spikeDir = (haloDist > 0.001) ? normalize(delta) : float2(1, 0);
    float spike = 0.0;
    [unroll] for (int s = 0; s < 4; s++) {
        float a = float(s) * 0.7854;
        float2 sa = float2(cos(a), sin(a));
        spike += pow(abs(dot(spikeDir, sa)), 40.0) * exp(-haloDist * 8.0);
    }
    result += sourceColor * spike * distAtten * sourceIntensity * 0.15;

    return result;
}
```

### 12.4 Depth-Test Occlusion

```hlsl
float LightOcclusionTest(float2 sourceUV, float sourceWorldDist) {
    float visible = 0.0;
    float totalSamples = 0.0;
    float2 texelSize = 1.0 / ScreenSize.xy;

    [unroll] for (int x = -1; x <= 1; x++) {
        [unroll] for (int y = -1; y <= 1; y++) {
            float2 sampleUV = sourceUV + float2(x, y) * texelSize * 3.0;
            if (any(sampleUV < 0.0) || any(sampleUV > 1.0)) continue;

            float depth = TextureDepth.SampleLevel(PointClampSampler,
                          sampleUV, 0).x;
            float worldZ = LinearizeDepth(depth);

            visible += step(sourceWorldDist * 0.9, worldZ);
            totalSamples += 1.0;
        }
    }

    return visible / max(totalSamples, 1.0);
}
```

### 12.5 Performance

Budget: <= 0.3ms at 1440p for 5 sources with SkyrimBridge light data. Screen-space source identification adds overhead; limit to 3 sources without SB.

---

## 13. Academic References

### Solar & Atmospheric Physics

[1] Neckel, H. & Labs, D. (1994). "Solar Limb Darkening 1986-1990." *Solar Physics*, 153, 91-114.

[2] Pierce, A.K. (2000). "Solar Limb Darkening." In Cox, A.N. (ed.), *Allen's Astrophysical Quantities*, 4th ed. Springer.

[3] Hestroffer, D. & Magnan, C. (1998). "Wavelength dependency of the Solar limb darkening." *Astronomy and Astrophysics*, 333, 338-342.

[4] Kopal, Z. (1950). "Detailed effects of limb darkening upon light and velocity curves of close binary systems." *Harvard College Observatory Circular*, 454, 1-12.

[5] Claret, A. & Bloemen, S. (2011). "Gravity and limb-darkening coefficients for the Kepler, CoRoT, Spitzer, uvby, UBVRIJHK, and Sloan photometric systems." *A&A*, 529, A75.

[6] Kasten, F. & Young, A.T. (1989). "Revised optical air mass tables and approximation formula." *Applied Optics*, 28(22), 4735-4738.

### Atmospheric Scattering

[7] Nishita, T., Sirai, T., Tadamura, K., & Nakamae, E. (1993). "Display of The Earth Taking into Account Atmospheric Scattering." *SIGGRAPH 1993*.

[8] Bruneton, E. & Neyret, F. (2008). "Precomputed Atmospheric Scattering." *Computer Graphics Forum*, 27(4), 1079-1086.

[9] Hillaire, S. (2020). "A Scalable and Production Ready Sky and Atmosphere Rendering Technique." *Computer Graphics Forum*.

[10] Buie, D. (2003). "The effective size of the solar cone for solar concentrating systems." *Solar Energy*, 74, 417-427.

### Phase Functions

[11] Henyey, L.G. & Greenstein, J.L. (1941). "Diffuse radiation in the galaxy." *Astrophysical Journal*, 93, 70-83.

[12] Cornette, W.M. & Shanks, J.G. (1992). "Physically reasonable analytic expression for the single-scattering phase function." *Applied Optics*, 31(16), 3152-3160.

[13] Schlick, C. (1993). "A Survey of Shading and Reflectance Models." *Computer Graphics Forum*, 13(2), 11-29.

[14] Jendersie, S. & d'Eon, E. (2023). "An Approximate Mie Scattering Function for Fog and Cloud Rendering." *SIGGRAPH 2023*.

[15] Draine, B.T. (2003). "Scattering by Interstellar Dust Grains." *The Astrophysical Journal*, 598, 1017-1025.

### God Rays & Volumetric Lighting

[16] Mitchell, J. (2007). "Volumetric Light Scattering as a Post-Process." *GPU Gems 3*, Chapter 13. Addison-Wesley.

[17] Engelhardt, T. & Dachsbacher, C. (2010). "Epipolar Sampling for Shadows and Crepuscular Rays in Participating Media with Single Scattering." *Proc. I3D*.

[18] Wronski, B. (2014). "Volumetric Fog: Unified Compute Shader Based Solution." *SIGGRAPH Course*, Frostbite Engine.

### Lunar Physics

[19] Hapke, B. (1986). "Bidirectional reflectance spectroscopy. 4. The extinction coefficient and the opposition effect." *Icarus*, 67, 264-280.

[20] Hapke, B. (2002). "Bidirectional Reflectance Spectroscopy. 5. The Coherent Backscatter Opposition Effect and Anisotropic Scattering." *Icarus*, 157, 523-534.

### Lens & Diffraction Physics

[21] Hullin, M.B., Eisemann, E., Seidel, H.-P., & Lee, S. (2011). "Physically-Based Real-Time Lens Flare Rendering." *ACM TOG*, 30(4).

[22] Lee, S. & Eisemann, E. (2013). "Practical Real-Time Lens-Flare Rendering." *Computer Graphics Forum*, 32(4), 1-6.

[23] Spencer, G., Shirley, P., Zimmerman, K., & Greenberg, D.P. (1995). "Physically-Based Glare Effects for Digital Images." *SIGGRAPH 1995*.

### Atmospheric Optics (Physics That Games Forgot)

[24] Fraser, A.B. (1994). "The sylvanshine: retroreflection from dew-covered trees." *Applied Optics*, 33(21), 4652-4655.

[25] Laven, P. (2005). "Atmospheric glories: simulations and observations." *Applied Optics*, 44(27), 5667-5674.

[26] Peck, E.R. & Reeder, K. (1972). "Dispersion of Air." *Journal of the Optical Society of America*, 62(8), 958-962.

[27] Steinberg, N. et al. (2024). "Generalized Ray for Wave-Optical Rendering." *SIGGRAPH Asia 2024*.

### ENB & Game Modding

[28] kingeric1992. enbsunsprite.fx -- Sprite-based sun rendering framework for ENB.

[29] Pilgrim ENB (Kitsuune/l00ping/TreyM). ParmLink weather-reactive parameter middleware.

[30] NAT ENB. Atmospheric scattering and sun rendering techniques for Skyrim SE ENB.

[31] Snapdragon ENB (Prod80). Sun and lens effect implementations for Skyrim SE.

### GPU Architecture & Color Science

[32] Jimenez, J. (2014). "Next Generation Post Processing in Call of Duty: Advanced Warfare." *SIGGRAPH Advances in Real-Time Rendering*.

[33] Young, A.T. (1994). "Air mass and refraction." *Applied Optics*, 33(6), 1108-1110.

[34] Vos, J.J. (2003). "Reflections on glare." *Lighting Research and Technology*, 35(2), 163-176.
