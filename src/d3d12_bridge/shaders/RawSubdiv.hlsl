// RawSubdiv.hlsl: Runtime GPU Compute Adaptive Curved PN-Triangle Subdivision & 3D Snow-Buildup Kernel (SM 6.6 / 6.9).
// Transforms ANY low-poly Creation Engine triangle stream (world terrain, rocks, architecture, furniture, skinned actors)
// into 4x higher-resolution curved geometry with crease preservation and live 3D snowpack elevation for DXR 1.2 BLAS
// and RAW-MESH rendering WITHOUT touching Bethesda's CPU Havok collision or stock NAVM records!
// Original work, MIT.

struct SubdivVertex {
    float3 pos;
    float3 normal;
    float2 uv;
};

cbuffer SubdivParams : register(b0) {
    uint  inTriangleCount;
    float curvatureGain;    // 0..1.25 (default 0.72)
    float displaceAmp;      // world-space micro-displacement scale
    float snowDepthU;       // volumetric snowpack buildup depth in world units (0..48)
    float snowAvalancheCos; // minimum n.z cosine for snow accumulation (default 0.42)
    float3 camPosW;
    uint  assetClass14;     // Wave 88: AssetClass14 (1..14) for class-specific 3D displacement
    uint  subdivPassIndex;  // 0 = 1->4 pass, 1 = 1->16 cascade pass
    float mossLiftAmp;      // 3D velvet moss cushion loft on upward rock/masonry
    float pad0;
};

StructuredBuffer<SubdivVertex>   InVerts  : register(t0);
StructuredBuffer<uint>           InIdx    : register(t1);
RWStructuredBuffer<SubdivVertex> OutVerts : register(u0);
RWStructuredBuffer<uint>         OutIdx   : register(u1);

float subHash31(float3 p) {
    p = frac(p * 0.1031f);
    p += dot(p, p.yzx + 33.33f);
    return frac((p.x + p.y) * p.z);
}

float subNoise3(float3 p) {
    const float3 i = floor(p);
    const float3 f = frac(p);
    const float3 u = f * f * (3.0f - 2.0f * f);
    const float n000 = subHash31(i + float3(0, 0, 0)), n100 = subHash31(i + float3(1, 0, 0));
    const float n010 = subHash31(i + float3(0, 1, 0)), n110 = subHash31(i + float3(1, 1, 0));
    const float n001 = subHash31(i + float3(0, 0, 1)), n101 = subHash31(i + float3(1, 0, 1));
    const float n011 = subHash31(i + float3(0, 1, 1)), n111 = subHash31(i + float3(1, 1, 1));
    return lerp(lerp(lerp(n000, n100, u.x), lerp(n010, n110, u.x), u.y),
                lerp(lerp(n001, n101, u.x), lerp(n011, n111, u.x), u.y), u.z);
}

// Wave 88: Crack-free deterministic class-specific 3D world-space displacement (matches RawTotalRebirth.h).
float evalClassDisplacement3D(float3 wp, float3 n, uint classId, float amp) {
    if (amp <= 1.0e-5f || classId == 14u || classId == 8u) { return 0.0f; }
    const float baseN = subNoise3(wp * 0.07f);
    if (classId == 3u) { // RockCliffBoulder: fractured sedimentary rock strata + basalt ridges
        const float strata = sin(wp.z * 0.16f + baseN * 2.2f);
        return (strata * 0.55f + (baseN - 0.5f) * 1.15f) * amp;
    }
    if (classId == 2u) { // CobbleRoadPath: chamfered cobblestone domes with recessed mortar
        const float2 g = frac(wp.xy * 0.045f);
        const float edge = min(min(g.x, 1.0f - g.x), min(g.y, 1.0f - g.y));
        return (smoothstep(0.04f, 0.22f, edge) - 0.35f) * amp * 1.10f;
    }
    if (classId == 11u) { // NordicStoneMasonry: hand-dressed ashlar stone bevels
        const float gx = frac((wp.x + wp.y) * 0.025f);
        const float gy = frac(wp.z * 0.040f);
        const float bevel = smoothstep(0.03f, 0.14f, min(min(gx, 1.0f - gx), min(gy, 1.0f - gy)));
        return (bevel - 0.40f + (baseN - 0.5f) * 0.35f) * amp;
    }
    if (classId == 5u) { // TreeTrunkBark: longitudinal corrugated bark furrows
        const float ridge = abs(subNoise3(float3((wp.x + wp.y) * 0.22f, wp.z * 0.035f, 1.7f)) * 2.0f - 1.0f);
        return (0.5f - ridge) * amp * 1.25f;
    }
    if (classId == 9u) { // GlacierIce: sharp faceted conchoidal ridges
        const float facet = abs(sin((wp.x * 0.05f - wp.y * 0.04f + wp.z * 0.06f) + baseN));
        return (facet - 0.5f) * amp * 1.15f;
    }
    return (baseN - 0.5f) * amp * 1.85f;
}

float3 pnEdge(float3 p, float3 n, float3 q, float creaseW) {
    const float3 lin = (2.0f * p + q) * (1.0f / 3.0f);
    const float3 crv = (2.0f * p + q - dot(q - p, n) * n) * (1.0f / 3.0f);
    return lerp(lin, crv, creaseW);
}

// 1 thread per input triangle -> emits 6 vertices and 4 curved micro-triangles (12 indices).
[numthreads(64, 1, 1)]
void subdiv_main(uint tid : SV_DispatchThreadID) {
    if (tid >= inTriangleCount) { return; }

    const SubdivVertex v0 = InVerts[InIdx[tid * 3u + 0u]];
    const SubdivVertex v1 = InVerts[InIdx[tid * 3u + 1u]];
    const SubdivVertex v2 = InVerts[InIdx[tid * 3u + 2u]];

    const float3 n0 = normalize(v0.normal + 1.0e-6f);
    const float3 n1 = normalize(v1.normal + 1.0e-6f);
    const float3 n2 = normalize(v2.normal + 1.0e-6f);
    const float c01 = smoothstep(0.25f, 0.72f, dot(n0, n1)) * curvatureGain;
    const float c12 = smoothstep(0.25f, 0.72f, dot(n1, n2)) * curvatureGain;
    const float c20 = smoothstep(0.25f, 0.72f, dot(n2, n0)) * curvatureGain;

    const float3 b300 = v0.pos, b030 = v1.pos, b003 = v2.pos;
    const float3 b210 = pnEdge(b300, n0, b030, c01);
    const float3 b120 = pnEdge(b030, n1, b300, c01);
    const float3 b021 = pnEdge(b030, n1, b003, c12);
    const float3 b012 = pnEdge(b003, n2, b030, c12);
    const float3 b102 = pnEdge(b003, n2, b300, c20);
    const float3 b201 = pnEdge(b300, n0, b003, c20);
    const float3 E = (b210 + b120 + b021 + b012 + b102 + b201) * (1.0f / 6.0f);
    const float3 V = (b300 + b030 + b003) * (1.0f / 3.0f);
    const float3 b111 = E + (E - V) * 0.5f;

    static const float3 kBary[6] = {
        float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f),
        float3(0.5f, 0.0f, 0.5f), float3(0.5f, 0.5f, 0.0f), float3(0.0f, 0.5f, 0.5f)
    };

    const uint vOutBase = tid * 6u;
    [unroll] for (uint k = 0u; k < 6u; ++k) {
        const float u = kBary[k].x, v = kBary[k].y, w = kBary[k].z;
        float3 p = b300 * (w * w * w) + b030 * (u * u * u) + b003 * (v * v * v)
                 + 3.0f * (b210 * w * w * u + b120 * w * u * u + b201 * w * w * v
                         + b021 * u * u * v + b102 * w * v * v + b012 * u * v * v)
                 + 6.0f * b111 * u * v * w;
        const float3 n = normalize(n0 * w + n1 * u + n2 * v);
        // Crack-free class-specific 3D displacement on subdivided edge midpoints (k >= 3):
        const float passScale = (subdivPassIndex == 0u) ? 1.0f : 0.45f;
        if (k >= 3u && displaceAmp > 0.001f) {
            const float wpDisp = evalClassDisplacement3D(p + camPosW, n, assetClass14, displaceAmp * passScale);
            p += n * wpDisp;
        }
        // Organic 3D velvet moss cushion loft on upward-facing rock/masonry/road edges:
        if (k >= 3u && mossLiftAmp > 0.001f && n.z > 0.40f && (assetClass14 == 3u || assetClass14 == 4u || assetClass14 == 11u)) {
            const float mPatch = smoothstep(0.48f, 0.78f, subNoise3((p + camPosW) * 0.025f));
            p.z += mPatch * smoothstep(0.40f, 0.80f, n.z) * mossLiftAmp * passScale;
        }
        // True 3D volumetric snow buildup lift on upward-facing surfaces above angle of repose:
        if (snowDepthU > 0.05f && n.z > snowAvalancheCos) {
            const float sMask = smoothstep(snowAvalancheCos, min(snowAvalancheCos + 0.35f, 0.98f), n.z);
            const float drift = 0.70f + 0.50f * subNoise3((p + camPosW) * 0.015f);
            p.z += sMask * drift * snowDepthU * 0.32f;
        }
        SubdivVertex ov;
        ov.pos = p;
        ov.normal = n;
        ov.uv = v0.uv * w + v1.uv * u + v2.uv * v;
        OutVerts[vOutBase + k] = ov;
    }

    const uint iOutBase = tid * 12u;
    static const uint3 kSubIdx[4] = {
        uint3(0u, 3u, 5u), uint3(3u, 1u, 4u), uint3(5u, 4u, 2u), uint3(3u, 4u, 5u)
    };
    [unroll] for (uint s = 0u; s < 4u; ++s) {
        OutIdx[iOutBase + s * 3u + 0u] = vOutBase + kSubIdx[s].x;
        OutIdx[iOutBase + s * 3u + 1u] = vOutBase + kSubIdx[s].y;
        OutIdx[iOutBase + s * 3u + 2u] = vOutBase + kSubIdx[s].z;
    }
}
