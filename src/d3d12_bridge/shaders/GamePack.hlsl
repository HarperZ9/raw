// GamePack.hlsl: the game's depth and normal targets, as they crossed the
// bridge, packed into the view-space G-buffer the engine's post passes read
// (8 floats a pixel: position, view depth, normal, back depth). Original work, MIT.
//
// Nothing here is estimated. Depth is the game's depth buffer value, taken
// back to view depth with the two terms of the game's own projection matrix.
// The normal is the game's normal target, decoded with the inverse of what
// the game's lighting shaders write (docs/FRAME_BUFFERS.md, "Normals").
//
// The game's view space looks along +z with y up. The engine's looks along
// -z with y up, so z changes sign for the position and for the normal.
//
// Bindings follow the engine RHI's compute convention: binding 0 is the
// uniform, binding i is a root UAV at u<i>.

cbuffer C : register(b0)
{
    uint  width;
    uint  height;
    uint  depthStride;    // floats between rows of D
    uint  normalStride;   // uints between rows of N
    float projA;          // depth = projA - projB / viewZ  (game matrix terms [2][2] and -[2][3])
    float projB;
    float tanHalfFovY;    // 1 / proj[1][1]
    float aspect;         // proj[1][1] / proj[0][0]
    uint  normalFormat;   // 0: R8G8B8A8_UNORM
    uint  pad0, pad1, pad2;
};
RWStructuredBuffer<float> D : register(u1);   // depth buffer values, 0 near, 1 far
RWStructuredBuffer<uint>  N : register(u2);   // normal target texels
RWStructuredBuffer<float> G : register(u3);

[numthreads(64, 1, 1)]
void main(uint3 id : SV_DispatchThreadID)
{
    const uint i = id.x + id.y * 2097152u;   // rows of 32,768 groups, as the engine's passes index
    if (i >= width * height) { return; }
    const uint x = i % width;
    const uint y = i / width;
    const float d = D[y * depthStride + x];
    const uint b = i * 8u;
    // Nothing drawn (the depth clear value) or nothing in front of the far plane.
    if (d >= 1.0f || projA - d <= 0.0f) {
        G[b] = 0.0f; G[b + 1u] = 0.0f; G[b + 2u] = 0.0f; G[b + 3u] = -1.0f;
        G[b + 4u] = 0.0f; G[b + 5u] = 0.0f; G[b + 6u] = 1.0f; G[b + 7u] = -1.0f;
        return;
    }
    const float viewZ = projB / (projA - d);
    // The pixel centre on the image plane, with the engine's camera model.
    const float sx = ((float(x) + 0.5f) / float(width)) * 2.0f - 1.0f;
    const float sy = 1.0f - ((float(y) + 0.5f) / float(height)) * 2.0f;
    G[b]      = sx * viewZ * tanHalfFovY * aspect;
    G[b + 1u] = sy * viewZ * tanHalfFovY;
    G[b + 2u] = -viewZ;
    G[b + 3u] = viewZ;

    // The game writes enc = n.xy / sqrt(8 - 8 n.z) + 0.5. With s = |enc - 0.5|^2:
    // n.z = 8 s - 1 and n.xy = 4 (enc - 0.5) sqrt(1 - 4 s).
    const uint t = N[y * normalStride + x];
    const float2 enc = float2(float(t & 255u), float((t >> 8u) & 255u)) / 255.0f - 0.5f;
    const float s = min(dot(enc, enc), 0.25f);
    const float k = 4.0f * sqrt(1.0f - 4.0f * s);
    float3 n = float3(enc.x * k, enc.y * k, 8.0f * s - 1.0f);
    n = n / max(length(n), 1.0e-6f);
    G[b + 4u] = n.x;
    G[b + 5u] = n.y;
    G[b + 6u] = -n.z;
    G[b + 7u] = viewZ;
}
