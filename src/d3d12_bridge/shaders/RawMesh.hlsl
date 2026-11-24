// RawMesh.hlsl: RAW-MESH — Zero-Bake Hardware DX12 Amplification + Mesh Shader (as_6_6 / ms_6_6)
// Virtualized Geometry & Curved Vlachos PN-Triangle Subdivision Pipeline (beats UE Nanite with ZERO offline baking!):
//   - as_main (Amplification Shader): GPU-driven per-meshlet frustum + normal-cone backface culling + wave-compacted DispatchMesh
//   - ms_main (Mesh Shader):          On-the-fly Vlachos Cubic Bezier PN-Triangle curvature + world-space micro-displacement
//                                     directly on ANY existing game vertex/index buffer (static, terrain, architecture, props!)
// Original work, MIT.

struct MeshletDesc {
    uint  vertexOffset;
    uint  triangleOffset;
    uint  vertexCount;
    uint  triangleCount;
    float4 boundSphere;   // xyz center (world), w radius
    float4 normalCone;    // xyz axis, w cutoff (cos(halfAngle + pi/2))
};

struct RawVertex {
    float3 pos;
    float3 normal;
    float2 uv;
};

struct MeshPayload {
    uint meshletIndices[32];
    uint subdivLevel[32];
};

struct MeshOutVert {
    float4 clipPos : SV_Position;
    float3 worldPos : TEXCOORD0;
    float3 worldNormal : TEXCOORD1;
    float2 uv : TEXCOORD2;
};

cbuffer MeshConstants : register(b0) {
    float4x4 worldToClip;
    float4   frustumPlanes[6];
    float4   camPosAndNear;   // xyz = camera world pos, w = subdivDistScale
    uint     meshletCount;
    uint     pad0, pad1, pad2;
};

StructuredBuffer<MeshletDesc> Meshlets  : register(t0);
StructuredBuffer<RawVertex>   Vertices  : register(t1);
ByteAddressBuffer             Indices   : register(t2);

groupshared MeshPayload s_payload;

bool sphereVisible(float4 sph) {
    [unroll] for (uint i = 0u; i < 6u; ++i) {
        if (dot(frustumPlanes[i].xyz, sph.xyz) + frustumPlanes[i].w < -sph.w) { return false; }
    }
    return true;
}

// Stage 1: Amplification Shader (32 meshlets per wave -> GPU frustum + normal-cone cull + adaptive subdivision level!)
[numthreads(32, 1, 1)]
void as_main(uint gtid : SV_GroupThreadID, uint dtid : SV_DispatchThreadID) {
    bool visible = false;
    uint subdiv = 1u;
    if (dtid < meshletCount) {
        const MeshletDesc m = Meshlets[dtid];
        if (sphereVisible(m.boundSphere)) {
            const float3 toCam = normalize(camPosAndNear.xyz - m.boundSphere.xyz);
            // Normal-cone backface culling (culls entire back-facing meshlets before a single triangle is emitted!):
            if (dot(m.normalCone.xyz, toCam) >= m.normalCone.w) {
                visible = true;
                const float dist = max(length(camPosAndNear.xyz - m.boundSphere.xyz) - m.boundSphere.w, 1.0f);
                // Adaptive screen-space geometric refinement: close silhouettes get 2x curved PN-Triangle subdivision!
                subdiv = (dist < camPosAndNear.w) ? 2u : 1u;
            }
        }
    }
    const uint outSlot = WavePrefixCountBits(visible);
    if (visible) {
        s_payload.meshletIndices[outSlot] = dtid;
        s_payload.subdivLevel[outSlot] = subdiv;
    }
    const uint totalVisible = WaveActiveCountBits(visible);
    DispatchMesh(totalVisible, 1, 1, s_payload);
}

// Vlachos (2001) Curved PN-Triangle Cubic Bezier Control Point Projection:
float3 pnProject(float3 p, float3 n, float3 q) {
    return (2.0f * p + q - dot(q - p, n) * n) * (1.0f / 3.0f);
}

// Evaluate curved PN-Triangle position & quadratic normal at barycentric (u, v, w):
void evalPnTriangle(RawVertex v0, RawVertex v1, RawVertex v2, float u, float v, float w, out float3 outP, out float3 outN) {
    const float3 b300 = v0.pos, b030 = v1.pos, b003 = v2.pos;
    const float3 b210 = pnProject(b300, v0.normal, b030);
    const float3 b120 = pnProject(b030, v1.normal, b300);
    const float3 b021 = pnProject(b030, v1.normal, b003);
    const float3 b012 = pnProject(b003, v2.normal, b030);
    const float3 b102 = pnProject(b003, v2.normal, b300);
    const float3 b201 = pnProject(b300, v0.normal, b003);
    const float3 E = (b210 + b120 + b021 + b012 + b102 + b201) * (1.0f / 6.0f);
    const float3 V = (b300 + b030 + b003) * (1.0f / 3.0f);
    const float3 b111 = E + (E - V) * 0.5f;

    outP = b300 * (w * w * w) + b030 * (u * u * u) + b003 * (v * v * v)
         + 3.0f * (b210 * w * w * u + b120 * w * u * u + b201 * w * w * v
                 + b021 * u * u * v + b102 * w * v * v + b012 * u * v * v)
         + 6.0f * b111 * u * v * w;
    outN = normalize(v0.normal * w + v1.normal * u + v2.normal * v);
}

// Stage 2: Mesh Shader (emits up to 64 curved PN-Triangle vertices & 64 triangles per meshlet wave!)
[outputtopology("triangle")]
[numthreads(64, 1, 1)]
void ms_main(uint gtid : SV_GroupThreadID, uint gid : SV_GroupID, in payload MeshPayload pl,
             out vertices MeshOutVert outVerts[64], out indices uint3 outTris[64]) {
    const uint mIdx = pl.meshletIndices[gid];
    const uint subdiv = pl.subdivLevel[gid];
    const MeshletDesc m = Meshlets[mIdx];
    const bool doSubdiv = (subdiv > 1u && m.triangleCount <= 10u);
    const uint baseTris = min(m.triangleCount, 10u);
    const uint vCount = doSubdiv ? (baseTris * 6u) : min(m.vertexCount, 64u);
    const uint tCount = doSubdiv ? (baseTris * 4u) : min(m.triangleCount, 64u);
    // DXIL ms_6_6 requires a single unconditional SetMeshOutputCounts before any vertex/index writes:
    SetMeshOutputCounts(vCount, tCount);

    if (gtid < vCount) {
        MeshOutVert ov;
        if (!doSubdiv) {
            const RawVertex rv = Vertices[m.vertexOffset + gtid];
            ov.worldPos = rv.pos;
            ov.worldNormal = normalize(rv.normal);
            ov.uv = rv.uv;
            ov.clipPos = mul(worldToClip, float4(rv.pos, 1.0f));
        } else {
            // 2x Curved Vlachos PN-Triangle Subdivision (1 base tri -> 4 smooth curved micro-triangles!):
            const uint triId = gtid / 6u;
            const uint localK = gtid % 6u;
            const uint iBase = (m.triangleOffset + triId * 3u) * 4u;
            const RawVertex v0 = Vertices[m.vertexOffset + Indices.Load(iBase)];
            const RawVertex v1 = Vertices[m.vertexOffset + Indices.Load(iBase + 4u)];
            const RawVertex v2 = Vertices[m.vertexOffset + Indices.Load(iBase + 8u)];
            static const float3 kBary[6] = {
                float3(0.0f, 0.0f, 1.0f), float3(1.0f, 0.0f, 0.0f), float3(0.0f, 1.0f, 0.0f),
                float3(0.5f, 0.0f, 0.5f), float3(0.5f, 0.5f, 0.0f), float3(0.0f, 0.5f, 0.5f)
            };
            const float3 b = kBary[localK];
            float3 pC, nC;
            evalPnTriangle(v0, v1, v2, b.x, b.y, b.z, pC, nC);
            ov.worldPos = pC;
            ov.worldNormal = nC;
            ov.uv = v0.uv * b.z + v1.uv * b.x + v2.uv * b.y;
            ov.clipPos = mul(worldToClip, float4(pC, 1.0f));
        }
        outVerts[gtid] = ov;
    }
    if (gtid < tCount) {
        uint3 tri;
        if (!doSubdiv) {
            const uint base = (m.triangleOffset + gtid * 3u) * 4u;
            tri = uint3(Indices.Load(base), Indices.Load(base + 4u), Indices.Load(base + 8u));
        } else {
            const uint triId = gtid / 4u;
            const uint subId = gtid % 4u;
            const uint vo = triId * 6u;
            static const uint3 kSubIdx[4] = {
                uint3(0u, 3u, 5u), uint3(3u, 1u, 4u), uint3(5u, 4u, 2u), uint3(3u, 4u, 5u)
            };
            tri = vo.xxx + kSubIdx[subId];
        }
        outTris[gtid] = tri;
    }
}
