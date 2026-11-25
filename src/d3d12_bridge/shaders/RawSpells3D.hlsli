// RawSpells3D.hlsli: Scene-Anchored True 3D Physical Volumetric Spell Engine for RAW (SM 6.6 - SM 6.10).
// Strictly anchored to verified real HDR magic/spell cores rendered in `Lit` + `Depth` — NEVER spawning
// opaque floating spheres around invisible Creation Engine point lights in empty air!
// Distinct physical 3D volumetric phenomena per element:
//   1) Fire Spells   : high-velocity incandescent blackbody plasma core (2200K..3600K) + trailing dark
//                      turbulent soot wake (Beer-Lambert absorption + orange bounce) + swirling ember sparks
//   2) Frost Spells  : cold cyan-white cryo-mist sinking downward (negative thermal buoyancy!) + sparkling
//                      anisotropic diamond-dust ice crystals catching live sunlight / moonlight
//   3) Shock Spells  : branching 3D multi-scale dielectric-breakdown Lichtenberg arcs (crisp white-violet
//                      ionized core + cyan ozone corona + rapid micro-flicker)
//   4) Arcane / Ward / Restoration : iridescent thin-film Airy caustic energy rings + shimmering volumetric wisps
// Returns float4(emissionRGB, backgroundTransmittance) for exact Beer-Lambert composition.
// 100% original MIT clean-room code; strictly depth-occluded against scene geometry.
#ifndef RAW_SPELLS_3D_HLSLI
#define RAW_SPELLS_3D_HLSLI

static const uint RAW_SPELL_NONE   = 0u;
static const uint RAW_SPELL_FIRE   = 1u;
static const uint RAW_SPELL_FROST  = 2u;
static const uint RAW_SPELL_SHOCK  = 3u;
static const uint RAW_SPELL_ARCANE = 4u;

// Smooth Planck blackbody ramp for volumetric spell plasma (2200K molten gold -> 3600K white-hot plasma).
float3 rawSpellBlackbody(float t) {
    float hdrRad = 0.0f;
    const float3 bb = rawPlanckBlackbodyRGB(lerp(2200.0f, 3600.0f, saturate(t)), hdrRad);
    return bb * min(hdrRad * 0.42f, 4.8f);
}

// Thin-film Airy interference iridescence for arcane / ward / restoration caustic rings (615/540/465 nm).
float3 rawSpellIridescence(float cosTheta, float phase) {
    const float opd = 420.0f + 260.0f * (1.0f - saturate(cosTheta)) + phase * 180.0f;
    return 0.56f + 0.44f * cos(6.2831853f * opd / float3(615.0f, 540.0f, 465.0f));
}

// Strict HDR scene-rendered magic emissive core detector:
// Requires high HDR luminance AND strong chromatic saturation, completely rejecting lit ice walls,
// blue cave bounce light, snow, water speculars, and calm world candles/campfires.
float rawDetectSceneSpellCore(float3 rgb, out uint outKind, out float3 outTint) {
    outKind = RAW_SPELL_NONE;
    outTint = 1.0f.xxx;
    const float luma = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    if (luma <= 1.60f) { return 0.0f; }
    const float cMax = max(rgb.r, max(rgb.g, rgb.b));
    const float cMin = min(rgb.r, min(rgb.g, rgb.b));
    const float sat  = (cMax - cMin) / max(cMax, 1.0e-4f);
    if (sat <= 0.38f || cMax <= 1.75f) { return 0.0f; }

    outTint = rgb / max(cMax, 1.0e-4f);

    // 1) High-Velocity Combat Fire / Plasma Spells (ultra-hot HDR core > 3.20 luma, distinct from quiet candles/hearths):
    if (luma > 3.20f && rgb.r > 3.80f && rgb.r > rgb.g * 1.12f && rgb.r > rgb.b * 1.85f) {
        outKind = RAW_SPELL_FIRE;
        outTint = float3(1.0f, 0.52f, 0.12f);
        return smoothstep(3.20f, 5.80f, luma) * smoothstep(0.45f, 0.78f, sat);
    }

    // Exclude ordinary lower-intensity warm combustion (handled by LightFog's buoyant fire/smoke plume engine):
    if (rgb.r > rgb.g * 1.15f && rgb.r > rgb.b * 2.0f) { return 0.0f; }

    const float lumW = smoothstep(1.60f, 3.60f, luma);
    const float satW = smoothstep(0.42f, 0.74f, sat);

    // 2) Shock / Lightning Spells: intense ionized violet-blue core
    if (sat > 0.42f && rgb.b > rgb.g * 1.16f && rgb.r > rgb.g * 0.78f && rgb.b > 1.85f) {
        outKind = RAW_SPELL_SHOCK;
        outTint = lerp(float3(0.62f, 0.46f, 1.38f), outTint, 0.45f);
        return lumW * satW;
    }
    // 3) Frost / Ice Spells: bright self-emissive cyan-glacial core (never lit cave ice walls!)
    if (sat > 0.42f && rgb.b > rgb.r * 1.45f && rgb.g > rgb.r * 1.12f && rgb.b > 1.80f) {
        outKind = RAW_SPELL_FROST;
        outTint = lerp(float3(0.24f, 0.84f, 1.32f), outTint, 0.45f);
        return lumW * satW;
    }
    // 4) Arcane / Ward / Conjuration / Restoration Spells:
    //    vivid emerald Alteration, magenta-violet Conjuration, or luminous golden-white Restoration/Ward
    const bool isEmerald = (sat > 0.42f && rgb.g > rgb.r * 1.28f && rgb.g > rgb.b * 1.22f && rgb.g > 1.75f);
    const bool isViolet  = (sat > 0.42f && rgb.r > rgb.g * 1.35f && rgb.b > rgb.g * 1.30f && cMax > 1.85f);
    const bool isRestore = (luma > 2.45f && rgb.g > rgb.r * 0.82f && rgb.r > rgb.b * 1.65f && rgb.r > 2.35f);
    if (isEmerald || isViolet || isRestore) {
        outKind = RAW_SPELL_ARCANE;
        if (isRestore) { outTint = lerp(float3(1.00f, 0.90f, 0.48f), outTint, 0.45f); }
        return smoothstep(1.60f, 3.80f, luma) * smoothstep(0.38f, 0.72f, sat);
    }
    return 0.0f;
}

// Scene-Anchored 3D Physical Volumetric Spell Engine called from LightFog.hlsl (exteriors & interiors).
// Returns float4(emissionRGB, backgroundTransmittance) so fire-spell soot wakes & dense cryo-mist
// attenuate background light physically while lighting up from within!
float4 evalVolumetricSpells3D(Texture2D<float4> litTex, Texture2D<float> depthTex, SamplerState linSamp,
                              float3 eye, float3 w, float dist, float curZ, float2 px, bool isInt,
                              out float2 outRefractPx) {
    outRefractPx = 0.0f.xx;
    const float jit = ign(px + float2(23.0f, 41.0f));
    const float tClock = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
    const float2 windXY = isInt ? float2(0.0f, 0.0f) : clamp(cloudWind.xy * 0.055f, -24.0f.xx, 24.0f.xx);

    // Project world +Z (up) into screen space so we can also catch Frost spell cores ABOVE sinking cold cryo-mist!
    const float4 sRef0 = screenOf(eye + w * 200.0f);
    const float4 sRefUp = screenOf(eye + w * 200.0f + float3(0.0f, 0.0f, 20.0f));
    float2 upPxDir = sRefUp.xy - sRef0.xy;
    const float upLen = length(upPxDir);
    upPxDir = (upLen > 1.0e-3f) ? (upPxDir / upLen) : float2(0.0f, -1.0f);

    // 11-tap local screen-space anchor search (center + 8 dual-ring radial probes + 2 upward probes for sinking frost mist):
    static const float2 kSpellProbe[9] = {
        float2( 0.0f,  0.0f),
        float2( 1.0f,  0.0f), float2(-1.0f,  0.0f),
        float2( 0.0f,  1.0f), float2( 0.0f, -1.0f),
        float2( 1.85f,  1.85f), float2(-1.85f,  1.85f),
        float2( 1.85f, -1.85f), float2(-1.85f, -1.85f)
    };
    const float ringPx = clamp(dims.y * 0.019f, 10.0f, 30.0f);

    float bestEnergy = 0.0f;
    uint  bestKind   = RAW_SPELL_NONE;
    float3 bestTint  = 1.0f.xxx;
    float2 bestPx    = px;
    float  bestD     = 1.0f;

    [unroll] for (int pi = 0; pi < 11; ++pi) {
        const float2 offsetPx = (pi < 9)
            ? (kSpellProbe[pi] * ringPx)
            : (upPxDir * (ringPx * (pi == 9 ? 2.8f : 4.6f)));
        const float2 qPx = clamp(px + offsetPx, float2(1.0f, 1.0f), dims.xy - 2.0f);
        const float2 qUv = (qPx + 0.5f) * dims.zw;
        const float dTap = depthTex.SampleLevel(linSamp, qUv, 0.0f);
        if (dTap >= 0.9990f) { continue; }
        const float zTap = viewZOf(dTap);
        if (zTap < 14.0f || zTap > 2000.0f) { continue; }
        // Strict foreground depth occlusion: never draw spell volumes OVER intervening walls, pillars, or shields!
        if (zTap > curZ * 1.04f + 10.0f) { continue; }

        const float3 cTap = litTex.SampleLevel(linSamp, qUv, 0.0f).rgb;
        uint kTap = RAW_SPELL_NONE;
        float3 tintTap = 1.0f.xxx;
        const float eTap = rawDetectSceneSpellCore(cTap, kTap, tintTap);
        // Upward probes (pi >= 9) ONLY anchor sinking negative-buoyancy Frost cryo-mist:
        if (pi >= 9 && kTap != RAW_SPELL_FROST) { continue; }
        const float wTap = eTap * (pi == 0 ? 1.15f : (pi <= 4 ? 0.92f : (pi < 9 ? 0.74f : 0.80f)));
        if (wTap > bestEnergy) {
            bestEnergy = wTap;
            bestKind   = kTap;
            bestTint   = tintTap;
            bestPx     = qPx;
            bestD      = dTap;
        }
    }

    // Immediate early-out on ~99% of pixels where no genuine HDR magic spell core exists:
    if (bestEnergy <= 0.02f || bestKind == RAW_SPELL_NONE) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

    // Reconstruct exact 3D world-space anchor of the verified spell core and intersect our view ray:
    const float3 pCore = worldAt(bestPx, bestD);
    const float tCenter = dot(pCore - eye, w);
    if (tCenter < 12.0f || tCenter > 2000.0f) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

    const float volR = (bestKind == RAW_SPELL_FIRE)  ? 44.0f
                     : (bestKind == RAW_SPELL_FROST) ? 52.0f
                     : (bestKind == RAW_SPELL_SHOCK) ? 42.0f : 36.0f;
    const float3 pClosest = eye + w * tCenter;
    const float3 qClose = pClosest - pCore;
    // Allow extended vertical reach below Frost cores for sinking cryo-mist and above Fire cores for soot wake:
    const float rCheck = (bestKind == RAW_SPELL_FROST)
        ? length(float3(qClose.xy, qClose.z * (qClose.z < 0.0f ? 0.42f : 1.15f)))
        : ((bestKind == RAW_SPELL_FIRE)
            ? length(float3(qClose.xy, qClose.z * (qClose.z > 0.0f ? 0.58f : 1.10f)))
            : length(qClose));
    if (rCheck >= volR * 1.12f) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

    const float tEnter = max(12.0f, tCenter - volR * 0.90f);
    const float tExit  = min(dist + 4.0f, tCenter + volR * 0.90f);
    if (tExit <= tEnter + 0.5f) { return float4(0.0f, 0.0f, 0.0f, 1.0f); }

    const float invR = 1.0f / volR;
    const float distFade = (1.0f - smoothstep(1100.0f, 2000.0f, tCenter)) * bestEnergy;

    // Localized heat-shimmer / arcane / shock refraction strictly around verified active spell core:
    const float rMid = saturate(rCheck * invR);
    if (rMid < 0.95f && tCenter < 950.0f) {
        const float ringW = exp(-pow((rMid - 0.52f) / 0.28f, 2.0f));
        const float3 wpM = camPosW.xyz + pClosest;
        const float4 gLens = rawNoise3Grad(wpM * 0.11f + float3(0.0f, 0.0f, -tClock * 4.6f));
        const float lensStr = (bestKind == RAW_SPELL_FIRE) ? 2.0f : ((bestKind == RAW_SPELL_ARCANE) ? 1.65f : 1.10f);
        outRefractPx = clamp(gLens.yz * (ringW * distFade * lensStr), -2.8f.xx, 2.8f.xx);
    }

    // Live sun/moon illumination vector for diamond-dust frost crystal glints:
    const float3 lightDirS = normalize((sunColour.w > 0.5f && sunDir.w > 0.5f) ? sunDir.xyz : float3(0.25f, 0.35f, 0.90f));
    const float3 lightColS0 = (sunColour.w > 0.5f) ? max(sunColour.rgb, float3(0.22f, 0.32f, 0.56f)) : float3(0.55f, 0.72f, 0.95f);
    const float3 ambSpell = max(float3(ambient[0].z + ambient[0].w, ambient[1].z + ambient[1].w, ambient[2].z + ambient[2].w),
                                float3(0.025f, 0.032f, 0.045f));

    const int kSteps = 10;
    const float ds = (tExit - tEnter) * (1.0f / float(kSteps));
    float3 acc = 0.0f.xxx;
    float T = 1.0f;

    [unroll] for (int si = 0; si < kSteps; ++si) {
        const float t = tEnter + (float(si) + jit) * ds;
        const float3 pRel = eye + w * t;
        const float3 q = pRel - pCore;
        const float r = length(q);
        const float rn = r * invR;
        const float3 dirQ = q / max(r, 1.0e-3f);
        const float3 wpQ = camPosW.xyz + pRel;
        const float env = saturate(1.0f - rn * rn);

        float3 emitStep = 0.0f.xxx;
        float sigmaStep = 0.0f;

        if (bestKind == RAW_SPELL_FIRE) {
            // 1) Fire Spells: High-velocity incandescent blackbody plasma core (2200K..3600K)
            //    + trailing dark turbulent soot wake + swirling ember sparks!
            const float spin = tClock * 5.8f + rn * 6.0f;
            const float cs = cos(spin), sn = sin(spin);
            const float3 qSwirl = float3(q.x * cs - q.y * sn, q.x * sn + q.y * cs, q.z);
            const float3 pTurb = float3((wpQ.xy - windXY * 0.25f) * 0.075f, wpQ.z * 0.065f - tClock * 4.8f);
            const float4 g1 = rawNoise3Grad(pTurb);
            const float3 curlF = float3(g1.z - g1.w, g1.w - g1.y, g1.y - g1.z);
            const float4 g2 = rawNoise3Grad(pTurb * 2.25f + curlF * 0.45f + float3(5.7f, -9.3f, -tClock * 2.5f));
            const float turb = saturate(g1.x * 0.60f + g2.x * 0.40f);

            // Incandescent plasma core (tight around r < 0.72 * volR):
            const float rCore = length(qSwirl + curlF * 4.2f) / (volR * 0.72f);
            const float plasmaDens = smoothstep(0.24f, 0.86f, saturate(1.0f - rCore * rCore) + (turb - 0.42f) * 0.85f);
            const float3 plasmaCol = rawSpellBlackbody(saturate(plasmaDens * (1.20f - saturate(rCore) * 0.55f)));

            // Trailing dark turbulent soot wake billowing upward/downwind from the plasma core:
            const float uWake = saturate((q.z + volR * 0.10f) / (volR * 1.65f));
            const float2 qWakeXY = q.xy - windXY * (uWake * uWake * 0.85f) + curlF.xy * (volR * 0.28f * uWake);
            const float rWake = length(qWakeXY) / max(volR * lerp(0.55f, 1.35f, uWake), 1.0f);
            const float wakeEnv = saturate(1.0f - rWake * rWake) * smoothstep(0.08f, 0.28f, uWake) * smoothstep(1.02f, 0.55f, uWake);
            const float sootDens = smoothstep(0.28f, 0.84f, wakeEnv + (turb - 0.40f) * 0.80f);
            const float fireIrradS = exp(-uWake * 3.8f) * exp(-rWake * rWake * 1.4f);
            const float downFaceS = saturate(-g1.w * 0.90f + 0.50f);
            const float3 sootCol = float3(0.042f, 0.038f, 0.038f) * ambSpell
                                 + float3(3.15f, 0.92f, 0.14f) * (fireIrradS * lerp(0.20f, 1.30f, downFaceS) * 0.42f);

            // Swirling high-velocity ember sparks orbiting the plasma vortex:
            float sparkE = 0.0f;
            if (rn < 1.15f) {
                const float3 spGrid = float3(qSwirl.xy * 0.19f, (q.z - tClock * 42.0f) * 0.14f);
                const float3 spCell = floor(spGrid);
                const float3 spRnd = rawHash33(spCell);
                if (spRnd.x > 0.91f) {
                    const float dSp = length((frac(spGrid) - 0.5f) - (spRnd - 0.5f) * 0.55f);
                    sparkE = smoothstep(0.91f, 0.995f, spRnd.x) * exp(-dSp * dSp * 55.0f) * saturate(1.15f - rn);
                }
            }

            const float sigP = plasmaDens * 0.068f;
            const float sigS = sootDens * 0.044f;
            sigmaStep = sigP + sigS + sparkE * 0.09f;
            if (sigmaStep > 1.0e-4f) {
                emitStep = (plasmaCol * sigP + sootCol * sigS + float3(3.60f, 1.85f, 0.42f) * (sparkE * 0.14f)) / sigmaStep;
            }
        } else if (bestKind == RAW_SPELL_FROST) {
            // 2) Frost Spells: Cold cyan-white volumetric cryo-mist SINKING DOWNWARD (negative thermal buoyancy!)
            //    + sparkling anisotropic diamond-dust ice crystals catching live sunlight / moonlight!
            const float sinkU = saturate((-q.z + volR * 0.22f) / (volR * 1.95f));
            const float swirlA = tClock * 2.2f - sinkU * 3.8f;
            const float ca = cos(swirlA), sa = sin(swirlA);
            const float2 qSinkXY = q.xy - windXY * (sinkU * 0.45f);
            const float3 qSwirl = float3(qSinkXY.x * ca - qSinkXY.y * sa, qSinkXY.x * sa + qSinkXY.y * ca, q.z);
            // Positive +tClock*sinkSpeed on Z advects the 3D cryo-mist DOWNWARD along -Z (heavy cold vapor!):
            const float3 mistCoord = float3(wpQ.xy * 0.068f, wpQ.z * 0.065f + tClock * 2.35f);
            const float4 gM1 = rawNoise3Grad(mistCoord);
            const float3 curlM = float3(gM1.z - gM1.w, gM1.w - gM1.y, gM1.y - gM1.z);
            const float4 gM2 = rawNoise3Grad(mistCoord * 2.15f + curlM * 0.38f + float3(9.1f, -4.7f, tClock * 1.4f));
            const float mistTurb = saturate(gM1.x * 0.62f + gM2.x * 0.38f);

            const float rSink = length(qSinkXY + curlM.xy * (volR * 0.22f)) / max(volR * lerp(0.65f, 1.45f, sinkU), 1.0f);
            const float sinkEnv = saturate(1.0f - rSink * rSink) * smoothstep(-0.04f, 0.14f, sinkU) * smoothstep(1.04f, 0.55f, sinkU);
            const float cryoMist = smoothstep(0.28f, 0.86f, sinkEnv + (mistTurb - 0.42f) * 0.78f);

            // Sparkling anisotropic diamond-dust hexagonal ice crystals catching sun/moon & view glint:
            const float3 dGrid = (camPosW.xyz + pCore + qSwirl + float3(0.0f, 0.0f, tClock * 18.0f)) * 0.24f;
            const float3 dCell = floor(dGrid);
            const float3 dFrac = frac(dGrid) - 0.5f;
            const float3 dRnd  = rawHash33(dCell);
            float diamondGlint = 0.0f;
            if (dRnd.x > 0.76f) {
                const float3 crystalN = normalize(rawHash33(dCell + float3(7.3f, 13.1f, 3.7f)) - 0.5f + 1.0e-4f);
                const float along = abs(dot(dFrac, crystalN));
                const float across = length(dFrac - crystalN * dot(dFrac, crystalN));
                const float plateD = max(along * 2.6f, across * 1.35f); // flat hexagonal ice-crystal plate!
                const float3 halfVec = normalize(lightDirS - w);
                const float anisoSpec = pow(saturate(abs(dot(crystalN, halfVec))), 16.0f);
                const float rimSpec   = pow(saturate(abs(dot(crystalN, -w))), 6.0f);
                const float twinkle   = 0.55f + 0.45f * sin(tClock * 9.5f + dRnd.y * 50.0f);
                diamondGlint = (1.0f - smoothstep(0.10f, 0.28f, plateD)) * smoothstep(0.76f, 0.98f, dRnd.x)
                             * (0.35f + 2.85f * anisoSpec + 0.65f * rimSpec) * twinkle * saturate(sinkEnv * 1.4f + env);
            }

            const float3 mistBase = lerp(float3(0.18f, 0.62f, 1.15f), float3(0.68f, 0.94f, 1.25f), saturate(1.0f - sinkU * 0.75f))
                                  * lerp(0.55f, 1.15f, saturate(gM1.w * 0.65f + 0.55f));
            const float3 glintCol = lerp(float3(1.55f, 2.30f, 3.10f), lightColS0 * 3.40f + float3(0.45f, 0.85f, 1.20f), 0.55f);
            const float sigM = cryoMist * 0.036f;
            const float sigD = diamondGlint * 0.11f;
            sigmaStep = sigM + sigD;
            if (sigmaStep > 1.0e-4f) {
                emitStep = (mistBase * sigM + glintCol * sigD) / sigmaStep;
            }
        } else if (bestKind == RAW_SPELL_SHOCK) {
            // 3) Shock Spells: Branching 3D multi-scale dielectric-breakdown Lichtenberg arcs
            //    (crisp white-violet ionized core + cyan ozone corona + rapid electrical micro-flicker!)
            if (rn >= 1.05f) { continue; }
            const float arcTick  = floor(tClock * 28.0f);
            const float microFlk = 0.68f + 0.32f * rawHash31(float3(arcTick, float(si) * 1.7f, 3.1f));
            const float3 arcP1 = wpQ * 0.088f + float3(arcTick * 0.73f, -arcTick * 0.41f, tClock * 1.8f);
            const float4 gA1 = rawNoise3Grad(arcP1);
            const float4 gA2 = rawNoise3Grad(arcP1 * 2.45f + gA1.yzw * 0.72f + float3(4.1f, -8.3f, arcTick * 0.29f));
            const float4 gA3 = rawNoise3Grad(arcP1 * 5.20f - gA2.yzw * 0.55f + float3(-11.2f, 6.7f, 1.9f));

            // Multi-scale intersecting zero-level curl surfaces carve razor-sharp primary & forked dendritic bolts:
            const float f1 = abs(gA1.x * 2.0f - 1.0f);
            const float f2 = abs(gA2.x * 2.0f - 1.0f);
            const float f3 = abs(gA3.x * 2.0f - 1.0f);
            const float leaderBolt = exp(-f1 * 22.0f) * exp(-f2 * 16.0f) * env;
            const float forkBranch = exp(-f2 * 18.0f) * exp(-f3 * 14.0f) * env * smoothstep(0.10f, 0.85f, rn);
            const float ozoneHalo  = exp(-f1 * 5.5f) * env * env * (0.35f + 0.65f * gA1.x);

            const float3 ionizedCore = float3(3.40f, 3.15f, 4.85f) * (leaderBolt * 3.60f + forkBranch * 1.65f);
            const float3 violetArc   = float3(1.15f, 0.36f, 2.95f) * (leaderBolt * 1.15f + forkBranch * 1.35f);
            const float3 cyanOzone   = float3(0.16f, 0.82f, 1.85f) * (ozoneHalo * 0.58f);
            emitStep = (ionizedCore + violetArc + cyanOzone) * microFlk;
            sigmaStep = (leaderBolt * 0.14f + forkBranch * 0.065f + ozoneHalo * 0.024f);
        } else {
            // 4) Arcane / Ward / Restoration Spells: Iridescent thin-film caustic energy rings + shimmering volumetric wisps:
            if (rn >= 1.05f) { continue; }
            const float nv = abs(dot(dirQ, w));
            const float fres = pow(1.0f - nv, 2.0f);
            const float3 irid = rawSpellIridescence(nv, tClock * 0.65f + rn * 1.8f);
            const float4 gC1 = rawNoise3Grad(wpQ * 0.090f + float3(tClock * 0.85f, -tClock * 0.62f, -tClock * 1.15f));
            const float4 gC2 = rawNoise3Grad(wpQ * 0.195f + gC1.yzw * 0.40f - float3(tClock * 0.55f, tClock * 0.72f, 0.0f));
            const float caustic = pow(saturate(1.0f - abs(gC1.x * 2.0f - 1.0f) * 2.8f), 3.0f) * env;
            const float shimmerWisp = smoothstep(0.36f, 0.86f, gC1.x * 0.55f + gC2.x * 0.45f) * env;

            // Counter-rotating tilted thin-film energy rings (equatorial + 50-deg inclined):
            const float angXY = atan2(q.y, q.x);
            const float ring1 = exp(-abs(dirQ.z) * 14.0f) * exp(-pow((rn - 0.68f) / 0.09f, 2.0f))
                              * (0.45f + 0.55f * sin(angXY * 8.0f + tClock * 2.8f));
            const float tiltZ = dirQ.z * 0.64f + dirQ.x * 0.77f;
            const float ring2 = exp(-abs(tiltZ) * 14.0f) * exp(-pow((rn - 0.54f) / 0.08f, 2.0f))
                              * (0.45f + 0.55f * cos(angXY * 6.0f - tClock * 3.4f));
            const float ringEnergy = ring1 + ring2;

            const float3 baseCol = lerp(bestTint, bestTint * irid, 0.55f);
            const float3 ringCol = lerp(bestTint * 1.55f + 0.35f.xxx, irid * 2.15f, 0.52f);
            emitStep = baseCol * (caustic * 0.95f + shimmerWisp * (0.28f + 0.65f * fres))
                     + ringCol * (ringEnergy * 1.25f);
            sigmaStep = (caustic * 0.052f + shimmerWisp * 0.024f + ringEnergy * 0.068f);
        }

        if (sigmaStep > 1.0e-4f) {
            const float stepT = exp(-sigmaStep * ds);
            acc += T * emitStep * (1.0f - stepT);
            T *= stepT;
            if (T < 0.08f) { break; }
        }
    }

    float3 spellOut = acc * (distFade * 0.88f);
    const float finalT = lerp(1.0f, T, distFade * ((bestKind == RAW_SPELL_FIRE || bestKind == RAW_SPELL_FROST) ? 1.0f : 0.25f));
    // Smooth energy-conserving HDR shoulder so active spells stay crisp & luminous without nuclear whiteout:
    const float sPk = max(spellOut.r, max(spellOut.g, spellOut.b));
    if (sPk > 2.40f) {
        spellOut *= (2.40f + (sPk - 2.40f) / (1.0f + (sPk - 2.40f) * 0.48f)) / sPk;
    }
    return float4(spellOut, finalT);
}

#endif
