// NormalUnpack.hlsl: SM 6.9 compute pass that unpacks Skyrim SE's R8G8B8A8_UNORM stereographic octahedral
// view-space normal + Specular gloss into a linear RGBA16F packed world-space normal + roughness buffer
// (RGB = linear world-space normal, A = linear perceptual roughness) for NVIDIA DLSS Ray Reconstruction (ePacked).
// Original work, MIT.
Texture2D<float4> NormalIn : register(t0);
Texture2D<float4> SpecIn   : register(t1);
RWTexture2D<float4> OutNR  : register(u0);

cbuffer UnpackCB : register(b0) {
    float4 v2w0;
    float4 v2w1;
    float4 v2w2;
    uint   width;
    uint   height;
    uint   pad0;
    uint   pad1;
};

[numthreads(8, 8, 1)]
void main(uint3 id : SV_DispatchThreadID) {
    if (id.x >= width || id.y >= height) { return; }
    const int3 ip = int3(id.xy, 0);
    const float2 e = NormalIn.Load(ip).xy - 0.5f;
    const float s = min(dot(e, e), 0.25f);
    const float3 nv = float3(e * (4.0f * sqrt(1.0f - 4.0f * s)), 8.0f * s - 1.0f);
    float3 nw = float3(dot(v2w0.xyz, nv), dot(v2w1.xyz, nv), dot(v2w2.xyz, nv));
    const float len = length(nw);
    nw = len > 1.0e-5f ? (nw / len) : float3(0.0f, 0.0f, 1.0f);
    const float gloss = SpecIn.Load(ip).y;
    const float rough = clamp(1.0f - gloss, 0.04f, 1.0f);
    OutNR[id.xy] = float4(nw, rough);
}
