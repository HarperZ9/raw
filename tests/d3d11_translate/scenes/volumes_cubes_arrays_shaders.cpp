// Scene 14 shaders.
#include "volumes_cubes_arrays.h"

namespace txtest::volumes {
namespace {
// Volume A: a 4x4x4 checker of 1.0 and 0.25. Volume B: A, A mirrored in x, and a ramp in z.
const char* kCsSrc = R"(
RWTexture3D<float> dst : register(u0);
Texture3D<float> src : register(t0);
[numthreads(8, 8, 8)]
void fill(uint3 id : SV_DispatchThreadID)
{
    uint par = ((id.x >> 2) + (id.y >> 2) + (id.z >> 2)) & 1;
    dst[id] = par ? 1.0 : 0.25;
}
[numthreads(8, 8, 8)]
void derive(uint3 id : SV_DispatchThreadID)
{
    float a = src.Load(int4(id, 0));
    float m = src.Load(int4(31 - id.x, id.y, id.z, 0));
    dst[id] = a * 0.5 + m * 0.25 + id.z / 64.0;
}
)";

const char* kPsSrc = R"(
cbuffer P : register(b0) { float4 p; };
struct FO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 psMipSeed(float4 pos : SV_Position) : SV_Target
{
    uint2 q = uint2(pos.xy);
    return float4(((q.x * q.x * 3 + q.y * 5 + (q.x ^ q.y) * 7) & 31) / 32.0, 0, 0, 1);
}
Texture2D<float> srcMip : register(t0);
float4 psMipMax(float4 pos : SV_Position) : SV_Target
{
    int2 q = int2(pos.xy) * 2;
    float a = srcMip.Load(int3(q, 0)), b = srcMip.Load(int3(q + int2(1, 0), 0));
    float c = srcMip.Load(int3(q + int2(0, 1), 0)), d = srcMip.Load(int3(q + int2(1, 1), 0));
    return float4(max(max(a, b), max(c, d)), 0, 0, 1);
}
TextureCube cube : register(t0);
Texture2DArray<float> arr : register(t0);
Texture3D<float> vol : register(t0);
Texture2D<float> pyramid : register(t0);
SamplerState smp : register(s0);
// Face order +X -X +Y -Y +Z -Z; (s, t) run right and down across the face.
float3 FaceDir(int f, float s, float t)
{
    if (f == 0) return float3(1, -t, -s);
    if (f == 1) return float3(-1, -t, s);
    if (f == 2) return float3(s, 1, t);
    if (f == 3) return float3(s, -1, -t);
    if (f == 4) return float3(s, -t, 1);
    return float3(-s, -t, -1);
}
float4 psCube(FO i) : SV_Target
{
    float3 dir = FaceDir(int(p.x), i.uv.x * 2 - 1, i.uv.y * 2 - 1);
    return float4(cube.SampleLevel(smp, dir, 0).rgb * 0.5, 1);
}
float4 psArr(FO i) : SV_Target
{
    float d = arr.SampleLevel(smp, float3(i.uv, p.x), 0);
    return p.x < 0.5 ? float4(d, d, 0, 1) : float4(0, d, d, 1);
}
float4 psVol(FO i) : SV_Target
{
    float v = vol.SampleLevel(smp, float3(i.uv, p.x), 0);
    return float4(v, v, v * 0.5, 1);
}
float4 psMip(FO i) : SV_Target
{
    float v = pyramid.SampleLevel(smp, i.uv, p.x) * 0.9;
    return float4(v, 1 - v, 0.25, 1);
}
)";

// Unit quad per instance, placed and coloured by SV_InstanceID (256ths of the target).
const char* kInstSrc = R"(
struct VO { float4 pos : SV_Position; float4 col : COLOR0; };
VO vsInst(float2 q : POSITION, uint inst : SV_InstanceID)
{
    float2 px = float2(8 + 48 * inst, 228) + q * 24;
    VO o;
    o.pos = float4(px.x / 128.0 - 1, 1 - px.y / 128.0, 0.5, 1);
    o.col = float4((inst + 1) * 0.2, 1 - inst * 0.2, 0.6, 1);
    return o;
}
)";
}  // namespace

bool MakeShaders(SceneCtx& c, Res& r)
{
    TX_REQ(kName, MakeColorPipe(c.dev, r.pipe));
    r.fsVs = MakeVS(c.dev, kFullscreenVS, "vs");
    ComPtr<ID3DBlob> blob;
    r.vsInst = MakeVS(c.dev, kInstSrc, "vsInst", &blob);
    const D3D11_INPUT_ELEMENT_DESC el[1] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    r.layoutInst = MakeLayout(c.dev, el, 1, blob.Get());
    r.psMipSeed = MakePS(c.dev, kPsSrc, "psMipSeed");
    r.psMipMax = MakePS(c.dev, kPsSrc, "psMipMax");
    r.psCube = MakePS(c.dev, kPsSrc, "psCube");
    r.psArr = MakePS(c.dev, kPsSrc, "psArr");
    r.psVol = MakePS(c.dev, kPsSrc, "psVol");
    r.psMip = MakePS(c.dev, kPsSrc, "psMip");
    r.csFill = MakeCS(c.dev, kCsSrc, "fill");
    r.csDerive = MakeCS(c.dev, kCsSrc, "derive");
    TX_REQ(kName, r.fsVs && r.vsInst && r.layoutInst && r.psMipSeed && r.psMipMax);
    TX_REQ(kName, r.psCube && r.psArr && r.psVol && r.psMip && r.csFill && r.csDerive);
    return true;
}

}  // namespace txtest::volumes
