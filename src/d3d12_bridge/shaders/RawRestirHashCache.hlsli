// RawRestirHashCache.hlsli: Wave 75 Disocclusion-Safe 2-Slot ReSTIR-GI/PT Shift-Mapping Jacobian,
// Generalized Balance Heuristic MIS, and Multi-Resolution World-Space Hash Radiance Cache Helpers.
// Self-contained clean-room MIT HLSL library (SM 6.6 / 6.9 / 6.10, zero NaN/Inf).
// Original work, MIT.
#ifndef RAW_RESTIR_HASH_CACHE_HLSLI
#define RAW_RESTIR_HASH_CACHE_HLSLI

float rawRestirSanitize1(float v, float fallbackVal) {
    return isfinite(v) ? v : fallbackVal;
}

float3 rawRestirSanitize3(float3 v) {
    return all(isfinite(v)) ? clamp(v, 0.0f.xxx, 32.0f.xxx) : 0.0f.xxx;
}

// Shift-mapping geometric Jacobian determinant |J_{T_{q_k -> q}}| for ReSTIR-GI/PT reuse:
//   |J| = (|cos(theta'_s)| * ||x_s - x_k||^2) / (|cos(theta_s)| * ||x_s - x_q||^2)
// Safely clamped to [0.10, 10.0] with strict zero-division & NaN/Inf protection.
float rawEvalRestirShiftJacobian(float3 xTarget, float3 xNeighbor, float3 xSample, float3 nSample) {
    const float3 dNeigh = xSample - xNeighbor;
    const float3 dTarg  = xSample - xTarget;
    const float  r2Neigh = dot(dNeigh, dNeigh);
    const float  r2Targ  = dot(dTarg, dTarg);
    if (!isfinite(r2Neigh) || !isfinite(r2Targ) || r2Neigh < 1.0e-5f || r2Targ < 1.0e-5f) {
        return 0.0f;
    }
    const float3 wNeigh = dNeigh * rsqrt(r2Neigh);
    const float3 wTarg  = dTarg * rsqrt(r2Targ);
    const float  cosSrc = abs(dot(nSample, wNeigh));
    const float  cosDst = abs(dot(nSample, wTarg));
    if (cosSrc < 1.0e-3f || cosDst < 1.0e-3f) {
        return 0.0f;
    }
    const float jac = (cosDst * r2Neigh) / max(cosSrc * r2Targ, 1.0e-5f);
    return isfinite(jac) ? clamp(jac, 0.10f, 10.0f) : 0.0f;
}

// Generalized Balance Heuristic MIS weight for merging neighbor reservoir k into target pixel q:
//   m_k = (M_k * p_k) / max(M_k * p_k + M_q * p_q * |J|, eps)
float rawEvalGeneralizedBalanceMis(float mNeighbor, float pNeighbor, float mTarget, float pTarget, float jac) {
    const float num = max(rawRestirSanitize1(mNeighbor, 0.0f), 0.0f) * max(rawRestirSanitize1(pNeighbor, 0.0f), 0.0f);
    const float den = num + max(rawRestirSanitize1(mTarget, 0.0f), 0.0f)
                          * max(rawRestirSanitize1(pTarget, 0.0f), 0.0f)
                          * max(rawRestirSanitize1(jac, 0.0f), 0.0f);
    if (den <= 1.0e-6f) { return 0.0f; }
    return saturate(num / den);
}

// 2-slot disocclusion-safe temporal layer selector:
// Returns 0 if slot 0 matches better, 1 if slot 1 matches better, or -1 if neither satisfies
// log-depth consistency and normal alignment (|n_q . n_prev| > 0.85).
int rawSelectTwoSlotReservoirLayer(
    float  zCur,
    float3 nCur,
    float  zSlot0,
    float3 nSlot0,
    bool   valid0,
    float  zSlot1,
    float3 nSlot1,
    bool   valid1)
{
    const float zSafe = max(rawRestirSanitize1(zCur, 1.0f), 1.0e-3f);
    float bestScore = 1.0e9f;
    int   bestSlot  = -1;

    if (valid0 && isfinite(zSlot0) && zSlot0 > 1.0e-3f) {
        const float logDz0 = abs(log(zSafe / zSlot0));
        const float nd0 = dot(nCur, nSlot0);
        if (logDz0 < 0.08f && nd0 > 0.85f && logDz0 < bestScore) {
            bestScore = logDz0;
            bestSlot = 0;
        }
    }
    if (valid1 && isfinite(zSlot1) && zSlot1 > 1.0e-3f) {
        const float logDz1 = abs(log(zSafe / zSlot1));
        const float nd1 = dot(nCur, nSlot1);
        if (logDz1 < 0.08f && nd1 > 0.85f && logDz1 < bestScore) {
            bestScore = logDz1;
            bestSlot = 1;
        }
    }
    return bestSlot;
}

// Multi-resolution L=8 world-space hash key (2^lod cascade over quantized world coordinates + octahedral normal).
uint rawHashCacheKeyMultiRes(float3 worldPos, float3 worldNormal, uint lodLevel, float baseCellSize) {
    const uint  lodClamped = min(lodLevel, 7u);
    const float cellSize = max(baseCellSize, 1.0f) * float(1u << lodClamped);
    const int3  qPos = int3(floor(worldPos / cellSize));
    const int3  qNrm = int3(round(clamp(worldNormal, -1.0f.xxx, 1.0f.xxx) * 3.0f));
    uint h = (uint(qPos.x) * 73856093u) ^ (uint(qPos.y) * 19349663u) ^ (uint(qPos.z) * 83492791u);
    h ^= (uint(qNrm.x + 4) * 2654435761u) ^ (uint(qNrm.y + 4) * 2246822519u) ^ (uint(qNrm.z + 4) * 3266489917u);
    h ^= (lodClamped + 1u) * 668265263u;
    h = (h ^ (h >> 16u)) * 2246822507u;
    h = (h ^ (h >> 13u)) * 3266489909u;
    h = h ^ (h >> 16u);
    return (h == 0u || h == 0xFFFFFFFFu) ? 1u : h;
}

// Relative-L2 online hash-cache EMA blend factor (SIGGRAPH NRC Eq. 2: ||L_hat - L_target||^2 / (||L_hat||^2 + 1e-2)):
float rawEvalHashCacheRelativeL2Alpha(float3 oldRad, float3 newRad, uint frameCount) {
    const float3 o = rawRestirSanitize3(oldRad);
    const float3 n = rawRestirSanitize3(newRad);
    const float3 diff = n - o;
    const float  relL2 = dot(diff, diff) / (dot(o, o) + 0.01f);
    const float  baseAlpha = 1.0f / max(float(frameCount), 1.0f);
    const float  adaptAlpha = smoothstep(0.12f, 1.25f, relL2) * 0.48f;
    return clamp(max(baseAlpha, adaptAlpha), 0.05f, 0.85f);
}

#endif
