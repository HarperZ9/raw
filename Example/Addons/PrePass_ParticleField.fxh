//=============================================================================
//  PrePass_ParticleField.fxh — Screen-Space Particle Effects v1.0
//
//  Procedural atmospheric particles for enbeffectprepass.fx:
//    Mode 0: Motes      — slow drift, small, white/gold, gentle sine wave
//    Mode 1: Fireflies  — pulsing brightness, warm yellow-green, slow random
//    Mode 2: Embers/Ash — falling downward with drift, orange-red glow
//    Mode 3: Dust       — very small, dense, brownish, slight drift
//    Mode 4: Snow       — falling downward with wind, white, medium density
//
//  Grid-based hash particle system: screen divided into cells, each cell
//  spawns 0-2 particles with hash-derived position, size, brightness.
//  Additive blend onto scene (HDR float16). Depth-tested against geometry.
//
//  SkyrimBridge integration: wind, time of day, ambient color tinting.
//  All features disabled by default.
//
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef _PREPASS_PARTICLEFIELD_
#define _PREPASS_PARTICLEFIELD_

#define _PARTICLE_FIELD_
#define PARTICLES_LOADED 1


//=== CONSTANTS ===//

static const float PF_PI    = 3.14159265;
static const float PF_TAU   = 6.28318530;
static const float PF_SQRT2 = 1.41421356;


//=== UI PARAMETERS ===//

bool ui_ParticleEnable
<
    string UIName = "PARTICLES | Enable";
> = {false};

int ui_ParticleMode
<
    string UIName = "PARTICLES | Mode (0=Motes 1=Firefly 2=Ember 3=Dust 4=Snow)";
    string UIWidget = "Spinner";
    int UIMin = 0; int UIMax = 4;
> = {0};

float ui_ParticleDensity
<
    string UIName = "PARTICLES | Density";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.4};

float ui_ParticleSize
<
    string UIName = "PARTICLES | Sprite Size";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.4};

float ui_ParticleSpeed
<
    string UIName = "PARTICLES | Animation Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 2.0; float UIStep = 0.01;
> = {0.5};

float3 ui_ParticleColor
<
    string UIName = "PARTICLES | Base Color";
    string UIWidget = "Color";
> = {1.0, 0.95, 0.85};

float ui_ParticleBrightness
<
    string UIName = "PARTICLES | Emission Strength";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 5.0; float UIStep = 0.01;
> = {0.5};

float ui_ParticleDepthFade
<
    string UIName = "PARTICLES | Depth Fadeout";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.5};

float ui_ParticleWind
<
    string UIName = "PARTICLES | Wind Influence";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.3};

bool ui_ParticleUseSB
<
    string UIName = "PARTICLES | Use SkyrimBridge Data";
> = {true};

float ui_ParticleFlicker
<
    string UIName = "PARTICLES | Flicker (fireflies/embers)";
    string UIWidget = "Spinner";
    float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01;
> = {0.6};


//=== HASH FUNCTIONS ===//
// PCG-style hashing for deterministic per-cell particle properties.
// Each function returns [0,1]. Unique names to avoid collisions with host.

float PF_Hash11(float p)
{
    float3 p3 = frac(float3(p, p, p) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float PF_Hash21(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 PF_Hash22(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float3 PF_Hash23(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yxz + 33.33);
    return frac((p3.xxy + p3.yzz) * p3.zyx);
}


//=== MODE PRESETS ===//
// Returns: float4(gravity, driftAmplitude, driftFrequency, sizeMultiplier)

float4 PF_GetModeMotion(int mode)
{
    // Motes: no gravity, gentle drift
    if (mode == 0) return float4(0.0, 0.6, 0.8, 1.0);
    // Fireflies: no gravity, slow random
    if (mode == 1) return float4(0.0, 0.4, 0.5, 1.3);
    // Embers: falling with drift
    if (mode == 2) return float4(0.5, 0.3, 1.2, 0.8);
    // Dust: no gravity, minimal drift
    if (mode == 3) return float4(0.0, 0.15, 0.3, 0.5);
    // Snow: moderate gravity, wind drift
    return float4(0.35, 0.5, 0.6, 1.1);
}

// Returns: float3 default color per mode (used when SB ambient not available)
float3 PF_GetModeColor(int mode)
{
    if (mode == 0) return float3(1.0, 0.97, 0.88);    // Motes: warm white/gold
    if (mode == 1) return float3(0.7, 0.95, 0.3);     // Fireflies: yellow-green
    if (mode == 2) return float3(1.0, 0.5, 0.15);     // Embers: orange-red
    if (mode == 3) return float3(0.75, 0.68, 0.55);    // Dust: brownish
    return float3(0.92, 0.95, 1.0);                    // Snow: cool white
}

// Returns: float2(minBrightness, maxBrightness) per mode
float2 PF_GetModeBrightness(int mode)
{
    if (mode == 0) return float2(0.3, 1.0);    // Motes: moderate variation
    if (mode == 1) return float2(0.1, 1.5);    // Fireflies: wide range (pulsing)
    if (mode == 2) return float2(0.5, 1.8);    // Embers: bright glow
    if (mode == 3) return float2(0.2, 0.5);    // Dust: subtle
    return float2(0.4, 0.8);                   // Snow: moderate
}

// Returns: float density scale per mode
float PF_GetModeDensity(int mode)
{
    if (mode == 0) return 0.6;     // Motes: sparse
    if (mode == 1) return 0.4;     // Fireflies: sparse
    if (mode == 2) return 0.5;     // Embers: moderate
    if (mode == 3) return 1.0;     // Dust: dense
    return 0.7;                    // Snow: moderate-dense
}


//=== PARTICLE EVALUATION ===//
//
// For a given screen UV, evaluate a single grid cell's particle contribution.
// Returns additive color (0 if no particle or occluded).
//
// cellID:    integer cell coordinate
// screenUV:  pixel's screen UV [0,1]
// time:      animated time value
// windUV:    wind bias in UV space
// motion:    mode motion params (gravity, drift amp/freq, size mult)
// virtDepth: output virtual depth of particle (for occlusion)

float3 PF_EvalParticle(
    int2   cellID,
    float2 screenUV,
    float  time,
    float2 windUV,
    float4 motion,
    float2 cellSize,
    float  particleSeed,
    out float virtDepth
)
{
    virtDepth = 1.0;

    // Per-cell random seed
    float2 seed = float2(cellID) + particleSeed;
    float  existence = PF_Hash21(seed * 1.731);

    // Density threshold: particles only spawn in some cells
    float densityThresh = ui_ParticleDensity * PF_GetModeDensity(ui_ParticleMode);
    if (existence > densityThresh)
        return 0.0;

    // Per-particle properties from hash
    float2 basePos  = PF_Hash22(seed * 2.137);           // [0,1] within cell
    float3 props    = PF_Hash23(seed * 3.519);            // .x=size, .y=phase, .z=brightness
    float  lifeHash = PF_Hash21(seed * 4.891);            // lifecycle offset

    // Size: base + per-particle variation, scaled by mode and UI
    float radius = lerp(0.3, 1.0, props.x) * motion.w * ui_ParticleSize;
    // Map to pixel radius: 1.0 UI = ~6px at 1080p
    float pixelRadius = radius * 6.0 * PixelSize.x;

    // Animated position offset
    float phaseOffset = props.y * PF_TAU;
    float tAnim = time * ui_ParticleSpeed;

    // Drift: sine wave in X, mode-dependent gravity in Y
    float driftX = sin(tAnim * motion.z + phaseOffset) * motion.y;
    float driftY = -motion.x * frac(tAnim * 0.15 + lifeHash);  // gravity: downward cycling

    // For falling modes (embers/snow), wrap Y position cyclically
    float gravWrap = 0.0;
    if (motion.x > 0.01)
        gravWrap = frac(tAnim * 0.15 * motion.x + lifeHash);
    else
        driftY = sin(tAnim * motion.z * 0.7 + phaseOffset * 1.3) * motion.y * 0.5;

    // Compose particle UV position
    float2 particleUV;
    particleUV.x = (float(cellID.x) + basePos.x + driftX * 0.3 + windUV.x) * cellSize.x;
    if (motion.x > 0.01)
        particleUV.y = (float(cellID.y) + frac(basePos.y + gravWrap) + windUV.y * 0.3) * cellSize.y;
    else
        particleUV.y = (float(cellID.y) + basePos.y + driftY * 0.3 + windUV.y * 0.15) * cellSize.y;

    // Wrap to [0,1] for seamless tiling
    particleUV = frac(particleUV);

    // Distance from pixel to particle center (in UV space, aspect-corrected)
    float2 delta = screenUV - particleUV;
    delta.x *= ScreenSize.z; // aspect correction
    float dist = length(delta);

    // Early out if pixel is outside particle radius
    if (dist > pixelRadius)
        return 0.0;

    // Smooth circular falloff (soft sprite)
    float falloff = 1.0 - smoothstep(0.0, pixelRadius, dist);
    falloff *= falloff; // quadratic for softer edges

    // Virtual depth for this particle: hash-based, biased toward mid-range
    virtDepth = lerp(0.02, 0.6, PF_Hash21(seed * 5.237));

    // Per-particle brightness variation
    float2 brightRange = PF_GetModeBrightness(ui_ParticleMode);
    float baseBright = lerp(brightRange.x, brightRange.y, props.z);

    // Firefly flicker: strong pulsing via sin wave with random phase
    float flicker = 1.0;
    if (ui_ParticleMode == 1)
    {
        float pulse = sin(tAnim * 2.5 + phaseOffset * 3.0) * 0.5 + 0.5;
        float blink = smoothstep(0.3, 0.7, pulse);
        // Occasional full-off periods
        float offCycle = sin(tAnim * 0.4 + lifeHash * PF_TAU) * 0.5 + 0.5;
        offCycle = smoothstep(0.2, 0.5, offCycle);
        flicker = lerp(1.0, blink * offCycle, ui_ParticleFlicker);
    }

    // Ember flicker: irregular brightness variation
    if (ui_ParticleMode == 2)
    {
        float emberPulse = sin(tAnim * 3.7 + phaseOffset * 2.0) *
                           sin(tAnim * 1.3 + lifeHash * PF_TAU);
        emberPulse = emberPulse * 0.5 + 0.5;
        flicker = lerp(1.0, 0.3 + emberPulse * 0.7, ui_ParticleFlicker);
    }

    float brightness = baseBright * flicker * ui_ParticleBrightness;

    return falloff * brightness;
}


//=== PIXEL SHADER ===//

float4 PS_ParticleField(float4 pos : SV_POSITION, float2 txcoord : TEXCOORD0) : SV_Target
{
    float3 scene = TextureColor.SampleLevel(smpLinear, txcoord, 0).rgb;

    // Early out if disabled
    if (!ui_ParticleEnable)
        return float4(scene, 1.0);

    // --- Time source ---
    float time = Timer.x * 0.001; // Timer.x is milliseconds, convert to seconds

    // --- Wind ---
    float2 windUV = float2(0.0, 0.0);
    float windSpeed = 0.0;

    // SkyrimBridge wind integration
    bool sbActive = false;
    if (ui_ParticleUseSB)
    {
        sbActive = SB_IsActive();
    }

    if (sbActive)
    {
        // SB_Wind: .x = speed [0,1], .y = direction (radians)
        windSpeed = SB_Wind.x;
        float windDir = SB_Wind.y;
        windUV = float2(cos(windDir), sin(windDir)) * windSpeed * ui_ParticleWind * time * 0.1;
    }
    else
    {
        // Fallback: gentle procedural wind
        windUV = float2(
            sin(time * 0.13) * 0.3,
            cos(time * 0.09) * 0.15
        ) * ui_ParticleWind * time * 0.05;
    }

    // --- SkyrimBridge time-of-day modulation ---
    float todMask = 1.0;
    if (sbActive)
    {
        float gameHour = SB_Time.x;
        // Fireflies: night only (fade in at dusk, fade out at dawn)
        if (ui_ParticleMode == 1)
        {
            float sunsetH = SB_Time.z;
            float sunriseH = SB_Time.y;
            // Night = after sunset or before sunrise
            float nightFade = 1.0;
            if (gameHour > sunriseH && gameHour < sunsetH)
            {
                // Daytime: fade based on distance from sunset/sunrise
                float distToNight = min(gameHour - sunriseH, sunsetH - gameHour);
                nightFade = 1.0 - saturate(distToNight / 2.0);
            }
            todMask = nightFade;
        }
        // Dust: slightly reduced at night (less visibility)
        if (ui_ParticleMode == 3)
        {
            todMask = SB_IsNight() ? 0.6 : 1.0;
        }
    }

    // Zero contribution if time-of-day says no
    if (todMask < 0.01)
        return float4(scene, 1.0);

    // --- Grid setup ---
    // Grid resolution: density-scaled, capped at 50x30 for performance
    float densityScale = 0.3 + ui_ParticleDensity * 0.7;
    int gridX = (int)lerp(15.0, 50.0, densityScale);
    int gridY = (int)lerp(9.0, 30.0, densityScale);
    float2 cellSize = float2(1.0 / (float)gridX, 1.0 / (float)gridY);

    // Which cell does this pixel fall into?
    int2 pixelCell = int2(txcoord * float2(gridX, gridY));

    // --- Scene depth at this pixel ---
    float sceneDepth = GetLinearDepth(txcoord);

    // --- Accumulate particle contributions ---
    // Check the pixel's own cell and the 8 neighbors (3x3 kernel)
    // to catch particles whose sprite radius crosses cell boundaries.
    float3 totalParticle = 0.0;

    [unroll]
    for (int oy = -1; oy <= 1; oy++)
    {
        [unroll]
        for (int ox = -1; ox <= 1; ox++)
        {
            int2 cell = pixelCell + int2(ox, oy);

            // Wrap cell coordinates for seamless tiling
            int2 wrappedCell = int2(
                ((cell.x % gridX) + gridX) % gridX,
                ((cell.y % gridY) + gridY) % gridY
            );

            // Evaluate up to 2 particles per cell (second uses offset seed)
            float virtDepth1;
            float3 p1 = PF_EvalParticle(
                wrappedCell, txcoord, time, windUV,
                PF_GetModeMotion(ui_ParticleMode),
                cellSize, 0.0, virtDepth1
            );

            // Depth test: particle occluded if geometry is closer
            float depthFade1 = saturate((sceneDepth - virtDepth1) / max(0.01, ui_ParticleDepthFade * 0.3));
            // Sky pixels (depth ~1.0) always show particles
            if (sceneDepth > 0.99) depthFade1 = 1.0;
            totalParticle += p1 * depthFade1;

            // Second particle per cell (only at higher densities)
            if (ui_ParticleDensity > 0.5)
            {
                float virtDepth2;
                float3 p2 = PF_EvalParticle(
                    wrappedCell, txcoord, time, windUV,
                    PF_GetModeMotion(ui_ParticleMode),
                    cellSize, 7.913, virtDepth2
                );
                float depthFade2 = saturate((sceneDepth - virtDepth2) / max(0.01, ui_ParticleDepthFade * 0.3));
                if (sceneDepth > 0.99) depthFade2 = 1.0;
                totalParticle += p2 * depthFade2;
            }
        }
    }

    // --- Depth-based distance fade ---
    // Particles fade out on very close geometry (avoids pop-in on faces)
    float nearFade = smoothstep(0.005, 0.03, sceneDepth);
    totalParticle *= nearFade;

    // --- Color ---
    float3 particleColor = ui_ParticleColor * PF_GetModeColor(ui_ParticleMode);

    // SkyrimBridge ambient tinting
    if (sbActive)
    {
        float3 ambient = SB_Atmos_Ambient.rgb;
        float ambientLuma = dot(ambient, float3(0.2126, 0.7152, 0.0722));
        // Blend particle color with ambient (subtle tinting, not full replace)
        if (ambientLuma > 0.01)
        {
            float3 ambientNorm = ambient / max(ambientLuma, 0.01);
            particleColor = lerp(particleColor, particleColor * ambientNorm, 0.35);
        }
    }

    // --- Time-of-day and final composite ---
    float3 emission = totalParticle * particleColor * todMask;

    // Additive blend: particles glow on top of scene
    float3 result = scene + emission;

    return float4(result, 1.0);
}


#endif // _PREPASS_PARTICLEFIELD_
