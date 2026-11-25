// RawSpells3D.hlsli: True 3D Depth-Occluded Volumetric Ray-Marched Spell Engine for RAW (SM 6.6 - SM 6.10).
// Wired into LightFog.hlsl for ALL Skyrim magic archetypes detected live from dynamic emitter spectral signatures & radii:
//   1) Fire / Flame Spells   : 3D swirling blackbody plasma vortex (1400K..3400K) + shockwave heat-lensing corona
//   2) Frost / Ice Spells    : 3D faceted crystalline ice-shard storm + swirling freezing cryogenic mist + cyan-indigo refraction
//   3) Shock / Lightning     : 3D branching Lichtenberg electric plasma arcs + ionized violet-white corona + volumetric ozone glow
//   4) Arcane / Ward / Heal  : 3D caustic refractive energy shells + counter-rotating spiral runic rings + Airy chromatic lensing
// 100% original MIT clean-room code; strictly depth-occluded against scene geometry (tExit = min(dist, ...)).
#ifndef RAW_SPELLS_3D_HLSLI
#define RAW_SPELLS_3D_HLSLI

static const uint RAW_SPELL_NONE   = 0u;
static const uint RAW_SPELL_FIRE   = 1u;
static const uint RAW_SPELL_FROST  = 2u;
static const uint RAW_SPELL_SHOCK  = 3u;
static const uint RAW_SPELL_ARCANE = 4u;

// Smooth Planck blackbody ramp for volumetric spell plasma (1350K deep crimson -> 2300K molten gold -> 3600K white-hot core).
float3 rawSpellBlackbody(float t) {
    const float u = saturate(t);
    const float3 c0 = float3(1.45f, 0.16f, 0.02f);
    const float3 c1 = float3(2.90f, 0.96f, 0.12f);
    const float3 c2 = float3(4.10f, 3.05f, 1.55f);
    return (u < 0.52f) ? lerp(c0, c1, u / 0.52f) : lerp(c1, c2, saturate((u - 0.52f) / 0.48f));
}

// Thin-film Airy interference rainbow tint for arcane/ward/conjuration shells (615/540/465 nm).
float3 rawSpellIridescence(float cosTheta, float phase) {
    const float opd = 420.0f + 260.0f * (1.0f - saturate(cosTheta)) + phase * 180.0f;
    return 0.56f + 0.44f * cos(6.2831853f * opd / float3(615.0f, 540.0f, 465.0f));
}

// Classify active dynamic Skyrim magic emitter strictly from spectral ratios & radius
// (avoids touching dim neutral ambient fill lights or calm steady world candles/hearths).
uint rawClassifySpellLight(float3 lc, float rL, out float3 outTint) {
    const float cMax = max(lc.r, max(lc.g, lc.b));
    const float cMin = min(lc.r, min(lc.g, lc.b));
    const float sat  = (cMax - cMin) / max(cMax, 1.0e-4f);
    outTint = lc / max(cMax, 1.0e-4f);
    if (cMax < 0.16f || sat < 0.22f || rL < 38.0f || rL > 920.0f) { return RAW_SPELL_NONE; }

    // 1) Shock / Lightning: electric violet-indigo-blue (high B, elevated R vs G, crisp ionized signature).
    if (lc.b > lc.g * 1.12f && lc.r > lc.g * 0.76f && lc.b > 0.20f && rL <= 860.0f) {
        outTint = lerp(float3(0.58f, 0.44f, 1.35f), outTint, 0.45f);
        return RAW_SPELL_SHOCK;
    }
    // 2) Frost / Ice: cold glacial cyan-blue (B dominant over R, G elevated over R, cold cryogenic signature).
    if (lc.b > lc.r * 1.32f && lc.g > lc.r * 1.06f && lc.b > 0.18f && rL <= 780.0f) {
        outTint = lerp(float3(0.24f, 0.78f, 1.28f), outTint, 0.45f);
        return RAW_SPELL_FROST;
    }
    // 3) Arcane / Healing / Ward / Conjuration:
    //    - Emerald/jade Alteration/Poison/Stamina (G dominant)
    //    - Magenta/violet Conjuration/Soul-Trap (R and B both >> G)
    //    - Golden-white Restoration healing (high R & G, low B, compact spell radius, distinct from orange torches)
    const bool isEmeraldArcane = (lc.g > lc.r * 1.16f && lc.g > lc.b * 1.12f && lc.g > 0.16f);
    const bool isVioletConjure = (lc.r > lc.g * 1.28f && lc.b > lc.g * 1.24f && cMax > 0.18f);
    const bool isGoldenHeal    = (lc.r > 0.40f && lc.g > lc.r * 0.78f && lc.b < lc.g * 0.58f && rL <= 520.0f);
    if (isEmeraldArcane || isVioletConjure || isGoldenHeal) {
        return RAW_SPELL_ARCANE;
    }
    // 4) Combat Fire / Flame Spells: intense high-chroma fireball/flame-stream emitters (distinct from calm torches/candles).
    if (lc.r > lc.b * 2.15f && lc.r > 0.74f && rL >= 75.0f && rL <= 560.0f) {
        return RAW_SPELL_FIRE;
    }
    return RAW_SPELL_NONE;
}

// Fast 3D volumetric ray-marcher for a single classified magic emitter, depth-occluded at `dist`.
// Outputs additive HDR volumetric radiance and accumulates subtle screen-space refractive heat/arcane lensing offset.
float3 evalSingleSpell3D(uint kind, float3 lPos, float3 lc, float3 tint, float rL,
                         float3 eye, float3 w, float dist, float jit, float tClock, float seed,
                         inout float2 refractPx) {
    // Physical spell volume radius derived from emitter reach:
    const float size01 = saturate((rL - 60.0f) / 440.0f);
    const float volR = (kind == RAW_SPELL_SHOCK)  ? lerp(24.0f, 68.0f, size01)
                     : (kind == RAW_SPELL_ARCANE) ? lerp(26.0f, 62.0f, size01)
                     : (kind == RAW_SPELL_FROST)  ? lerp(22.0f, 58.0f, size01)
                                                  : lerp(18.0f, 46.0f, size01);

    // Exact analytic ray-sphere entry/exit clipped by scene depth `dist` (100% occluded by walls, shields, NPCs!):
    const float3 oc = eye - lPos;
    const float b = dot(oc, w);
    const float c = dot(oc, oc) - volR * volR;
    const float disc = b * b - c;
    if (disc <= 0.0f) { return 0.0f.xxx; }
    const float sDisc = sqrt(disc);
    const float tEnter = max(10.0f, -b - sDisc);
    const float tExit  = min(dist, -b + sDisc);
    if (tExit <= tEnter + 0.4f || tEnter > 2600.0f) { return 0.0f.xxx; }

    const float distFade = 1.0f - smoothstep(1600.0f, 2600.0f, tEnter);
    const float invR = 1.0f / max(volR, 1.0f);
    const int kSteps = 12;
    const float ds = (tExit - tEnter) * (1.0f / float(kSteps));

    // Closest-approach ring lensing (fire shockwave heat shimmer / arcane refractive bubble / frost mirage):
    const float tMid = clamp(-b, tEnter, tExit);
    const float3 qMid = (eye + w * tMid) - lPos;
    const float rMid = length(qMid) * invR;
    if (rMid < 0.96f && tMid < 1200.0f) {
        const float shellRing = exp(-pow((rMid - 0.72f) / 0.20f, 2.0f));
        const float lensAmp = (kind == RAW_SPELL_FIRE)   ? (shellRing * 2.2f)
                            : (kind == RAW_SPELL_ARCANE) ? (shellRing * 1.8f)
                            : (kind == RAW_SPELL_FROST)  ? (shellRing * 1.1f)
                                                         : (shellRing * 0.9f);
        const float3 wpMid = camPosW.xyz + eye + w * tMid;
        const float4 gLens = rawNoise3Grad(wpMid * 0.09f + float3(0.0f, 0.0f, -tClock * 3.8f));
        refractPx += gLens.yz * (lensAmp * distFade * saturate(1.0f - tMid / 1200.0f));
    }

    float3 acc = 0.0f.xxx;
    float T = 1.0f;

    [unroll] for (int si = 0; si < kSteps; ++si) {
        const float t = tEnter + (float(si) + jit) * ds;
        const float3 pRel = eye + w * t;
        const float3 q = pRel - lPos;
        const float r = length(q);
        const float rn = r * invR;
        if (rn >= 1.0f) { continue; }
        const float3 dirQ = q / max(r, 1.0e-3f);
        const float3 wpQ = camPosW.xyz + pRel;
        const float envSphere = saturate(1.0f - rn * rn);

        float3 emitStep = 0.0f.xxx;
        float sigmaStep = 0.0f;

        if (kind == RAW_SPELL_FIRE) {
            // 1) Fire / Flame Spell: 3D helical swirling blackbody plasma vortex + shockwave heat ring:
            const float spin = tClock * 4.2f + rn * 5.2f + seed;
            const float cs = cos(spin), sn = sin(spin);
            const float3 qRot = float3(q.x * cs - q.y * sn, q.x * sn + q.y * cs, q.z - tClock * 26.0f);
            const float4 g1 = rawNoise3Grad(qRot * (invR * 2.4f) + seed);
            const float4 g2 = rawNoise3Grad(qRot * (invR * 5.2f) + float3(7.1f, -4.3f, -tClock * 2.4f));
            const float swirlDens = smoothstep(0.26f, 0.86f, envSphere * 1.18f + (g1.x * 0.62f + g2.x * 0.38f - 0.42f) * 0.95f);
            const float shockShell = exp(-pow((rn - 0.82f) / 0.11f, 2.0f)) * smoothstep(0.35f, 0.85f, g2.x);
            const float temp = saturate(swirlDens * (1.20f - rn * 0.68f) + (1.0f - rn) * 0.35f);
            emitStep = rawSpellBlackbody(temp) * (swirlDens * 0.85f) + float3(2.40f, 0.78f, 0.18f) * (shockShell * 0.42f);
            sigmaStep = (swirlDens * 0.085f + shockShell * 0.025f) * invR * 28.0f;
        } else if (kind == RAW_SPELL_FROST) {
            // 2) Frost / Ice Spell: 3D faceted crystalline ice-shard storm + swirling cryogenic mist + cyan-indigo refraction:
            const float swirlA = tClock * 2.6f - rn * 4.4f + seed;
            const float ca = cos(swirlA), sa = sin(swirlA);
            const float3 qSwirl = float3(q.x * ca - q.y * sa, q.x * sa + q.y * ca, q.z + sin(tClock * 1.8f + rn * 4.0f) * 3.5f);
            const float4 gMist = rawNoise3Grad(qSwirl * (invR * 2.6f) + float3(0.0f, 0.0f, -tClock * 1.1f));
            const float cryoMist = smoothstep(0.30f, 0.88f, envSphere * 1.05f + (gMist.x - 0.45f) * 0.82f);

            // Orbiting 3D crystalline ice shards (faceted diamond lattice with crisp specular flashes):
            const float3 shardGrid = qSwirl * (invR * 5.8f);
            const float3 sCell = floor(shardGrid);
            const float3 sFrac = frac(shardGrid) - 0.5f;
            const float sHash = rawHash31(sCell + seed);
            float shardFlash = 0.0f;
            if (sHash > 0.72f) {
                const float3 shardAxis = normalize(float3(rawHash31(sCell + 3.1f) - 0.5f,
                                                          rawHash31(sCell + 7.7f) - 0.5f,
                                                          rawHash31(sCell + 13.3f) - 0.5f) + 1.0e-4f);
                // Elongated hexagonal prism / icicle diamond SDF approximation:
                const float along = abs(dot(sFrac, shardAxis));
                const float across = length(sFrac - shardAxis * dot(sFrac, shardAxis));
                const float crystalD = max(along * 1.15f, across * 2.85f);
                const float facetSpec = pow(saturate(abs(dot(normalize(sFrac + 1.0e-4f), -w))), 6.0f);
                shardFlash = (1.0f - smoothstep(0.14f, 0.34f, crystalD)) * smoothstep(0.72f, 0.96f, sHash)
                           * (0.55f + 1.65f * facetSpec) * envSphere;
            }
            const float3 cryoDeep = float3(0.08f, 0.32f, 0.92f);
            const float3 cryoMid  = float3(0.28f, 0.88f, 1.45f);
            const float3 cryoCore = float3(1.55f, 2.15f, 2.75f);
            const float3 mistCol  = lerp(cryoDeep, cryoMid, saturate(1.0f - rn * 0.85f));
            emitStep = mistCol * (cryoMist * 0.58f) + cryoCore * (shardFlash * 1.45f + exp(-rn * rn * 7.5f) * 0.45f);
            sigmaStep = (cryoMist * 0.055f + shardFlash * 0.14f) * invR * 28.0f;
        } else if (kind == RAW_SPELL_SHOCK) {
            // 3) Shock / Lightning Spell: 3D branching Lichtenberg electric plasma arcs + ionized violet-white corona + ozone glow:
            const float arcTick = floor(tClock * 22.0f + seed * 3.7f);
            const float3 arcCoord = dirQ * 3.4f + float3(rn * 2.8f, arcTick * 0.73f, -arcTick * 0.41f);
            const float4 gA1 = rawNoise3Grad(arcCoord);
            const float4 gA2 = rawNoise3Grad(arcCoord * 2.35f + gA1.yzw * 0.65f);
            // Intersect two zero-level-set curl surfaces to carve 1D branching 3D Lichtenberg plasma filaments!
            const float f1 = abs(gA1.x * 2.0f - 1.0f);
            const float f2 = abs(gA2.x * 2.0f - 1.0f);
            const float filament = exp(-f1 * 18.0f) * exp(-f2 * 14.0f) * smoothstep(0.06f, 0.25f, rn) * (1.0f - smoothstep(0.75f, 0.99f, rn));
            const float subBranch = exp(-abs((gA1.x + gA2.x) - 1.0f) * 24.0f) * envSphere * 0.45f;
            const float coronaCore = exp(-rn * rn * 9.5f);
            const float ozoneHalo = envSphere * envSphere * (0.28f + 0.42f * gA1.x);
            const float3 arcWhite  = float3(2.85f, 3.15f, 4.40f);
            const float3 ionViolet = float3(0.88f, 0.34f, 2.65f);
            const float3 ozoneBlue = float3(0.18f, 0.32f, 1.15f);
            emitStep = arcWhite * (filament * 3.60f + coronaCore * 1.15f)
                     + ionViolet * (subBranch * 1.40f + filament * 1.25f)
                     + ozoneBlue * (ozoneHalo * 0.42f);
            sigmaStep = (filament * 0.16f + coronaCore * 0.08f + ozoneHalo * 0.025f) * invR * 28.0f;
        } else {
            // 4) Arcane / Healing / Ward / Conjuration: 3D caustic refractive shell + spiral runic rings + chromatic lensing:
            const float nvShell = abs(dot(dirQ, w));
            const float fresShell = pow(1.0f - nvShell, 2.2f);
            const float shellBand = exp(-pow((rn - 0.78f) / 0.10f, 2.0f));
            const float3 irid = rawSpellIridescence(nvShell, tClock * 0.45f + rn * 1.5f);

            // Equatorial & tilted counter-rotating 3D spiral runic rings:
            const float angXY = atan2(q.y, q.x);
            const float ringPlane1 = exp(-abs(dirQ.z) * 14.0f) * exp(-pow((rn - 0.68f) / 0.075f, 2.0f));
            const float tiltedZ = dirQ.z * 0.78f + dirQ.x * 0.62f;
            const float ringPlane2 = exp(-abs(tiltedZ) * 14.0f) * exp(-pow((rn - 0.52f) / 0.070f, 2.0f));
            const float runeTicks1 = smoothstep(0.25f, 0.78f, sin(angXY * 12.0f + tClock * 2.2f + seed) * 0.5f + 0.5f)
                                   * smoothstep(0.18f, 0.72f, cos(angXY * 36.0f - tClock * 4.4f) * 0.5f + 0.5f);
            const float runeTicks2 = smoothstep(0.22f, 0.76f, sin(angXY * 8.0f - tClock * 3.1f + seed * 1.7f) * 0.5f + 0.5f);
            const float spiralRibbon = exp(-abs(sin(angXY * 3.0f - rn * 10.5f + tClock * 3.4f)) * 9.0f) * envSphere;
            const float causticN = rawNoise3Grad(dirQ * 4.2f + float3(tClock * 0.65f, -tClock * 0.48f, seed)).x;
            const float causticWeb = pow(saturate(1.0f - abs(causticN * 2.0f - 1.0f) * 2.6f), 3.0f) * shellBand;

            const float corePulse = exp(-rn * rn * 6.5f) * (0.75f + 0.25f * sin(tClock * 3.2f + seed));
            const float3 baseCol = lerp(tint, tint * irid, 0.55f);
            const float3 runeCol = lerp(tint * 1.65f + 0.35f.xxx, irid * 2.10f, 0.35f);
            emitStep = baseCol * (shellBand * (0.28f + 0.95f * fresShell) + causticWeb * 0.85f + corePulse * 0.55f)
                     + runeCol * (ringPlane1 * (0.35f + 1.25f * runeTicks1) + ringPlane2 * (0.30f + 1.05f * runeTicks2) + spiralRibbon * 0.42f);
            sigmaStep = (shellBand * 0.055f + (ringPlane1 + ringPlane2) * 0.085f + corePulse * 0.045f) * invR * 28.0f;
        }

        if (sigmaStep > 1.0e-4f) {
            const float stepT = exp(-sigmaStep * ds);
            acc += T * emitStep * (1.0f - stepT) * 1.15f;
            T *= stepT;
            if (T < 0.04f) { break; }
        }
    }

    const float lumGain = clamp(dot(lc, float3(0.25f, 0.45f, 0.30f)) * 1.35f, 0.45f, 1.35f);
    return acc * (distFade * lumGain);
}

// Entry point called from LightFog.hlsl (exteriors & interiors).
// Scans active dynamic point lights, ray-marches all detected magic archetypes in 3D, and outputs smooth HDR radiance
// plus subtle refractive screen-space heat/arcane lensing offset.
float3 evalVolumetricSpells3D(float3 eye, float3 w, float dist, float2 px, out float2 outRefractPx) {
    outRefractPx = 0.0f.xx;
    if (flags.y == 0u) { return 0.0f.xxx; }
    const float jit = ign(px + float2(23.0f, 41.0f));
    const float tClock = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;
    const uint nLights = min(flags.y, 20u);

    float3 spellTotal = 0.0f.xxx;
    uint spellsMarched = 0u;
    [loop] for (uint li = 0u; li < nLights; ++li) {
        const float3 lc = lightColS[li].rgb;
        const float rL  = lightPosR[li].w;
        float3 tint = 1.0f.xxx;
        const uint kind = rawClassifySpellLight(lc, rL, tint);
        if (kind == RAW_SPELL_NONE) { continue; }
        spellTotal += evalSingleSpell3D(kind, lightPosR[li].xyz, lc, tint, rL,
                                        eye, w, dist, jit, tClock, float(li) * 7.13f, outRefractPx);
        if (++spellsMarched >= 6u) { break; }
    }
    // Smooth energy-conserving HDR shoulder so stacked combat spells stay vivid & luminous without nuclear whiteout:
    const float sPk = max(spellTotal.r, max(spellTotal.g, spellTotal.b));
    if (sPk > 2.40f) {
        spellTotal *= (2.40f + (sPk - 2.40f) / (1.0f + (sPk - 2.40f) * 0.48f)) / sPk;
    }
    outRefractPx = clamp(outRefractPx, -4.5f.xx, 4.5f.xx);
    return spellTotal;
}

#endif
