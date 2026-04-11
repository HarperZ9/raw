//=============================================================================
//  Sunsprite_AtmosphericOptics.fxh — Atmospheric Optics Suite
//
//  9 passes for enbsunsprite.fx:
//    Atmospheric Aureole (Mie forward-scatter + Rayleigh extinction)
//    Ciliary Corona (spectral diffraction rings + fiber detail)
//    Ice Crystal Halos (22deg + 46deg with chromatic dispersion)
//    Parhelia / Sundogs (bright spots at +/-22deg horizontal)
//    Heiligenschein (antisolar dew-drop retroreflection)
//    Procedural Star Field (hash-grid + scintillation + spectral color)
//    Light Pillar (vertical ice crystal columns)
//    Belt of Venus + Earth Shadow (anti-twilight phenomena)
//    Green Flash (differential refraction at sunrise/sunset)
//    Atmospheric Disc Refraction (horizon flattening)
//
//  All features disabled by default.
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef SUNSPRITE_ATMOSPHERIC_OPTICS_FXH
#define SUNSPRITE_ATMOSPHERIC_OPTICS_FXH


//=== UI PARAMETERS ===//

// --- Atmospheric Aureole ---

bool ui_AurEnable
<
    string UIName = "ATMOS | Aureole Enable";
> = {false};

float ui_AurIntensity
<
    string UIName = "ATMOS | Aureole Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01;
> = {1.0};

float ui_AurCoreScale
<
    string UIName = "ATMOS | Aureole Core Scale";
    string UIWidget = "Spinner";
    float UIMin = 0.01; float UIMax = 2.0; float UIStep = 0.01;
> = {0.5};

float ui_AurCoreFalloff
<
    string UIName = "ATMOS | Aureole Core Falloff";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 20.0; float UIStep = 0.1;
> = {8.0};

float ui_AurHazeScale
<
    string UIName = "ATMOS | Aureole Haze Scale";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.01;
> = {2.0};

float ui_AurHazeFalloff
<
    string UIName = "ATMOS | Aureole Haze Falloff";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 10.0; float UIStep = 0.1;
> = {3.0};

float ui_AurExtinction
<
    string UIName = "ATMOS | Rayleigh Extinction";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {1.0};

float ui_AurWarmth
<
    string UIName = "ATMOS | Aureole Warmth";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_AurScintillation
<
    string UIName = "ATMOS | Scintillation";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.5; float UIStep = 0.01;
> = {0.1};

float ui_AurBreathing
<
    string UIName = "ATMOS | Breathing Amount";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.3; float UIStep = 0.01;
> = {0.05};

float ui_AurHorizonBoost
<
    string UIName = "ATMOS | Horizon Boost";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 5.0; float UIStep = 0.1;
> = {1.5};

int ui_AurPhaseMode
<
    string UIName = "ATMOS | Phase (0=Cornette 1=Draine)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 1;
> = {0};

float ui_AurMieMultiplier
<
    string UIName = "ATMOS | Mie Multiplier (haze level)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 10.0; float UIStep = 0.1;
> = {1.0};

// --- Ciliary Corona ---

bool ui_CorEnable
<
    string UIName = "ATMOS | Corona Enable";
> = {false};

float ui_CorIntensity
<
    string UIName = "ATMOS | Corona Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.5};

int ui_CorRingCount
<
    string UIName = "ATMOS | Corona Ring Count";
    string UIWidget = "Spinner";
    int UIMin = 1; int UIMax = 20;
> = {8};

float ui_CorSharpness
<
    string UIName = "ATMOS | Corona Ring Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 8.0; float UIStep = 0.1;
> = {3.0};

float ui_CorSpectralSpread
<
    string UIName = "ATMOS | Corona Spectral Spread";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.3; float UIStep = 0.01;
> = {0.1};

int ui_CorFiberFreq
<
    string UIName = "ATMOS | Corona Fiber Frequency";
    string UIWidget = "Spinner";
    int UIMin = 10; int UIMax = 300;
> = {80};

float ui_CorFiberDetail
<
    string UIName = "ATMOS | Corona Fiber Detail";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_CorScale
<
    string UIName = "ATMOS | Corona Scale";
    string UIWidget = "Spinner";
    float UIMin = 0.05; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

// --- 22-Degree Halo ---

bool ui_Halo22Enable
<
    string UIName = "ATMOS | 22deg Halo Enable";
> = {false};

float ui_Halo22Intensity
<
    string UIName = "ATMOS | 22deg Halo Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.4};

float ui_Halo22Thickness
<
    string UIName = "ATMOS | 22deg Halo Thickness";
    string UIWidget = "Spinner";
    float UIMin = 0.005; float UIMax = 0.1; float UIStep = 0.001;
> = {0.02};

float ui_Halo22InnerEdge
<
    string UIName = "ATMOS | 22deg Inner Edge Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 5.0; float UIStep = 0.1;
> = {2.0};

// --- 46-Degree Halo ---

bool ui_Halo46Enable
<
    string UIName = "ATMOS | 46deg Halo Enable";
> = {false};

float ui_Halo46Intensity
<
    string UIName = "ATMOS | 46deg Halo Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.2};

// --- Parhelia (Sundogs) ---

bool ui_ParheliaEnable
<
    string UIName = "ATMOS | Parhelia (Sundogs) Enable";
> = {false};

float ui_ParheliaIntensity
<
    string UIName = "ATMOS | Parhelia Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.5};

float ui_ParheliaSize
<
    string UIName = "ATMOS | Parhelia Size";
    string UIWidget = "Spinner";
    float UIMin = 0.005; float UIMax = 0.1; float UIStep = 0.001;
> = {0.02};

float ui_ParheliaDispersion
<
    string UIName = "ATMOS | Parhelia Chromatic Dispersion";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

// --- Heiligenschein ---

bool ui_HeiligEnable
<
    string UIName = "ATMOS | Heiligenschein Enable";
> = {false};

float ui_HeiligIntensity
<
    string UIName = "ATMOS | Heiligenschein Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.3};

float ui_HeiligRadius
<
    string UIName = "ATMOS | Heiligenschein Radius";
    string UIWidget = "Spinner";
    float UIMin = 0.01; float UIMax = 0.3; float UIStep = 0.005;
> = {0.08};

// --- Star Field ---

bool ui_StarEnable
<
    string UIName = "ATMOS | Star Field Enable";
> = {false};

float ui_StarDensity
<
    string UIName = "ATMOS | Star Density";
    string UIWidget = "Spinner";
    float UIMin = 50.0; float UIMax = 500.0; float UIStep = 10.0;
> = {200.0};

float ui_StarIntensity
<
    string UIName = "ATMOS | Star Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.5};

float ui_StarScintillation
<
    string UIName = "ATMOS | Star Twinkle";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.4};

float ui_StarColorVariation
<
    string UIName = "ATMOS | Star Color Variation";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

// --- Moon Multipliers ---

float ui_MoonIntensityMult
<
    string UIName = "ATMOS | Moon Intensity Multiplier";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.1;
> = {2.0};

float ui_EarthshineIntensity
<
    string UIName = "ATMOS | Earthshine Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.15};


//=== CONSTANTS ===//

// Ice refractive indices per-channel (Cauchy equation fit for visible light)
// Red: n=1.3066, Green: n=1.3098, Blue: n=1.3167
static const float3 ICE_IOR = float3(1.3066, 1.3098, 1.3167);

// Hestroffer limb darkening exponents (solar disc, per-channel)
// Based on Hestroffer & Magnan (1998) power-law fit to solar observations.
// Blue darkens more at the limb because shorter wavelengths originate
// from higher (cooler) photospheric layers when viewed at grazing angles.
static const float3 HESTROFFER_ALPHA = float3(0.429, 0.530, 0.621);


//=== HELPER FUNCTIONS ===//

// Interleaved Gradient Noise — best screen-space noise, no texture needed
float AtmosIGN(float2 px)
{
    return frac(52.9829189 * frac(dot(px, float2(0.06711056, 0.00583715))));
}

// Spectral wavelength -> RGB (simplified CIE 1931 color matching approximation)
float3 AtmosSpectralToRGB(float t) // t in [0,1] maps across visible spectrum
{
    t = saturate(t);
    float3 c;
    c.r = smoothstep(0.0, 0.2, t) * (1.0 - smoothstep(0.6, 1.0, t));
    c.g = smoothstep(0.1, 0.4, t) * (1.0 - smoothstep(0.7, 1.0, t));
    c.b = (1.0 - smoothstep(0.0, 0.5, t));
    float len = length(c) + 0.001;
    return c / len * len; // preserve zero
}

// Cornette-Shanks phase function — properly normalized Mie scattering.
// Improvement over Henyey-Greenstein: includes (1+cos^2) Rayleigh-like term
// and correct normalization factor 3/(8pi) * (1-g^2)/(2+g^2).
// Reference: Cornette & Shanks (1992), Applied Optics 31(16).
float CornetteShanks(float cosTheta, float g)
{
    float g2 = g * g;
    float num = 1.5 * (1.0 - g2) * (1.0 + cosTheta * cosTheta);
    float denom = (2.0 + g2) * pow(1.0 + g2 - 2.0 * g * cosTheta, 1.5);
    return num / max(denom, 0.0001);
}

// Hestroffer power-law limb darkening for the solar disc.
// cosTheta: cos of angle between surface normal and line of sight (1.0 at center, 0.0 at limb).
// Returns per-channel darkening factor.
float3 HestrofferLimbDarkening(float cosTheta)
{
    return pow(max(cosTheta, 0.0), HESTROFFER_ALPHA);
}

// Hapke opposition surge for the Moon (Hapke 1986, Icarus 67).
// At exact opposition (full moon, alpha=0), the Moon shows a sharp brightness surge
// due to shadow hiding. B0 = surge amplitude, h = angular width (radians).
float HapkeOppositionSurge(float phaseAngle, float B0, float h)
{
    return 1.0 + B0 / (1.0 + tan(phaseAngle * 0.5) / max(h, 0.001));
}

// Draine phase function (Jendersie & d'Eon, SIGGRAPH 2023)
// Improvement over HG/Cornette-Shanks: models real aerosol scattering patterns
// with per-droplet-size angular dependence. Pre-fitted for 5 weather conditions.
struct DraineParams
{
    float g;       // HG asymmetry
    float alpha;   // Draine angular dependence
    float weight;  // Blend weight with pure HG
};

static const DraineParams DRAINE_CLEAR = { 0.76, 0.15, 0.3 };
static const DraineParams DRAINE_HAZY  = { 0.82, 0.25, 0.4 };
static const DraineParams DRAINE_FOGGY = { 0.88, 0.45, 0.6 };
static const DraineParams DRAINE_RAIN  = { 0.92, 0.60, 0.7 };

DraineParams GetDraineForWeather(float mieMultiplier)
{
    DraineParams result;
    if (mieMultiplier < 2.0)
    {
        float t = mieMultiplier / 2.0;
        result.g      = lerp(DRAINE_CLEAR.g,     DRAINE_HAZY.g,     t);
        result.alpha  = lerp(DRAINE_CLEAR.alpha,  DRAINE_HAZY.alpha, t);
        result.weight = lerp(DRAINE_CLEAR.weight, DRAINE_HAZY.weight, t);
    }
    else if (mieMultiplier < 6.0)
    {
        float t = (mieMultiplier - 2.0) / 4.0;
        result.g      = lerp(DRAINE_HAZY.g,      DRAINE_FOGGY.g,      t);
        result.alpha  = lerp(DRAINE_HAZY.alpha,   DRAINE_FOGGY.alpha,  t);
        result.weight = lerp(DRAINE_HAZY.weight,  DRAINE_FOGGY.weight, t);
    }
    else
    {
        float t = saturate((mieMultiplier - 6.0) / 4.0);
        result.g      = lerp(DRAINE_FOGGY.g,      DRAINE_RAIN.g,      t);
        result.alpha  = lerp(DRAINE_FOGGY.alpha,   DRAINE_RAIN.alpha,  t);
        result.weight = lerp(DRAINE_FOGGY.weight,  DRAINE_RAIN.weight, t);
    }
    return result;
}

float DrainePhase(float cosTheta, DraineParams dp)
{
    float g2 = dp.g * dp.g;
    float denom = pow(abs(1.0 + g2 - 2.0 * dp.g * cosTheta), 1.5);
    float hg = (1.0 - g2) / (4.0 * 3.14159265 * max(denom, 0.0001));
    float draine_factor = (1.0 + dp.alpha * cosTheta * cosTheta)
                        / (1.0 + dp.alpha * (1.0 + 2.0 * g2) / 3.0);
    float draine = hg * draine_factor;
    return lerp(hg, draine, dp.weight);
}

// Buie sunshape: disc region + aureole region (Buie 2003)
// CSR (Circumsolar Ratio) parametrizes aureole extent.
// theta_mrad: angular distance from sun center in milliradians
float BuieSunshape(float theta_mrad, float CSR)
{
    if (theta_mrad <= 4.65)
    {
        // Disc region
        return cos(0.326 * theta_mrad) / max(cos(0.308 * theta_mrad), 0.01);
    }
    else
    {
        // Aureole region — exponential falloff parameterized by CSR
        float kappa = 2.2 * log(max(0.52 * CSR, 0.001)) + 0.1;
        float gamma = -0.3 + 0.9 * log(max(13.5 * CSR, 0.001));
        return exp(kappa) * pow(theta_mrad, gamma);
    }
}

// Get sun screen position from ENB LightParameters
float2 AtmosGetSunUV()
{
    return LightParameters.xy * float2(0.5, -0.5) + 0.5;
}

// Get sun visibility (mask × parameter)
float AtmosGetSunVis()
{
    float mask = TextureMask.Load(int3(0, 0, 0)).x;
    return saturate(mask * LightParameters.w);
}


//=== PIXEL SHADERS ===//

// Pass: Atmospheric Aureole — Mie forward-scatter with Rayleigh extinction
// Output is ADDITIVE (enbsunsprite.fx forces additive blending on all passes)
float4 PS_Aureole(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    if (!ui_AurEnable) return float4(0, 0, 0, 0);

    float sunVis = AtmosGetSunVis();
    if (sunVis < 0.001) return float4(0, 0, 0, 0);

    float2 sunUV = AtmosGetSunUV();
    float2 delta = (txcoord - sunUV) * float2(ScreenSize.z, 1.0); // aspect correct
    float dist = length(delta);

    // Phase function selection
    float cosTheta = saturate(1.0 - dist * 2.0);
    float miePhase;

    if (ui_AurPhaseMode == 1)
    {
        // Draine phase function (Jendersie & d'Eon, SIGGRAPH 2023)
        // Weather-reactive: mieMultiplier selects pre-fitted aerosol parameters
        DraineParams dp = GetDraineForWeather(ui_AurMieMultiplier);
        miePhase = DrainePhase(cosTheta, dp);
    }
    else
    {
        // Cornette-Shanks (default) — properly normalized Mie with (1+cos²) term
        float g = 0.76;
        miePhase = CornetteShanks(cosTheta, g);
    }

    // Dual-layer falloff: core (tight, bright) + haze (wide, dim)
    float core = exp(-dist * ui_AurCoreFalloff / max(ui_AurCoreScale, 0.01)) * 1.5;
    float haze = exp(-dist * ui_AurHazeFalloff / max(ui_AurHazeScale, 0.01)) * 0.5;
    float aureole = (core + haze) * miePhase;

    // Rayleigh extinction — physically models atmospheric path length
    // Sun elevation from LightParameters (approximate from y position)
    float sinElev = max(abs(1.0 - sunUV.y * 2.0), 0.05);
    // Kasten & Young (1989) airmass formula — finite at horizon (max ~38)
    // vs naive 1/sin which diverges to infinity. Uses zenith angle in degrees.
    float elevDeg = degrees(asin(sinElev));
    float airMass = 1.0 / (sinElev + 0.50572 * pow(max(elevDeg + 6.07995, 0.1), -1.6364));
    // Rayleigh coefficients: blue >> green >> red (blue scatters away)
    float3 extinction = exp(-float3(0.06, 0.12, 0.30) * airMass * ui_AurExtinction);

    // Warmth tinting (surviving light after Rayleigh is warm)
    float3 warmTint = lerp(float3(1, 1, 1), float3(1.0, 0.85, 0.6), ui_AurWarmth);

    // Scintillation — IGN temporal noise for per-sector shimmer
    float angle = atan2(delta.y, delta.x + 0.0001);
    float sector = floor(angle * 40.0 / 6.283);
    float scintNoise = AtmosIGN(float2(sector, Timer.x * 3.7 + sector * 0.1));
    float scintillation = 1.0 + ui_AurScintillation * (scintNoise - 0.5);

    // Breathing animation
    float breath = 1.0 + ui_AurBreathing * sin(Timer.x * 0.3);

    // Horizon boost (amplify when sun near horizon — more scattering)
    float horizonBoost = lerp(1.0, ui_AurHorizonBoost, 1.0 - sinElev);

    // Moon factor: amplify at night with Hapke opposition surge
    float nightFactor = 1.0 - ENightDayFactor;
    float moonBoost = lerp(1.0, ui_MoonIntensityMult, nightFactor);
    // Approximate lunar phase angle from ENightDayFactor (0=full night/opposition, 1=day)
    // At full moon (opposition), phase angle ~ 0; at new moon, ~ pi
    if (nightFactor > 0.5)
    {
        float phaseAngle = nightFactor * 0.6; // ~0 at deep night, ~0.3 at dusk
        float surge = HapkeOppositionSurge(phaseAngle, 1.3, 0.06);
        moonBoost *= surge;
    }

    float3 result = aureole * extinction * warmTint * scintillation
                  * breath * horizonBoost * sunVis * moonBoost * ui_AurIntensity;

    // Earthshine: sunlight reflected off Earth onto Moon's dark limb.
    // Visible when Moon is crescent (not full). Blue-grey tint from Earth's
    // atmospheric Rayleigh scattering. Ref: Goode et al. 2001, Pallé et al. 2003.
    if (nightFactor > 0.3 && ui_EarthshineIntensity > 0.001)
    {
        // Crescent factor: earthshine strongest when Moon is thin crescent
        // (ENightDayFactor near 0 = full night ~ full moon, near 0.5 = half moon)
        float crescentPhase = saturate(nightFactor * 2.0 - 0.6);
        float earthshine = crescentPhase * exp(-dist * 6.0) * ui_EarthshineIntensity;
        // Earth-reflected light is blue-grey (Rayleigh-filtered sunlight)
        result += float3(0.6, 0.7, 1.0) * earthshine * sunVis;
    }

    return float4(max(result, 0.0), 1.0);
}

// Pass: Ciliary Corona — spectral diffraction rings with fiber detail
// Output is ADDITIVE
float4 PS_Corona(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    if (!ui_CorEnable) return float4(0, 0, 0, 0);

    float sunVis = AtmosGetSunVis();
    if (sunVis < 0.001) return float4(0, 0, 0, 0);

    float2 sunUV = AtmosGetSunUV();
    float2 delta = (txcoord - sunUV) * float2(ScreenSize.z, 1.0);
    float dist = length(delta);
    float normDist = dist / max(ui_CorScale, 0.001);

    // Spectral ring pattern — 3 channels at offset frequencies
    // Physical: different wavelengths diffract at different angles
    float baseFreq = (float)ui_CorRingCount * PI;
    float3 rings;
    rings.r = pow(abs(sin(normDist * baseFreq * (1.0 - ui_CorSpectralSpread))), ui_CorSharpness);
    rings.g = pow(abs(sin(normDist * baseFreq)), ui_CorSharpness);
    rings.b = pow(abs(sin(normDist * baseFreq * (1.0 + ui_CorSpectralSpread))), ui_CorSharpness);

    // Spectral coloring via CIE approximation
    float3 spectralColor = AtmosSpectralToRGB(normDist * 1.2);
    rings = lerp(rings, rings * spectralColor, 0.3);

    // Fiber detail (corneal fiber scratches) — per-angle hash noise
    float angle = atan2(delta.y, delta.x + 0.0001);
    float fiberHash = frac(sin(floor(angle * (float)ui_CorFiberFreq) * 127.1) * 43758.5453);
    float fiberNoise = sin(angle * (float)ui_CorFiberFreq + fiberHash * 6.283);
    float fiberMod = 1.0 + ui_CorFiberDetail * fiberNoise * 0.5;

    // Radial falloff: inner fade (avoid burn-out) + outer exponential
    float innerFade = smoothstep(0.0, 0.1, normDist);
    float outerFade = exp(-normDist * 3.0);

    // Moon factor
    float nightFactor = 1.0 - ENightDayFactor;
    float moonBoost = lerp(1.0, ui_MoonIntensityMult, nightFactor);

    float3 result = rings * fiberMod * innerFade * outerFade
                  * sunVis * moonBoost * ui_CorIntensity;

    return float4(max(result, 0.0), 1.0);
}

// Pass: Ice Crystal Halos (22deg + 46deg) + Parhelia (Sundogs)
// Output is ADDITIVE
float4 PS_IceHalos(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    bool anyEnabled = ui_Halo22Enable || ui_Halo46Enable || ui_ParheliaEnable;
    if (!anyEnabled) return float4(0, 0, 0, 0);

    float sunVis = AtmosGetSunVis();
    if (sunVis < 0.001) return float4(0, 0, 0, 0);

    float2 sunUV = AtmosGetSunUV();
    float2 fromSun = (txcoord - sunUV) * float2(ScreenSize.z, 1.0);
    float screenDist = length(fromSun);
    float tanHFov = tan(FieldOfView * 0.5 * 0.01745329);

    float3 result = 0;

    // Moon factor
    float nightFactor = 1.0 - ENightDayFactor;
    float moonBoost = lerp(1.0, ui_MoonIntensityMult * 0.75, nightFactor);

    // --- 22deg Circular Halo ---
    if (ui_Halo22Enable)
    {
        // Minimum deviation through hexagonal ice prism (60deg prism)
        // delta_min = 2*arcsin(n*sin(30deg)) - 60deg
        float3 devAngle;
        devAngle.r = 2.0 * asin(ICE_IOR.r * 0.5) - 1.0472;
        devAngle.g = 2.0 * asin(ICE_IOR.g * 0.5) - 1.0472;
        devAngle.b = 2.0 * asin(ICE_IOR.b * 0.5) - 1.0472;

        float3 angRadius = tan(devAngle) / tanHFov;

        // Per-channel signed distance from halo ring
        float3 sd = float3(screenDist, screenDist, screenDist) - angRadius;

        // Ring: sharp inner edge (Fresnel-like), soft outer fade
        float3 innerEdge = pow(saturate(smoothstep(-0.003, 0.0, sd)), ui_Halo22InnerEdge);
        float3 outerFade = exp(-max(sd, 0.0) / max(ui_Halo22Thickness, 0.001));

        result += innerEdge * outerFade * ui_Halo22Intensity * moonBoost;
    }

    // --- 46deg Circumscribed Halo ---
    if (ui_Halo46Enable)
    {
        // 90deg prism: delta_min = 2*arcsin(n*sin(45deg)) - 90deg
        float3 devAngle46;
        devAngle46.r = 2.0 * asin(ICE_IOR.r * 0.7071) - 1.5708;
        devAngle46.g = 2.0 * asin(ICE_IOR.g * 0.7071) - 1.5708;
        devAngle46.b = 2.0 * asin(ICE_IOR.b * 0.7071) - 1.5708;
        float3 angRadius46 = tan(devAngle46) / tanHFov;

        float3 sd46 = float3(screenDist, screenDist, screenDist) - angRadius46;
        float3 halo46 = pow(saturate(smoothstep(-0.003, 0.0, sd46)), 2.0)
                      * exp(-max(sd46, 0.0) / 0.025);
        result += halo46 * ui_Halo46Intensity * moonBoost;
    }

    // --- Parhelia (Sundogs) at +/-22deg horizontal ---
    if (ui_ParheliaEnable)
    {
        float parheliaR = tan(0.3839) / tanHFov; // 22deg in screen coords

        // Two bright spots: left and right of sun on horizontal line
        float2 leftDog  = float2(-parheliaR, 0.0);
        float2 rightDog = float2( parheliaR, 0.0);

        float dL = length(fromSun - leftDog);
        float dR = length(fromSun - rightDog);

        // Per-channel chromatic dispersion (red further from sun)
        float3 chromaOff = (ICE_IOR - 1.3098) * ui_ParheliaDispersion * 5.0;
        float sigma2 = 2.0 * ui_ParheliaSize * ui_ParheliaSize;

        float3 dogL, dogR;
        dogL.r = exp(-pow(dL + chromaOff.r, 2) / sigma2);
        dogL.g = exp(-pow(dL + chromaOff.g, 2) / sigma2);
        dogL.b = exp(-pow(dL + chromaOff.b, 2) / sigma2);
        dogR.r = exp(-pow(dR - chromaOff.r, 2) / sigma2);
        dogR.g = exp(-pow(dR - chromaOff.g, 2) / sigma2);
        dogR.b = exp(-pow(dR - chromaOff.b, 2) / sigma2);

        // Sundogs brightest when sun near horizon
        float sinElev = max(abs(1.0 - sunUV.y * 2.0), 0.01);
        float horizonFactor = 1.0 - sinElev;

        result += (dogL + dogR) * ui_ParheliaIntensity * horizonFactor * moonBoost;
    }

    return float4(max(result * sunVis, 0.0), 1.0);
}


// Pass: Heiligenschein — Retroreflection glow from dew drops around antisolar point.
// Each dew droplet acts as a cat's-eye retroreflector (refract-reflect-refract).
// Observed as a bright halo around the observer's shadow on dewy grass at sunrise.
// Ref: Minnaert 1954 "The Nature of Light and Colour in the Open Air", Ch. 11.
// Output is ADDITIVE.
float4 PS_Heiligenschein(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    if (!ui_HeiligEnable) return float4(0, 0, 0, 0);

    // Only visible during day, outdoors
    if (ENightDayFactor < 0.3 || EInteriorFactor > 0.5) return float4(0, 0, 0, 0);

    // Antisolar point: opposite direction from sun on screen
    float2 sunUV = AtmosGetSunUV();
    float2 antiSolarUV = 1.0 - sunUV;

    // Angular distance from antisolar point (aspect-corrected)
    float2 delta = (txcoord - antiSolarUV) * float2(ScreenSize.z, 1.0);
    float dist = length(delta);

    // Radial falloff: sharp Gaussian centered at antisolar point
    float heilig = exp(-dist * dist / (2.0 * ui_HeiligRadius * ui_HeiligRadius));

    // Depth mask: only on near-ground geometry (not sky, not distant)
    float depth = TextureDepth.SampleLevel(smpPoint, txcoord, 0).x;
    float linDepth = depth / (depth * (-2999.0) + 3000.0);
    float groundMask = smoothstep(0.0, 0.05, linDepth) * smoothstep(0.3, 0.1, linDepth);

    // Dawn/dusk moisture factor (dew strongest at sunrise, evaporates by noon)
    float timeOfDay = TimeOfDay1.x + TimeOfDay1.y; // dawn + morning weight
    float dewFactor = saturate(timeOfDay * 2.0);

    // Warm yellow-green tint (chlorophyll retroreflection from grass)
    float3 tint = float3(0.9, 1.0, 0.7);

    float3 result = tint * heilig * groundMask * dewFactor
                  * ui_HeiligIntensity * ENightDayFactor;

    return float4(max(result, 0.0), 1.0);
}


// Pass: Procedural Star Field — Hash-based stars with scintillation.
// Uses screen-space hash grid to generate star positions.
// Scintillation modeled as temporal IGN noise per star cell (atmospheric turbulence).
// Ref: Bohren & Huffman 1983 (scintillation from atmospheric turbulence cells).
// Output is ADDITIVE.
float4 PS_StarField(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    if (!ui_StarEnable) return float4(0, 0, 0, 0);

    // Night only, outdoors only
    float nightFactor = 1.0 - ENightDayFactor;
    if (nightFactor < 0.2 || EInteriorFactor > 0.5) return float4(0, 0, 0, 0);

    // Sky mask: only at extreme depth (sky pixels)
    float depth = TextureDepth.SampleLevel(smpPoint, txcoord, 0).x;
    if (depth < 0.999) return float4(0, 0, 0, 0);

    // Grid cell for star placement
    float2 aspectUV = txcoord * float2(ScreenSize.z, 1.0);
    float cellSize = 1.0 / ui_StarDensity;
    float2 cell = floor(aspectUV / cellSize);
    float2 cellUV = frac(aspectUV / cellSize);

    // Hash to determine star presence and position within cell
    float h1 = frac(sin(dot(cell, float2(127.1, 311.7))) * 43758.5453);
    float h2 = frac(sin(dot(cell, float2(269.5, 183.3))) * 76543.2109);
    float h3 = frac(sin(dot(cell, float2(419.2, 371.9))) * 28741.6532);

    // Only ~30% of cells have a star (sparse field)
    if (h1 > 0.30) return float4(0, 0, 0, 0);

    // Star position within cell (jittered from center)
    float2 starPos = float2(h2, h3) * 0.6 + 0.2;
    float distToStar = length(cellUV - starPos);

    // Star PSF: tight Gaussian (sub-pixel to few pixels)
    float magnitude = h1 / 0.30; // 0-1 normalized brightness (brighter = rarer)
    float starSize = lerp(0.15, 0.05, magnitude); // brighter stars slightly larger
    float star = exp(-distToStar * distToStar / (starSize * starSize * cellSize * cellSize));

    // Magnitude-based brightness (logarithmic, like real star magnitudes)
    float brightness = pow(1.0 - magnitude, 3.0) * ui_StarIntensity;

    // Scintillation: atmospheric turbulence causes intensity fluctuation
    // Each star twinkles at its own rate based on cell hash
    float twinkleRate = 1.5 + h2 * 4.0; // 1.5-5.5 Hz
    float twinklePhase = h3 * 6.283;
    float scintillation = 1.0 + ui_StarScintillation
        * sin(Timer.x * twinkleRate + twinklePhase)
        * (0.5 + 0.5 * sin(Timer.x * twinkleRate * 0.7 + twinklePhase * 1.3));

    // Star color: spectral type variation (O=blue, A=white, G=yellow, K=orange, M=red)
    float colorHash = frac(h1 * 7.31 + h2 * 3.17);
    float3 starColor = float3(1, 1, 1);
    if (ui_StarColorVariation > 0.001)
    {
        // Map hash to color temperature
        float3 hot  = float3(0.7, 0.8, 1.3);  // O/B type (blue-white)
        float3 warm = float3(1.0, 1.0, 0.9);  // A/F/G type (white-yellow)
        float3 cool = float3(1.3, 0.85, 0.6); // K/M type (orange-red)

        float3 spectralType = (colorHash < 0.3)
            ? lerp(hot, warm, colorHash / 0.3)
            : lerp(warm, cool, saturate((colorHash - 0.3) / 0.7));

        starColor = lerp(float3(1, 1, 1), spectralType, ui_StarColorVariation);
    }

    // Atmospheric extinction near horizon: stars dim toward horizon
    float horizonFade = smoothstep(0.7, 0.4, txcoord.y);

    float3 result = starColor * star * brightness * scintillation
                  * nightFactor * (1.0 - horizonFade);

    return float4(max(result, 0.0), 1.0);
}


// --- Light Pillar UI ---

bool ui_LightPillarEnable
<
    string UIName = "ATMOS | Light Pillar Enable";
> = {false};

float ui_LightPillarIntensity
<
    string UIName = "ATMOS | Light Pillar Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.5};

float ui_LightPillarLength
<
    string UIName = "ATMOS | Light Pillar Length";
    string UIWidget = "Spinner";
    float UIMin = 0.05; float UIMax = 0.8; float UIStep = 0.01;
> = {0.3};

float ui_LightPillarWidth
<
    string UIName = "ATMOS | Light Pillar Width";
    string UIWidget = "Spinner";
    float UIMin = 0.002; float UIMax = 0.05; float UIStep = 0.001;
> = {0.01};

// --- Belt of Venus + Earth Shadow UI ---

bool ui_BeltOfVenusEnable
<
    string UIName = "ATMOS | Belt of Venus Enable";
> = {false};

float ui_BeltOfVenusIntensity
<
    string UIName = "ATMOS | Belt of Venus Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.6};

// --- Green Flash UI ---

bool ui_GreenFlashEnable
<
    string UIName = "ATMOS | Green Flash Enable";
> = {false};

float ui_GreenFlashIntensity
<
    string UIName = "ATMOS | Green Flash Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {1.0};

// --- Atmospheric Disc Refraction UI ---

bool ui_DiscRefractionEnable
<
    string UIName = "ATMOS | Disc Refraction (Flatten)";
> = {false};

float ui_DiscRefractionStr
<
    string UIName = "ATMOS | Disc Refraction Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};


//=============================================================================
//  PASS: Light Pillar — Vertical columns from ice plate crystals
//
//  Horizontally oriented hexagonal ice plate crystals reflect sunlight into
//  vertical columns above and below the light source. Length depends on
//  crystal tilt distribution (wobble angle). Strongest near horizon.
//  Ref: Tape 1994 "Atmospheric Halos and the Search for Angle x"
//=============================================================================
float4 PS_LightPillar(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    if (!ui_LightPillarEnable) return float4(0, 0, 0, 0);

    // Only visible outdoors, during twilight/day
    if (EInteriorFactor > 0.5) return float4(0, 0, 0, 0);

    float2 sunUV = AtmosGetSunUV();

    // Horizontal distance from sun column (aspect-corrected)
    float hDist = abs(txcoord.x - sunUV.x) * ScreenSize.z;

    // Vertical: pillar extends above and below the source
    float vDist = txcoord.y - sunUV.y;
    float vNorm = abs(vDist) / max(ui_LightPillarLength, 0.01);

    // Gaussian horizontal profile (very narrow column)
    float hProfile = exp(-hDist * hDist / (2.0 * ui_LightPillarWidth * ui_LightPillarWidth));

    // Vertical: linear fade to zero at pillar tip, quadratic for smooth falloff
    float vProfile = saturate(1.0 - vNorm);
    vProfile *= vProfile;

    // Pillar strongest near horizon (low sun elevation)
    float sinElev = max(abs(1.0 - sunUV.y * 2.0), 0.01);
    float horizonFactor = smoothstep(0.3, 0.0, sinElev);

    // Warm tint from ice crystal specular reflection
    float3 tint = float3(1.0, 0.92, 0.8);

    // Sun visibility factor
    float sunVis = saturate(LightParameters.w);

    float3 result = tint * hProfile * vProfile * horizonFactor
                  * ui_LightPillarIntensity * sunVis;

    return float4(max(result, 0.0), 1.0);
}


//=============================================================================
//  PASS: Belt of Venus + Earth Shadow
//
//  During civil twilight, the antisolar horizon shows two bands:
//  1. Earth's shadow: blue-grey band near the horizon (unlit atmosphere)
//  2. Belt of Venus: pink/magenta arch above it (backscattered reddened sunlight)
//  Both visible only when sun is near/below horizon, opposite the sun.
//  Ref: Hulburt 1953, Lynch & Livingston 2001 "Color and Light in Nature"
//=============================================================================
float4 PS_BeltOfVenus(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    if (!ui_BeltOfVenusEnable) return float4(0, 0, 0, 0);
    if (EInteriorFactor > 0.5) return float4(0, 0, 0, 0);

    // Sky mask: only on sky pixels
    float depth = TextureDepth.SampleLevel(smpPoint, txcoord, 0).x;
    if (depth < 0.999) return float4(0, 0, 0, 0);

    // Only visible during civil twilight (ENightDayFactor near transition)
    float twilightFactor = smoothstep(0.15, 0.4, ENightDayFactor)
                         * smoothstep(0.7, 0.5, ENightDayFactor);
    if (twilightFactor < 0.01) return float4(0, 0, 0, 0);

    // Antisolar point: opposite the sun
    float2 sunUV = AtmosGetSunUV();
    float2 antiSolarUV = float2(1.0 - sunUV.x, sunUV.y);

    // Horizontal proximity to antisolar azimuth (wide band across horizon)
    float hDist = abs(txcoord.x - antiSolarUV.x);
    float hFade = smoothstep(0.6, 0.0, hDist);

    // Vertical position: Earth shadow band at horizon, Belt of Venus above it
    // txcoord.y: 0=top, 1=bottom. Horizon is near bottom of sky.
    float horizonDist = 1.0 - txcoord.y; // 0 at bottom, 1 at top

    // Earth shadow: dark blue-grey band just above true horizon
    float shadowBand = smoothstep(0.0, 0.08, horizonDist) * smoothstep(0.15, 0.05, horizonDist);
    float3 shadowColor = float3(0.15, 0.18, 0.30); // blue-grey

    // Belt of Venus: pink/magenta arch above the shadow
    float beltBand = smoothstep(0.08, 0.18, horizonDist) * smoothstep(0.35, 0.20, horizonDist);
    float3 beltColor = float3(0.45, 0.25, 0.35); // warm pink/magenta

    float3 result = (shadowColor * shadowBand + beltColor * beltBand)
                  * hFade * twilightFactor * ui_BeltOfVenusIntensity;

    return float4(max(result, 0.0), 1.0);
}


//=============================================================================
//  PASS: Green Flash
//
//  Differential atmospheric refraction separates the solar disc by wavelength
//  at the horizon. Blue is scattered away, red refracts least, leaving a brief
//  green/cyan fringe at the upper limb as the sun crosses the horizon.
//  Visible only in the last ~1° before sunset (or first 1° after sunrise).
//  Ref: Young 1999, O'Connell 1958 "The Green Flash"
//=============================================================================
float4 PS_GreenFlash(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    if (!ui_GreenFlashEnable) return float4(0, 0, 0, 0);
    if (EInteriorFactor > 0.5) return float4(0, 0, 0, 0);

    float2 sunUV = AtmosGetSunUV();
    float sunVis = saturate(LightParameters.w);

    // Only visible when sun is at/near horizon (very low elevation)
    // Sun UV.y near 0.5 = horizon, approaching 1.0 = below
    float sinElev = max(1.0 - sunUV.y * 2.0, -0.1);
    float horizonProx = smoothstep(0.05, 0.0, abs(sinElev));
    if (horizonProx < 0.01) return float4(0, 0, 0, 0);

    // Distance from sun upper limb (flash appears at top edge of solar disc)
    float2 delta = txcoord - sunUV;
    delta.x *= ScreenSize.z;
    float dist = length(delta);

    // Vertical: flash sits just above the sun center (upper limb)
    float vOffset = txcoord.y - sunUV.y;
    float upperLimb = smoothstep(0.0, -0.015, vOffset); // above sun center

    // Tight radial proximity (small angular extent)
    float radialFade = exp(-dist * dist / (0.008 * 0.008));

    // Green/cyan tint from differential refraction
    // Blue scattered, red refracts down, green/cyan remains at upper limb
    float3 flashColor = float3(0.1, 1.0, 0.7);

    float3 result = flashColor * radialFade * upperLimb * horizonProx
                  * ui_GreenFlashIntensity * sunVis * ENightDayFactor;

    return float4(max(result, 0.0), 1.0);
}


//=============================================================================
//  PASS: Atmospheric Disc Refraction (Horizon Flattening)
//
//  Atmospheric refraction compresses the solar/lunar disc vertically near the
//  horizon. The bottom limb is refracted more than the top, producing an
//  oblate disc with ~5:4 aspect ratio at 0° altitude. Decreases with elevation.
//  Implemented as a UV perturbation on the scene near the sun position.
//  Ref: Meinel & Meinel 1983 "Sunsets, Twilights, and Evening Skies"
//=============================================================================
float4 PS_DiscRefraction(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;

    if (!ui_DiscRefractionEnable) return float4(color, 1.0);
    if (EInteriorFactor > 0.5) return float4(color, 1.0);

    float2 sunUV = AtmosGetSunUV();

    // Only near horizon: refraction diminishes rapidly with elevation
    float sinElev = abs(1.0 - sunUV.y * 2.0);
    float refrStr = smoothstep(0.15, 0.0, sinElev) * ui_DiscRefractionStr;
    if (refrStr < 0.001) return float4(color, 1.0);

    // Distance from sun center (only affect pixels near the disc)
    float2 delta = txcoord - sunUV;
    delta.x *= ScreenSize.z;
    float dist = length(delta);

    // Only affect the solar/lunar disc region (~3% of screen)
    float discMask = smoothstep(0.04, 0.02, dist);
    if (discMask < 0.001) return float4(color, 1.0);

    // Vertical compression: shift UV.y toward sun center
    // Bottom limb shifts up more than top limb (differential refraction)
    float vOffset = txcoord.y - sunUV.y;
    float compressionAmount = refrStr * 0.2 * discMask;

    // Asymmetric: bottom limb (vOffset > 0) compresses more
    float asymmetry = 1.0 + saturate(vOffset * 20.0) * 0.25;

    float2 refractedUV = txcoord;
    refractedUV.y -= vOffset * compressionAmount * asymmetry;

    float3 refractedColor = TextureColor.SampleLevel(smpLinear, refractedUV, 0).rgb;
    color = lerp(color, refractedColor, discMask);

    return float4(max(color, 0.0), 1.0);
}


#endif // SUNSPRITE_ATMOSPHERIC_OPTICS_FXH
