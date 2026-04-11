//=============================================================================
//  Underwater_VolumetricFX.fxh — Underwater Volumetric Effects Suite
//
//  Called from enbunderwater.fx PS_Draw() after existing depth blur composite.
//  Effects: Caustics (trochoidal/noise/voronoi), God Rays, Bioluminescence,
//           Tyndall Scattering, Particles, Bubbles, Absorption/Photic Zones
//
//  All features disabled by default.
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef UNDERWATER_VOLUMETRICFX_FXH
#define UNDERWATER_VOLUMETRICFX_FXH


//=== UI PARAMETERS ===//

// --- Caustics ---

bool ui_CausticEnable
<
    string UIName = "UW FX | Caustics Enable";
> = {false};

int ui_CausticMode
<
    string UIName = "UW FX | Caustic Mode (0=Trochoidal 1=Noise 2=Voronoi)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 2;
> = {0};

float ui_CausticIntensity
<
    string UIName = "UW FX | Caustic Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.8};

float ui_CausticScale
<
    string UIName = "UW FX | Caustic Scale";
    string UIWidget = "Spinner";
    float UIMin = 1.0; float UIMax = 50.0; float UIStep = 0.5;
> = {15.0};

float ui_CausticSpeed
<
    string UIName = "UW FX | Caustic Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {1.0};

float ui_CausticSharpness
<
    string UIName = "UW FX | Caustic Sharpness";
    string UIWidget = "Spinner";
    float UIMin = 2.0; float UIMax = 20.0; float UIStep = 0.5;
> = {12.0};

float ui_CausticDepthFade
<
    string UIName = "UW FX | Caustic Depth Fade";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 10.0; float UIStep = 0.1;
> = {3.0};

float ui_CausticSurfaceFade
<
    string UIName = "UW FX | Caustic Surface Fade";
    string UIWidget = "Spinner";
    float UIMin = 0.01; float UIMax = 0.5; float UIStep = 0.01;
> = {0.1};

float ui_CausticChromatic
<
    string UIName = "UW FX | Caustic Chromatic Split";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

// --- God Rays ---

bool ui_GodRayEnable
<
    string UIName = "UW FX | God Rays Enable";
> = {false};

float ui_GodRayIntensity
<
    string UIName = "UW FX | God Ray Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {0.5};

float ui_GodRayDecay
<
    string UIName = "UW FX | God Ray Decay";
    string UIWidget = "Spinner";
    float UIMin = 0.8; float UIMax = 1.0; float UIStep = 0.005;
> = {0.96};

float ui_GodRayDensity
<
    string UIName = "UW FX | God Ray Density";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 2.0; float UIStep = 0.01;
> = {0.5};

int ui_GodRaySamples
<
    string UIName = "UW FX | God Ray Samples (8-24)";
    string UIWidget = "Spinner";
    int UIMin = 8; int UIMax = 24;
> = {12};

float ui_GodRaySunY
<
    string UIName = "UW FX | God Ray Sun Height";
    string UIWidget = "Spinner";
    float UIMin = -0.5; float UIMax = 0.5; float UIStep = 0.01;
> = {0.0};

float ui_GodRaySunX
<
    string UIName = "UW FX | God Ray Sun Offset X";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

// --- Bioluminescence ---

bool ui_BioEnable
<
    string UIName = "UW FX | Bioluminescence Enable";
> = {false};

float ui_BioDensity
<
    string UIName = "UW FX | Bio Density";
    string UIWidget = "Spinner";
    float UIMin = 0.05; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_BioBrightness
<
    string UIName = "UW FX | Bio Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 3.0; float UIStep = 0.01;
> = {1.0};

float ui_BioPulseSpeed
<
    string UIName = "UW FX | Bio Pulse Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 3.0; float UIStep = 0.1;
> = {1.0};

float3 ui_BioColor
<
    string UIName = "UW FX | Bio Color";
    string UIWidget = "Color";
> = {0.2, 0.8, 0.5};

// --- Tyndall Scattering ---

bool ui_TyndallEnable
<
    string UIName = "UW FX | Tyndall Scattering Enable";
> = {false};

float ui_TyndallIntensity
<
    string UIName = "UW FX | Tyndall Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.3};

float ui_TyndallAnisotropy
<
    string UIName = "UW FX | Tyndall Anisotropy (g)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 0.95; float UIStep = 0.01;
> = {0.7};

float ui_TyndallTurbidity
<
    string UIName = "UW FX | Tyndall Turbidity";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 5.0; float UIStep = 0.1;
> = {1.0};

float ui_TyndallWavelength
<
    string UIName = "UW FX | Tyndall Wavelength Dependence";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

// --- Particles ---

bool ui_ParticleEnable
<
    string UIName = "UW FX | Particles Enable";
> = {false};

float ui_ParticleDensity
<
    string UIName = "UW FX | Particle Density";
    string UIWidget = "Spinner";
    float UIMin = 0.05; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_ParticleBrightness
<
    string UIName = "UW FX | Particle Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.5};

float ui_ParticleSize
<
    string UIName = "UW FX | Particle Size";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 3.0; float UIStep = 0.1;
> = {1.0};

// --- Bubbles ---

bool ui_BubbleEnable
<
    string UIName = "UW FX | Bubbles Enable";
> = {false};

float ui_BubbleDensity
<
    string UIName = "UW FX | Bubble Density";
    string UIWidget = "Spinner";
    float UIMin = 0.05; float UIMax = 1.0; float UIStep = 0.01;
> = {0.2};

float ui_BubbleRiseSpeed
<
    string UIName = "UW FX | Bubble Rise Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 3.0; float UIStep = 0.1;
> = {1.0};

float ui_BubbleWobble
<
    string UIName = "UW FX | Bubble Wobble";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

// --- Absorption / Photic Zones ---

bool ui_AbsorpEnable
<
    string UIName = "UW FX | Absorption Grading Enable";
> = {false};

float ui_AbsorpSurfaceWarmth
<
    string UIName = "UW FX | Surface Warmth";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_AbsorpDeepCold
<
    string UIName = "UW FX | Deep Cold Tint";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

float ui_AbsorpDesat
<
    string UIName = "UW FX | Depth Desaturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.4};

float ui_AbsorpContrastLoss
<
    string UIName = "UW FX | Depth Contrast Loss";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

float ui_AbsorpTwilightDepth
<
    string UIName = "UW FX | Twilight Zone Depth";
    string UIWidget = "Spinner";
    float UIMin = 0.05; float UIMax = 0.5; float UIStep = 0.01;
> = {0.15};

float ui_AbsorpAbyssalDepth
<
    string UIName = "UW FX | Abyssal Zone Depth";
    string UIWidget = "Spinner";
    float UIMin = 0.1; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};


//=== HELPER FUNCTIONS ===//

float UW_Hash21(float2 p)
{
    return frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453);
}

float2 UW_Hash22(float2 p)
{
    return float2(
        frac(sin(dot(p, float2(127.1, 311.7))) * 43758.5453),
        frac(sin(dot(p, float2(269.5, 183.3))) * 43758.5453)
    );
}

float3 UW_HueShift(float3 color, float shift)
{
    // Simple hue rotation via YIQ
    float Y = dot(color, float3(0.299, 0.587, 0.114));
    float I = dot(color, float3(0.596, -0.275, -0.321));
    float Q = dot(color, float3(0.212, -0.523, 0.311));
    float angle = shift * 6.283;
    float cosA = cos(angle);
    float sinA = sin(angle);
    float newI = I * cosA - Q * sinA;
    float newQ = I * sinA + Q * cosA;
    return float3(
        Y + 0.956 * newI + 0.621 * newQ,
        Y - 0.272 * newI - 0.647 * newQ,
        Y - 1.106 * newI + 1.703 * newQ
    );
}


//=== EFFECT FUNCTIONS ===//

// Absorption / Photic Zone Grading (applied FIRST — before additive effects)
float3 UW_Absorption(float3 color, float2 uv, float depth)
{
    float luma = dot(color, float3(0.2126, 0.7152, 0.0722));

    // Surface zone: warm tint
    float surfaceW = 1.0 - smoothstep(0.0, ui_AbsorpTwilightDepth, depth);
    float3 warmTint = lerp(float3(1, 1, 1), float3(1.1, 1.0, 0.9), ui_AbsorpSurfaceWarmth);
    color *= lerp(float3(1, 1, 1), warmTint, surfaceW);

    // Depth desaturation
    float depthFactor = smoothstep(0.0, ui_AbsorpAbyssalDepth, depth);
    color = lerp(color, luma, ui_AbsorpDesat * depthFactor);

    // Deep cold tint
    float3 coldTint = float3(0.7, 0.85, 1.0);
    color = lerp(color, color * coldTint, ui_AbsorpDeepCold * depthFactor);

    // Contrast loss with depth
    float3 fogColor = ui_TintColor * 0.3; // Reuse existing tint color
    color = lerp(color, fogColor, ui_AbsorpContrastLoss * depthFactor);

    return color;
}

// Caustics — 3 modes: Trochoidal (physical), Noise, Voronoi
float3 UW_Caustics(float3 color, float2 uv, float depth)
{
    float depthFade = exp(-depth * ui_CausticDepthFade);
    float surfaceFade = smoothstep(0.0, ui_CausticSurfaceFade, depth);
    float t = Timer.x * 1677.7216 * ui_CausticSpeed;

    float3 causticRGB;

    if (ui_CausticMode == 0) // Trochoidal Wave (physical interference)
    {
        // 4-octave trochoidal wave sum -> sharp interference pattern
        float2 worldUV = uv * ui_CausticScale;

        // Four crossing wave trains at different angles
        float c1 = 0.5 + 0.5 * sin(dot(worldUV, float2( 1.0, 0.3)) * 8.0 + t * 1.1);
        float c2 = 0.5 + 0.5 * sin(dot(worldUV, float2(-0.7, 0.8)) * 6.0 + t * 0.9);
        float c3 = 0.5 + 0.5 * sin(dot(worldUV, float2( 0.3,-1.0)) * 7.0 + t * 1.3);
        float c4 = 0.5 + 0.5 * sin(dot(worldUV, float2(-0.9,-0.4)) * 5.0 + t * 0.7);

        // Sharp interference: pow(1-min, sharpness) creates caustic lines
        float caustic = pow(1.0 - min(min(c1, c2), min(c3, c4)), ui_CausticSharpness);

        // Chromatic split: per-channel with slightly offset UVs
        float chromaOff = ui_CausticChromatic * 0.003;
        float c1r = 0.5 + 0.5 * sin(dot(worldUV + chromaOff, float2( 1.0, 0.3)) * 8.0 + t * 1.1);
        float c2r = 0.5 + 0.5 * sin(dot(worldUV + chromaOff, float2(-0.7, 0.8)) * 6.0 + t * 0.9);
        float c3r = 0.5 + 0.5 * sin(dot(worldUV + chromaOff, float2( 0.3,-1.0)) * 7.0 + t * 1.3);
        float c4r = 0.5 + 0.5 * sin(dot(worldUV + chromaOff, float2(-0.9,-0.4)) * 5.0 + t * 0.7);
        float causticR = pow(1.0 - min(min(c1r, c2r), min(c3r, c4r)), ui_CausticSharpness);

        float c1b = 0.5 + 0.5 * sin(dot(worldUV - chromaOff, float2( 1.0, 0.3)) * 8.0 + t * 1.1);
        float c2b = 0.5 + 0.5 * sin(dot(worldUV - chromaOff, float2(-0.7, 0.8)) * 6.0 + t * 0.9);
        float c3b = 0.5 + 0.5 * sin(dot(worldUV - chromaOff, float2( 0.3,-1.0)) * 7.0 + t * 1.3);
        float c4b = 0.5 + 0.5 * sin(dot(worldUV - chromaOff, float2(-0.9,-0.4)) * 5.0 + t * 0.7);
        float causticB = pow(1.0 - min(min(c1b, c2b), min(c3b, c4b)), ui_CausticSharpness);

        causticRGB = float3(causticR, caustic, causticB);
    }
    else if (ui_CausticMode == 1) // Noise (fast)
    {
        float2 worldUV = uv * ui_CausticScale;
        // 3-layer scrolling sine with counter-drift
        float n1 = sin(dot(worldUV * 1.0 + t * float2(0.7, 0.5), float2(1.0, 1.0)));
        float n2 = sin(dot(worldUV * 1.37 + t * float2(-0.5, 0.6), float2(1.0, 1.0)));
        float n3 = sin(dot(worldUV * 2.13 + t * float2(0.3, -0.7), float2(1.0, 1.0)));
        float caustic = (n1 + n2 + n3) / 3.0 * 0.5 + 0.5;
        caustic = pow(saturate(caustic), ui_CausticSharpness * 0.5);
        causticRGB = caustic;
    }
    else // Voronoi (detailed)
    {
        float2 worldUV = uv * ui_CausticScale * 0.3;
        // Dual animated Worley grids
        float2 drift1 = float2(0.3, 0.2) * t;
        float2 drift2 = float2(-0.2, 0.35) * t;

        float minDist1 = 1.0;
        float minDist2 = 1.0;

        int2 cell1 = (int2)floor(worldUV + drift1);
        int2 cell2 = (int2)floor(worldUV + drift2);

        [loop] for (int cy = -1; cy <= 1; cy++)
        [loop] for (int cx = -1; cx <= 1; cx++)
        {
            float2 cid1 = float2(cell1 + int2(cx, cy));
            float2 center1 = UW_Hash22(cid1) * 0.8 + 0.1;
            center1 += 0.2 * sin(t * 0.5 + UW_Hash22(cid1) * 6.283);
            float d1 = length(frac(worldUV + drift1) - float2(cx, cy) - center1);
            minDist1 = min(minDist1, d1);

            float2 cid2 = float2(cell2 + int2(cx, cy));
            float2 center2 = UW_Hash22(cid2 + 100.0) * 0.8 + 0.1;
            center2 += 0.2 * sin(t * 0.4 + UW_Hash22(cid2 + 100.0) * 6.283);
            float d2 = length(frac(worldUV + drift2) - float2(cx, cy) - center2);
            minDist2 = min(minDist2, d2);
        }

        float caustic = sqrt(minDist1) * sqrt(minDist2);
        caustic = pow(saturate(1.0 - caustic * 2.0), ui_CausticSharpness * 0.5);
        causticRGB = caustic;
    }

    return color + causticRGB * ui_CausticIntensity * depthFade * surfaceFade;
}

// God Rays — screen-space radial sampling with IGN dithering
float3 UW_GodRays(float2 uv, float depth)
{
    // Sun position (UI-configurable)
    float2 sunPos = float2(ui_GodRaySunX, ui_GodRaySunY);
    float2 rayDir = uv - sunPos;
    float2 stepSize = rayDir / (float)ui_GodRaySamples * ui_GodRayDensity;

    // IGN temporal dither to prevent ring artifacts
    float dither = frac(52.9829189 * frac(dot(uv * ScreenSize.x, float2(0.06711056, 0.00583715))));
    float2 samplePos = uv - stepSize * dither;

    float3 accum = 0;
    float decay = 1.0;

    [loop] for (int i = 0; i < 24; i++)
    {
        if (i >= ui_GodRaySamples) break;
        samplePos -= stepSize;
        if (any(samplePos < 0.0) || any(samplePos > 1.0)) break;

        float3 samp = TextureOriginal.SampleLevel(smpLinear, samplePos, 0).rgb;
        accum += samp * decay;
        decay *= ui_GodRayDecay;
    }

    // Depth-based absorption tinting (deeper = more blue)
    float3 absTint = lerp(float3(1, 1, 1), ui_TintColor, saturate(depth * 3.0));

    return accum * ui_GodRayIntensity * absTint / (float)ui_GodRaySamples;
}

// Bioluminescence — 3-species depth-stratified procedural plankton
float3 UW_Bioluminescence(float2 uv, float depth)
{
    float3 bioAccum = 0;

    // 3 species layers at different depths and scales
    static const float3 SPECIES_COLOR[3] = {
        float3(0.3, 1.0, 0.5),   // Shallow: dinoflagellate green-cyan
        float3(0.2, 0.6, 1.0),   // Mid: comb jelly blue
        float3(0.1, 0.2, 1.0),   // Deep: deep-sea blue-violet
    };
    static const float SPECIES_DEPTH[3] = { 0.1, 0.25, 0.5 };
    static const float SPECIES_SCALE[3] = { 20.0, 14.0, 8.0 };

    [loop] for (int layer = 0; layer < 3; layer++)
    {
        float depthWeight = exp(-pow(depth - SPECIES_DEPTH[layer], 2) * 30.0);
        if (depthWeight < 0.01) continue;

        float scale = SPECIES_SCALE[layer];
        float2 gridUV = uv * scale;
        float2 drift = float2(0.3, 0.1) * Timer.x * 1677.7216 * ui_BioPulseSpeed * (1.0 + layer * 0.3);
        gridUV += drift;

        int2 cell = (int2)floor(gridUV);
        float2 f = frac(gridUV);

        float3 layerAccum = 0;

        [loop] for (int cy = -1; cy <= 1; cy++)
        [loop] for (int cx = -1; cx <= 1; cx++)
        {
            float2 cid = float2(cell + int2(cx, cy));
            float h = UW_Hash21(cid);
            if (h > ui_BioDensity * 0.3) continue;

            float2 orgPos = UW_Hash22(cid + 50.0);
            float orgRadius = 0.06 + h * 0.08;
            float2 d = f - float2(cx, cy) - orgPos;
            float dist = length(d);

            // Gaussian glow
            float glow = exp(-dist * dist / (orgRadius * orgRadius * 0.5));

            // 3-frequency chaotic pulse
            float phase = h * 6.283;
            float freq1 = 0.8 + h * 0.4;
            float freq2 = 1.3 + h * 0.6;
            float freq3 = 2.1 + h * 0.3;
            float t = Timer.x * 1677.7216 * ui_BioPulseSpeed;
            float pulse = 0.3 + 0.7 * (
                0.33 * (0.5 + 0.5 * sin(t * freq1 + phase)) +
                0.33 * (0.5 + 0.5 * sin(t * freq2 + phase * 2.0)) +
                0.34 * (0.5 + 0.5 * sin(t * freq3 + phase * 3.0))
            );

            // Per-organism hue variation
            float3 orgColor = lerp(SPECIES_COLOR[layer], ui_BioColor,
                                   (h - 0.5) * 0.6 + 0.5);

            layerAccum += glow * pulse * orgColor;
        }

        // Night boost (1.5x at night, 0.7x at day)
        float nightBoost = lerp(1.5, 0.7, ENightDayFactor);
        bioAccum += layerAccum * depthWeight * nightBoost;
    }

    return bioAccum * ui_BioBrightness;
}

// Tyndall Scattering — Henyey-Greenstein with wavelength dependence
float3 UW_Tyndall(float2 uv, float depth)
{
    // Approximate sun direction in screen space (top of screen)
    float2 sunDir = normalize(float2(0.5, 0.0) - uv + 0.001);
    float2 viewDir = normalize(uv - 0.5 + 0.001);
    float cosTheta = dot(viewDir, -sunDir);

    // Henyey-Greenstein phase function (raw, no 4pi normalization)
    float g = ui_TyndallAnisotropy;
    float denom = 1.0 + g * g - 2.0 * g * cosTheta;
    float phase = (1.0 - g * g) / (denom * sqrt(max(denom, 0.0001)));

    // Wavelength dependence: blue scatters more (Rayleigh in water)
    float3 wavelengthWeight = lerp(float3(1, 1, 1),
                                    float3(0.7, 0.85, 1.0),
                                    ui_TyndallWavelength);

    float depthFactor = saturate(depth * ui_TyndallTurbidity);

    return phase * wavelengthWeight * depthFactor * ui_TyndallIntensity * 0.1;
}

// Particles — 3-layer parallax cell noise
float3 UW_Particles(float2 uv, float depth)
{
    float3 particleAccum = 0;
    float t = Timer.x * 1677.7216;

    [loop] for (int layer = 0; layer < 3; layer++)
    {
        float layerScale = (10.0 + layer * 5.0) * ui_ParticleSize;
        float2 gridUV = uv * layerScale;
        // Parallax drift per layer
        gridUV += float2(t * 0.1 * (layer + 1), t * 0.05 * (layer + 1));

        int2 cell = (int2)floor(gridUV);
        float2 f = frac(gridUV);

        [loop] for (int cy = 0; cy <= 1; cy++)
        [loop] for (int cx = 0; cx <= 1; cx++)
        {
            float2 cid = float2(cell + int2(cx, cy));
            float h = UW_Hash21(cid);
            if (h > ui_ParticleDensity * 0.4) continue;

            float2 pPos = UW_Hash22(cid + 200.0);
            // Gentle sway
            pPos.x += sin(t * 0.3 + h * 6.283) * 0.05;
            pPos.y += cos(t * 0.2 + h * 4.0) * 0.03;

            float dist = length(f - float2(cx, cy) - pPos);
            float particleRadius = 0.03 + h * 0.04;
            float glow = exp(-dist * dist / (particleRadius * particleRadius));

            particleAccum += glow * (0.5 + h * 0.5);
        }
    }

    // Depth visibility (mid-range most visible)
    float depthVis = smoothstep(0.02, 0.1, depth) * (1.0 - smoothstep(0.3, 0.8, depth));

    return particleAccum * ui_ParticleBrightness * depthVis * float3(0.8, 0.9, 1.0) * 0.1;
}

// Bubbles — 4-layer parallax with rise + wobble
float3 UW_Bubbles(float3 color, float2 uv, float depth)
{
    float3 result = color;
    float t = Timer.x * 1677.7216;

    [loop] for (int layer = 0; layer < 4; layer++)
    {
        float layerScale = 8.0 + layer * 3.0;
        float2 gridUV = uv * layerScale;
        // Rise: bubbles move upward (near layers = faster parallax, far = slower)
        gridUV.y -= t * ui_BubbleRiseSpeed * (0.9 - layer * 0.2);

        int2 cell = (int2)floor(gridUV);
        float2 f = frac(gridUV);

        [loop] for (int cy = 0; cy <= 1; cy++)
        [loop] for (int cx = 0; cx <= 1; cx++)
        {
            float2 cid = float2(cell + int2(cx, cy));
            float h = UW_Hash21(cid + 300.0);
            if (h > ui_BubbleDensity * 0.25) continue;

            float2 bPos = UW_Hash22(cid + 400.0);
            // Horizontal wobble
            bPos.x += sin(t * (1.0 + h) + h * 6.283) * ui_BubbleWobble * 0.1;

            float dist = length(f - float2(cx, cy) - bPos);
            float radius = 0.04 + h * 0.06;
            float normDist = dist / radius;

            if (normDist < 1.0)
            {
                // Fresnel edge highlight
                float rim = pow(normDist, 3.0) * 0.6;
                // Specular highlight
                float spec = pow(saturate(1.0 - normDist * 0.8), 8.0) * 0.3;
                // Subtle refraction (use TextureOriginal for clean unprocessed scene)
                float2 refractOff = (f - float2(cx, cy) - bPos) * 0.002;
                float3 refracted = TextureOriginal.SampleLevel(smpLinear, uv + refractOff, 0).rgb;

                result = lerp(result, refracted + rim + spec, 0.3 * (1.0 - normDist));
            }
        }
    }

    return result;
}


#endif // UNDERWATER_VOLUMETRICFX_FXH
