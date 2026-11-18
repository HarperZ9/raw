// Scene 6: shadow mapping. A depth-only pass from the light into a 512x512
// R32_TYPELESS texture (DSV D32_FLOAT, SRV R32_FLOAT) with depth bias and front-face
// culling, then a main pass with back-face culling that samples it through a
// comparison sampler with a 3x3 PCF. Output alpha carries the shadow factor.
#include "../scene_util.h"

namespace txtest {
namespace {
const char* kName = "shadow_map";
const UINT kShadowSize = 512;

const char* kSrc = R"(
cbuffer Frame : register(b0)
{
    row_major float4x4 viewProj;
    row_major float4x4 world;
    row_major float4x4 lightViewProj;
    float4 albedo;
    float4 lightPos;
};
struct VI { float3 pos : POSITION; float3 nrm : NORMAL; float2 uv : TEXCOORD0; };
struct VO { float4 pos : SV_Position; float4 spos : TEXCOORD0; float3 nrm : NORMAL; float3 wpos : TEXCOORD1; };
float4 vsDepth(VI i) : SV_Position
{
    return mul(mul(float4(i.pos, 1), world), lightViewProj);
}
VO vs(VI i)
{
    float4 wp = mul(float4(i.pos, 1), world);
    VO o;
    o.pos = mul(wp, viewProj);
    o.spos = mul(wp, lightViewProj);
    o.nrm = mul(float4(i.nrm, 0), world).xyz;
    o.wpos = wp.xyz;
    return o;
}
Texture2D<float> shadowMap : register(t0);
SamplerComparisonState cmp : register(s0);
float4 ps(VO i) : SV_Target
{
    float3 sc = i.spos.xyz / i.spos.w;
    float2 uv = sc.xy * float2(0.5, -0.5) + 0.5;
    float lit = 0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            lit += shadowMap.SampleCmpLevelZero(cmp, uv + float2(x, y) / 512.0, sc.z - 0.003);
    lit /= 9.0;
    float ndl = saturate(dot(normalize(i.nrm), normalize(lightPos.xyz - i.wpos)));
    return float4(albedo.rgb * (0.25 + 0.75 * ndl * lit), lit);
}
)";

struct FrameCb { Mat4 viewProj, world, lightViewProj; float albedo[4], lightPos[4]; };
struct Object { Mat4 world; float albedo[4]; };

std::vector<Object> MakeObjects()
{
    return {
        {Mul(Scale(6.0f, 0.2f, 6.0f), Translate(0, -1.2f, 0)), {0.8f, 0.8f, 0.75f, 1}},
        {Mul(RotY(0.5f), Translate(-1.0f, 0, 0)), {0.9f, 0.3f, 0.25f, 1}},
        {Mul(Mul(Scale(0.6f, 1.5f, 0.6f), RotY(-0.3f)), Translate(1.8f, 0.5f, 1.2f)), {0.25f, 0.6f, 0.9f, 1}},
        {Mul(Scale(0.5f, 0.5f, 0.5f), Translate(1.2f, -0.5f, -1.8f)), {0.95f, 0.85f, 0.3f, 1}},
    };
}

struct Res {
    Target rt;
    Depth depth;
    ComPtr<ID3D11Texture2D> shadowTex;
    ComPtr<ID3D11DepthStencilView> shadowDsv;
    ComPtr<ID3D11ShaderResourceView> shadowSrv;
    ComPtr<ID3D11VertexShader> vs, vsDepth;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vb, ib, cb;
    ComPtr<ID3D11SamplerState> cmp;
    ComPtr<ID3D11RasterizerState> rsShadow, rsMain;
    ComPtr<ID3D11DepthStencilState> ds;
    FrameCb frame{};
};

bool Setup(SceneCtx& c, Res& r)
{
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, r.rt));
    TX_REQ(kName, MakeDepth(c.dev, c.width, c.height, r.depth));
    TexDesc sd;
    sd.w = sd.h = kShadowSize; sd.fmt = DXGI_FORMAT_R32_TYPELESS;
    sd.bind = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    r.shadowTex = MakeTex(c.dev, sd);
    r.shadowDsv = MakeDSV(c.dev, r.shadowTex.Get(), DXGI_FORMAT_D32_FLOAT);
    r.shadowSrv = MakeSRV(c.dev, r.shadowTex.Get(), DXGI_FORMAT_R32_FLOAT);
    ComPtr<ID3DBlob> blob;
    r.vs = MakeVS(c.dev, kSrc, "vs", &blob);
    r.vsDepth = MakeVS(c.dev, kSrc, "vsDepth");
    r.ps = MakePS(c.dev, kSrc, "ps");
    r.layout = MakeLayout(c.dev, kPNTLayout, 3, blob.Get());
    std::vector<VtxPNT> verts;
    std::vector<uint16_t> idx;
    MakeCube(verts, idx);
    r.vb = MakeBuffer(c.dev, static_cast<UINT>(verts.size() * sizeof(VtxPNT)), D3D11_BIND_VERTEX_BUFFER, verts.data());
    r.ib = MakeBuffer(c.dev, static_cast<UINT>(idx.size() * 2), D3D11_BIND_INDEX_BUFFER, idx.data());
    r.cb = MakeBuffer(c.dev, sizeof(FrameCb), D3D11_BIND_CONSTANT_BUFFER, nullptr);
    r.cmp = MakeSampler(c.dev, D3D11_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,
                        D3D11_TEXTURE_ADDRESS_BORDER, D3D11_COMPARISON_LESS_EQUAL, 1.0f);
    r.rsShadow = MakeRaster(c.dev, D3D11_CULL_FRONT, 200, 1.5f);
    r.rsMain = MakeRaster(c.dev, D3D11_CULL_BACK);
    r.ds = MakeDepthState(c.dev, true, true, D3D11_COMPARISON_LESS);
    TX_REQ(kName, r.shadowTex && r.shadowDsv && r.shadowSrv && r.vs && r.vsDepth && r.ps);
    TX_REQ(kName, r.layout && r.vb && r.ib && r.cb && r.cmp && r.rsShadow && r.rsMain && r.ds);
    const float aspect = static_cast<float>(c.width) / static_cast<float>(c.height);
    const Vec3 light = {-5, 8, -4};
    r.frame.viewProj = Mul(LookAtLH({3, 6, -8}, {0, -0.5f, 0}, {0, 1, 0}), PerspLH(0.9f, aspect, 0.5f, 40.0f));
    r.frame.lightViewProj = Mul(LookAtLH(light, {0, 0, 0}, {0, 1, 0}), OrthoLH(14, 14, 1, 25));
    r.frame.lightPos[0] = light.x; r.frame.lightPos[1] = light.y; r.frame.lightPos[2] = light.z;
    return true;
}

void DrawObjects(SceneCtx& c, Res& r, const std::vector<Object>& objs)
{
    for (const Object& o : objs) {
        r.frame.world = o.world;
        for (int i = 0; i < 4; ++i) r.frame.albedo[i] = o.albedo[i];
        c.ctx->UpdateSubresource(r.cb.Get(), 0, nullptr, &r.frame, 0, 0);
        c.ctx->DrawIndexed(36, 0, 0);
    }
}

bool Check(const SceneCtx& c, const Image& out)
{
    // Alpha is the PCF result: 255 lit, 0 shadowed, between at shadow edges.
    uint32_t lit = 0, dark = 0, partial = 0, bright = 0;
    for (uint32_t y = 0; y < out.height; ++y)
        for (uint32_t x = 0; x < out.width; ++x) {
            if (IsClear(out, x, y)) continue;
            const uint8_t a = Px(out, x, y)[3];
            if (Px(out, x, y)[0] > 150) ++bright;
            if (a == 255) ++lit; else if (a == 0) ++dark; else ++partial;
        }
    const uint32_t total = c.width * c.height;
    TX_REQ(kName, IsClear(out, 1, 1));
    TX_REQ(kName, lit > total / 8);
    TX_REQ(kName, bright > total / 8);   // lit ground: albedo reached the pixel shader
    TX_REQ(kName, dark > total / 60);
    TX_REQ(kName, partial > total / 2000 && partial < dark + lit);
    return true;
}
}  // namespace

bool RunShadowMap(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    Res r;
    if (!Setup(c, r)) return false;
    const std::vector<Object> objs = MakeObjects();
    BindVB(c.ctx, r.vb.Get(), sizeof(VtxPNT));
    c.ctx->IASetIndexBuffer(r.ib.Get(), DXGI_FORMAT_R16_UINT, 0);
    c.ctx->IASetInputLayout(r.layout.Get());
    c.ctx->VSSetConstantBuffers(0, 1, r.cb.GetAddressOf());
    c.ctx->OMSetDepthStencilState(r.ds.Get(), 0);

    // Shadow pass.
    c.ctx->ClearDepthStencilView(r.shadowDsv.Get(), D3D11_CLEAR_DEPTH, 1.0f, 0);
    c.ctx->OMSetRenderTargets(0, nullptr, r.shadowDsv.Get());
    SetViewport(c.ctx, static_cast<float>(kShadowSize), static_cast<float>(kShadowSize));
    c.ctx->RSSetState(r.rsShadow.Get());
    c.ctx->VSSetShader(r.vsDepth.Get(), nullptr, 0);
    c.ctx->PSSetShader(nullptr, nullptr, 0);
    DrawObjects(c, r, objs);

    // Main pass.
    c.ctx->ClearRenderTargetView(r.rt.rtv.Get(), kClear);
    c.ctx->ClearDepthStencilView(r.depth.dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
    c.ctx->OMSetRenderTargets(1, r.rt.rtv.GetAddressOf(), r.depth.dsv.Get());
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->RSSetState(r.rsMain.Get());
    c.ctx->VSSetShader(r.vs.Get(), nullptr, 0);
    c.ctx->PSSetShader(r.ps.Get(), nullptr, 0);
    c.ctx->PSSetConstantBuffers(0, 1, r.cb.GetAddressOf());   // albedo and lightPos
    c.ctx->PSSetShaderResources(0, 1, r.shadowSrv.GetAddressOf());
    c.ctx->PSSetSamplers(0, 1, r.cmp.GetAddressOf());
    DrawObjects(c, r, objs);

    TX_REQ(kName, Readback(c, r.rt.tex.Get(), out));
    c.ctx->ClearState();
    return Check(c, out);
}
}  // namespace txtest
