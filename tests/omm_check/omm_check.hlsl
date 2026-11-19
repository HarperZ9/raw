// omm_check.hlsl: one ray straight down through the centre of each micro-triangle of a unit triangle
// (tests/omm_check/main.cpp). Out: 1 where the ray hit, 0 where it passed. Original work, MIT.
RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<float2> Points : register(t1);   // barycentric centres, in the order the test lists them
RWStructuredBuffer<uint> Hit : register(u0);
cbuffer C : register(b0) { uint count, pad0, pad1, pad2; };

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= count) { return; }
    const float2 b = Points[id.x];
    // The triangle: v0 (0, 0, 0), v1 (1, 0, 0), v2 (0, 1, 0); a point is v0 + b.x (v1 - v0) + b.y (v2 - v0).
    RayDesc r;
    r.Origin = float3(b.x, b.y, 1.0f);
    r.Direction = float3(0.0f, 0.0f, -1.0f);
    r.TMin = 0.0f;
    r.TMax = 2.0f;
    RayQuery<RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(Scene, 0, 0xFF, r);
    // Unknown states come back as candidates; the test builds 2-state maps, so none should.
    uint candidates = 0u;
    while (q.Proceed()) { candidates++; }
    Hit[id.x] = (q.CommittedStatus() == COMMITTED_TRIANGLE_HIT ? 1u : 0u) | (candidates << 1);
}
