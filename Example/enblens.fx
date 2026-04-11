//----------------------------------------------------------------------------------------------//
//                        ENB of the Elders - Cinematic Lens Effects                             //
//----------------------------------------------------------------------------------------------//
//                                                                                              //
//  Anamorphic lens flares (ALF), ghost reflections, and halo ring.                              //
//                                                                                              //
//  Downsample blur chain + 6 ghosts with chromatic dispersion +                                 //
//  rotatable anamorphic streaks with cinematic tint + halo ring with CA.                        //
//  Based on Boris Vorontsov lens framework and kingeric1992 ALF.                                //
//  Adapted for ENB of the Elders by Zain Dana Harper - March 2026                              //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=== ENB EXTERNAL VARIABLES ===//

float4  Timer;
float4  ScreenSize;
float   AdaptiveQuality;
float4  Weather;
float4  TimeOfDay1;
float4  TimeOfDay2;
float   ENightDayFactor;
float   EInteriorFactor;
float   FieldOfView;
float4  tempF1;
float4  tempF2;
float4  tempF3;
float4  tempInfo1;
float4  tempInfo2;


//=== TEXTURES ===//

Texture2D   TextureDownsampled;     // 1024x1024 HDR scene (downscaled)
Texture2D   TextureColor;           // Output of previous technique
Texture2D   TextureOriginal;        // Original scene color (screen size)
Texture2D   TextureDepth;           // Scene depth
Texture2D   TextureAperture;        // Aperture from DOF (1x1)
Texture2D   RenderTarget1024;
Texture2D   RenderTarget512;
Texture2D   RenderTarget256;
Texture2D   RenderTarget128;
Texture2D   RenderTarget64;
Texture2D   RenderTarget32;
Texture2D   RenderTarget16;
Texture2D   RenderTargetRGBA32;
Texture2D   RenderTargetRGBA64F;


//=== SAMPLERS ===//

SamplerState Sampler0
{
    Filter = MIN_MAG_MIP_POINT;
    AddressU = Clamp;
    AddressV = Clamp;
};

SamplerState Sampler1
{
    Filter = MIN_MAG_MIP_LINEAR;
    AddressU = Clamp;
    AddressV = Clamp;
};


//=== GLOBALS ===//

#include "enbglobals.fxh"
#include "Helper/SB_LensCore.fxh"


//=== CONSTANTS ===//

static const float2 PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z);
static const float3 N_LUM = float3(0.2126, 0.7152, 0.0722);
static const float  MAX_LENS_RES = 1024.0;

// ALF sampling
#define ALF_SAMPLE   16
#define ALF_OFFSET   0.5

#ifndef PI
#define PI 3.1415926535897932
#endif


//=== UI PARAMETERS ===//

// --- Master ---

float ui_LensIntensityDay
<
    string UIName = "LENS | Day - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {1.0};

float ui_LensIntensityNight
<
    string UIName = "LENS | Night - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.5};

float ui_LensIntensityInterior
<
    string UIName = "LENS | Interior - Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.3};

float ui_LensSaturation
<
    string UIName = "LENS | Saturation";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {1.0};

// --- Threshold ---

float ui_Threshold
<
    string UIName = "LENS | Bright Threshold";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.01;
> = {0.8};

float ui_ThreshSoftKnee
<
    string UIName = "LENS | Threshold Soft Knee";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

// --- Ghost Reflections ---

float ui_GhostIntensity
<
    string UIName = "GHOST | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.5};

float ui_GhostDispersion
<
    string UIName = "GHOST | Chromatic Dispersion";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

float ui_GhostSpacing
<
    string UIName = "GHOST | Spacing";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.4};

// --- ALF (Anamorphic Lens Flare) ---

float ui_ALFIntensity
<
    string UIName = "ALF | Streak Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.5};

float ui_ALFWidth
<
    string UIName = "ALF | Streak Width";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 20.0;
    float UIStep = 0.1;
> = {8.0};

float ui_ALFSecondary
<
    string UIName = "ALF | Secondary Streak";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.3};

float ui_ALFAngle
<
    string UIName = "ALF | Streak Angle";
    string UIWidget = "Spinner";
    float UIMin = -45.0;
    float UIMax = 45.0;
    float UIStep = 0.5;
> = {0.0};

float ui_ALFTint
<
    string UIName = "ALF | Anamorphic Blue Tint";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

// --- Halo Ring ---

float ui_HaloIntensity
<
    string UIName = "HALO | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.3};

float ui_HaloWidth
<
    string UIName = "HALO | Ring Width";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 0.6;
    float UIStep = 0.01;
> = {0.35};

float ui_HaloCA
<
    string UIName = "HALO | Chromatic Aberration";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.5;
    float UIStep = 0.01;
> = {0.0};

// --- Ghost Shape ---

int ui_GhostBlades
<
    string UIName = "GHOST | Aperture Blades";
    string UIWidget = "Spinner";
    int UIMin = 4;
    int UIMax = 9;
> = {6};

float ui_GhostRoundness
<
    string UIName = "GHOST | Blade Roundness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.5};

float ui_GhostThinFilm
<
    string UIName = "GHOST | Thin-Film Coating";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.0};

// --- ALF Quality ---

float ui_ALFFalloff
<
    string UIName = "ALF | Distance Falloff";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 5.0;
    float UIStep = 0.1;
> = {1.0};

// --- Lens Character (physically-based optics) ---

int ui_LensPreset
<
    string UIName = "OPTICS | Lens Preset (0=Off 1=Cooke 2=Zeiss 3=MIR1 4=Primo)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 4;
> = {0};

float ui_CoatingQuality
<
    string UIName = "OPTICS | Coating Quality (0=uncoated, 1=multi-coat)";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.7};

float ui_DistortionStr
<
    string UIName = "OPTICS | Barrel Distortion Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.0};

float ui_SpectralCAStr
<
    string UIName = "OPTICS | Spectral CA Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.0};

float ui_VeilingStr
<
    string UIName = "OPTICS | Veiling Glare Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.0};

float ui_ThoriumYellowing
<
    string UIName = "OPTICS | Thorium Oxide Yellowing";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.0};

// --- Physical Vignette (cos⁴θ optical falloff) ---

bool ui_PhysicalVignette
<
    string UIName = "OPTICS | Physical Vignette (cos4)";
> = {false};

float ui_PhysicalVignetteStr
<
    string UIName = "OPTICS | Vignette Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {1.0};

// --- Sensor Clipping ---

bool ui_SensorClipping
<
    string UIName = "OPTICS | Sensor Clipping";
> = {false};

float ui_SensorClipLevel
<
    string UIName = "OPTICS | Clip Saturation Level";
    string UIWidget = "Spinner";
    float UIMin = 0.5; float UIMax = 5.0; float UIStep = 0.01;
> = {1.5};

// --- Lens Dirt ---

bool ui_DirtEnable
<
    string UIName = "DIRT | Enable Lens Dirt";
> = {false};

float ui_DirtIntensity
<
    string UIName = "DIRT | Intensity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {0.5};

float ui_DirtScale
<
    string UIName = "DIRT | Pattern Scale";
    string UIWidget = "Spinner";
    float UIMin = 1.0;
    float UIMax = 10.0;
    float UIStep = 0.1;
> = {3.0};

float ui_DirtRadialBias
<
    string UIName = "DIRT | Edge Bias";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.7};


//=== ADDON INCLUDES (after UI params — addons reference host variables) ===//

#include "Addons/Lens_SpectralEffects.fxh"


//=== HELPER FUNCTIONS ===//

// Boris circular box blur for downsample chain
float3 FuncBlur(Texture2D inputtex, float2 uv, float srcsize, float destsize)
{
    float scale = 4.0;
    float2 invtargetsize = scale / srcsize;
    invtargetsize.y *= ScreenSize.z;

    float2 fstepcount = srcsize * invtargetsize;
    fstepcount = clamp(fstepcount, 2.0, 16.0);

    int stepcountX = (int)(fstepcount.x + 0.4999);
    int stepcountY = (int)(fstepcount.y + 0.4999);

    fstepcount = 1.0 / fstepcount;
    float4 curr = float4(0.0, 0.0, 0.0, 0.000001);
    float2 pos;
    float2 halfstep = 0.5 * fstepcount.xy;
    pos.x = -0.5 + halfstep.x;
    invtargetsize *= 2.0;

    [loop]
    for (int x = 0; x < stepcountX; x++)
    {
        pos.y = -0.5 + halfstep.y;
        [loop]
        for (int y = 0; y < stepcountY; y++)
        {
            float2 coord = pos.xy * invtargetsize + uv.xy;
            float3 tempcurr = inputtex.SampleLevel(Sampler1, coord.xy, 0).xyz;
            float2 dpos = pos.xy * 2.0;
            float rangefactor = dot(dpos.xy, dpos.xy);
            float tempweight = saturate(1001.0 - 1000.0 * rangefactor);
            tempweight *= saturate(1.0 - rangefactor);
            // Anti-firefly: reduce bright pixel dominance in lens input
            tempweight /= (1.0 + dot(tempcurr.xyz, float3(0.2126, 0.7152, 0.0722)));
            curr.xyz += tempcurr.xyz * tempweight;
            curr.w += tempweight;
            pos.y += fstepcount.y;
        }
        pos.x += fstepcount.x;
    }

    curr.xyz /= curr.w;
    return curr.xyz;
}


//=== LENS HELPER FUNCTIONS ===//

// N-gon polygon SDF — returns normalized distance (1.0 = polygon edge)
float LensNGonSDF(float2 pos, int blades, float rotation, float roundness)
{
    float dist = length(pos);
    if (dist < 0.001) return 0.0;

    float angle = atan2(pos.y, pos.x) + rotation;
    float sector = 2.0 * PI / (float)blades;

    // Fold angle into single sector [−sector/2, sector/2]
    float a = frac(angle / sector + 0.5) * sector - sector * 0.5;

    // Distance to polygon edge in this sector
    float polyRadius = cos(sector * 0.5) / cos(a);

    // Blend polygon ↔ circle
    float effectiveRadius = lerp(polyRadius, 1.0, roundness);

    return dist / max(effectiveRadius, 0.001);
}

// Thin-film interference — iridescent coating reflection per wavelength
// Simulates MgF2 anti-reflection coating on each ghost element
float3 LensThinFilmColor(float incidence, float filmIndex)
{
    // RGB wavelengths in nm
    float3 lambda = float3(650.0, 550.0, 450.0);

    // Film thickness varies per ghost element (different coating layers)
    float thickness = 200.0 + filmIndex * 60.0; // nm

    // MgF2 coating refractive index
    float n = 1.38;

    // Snell's law inside coating
    float cosT = sqrt(max(1.0 - pow(sin(incidence) / n, 2), 0.0));

    // Optical path difference → constructive/destructive interference
    float opd = 2.0 * n * thickness * cosT;
    float3 phase = 2.0 * PI * opd / lambda;
    return 0.5 + 0.5 * cos(phase);
}

// Value noise with smoothstep interpolation (for lens dirt)
float LensDirtNoise(float2 uv)
{
    float2 cell = floor(uv);
    float2 f = frac(uv);
    f = f * f * (3.0 - 2.0 * f); // smoothstep

    float a = frac(sin(dot(cell,                  float2(127.1, 311.7))) * 43758.5453);
    float b = frac(sin(dot(cell + float2(1, 0),   float2(127.1, 311.7))) * 43758.5453);
    float c = frac(sin(dot(cell + float2(0, 1),   float2(127.1, 311.7))) * 43758.5453);
    float d = frac(sin(dot(cell + float2(1, 1),   float2(127.1, 311.7))) * 43758.5453);

    return lerp(lerp(a, b, f.x), lerp(c, d, f.x), f.y);
}

// Procedural lens dirt: multi-octave noise with radial edge bias
float LensDirtPattern(float2 uv, float scale, float radialBias)
{
    float n1 = LensDirtNoise(uv * scale);
    float n2 = LensDirtNoise(uv * scale * 1.73 + 13.37);
    float n3 = LensDirtNoise(uv * scale * 3.11 + 27.19);

    // Smudge pattern: combine noise layers with contrast boost
    float dirt = n1 * n2 * 4.0 + n3 * 0.3;
    dirt = pow(saturate(dirt), 0.7);

    // Radial bias (more dirt at edges, like real lens)
    float2 c = uv * 2.0 - 1.0;
    c.x *= ScreenSize.z;
    float r = length(c);
    float radial = saturate(pow(r, 1.5) * radialBias + (1.0 - radialBias) * 0.3);

    return dirt * radial;
}


//=== VERTEX SHADER ===//

void VS_Lens(inout float4 pos : SV_POSITION, inout float2 txcoord : TEXCOORD0)
{
    pos.w = 1.0;
}


//=== PIXEL SHADERS ===//

// Pass 0: Downsample + soft threshold 1024 → 512
float4 PS_Down512(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = FuncBlur(TextureDownsampled, txcoord, 1024.0, 512.0);

    // Karis-style soft-knee threshold
    float luma = dot(color, N_LUM);
    float knee = ui_Threshold * ui_ThreshSoftKnee;
    float soft = luma - ui_Threshold + knee;
    soft = clamp(soft, 0.0, 2.0 * knee);
    soft = soft * soft / (4.0 * knee + 1e-5);
    float contribution = max(soft, luma - ui_Threshold) / max(luma, 1e-5);
    color *= max(contribution, 0.0);

    return float4(max(color, 0.0), 1.0);
}

// Pass 1: Downsample 512 → 256
float4 PS_Down256(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    return float4(FuncBlur(RenderTarget512, txcoord, 512.0, 256.0), 1.0);
}

// Pass 2: Downsample 256 → 128
float4 PS_Down128(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    return float4(FuncBlur(RenderTarget256, txcoord, 256.0, 128.0), 1.0);
}

// Pass 3: Downsample 128 → 32 (for halo extra blur)
float4 PS_Down32(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    return float4(FuncBlur(RenderTarget128, txcoord, 128.0, 32.0), 1.0);
}


// Pass 4: ALF streaks + ghost reflections + halo + dirt → final lens output
// NOTE: Output must be lens effects ONLY (no scene). enbeffect.fx ADDs TextureLens to the scene.
float4 PS_Compose(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float intensity = lerp(lerp(ui_LensIntensityNight, ui_LensIntensityDay, ENightDayFactor),
                           ui_LensIntensityInterior, EInteriorFactor);

    float3 lensResult = 0.0;

    // --- ALF: Anamorphic Lens Flare with distance falloff + Gaussian cross-section ---
    if (ui_ALFIntensity > 0.001)
    {
        float rad = ui_ALFAngle * (PI / 180.0);
        float cs = cos(rad);
        float sn = sin(rad);

        // Perpendicular direction for Gaussian cross-section
        float2 perpDir = float2(-sn, cs);

        float3 alfColor = 0.0;
        float alfTotal = 0.0;

        [unroll]
        for (int i = 0; i < ALF_SAMPLE; i++)
        {
            float t = float(i) - float(ALF_SAMPLE - 1) * ALF_OFFSET;
            float2 offset = float2(cs * t, sn * t) * ui_ALFWidth / MAX_LENS_RES;

            // Exponential distance falloff: center samples contribute more
            float distWeight = exp(-abs(t) * ui_ALFFalloff / float(ALF_SAMPLE));

            float2 coord1 = txcoord + offset;
            if (all(coord1 > 0.0) && all(coord1 < 1.0))
            {
                alfColor += RenderTarget512.SampleLevel(Sampler1, coord1, 0).rgb * distWeight;
                alfTotal += distWeight;
            }

            // Secondary streak (mirrored through center)
            float2 coord2 = 1.0 - coord1;
            if (all(coord2 > 0.0) && all(coord2 < 1.0))
            {
                alfColor += RenderTarget512.SampleLevel(Sampler1, coord2, 0).rgb
                          * ui_ALFSecondary * distWeight;
                alfTotal += ui_ALFSecondary * distWeight;
            }
        }
        alfColor /= max(alfTotal, 0.001);

        // Gaussian cross-section perpendicular to streak (narrower = sharper streak)
        float perpDist = abs(dot(txcoord - 0.5, perpDir));
        float crossSection = exp(-perpDist * perpDist * 50.0);
        alfColor *= crossSection;

        // Anamorphic blue tint
        float3 tint = lerp(1.0, float3(0.7, 0.85, 1.25), ui_ALFTint);
        alfColor *= tint;

        lensResult += alfColor * ui_ALFIntensity;
    }

    // --- Ghost reflections ---
    // When lens preset is active: polynomial/ABCD physically-based optics (8 ghosts)
    // Otherwise: classic N-gon shaped with thin-film coating (6 ghosts)
    if (ui_GhostIntensity > 0.001)
    {
        float2 ghostVec = (0.5 - txcoord) * 2.0;

        // Coating-based ghost brightness (physically derived)
        float coatingGhostMul = (ui_LensPreset > 0) ? GetGhostIntensity(ui_CoatingQuality) / GetGhostIntensity(0.7) : 1.0;

        if (ui_LensPreset > 0)
        {
            // === Physically-based ghost rendering (polynomial + ABCD hybrid) ===
            float2 centeredUV = txcoord * 2.0 - 1.0;
            centeredUV.x *= ScreenSize.z; // aspect correct

            // Light direction: use center-mirror vector as proxy for dominant light
            float2 lightDir = normalize(-centeredUV + 0.001);

            // Select preset arrays
            int presetIdx = ui_LensPreset - 1; // 0-3

            // Ghost tint palette (per-element spectral color from coating interference)
            static const float3 ghostTints[8] =
            {
                float3(1.0, 0.9, 0.8),    // warm
                float3(0.8, 0.9, 1.0),    // cool
                float3(0.9, 1.0, 0.85),   // green
                float3(1.0, 0.85, 0.9),   // magenta
                float3(0.95, 0.95, 1.0),  // neutral cool
                float3(1.0, 0.95, 0.9),   // neutral warm
                float3(0.85, 0.95, 1.0),  // hero 1: blue-tinted
                float3(1.0, 0.88, 0.75),  // hero 2: amber-tinted
            };

            // 6 ABCD ghosts (fast, first-order accurate)
            [unroll]
            for (int a = 0; a < 6; a++)
            {
                GhostABCD abcd;
                if      (presetIdx == 0) abcd = COOKE_ABCD[a];
                else if (presetIdx == 1) abcd = ZEISS_ABCD[a];
                else if (presetIdx == 2) abcd = MIR1_ABCD[a];
                else                     abcd = PRIMO_ABCD[a];

                float2 ghostUV = EvalGhostABCD(abcd, centeredUV, lightDir);

                // Aperture mask
                float mask = GhostApertureMask(ghostUV, 1.2);
                if (mask < 0.001) continue;

                // Map back to [0,1]
                float2 sampleUV;
                sampleUV.x = ghostUV.x / ScreenSize.z;
                sampleUV = sampleUV * 0.5 + 0.5;

                // Chromatic sampling
                float3 ghost;
                ghost.r = RenderTarget128.SampleLevel(Sampler1,
                           sampleUV + ghostVec * ui_GhostDispersion * 0.01, 0).r;
                ghost.g = RenderTarget128.SampleLevel(Sampler1, sampleUV, 0).g;
                ghost.b = RenderTarget128.SampleLevel(Sampler1,
                           sampleUV - ghostVec * ui_GhostDispersion * 0.01, 0).b;

                // Thin-film coating
                if (ui_GhostThinFilm > 0.001)
                {
                    float incidence = saturate(length(ghostUV) * 0.8);
                    float3 filmColor = LensThinFilmColor(incidence, float(a));
                    ghost *= lerp(1.0, filmColor * 1.8, ui_GhostThinFilm);
                }

                float ghostStr = abcd.intensity * ui_GhostIntensity * coatingGhostMul;
                lensResult += ghost * ghostTints[a] * mask * ghostStr;
            }

            // 2 hero ghosts (polynomial, higher-order aberrations)
            [unroll]
            for (int h = 0; h < 2; h++)
            {
                LensPolyCoeffs coeffs;
                if      (presetIdx == 0) coeffs = COOKE_HERO[h];
                else if (presetIdx == 1) coeffs = ZEISS_HERO[h];
                else if (presetIdx == 2) coeffs = MIR1_HERO[h];
                else                     coeffs = PRIMO_HERO[h];

                float2 ghostUV = EvalGhostPoly(coeffs, centeredUV, lightDir);

                float mask = GhostApertureMask(ghostUV, 1.4);
                if (mask < 0.001) continue;

                float2 sampleUV;
                sampleUV.x = ghostUV.x / ScreenSize.z;
                sampleUV = sampleUV * 0.5 + 0.5;

                float3 ghost;
                ghost.r = RenderTarget128.SampleLevel(Sampler1,
                           sampleUV + ghostVec * ui_GhostDispersion * 0.015, 0).r;
                ghost.g = RenderTarget128.SampleLevel(Sampler1, sampleUV, 0).g;
                ghost.b = RenderTarget128.SampleLevel(Sampler1,
                           sampleUV - ghostVec * ui_GhostDispersion * 0.015, 0).b;

                if (ui_GhostThinFilm > 0.001)
                {
                    float incidence = saturate(length(ghostUV) * 0.6);
                    float3 filmColor = LensThinFilmColor(incidence, float(h) + 6.0);
                    ghost *= lerp(1.0, filmColor * 2.0, ui_GhostThinFilm);
                }

                float heroStr = ui_GhostIntensity * 1.5 * coatingGhostMul;
                lensResult += ghost * ghostTints[6 + h] * mask * heroStr;
            }
        }
        else
        {
            // === Classic ghost rendering (original N-gon method) ===
            static const float ghostScales[6] = { 1.0, -0.8, 0.6, -0.5, 0.4, -0.3 };
            float ghostRot = PI / max((float)ui_GhostBlades, 1.0);

            [unroll]
            for (int g = 0; g < 6; g++)
            {
                float2 gOffset = txcoord + ghostVec * (float(g + 1) * ui_GhostSpacing * ghostScales[g]);

                float2 gLocal = (gOffset - 0.5) * 2.0;
                gLocal.x *= ScreenSize.z;
                float ngonDist = LensNGonSDF(gLocal, ui_GhostBlades,
                                              ghostRot * float(g + 1), ui_GhostRoundness);
                float gWeight = 1.0 - smoothstep(0.5, 1.0, ngonDist);
                gWeight *= gWeight;

                if (gWeight < 0.001) continue;

                float3 ghost;
                ghost.r = RenderTarget128.SampleLevel(Sampler1,
                           gOffset + ghostVec * ui_GhostDispersion * 0.01, 0).r;
                ghost.g = RenderTarget128.SampleLevel(Sampler1, gOffset, 0).g;
                ghost.b = RenderTarget128.SampleLevel(Sampler1,
                           gOffset - ghostVec * ui_GhostDispersion * 0.01, 0).b;

                if (ui_GhostThinFilm > 0.001)
                {
                    float incidence = saturate(ngonDist * 1.2);
                    float3 filmColor = LensThinFilmColor(incidence, float(g));
                    ghost *= lerp(1.0, filmColor * 1.8, ui_GhostThinFilm);
                }

                lensResult += ghost * gWeight * ui_GhostIntensity * abs(ghostScales[g]);
            }
        }
    }

    // --- Halo ring with multi-radius smoothing and optional CA ---
    if (ui_HaloIntensity > 0.001)
    {
        float2 ghostVec = (0.5 - txcoord) * 2.0;
        float gvLen = length(ghostVec);
        float2 haloDir = (gvLen > 0.001) ? ghostVec / gvLen : 0.0;

        // Multi-radius averaging (3 samples at slightly different widths for smoother ring)
        float3 halo = 0.0;
        [unroll]
        for (int h = 0; h < 3; h++)
        {
            float radiusScale = 1.0 + (float(h) - 1.0) * 0.08;
            float2 haloVec = haloDir * ui_HaloWidth * radiusScale;

            if (ui_HaloCA > 0.001)
            {
                float2 caOffset = haloDir * ui_HaloCA * 0.05;
                halo.r += RenderTarget128.SampleLevel(Sampler1, txcoord + haloVec + caOffset, 0).r;
                halo.g += RenderTarget128.SampleLevel(Sampler1, txcoord + haloVec, 0).g;
                halo.b += RenderTarget128.SampleLevel(Sampler1, txcoord + haloVec - caOffset, 0).b;
            }
            else
            {
                halo += RenderTarget128.SampleLevel(Sampler1, txcoord + haloVec, 0).rgb;
            }
        }
        halo /= 3.0;

        float haloWeight = length(0.5 - txcoord);
        haloWeight = saturate(1.0 - haloWeight * 3.0) * saturate(haloWeight * 5.0 - 1.0);
        lensResult += halo * haloWeight * ui_HaloIntensity;
    }

    // --- Procedural lens dirt (backlit grime overlay) ---
    if (ui_DirtEnable)
    {
        float3 brightData = RenderTarget128.SampleLevel(Sampler1, txcoord, 0).rgb;
        float brightLuma = dot(brightData, N_LUM);
        if (brightLuma > 0.01)
        {
            float dirt = LensDirtPattern(txcoord, ui_DirtScale, ui_DirtRadialBias);
            // Dirt visible only when backlit by bright areas (like real lens grime)
            lensResult += brightData * dirt * ui_DirtIntensity * sqrt(brightLuma);
        }
    }

    // --- Veiling glare (bloom-mip proxy, coating-dependent) ---
    if (ui_VeilingStr > 0.001 && ui_LensPreset > 0)
    {
        float veilFrac = ComputeVeilingFraction(ui_CoatingQuality);
        // Global: lowest available mip = scene-wide average brightness (1/r^2 PSF far-field)
        float3 globalGlare = RenderTarget32.SampleLevel(Sampler1, float2(0.5, 0.5), 0).rgb;
        // Local: mid-level mip preserves spatial variation (near-field PSF)
        float3 localGlare = RenderTarget128.SampleLevel(Sampler1, txcoord, 0).rgb;
        float3 veil = globalGlare * 0.7 + localGlare * 0.3;
        lensResult += veil * veilFrac * ui_VeilingStr;
    }

    // Lens saturation control
    float lensLuma = dot(lensResult, N_LUM);
    lensResult = lerp(lensLuma, lensResult, ui_LensSaturation);

    // Output lens effects ONLY — no scene. enbeffect.fx adds this to the scene.
    return float4(max(lensResult * intensity, 0.0), 1.0);
}


//=== TECHNIQUES ===//

// Pass 0: Threshold + downsample → RenderTarget512
technique11 Draw <string UIName = "EotE: Lens"; string RenderTarget="RenderTarget512";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Down512()));
    }
}

// Pass 1: Downsample → RenderTarget256
technique11 Draw1 <string RenderTarget="RenderTarget256";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Down256()));
    }
}

// Pass 2: Downsample → RenderTarget128
technique11 Draw2 <string RenderTarget="RenderTarget128";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Down128()));
    }
}

// Pass 3: Downsample → RenderTarget32
technique11 Draw3 <string RenderTarget="RenderTarget32";>
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Down32()));
    }
}

// Pass 4: Ghost + ALF streaks + halo → final output (becomes TextureLens)
technique11 Draw4
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_Compose()));
    }
}

// Pass 5: Spectral effects (diffraction starburst + veil glare)
technique11 Draw5
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_LensSpectral()));
    }
}


//=== PASS 6: BROWN-CONRADY DISTORTION + SPECTRAL CA ===//
// Fused pass: applies barrel/pincushion distortion and 6-band spectral
// chromatic aberration in a single pixel shader invocation.
// Both effects disabled by default (strength = 0).

float4 PS_LensDistortionCA(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 color = TextureColor.SampleLevel(Sampler0, txcoord, 0).rgb;

    bool hasDistortion = (ui_DistortionStr > 0.001 && ui_LensPreset > 0);
    bool hasSpectralCA = (ui_SpectralCAStr > 0.001);

    if (!hasDistortion && !hasSpectralCA)
        return float4(color, 1.0);

    float2 uv = txcoord;
    // ENB ScreenSize: .x=width, .y=1/width, .z=width/height, .w=height
    float aspectRatio = ScreenSize.z;

    // Step 1: Brown-Conrady distortion
    if (hasDistortion)
    {
        int presetIdx = clamp(ui_LensPreset - 1, 0, 3);
        uv = ApplyBrownConrady(txcoord, DISTORTION_PRESETS[presetIdx], aspectRatio, ui_DistortionStr);
    }

    // Step 2: 6-band spectral CA (applied after distortion in optical path)
    if (hasSpectralCA)
    {
        float2 center = hasDistortion
            ? ApplyBrownConrady(float2(0.5, 0.5), DISTORTION_PRESETS[clamp(ui_LensPreset - 1, 0, 3)], aspectRatio, ui_DistortionStr)
            : float2(0.5, 0.5);

        // FOV scale: wider FOV = more CA (telephoto = less)
        float fovScale = saturate(FieldOfView / 90.0) * 1.5;

        color = EvalSpectralCA(TextureColor, Sampler1, uv, center, ui_SpectralCAStr, fovScale);
    }
    else if (hasDistortion)
    {
        // Distortion only: just resample at distorted UV
        color = TextureColor.SampleLevel(Sampler1, uv, 0).rgb;
    }

    // Border mask to prevent edge clamping artifacts
    if (hasDistortion)
    {
        color *= DistortionBorderMask(uv);
    }

    // Thorium oxide yellowing: vintage radioactive glass F-center absorption
    if (ui_ThoriumYellowing > 0.001)
        color = ThoriumYellowing(color, ui_ThoriumYellowing);

    // Physical cos⁴θ vignette: natural light falloff from oblique incidence.
    // cos⁴(θ) where θ = angle from optical axis. Trig-free form:
    // cos²θ = 1/(1 + r²·tan²(FOV/2)), cos⁴θ = cos²θ².
    // Wide-angle lenses exhibit stronger vignetting than telephoto.
    // Ref: Smith "Modern Optical Engineering" Ch. 6
    if (ui_PhysicalVignette)
    {
        float2 centered = txcoord - 0.5;
        centered.x *= aspectRatio;
        float r2 = dot(centered, centered);
        float tanHFov = tan(FieldOfView * 0.5 * 3.14159 / 180.0);
        float cos2theta = 1.0 / (1.0 + r2 * tanHFov * tanHFov);
        float cos4theta = cos2theta * cos2theta;
        color *= lerp(1.0, cos4theta, ui_PhysicalVignetteStr);
    }

    // Sensor clipping: per-channel soft saturation modeling photosite capacity.
    // Green channel clips earliest (highest quantum efficiency on Bayer sensors),
    // producing pink/magenta highlight clipping characteristic of digital cameras.
    // Uses tanh for smooth rolloff instead of hard clip.
    // Ref: Hasinoff et al. 2010 "Noise-Optimal Capture for High Dynamic Range Photography"
    if (ui_SensorClipping)
    {
        // Per-channel saturation levels (G < R < B) modeling sensor QE response
        float3 satLevel = float3(1.0, 0.92, 1.05) * ui_SensorClipLevel;
        color = float3(
            tanh(color.r / satLevel.r) * satLevel.r,
            tanh(color.g / satLevel.g) * satLevel.g,
            tanh(color.b / satLevel.b) * satLevel.b
        );
    }

    return float4(max(color, 0.0), 1.0);
}

// Pass 6: Distortion + Spectral CA
technique11 Draw6
{
    pass p0
    {
        SetVertexShader(CompileShader(vs_5_0, VS_Lens()));
        SetPixelShader(CompileShader(ps_5_0, PS_LensDistortionCA()));
    }
}
