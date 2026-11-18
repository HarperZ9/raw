// Scene 2: an indexed cube drawn 64 times with DrawIndexedInstanced, a per-instance
// vertex buffer, a mip-mapped procedural texture and a D24S8 depth buffer.
#include "../scene_util.h"

namespace txtest {
namespace {
const char* kName = "textured_instanced";

static const char* kSrc = R"(
cbuffer Frame : register(b0) { row_major float4x4 viewProj; };
struct VI {
    float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0;
    float4 w0 : WORLD0; float4 w1 : WORLD1; float4 w2 : WORLD2; float4 w3 : WORLD3;
    float4 tint : TINT;
};
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; float4 tint : COLOR0; float3 nrm : NORMAL; };
VO vs(VI i)
{
    float4x4 world = float4x4(i.w0, i.w1, i.w2, i.w3);
    float4 wp = mul(float4(i.pos, 1), world);
    VO o;
    o.pos = mul(wp, viewProj);
    o.uv = i.uv * 2.0;
    o.tint = i.tint;
    o.nrm = mul(float4(i.nrm, 0), world).xyz;
    return o;
}
Texture2D tex : register(t0);
SamplerState smp : register(s0);
float4 ps(VO i) : SV_Target
{
    float l = 0.35 + 0.65 * saturate(dot(normalize(i.nrm), normalize(float3(0.3, 0.8, -0.5))));
    return float4(tex.Sample(smp, i.uv).rgb * i.tint.rgb * l, 1);
}
)";

struct Inst { Mat4 world; float tint[4]; };

static ComPtr<ID3D11ShaderResourceView> MakeMipTexture(ID3D11Device* dev)
{
    const UINT size = 64, mips = 7;
    std::vector<std::vector<uint32_t>> level(mips);
    level[0].resize(size * size);
    for (UINT y = 0; y < size; ++y)
        for (UINT x = 0; x < size; ++x) {
            const bool chk = ((x / 8) ^ (y / 8)) & 1;
            const uint32_t r = chk ? 255 : 60 + x * 2, g = chk ? 200 + (y & 31) : 90, b = 255 - y * 3;
            level[0][y * size + x] = r | (g << 8) | ((b & 255) << 16) | 0xFF000000u;
        }
    for (UINT m = 1; m < mips; ++m) {
        const UINT s = size >> m, ps = s * 2;
        level[m].resize(s * s);
        for (UINT y = 0; y < s; ++y)
            for (UINT x = 0; x < s; ++x) {
                uint32_t px = 0;
                for (int ch = 0; ch < 4; ++ch) {
                    uint32_t sum = 2;
                    for (UINT k = 0; k < 4; ++k)
                        sum += (level[m - 1][(y * 2 + k / 2) * ps + x * 2 + k % 2] >> (8 * ch)) & 255;
                    px |= (sum / 4) << (8 * ch);
                }
                level[m][y * s + x] = px;
            }
    }
    D3D11_SUBRESOURCE_DATA sd[7]{};
    for (UINT m = 0; m < mips; ++m) sd[m] = {level[m].data(), (size >> m) * 4, 0};
    TexDesc d;
    d.w = d.h = size; d.mips = mips; d.bind = D3D11_BIND_SHADER_RESOURCE;
    auto tex = MakeTex(dev, d, sd);
    return MakeSRV(dev, tex.Get());
}

static std::vector<Inst> MakeInstances()
{
    std::vector<Inst> inst(64);
    Rng rng(0x51CE0002u);
    for (int i = 0; i < 64; ++i) {
        const float x = (static_cast<float>(i % 8) - 3.5f) * 1.45f;
        const float y = (static_cast<float>(i / 8) - 3.5f) * 1.45f;
        const Mat4 rot = Mul(RotY(rng.unit() * 6.2831853f), RotX(rng.unit() * 6.2831853f));
        inst[i].world = Mul(Mul(Scale(0.42f, 0.42f, 0.42f), rot), Translate(x, y, rng.unit() * 2.0f));
        inst[i].tint[0] = 0.4f + 0.6f * rng.unit();
        inst[i].tint[1] = 0.4f + 0.6f * rng.unit();
        inst[i].tint[2] = 0.4f + 0.6f * rng.unit();
        inst[i].tint[3] = 1.0f;
    }
    return inst;
}

static ComPtr<ID3D11InputLayout> MakeInstLayout(ID3D11Device* dev, ID3DBlob* blob)
{
    const D3D11_INPUT_CLASSIFICATION pi = D3D11_INPUT_PER_INSTANCE_DATA;
    const DXGI_FORMAT f4 = DXGI_FORMAT_R32G32B32A32_FLOAT;
    const D3D11_INPUT_ELEMENT_DESC el[8] = {
        kPNTLayout[0], kPNTLayout[1], kPNTLayout[2],
        {"WORLD", 0, f4, 1, 0, pi, 1},  {"WORLD", 1, f4, 1, 16, pi, 1},
        {"WORLD", 2, f4, 1, 32, pi, 1}, {"WORLD", 3, f4, 1, 48, pi, 1},
        {"TINT", 0, f4, 1, 64, pi, 1},
    };
    return MakeLayout(dev, el, 8, blob);
}

struct Res {
    Target rt;
    Depth depth;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vb, ib, instVb, cb;
    ComPtr<ID3D11ShaderResourceView> srv;
    ComPtr<ID3D11SamplerState> smp;
    ComPtr<ID3D11RasterizerState> rs;
    ComPtr<ID3D11DepthStencilState> ds;
};

static bool Setup(SceneCtx& c, Res& r)
{
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, r.rt));
    TX_REQ(kName, MakeDepth(c.dev, c.width, c.height, r.depth));
    ComPtr<ID3DBlob> blob;
    r.vs = MakeVS(c.dev, kSrc, "vs", &blob);
    r.ps = MakePS(c.dev, kSrc, "ps");
    r.layout = MakeInstLayout(c.dev, blob.Get());
    std::vector<VtxPNT> verts;
    std::vector<uint16_t> idx;
    MakeCube(verts, idx);
    const std::vector<Inst> inst = MakeInstances();
    const UINT vbBind = D3D11_BIND_VERTEX_BUFFER;
    r.vb = MakeBuffer(c.dev, static_cast<UINT>(verts.size() * sizeof(VtxPNT)), vbBind, verts.data());
    r.ib = MakeBuffer(c.dev, static_cast<UINT>(idx.size() * 2), D3D11_BIND_INDEX_BUFFER, idx.data());
    r.instVb = MakeBuffer(c.dev, static_cast<UINT>(inst.size() * sizeof(Inst)), vbBind, inst.data());
    const float aspect = static_cast<float>(c.width) / static_cast<float>(c.height);
    const Mat4 vp = Mul(LookAtLH({0, 0, -12}, {0, 0, 0}, {0, 1, 0}), PerspLH(1.0472f, aspect, 0.5f, 50.0f));
    r.cb = MakeBuffer(c.dev, sizeof(Mat4), D3D11_BIND_CONSTANT_BUFFER, &vp);
    r.srv = MakeMipTexture(c.dev);
    r.smp = MakeSampler(c.dev, D3D11_FILTER_MIN_MAG_MIP_LINEAR, D3D11_TEXTURE_ADDRESS_WRAP);
    r.rs = MakeRaster(c.dev, D3D11_CULL_BACK);
    r.ds = MakeDepthState(c.dev, true, true, D3D11_COMPARISON_LESS);
    TX_REQ(kName, r.vs && r.ps && r.layout && r.vb && r.ib && r.instVb && r.cb);
    TX_REQ(kName, r.srv && r.smp && r.rs && r.ds);
    return true;
}

}  // namespace

bool RunTexturedInstanced(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    Res r;
    if (!Setup(c, r)) return false;
    c.ctx->ClearRenderTargetView(r.rt.rtv.Get(), kClear);
    c.ctx->ClearDepthStencilView(r.depth.dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    c.ctx->OMSetRenderTargets(1, r.rt.rtv.GetAddressOf(), r.depth.dsv.Get());
    c.ctx->OMSetDepthStencilState(r.ds.Get(), 0);
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->RSSetState(r.rs.Get());
    ID3D11Buffer* vbs[2] = {r.vb.Get(), r.instVb.Get()};
    const UINT strides[2] = {sizeof(VtxPNT), sizeof(Inst)}, offs[2] = {0, 0};
    c.ctx->IASetVertexBuffers(0, 2, vbs, strides, offs);
    c.ctx->IASetIndexBuffer(r.ib.Get(), DXGI_FORMAT_R16_UINT, 0);
    c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c.ctx->IASetInputLayout(r.layout.Get());
    c.ctx->VSSetShader(r.vs.Get(), nullptr, 0);
    c.ctx->VSSetConstantBuffers(0, 1, r.cb.GetAddressOf());
    c.ctx->PSSetShader(r.ps.Get(), nullptr, 0);
    c.ctx->PSSetShaderResources(0, 1, r.srv.GetAddressOf());
    c.ctx->PSSetSamplers(0, 1, r.smp.GetAddressOf());
    c.ctx->DrawIndexedInstanced(36, 64, 0, 0, 0);

    TX_REQ(kName, Readback(c, r.rt.tex.Get(), out));
    c.ctx->ClearState();
    const uint32_t covered = CountNotClear(out), total = c.width * c.height;
    TX_REQ(kName, IsClear(out, 1, 1) && IsClear(out, c.width - 2, c.height - 2));
    // 64 cubes of about 0.42 half-size on a 1.45 pitch cover between 15% and 60%.
    TX_REQ(kName, covered > total * 3 / 20 && covered < total * 3 / 5);
    return true;
}
}  // namespace txtest
