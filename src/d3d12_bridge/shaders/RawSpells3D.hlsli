// RawSpells3D.hlsli: Scene-Anchored 3D Volumetric Spell Engine for RAW (SM 6.6 - SM 6.10).
// Strictly anchored to verified real HDR magic/spell cores rendered in the scene color buffer (`Lit` + `Depth`),
// NEVER spawning opaque floating spheres around invisible Creation Engine point lights in empty air!
// Supports live detected Skyrim magic archetypes:
//   1) Frost / Ice Spells    : 3D faceted crystalline ice-shard storm + swirling cryogenic mist
//   2) Shock / Lightning     : 3D branching Lichtenberg electric plasma arcs + ionized violet-white corona
//   3) Arcane / Ward / Heal  : 3D caustic energy wisps + Airy chromatic interference lensing
// 100% original MIT clean-room code; strictly depth-occluded against scene geometry.
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

// Thin-film Airy interference rainbow tint for arcane/ward/conjuration wisps (615/540/465 nm).
float3 rawSpellIridescence(float cosTheta, float phase) {
    const float opd = 420.0f + 260.0f * (1.0f - saturate(cosTheta)) + phase * 180.0f;
    return 0.56f + 0.44f * cos(6.2831853f * opd / float3(615.0f, 540.0f, 465.0f));
}

// Strict HDR scene-rendered magic emissive core detector:
// Requires high HDR luminance (luma > 1.60) AND strong chromatic saturation (sat > 0.42),
// completely rejecting lit ice walls, blue cave bounce light, snow, water speculars, and warm fire.
float rawDetectSceneSpellCore(float3 rgb, out uint outKind, out float3 outTint) {
    outKind = RAW_SPELL_NONE;
    outTint = 1.0f.xxx;
    const float luma = dot(rgb, float3(0.2126f, 0.7152f, 0.0722f));
    if (luma <= 1.60f) { return 0.0f; }
    const float cMax = max(rgb.r, max(rgb.g, rgb.b));
    const float cMin = min(rgb.r, min(rgb.g, rgb.b));
    const float sat  = (cMax - cMin) / max(cMax, 1.0e-4f);
    if (sat <= 0.42f || cMax <= 1.75f) { return 0.0f; }

    // Exclude warm orange-red combustion (handled by the dedicated fire/smoke/ember plume engine):
    if (rgb.r > rgb.g * 1.15f && rgb.r > rgb.b * 2.0f) { return 0.0f; }

    outTint = rgb / max(cMax, 1.0e-4f);
    const float lumW = smoothstep(1.60f, 3.60f, luma);
    const float satW = smoothstep(0.42f, 0.74f, sat);

    // 1) Shock / Lightning: intense ionized violet-blue core
    if (rgb.b > rgb.g * 1.16f && rgb.r > rgb.g * 0.78f && rgb.b > 1.85f) {
        outKind = RAW_SPELL_SHOCK;
        outTint = lerp(float3(0.62f, 0.46f, 1.38f), outTint, 0.45f);
        return lumW * satW;
    }
    // 2) Frost / Ice Magic: bright self-emissive cyan-glacial spell core (never lit cave ice walls!)
    if (rgb.b > rgb.r * 1.45f && rgb.g > rgb.r * 1.12f && rgb.b > 1.80f) {
        outKind = RAW_SPELL_FROST;
        outTint = lerp(float3(0.24f, 0.84f, 1.32f), outTint, 0.45f);
        return lumW * satW;
    }
    // 3) Arcane / Alteration / Conjuration / Ward: vivid emerald or magenta-violet HDR magic core
    const bool isEmerald = (rgb.g > rgb.r * 1.28f && rgb.g > rgb.b * 1.22f && rgb.g > 1.75f);
    const bool isViolet  = (rgb.r > rgb.g * 1.35f && rgb.b > rgb.g * 1.30f && cMax > 1.85f);
    if (isEmerald || isViolet) {
        outKind = RAW_SPELL_ARCANE;
        return lumW * satW;
    }
    return 0.0f;
}

// Scene-Anchored 3D Volumetric Spell Engine called from LightFog.hlsl (exteriors & interiors).
// NEVER loops blindly over point lights! Anchors 3D crystalline frost shards, branching Lichtenberg shock arcs,
// and arcane caustic energy strictly to verified real HDR magic spell pixels present in `litTex` + `depthTex`.
float3 evalVolumetricSpells3D(Texture2D<float4> litTex, Texture2D<float> depthTex, SamplerState linSamp,
                              float3 eye, float3 w, float dist, float curZ, float2 px, out float2 outRefractPx) {
    outRefractPx = 0.0f.xx;
    const float2 uv = (px + 0.5f) * dims.zw;
    const float jit = ign(px + float2(23.0f, 41.0f));
    const float tClock = physWorld[0].x > 0.001f ? physWorld[0].x : camPosW.w;

    // 9-tap local screen-space anchor search around current pixel (1 center + 8 dual-ring probes)
    // to find any verified real HDR magic spell core that is NOT occluded by foreground geometry.
    static const float2 kSpellProbe[9] = {
        float2( 0.0f,  0.0f),
        float2( 1.0f,  0.0f), float2(-1.0f,  0.0f),
        float2( 0.0f,  1.0f), float2( 0.0f, -1.0f),
        float2( 1.75f,  1.75f), float2(-1.75f,  1.75f),
        float2( 1.75f, -1.75f), float2(-1.75f, -1.75f)
    };
    const float ringPx = clamp(dims.y * 0.018f, 10.0f, 28.0f);

    float bestEnergy = 0.0f;
    uint  bestKind   = RAW_SPELL_NONE;
    float3 bestTint  = 1.0f.xxx;
    float2 bestPx    = px;
    float  bestD     = 1.0f;
    float  bestZ     = 1.0e6f;

    [unroll] for (int pi = 0; pi < 9; ++pi) {
        const float2 qPx = clamp(px + kSpellProbe[pi] * ringPx, float2(1.0f, 1.0f), dims.xy - 2.0f);
        const float2 qUv = (qPx + 0.5f) * dims.zw;
        const float dTap = depthTex.SampleLevel(linSamp, qUv, 0.0f);
        // Reject sky, far horizon, and near-clip planes:
        if (dTap >= 0.9990f) { continue; }
        const float zTap = viewZOf(dTap);
        if (zTap < 14.0f || zTap > 2000.0f) { continue; }
        // Strict depth occlusion: never project spell effects OVER foreground walls/NPCs/shields!
        if (zTap > curZ * 1.04f + 10.0f) { continue; }

        const float3 cTap = litTex.SampleLevel(linSamp, qUv, 0.0f).rgb;
        uint kTap = RAW_SPELL_NONE;
        float3 tintTap = 1.0f.xxx;
        const float eTap = rawDetectSceneSpellCore(cTap, kTap, tintTap);
        const float wTap = eTap * (pi == 0 ? 1.15f : (pi <= 4 ? 0.92f : 0.72f));
        if (wTap > bestEnergy) {
            bestEnergy = wTap;
            bestKind   = kTap;
            bestTint   = tintTap;
            bestPx     = qPx;
            bestD      = dTap;
            bestZ      = zTap;
        }
    }

    // Early out on 99% of pixels where no genuine HDR magic spell core exists:
    if (bestEnergy <= 0.02f || bestKind == RAW_SPELL_NONE) { return 0.0f.xxx; }

    // Reconstruct exact 3D world-space anchor of the verified spell core and intersect our view ray:
    const float3 pCore = worldAt(bestPx, bestD);
    const float tCenter = dot(pCore - eye, w);
    if (tCenter < 12.0f || tCenter > 2000.0f) { return 0.0f.xxx; }

    const float volR = (bestKind == RAW_SPELL_SHOCK) ? 38.0f : ((bestKind == RAW_SPELL_FROST) ? 34.0f : 32.0f);
    const float3 pClosest = eye + w * tCenter;
    const float rClosest = length(pClosest - pCore);
    if (rClosest >= volR * 1.05f) { return 0.0f.xxx; }

    const float tEnter = max(12.0f, tCenter - volR * 0.85f);
    const float tExit  = min(dist + 4.0f, tCenter + volR * 0.85f);
    if (tExit <= tEnter + 0.5f) { return 0.0f.xxx; }

    const float invR = 1.0f / volR;
    const float distFade = (1.0f - smoothstep(1100.0f, 2000.0f, tCenter)) * bestEnergy;

    // Subtle localized refractive distortion strictly around verified active spell core:
    const float rMid = saturate(rClosest * invR);
    if (rMid < 0.95f && tCenter < 950.0f) {
        const float ringW = exp(-pow((rMid - 0.55f) / 0.28f, 2.0f));
        const float3 wpM = camPosW.xyz + pClosest;
        const float4 gLens = rawNoise3Grad(wpM * 0.11f + float3(0.0f, 0.0f, -tClock * 4.2f));
        outRefractPx = clamp(gLens.yz * (ringW * distFade * 1.45f), -2.5f.xx, 2.5f.xx);
    }

    const int kSteps = 8;
    const float ds = (tExit - tEnter) * (1.0f / float(kSteps));
    float3 acc = 0.0f.xxx;
    float T = 1.0f;

    [unroll] for (int si = 0; si < kSteps; ++si) {
        const float t = tEnter + (float(si) + jit) * ds;
        const float3 pRel = eye + w * t;
        const float3 q = pRel - pCore;
        const float r = length(q);
        const float rn = r * invR;
        if (rn >= 1.0f) { continue; }
        const float3 dirQ = q / max(r, 1.0e-3f);
        const float3 wpQ = camPosW.xyz + pRel;
        const float env = saturate(1.0f - rn * rn);

        float3 emitStep = 0.0f.xxx;
        float sigmaStep = 0.0f;

        if (bestKind == RAW_SPELL_SHOCK) {
            // Branching 3D Lichtenberg electric plasma filaments & ionized violet-white arcs:
            const float arcTick = floor(tClock * 24.0f);
            const float3 arcCoord = wpQ * 0.095f + float3(arcTick * 0.73f, -arcTick * 0.41f, tClock * 1.2f);
            const float4 gA1 = rawNoise3Grad(arcCoord);
            const float4 gA2 = rawNoise3Grad(arcCoord * 2.4f + gA1.yzw * 0.65f);
            const float f1 = abs(gA1.x * 2.0f - 1.0f);
            const float f2 = abs(gA2.x * 2.0f - 1.0f);
            const float filament = exp(-f1 * 18.0f) * exp(-f2 * 14.0f) * env;
            const float subBranch = exp(-abs((gA1.x + gA2.x) - 1.0f) * 22.0f) * env * 0.45f;
            emitStep = float3(2.85f, 3.10f, 4.40f) * (filament * 3.10f)
                     + float3(0.85f, 0.34f, 2.55f) * (subBranch * 1.25f + filament * 1.05f);
            sigmaStep = (filament * 0.12f + subBranch * 0.04f);
        } else if (bestKind == RAW_SPELL_FROST) {
            // Orbiting 3D faceted crystalline ice shards + delicate swirling freezing vapor:
            const float swirlA = tClock * 2.8f - rn * 4.2f;
            const float ca = cos(swirlA), sa = sin(swirlA);
            const float3 qSwirl = float3(q.x * ca - q.y * sa, q.x * sa + q.y * ca, q.z);
            const float4 gMist = rawNoise3Grad(wpQ * 0.085f + float3(0.0f, 0.0f, -tClock * 1.4f));
            const float cryoMist = smoothstep(0.38f, 0.88f, env * 0.95f + (gMist.x - 0.48f) * 0.75f);

            const float3 shardGrid = (camPosW.xyz + pCore + qSwirl) * 0.18f;
            const float3 sCell = floor(shardGrid);
            const float3 sFrac = frac(shardGrid) - 0.5f;
            const float sHash = rawHash31(sCell);
            float shardFlash = 0.0f;
            if (sHash > 0.78f) {
                const float3 shardAxis = normalize(rawHash33(sCell) - 0.5f + 1.0e-4f);
                const float along = abs(dot(sFrac, shardAxis));
                const float across = length(sFrac - shardAxis * dot(sFrac, shardAxis));
                const float crystalD = max(along * 1.2f, across * 2.9f);
                const float facetSpec = pow(saturate(abs(dot(normalize(sFrac + 1.0e-4f), -w))), 5.0f);
                shardFlash = (1.0f - smoothstep(0.12f, 0.30f, crystalD)) * smoothstep(0.78f, 0.98f, sHash)
                           * (0.60f + 1.55f * facetSpec) * env;
            }
            emitStep = lerp(float3(0.14f, 0.48f, 1.08f), float3(0.32f, 0.92f, 1.45f), env) * (cryoMist * 0.34f)
                     + float3(1.65f, 2.25f, 2.85f) * (shardFlash * 1.35f);
            sigmaStep = (cryoMist * 0.032f + shardFlash * 0.11f);
        } else {
            // Arcane / Ward / Conjuration: 3D curl-warped caustic energy filaments + subtle Airy iridescence:
            const float nv = abs(dot(dirQ, w));
            const float3 irid = rawSpellIridescence(nv, tClock * 0.55f + rn * 1.4f);
            const float4 gC = rawNoise3Grad(wpQ * 0.095f + float3(tClock * 0.8f, -tClock * 0.6f, -tClock * 1.2f));
            const float caustic = pow(saturate(1.0f - abs(gC.x * 2.0f - 1.0f) * 2.8f), 3.0f) * env;
            const float rimWisp = pow(1.0f - nv, 2.0f) * exp(-pow((rn - 0.68f) / 0.22f, 2.0f)) * smoothstep(0.30f, 0.82f, gC.x);
            emitStep = lerp(bestTint, bestTint * irid, 0.50f) * (caustic * 0.95f + rimWisp * 0.65f);
            sigmaStep = (caustic * 0.055f + rimWisp * 0.035f);
        }

        if (sigmaStep > 1.0e-4f) {
            const float stepT = exp(-sigmaStep * ds);
            acc += T * emitStep * (1.0f - stepT);
            T *= stepT;
            if (T < 0.08f) { break; }
        }
    }

    float3 spellOut = acc * (distFade * 0.85f);
    // Smooth energy-conserving HDR shoulder so active spells stay crisp & luminous without nuclear whiteout:
    const float sPk = max(spellOut.r, max(spellOut.g, spellOut.b));
    if (sPk > 2.20f) {
        spellOut *= (2.20f + (sPk - 2.20f) / (1.0f + (sPk - 2.20f) * 0.50f)) / sPk;
    }
    return spellOut;
}

#endif
