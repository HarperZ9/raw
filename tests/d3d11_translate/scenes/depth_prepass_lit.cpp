// Scene 3: a depth-only pre-pass (null pixel shader, no render target), then a lit
// pass with depth func EQUAL and depth writes off. Two constant buffers: per-object
// in VS b0, light in PS b1.
#include "../scene_util.h"

namespace txtest {
namespace {
const char* kName = "depth_prepass_lit";

const char* kSrc = R"(
cbuffer Frame : register(b0)
{
    row_major float4x4 viewProj;
    row_major float4x4 world;
    float4 albedo;
};
cbuffer Light : register(b1)
{
    float4 lightDir;     // direction the light travels
    float4 lightColor;
    float4 eyePos;
    float4 ambient;
};
struct VI { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct VO { float4 pos : SV_Position; float3 wpos : TEXCOORD0; float3 nrm : NORMAL; float4 albedo : COLOR0; };
VO vs(VI i)
{
    float4 wp = mul(float4(i.pos, 1), world);
    VO o;
    o.pos = mul(wp, viewProj);
    o.wpos = wp.xyz;
    o.nrm = mul(float4(i.nrm, 0), world).xyz;
    o.albedo = albedo;
    return o;
}
float4 ps(VO i) : SV_Target
{
    float3 n = normalize(i.nrm);
    float3 l = normalize(-lightDir.xyz);
    float3 v = normalize(eyePos.xyz - i.wpos);
    float3 h = normalize(l + v);
    float diff = saturate(dot(n, l));
    float spec = pow(saturate(dot(n, h)), 32.0) * 0.5 * step(0.0001, diff);
    float3 col = i.albedo.rgb * (ambient.rgb + diff * lightColor.rgb) + spec * lightColor.rgb;
    return float4(saturate(col), 1);
}
)";

struct FrameCb { Mat4 viewProj; Mat4 world; float albedo[4]; };
struct LightCb { float dir[4], color[4], eye[4], ambient[4]; };
struct Object { Mat4 world; float albedo[4]; };

std::vector<Object> MakeObjects()
{
    return {
        {Mul(Scale(5.0f, 0.2f, 5.0f), Translate(0, -1.2f, 0)), {0.55f, 0.55f, 0.5f, 1}},
        {Mul(RotY(0.4f), Translate(-1.2f, 0, 0.5f)), {0.9f, 0.25f, 0.2f, 1}},
        {Mul(Mul(Scale(0.8f, 1.6f, 0.8f), RotY(-0.7f)), Translate(0.6f, 0.6f, 1.6f)), {0.2f, 0.7f, 0.9f, 1}},
        {Mul(Mul(Scale(0.6f, 0.6f, 0.6f), RotX(0.6f)), Translate(1.4f, -0.4f, -1.0f)), {0.95f, 0.8f, 0.2f, 1}},
        {Mul(Mul(Scale(1.4f, 0.3f, 0.5f), RotY(1.1f)), Translate(-0.2f, 1.1f, 0.2f)), {0.4f, 0.9f, 0.35f, 1}},
        {Mul(Scale(0.5f, 0.9f, 0.5f), Translate(-2.6f, -0.1f, -1.6f)), {0.8f, 0.4f, 0.9f, 1}},
    };
}

struct Res {
    Target rt;
    Depth depth;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vb, ib, frameCb, lightCb;
    ComPtr<ID3D11RasterizerState> rs;
    ComPtr<ID3D11DepthStencilState> dsWrite, dsEqual;
    Mat4 viewProj;
};

bool Setup(SceneCtx& c, Res& r)
{
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, r.rt));
    TX_REQ(kName, MakeDepth(c.dev, c.width, c.height, r.depth));
    ComPtr<ID3DBlob> blob;
    r.vs = MakeVS(c.dev, kSrc, "vs", &blob);
    r.ps = MakePS(c.dev, kSrc, "ps");
    r.layout = MakeLayout(c.dev, kPNTLayout, 3, blob.Get());
    std::vector<VtxPNT> verts;
    std::vector<uint16_t> idx;
    MakeCube(verts, idx);
    r.vb = MakeBuffer(c.dev, static_cast<UINT>(verts.size() * sizeof(VtxPNT)), D3D11_BIND_VERTEX_BUFFER, verts.data());
    r.ib = MakeBuffer(c.dev, static_cast<UINT>(idx.size() * 2), D3D11_BIND_INDEX_BUFFER, idx.data());
    r.frameCb = MakeBuffer(c.dev, sizeof(FrameCb), D3D11_BIND_CONSTANT_BUFFER, nullptr);
    const LightCb light = {{0.4f, -0.8f, 0.45f, 0}, {1.0f, 0.95f, 0.85f, 0}, {4, 4, -7, 1}, {0.18f, 0.2f, 0.25f, 0}};
    r.lightCb = MakeBuffer(c.dev, sizeof(LightCb), D3D11_BIND_CONSTANT_BUFFER, &light);
    r.rs = MakeRaster(c.dev, D3D11_CULL_BACK);
    r.dsWrite = MakeDepthState(c.dev, true, true, D3D11_COMPARISON_LESS);
    r.dsEqual = MakeDepthState(c.dev, true, false, D3D11_COMPARISON_EQUAL);
    const float aspect = static_cast<float>(c.width) / static_cast<float>(c.height);
    r.viewProj = Mul(LookAtLH({4, 4, -7}, {0, 0, 0}, {0, 1, 0}), PerspLH(0.9f, aspect, 0.5f, 40.0f));
    TX_REQ(kName, r.vs && r.ps && r.layout && r.vb && r.ib && r.frameCb && r.lightCb);
    TX_REQ(kName, r.rs && r.dsWrite && r.dsEqual);
    return true;
}

void DrawObjects(SceneCtx& c, const Res& r, const std::vector<Object>& objs)
{
    for (const Object& o : objs) {
        FrameCb f;
        f.viewProj = r.viewProj;
        f.world = o.world;
        for (int i = 0; i < 4; ++i) f.albedo[i] = o.albedo[i];
        c.ctx->UpdateSubresource(r.frameCb.Get(), 0, nullptr, &f, 0, 0);
        c.ctx->DrawIndexed(36, 0, 0);
    }
}
}  // namespace

bool RunDepthPrepassLit(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    Res r;
    if (!Setup(c, r)) return false;
    const std::vector<Object> objs = MakeObjects();
    c.ctx->ClearRenderTargetView(r.rt.rtv.Get(), kClear);
    c.ctx->ClearDepthStencilView(r.depth.dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->RSSetState(r.rs.Get());
    BindVB(c.ctx, r.vb.Get(), sizeof(VtxPNT));
    c.ctx->IASetIndexBuffer(r.ib.Get(), DXGI_FORMAT_R16_UINT, 0);
    c.ctx->IASetInputLayout(r.layout.Get());
    c.ctx->VSSetShader(r.vs.Get(), nullptr, 0);
    c.ctx->VSSetConstantBuffers(0, 1, r.frameCb.GetAddressOf());

    // Pass 1: depth only.
    c.ctx->PSSetShader(nullptr, nullptr, 0);
    c.ctx->OMSetRenderTargets(0, nullptr, r.depth.dsv.Get());
    c.ctx->OMSetDepthStencilState(r.dsWrite.Get(), 0);
    DrawObjects(c, r, objs);

    // Pass 2: lit, depth EQUAL, no depth writes.
    c.ctx->PSSetShader(r.ps.Get(), nullptr, 0);
    c.ctx->PSSetConstantBuffers(1, 1, r.lightCb.GetAddressOf());
    c.ctx->OMSetRenderTargets(1, r.rt.rtv.GetAddressOf(), r.depth.dsv.Get());
    c.ctx->OMSetDepthStencilState(r.dsEqual.Get(), 0);
    DrawObjects(c, r, objs);

    TX_REQ(kName, Readback(c, r.rt.tex.Get(), out));
    c.ctx->ClearState();
    const uint32_t covered = CountNotClear(out), total = c.width * c.height;
    TX_REQ(kName, IsClear(out, 1, 1));
    TX_REQ(kName, !IsClear(out, c.width / 2, c.height / 2));
    // Every pre-pass pixel must pass the EQUAL test: the ground alone covers over 30%.
    TX_REQ(kName, covered > total * 3 / 10);
    return true;
}
}  // namespace txtest
