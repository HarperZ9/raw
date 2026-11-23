// HiZDownsample.hlsl: SM 6.9 native DX12 compute 8x8 conservative MAX-depth reduction (1920x1080 -> 240x135).
// Produces a 129 KB R32_FLOAT mip0 depth buffer for zero-stall 3-frame ring readback into OcclusionCuller.
// Original work, MIT.
Texture2D<float> DepthIn       : register(t0);
RWStructuredBuffer<float> OutZ : register(u0);

cbuffer HiZCB : register(b0) {
    uint srcW;
    uint srcH;
    uint dstW;
    uint dstH;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= dstW || id.y >= dstH) { return; }
    const uint x0 = id.x * 8u;
    const uint y0 = id.y * 8u;
    float maxZ = 0.0f;
    [unroll] for (uint dy = 0u; dy < 8u; ++dy) {
        const uint sy = min(y0 + dy, srcH - 1u);
        [unroll] for (uint dx = 0u; dx < 8u; ++dx) {
            const uint sx = min(x0 + dx, srcW - 1u);
            maxZ = max(maxZ, DepthIn.Load(int3(sx, sy, 0)));
        }
    }
    OutZ[id.y * dstW + id.x] = maxZ;
}
