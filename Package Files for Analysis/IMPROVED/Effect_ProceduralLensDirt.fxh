//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                   Procedural Lens Dirt for ENBSeries                                         //
//                   by Zain Dana Harper                                                        //
//                                                                                              //
//  v2.0.0 - IMPROVED with SkyrimBridge v3.0.0 integration                                     //
//                                                                                              //
//  CHANGELOG from v1.0:                                                                        //
//    [+] Weather-reactive dirt accumulation (dusty weather = more dust)                        //
//    [+] Rain/wetness cleansing effect (rain washes away dirt)                                 //
//    [+] Snow accumulation on lens (frost-like buildup in blizzards)                           //
//    [+] Interior/exterior dirt differentiation (dusty interiors)                              //
//    [+] Wind-driven dust accumulation patterns                                                //
//    [+] Combat-reactive dirt visibility (less visible during combat)                          //
//    [~] Improved water spot generation linked to precipitation                                //
//                                                                                              //
//   Layers:                                                                                    //
//     1. Dust particles - Voronoi-distributed microscopic specks                               //
//     2. Smudge streaks - Domain-warped FBM fingerprint/oil patterns                           //
//     3. Radial wipe marks - Concentric cleaning streaks from lens wiping                      //
//     4. Water spots - Dried mineral rings from evaporated droplets                            //
//     5. Film residue - Low-frequency haze from coating degradation                            //
//                                                                                              //
//----------------------------------------------------------------------------------------------//

#ifndef EFFECT_PROCEDURALLENSDIRT_FXH
#define EFFECT_PROCEDURALLENSDIRT_FXH


//=============================================================================//
//  [NEW v2.0] ADDITIONAL UI PARAMETERS                                        //
//=============================================================================//

bool  UIDIRT_UseSkyrimBridge < string UIName = "Dirt | Use SkyrimBridge"; > = true;
bool  UIDIRT_WeatherReactive < string UIName = "Dirt | Weather Reactive"; > = true;
float UIDIRT_RainCleanse     < string UIName = "Dirt | Rain Cleansing"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.5;
bool  UIDIRT_SnowAccum       < string UIName = "Dirt | Snow Accumulation"; > = true;
float UIDIRT_SnowAccumRate   < string UIName = "Dirt | Snow Accumulation Rate"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 1.0; float UIStep = 0.01; > = 0.3;
bool  UIDIRT_WindDust        < string UIName = "Dirt | Wind Dust Patterns"; > = true;
bool  UIDIRT_CombatReduce    < string UIName = "Dirt | Combat Visibility Reduce"; > = false;
float UIDIRT_CombatReduction < string UIName = "Dirt | Combat Reduction Amount"; string UIWidget = "spinner"; float UIMin = 0.0; float UIMax = 0.7; float UIStep = 0.01; > = 0.3;


//=============================================================================//
//  Dirt Hash Functions                                                        //
//  (Isolated namespace to avoid collision with WeatherFX hashes)              //
//=============================================================================//

float DIRT_Hash(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float2 DIRT_Hash2(float2 p)
{
    float3 p3 = frac(float3(p.xyx) * float3(0.1031, 0.1030, 0.0973));
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.xx + p3.yz) * p3.zy);
}

float DIRT_ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);

    float a = DIRT_Hash(i);
    float b = DIRT_Hash(i + float2(1.0, 0.0));
    float c = DIRT_Hash(i + float2(0.0, 1.0));
    float d = DIRT_Hash(i + float2(1.0, 1.0));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

float DIRT_FBM(float2 p, int Octaves, float Lacunarity, float Gain)
{
    float Sum = 0.0;
    float Amp = 0.5;

    [loop] for(int i = 0; i < Octaves; i++)
    {
        Sum += Amp * DIRT_ValueNoise(p);
        p   *= Lacunarity;
        Amp *= Gain;
    }
    return Sum;
}


//=============================================================================//
//  [NEW v2.0] WEATHER-REACTIVE MODIFIERS                                      //
//=============================================================================//

// Get overall dirt intensity modifier based on weather
float DIRT_GetWeatherModifier()
{
    float mod = 1.0;

    [branch] if (!UIDIRT_UseSkyrimBridge || !UIDIRT_WeatherReactive || !SB_IsActive())
        return mod;

    // Rain cleansing - washes away dirt
    float isRain = (SB_Precipitation.x > 0.5 && SB_Precipitation.x < 1.5) ? 1.0 : 0.0;
    float rainIntensity = SB_Precipitation.y * isRain;
    float sceneWetness = SB_SceneWetness();

    // Cleansing reduces dirt visibility
    float cleanse = max(rainIntensity, sceneWetness) * UIDIRT_RainCleanse;
    mod *= 1.0 - cleanse * 0.7;

    // Interior suppression (less outdoor dirt inside)
    if (EInteriorFactor > 0.5)
    {
        mod *= lerp(1.0, 0.6, EInteriorFactor);
    }

    return saturate(mod);
}

// Get dust intensity modifier (increases in dusty/windy conditions)
float DIRT_GetDustModifier()
{
    float mod = 1.0;

    [branch] if (!UIDIRT_UseSkyrimBridge || !SB_IsActive()) return mod;

    // Wind increases dust
    [branch] if (UIDIRT_WindDust)
    {
        float windSpeed = SB_Wind.z;
        mod *= 1.0 + windSpeed * 0.4;
    }

    // Dusty weather flag
    float isDusty = SB_Weather_Flags.x;  // Using generic weather flag
    if (isDusty > 0.5)
    {
        mod *= 1.3;
    }

    // Snow reduces dust visibility (covered by snow)
    float isSnow = (SB_Precipitation.x > 1.5) ? 1.0 : 0.0;
    float snowInt = SB_Precipitation.y * isSnow;
    mod *= 1.0 - snowInt * 0.3;

    return saturate(mod);
}

// Get water spot modifier (increases after rain, decreases during rain)
float DIRT_GetWaterSpotModifier()
{
    float mod = 1.0;

    [branch] if (!UIDIRT_UseSkyrimBridge || !SB_IsActive()) return mod;

    // During active rain, spots are washed away
    float isRain = (SB_Precipitation.x > 0.5 && SB_Precipitation.x < 1.5) ? 1.0 : 0.0;
    float rainIntensity = SB_Precipitation.y * isRain;

    if (rainIntensity > 0.3)
    {
        mod *= 0.5;  // Active rain reduces old spots
    }

    // Scene wetness can add fresh water spots (drying phase)
    float sceneWetness = SB_SceneWetness();
    if (sceneWetness > 0.2 && rainIntensity < 0.2)
    {
        // Post-rain drying phase - spots form
        mod *= 1.0 + sceneWetness * 0.5;
    }

    return saturate(mod);
}

// Get wind direction for dust pattern offset
float2 DIRT_GetWindOffset()
{
    float2 offset = float2(0.0, 0.0);

    [branch] if (!UIDIRT_WindDust || !UIDIRT_UseSkyrimBridge || !SB_IsActive())
        return offset;

    float2 windDir = SB_Wind.xy;
    float windSpeed = SB_Wind.z;

    float windLen = length(windDir);
    if (windLen > 0.01)
    {
        offset = (windDir / windLen) * windSpeed * 0.1;
    }

    return offset;
}

// Get combat visibility modifier
float DIRT_GetCombatModifier()
{
    float mod = 1.0;

    [branch] if (!UIDIRT_CombatReduce || !UIDIRT_UseSkyrimBridge || !SB_IsActive())
        return mod;

    float combatInt = SB_CombatIntensity();
    mod *= 1.0 - combatInt * UIDIRT_CombatReduction;

    return mod;
}


//=============================================================================//
//  Layer 1: Dust Particles (IMPROVED v2.0)                                    //
//                                                                             //
//  Microscopic specks distributed via Voronoi cells.                          //
//  Now with wind-driven offset and weather reactivity.                        //
//=============================================================================//

float DirtLayer_DustParticles(float2 UV, float Density, float SizeRange)
{
    float Dust = 0.0;

    // [NEW v2.0] Apply wind offset to dust pattern
    float2 windOffset = DIRT_GetWindOffset();
    UV += windOffset;

    // [NEW v2.0] Apply dust modifier
    float dustMod = DIRT_GetDustModifier();

    //Two scales of dust: coarse (fewer, larger) and fine (many, small)
    [unroll] for(int scale = 0; scale < 2; scale++)
    {
        float Freq = lerp(18.0, 50.0, (float)scale);
        float2 CellP = UV * Freq;
        float2 CellI = floor(CellP);
        float2 CellF = frac(CellP);

        //Check 3x3 neighborhood for nearby dust motes
        [unroll] for(int j = -1; j <= 1; j++)
        [unroll] for(int i = -1; i <= 1; i++)
        {
            float2 Neighbor = float2(i, j);
            float2 Offset = DIRT_Hash2(CellI + Neighbor);

            //Probability of dust mote existing in this cell
            float Prob = DIRT_Hash(CellI + Neighbor + 99.0);
            if(Prob > Density * dustMod) continue;

            //Dust mote position and size
            float2 MotePos = Neighbor + Offset - CellF;
            float  MoteDist = length(MotePos);
            float  MoteSize = lerp(0.01, SizeRange, DIRT_Hash(CellI + Neighbor + 17.0));

            //Sharp circular mote with slight softness
            float Mote = 1.0 - smoothstep(MoteSize * 0.6, MoteSize, MoteDist);

            //Brightness variation per mote
            Mote *= lerp(0.3, 1.0, DIRT_Hash(CellI + Neighbor + 41.0));

            Dust += Mote * lerp(1.0, 0.4, (float)scale); //Fine dust is dimmer
        }
    }

    return saturate(Dust * dustMod);
}


//=============================================================================//
//  Layer 2: Smudge / Oil Patterns (unchanged from v1.0)                       //
//=============================================================================//

float DirtLayer_Smudges(float2 UV, float Spread, float Opacity)
{
    //Primary smudge pattern: domain-warped for organic shapes
    float2 Warp = float2(
        DIRT_FBM(UV * 2.5 + float2(3.1, 7.2), 4, 2.1, 0.5),
        DIRT_FBM(UV * 2.5 + float2(8.4, 1.6), 4, 2.1, 0.5)
    );

    float Smudge = DIRT_FBM(UV * Spread + Warp * 1.5, 5, 2.0, 0.55);

    //Threshold to create discrete smudge regions
    Smudge = smoothstep(0.40, 0.70, Smudge);

    //Internal texture: fingerprint ridge-like structure
    float Ridges = sin(UV.x * 120.0 + Warp.x * 40.0 + UV.y * 30.0);
    Ridges = Ridges * 0.5 + 0.5;
    Ridges = smoothstep(0.3, 0.8, Ridges);

    Smudge *= lerp(0.6, 1.0, Ridges);

    //Spatial distribution: smudges cluster toward edges and center
    float2 FromCenter = UV - 0.5;
    float RadialDist = length(FromCenter);
    float SmudgeDistrib = smoothstep(0.1, 0.4, RadialDist) * smoothstep(0.8, 0.5, RadialDist);
    SmudgeDistrib = lerp(0.3, 1.0, SmudgeDistrib);

    return saturate(Smudge * SmudgeDistrib * Opacity);
}


//=============================================================================//
//  Layer 3: Radial Wipe Streaks (unchanged from v1.0)                         //
//=============================================================================//

float DirtLayer_WipeStreaks(float2 UV, float Intensity, float Count)
{
    float2 Center = UV - 0.5;
    float Angle  = atan2(Center.y, Center.x);
    float Radius = length(Center);

    //Angular streaks modulated by noise
    float AngularNoise = DIRT_ValueNoise(float2(Angle * Count / TWO_PI, Radius * 8.0));
    float Streak = sin(Angle * Count + AngularNoise * 4.0);
    Streak = pow(abs(Streak), 8.0); //Sharp streaks

    //Radial falloff: streaks are strongest in the middle ring
    float RadialMask = smoothstep(0.05, 0.2, Radius) * smoothstep(0.65, 0.35, Radius);

    //Vary streak width and brightness
    float StreakVariation = DIRT_FBM(float2(Angle * 3.0, Radius * 5.0), 3, 2.0, 0.5);
    Streak *= lerp(0.2, 1.0, StreakVariation);

    return saturate(Streak * RadialMask * Intensity);
}


//=============================================================================//
//  Layer 4: Water Spots (IMPROVED v2.0)                                       //
//                                                                             //
//  Now with weather-reactive density based on precipitation.                  //
//=============================================================================//

float DirtLayer_WaterSpots(float2 UV, float Density)
{
    float Spots = 0.0;

    // [NEW v2.0] Apply water spot modifier
    float spotMod = DIRT_GetWaterSpotModifier();

    float2 CellP = UV * 8.0;
    float2 CellI = floor(CellP);
    float2 CellF = frac(CellP);

    [unroll] for(int j = -1; j <= 1; j++)
    [unroll] for(int i = -1; i <= 1; i++)
    {
        float2 Neighbor = float2(i, j);
        float2 Offset = DIRT_Hash2(CellI + Neighbor);
        float  Prob   = DIRT_Hash(CellI + Neighbor + 53.0);

        if(Prob > Density * spotMod) continue;

        float2 SpotPos  = Neighbor + Offset - CellF;
        float  SpotDist = length(SpotPos);
        float  SpotSize = lerp(0.15, 0.40, DIRT_Hash(CellI + Neighbor + 23.0));

        //Ring shape: bright at the edge, clear in center
        float Ring = smoothstep(SpotSize - 0.04, SpotSize - 0.01, SpotDist)
                   * smoothstep(SpotSize + 0.01, SpotSize - 0.02, SpotDist);

        //Internal mineral residue (faint fill)
        float Fill = (1.0 - smoothstep(0.0, SpotSize, SpotDist)) * 0.15;

        //Incomplete ring: segments missing from uneven evaporation
        float SegAngle = atan2(SpotPos.y, SpotPos.x);
        float SegNoise = DIRT_ValueNoise(float2(SegAngle * 3.0, DIRT_Hash(CellI + Neighbor + 71.0) * 100.0));
        Ring *= smoothstep(0.25, 0.55, SegNoise);

        Spots += (Ring + Fill) * lerp(0.4, 1.0, DIRT_Hash(CellI + Neighbor + 37.0));
    }

    return saturate(Spots * spotMod);
}


//=============================================================================//
//  Layer 5: Film Residue / Haze (unchanged from v1.0)                         //
//=============================================================================//

float DirtLayer_FilmResidue(float2 UV, float Opacity)
{
    //Very low frequency noise for broad haze patches
    float Haze = DIRT_FBM(UV * 1.5 + 99.0, 4, 1.8, 0.6);
    Haze = smoothstep(0.3, 0.8, Haze);

    //Edge concentration: coating wears more uniformly but collects at edges
    float2 Center = UV - 0.5;
    float EdgeDist = length(Center);
    float EdgeBias = smoothstep(0.2, 0.6, EdgeDist);

    return saturate(Haze * lerp(0.3, 1.0, EdgeBias) * Opacity);
}


//=============================================================================//
//  [NEW v2.0] Layer 6: Snow Accumulation                                      //
//                                                                             //
//  Frost-like buildup during snowy weather.                                   //
//=============================================================================//

float DirtLayer_SnowAccum(float2 UV)
{
    [branch] if (!UIDIRT_SnowAccum || !UIDIRT_UseSkyrimBridge || !SB_IsActive())
        return 0.0;

    float isSnow = (SB_Precipitation.x > 1.5) ? 1.0 : 0.0;
    float snowInt = SB_Precipitation.y * isSnow;

    if (snowInt < 0.1) return 0.0;

    // Interior suppression
    if (EInteriorFactor > 0.5) return 0.0;

    // Frost-like pattern concentrated at edges
    float2 Center = UV - 0.5;
    float EdgeDist = length(Center);

    // Fractal frost pattern
    float frost = DIRT_FBM(UV * 8.0 + 50.0, 5, 2.2, 0.5);
    frost = smoothstep(0.3, 0.7, frost);

    // Edge concentration
    float edgeMask = smoothstep(0.2, 0.6, EdgeDist);

    // Combine with snow intensity
    float snowAccum = frost * edgeMask * snowInt * UIDIRT_SnowAccumRate;

    return saturate(snowAccum);
}


//=============================================================================//
//  Combined Procedural Lens Dirt (IMPROVED v2.0)                              //
//                                                                             //
//  Now with weather reactivity and snow accumulation.                         //
//=============================================================================//

float3 ProceduralLensDirt(float2 UV)
{
    // [NEW v2.0] Get overall weather and combat modifiers
    float weatherMod = DIRT_GetWeatherModifier();
    float combatMod = DIRT_GetCombatModifier();
    float overallMod = weatherMod * combatMod;

    //Generate each layer
    float Dust    = DirtLayer_DustParticles(UV, 0.35, 0.05);
    float Smudge  = DirtLayer_Smudges(UV, 3.5, 0.7);
    float Wipe    = DirtLayer_WipeStreaks(UV, 0.4, 30.0);
    float Spots   = DirtLayer_WaterSpots(UV, 0.25);
    float Residue = DirtLayer_FilmResidue(UV, 0.3);

    // [NEW v2.0] Snow accumulation layer
    float Snow    = DirtLayer_SnowAccum(UV);

    //Composite: each layer adds to the dirt mask
    float DirtMono = Dust * 0.5 + Smudge * 0.35 + Wipe * 0.20
                   + Spots * 0.25 + Residue * 0.15;

    // [NEW v2.0] Apply overall modifier
    DirtMono *= overallMod;

    //Per-channel spectral variation:
    //  Dust scatters short wavelengths more (Rayleigh-like)
    //  Oil creates warm-shifted halos
    float3 DirtRGB;
    DirtRGB.r = DirtMono + Smudge * 0.05 + Residue * 0.03;
    DirtRGB.g = DirtMono;
    DirtRGB.b = DirtMono + Dust * 0.08 - Smudge * 0.02;

    // [NEW v2.0] Add snow layer (white/blue tint)
    float3 SnowRGB = float3(0.95, 0.97, 1.0) * Snow;
    DirtRGB += SnowRGB;

    return saturate(DirtRGB);
}


//=============================================================================//
//  Summary: Procedural lens dirt with weather reactivity including rain       //
//  cleansing, wind-driven dust, precipitation-based water spots, and          //
//  snow accumulation. Combat visibility reduction for better gameplay.        //
//=============================================================================//

#endif // EFFECT_PROCEDURALLENSDIRT_FXH
