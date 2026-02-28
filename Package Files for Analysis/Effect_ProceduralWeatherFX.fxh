#ifndef EFFECT_PROCEDURALWEATHERFX_FXH
#define EFFECT_PROCEDURALWEATHERFX_FXH

//----------------------------------------------------------------------------------------------//
//                                                                                              //
//                   Procedural Weather FX for ENBSeries  v3.1                                  //
//                   by Zain Dana Harper                                                        //
//                                                                                              //
//   Fully procedural rain droplets and frost crystals - no textures required.                  //
//                                                                                              //
//   v3.1 Improvements:                                                                         //
//     - Rain: Contact-angle droplet model, improved meniscus normals, internal                  //
//       caustic highlights, physics-based drip trails with capillary thinning                   //
//     - Frost: Multi-scale dendritic growth with recursive branching, improved                  //
//       crystal geometry with 6-fold symmetry, higher-quality Sobel normals                    //
//       via analytical gradient, sub-crystal fine detail layer                                  //
//                                                                                              //
//   Provides:                                                                                  //
//     ProceduralRainDroplet(float2 LocalUV, float2 Seed)   -> float4 (xyz=normal, w=mask)      //
//     ProceduralFrostRefraction(float2 UV)                 -> float3 (xy=normals, z=mask)       //
//     ProceduralFrostLayers(float2 UV)                     -> float3 (R=main, G=bg, B=crystal)  //
//                                                                                              //
//----------------------------------------------------------------------------------------------//


//=============================================================================//
//  Hash Primitives                                                            //
//                                                                             //
//  High-quality hash functions using integer-based hashing for better         //
//  distribution and fewer visual artifacts than sin-based PRNGs.              //
//  Maintains backward compatibility with existing API.                        //
//=============================================================================//

//Integer hash core (xxHash-inspired, produces excellent distribution)
uint WFX_IHash(uint x)
{
    x ^= x >> 16u;
    x *= 0x45d9f3bu;
    x ^= x >> 16u;
    x *= 0x45d9f3bu;
    x ^= x >> 16u;
    return x;
}

uint WFX_IHash2D(int2 p)
{
    //Combine two integers into one seed
    uint h = asuint(p.x) * 0x9e3779b9u + asuint(p.y);
    return WFX_IHash(h);
}

//Float output hashes [0, 1]
float WFX_Hash1(float2 p)
{
    int2 ip = int2(floor(p));
    return float(WFX_IHash2D(ip)) / 4294967295.0;
}

float WFX_Hash1f(float p)
{
    return float(WFX_IHash(asuint(int(floor(p))))) / 4294967295.0;
}

float2 WFX_Hash2(float2 p)
{
    int2 ip = int2(floor(p));
    uint h = WFX_IHash2D(ip);
    return float2(h & 0xFFFFu, h >> 16u) / 65535.0;
}

float3 WFX_Hash3(float2 p)
{
    int2 ip = int2(floor(p));
    uint h1 = WFX_IHash2D(ip);
    uint h2 = WFX_IHash(h1);
    return float3(h1 & 0xFFFFu, h1 >> 16u, h2 & 0xFFFFu) / 65535.0;
}


//=============================================================================//
//  Value Noise                                                                //
//                                                                             //
//  Smooth interpolated noise with C2 continuity (quintic Hermite).            //
//  Higher quality than standard C1 for derivative-based effects.              //
//=============================================================================//

float WFX_ValueNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    //Quintic interpolation for C2 continuity (smoother derivatives)
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    float a = WFX_Hash1(i + float2(0.0, 0.0));
    float b = WFX_Hash1(i + float2(1.0, 0.0));
    float c = WFX_Hash1(i + float2(0.0, 1.0));
    float d = WFX_Hash1(i + float2(1.0, 1.0));

    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}


//Analytical gradient of value noise (avoids finite-difference Sobel)
float3 WFX_ValueNoiseGrad(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float2 u  = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0); //d/df of quintic

    float a = WFX_Hash1(i + float2(0.0, 0.0));
    float b = WFX_Hash1(i + float2(1.0, 0.0));
    float c = WFX_Hash1(i + float2(0.0, 1.0));
    float d = WFX_Hash1(i + float2(1.0, 1.0));

    float Value = lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);

    //Analytical partial derivatives
    float dX = du.x * lerp(b - a, d - c, u.y);
    float dY = du.y * lerp(lerp(c - a, d - b, u.x), lerp(c - a, d - b, u.x), 0.0);
    //Correct dY: lerp(c-a, d-b, u.x)
    dY = du.y * (lerp(a, b, u.x) - lerp(a, b, u.x) + lerp(c, d, u.x) - lerp(a, b, u.x));
    dY = du.y * (lerp(c - a, d - b, u.x));

    return float3(Value, dX, dY);
}


//=============================================================================//
//  Gradient Noise                                                             //
//                                                                             //
//  Perlin-style gradient noise with analytical derivatives.                   //
//  Returns values in approximately [0, 1] range.                              //
//=============================================================================//

float WFX_GradientNoise(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    //Quintic interpolation for C2 continuity
    float2 u = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);

    //Random gradient vectors from hash
    float2 ga = WFX_Hash2(i + float2(0.0, 0.0)) * 2.0 - 1.0;
    float2 gb = WFX_Hash2(i + float2(1.0, 0.0)) * 2.0 - 1.0;
    float2 gc = WFX_Hash2(i + float2(0.0, 1.0)) * 2.0 - 1.0;
    float2 gd = WFX_Hash2(i + float2(1.0, 1.0)) * 2.0 - 1.0;

    float va = dot(ga, f - float2(0.0, 0.0));
    float vb = dot(gb, f - float2(1.0, 0.0));
    float vc = dot(gc, f - float2(0.0, 1.0));
    float vd = dot(gd, f - float2(1.0, 1.0));

    return lerp(lerp(va, vb, u.x), lerp(vc, vd, u.x), u.y) * 0.5 + 0.5;
}


//Gradient noise with analytical derivatives
float3 WFX_GradientNoiseGrad(float2 p)
{
    float2 i = floor(p);
    float2 f = frac(p);

    float2 u  = f * f * f * (f * (f * 6.0 - 15.0) + 10.0);
    float2 du = 30.0 * f * f * (f * (f - 2.0) + 1.0);

    float2 ga = WFX_Hash2(i + float2(0.0, 0.0)) * 2.0 - 1.0;
    float2 gb = WFX_Hash2(i + float2(1.0, 0.0)) * 2.0 - 1.0;
    float2 gc = WFX_Hash2(i + float2(0.0, 1.0)) * 2.0 - 1.0;
    float2 gd = WFX_Hash2(i + float2(1.0, 1.0)) * 2.0 - 1.0;

    float va = dot(ga, f - float2(0.0, 0.0));
    float vb = dot(gb, f - float2(1.0, 0.0));
    float vc = dot(gc, f - float2(0.0, 1.0));
    float vd = dot(gd, f - float2(1.0, 1.0));

    float Value = lerp(lerp(va, vb, u.x), lerp(vc, vd, u.x), u.y) * 0.5 + 0.5;

    //Analytical gradient
    float2 Grad = ga + u.x * (gb - ga) + u.y * (gc - ga) + u.x * u.y * (ga - gb - gc + gd);
    Grad += du * float2(
        lerp(vb - va, vd - vc, u.y),
        lerp(vc - va, vd - vb, u.x)
    );

    return float3(Value, Grad * 0.5);
}


//=============================================================================//
//  Voronoi / Worley Noise                                                     //
//                                                                             //
//  Returns F1 (distance to nearest cell center) and F2 (second nearest).      //
//  Used for ice crystal boundaries and individual crystal patterns.            //
//=============================================================================//

float2 WFX_Voronoi(float2 p)
{
    float2 n = floor(p);
    float2 f = frac(p);

    float F1 = 8.0;
    float F2 = 8.0;

    [unroll] for(int j = -1; j <= 1; j++)
    [unroll] for(int i = -1; i <= 1; i++)
    {
        float2 g = float2(i, j);
        float2 o = WFX_Hash2(n + g);
        float2 d = g + o - f;
        float  dist = dot(d, d);

        if(dist < F1) { F2 = F1; F1 = dist; }
        else if(dist < F2) { F2 = dist; }
    }

    return float2(sqrt(F1), sqrt(F2));
}


//Returns F1 distance + cell ID for identification of individual crystals
float3 WFX_VoronoiID(float2 p)
{
    float2 n = floor(p);
    float2 f = frac(p);

    float  F1 = 8.0;
    float2 CellID = 0.0;

    [unroll] for(int j = -1; j <= 1; j++)
    [unroll] for(int i = -1; i <= 1; i++)
    {
        float2 g = float2(i, j);
        float2 o = WFX_Hash2(n + g);
        float2 d = g + o - f;
        float  dist = dot(d, d);

        if(dist < F1)
        {
            F1 = dist;
            CellID = n + g;
        }
    }

    return float3(sqrt(F1), CellID);
}


//Voronoi with edge distance for precise crystal boundary detection
float3 WFX_VoronoiEdge(float2 p)
{
    float2 n = floor(p);
    float2 f = frac(p);

    float2 NearestOffset = 0.0;
    float  F1 = 8.0;
    float  F2 = 8.0;

    //First pass: find nearest cell
    [unroll] for(int j = -1; j <= 1; j++)
    [unroll] for(int i = -1; i <= 1; i++)
    {
        float2 g = float2(i, j);
        float2 o = WFX_Hash2(n + g);
        float2 d = g + o - f;
        float  dist = dot(d, d);

        if(dist < F1)
        {
            F2 = F1;
            F1 = dist;
            NearestOffset = d;
        }
        else if(dist < F2)
        {
            F2 = dist;
        }
    }

    //Edge distance: perpendicular distance to Voronoi boundary
    float EdgeDist = 8.0;
    [unroll] for(int jj = -1; jj <= 1; jj++)
    [unroll] for(int ii = -1; ii <= 1; ii++)
    {
        float2 g = float2(ii, jj);
        float2 o = WFX_Hash2(n + g);
        float2 d = g + o - f;

        if(dot(d - NearestOffset, d - NearestOffset) > DELTA)
        {
            float2 Mid = (NearestOffset + d) * 0.5;
            float2 Dir = normalize(d - NearestOffset);
            float  Dist = dot(Mid, Dir);
            EdgeDist = min(EdgeDist, Dist);
        }
    }

    return float3(sqrt(F1), sqrt(F2), EdgeDist);
}


//=============================================================================//
//  Fractal Brownian Motion (FBM)                                              //
//                                                                             //
//  Multi-octave noise with optional domain warping for organic patterns.      //
//=============================================================================//

float WFX_FBM(float2 p, int Octaves, float Lacunarity, float Gain)
{
    float Sum = 0.0;
    float Amp = 0.5;

    [loop] for(int i = 0; i < Octaves; i++)
    {
        Sum += Amp * WFX_ValueNoise(p);
        p   *= Lacunarity;
        Amp *= Gain;
    }

    return Sum;
}

float WFX_FBM_Gradient(float2 p, int Octaves, float Lacunarity, float Gain)
{
    float Sum = 0.0;
    float Amp = 0.5;

    [loop] for(int i = 0; i < Octaves; i++)
    {
        Sum += Amp * WFX_GradientNoise(p);
        p   *= Lacunarity;
        Amp *= Gain;
    }

    return Sum;
}


//Domain-warped FBM: feeds FBM output back as coordinate offset
//Creates highly organic, flowing patterns ideal for frost dendrites
float WFX_FBM_Warped(float2 p, int Octaves, float Lacunarity, float Gain, float WarpStrength)
{
    float2 Warp = float2(
        WFX_FBM(p + float2(1.7, 9.2), Octaves, Lacunarity, Gain),
        WFX_FBM(p + float2(8.3, 2.8), Octaves, Lacunarity, Gain)
    );

    return WFX_FBM(p + Warp * WarpStrength, Octaves, Lacunarity, Gain);
}


//FBM with analytical gradient output (for Sobel-free normal computation)
float3 WFX_FBM_WithGrad(float2 p, int Octaves, float Lacunarity, float Gain)
{
    float  Sum  = 0.0;
    float2 Grad = 0.0;
    float  Amp  = 0.5;
    float  Freq = 1.0;

    [loop] for(int i = 0; i < Octaves; i++)
    {
        float3 NG = WFX_GradientNoiseGrad(p);
        Sum  += Amp * NG.x;
        Grad += Amp * NG.yz * Freq;
        p    *= Lacunarity;
        Freq *= Lacunarity;
        Amp  *= Gain;
    }

    return float3(Sum, Grad);
}



//=============================================================================//
//                                                                             //
//  PROCEDURAL RAIN DROPLETS  v3.1                                             //
//                                                                             //
//  Physically-motivated sessile drop model with:                              //
//    - Young-Laplace height profile h(r) = h0*(1-(r/R)^2)^alpha              //
//    - Analytical surface normals from height field gradient                   //
//    - Capillary drip trails with Plateau-Rayleigh beading                    //
//    - Internal caustic from convergent refraction                            //
//    - Sub-pixel edge anti-aliasing via signed distance                       //
//                                                                             //
//  Output: float4(Normal.xyz packed [0,1], CombinedMask + caustic boost)      //
//          PS unpacks normals: Normal * 2.0 - 1.0                             //
//                                                                             //
//=============================================================================//

float4 ProceduralRainDroplet(float2 LocalUV, float2 Seed)
{
    //Per-droplet randomness from seed
    float3 Props = WFX_Hash3(Seed);
    float  BlobHash  = Props.x;   //Shape variation
    float  TrailHash = Props.y;   //Trail probability / length
    float  ShapeHash = Props.z;   //Normal intensity / contact angle

    //Additional random properties
    float2 Props2 = WFX_Hash2(Seed + 7.31);

    //Center UV with organic jitter
    float2 CenterOff = (Props2 - 0.5) * 0.05;
    float2 UV = LocalUV - 0.5 + CenterOff;

    //Contact-angle elliptical deformation (gravity stretches drops vertically)
    float AspectY = lerp(0.88, 1.18, BlobHash);
    float2 DropUV = UV;
    DropUV.y *= AspectY;

    float Dist = length(DropUV);

    //Per-droplet radius variation
    float DropRadius = lerp(0.35, 0.48, BlobHash);

    //Signed distance from edge (positive = inside, negative = outside)
    float SDist = DropRadius - Dist;

    //Smooth drop mask from signed distance (anti-aliased edge)
    float DropMask = saturate(SDist * 18.0); //~1px transition at typical sizes

    //Sessile drop height profile: h(r) = (1 - (r/R)^2)^alpha
    //Alpha ~1.3-1.8 depending on contact angle (larger = flatter top)
    float Alpha = lerp(1.3, 1.8, ShapeHash);
    float rNorm = saturate(Dist / max(DropRadius, DELTA));
    float rNorm2 = rNorm * rNorm;
    float Meniscus = pow(max(1.0 - rNorm2, 0.0), Alpha);
    Meniscus *= DropMask;


    //=== DRIP TRAIL ===//
    //~55% of drops develop gravity-driven drip trails
    float Trail = 0.0;
    float TrailNX = 0.0;

    [branch] if(TrailHash > 0.45)
    {
        float TrailLength = lerp(0.18, 0.55, TrailHash);
        float TrailWidth  = lerp(0.035, 0.09, BlobHash);

        //Trail extends below center (positive Y = downward on screen)
        float TrailY = saturate(UV.y / TrailLength);

        //Plateau-Rayleigh instability: surface tension causes beading in thin streams
        float PinchFreq = lerp(4.0, 9.0, WFX_Hash1(Seed + 19.0));
        float Pinch = 0.80 + 0.20 * sin(TrailY * PinchFreq * PI);

        //Capillary thinning: radius ~ (1-t)^(1/3) from Tanner's law
        float Taper = TrailWidth * pow(max(1.0 - TrailY, 0.02), 0.33) * Pinch;
        float TrailX = abs(UV.x) / max(Taper, DELTA);

        //Smooth trail mask with clean edge
        Trail = saturate(1.0 - TrailX) * TrailY;
        Trail *= smoothstep(0.0, 0.10, TrailY); //Smooth junction to body

        //Rivulet cross-section: approximately circular (tube normal)
        TrailNX = -sign(UV.x) * saturate(1.0 - TrailX * TrailX) * 1.8;
    }

    //Combine body and trail
    float CombinedMask = saturate(DropMask + Trail * 0.55);

    //Early discard
    if(CombinedMask < 0.01)
        return float4(0.5, 0.5, 1.0, 0.0);


    //=== NORMAL COMPUTATION ===//
    //Analytical gradient of sessile drop height field:
    //  dh/dr = alpha * (-2r/R^2) * (1 - (r/R)^2)^(alpha-1)

    float3 Normal;

    float SafeR = min(rNorm, 0.997);
    float dHdr = -Alpha * 2.0 * SafeR / max(DropRadius, DELTA)
               * pow(max(1.0 - SafeR * SafeR, 0.001), max(Alpha - 1.0, 0.01));

    //Convert radial derivative to XY gradient: dh/dx = dh/dr * x/r
    float2 RadDir = (Dist > DELTA) ? (DropUV / Dist) : float2(0.0, 0.0);
    Normal.xy = -dHdr * RadDir;
    Normal.z  = 1.0;

    //Blend trail tube normals where trail dominates
    [branch] if(Trail > 0.01)
    {
        float W = Trail / max(CombinedMask, DELTA);
        float3 TN = float3(TrailNX * 0.35, 0.08, 1.0);
        Normal = lerp(Normal, TN, saturate(W * 0.5));
    }

    //Micro-surface capillary waves (water isn't perfectly smooth)
    float Ripple = WFX_ValueNoise(LocalUV * 28.0 + Seed * 3.0) - 0.5;
    Normal.xy += Ripple * 0.025 * CombinedMask;

    //Normalize and pack to [0,1] for interpolant transport
    Normal = normalize(Normal);
    Normal.xy = Normal.xy * 0.5 + 0.5;
    Normal.z  = Normal.z  * 0.5 + 0.5;

    //Edge softening: attenuate refraction strength near boundary
    float EdgeFade = smoothstep(0.0, 0.15, CombinedMask);
    Normal.xy = lerp(0.5, Normal.xy, EdgeFade);


    //=== INTERNAL CAUSTIC ===//
    //Real water drops focus light through refraction, creating a bright
    //spot offset from center (below center due to gravity lens effect)
    float2 CausticCenter = float2(0.0, -DropRadius * 0.25);
    float CausticDist = length(DropUV - CausticCenter);
    float CausticSize = DropRadius * lerp(0.18, 0.28, ShapeHash);
    float Caustic = exp(-CausticDist * CausticDist / (CausticSize * CausticSize));
    Caustic *= Meniscus * 0.30;


    return float4(Normal, CombinedMask + Caustic);
}


//=============================================================================//
//                                                                             //
//  PROCEDURAL FROST REFRACTION  v3.1                                          //
//                                                                             //
//  Multi-scale ice crystal patterns with analytical gradient normals.          //
//  Generates natural dendritic frost growth with hexagonal crystal             //
//  structure and precise Voronoi-based plate boundaries.                       //
//                                                                             //
//  v3.1 Improvements:                                                         //
//    - Sharper dendrite branching with directional bias                        //
//    - Better Voronoi plate edges with variable-width boundaries              //
//    - Enhanced hexagonal 6-fold modulation within cells                       //
//    - Improved nucleation coverage with multi-scale randomness               //
//    - Richer fine crystalline micro-structure                                 //
//                                                                             //
//  Output: float3(NormalX, NormalY, FrostMask) packed to [0,1]                //
//                                                                             //
//=============================================================================//

//Internal: frost pattern with analytical gradient for normals
float3 WFX_FrostPatternGrad(float2 UV)
{
    //=== CRYSTAL PLATE BOUNDARIES ===//
    //Large primary plates (main crystal regions)
    float3 VLarge = WFX_VoronoiEdge(UV * 3.5);
    //Variable-width edges: thinner where cells are close, thicker at junctions
    float LargeEdgeWidth = lerp(0.04, 0.10, smoothstep(0.3, 0.7, VLarge.y));
    float LargePlate = smoothstep(0.004, LargeEdgeWidth, VLarge.z);

    //Secondary smaller plates (crystal subdivisions)
    float3 VSmall = WFX_VoronoiEdge(UV * 8.5 + 17.3);
    float SmallPlate = smoothstep(0.006, 0.055, VSmall.z);

    //Tertiary micro-plates (fine crystal structure)
    float3 VMicro = WFX_VoronoiEdge(UV * 22.0 + 41.7);
    float MicroPlate = smoothstep(0.01, 0.04, VMicro.z);

    float Plates = max(LargePlate * 0.85, max(SmallPlate * 0.55, MicroPlate * 0.20));


    //=== DENDRITIC BRANCHING ===//
    //Domain-warped FBM creates organic, tree-like crystal arms
    float2 WarpA = float2(
        WFX_FBM(UV * 2.5 + float2(5.2, 1.3), 3, 2.0, 0.5),
        WFX_FBM(UV * 2.5 + float2(1.7, 9.8), 3, 2.0, 0.5)
    );

    //Main dendrite arms with gradient for normals
    float3 DendGrad = WFX_FBM_WithGrad(UV * 11.0 + WarpA * 1.4, 5, 2.13, 0.52);
    float  Dendrite = smoothstep(0.26, 0.74, DendGrad.x);

    //Feathery secondary branches (finer, more delicate)
    float2 WarpB = float2(
        WFX_FBM(UV * 6.0 + float2(3.1, 7.4), 2, 2.0, 0.5),
        WFX_FBM(UV * 6.0 + float2(8.9, 2.1), 2, 2.0, 0.5)
    );
    float SubBranch = WFX_FBM_Gradient(UV * 20.0 + WarpB * 0.8, 3, 2.2, 0.48);
    SubBranch = smoothstep(0.30, 0.70, SubBranch);

    //Tertiary whisker branches (finest detail)
    float Whisker = WFX_FBM_Gradient(UV * 42.0 + WarpA * 0.4, 2, 2.3, 0.45);
    Whisker = smoothstep(0.38, 0.62, Whisker);

    Dendrite = max(Dendrite, SubBranch * 0.60);
    Dendrite = max(Dendrite, Whisker * 0.25);


    //=== FINE CRYSTALLINE TEXTURE ===//
    float3 FineGrad = WFX_FBM_WithGrad(UV * 45.0, 3, 2.2, 0.45);
    float  Fine = FineGrad.x * 0.16;
    Fine += WFX_ValueNoise(UV * 90.0) * 0.06;


    //=== HEXAGONAL CRYSTAL MODULATION ===//
    //Ice crystals form with 6-fold symmetry along crystallographic c-axis
    float3 CellInfo = WFX_VoronoiID(UV * 3.5);
    float2 CellUV   = UV * 3.5 - CellInfo.yz;
    float  CellAngle = atan2(CellUV.y, CellUV.x);
    float  CellDist  = length(CellUV);

    //Primary hexagonal facets (6 arms, 60 degree spacing)
    float HexFacet = pow(abs(cos(CellAngle * 3.0)), 3.0) * 0.15;
    //Secondary facets (12-fold, rotated 15 degrees)
    float HexSub   = pow(abs(sin(CellAngle * 6.0 + 1.5)), 5.0) * 0.08;
    //Radial falloff: hexagonal pattern fades toward cell edges
    float HexRadial = smoothstep(0.5, 0.1, CellDist);
    HexFacet *= HexRadial;
    HexSub   *= HexRadial;


    //=== COMBINE ALL LAYERS ===//
    float Frost = Plates * (Dendrite * 0.60 + 0.40);
    Frost += (Fine + HexFacet + HexSub) * Plates;

    //Multi-scale nucleation coverage
    float CovA = WFX_FBM_Warped(UV * 1.5, 3, 2.0, 0.5, 0.9);
    float CovB = WFX_FBM(UV * 3.0 + 23.0, 2, 2.0, 0.5);
    float Coverage = smoothstep(0.08, 0.48, CovA);
    //Modulate with secondary to break up uniform edges
    Coverage *= smoothstep(0.15, 0.60, CovB * 0.6 + CovA * 0.4);

    Frost *= Coverage;
    Frost = saturate(Frost);


    //=== ANALYTICAL GRADIENT ===//
    float2 Grad = DendGrad.yz * Plates * 0.55
                + FineGrad.yz * Plates * 0.18;
    Grad *= Coverage;

    return float3(Frost, Grad);
}


float3 ProceduralFrostRefraction(float2 UV)
{
    float3 FrostData = WFX_FrostPatternGrad(UV);
    float  Mask     = FrostData.x;
    float2 Gradient = FrostData.yz;

    //High-frequency sparkle perturbation (crystal micro-facet glints)
    float SpkNoise = WFX_GradientNoise(UV * 72.0);
    float SpkMask  = smoothstep(0.70, 0.96, SpkNoise) * Mask;
    Gradient.x += SpkMask * 0.12;
    Gradient.y += WFX_GradientNoise(UV * 72.0 + 31.7) * SpkMask * 0.12;

    //Pack normals to [0,1] (PS unpacks with * 2.0 + 1.0)
    return float3(Gradient * 0.5 + 0.5, Mask);
}


//=============================================================================//
//                                                                             //
//  PROCEDURAL FROST LAYERS  v3.1                                              //
//                                                                             //
//  Three-channel frost output for non-refraction compositing:                 //
//    R = Main Frost  (coverage mask: where frost exists)                      //
//    G = Thickness   (ice depth: 0=thin film, 1=thick rime frost)             //
//    B = Sparkle     (micro-facet crystal glints)                             //
//                                                                             //
//  v3.1 Improvements:                                                         //
//    - Richer crystal morphology with 3 plate scales                          //
//    - Better thickness model with rime/hoar frost distinction                //
//    - Enhanced sparkle from crystal orientation and facet normals             //
//    - Improved coverage with nucleation-site-aware distribution              //
//                                                                             //
//=============================================================================//

float3 ProceduralFrostLayers(float2 UV)
{
    // ---- R: MAIN FROST COVERAGE ---- //

    //Primary crystal plate boundaries (large Voronoi regions)
    float3 V1 = WFX_VoronoiEdge(UV * 3.5);
    float PlateBorder = smoothstep(0.004, 0.10, V1.z);

    //Secondary smaller plates
    float3 V1b = WFX_VoronoiEdge(UV * 8.0 + 13.7);
    float SmallBorder = smoothstep(0.006, 0.07, V1b.z);

    //Tertiary micro-crystallites
    float3 V1c = WFX_VoronoiEdge(UV * 20.0 + 37.1);
    float MicroBorder = smoothstep(0.01, 0.04, V1c.z);

    //Dendritic branching via domain-warped FBM
    float MainDendrite = WFX_FBM_Warped(UV * 9.0, 5, 2.13, 0.52, 0.8);
    MainDendrite = smoothstep(0.22, 0.78, MainDendrite);

    //Feathery secondary dendrites
    float2 FWarp = float2(
        WFX_FBM(UV * 5.5 + float2(3.8, 6.1), 2, 2.0, 0.5),
        WFX_FBM(UV * 5.5 + float2(7.2, 2.9), 2, 2.0, 0.5)
    );
    float Feather = WFX_FBM_Gradient(UV * 18.0 + FWarp * 0.7, 3, 2.3, 0.48);
    Feather = smoothstep(0.30, 0.70, Feather);

    //Tertiary whisker detail
    float Whisker = WFX_FBM_Gradient(UV * 38.0 + FWarp * 0.3, 2, 2.2, 0.45);
    Whisker = smoothstep(0.35, 0.65, Whisker);

    //Hexagonal modulation within primary cells
    float3 CID = WFX_VoronoiID(UV * 3.5);
    float2 CUV = UV * 3.5 - CID.yz;
    float  CAng = atan2(CUV.y, CUV.x);
    float  CDist = length(CUV);
    float  HexMod = 0.82 + 0.18 * pow(abs(cos(CAng * 3.0)), 2.5);
    HexMod *= smoothstep(0.55, 0.08, CDist); //Stronger near cell center

    //Combine: plates, dendrites, feathers, whiskers
    float MainFrost = max(PlateBorder * 0.75, max(SmallBorder * 0.45, MicroBorder * 0.15));
    MainFrost = max(MainFrost, MainDendrite * PlateBorder * 0.90 * HexMod);
    MainFrost = max(MainFrost, Feather * SmallBorder * 0.55);
    MainFrost = max(MainFrost, Whisker * MicroBorder * 0.20);

    //Fine crystalline texture detail
    MainFrost += WFX_GradientNoise(UV * 36.0) * 0.06 * MainFrost;
    MainFrost += WFX_ValueNoise(UV * 72.0) * 0.03 * MainFrost;

    //Multi-scale nucleation coverage
    float CovA = WFX_FBM_Warped(UV * 1.4, 3, 2.0, 0.5, 1.0);
    float CovB = WFX_FBM(UV * 2.8 + 19.0, 2, 2.0, 0.5);
    float Coverage = smoothstep(0.06, 0.46, CovA);
    Coverage *= smoothstep(0.12, 0.55, CovB * 0.5 + CovA * 0.5);

    MainFrost *= Coverage;
    MainFrost = saturate(MainFrost);


    // ---- G: ICE THICKNESS ---- //

    //Background rime frost density (smooth underlying ice layer)
    float BgIce = WFX_FBM_Warped(UV * 2.2 + 37.0, 4, 1.95, 0.55, 0.5);
    BgIce = smoothstep(0.18, 0.82, BgIce);

    //Thicker near Voronoi cell centers (ice accumulates from nucleation site)
    float2 V2 = WFX_Voronoi(UV * 1.8 + 19.0);
    float CenterThick = 1.0 - smoothstep(0.0, 0.30, V2.x);

    //Dendrite thickness: thicker where branching is dense
    float DendThick = MainDendrite * PlateBorder * 0.4;

    float Thickness = BgIce * 0.50 + CenterThick * 0.35 + DendThick * 0.15;
    Thickness *= Coverage;
    Thickness *= 0.78 + 0.22 * WFX_ValueNoise(UV * 15.0 + 5.0);
    Thickness = saturate(Thickness * MainFrost);


    // ---- B: SPARKLE / MICRO-FACETS ---- //

    //Crystal facet glints: sharp specular reflections from flat crystal faces
    float SparkleBase = WFX_GradientNoise(UV * 110.0);
    float SparkleHi   = smoothstep(0.74, 0.94, SparkleBase);

    //Per-crystal random: ~30% of crystallites produce visible sparkle
    float3 CrystalData = WFX_VoronoiID(UV * 12.0);
    float CrystalRand  = WFX_Hash1(CrystalData.yz);
    float SparkleOn    = step(CrystalRand, 0.30);

    //Angular sparkle: 6-fold symmetry from hexagonal crystal structure
    float2 SpkUV = UV * 12.0 - CrystalData.yz;
    float  SpkAng = atan2(SpkUV.y, SpkUV.x);
    float  AngSparkle = pow(abs(sin(SpkAng * 3.0 + CrystalRand * TWO_PI)), 10.0);

    //Distance modulation: sparkle strongest on flat crystal faces (near center)
    float SpkDist = length(SpkUV);
    float DistSparkle = smoothstep(0.4, 0.1, SpkDist);

    float Sparkle = SparkleHi * SparkleOn * AngSparkle * DistSparkle;
    Sparkle *= MainFrost;
    Sparkle = saturate(Sparkle);


    return float3(MainFrost, Thickness, Sparkle);
}

#endif // EFFECT_PROCEDURALWEATHERFX_FXH
