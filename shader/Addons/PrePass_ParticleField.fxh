//=============================================================================
//  PrePass_ParticleField.fxh — Screen-Space Atmospheric Particles
//
//  2-pass addon for enbeffectprepass.fx:
//    Pass A: Compute particle field (positions, sizes, colors) → render target
//    Pass B: Composite particles onto scene with depth awareness
//
//  Procedural particle system driven by hash functions — no textures needed.
//  Particle types: dust motes, embers, snow flakes, fireflies, ash.
//  Movement reacts to SkyrimBridge wind, precipitation, and time of day.
//
//  Author: Zain Dana Harper — March 2026
//=============================================================================

#ifndef PREPASS_PARTICLE_FIELD_FXH
#define PREPASS_PARTICLE_FIELD_FXH

#define PARTICLE_LOADED 1


//=== UI PARAMETERS ===//

int ui_Particle_Sep0
<
    string UIName = "===== PARTICLE FIELD =====";
    int UIMin = 0; int UIMax = 0;
> = {0};

bool ui_Particle_Enable
<
    string UIName = "Particles | Enable";
> = {true};

int ui_Particle_Type
<
    string UIName = "Particles | Type (0=Dust 1=Embers 2=Snow 3=Fireflies 4=Ash)";
    string UIWidget = "Spinner";
    int UIMin = 0;
    int UIMax = 4;
> = {0};

bool ui_Particle_AutoType
<
    string UIName = "Particles | Auto-Select by Weather";
> = {true};

float ui_Particle_Density
<
    string UIName = "Particles | Density";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {1.0};

float ui_Particle_Size
<
    string UIName = "Particles | Base Size (px)";
    string UIWidget = "Spinner";
    float UIMin = 0.5;
    float UIMax = 8.0;
    float UIStep = 0.1;
> = {2.5};

float ui_Particle_Brightness
<
    string UIName = "Particles | Brightness";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {1.0};

float ui_Particle_WindResponse
<
    string UIName = "Particles | Wind Influence";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 2.0;
    float UIStep = 0.01;
> = {0.5};

float ui_Particle_Speed
<
    string UIName = "Particles | Animation Speed";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 3.0;
    float UIStep = 0.01;
> = {1.0};

float ui_Particle_DepthFade
<
    string UIName = "Particles | Near Depth Fade";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 0.1;
    float UIStep = 0.001;
> = {0.005};

float ui_Particle_FarFade
<
    string UIName = "Particles | Far Depth Fade";
    string UIWidget = "Spinner";
    float UIMin = 0.1;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.5};

float ui_Particle_Opacity
<
    string UIName = "Particles | Overall Opacity";
    string UIWidget = "Spinner";
    float UIMin = 0.0;
    float UIMax = 1.0;
    float UIStep = 0.01;
> = {0.7};


//=== PARTICLE HELPERS ===//

// Integer hash (fast, high quality)
uint ParticleHash(uint x)
{
    x ^= x >> 16;
    x *= 0x45d9f3bu;
    x ^= x >> 16;
    x *= 0x45d9f3bu;
    x ^= x >> 16;
    return x;
}

// Float hash [0,1] from 2D integer coordinates
float Hash2D(int2 p)
{
    uint h = ParticleHash(uint(p.x) * 73856093u ^ uint(p.y) * 19349663u);
    return float(h & 0xFFFFu) / 65535.0;
}

// Float hash from single uint
float HashToFloat(uint h)
{
    return float(h & 0xFFFFu) / 65535.0;
}

// Soft circle SDF for particle rendering
float SoftCircle(float2 center, float2 uv, float radius)
{
    float dist = length(uv - center);
    return saturate(1.0 - dist / max(radius, 0.001));
}

// Get particle type based on weather (auto mode)
int GetAutoParticleType()
{
    #ifdef SKYRIMBRIDGE_FXH
        // Snow weather → snow particles
        if (SB_Weather_Flags.w > 0.5)
            return 2;
        // Rain weather → no auto particles (rain is handled elsewhere)
        if (SB_Weather_Flags.z > 0.5)
            return 0;  // Dust in rain looks like mist
        // Night + exterior + warm → fireflies
        if (SB_Time.x < 5.0 || SB_Time.x > 21.0)
            return 3;
        // Default: dust motes in sunlight
        return 0;
    #else
        // Fallback: use ENB Weather variable
        if (Weather.w > 0.5) return 2;       // Snow
        if (ENightDayFactor < 0.3) return 3;  // Night → fireflies
        return 0;                              // Default: dust
    #endif
}

// Get wind vector for particle movement
float2 GetWindVector()
{
    #ifdef SKYRIMBRIDGE_FXH
        float speed = SB_Wind.x;
        float dir = SB_Wind.y;  // radians
        return float2(cos(dir), sin(dir)) * speed;
    #else
        return float2(0.1, 0.05);  // Default gentle breeze
    #endif
}


//=== PIXEL SHADERS ===//

// Pass A: Compute particle field
// Output: RGB = particle color contribution, A = particle alpha
float4 PS_ParticleCompute(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    if (!ui_Particle_Enable)
        return float4(0, 0, 0, 0);

    int pType = ui_Particle_AutoType ? GetAutoParticleType() : ui_Particle_Type;

    float time = Timer.x * ui_Particle_Speed;
    float2 wind = GetWindVector() * ui_Particle_WindResponse;

    // Particle grid: divide screen into cells, one particle per cell
    float cellSize = 30.0 / ui_Particle_Density;  // Pixels per cell
    float2 pixelPos = pos.xy;

    // Number of particle layers for depth variation
    static const int LAYERS = 3;

    float3 totalColor = 0;
    float totalAlpha = 0;

    [unroll]
    for (int layer = 0; layer < LAYERS; layer++)
    {
        float layerDepth = 0.1 + float(layer) * 0.3;  // Virtual depth per layer
        float layerScale = 1.0 / (0.5 + float(layer) * 0.5);  // Parallax

        // Offset by wind and layer-specific drift
        float2 drift = wind * time * (0.5 + float(layer) * 0.3);
        float2 gravity = float2(0, 0);

        // Particle behavior by type
        if (pType == 0) // Dust: slow float with wind
        {
            drift += float2(sin(time * 0.3 + float(layer)), cos(time * 0.2)) * 5.0;
            gravity = float2(0, -0.5);
        }
        else if (pType == 1) // Embers: rise with turbulence
        {
            drift += float2(sin(time * 0.8 + float(layer) * 2.0) * 10.0, 0);
            gravity = float2(0, -20.0 * ui_Particle_Speed);
        }
        else if (pType == 2) // Snow: gentle fall with sway
        {
            drift += float2(sin(time * 0.4 + float(layer) * 1.5) * 8.0, 0);
            gravity = float2(0, 15.0 * ui_Particle_Speed);
        }
        else if (pType == 3) // Fireflies: random float
        {
            drift += float2(
                sin(time * 0.5 + float(layer) * 3.14) * 15.0,
                cos(time * 0.3 + float(layer) * 2.0) * 10.0
            );
        }
        else // Ash: drift down slowly
        {
            drift += float2(sin(time * 0.2 + float(layer)) * 6.0, 0);
            gravity = float2(0, 8.0 * ui_Particle_Speed);
        }

        float2 scrolledPos = pixelPos + drift + gravity * time;

        // Grid cell coordinates
        int2 cell = int2(floor(scrolledPos / cellSize));

        // Check this cell and 8 neighbors for particles that might overlap
        [unroll]
        for (int cy = -1; cy <= 1; cy++)
        {
            [unroll]
            for (int cx = -1; cx <= 1; cx++)
            {
                int2 checkCell = cell + int2(cx, cy);

                // Hash to get particle properties
                uint seed = ParticleHash(uint(checkCell.x + layer * 1000) * 73856093u ^
                                        uint(checkCell.y + layer * 2000) * 19349663u);

                // Particle exists? (density control)
                float existChance = HashToFloat(seed);
                if (existChance > ui_Particle_Density * 0.5)
                    continue;

                // Particle position within cell (jittered)
                float2 particlePos = (float2(checkCell) + 0.5) * cellSize;
                particlePos.x += (HashToFloat(ParticleHash(seed + 1u)) - 0.5) * cellSize * 0.8;
                particlePos.y += (HashToFloat(ParticleHash(seed + 2u)) - 0.5) * cellSize * 0.8;

                // Particle size with variation
                float size = ui_Particle_Size * (0.5 + HashToFloat(ParticleHash(seed + 3u)));

                // Flicker for embers and fireflies
                float flicker = 1.0;
                if (pType == 1 || pType == 3)
                {
                    float flickerPhase = HashToFloat(ParticleHash(seed + 4u)) * 6.283;
                    flicker = 0.3 + 0.7 * saturate(sin(time * 3.0 + flickerPhase));
                }

                // Distance from pixel to particle center
                float2 diff = pixelPos - particlePos;
                float dist = length(diff);

                if (dist > size * 2.0)
                    continue;

                // Soft circle falloff
                float alpha = SoftCircle(particlePos, pixelPos, size);
                alpha *= alpha;  // Squared for softer falloff
                alpha *= flicker;

                // Particle color by type
                float3 pColor;
                if (pType == 0)       // Dust: warm golden
                    pColor = float3(1.0, 0.95, 0.8);
                else if (pType == 1)  // Embers: hot orange-red
                    pColor = lerp(float3(1.0, 0.4, 0.1), float3(1.0, 0.8, 0.2),
                                 HashToFloat(ParticleHash(seed + 5u)));
                else if (pType == 2)  // Snow: cool white
                    pColor = float3(0.95, 0.97, 1.0);
                else if (pType == 3)  // Fireflies: warm yellow-green
                    pColor = lerp(float3(0.7, 1.0, 0.3), float3(1.0, 1.0, 0.5),
                                 HashToFloat(ParticleHash(seed + 5u)));
                else                  // Ash: grey
                    pColor = float3(0.6, 0.58, 0.55);

                // Tint by ambient light
                #ifdef SKYRIMBRIDGE_FXH
                    pColor *= lerp(float3(1,1,1), SB_Atmos_Ambient.rgb * 2.0, 0.3);
                #endif

                pColor *= ui_Particle_Brightness;

                totalColor += pColor * alpha;
                totalAlpha += alpha;
            }
        }
    }

    // Clamp accumulated alpha
    totalAlpha = saturate(totalAlpha);

    // Normalize color if we have contributions
    if (totalAlpha > 0.001)
        totalColor /= max(totalAlpha, 0.001);
    else
        totalColor = 0;

    return float4(totalColor, totalAlpha);
}

// Pass B: Composite particles onto scene with depth awareness
float4 PS_ParticleComposite(float4 pos : SV_POSITION, float2 uv : TEXCOORD0) : SV_Target
{
    float3 sceneColor = TextureColor.Sample(smpPoint, uv).rgb;

    if (!ui_Particle_Enable)
        return float4(sceneColor, 1.0);

    // Read particle data from compute pass
    float4 particleData = RenderTargetRGBA64F.Sample(smpLinear, uv);
    float3 pColor = particleData.rgb;
    float pAlpha = particleData.a;

    if (pAlpha < 0.001)
        return float4(sceneColor, 1.0);

    // Depth-based fade: don't render particles too close or too far
    float sceneDepth = GetLinearDepth(uv);

    // Near fade (avoid particles on the camera lens)
    float nearFade = saturate((sceneDepth - ui_Particle_DepthFade) /
                    max(ui_Particle_DepthFade, 0.001));

    // Far fade
    float farFade = 1.0 - saturate((sceneDepth - ui_Particle_FarFade) /
                   max(1.0 - ui_Particle_FarFade, 0.001));

    // Don't draw on sky
    if (sceneDepth > 0.99)
        nearFade = 0.0;

    float depthMask = nearFade * farFade;
    pAlpha *= depthMask * ui_Particle_Opacity;

    // Composite: additive for bright particles (embers, fireflies),
    // alpha blend for opaque particles (dust, snow, ash)
    int pType = ui_Particle_AutoType ? GetAutoParticleType() : ui_Particle_Type;

    float3 result;
    if (pType == 1 || pType == 3)  // Embers, fireflies: additive
    {
        result = sceneColor + pColor * pAlpha;
    }
    else  // Dust, snow, ash: alpha blend
    {
        result = lerp(sceneColor, pColor, pAlpha * 0.6);
    }

    return float4(result, 1.0);
}


//=== TECHNIQUE MACRO ===//

#define PARTICLE_TECHS(name, p1, p2) \
technique11 name##p1 <string UIName="Particles: Compute"; string RenderTarget="RenderTargetRGBA64F";> \
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic())); \
            SetPixelShader(CompileShader(ps_5_0, PS_ParticleCompute())); } } \
technique11 name##p2 <string UIName="Particles: Composite";> \
{ pass p0 { SetVertexShader(CompileShader(vs_5_0, VS_Basic())); \
            SetPixelShader(CompileShader(ps_5_0, PS_ParticleComposite())); } }

#endif // PREPASS_PARTICLE_FIELD_FXH
