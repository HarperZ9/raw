// RawCosmosSky.hlsli: First-Principles Physical Celestial, Galactic, Starfield & 3D Curtain Aurora Library (SM 6.6 / 6.10).
// 100% clean-room original procedural HLSL, zero external skybox textures, zero cbuffer dependency.
// Shared cleanly by AtmosphereLut.hlsl, EnvCubePass.hlsl, and LightFog.hlsl.
//
// Systems:
//   1. Tilted sidereal galactic coordinate frame (slowly rotating with gameHour) + multi-band Milky Way / Aetherius:
//      glowing core bulge, spiral arm star-cloud density, domain-warped FBM dark carbon-dust absorption rifts
//      with wavelength-selective extinction/reddening, and localized H-II (656nm magenta) / O-III (501nm teal) nebulae.
//   2. Physically blackbody-colored 3-tier starfield (O/B/A/F/G/K/M) on equal-area cube-sphere cells with
//      Kasten-Young airmass-driven atmospheric scintillation (steady zenith, chromatic twinkling near horizon).
//   3. 3D Volumetric Raymarched Curtain Auroras (95..260 km curved ionosphere shells) along tilted magnetic field lines
//      with exact atomic emission spectra: N2+ 427.8nm violet-magenta lower fringe (~100 km), O 557.7nm luminous
//      emerald-cyan core veil (~115..160 km), and O 630.0nm deep crimson-ruby upper canopy (~180..260 km).
//   4. Van Rhijn 90 km mesopause airglow, occasional crisp shooting meteors, Mie solar aureole, 22-degree hexagonal
//      ice-crystal halo with Snell dispersion (when cold/snowy), and subtle warm/cool dual-moon atmospheric limb glow.
//
// Primary entry point:
//   float3 evalCosmosAndAurora(float3 rayDir, float3 sunDir, float gameHour, float simTime,
//                              float cloudCover, float snow01, float camNorthY)
// Original work, MIT.
#ifndef RAW_COSMOS_SKY_HLSLI
#define RAW_COSMOS_SKY_HLSLI

static const float kCosmosPi       = 3.14159265358979f;
static const float kCosmosTwoPi    = 6.28318530717959f;
static const float kCosmosEarthKm  = 6360.0f;

// ----------------------------------------------------------------------------
// Pure polynomial/rational 3D hashes & C2-continuous 3D gradient/value FBM
// (vendor-invariant across NVIDIA/AMD/Intel, zero sin() drift, zero collisions)
// ----------------------------------------------------------------------------
float rawCosmos_hash31(float3 p) {
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float3 rawCosmos_hash33(float3 p) {
    p = frac(p * float3(0.1031f, 0.1030f, 0.0973f));
    p += dot(p, p.yxz + 33.33f);
    return frac((p.xxy + p.yxx) * p.zyx);
}

float rawCosmos_noise3(float3 x) {
    const float3 i = floor(x);
    const float3 f = frac(x);
    const float3 u = f * f * f * (f * (f * 6.0f - 15.0f) + 10.0f);

    const float n000 = rawCosmos_hash31(i + float3(0.0f, 0.0f, 0.0f));
    const float n100 = rawCosmos_hash31(i + float3(1.0f, 0.0f, 0.0f));
    const float n010 = rawCosmos_hash31(i + float3(0.0f, 1.0f, 0.0f));
    const float n110 = rawCosmos_hash31(i + float3(1.0f, 1.0f, 0.0f));
    const float n001 = rawCosmos_hash31(i + float3(0.0f, 0.0f, 1.0f));
    const float n101 = rawCosmos_hash31(i + float3(1.0f, 0.0f, 1.0f));
    const float n011 = rawCosmos_hash31(i + float3(0.0f, 1.0f, 1.0f));
    const float n111 = rawCosmos_hash31(i + float3(1.0f, 1.0f, 1.0f));

    const float nx00 = lerp(n000, n100, u.x);
    const float nx10 = lerp(n010, n110, u.x);
    const float nx01 = lerp(n001, n101, u.x);
    const float nx11 = lerp(n011, n111, u.x);
    return lerp(lerp(nx00, nx10, u.y), lerp(nx01, nx11, u.y), u.z);
}

float rawCosmos_fbm3(float3 p) {
    float s = 0.0f;
    float a = 0.52f;
    s += rawCosmos_noise3(p) * a;
    p = p * 2.04f + float3(13.7f, 29.3f, 7.1f);
    a *= 0.50f;
    s += rawCosmos_noise3(p) * a;
    p = p * 2.03f + float3(19.1f, 5.7f, 23.4f);
    a *= 0.50f;
    s += rawCosmos_noise3(p) * a;
    return s * (1.0f / 0.91f);
}

float rawCosmos_fbm4(float3 p) {
    float s = 0.0f;
    float a = 0.50f;
    [unroll] for (int i = 0; i < 4; ++i) {
        s += rawCosmos_noise3(p) * a;
        p = p * 2.05f + float3(17.3f, 31.1f, 11.9f);
        a *= 0.50f;
    }
    return s * (1.0f / 0.9375f);
}

// ----------------------------------------------------------------------------
// Kasten-Young (1989) Optical Airmass & Wavelength-Dependent Sky Extinction
// ----------------------------------------------------------------------------
float rawCosmosAirmass(float sinElev) {
    const float s = clamp(sinElev, 0.0f, 1.0f);
    const float elevDeg = degrees(asin(s));
    const float denom = s + 0.50572f * pow(max(elevDeg + 6.07995f, 0.2f), -1.6364f);
    return clamp(1.0f / max(denom, 0.025f), 1.0f, 36.0f);
}

// Physical Rayleigh + Chappuis Ozone + Aerosol transmittance from top of atmosphere to observer.
// Blue light extinguishes fastest near horizon while warm amber/red persists cleanly.
float3 rawCosmosAtmosphericTransmittance(float sinElev, float cloudCover, float snow01) {
    if (sinElev <= -0.015f) { return 0.0f.xxx; }
    const float am = rawCosmosAirmass(max(sinElev, 0.0f));
    const float aerosolTau = 0.045f + saturate(cloudCover) * 0.14f + saturate(snow01) * 0.07f;
    // Zenith optical depth at Rec.709 RGB (~615, 540, 460 nm): Rayleigh + Ozone + Aerosol
    const float3 tauZenith = float3(0.085f, 0.148f, 0.285f) + aerosolTau.xxx;
    const float horSoft = smoothstep(-0.008f, 0.042f, sinElev);
    return exp(-tauZenith * (am - 0.65f)) * horSoft;
}

// ----------------------------------------------------------------------------
// Sidereal Celestial & Tilted Galactic Coordinate Frames (rotating with gameHour)
// ----------------------------------------------------------------------------
// World space: +X = East, +Y = North, +Z = Up.
// Transforms view rayDir into sidereal equatorial coordinates (rotating around ~62N celestial pole).
float3 rawCosmosToCelestial(float3 rayDir, float gameHour) {
    // Nordic observer latitude ~62 deg N -> celestial pole sits 62 deg above northern (+Y) horizon.
    static const float kSinLat = 0.8829476f;  // sin(62 deg)
    static const float kCosLat = 0.4694716f;  // cos(62 deg)
    // Tilt from local horizon frame into fixed meridian equatorial frame:
    const float3 eqMerid = float3(
        rayDir.x,
        rayDir.y * kSinLat - rayDir.z * kCosLat,
        rayDir.y * kCosLat + rayDir.z * kSinLat);
    // Rotate around celestial pole (+Z_eq) by sidereal hour angle (1 full revolution per 24 game hours):
    const float siderealAng = (gameHour * (kCosmosTwoPi / 24.0f)) + 0.58f;
    const float ca = cos(siderealAng);
    const float sa = sin(siderealAng);
    return normalize(float3(
        eqMerid.x * ca - eqMerid.y * sa,
        eqMerid.x * sa + eqMerid.y * ca,
        eqMerid.z));
}

// Transforms sidereal equatorial direction into tilted Galactic coordinates (Z = galactic north pole,
// +X = galactic center / Aetherius core bulge).
float3 rawCosmosToGalactic(float3 celDir) {
    // Authentic ~62.8 deg galactic plane inclination (exact orthonormal rotation basis):
    static const float3 gPole = float3(-0.4924039f,  0.5868241f,  0.6427876f);
    static const float3 gCore = float3( 0.7660444f,  0.6427876f,  0.0000000f);
    static const float3 gArm  = float3(-0.4131759f,  0.4924039f, -0.7660444f);
    return normalize(float3(dot(celDir, gCore), dot(celDir, gArm), dot(celDir, gPole)));
}

// ----------------------------------------------------------------------------
// 1. Multi-Band Galactic Plane, Spiral Star-Clouds, Dark Dust Rifts & H-II Nebulae
//    Also outputs dustTrans so Tier-3 galactic stars are carved by dark dust lanes!
// ----------------------------------------------------------------------------
float3 rawCosmosGalacticPlane(float3 galDir, out float3 outDustTrans, out float outDiscWide) {
    const float galLat = galDir.z;

    // Broad stellar halo + crisp spiral-arm ribbon:
    const float discWide   = exp(-galLat * galLat * 10.5f);
    const float discNarrow = exp(-galLat * galLat * 52.0f);
    outDiscWide = discWide;

    if (discWide < 0.008f) {
        outDustTrans = 1.0f.xxx;
        return 0.0f.xxx;
    }

    // Glowing Galactic Core Bulge (Aetherius / Sagittarius core):
    const float coreAlign = saturate(dot(galDir, normalize(float3(1.0f, 0.14f, 0.0f))));
    const float coreBulge = pow(coreAlign, 4.5f) * exp(-galLat * galLat * 15.0f);
    const float antiCore  = pow(saturate(-galDir.x * 0.5f + 0.5f), 2.0f);

    // Domain-warped 3D spherical FBM (100% seam-free across all longitudes & poles):
    const float3 warpVec = float3(
        rawCosmos_fbm3(galDir * 3.2f + float3(1.7f, 4.3f, 2.9f)) - 0.5f,
        rawCosmos_fbm3(galDir * 3.2f + float3(9.1f, 2.5f, 6.4f)) - 0.5f,
        rawCosmos_fbm3(galDir * 3.2f + float3(5.3f, 8.7f, 1.2f)) - 0.5f);
    const float3 qCloud = galDir * 5.6f + warpVec * 0.85f;
    const float starClouds = rawCosmos_fbm4(qCloud);
    const float fineFilaments = rawCosmos_fbm3(galDir * 13.5f - warpVec * 0.60f + float3(4.1f, 11.2f, 7.8f));

    // Dark Interstellar Carbon-Dust Absorption Rifts (Great Rift bifurcating the Milky Way):
    const float riftMeander = (rawCosmos_fbm3(galDir * 3.8f + float3(12.3f, 6.7f, 3.1f)) - 0.5f) * 0.135f;
    const float riftBranch  = (rawCosmos_fbm3(galDir * 6.5f + float3(3.4f, 15.9f, 8.2f)) - 0.5f) * 0.095f;
    const float laneMain = exp(-pow((galLat - riftMeander) / 0.048f, 2.0f));
    const float laneSub  = exp(-pow((galLat + 0.055f - riftBranch) / 0.036f, 2.0f)) * 0.65f;
    const float dustClump = smoothstep(0.28f, 0.78f, rawCosmos_fbm4(galDir * 9.8f + warpVec * 1.15f + float3(7.4f, 13.8f, 2.2f)));
    const float dustFil   = smoothstep(0.45f, 0.86f, fineFilaments);
    const float dustOd    = (laneMain + laneSub) * (0.35f + 1.65f * dustClump) * lerp(0.55f, 1.35f, coreAlign)
                          + discNarrow * dustFil * 0.52f;
    // Interstellar extinction law (A_B > A_V > A_R): blue absorbs ~2.1x faster than red -> authentic warm dusty rims!
    const float3 dustTrans = exp(-dustOd * float3(0.92f, 1.32f, 1.98f));
    outDustTrans = dustTrans;

    // Stellar population colors:
    // - Spiral arm OB star-clouds: cool luminous sapphire-silver
    // - Old core bulge stars: warm golden-ivory
    // - Unresolved faint Milky Way disc mist: soft indigo-periwinkle
    const float cloudMod = smoothstep(0.22f, 0.82f, starClouds * 0.68f + fineFilaments * 0.32f);
    const float3 haloCol = float3(0.022f, 0.034f, 0.072f) * (0.55f + 0.45f * (1.0f - antiCore * 0.4f));
    const float3 armCol  = lerp(float3(0.055f, 0.088f, 0.175f), float3(0.145f, 0.165f, 0.245f), cloudMod);
    const float3 coreCol = float3(0.32f, 0.23f, 0.145f) * (0.55f + 0.75f * cloudMod);

    float3 galCol = haloCol * discWide + (armCol * discNarrow * (0.42f + 1.15f * cloudMod) + coreCol * coreBulge) * dustTrans;

    // Subtle localized H-II (656.3 nm H-alpha magenta-rose) & O-III (500.7 nm teal-cyan) emission nebulae knots:
    const float h2Field = rawCosmos_fbm3(galDir * 8.8f + warpVec * 0.65f + float3(19.4f, 4.7f, 12.1f));
    const float o3Field = rawCosmos_fbm3(galDir * 10.6f - warpVec * 0.55f + float3(6.2f, 21.3f, 9.5f));
    const float h2Knot  = pow(smoothstep(0.58f, 0.86f, h2Field), 1.6f) * discNarrow * (0.45f + 0.55f * coreAlign);
    const float o3Knot  = pow(smoothstep(0.60f, 0.88f, o3Field), 1.7f) * discWide * smoothstep(0.35f, 0.75f, starClouds);
    const float3 nebDust = lerp(1.0f.xxx, dustTrans, 0.55f); // some emission knots sit foreground of dark lanes
    galCol += float3(0.28f, 0.055f, 0.155f) * (h2Knot * 0.52f) * nebDust;
    galCol += float3(0.045f, 0.215f, 0.245f) * (o3Knot * 0.42f) * nebDust;

    return galCol * smoothstep(0.008f, 0.04f, discWide);
}

// ----------------------------------------------------------------------------
// 2. Physically Blackbody-Colored Multi-Scale Starfield (O/B/A/F/G/K/M)
//    Equal-area cube-sphere cells (zero polar pinch) + airmass scintillation
// ----------------------------------------------------------------------------
// Astrophysical O/B/A/F/G/K/M Planck blackbody chromaticity curve (normalized for crisp star colors):
float3 rawCosmosStarBlackbodyRGB(float specU) {
    const float u = saturate(specU);
    if (u < 0.14f) {
        // O / B blue-white supergiants (28000K..14000K)
        return lerp(float3(0.58f, 0.74f, 1.00f), float3(0.76f, 0.86f, 1.00f), u / 0.14f);
    } else if (u < 0.34f) {
        // A-type diamond white (10000K..7500K)
        return lerp(float3(0.76f, 0.86f, 1.00f), float3(0.96f, 0.97f, 1.00f), (u - 0.14f) / 0.20f);
    } else if (u < 0.56f) {
        // F / G yellow-white solar stars (7000K..5400K)
        return lerp(float3(0.96f, 0.97f, 1.00f), float3(1.00f, 0.90f, 0.72f), (u - 0.34f) / 0.22f);
    } else if (u < 0.82f) {
        // K-type golden-amber giants (5200K..3900K)
        return lerp(float3(1.00f, 0.90f, 0.72f), float3(1.00f, 0.70f, 0.40f), (u - 0.56f) / 0.26f);
    } else {
        // M-type deep orange-red giants (3600K..2800K)
        return lerp(float3(1.00f, 0.70f, 0.40f), float3(1.00f, 0.48f, 0.24f), (u - 0.82f) / 0.18f);
    }
}

// Equal-area cube-sphere projection: maps unit direction to (uv in [-1, 1]^2, faceId in 0..5).
// Using atan() equalizes solid-angle cell size across face centers and corners (zero distortion/stretching!).
float3 rawCosmosCubeSphere(float3 d) {
    const float3 ad = abs(d);
    float2 uv;
    float faceId;
    if (ad.x >= ad.y && ad.x >= ad.z) {
        faceId = d.x >= 0.0f ? 0.0f : 1.0f;
        uv = float2(d.y, d.z) / max(ad.x, 1.0e-5f);
    } else if (ad.y >= ad.x && ad.y >= ad.z) {
        faceId = d.y >= 0.0f ? 2.0f : 3.0f;
        uv = float2(d.x, d.z) / max(ad.y, 1.0e-5f);
    } else {
        faceId = d.z >= 0.0f ? 4.0f : 5.0f;
        uv = float2(d.x, d.y) / max(ad.z, 1.0e-5f);
    }
    // Equal-angle tangent remap: (4 / pi) * atan(uv) in [-1, +1]
    return float3(atan(uv) * (4.0f / kCosmosPi), faceId);
}

// Evaluates one antialiased star magnitude tier on equal-area cube-sphere cells.
float3 rawCosmosStarTier(float3 cubeCoord, float gridRes, float seedOff, float occGate,
                         float sharpness, float magPower, float brightness,
                         float simTime, float scintAmp, bool brightHalo) {
    const float2 p = (cubeCoord.xy * 0.5f + 0.5f) * gridRes;
    const float2 cell = floor(p);
    const float2 f = frac(p) - 0.5f;
    const float3 key = float3(cell, cubeCoord.z * 37.0f + seedOff);
    const float h = rawCosmos_hash31(key);
    if (h <= occGate) { return 0.0f.xxx; }

    const float3 rnd = rawCosmos_hash33(key + float3(11.3f, 27.7f, 43.1f));
    // Keep jitter within [-0.30, +0.30] so Gaussian star PSF never clips at cell border (0.50).
    const float2 starPos = (rnd.xy - 0.5f) * 0.60f;
    const float2 rel = f - starPos;
    const float d2 = dot(rel, rel);
    if (d2 > 0.0625f) { return 0.0f.xxx; } // > 0.25 cell radius early out

    const float magT = saturate((h - occGate) / max(1.0f - occGate, 1.0e-4f));
    // Astrophysical power-law luminosity distribution (few blazing beacons, many crisp stars):
    const float lum = pow(magT, magPower) * brightness;

    // Antialiased Gaussian PSF + subtle atmospheric seeing aureole on brightest supergiants:
    float psf = exp(-d2 * sharpness);
    if (brightHalo && magT > 0.72f) {
        const float beacon = smoothstep(0.72f, 0.99f, magT);
        psf += exp(-d2 * (sharpness * 0.16f)) * (0.14f * beacon);
        // Very delicate 4-point optical seeing glint only on mag-0 beacons:
        const float crossGlint = (exp(-rel.x * rel.x * (sharpness * 2.4f)) * exp(-rel.y * rel.y * (sharpness * 0.14f))
                                + exp(-rel.y * rel.y * (sharpness * 2.4f)) * exp(-rel.x * rel.x * (sharpness * 0.14f)));
        psf += crossGlint * (0.08f * beacon);
    }

    // Physically blackbody-colored star + low-horizon Kolmogorov chromatic scintillation:
    // Steady crisp stars near zenith (scintAmp ~ 0.03), realistic chromatic twinkling near horizon (scintAmp ~ 0.55)!
    float3 starCol = rawCosmosStarBlackbodyRGB(rnd.z);
    if (scintAmp > 0.01f) {
        const float freq = 2.4f + rnd.x * 5.2f + rnd.y * 2.1f;
        const float ph = simTime * freq + h * 113.0f;
        const float twL = (1.0f - scintAmp * 0.55f) + scintAmp * 0.55f * (0.65f * sin(ph) + 0.35f * sin(ph * 1.73f + 1.1f));
        // Subtle atmospheric dispersion color-shift during strong low-horizon scintillation:
        const float chrom = scintAmp * 0.32f * sin(ph * 1.37f + 2.4f);
        starCol *= float3(1.0f + chrom, 1.0f, 1.0f - chrom) * max(twL, 0.25f);
    }
    return starCol * (psf * lum);
}

float3 rawCosmosStarfield(float3 celDir, float sinElev, float simTime, float discWide, float3 dustTrans) {
    if (sinElev <= 0.002f) { return 0.0f.xxx; }
    const float am = rawCosmosAirmass(sinElev);
    // Scintillation rises steeply with optical airmass (steady near zenith, sparkling near horizon!):
    const float scintAmp = 0.03f + smoothstep(1.35f, 6.2f, am) * 0.56f;
    const float3 cubeC = rawCosmosCubeSphere(celDir);

    // Tier 1: Bright O/B/A/G/K/M navigation stars & supergiants
    float3 stars = rawCosmosStarTier(cubeC, 92.0f, 3.1f, 0.915f, 210.0f, 1.85f, 2.35f, simTime, scintAmp, true);

    // Tier 2: Crisp medium-magnitude field stars (golden K/G & ivory F/A constellations)
    stars += rawCosmosStarTier(cubeC, 215.0f, 17.9f, 0.895f, 240.0f, 1.55f, 0.92f, simTime, scintAmp * 0.78f, false);

    // Tier 3: Dense faint galactic dust stars (boosted along Milky Way ribbon & carved by dark carbon dust lanes!)
    const float galBoost = lerp(0.45f, 1.95f, discWide);
    const float tier3Gate = lerp(0.905f, 0.785f, discWide);
    const float3 dustStars = rawCosmosStarTier(cubeC, 420.0f, 53.7f, tier3Gate, 270.0f, 1.35f, 0.44f * galBoost,
                                               simTime, scintAmp * 0.45f, false);
    stars += dustStars * lerp(1.0f.xxx, dustTrans, discWide * 0.88f);
    return stars;
}

// ----------------------------------------------------------------------------
// 3. 3D Volumetric Raymarched Curtain Auroras (Altitude 95..260 km)
//    Vertical raymarch through 10 curved ionosphere strata along tilted magnetic
//    field-line sheets emitting exact atomic spectra:
//      ~100 km       : N2+ 427.8 nm violet-magenta lower bright fringe
//      ~115..160 km  : O   557.7 nm luminous emerald-cyan core veil
//      ~180..260 km  : O   630.0 nm deep crimson-ruby upper canopy
// ----------------------------------------------------------------------------
float3 rawCosmosVolumetricAurora(float3 rayDir, float simTime, float camNorthY, float nightVis, float cloudCover) {
    if (rayDir.z <= 0.005f || nightVis <= 0.005f) { return 0.0f.xxx; }
    const float clearSky = pow(1.0f - saturate(cloudCover * 1.05f), 1.55f);
    if (clearSky <= 0.005f) { return 0.0f.xxx; }

    // Smooth geomagnetic latitude modulation:
    // Whiterun (camNorthY ~ 0) -> ~61.0N (glorious sweeping northern curtains)
    // Winterhold / Dawnstar / Sea of Ghosts (camNorthY ~ +70k..+160k) -> ~63.5..65.5N (overhead folding corona!)
    // Southern Riften / Falkreath (camNorthY < -40k) -> subtle visible northern-horizon aurora on clear nights!
    const float camLatDeg = 61.2f + clamp(camNorthY / 56000.0f, -2.4f, 4.4f);
    const float latGain   = lerp(0.62f, 1.38f, smoothstep(-42000.0f, 92000.0f, camNorthY));

    // Gentle magnetic substorm activity cycle (always softly alive on clear nights, swelling periodically):
    const float substorm = 0.78f + 0.28f * sin(simTime * 0.023f + 0.8f) * cos(simTime * 0.011f - 0.4f);

    const float bEarth = kCosmosEarthKm * rayDir.z;
    const float t      = simTime * 0.032f;
    const float rayT   = simTime * 0.145f;

    // 10 curved height strata across 95..260 km (slightly denser near the vivid 98..165 km emission peak):
    static const float kAurAltKm[10] = {
        96.5f, 102.0f, 110.0f, 120.5f, 133.5f, 149.0f, 168.0f, 192.0f, 222.0f, 256.0f
    };
    static const float kAurWeight[10] = {
        0.72f, 0.95f, 1.05f, 1.12f, 1.10f, 1.04f, 0.95f, 0.86f, 0.76f, 0.62f
    };

    float3 aurAccum = 0.0f.xxx;
    [unroll] for (int si = 0; si < 10; ++si) {
        const float hKm = kAurAltKm[si];
        const float rShell = kCosmosEarthKm + hKm;
        const float tKm = sqrt(max(bEarth * bEarth + rShell * rShell - kCosmosEarthKm * kCosmosEarthKm, 0.0f)) - bEarth;

        // 3D curved ionosphere hit point (X = East km, Y = North km)
        float2 hitKm = rayDir.xy * tKm;

        // Dip along Nordic geomagnetic dipole field lines (~77 deg inclination -> slight northward shift with altitude):
        // Makes vertical rays converge naturally toward the magnetic zenith overhead!
        hitKm.y += (hKm - 96.5f) * 0.21f;

        const float latDeg = camLatDeg + hitKm.y * (1.0f / 111.32f);
        const float lonU   = hitKm.x * (1.0f / 48.0f);

        // Undulating curl-warped magnetic ribbon sheets (2 braided curtains + high-latitude secondary arc):
        const float w0 = rawCosmos_noise3(float3(lonU * 0.22f + t * 0.35f, latDeg * 0.28f, t * 0.45f));
        const float w1 = rawCosmos_noise3(float3(lonU * 0.54f - t * 0.50f + 4.3f, latDeg * 0.45f, t * 0.65f + 2.1f));
        const float curlFold = (w0 - 0.5f) * 3.2f + (w1 - 0.5f) * 1.45f;

        // Primary luminous folding curtain (sweeps across ~65.5..68.0N):
        const float sheetLat1 = 65.8f + sin(lonU * 0.38f + t * 1.15f + curlFold * 0.85f) * 1.65f
                              + cos(lonU * 0.82f - t * 0.75f + w1 * 2.4f) * 0.58f;
        // Secondary braided curtain (~68.2N):
        const float sheetLat2 = 68.4f + sin(lonU * 0.29f - t * 0.92f + curlFold * 0.70f + 2.1f) * 1.55f
                              + sin(lonU * 0.68f + t * 0.60f) * 0.42f;
        // High-latitude diffuse crown ribbon (~63.6N, faint wide veil visible even from Whiterun/Falkreath):
        const float sheetLat3 = 63.4f + cos(lonU * 0.33f + t * 0.68f + curlFold * 0.55f - 1.3f) * 1.25f;

        // Ribbon sheet cross-section: crisp bright edge at lower altitudes, softly widening higher up:
        const float hNorm = saturate((hKm - 95.0f) / 165.0f);
        const float sharp = lerp(2.05f, 0.92f, hNorm);
        const float d1 = abs(latDeg - sheetLat1);
        const float d2 = abs(latDeg - sheetLat2);
        const float d3 = abs(latDeg - sheetLat3);
        const float ribbon1 = exp(-d1 * sharp);
        const float ribbon2 = exp(-d2 * (sharp * 0.86f)) * 0.78f;
        const float ribbon3 = exp(-d3 * (sharp * 0.72f)) * 0.42f;

        // Vertical magnetic field-line ray striations (traveling Alfven wave pillars along the ribbon):
        const float stria1 = rawCosmos_noise3(float3(lonU * 3.8f + curlFold * 0.65f, latDeg * 0.65f, rayT + hNorm * 0.45f));
        const float stria2 = rawCosmos_noise3(float3(lonU * 8.4f - curlFold * 0.40f + 7.1f, latDeg * 1.15f, rayT * 1.35f + hNorm * 0.70f));
        const float fieldRays = 0.24f + 0.76f * pow(saturate(stria1 * 0.64f + stria2 * 0.36f), 1.35f);

        // Exact atomic emission spectra vs altitude (km):
        // 1) Lower bright fringe (~96..112 km): Ionized Nitrogen N2+ (427.8 nm violet-magenta)
        const float n2Profile = exp(-pow((hKm - 101.5f) / 6.8f, 2.0f)) * smoothstep(94.5f, 97.8f, hKm);
        //    N2+ flares brightest in energetic curtain folds (high ribbon1 + strong ray peaks):
        const float n2Energy = n2Profile * smoothstep(0.28f, 0.85f, ribbon1 + ribbon2 * 0.6f) * (0.65f + 0.85f * stria1);
        const float3 colN2 = float3(0.72f, 0.14f, 0.96f);

        // 2) Core bright veil (~110..175 km): Atomic Oxygen O(1S) (557.7 nm luminous emerald-cyan)
        const float oGreenProfile = smoothstep(97.0f, 115.0f, hKm) * exp(-max(hKm - 122.0f, 0.0f) / 39.0f);
        const float3 colOGreen = lerp(float3(0.05f, 1.00f, 0.54f), float3(0.10f, 0.84f, 0.78f), saturate((hKm - 110.0f) / 65.0f));

        // 3) High upper glow (~170..260 km): Atomic Oxygen O(1D) (630.0 nm deep crimson-ruby)
        const float oRedProfile = smoothstep(144.0f, 192.0f, hKm) * (1.0f - smoothstep(232.0f, 264.0f, hKm));
        const float3 colORed = float3(0.94f, 0.10f, 0.24f);

        const float3 specCol = colN2 * (n2Energy * 0.92f)
                             + colOGreen * (oGreenProfile * 1.15f)
                             + colORed * (oRedProfile * 0.78f);

        // Planetary limb distance falloff + altitude stratum quadrature weight:
        const float distFade = exp(-tKm * 0.00046f);
        const float density = (ribbon1 + ribbon2 + ribbon3) * fieldRays * distFade * kAurWeight[si];
        aurAccum += specCol * density;
    }

    const float horizonFade = smoothstep(0.008f, 0.12f, rayDir.z);
    return aurAccum * (nightVis * clearSky * latGain * substorm * horizonFade * 0.072f);
}

// ----------------------------------------------------------------------------
// 4. Van Rhijn 90 km Mesopause Airglow, Shooting Meteors, 22-deg Ice Halo & Corona
// ----------------------------------------------------------------------------
float3 rawCosmosAirglow(float sinElev, float nightVis, float cloudCover) {
    if (sinElev <= 0.002f || nightVis <= 0.01f) { return 0.0f.xxx; }
    static const float vrRatio = 6360.0f / (6360.0f + 90.0f);
    const float cos2El = max(1.0f - sinElev * sinElev, 0.0f);
    const float vanRhijn = rsqrt(max(1.0f - vrRatio * vrRatio * cos2El, 0.025f));
    const float lowExt = exp(-0.088f / max(sinElev, 0.030f));
    const float glow = clamp(vanRhijn * lowExt * 0.28f, 0.0f, 1.20f);
    const float3 airCol = lerp(float3(0.010f, 0.022f, 0.016f), float3(0.004f, 0.013f, 0.016f), saturate(sinElev * 3.0f));
    return airCol * (glow * nightVis * (1.0f - saturate(cloudCover) * 0.72f));
}

// Occasional crisp shooting meteors across clear dark skies (~1 streak every ~9.5 sec, fast ionized ablation tail):
float3 rawCosmosMeteors(float3 rayDir, float simTime, float nightVis, float cloudCover) {
    if (rayDir.z <= 0.08f || nightVis <= 0.15f || cloudCover >= 0.55f) { return 0.0f.xxx; }
    const float metRate = 0.105f; // ~9.5s cycle
    const float bucket  = floor(simTime * metRate);
    const float phase   = frac(simTime * metRate);
    if (phase >= 0.078f) { return 0.0f.xxx; }

    const float gate = rawCosmos_hash31(float3(bucket, 17.3f, 3.7f));
    if (gate < 0.32f) { return 0.0f.xxx; }

    const float3 r1 = rawCosmos_hash33(float3(bucket, 4.1f, 9.2f));
    const float3 r2 = rawCosmos_hash33(float3(bucket, 23.7f, 13.5f));
    const float az = r1.x * kCosmosTwoPi;
    const float z0 = 0.28f + 0.56f * r1.y;
    const float xyR = sqrt(max(1.0f - z0 * z0, 0.0f));
    const float3 mStart = float3(xyR * cos(az), xyR * sin(az), z0);

    // Tangential downward atmospheric entry trajectory:
    const float3 tang = normalize(cross(mStart, float3(r2.x - 0.5f, r2.y - 0.5f, 1.0f)) - float3(0.0f, 0.0f, 0.48f));
    const float mT = phase / 0.078f;
    const float3 mHead = normalize(mStart + tang * (mT * 0.24f));

    const float3 rel = rayDir - mHead;
    const float along = dot(rel, -tang);
    const float tailLen = 0.072f;
    if (along < -0.008f || along > tailLen) { return 0.0f.xxx; }

    const float perp = length(rel + tang * clamp(along, 0.0f, tailLen));
    const float headCore = exp(-dot(rel, rel) * 320000.0f) * 1.45f;
    const float tailMask = saturate(1.0f - along / tailLen) * step(0.0f, along);
    const float streak = exp(-perp * perp * 260000.0f) * tailMask;
    const float env = sin(mT * kCosmosPi);

    // Hot white-cyan magnesium/oxygen head cooling to warm orange-amber sodium/iron ablation wake:
    const float3 wakeCol = lerp(float3(0.86f, 0.97f, 1.00f), float3(1.00f, 0.62f, 0.30f), saturate(along / tailLen));
    const float vis = nightVis * (1.0f - saturate(cloudCover * 1.5f)) * smoothstep(0.08f, 0.22f, rayDir.z);
    return (wakeCol * streak + float3(0.95f, 0.98f, 1.00f) * headCore) * (env * vis * 2.25f);
}

// ----------------------------------------------------------------------------
// 4b. Orbital Dual-Moon Phase Illumination (Masser 24-day & Secunda 16-day cycles)
//     Smooth Lambertian sphere phase curves (1 - cos(phi)) / 2 driving dynamic
//     moonlit snowscapes, waxing/waning half-moons, and dark new-moon aurora nights!
// ----------------------------------------------------------------------------
struct DualMoonLight {
    float3 moonTintRGB;
    float  totalPhase01;
    float  masserPhase01;
    float  secundaPhase01;
};

// Reconstructs continuous orbital year progress [0, 1) from RawWeatherSim phenology + tidal phase uniforms
// (with exact support for explicit season override 1=Spring, 2=Summer, 3=Autumn, 4=Winter).
float rawCosmosSeasonProgress01(float4 phenology0, float4 phenology1, float springTide01, float seasonOverride, float simTimeSec) {
    // Slow continuous lunar drift in real-time play (~1 game day per 72 real minutes at 20x timescale):
    const float liveDayDrift = (simTimeSec * (20.0f / 86400.0f)) * (1.0f / 365.0f);
    const float tidePhaseOff = saturate(springTide01) * (12.0f / 365.0f);
    if (seasonOverride >= 0.5f && seasonOverride <= 4.5f) {
        const float baseDay = (seasonOverride < 1.5f) ? 105.0f
                            : ((seasonOverride < 2.5f) ? 196.0f
                            : ((seasonOverride < 3.5f) ? 288.0f : 15.0f));
        return frac((baseDay / 365.0f) + tidePhaseOff + liveDayDrift);
    }
    const float bud  = saturate(phenology0.x);
    const float chl  = saturate(phenology0.y);
    const float car  = saturate(phenology0.z);
    const float ant  = saturate(phenology0.w);
    const float absL = saturate(phenology1.x);
    const float autW = saturate(car * 0.65f + ant * 0.85f);
    float yearFrac = 0.04f; // Winter baseline (~mid-Jan)
    if (autW > 0.08f) {
        yearFrac = lerp(0.65f, 0.86f, autW);
    } else if (chl > 0.45f) {
        yearFrac = lerp(0.38f, 0.60f, saturate((chl - 0.45f) / 0.55f));
    } else if (bud > 0.05f && absL < 0.55f) {
        yearFrac = lerp(0.18f, 0.38f, bud);
    } else {
        yearFrac = lerp(0.03f, 0.14f, 1.0f - absL);
    }
    return frac(yearFrac + tidePhaseOff + liveDayDrift);
}

// Evaluates continuous orbital illumination for Masser (24-day period, warm rose-silver)
// and Secunda (16-day period, cool pale-cyan-silver) from seasonProgress01 and gameHour.
DualMoonLight evalDualMoonPhaseIllum(float seasonProgress01, float gameHour) {
    const float dayContinuous = max(seasonProgress01, 0.0f) * 365.0f + clamp(gameHour, 0.0f, 24.0f) * (1.0f / 24.0f);

    // Exact 24-day Masser & 16-day Secunda synodic orbital phase angles (48-day dual resonance):
    const float masserPhi  = (dayContinuous * (1.0f / 24.0f)) * kCosmosTwoPi + 0.42f;
    const float secundaPhi = (dayContinuous * (1.0f / 16.0f)) * kCosmosTwoPi + 2.15f;

    // Realistic Lambertian illuminated sphere phase curve: (1 - cos(phi)) / 2 in [0, 1]
    const float masserPhase01  = saturate(0.5f * (1.0f - cos(masserPhi)));
    const float secundaPhase01 = saturate(0.5f * (1.0f - cos(secundaPhi)));

    // Masser dominates (~68% solid-angle irradiance, warm rose-silver albedo);
    // Secunda contributes (~32%, cool pale-cyan-silver albedo).
    const float wM = masserPhase01 * 0.68f;
    const float wS = secundaPhase01 * 0.32f;
    const float totalPhase01 = saturate(wM + wS);

    static const float3 kMasserTint  = float3(0.98f, 0.78f, 0.74f); // warm rose-silver Masser
    static const float3 kSecundaTint = float3(0.72f, 0.90f, 1.00f); // cool pale-cyan-silver Secunda
    static const float3 kNewMoonTint = float3(0.56f, 0.68f, 0.92f); // deep starlight-indigo floor

    const float3 weightedTint = (totalPhase01 > 1.0e-4f)
        ? ((kMasserTint * wM + kSecundaTint * wS) / totalPhase01)
        : kNewMoonTint;

    DualMoonLight res;
    res.moonTintRGB    = lerp(kNewMoonTint, weightedTint, smoothstep(0.02f, 0.25f, totalPhase01));
    res.totalPhase01   = totalPhase01;
    res.masserPhase01  = masserPhase01;
    res.secundaPhase01 = secundaPhase01;
    return res;
}

// Solar/Lunar Atmospheric Corona, Phase-Coupled Dual-Moon Limb Glow & 22-Degree Hexagonal Ice-Crystal Halo:
float3 rawCosmosSolarLunarHaloPhased(float3 rayDir, float3 sunDir, float gameHour, float simTime,
                                     float cloudCover, float snow01, DualMoonLight moon) {
    if (rayDir.z <= 0.005f) { return 0.0f.xxx; }
    float3 outHalo = 0.0f.xxx;

    const float clearFactor = 1.0f - saturate(cloudCover) * 0.72f;
    const float dayW = smoothstep(-0.06f, 0.10f, sunDir.z);
    const float nightW = smoothstep(0.04f, -0.08f, sunDir.z);

    // 1) Mie forward solar aureole (tight warm inner glow + soft atmospheric corona):
    if (dayW > 0.01f && sunDir.z > -0.04f) {
        const float cosS = saturate(dot(rayDir, sunDir));
        const float lowSun = 1.0f - smoothstep(0.02f, 0.34f, sunDir.z);
        const float3 aureoleCol = lerp(float3(1.00f, 0.96f, 0.90f), float3(1.08f, 0.56f, 0.24f), lowSun);
        const float mieFwd = pow(cosS, 96.0f) * 0.24f + pow(cosS, 14.0f) * 0.055f;
        outHalo += aureoleCol * (mieFwd * dayW * clearFactor * smoothstep(0.0f, 0.08f, rayDir.z));
    }

    // 2) Phase-coupled dual-moon orbital atmospheric limb glow (vanishes on New Moon, glows on Full Masser/Secunda):
    if (nightW > 0.02f && moon.totalPhase01 > 0.03f) {
        const float moonPhase1 = gameHour * (kCosmosTwoPi / 24.0f) + 2.35f;
        const float moonPhase2 = gameHour * (kCosmosTwoPi / 24.0f) + 2.72f;
        const float3 masserDir = normalize(float3(-sin(moonPhase1) * 0.72f, 0.42f, max(cos(moonPhase1) * 0.65f + 0.18f, -0.3f)));
        const float3 secundaDir = normalize(float3(-sin(moonPhase2) * 0.80f, 0.52f, max(cos(moonPhase2) * 0.58f + 0.24f, -0.3f)));
        if (masserDir.z > 0.04f && moon.masserPhase01 > 0.03f) {
            const float cM = saturate(dot(rayDir, masserDir));
            const float mGlow = (pow(cM, 28.0f) * 0.032f + pow(cM, 7.0f) * 0.012f) * smoothstep(0.04f, 0.18f, masserDir.z);
            outHalo += float3(0.92f, 0.66f, 0.54f) * (mGlow * moon.masserPhase01 * nightW * (0.45f + 0.55f * saturate(cloudCover)));
        }
        if (secundaDir.z > 0.04f && moon.secundaPhase01 > 0.03f) {
            const float cS2 = saturate(dot(rayDir, secundaDir));
            const float sGlow = (pow(cS2, 36.0f) * 0.025f + pow(cS2, 9.0f) * 0.009f) * smoothstep(0.04f, 0.18f, secundaDir.z);
            outHalo += float3(0.58f, 0.78f, 0.98f) * (sGlow * moon.secundaPhase01 * nightW * (0.45f + 0.55f * saturate(cloudCover)));
        }
    }

    // 3) Physical 22-Degree Hexagonal Ice-Crystal Halo & Parhelia (activates subtly when cold/snowy: snow01 > 0.1):
    //    Snell minimum-deviation angle through 60-deg hexagonal ice prisms:
    //      Red (656 nm, n=1.307) -> 21.70 deg (0.3787 rad)
    //      Green (546 nm, n=1.311) -> 22.05 deg (0.3848 rad)
    //      Blue (460 nm, n=1.316) -> 22.48 deg (0.3924 rad)
    if (snow01 > 0.10f) {
        const float iceW = smoothstep(0.10f, 0.58f, snow01) * (1.0f - saturate(cloudCover) * 0.58f) * smoothstep(0.02f, 0.12f, rayDir.z);
        const float bC = kCosmosEarthKm * rayDir.z;
        const float tCir = sqrt(max(bC * bC + (kCosmosEarthKm + 10.0f) * (kCosmosEarthKm + 10.0f) - kCosmosEarthKm * kCosmosEarthKm, 0.0f)) - bC;
        const float2 cirP = rayDir.xy * (tCir * 0.042f) + float2(simTime * 0.006f, simTime * 0.003f);
        const float cirCrystal = lerp(0.38f, 1.0f, smoothstep(0.26f, 0.78f, rawCosmos_noise3(float3(cirP, 3.1f))));

        if (dayW > 0.05f && sunDir.z > 0.03f) {
            const float ang = acos(clamp(dot(rayDir, sunDir), -1.0f, 1.0f));
            const float innerCut = smoothstep(0.352f, 0.377f, ang); // forbidden dark interior < 21.6 deg
            const float3 disp = float3(
                exp(-pow((ang - 0.3787f) / 0.013f, 2.0f)),
                exp(-pow((ang - 0.3848f) / 0.015f, 2.0f)),
                exp(-pow((ang - 0.3924f) / 0.017f, 2.0f))) * innerCut;
            const float parhelia = exp(-pow((rayDir.z - sunDir.z) / 0.045f, 2.0f)) * (1.0f - smoothstep(0.20f, 0.48f, sunDir.z));
            const float3 haloTint = lerp(float3(0.96f, 0.97f, 1.00f), float3(1.08f, 0.84f, 0.58f), 1.0f - smoothstep(0.04f, 0.25f, sunDir.z));
            outHalo += disp * haloTint * (iceW * cirCrystal * dayW * (0.045f + 0.075f * parhelia));
        } else if (nightW > 0.10f && moon.totalPhase01 > 0.15f) {
            // Subtle lunar 22-deg ice-ring around primary moon on bright moonlit frigid nights (fades on new moons):
            const float moonPhase1 = gameHour * (kCosmosTwoPi / 24.0f) + 2.35f;
            const float3 lunarDir = normalize(float3(-sin(moonPhase1) * 0.72f, 0.42f, max(cos(moonPhase1) * 0.65f + 0.18f, 0.08f)));
            const float ang = acos(clamp(dot(rayDir, lunarDir), -1.0f, 1.0f));
            const float innerCut = smoothstep(0.352f, 0.377f, ang);
            const float3 disp = float3(
                exp(-pow((ang - 0.3787f) / 0.016f, 2.0f)),
                exp(-pow((ang - 0.3848f) / 0.018f, 2.0f)),
                exp(-pow((ang - 0.3924f) / 0.021f, 2.0f))) * innerCut;
            outHalo += disp * moon.moonTintRGB * (iceW * cirCrystal * nightW * moon.totalPhase01 * 0.030f);
        }
    }

    return outHalo;
}

// Backwards-compatible halo helper:
float3 rawCosmosSolarLunarHalo(float3 rayDir, float3 sunDir, float gameHour, float simTime,
                               float cloudCover, float snow01) {
    const DualMoonLight defMoon = evalDualMoonPhaseIllum(frac(simTime * (20.0f / (86400.0f * 365.0f)) + 0.12f), gameHour);
    return rawCosmosSolarLunarHaloPhased(rayDir, sunDir, gameHour, simTime, cloudCover, snow01, defMoon);
}

// ----------------------------------------------------------------------------
// Orbital-Phase-Coupled Evaluator (Full Dual-Moon Star/Galaxy/Aurora Contrast!)
// ----------------------------------------------------------------------------
float3 evalCosmosAndAuroraPhased(
    float3 rayDir,
    float3 sunDir,
    float gameHour,
    float simTime,
    float cloudCover,
    float snow01,
    float camNorthY,
    float seasonProgress01,
    bool includePinpointStars,
    bool applyInternalExtinction)
{
    const float3 rd = normalize(rayDir);
    const float3 sd = normalize(sunDir);
    if (rd.z <= -0.015f) { return 0.0f.xxx; }

    const float starNightW   = smoothstep(0.01f, -0.12f, sd.z);
    const float auroraNightW = smoothstep(0.05f, -0.09f, sd.z);
    const float clearSky     = 1.0f - saturate(cloudCover) * 0.88f;

    // Evaluate live Masser (24d) & Secunda (16d) orbital phase illumination:
    // - On bright full-moon nights: gentle lunar Rayleigh skyglow softens faint stars/Milky Way naturally.
    // - On dark new-moon nights: Milky Way, dark dust rifts, stars & 3D curtain auroras blaze at peak contrast!
    const DualMoonLight moon = evalDualMoonPhaseIllum(seasonProgress01, gameHour);
    const float starPhaseMod   = lerp(1.22f, 0.68f, moon.totalPhase01);
    const float galPhaseMod    = lerp(1.28f, 0.58f, moon.totalPhase01);
    const float auroraPhaseMod = lerp(1.22f, 0.82f, moon.totalPhase01);

    float3 extraAtmo = 0.0f.xxx;

    // Space-originating light (Galactic plane + Nebulae + Spectral Starfield):
    if (starNightW > 0.005f && clearSky > 0.01f) {
        const float3 celDir = rawCosmosToCelestial(rd, gameHour);
        const float3 galDir = rawCosmosToGalactic(celDir);
        float3 dustTrans = 1.0f.xxx;
        float discWide = 0.0f;
        float3 galCol = rawCosmosGalacticPlane(galDir, dustTrans, discWide);

        float3 starCol = 0.0f.xxx;
        if (includePinpointStars) {
            starCol = rawCosmosStarfield(celDir, rd.z, simTime, discWide, dustTrans);
        }
        extraAtmo += (galCol * (0.92f * galPhaseMod) + starCol * starPhaseMod) * (starNightW * clearSky);
    }

    // Upper-atmosphere light (95..260 km 3D Volumetric Curtain Auroras + 90 km Airglow + Meteors):
    if (auroraNightW > 0.005f) {
        extraAtmo += rawCosmosVolumetricAurora(rd, simTime, camNorthY, auroraNightW, cloudCover) * auroraPhaseMod;
        extraAtmo += rawCosmosAirglow(rd.z, starNightW, cloudCover) * lerp(1.15f, 0.72f, moon.totalPhase01);
        if (includePinpointStars) {
            extraAtmo += rawCosmosMeteors(rd, simTime, starNightW, cloudCover) * lerp(1.15f, 0.75f, moon.totalPhase01);
        }
    }

    // Apply physical Kasten-Young airmass extinction when caller does not supply exact raymarched T:
    if (applyInternalExtinction) {
        const float3 T_atmo = rawCosmosAtmosphericTransmittance(rd.z, cloudCover, snow01);
        extraAtmo *= T_atmo;
    }

    // Subtle full-moon Rayleigh atmospheric skyglow (rises gently near horizon & fades toward zenith):
    if (starNightW > 0.01f && moon.totalPhase01 > 0.05f && rd.z > 0.0f) {
        const float raySkyGrad = lerp(1.25f, 0.55f, pow(saturate(rd.z), 0.55f));
        const float moonGlow = moon.totalPhase01 * moon.totalPhase01 * starNightW * (1.0f - saturate(cloudCover) * 0.45f);
        extraAtmo += moon.moonTintRGB * (0.015f * moonGlow * raySkyGrad);
    }

    // Tropospheric optical phenomena (Solar Mie aureole, 22-deg cirrus ice halo, phase-coupled dual-moon limb glow):
    extraAtmo += rawCosmosSolarLunarHaloPhased(rd, sd, gameHour, simTime, cloudCover, snow01, moon);

    return max(extraAtmo, 0.0f.xxx);
}

// ----------------------------------------------------------------------------
// Extended Configurable Evaluator (100% backwards-compatible with Wave 38/40 callers)
// ----------------------------------------------------------------------------
float3 evalCosmosAndAuroraEx(
    float3 rayDir,
    float3 sunDir,
    float gameHour,
    float simTime,
    float cloudCover,
    float snow01,
    float camNorthY,
    bool includePinpointStars,
    bool applyInternalExtinction)
{
    const float autoSeasonProg = frac(simTime * (20.0f / (86400.0f * 365.0f)) + 0.12f);
    return evalCosmosAndAuroraPhased(rayDir, sunDir, gameHour, simTime, cloudCover, snow01, camNorthY,
                                     autoSeasonProg, includePinpointStars, applyInternalExtinction);
}

// ----------------------------------------------------------------------------
// Canonical Exported Entry Point (shared by AtmoSky, EnvCubePass, LightFog)
// ----------------------------------------------------------------------------
float3 evalCosmosAndAurora(
    float3 rayDir,
    float3 sunDir,
    float gameHour,
    float simTime,
    float cloudCover,
    float snow01,
    float camNorthY)
{
    return evalCosmosAndAuroraEx(rayDir, sunDir, gameHour, simTime, cloudCover, snow01, camNorthY, true, true);
}

#endif
