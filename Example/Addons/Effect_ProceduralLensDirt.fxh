//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                   Procedural Lens Dirt for ENBSeries                                         //
//                   by Zain Dana Harper                                                        //
//                                                                                              //
//   Multi-layer procedural dirt generation that replaces static texture atlases                 //
//   with infinite-resolution, non-repeating patterns.                                          //
//                                                                                              //
//   Layers:                                                                                    //
//     1. Dust particles - Voronoi-distributed microscopic specks                               //
//     2. Smudge streaks - Domain-warped FBM fingerprint/oil patterns                           //
//     3. Radial wipe marks - Concentric cleaning streaks from lens wiping                      //
//     4. Water spots - Dried mineral rings from evaporated droplets                            //
//     5. Film residue - Low-frequency haze from coating degradation                            //
//                                                                                              //
//   Provides:                                                                                  //
//     ProceduralLensDirt(float2 UV, float3 Params)  -> float3 (dirt mask, RGB-weighted)        //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


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
//  Layer 1: Dust Particles                                                    //
//                                                                             //
//  Microscopic specks distributed via Voronoi cells.                          //
//  Each cell has a probability of containing a dust mote.                     //
//  Produces sharp, small bright points when bloom catches them.               //
//=============================================================================//

float DirtLayer_DustParticles(float2 UV, float Density, float SizeRange)
{
    float Dust = 0.0;

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
            if(Prob > Density) continue;

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

    return saturate(Dust);
}


//=============================================================================//
//  Layer 2: Smudge / Oil Patterns                                             //
//                                                                             //
//  Domain-warped FBM creates organic fingerprint-like oil smears.             //
//  Low-frequency, soft blobs that catch and scatter bloom light.              //
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
//  Layer 3: Radial Wipe Streaks                                               //
//                                                                             //
//  Concentric arc patterns from lens cleaning with a circular motion.         //
//  Produces characteristic curved streaks that catch directional light.       //
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
//  Layer 4: Water Spots                                                       //
//                                                                             //
//  Dried mineral rings from evaporated water droplets.                        //
//  Produces bright ring outlines where minerals deposited.                    //
//=============================================================================//

float DirtLayer_WaterSpots(float2 UV, float Density)
{
    float Spots = 0.0;

    float2 CellP = UV * 8.0;
    float2 CellI = floor(CellP);
    float2 CellF = frac(CellP);

    [unroll] for(int j = -1; j <= 1; j++)
    [unroll] for(int i = -1; i <= 1; i++)
    {
        float2 Neighbor = float2(i, j);
        float2 Offset = DIRT_Hash2(CellI + Neighbor);
        float  Prob   = DIRT_Hash(CellI + Neighbor + 53.0);

        if(Prob > Density) continue;

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

    return saturate(Spots);
}


//=============================================================================//
//  Layer 5: Film Residue / Haze                                               //
//                                                                             //
//  Low-frequency coating degradation haze.                                    //
//  Provides the subtle overall fogging that old or dirty lenses exhibit.      //
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
//  Combined Procedural Lens Dirt                                              //
//                                                                             //
//  Blends all 5 layers with energy-preserving weighting.                      //
//  Returns float3 with slight per-channel variation                           //
//  (dust scatters blue slightly more, oil shifts warm).                       //
//                                                                             //
//  Params.x = Overall intensity (maps to UID_Strength)                        //
//  Params.y = Power curve (maps to UID_Power)                                 //
//  Params.z = Style bias (0=clean/dust, 1=dirty/smudge)                       //
//=============================================================================//

float3 ProceduralLensDirt(float2 UV)
{
    //Generate each layer
    float Dust    = DirtLayer_DustParticles(UV, 0.35, 0.05);
    float Smudge  = DirtLayer_Smudges(UV, 3.5, 0.7);
    float Wipe    = DirtLayer_WipeStreaks(UV, 0.4, 30.0);
    float Spots   = DirtLayer_WaterSpots(UV, 0.25);
    float Residue = DirtLayer_FilmResidue(UV, 0.3);

    //Composite: each layer adds to the dirt mask
    float DirtMono = Dust * 0.5 + Smudge * 0.35 + Wipe * 0.20
                   + Spots * 0.25 + Residue * 0.15;

    //Per-channel spectral variation:
    //  Dust scatters short wavelengths more (Rayleigh-like)
    //  Oil creates warm-shifted halos
    float3 DirtRGB;
    DirtRGB.r = DirtMono + Smudge * 0.05 + Residue * 0.03;
    DirtRGB.g = DirtMono;
    DirtRGB.b = DirtMono + Dust * 0.08 - Smudge * 0.02;

    return saturate(DirtRGB);
}
