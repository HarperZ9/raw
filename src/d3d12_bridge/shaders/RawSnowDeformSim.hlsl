// RawSnowDeformSim.hlsl: Real-Time 512x512 GPU Compute Snow Accumulation,
// Thermal/Solar Melt, Slush-to-Black-Ice Refreeze & Mass-Conserving Footprint
// Deformation Solver (SM 6.6 / 6.10).
//
// Pass 1 (cs_snow_deform_step):
//   - World-anchored toroidal scrolling with clean border re-initialization.
//   - Up to 32 simultaneous actor/horse/cart elliptical footprint stamps with
//     analytically balanced mass-conserving raised compacted snow rim displacement.
//   - Up to 16 circular campfire/lava thermal heat sources + ambient/solar warmth
//     melting snow into wet slush, freezing slush into glossy black ice below 0 C,
//     seasonal snowfall replenishment, and compaction-aware wind-drift smoothing.
//
// Pass 2 (cs_snow_normal_height):
//   - 3x3 Sobel analytical height-gradient surface normal aligned with footprint
//     trench walls and raised rims + snow micro-crystal sparkle / black-ice glint mask.
//
// State packing (RGBA16F):
//   R = snowDepthM   [0 .. maxDepthM]
//   G = compaction   [0 .. 1]
//   B = slushWetness [0 .. 1]
//   A = blackIceMask [0 .. 1]
//
// Normal output packing (RGBA16F):
//   RGB = world-space unit normal (Y-up: (0, 1, 0) on flat snow)
//   A   = sparkleGlint [0 .. 1] (micro-crystal sparkle + glossy black-ice glint)
//
// Original clean-room implementation, MIT License.

static const uint kMaxStamps      = 32u;
static const uint kMaxHeatSources = 16u;
static const float kPi            = 3.14159265359f;
// Exact 2D polar integral ratio I_trench / I_rim:
//   I_trench = 2*pi * int_0^1 (1 - r^2)^2 r dr = 2*pi / 6
//   I_rim    = 2*pi * int_1^1.65 sin^2(pi*(r-1)/0.65) r dr = 2*pi * 0.430625
static const float kRimAmpRatio   = 0.38703435f;

cbuffer SnowDeformCB : register(b0)
{
    uint4  gGridDims;       // (width, height, stampCount, heatCount)
    int4   gToroidScroll;   // (deltaCellX, deltaCellZ, originCellX, originCellZ)
    float4 gWorldParams;    // (originWorldX, originWorldZ, cellSizeM, worldSpanM)
    float4 gClimateParams;  // (dtSec, snowfallRateMps, ambientTempC, solarWarmthC)
    float4 gSimParams;      // (baselineDepthM, maxDepthM, windSpeedMps, meltCoeff)
    float4 gWindAndFlags;   // (windDirX, windDirZ, normalStrength, flagsBitmask)
    float4 gStamps[kMaxStamps];       // (worldX, worldZ, radiusM, compressDepthM)
    float4 gStampMeta[kMaxStamps];    // (rimRatio, headingCos, headingSin, aspectRatio)
    float4 gHeatSources[kMaxHeatSources]; // (worldX, worldZ, radiusM, heatPowerC)
};

Texture2D<float4>   SnowStateIn  : register(t0);
RWTexture2D<float4> SnowStateOut : register(u0);

float Sanitize(float v, float fallback)
{
    return isfinite(v) ? v : fallback;
}

float4 SanitizeState(float4 s, float maxDepthM)
{
    return float4(
        clamp(Sanitize(s.r, 0.0f), 0.0f, maxDepthM),
        saturate(Sanitize(s.g, 0.0f)),
        saturate(Sanitize(s.b, 0.0f)),
        saturate(Sanitize(s.a, 0.0f)));
}

float Hash21(int2 p)
{
    uint ux = asuint(p.x) * 374761393u + 668265263u;
    uint uz = asuint(p.y) * 1274126177u + 2246822519u;
    uint h  = (ux ^ (uz >> 13u)) * 1103515245u;
    h ^= (h >> 16u);
    return float(h & 0x00FFFFFFu) * (1.0f / 16777216.0f);
}

// ============================================================================
// Pass 1: Toroidal Snow Accumulation, Thermal Melt/Refreeze & Footprint Stamps
// ============================================================================
[numthreads(8, 8, 1)]
void cs_snow_deform_step(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gGridDims.x || dtid.y >= gGridDims.y) return;

    const int2  dims       = int2(gGridDims.xy);
    const int2  p          = int2(dtid.xy);
    const uint  stampCount = min(gGridDims.z, kMaxStamps);
    const uint  heatCount  = min(gGridDims.w, kMaxHeatSources);
    const uint  flags      = (uint)gWindAndFlags.w;
    const bool  resetField = ((flags & 1u) != 0u);

    const float cellSizeM      = max(gWorldParams.z, 1.0e-3f);
    const float dtSec          = clamp(gClimateParams.x, 0.0f, 0.25f);
    const float snowfallMps    = max(gClimateParams.y, 0.0f);
    const float ambientTempC   = gClimateParams.z;
    const float solarWarmthC   = max(gClimateParams.w, 0.0f);
    const float baselineDepthM = max(gSimParams.x, 0.0f);
    const float maxDepthM      = max(gSimParams.y, 0.1f);
    const float windSpeedMps   = max(gSimParams.z, 0.0f);
    const float meltCoeff      = max(gSimParams.w, 0.0f);

    // World-space center of this texel (XZ horizontal plane)
    const float2 worldXZ = gWorldParams.xy + (float2(p) + 0.5f) * cellSizeM;

    // 1. Toroidal scroll sample (newly exposed border strips initialize to baseline snowpack)
    const int2 prevP = p + gToroidScroll.xy;
    const bool outOfPrev = resetField ||
                           (prevP.x < 0) || (prevP.x >= dims.x) ||
                           (prevP.y < 0) || (prevP.y >= dims.y);

    float4 state = float4(clamp(baselineDepthM, 0.0f, maxDepthM), 0.0f, 0.0f, 0.0f);
    if (!outOfPrev) {
        state = SanitizeState(SnowStateIn.Load(int3(prevP, 0)), maxDepthM);

        // Conservative 4-neighbor wind-drift smoothing (compacted tracks resist wind infill)
        if (windSpeedMps > 0.05f && dtSec > 0.0f) {
            const int2 pL = clamp(prevP + int2(-1,  0), int2(0, 0), dims - 1);
            const int2 pR = clamp(prevP + int2( 1,  0), int2(0, 0), dims - 1);
            const int2 pD = clamp(prevP + int2( 0, -1), int2(0, 0), dims - 1);
            const int2 pU = clamp(prevP + int2( 0,  1), int2(0, 0), dims - 1);

            const float hL = SanitizeState(SnowStateIn.Load(int3(pL, 0)), maxDepthM).r;
            const float hR = SanitizeState(SnowStateIn.Load(int3(pR, 0)), maxDepthM).r;
            const float hD = SanitizeState(SnowStateIn.Load(int3(pD, 0)), maxDepthM).r;
            const float hU = SanitizeState(SnowStateIn.Load(int3(pU, 0)), maxDepthM).r;

            const float avgH       = 0.25f * (hL + hR + hD + hU);
            const float driftAlpha = saturate(windSpeedMps * 0.015f * dtSec) * (1.0f - 0.80f * state.g);
            state.r = lerp(state.r, avgH, driftAlpha);
        }
    }

    // 2. Evaluate local thermal temperature from ambient air + solar insolation + up to 16 heat sources
    float localTempC = ambientTempC + solarWarmthC;
    float heatSourceBoostC = 0.0f;
    for (uint hIdx = 0u; hIdx < heatCount; ++hIdx) {
        const float4 hs = gHeatSources[hIdx];
        const float  radM = max(hs.z, 1.0e-3f);
        const float2 dv   = worldXZ - hs.xy;
        const float  d2   = dot(dv, dv);
        const float  r2   = radM * radM;
        if (d2 < r2 * 2.25f) {
            const float normD2 = d2 / r2;
            const float falloff = exp(-2.2f * normD2) * saturate(1.0f - normD2 / 2.25f);
            heatSourceBoostC += max(hs.w, 0.0f) * falloff;
        }
    }
    localTempC += heatSourceBoostC;

    // 3. Seasonal snowfall replenishment (suppressed directly over blazing heat sources)
    if (snowfallMps > 0.0f && dtSec > 0.0f) {
        const float fallMask = saturate(1.0f - max(localTempC - 1.0f, 0.0f) * 0.25f);
        const float addedSnow = snowfallMps * dtSec * fallMask;
        state.r = min(maxDepthM, state.r + addedSnow);
        // Fresh powder gradually softens compaction and covers old black ice
        const float coverRate = saturate(addedSnow * 6.0f);
        state.g = max(0.0f, state.g * (1.0f - 0.5f * coverRate));
        state.a = max(0.0f, state.a * (1.0f - coverRate));
    }

    // 4. Thermal melt -> wet slush & sub-zero slush -> glossy black-ice refreeze
    if (dtSec > 0.0f) {
        if (localTempC > 0.0f) {
            const float meltPotential = meltCoeff * localTempC * dtSec;
            const float actualMelted  = min(state.r, meltPotential);
            state.r = max(0.0f, state.r - actualMelted);

            // Melted snow produces glistening wet slush
            state.b = saturate(state.b + actualMelted * 4.5f);

            // Warmth also melts existing black ice back into wet slush
            if (localTempC > 1.5f && state.a > 0.0f) {
                const float iceMelt = min(state.a, (localTempC - 1.5f) * 0.04f * dtSec);
                state.a -= iceMelt;
                state.b  = saturate(state.b + iceMelt * 0.8f);
            }

            // Very hot inner hearth core gradually evaporates residual slush once snow is gone
            if (state.r < 0.005f && localTempC > 25.0f) {
                state.b = max(0.0f, state.b - (localTempC - 25.0f) * 0.015f * dtSec);
            }
        } else {
            // Sub-zero air freezes wet slush into smooth reflective black ice
            const float subZeroC   = -localTempC;
            const float freezeFrac = saturate(subZeroC * 0.14f * dtSec);
            const float frozenAmt  = state.b * freezeFrac;
            state.b = max(0.0f, state.b - frozenAmt);
            state.a = saturate(state.a + frozenAmt * 1.35f);
        }
    }

    // 5. Up to 32 simultaneous actor/horse/cart footprint stamps with mass-conserving rim displacement
    float totalDeltaH   = 0.0f;
    float addedCompact  = 0.0f;
    const float preStampH = state.r;

    if (preStampH > 0.002f) {
        for (uint sIdx = 0u; sIdx < stampCount; ++sIdx) {
            const float4 st   = gStamps[sIdx];
            const float4 meta = gStampMeta[sIdx];
            const float  radM = max(st.z, cellSizeM * 0.5f);
            const float  compDepth = max(st.w, 0.0f);
            if (compDepth <= 1.0e-5f) continue;

            const float2 rel = worldXZ - st.xy;
            const float  aspect = clamp(meta.w > 0.0f ? meta.w : 1.0f, 0.35f, 3.0f);
            const float  maxRad = radM * max(aspect, 1.0f) * 1.70f;
            if (dot(rel, rel) > maxRad * maxRad) continue;

            // Rotate into stamp heading frame (allows oval boots, horse hooves, and sled/cart ruts)
            const float c = meta.y;
            const float s = meta.z;
            const float lenCS = max(length(float2(c, s)), 1.0e-4f);
            const float cn = c / lenCS;
            const float sn = s / lenCS;
            const float2 localP = float2( rel.x * cn + rel.y * sn,
                                         -rel.x * sn + rel.y * cn);

            const float rx = radM * aspect;
            const float rz = radM;
            const float normR = length(float2(localP.x / rx, localP.y / rz));

            const float availDepth = min(preStampH, compDepth);
            const float rimRatio   = clamp(meta.x, 0.0f, 1.25f);

            if (normR < 1.0f) {
                // Smooth inner footprint trench: (1 - r^2)^2
                const float oneMinusR2 = 1.0f - normR * normR;
                const float trenchW    = oneMinusR2 * oneMinusR2;
                totalDeltaH  -= availDepth * trenchW;
                addedCompact  = max(addedCompact, trenchW * saturate(availDepth / max(radM, 0.1f)));
            } else if (normR < 1.65f) {
                // Mass-conserving raised outer snow rim: sin^2(pi * (r - 1) / 0.65)
                const float u    = (normR - 1.0f) / 0.65f;
                const float su   = sin(kPi * u);
                const float rimW = su * su;
                totalDeltaH  += availDepth * rimRatio * kRimAmpRatio * rimW;
                addedCompact  = max(addedCompact, 0.35f * rimW);
            }
        }
    }

    state.r = clamp(state.r + totalDeltaH, 0.0f, maxDepthM);
    state.g = saturate(state.g + addedCompact * 0.75f);

    // High compaction in freezing weather subtly glazes compacted tracks
    if (localTempC < -2.0f && addedCompact > 0.25f) {
        state.a = saturate(state.a + addedCompact * 0.08f);
    }

    SnowStateOut[dtid.xy] = SanitizeState(state, maxDepthM);
}

// ============================================================================
// Pass 2: 3x3 Sobel Surface Normal + Snow Micro-Crystal Sparkle / Black-Ice Glint
// ============================================================================
[numthreads(8, 8, 1)]
void cs_snow_normal_height(uint3 dtid : SV_DispatchThreadID)
{
    if (dtid.x >= gGridDims.x || dtid.y >= gGridDims.y) return;

    const int2  dims = int2(gGridDims.xy);
    const int2  p    = int2(dtid.xy);
    const float maxDepthM      = max(gSimParams.y, 0.1f);
    const float cellSizeM      = max(gWorldParams.z, 1.0e-3f);
    const float normalStrength = max(gWindAndFlags.z, 0.05f);

    // 3x3 height neighborhood for clean Sobel derivative
    const float hTL = SanitizeState(SnowStateIn.Load(int3(clamp(p + int2(-1, -1), int2(0, 0), dims - 1), 0)), maxDepthM).r;
    const float hTC = SanitizeState(SnowStateIn.Load(int3(clamp(p + int2( 0, -1), int2(0, 0), dims - 1), 0)), maxDepthM).r;
    const float hTR = SanitizeState(SnowStateIn.Load(int3(clamp(p + int2( 1, -1), int2(0, 0), dims - 1), 0)), maxDepthM).r;
    const float hML = SanitizeState(SnowStateIn.Load(int3(clamp(p + int2(-1,  0), int2(0, 0), dims - 1), 0)), maxDepthM).r;
    const float4 cS = SanitizeState(SnowStateIn.Load(int3(p, 0)), maxDepthM);
    const float hMR = SanitizeState(SnowStateIn.Load(int3(clamp(p + int2( 1,  0), int2(0, 0), dims - 1), 0)), maxDepthM).r;
    const float hBL = SanitizeState(SnowStateIn.Load(int3(clamp(p + int2(-1,  1), int2(0, 0), dims - 1), 0)), maxDepthM).r;
    const float hBC = SanitizeState(SnowStateIn.Load(int3(clamp(p + int2( 0,  1), int2(0, 0), dims - 1), 0)), maxDepthM).r;
    const float hBR = SanitizeState(SnowStateIn.Load(int3(clamp(p + int2( 1,  1), int2(0, 0), dims - 1), 0)), maxDepthM).r;

    // 3x3 Sobel partial derivatives dh/dx and dh/dz in meters per meter
    const float invSobel = normalStrength / (8.0f * cellSizeM);
    const float dhdx = ((hTR + 2.0f * hMR + hBR) - (hTL + 2.0f * hML + hBL)) * invSobel;
    const float dhdz = ((hBL + 2.0f * hBC + hBR) - (hTL + 2.0f * hTC + hTR)) * invSobel;

    float3 n = normalize(float3(-dhdx, 1.0f, -dhdz));
    if (!all(isfinite(n))) {
        n = float3(0.0f, 1.0f, 0.0f);
    }

    // World-anchored deterministic snow crystal sparkle + glossy black-ice / wet-slush glint mask
    const int2  worldCell = gToroidScroll.zw + p;
    const float hSpark    = Hash21(worldCell);
    const float crystal   = pow(saturate((hSpark - 0.72f) / 0.28f), 2.5f);
    const float snowMask  = saturate(cS.r / 0.03f);
    const float dryPowder = snowMask * (1.0f - 0.85f * cS.b) * (1.0f - 0.45f * cS.g);
    const float sparkle   = saturate(max(crystal * dryPowder, max(0.85f * cS.a, 0.35f * cS.b)));

    SnowStateOut[dtid.xy] = float4(n, sparkle);
}
