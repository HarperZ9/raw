// Scene 13 resources: the depth texture with its three views, its copy, six colour
// targets in six formats, stencil states, the four-target blend state and shaders.
#include "gbuffer_frame.h"

namespace txtest::gbuffer {
namespace {
const char* kSrc = R"(
cbuffer P : register(b0) { float2 invSize; float2 pad; };
struct VO { float4 pos : SV_Position; float4 col : COLOR0; };
struct GOut
{
    float4 colour : SV_Target0;   // R16G16B16A16_FLOAT, above 1
    float2 motion : SV_Target1;   // R16G16_FLOAT, signed
    float4 normal : SV_Target2;   // R8G8B8A8_UNORM
    float2 mask : SV_Target3;     // R8G8_UNORM, green masked off by the blend state
};
GOut psOpaque(VO i)
{
    GOut o;
    o.colour = float4(i.col.rgb * 2.0, 1);
    o.motion = float2(i.pos.x * invSize.x - 0.5, 0.5 - i.pos.y * invSize.y);
    o.normal = float4(1.0 - i.col.rgb, 1);
    o.mask = float2(1, 1);
    return o;
}
Texture2D<float> depthTex : register(t6);
float4 psLit(float4 pos : SV_Position) : SV_Target
{
    float d = depthTex.Load(int3(pos.xy, 0));
    return float4(2.0 * d, 1.5 - d, 0.25 + d, 1);
}
Texture2D hdr : register(t0);
Texture2D motion : register(t1);
Texture2D normal : register(t2);
Texture2D mask : register(t3);
Texture2D lit : register(t4);
Texture2D late : register(t5);
SamplerState pt : register(s0);
struct FO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
float4 psComposite(FO i) : SV_Target
{
    float2 g = i.uv * float2(4, 2);
    int tile = int(g.x) + 4 * int(g.y);
    float2 uv = frac(g);
    float3 h = hdr.SampleLevel(pt, uv, 0).rgb;
    float3 l = lit.SampleLevel(pt, uv, 0).rgb;
    float4 e = late.SampleLevel(pt, uv, 0);
    float3 c = h * 0.25 + l * 0.25 + e.rgb * 0.5;                              // tile 7
    if (tile == 0) c = h * 0.5;
    if (tile == 1) c = float3(motion.SampleLevel(pt, uv, 0).rg * 0.4 + 0.4, 0);
    if (tile == 2) c = normal.SampleLevel(pt, uv, 0).rgb;
    if (tile == 3) c = float3(mask.SampleLevel(pt, uv, 0).rg, 0);
    if (tile == 4) c = l * 0.5;
    if (tile == 5) c = e.rgb * e.a;
    if (tile == 6) c = depthTex.SampleLevel(pt, uv, 0).rrr;
    return float4(c, 1);
}
)";

struct StencilSpec {
    bool depth, depthWrite;
    D3D11_COMPARISON_FUNC depthFunc, stencilFunc;
    D3D11_STENCIL_OP pass;
    UINT8 writeMask;
};

ComPtr<ID3D11DepthStencilState> MakeStencilState(ID3D11Device* dev, const StencilSpec& s)
{
    D3D11_DEPTH_STENCIL_DESC d{};
    d.DepthEnable = s.depth;
    d.DepthWriteMask = s.depthWrite ? D3D11_DEPTH_WRITE_MASK_ALL : D3D11_DEPTH_WRITE_MASK_ZERO;
    d.DepthFunc = s.depthFunc;
    d.StencilEnable = TRUE;
    d.StencilReadMask = 0xFF;
    d.StencilWriteMask = s.writeMask;
    d.FrontFace = {D3D11_STENCIL_OP_KEEP, D3D11_STENCIL_OP_KEEP, s.pass, s.stencilFunc};
    d.BackFace = d.FrontFace;
    ComPtr<ID3D11DepthStencilState> st;
    const HRESULT hr = dev->CreateDepthStencilState(&d, &st);
    if (FAILED(hr)) { Fail(kName, "CreateDepthStencilState(stencil)", hr); return nullptr; }
    return st;
}

// Four targets, no blending, target 3 writes red only.
ComPtr<ID3D11BlendState> MakeMrtBlend(ID3D11Device* dev)
{
    D3D11_BLEND_DESC bd{};
    bd.IndependentBlendEnable = TRUE;
    for (auto& rt : bd.RenderTarget) {
        rt.SrcBlend = rt.SrcBlendAlpha = D3D11_BLEND_ONE;
        rt.DestBlend = rt.DestBlendAlpha = D3D11_BLEND_ZERO;
        rt.BlendOp = rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
        rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    }
    bd.RenderTarget[3].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_RED;
    ComPtr<ID3D11BlendState> s;
    const HRESULT hr = dev->CreateBlendState(&bd, &s);
    if (FAILED(hr)) { Fail(kName, "CreateBlendState(independent)", hr); return nullptr; }
    return s;
}

bool MakeDepthViews(SceneCtx& c, Res& r)
{
    TexDesc d;
    d.w = c.width; d.h = c.height; d.fmt = DXGI_FORMAT_R24G8_TYPELESS;
    d.bind = D3D11_BIND_DEPTH_STENCIL | D3D11_BIND_SHADER_RESOURCE;
    r.depthTex = MakeTex(c.dev, d);
    r.depthCopy = MakeTex(c.dev, d);
    r.dsv = MakeDSV(c.dev, r.depthTex.Get(), DXGI_FORMAT_D24_UNORM_S8_UINT);
    r.dsvCopy = MakeDSV(c.dev, r.depthCopy.Get(), DXGI_FORMAT_D24_UNORM_S8_UINT);
    r.depthSrv = MakeSRV(c.dev, r.depthTex.Get(), DXGI_FORMAT_R24_UNORM_X8_TYPELESS);
    TX_REQ(kName, r.depthTex && r.depthCopy && r.dsv && r.dsvCopy && r.depthSrv);
    D3D11_DEPTH_STENCIL_VIEW_DESC ro{};
    ro.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
    ro.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    ro.Flags = D3D11_DSV_READ_ONLY_DEPTH | D3D11_DSV_READ_ONLY_STENCIL;
    TX_HR(kName, c.dev->CreateDepthStencilView(r.depthTex.Get(), &ro, &r.dsvReadOnly));
    return true;
}

bool MakeGeometry(SceneCtx& c, Res& r)
{
    // Coordinates are 256ths of the target. B is nearest and carries stencil 3.
    const float w = 256, h = 256;
    std::vector<VtxPC> v;
    PushQuadPx(v, w, h, 96, 96, 224, 224, 0.25f, {0.2f, 0.8f, 0.4f, 1});   // B
    PushQuadPx(v, w, h, 16, 16, 144, 144, 0.6f, {1.0f, 0.2f, 0.2f, 1});    // A
    PushQuadPx(v, w, h, 24, 168, 88, 240, 0.8f, {0.2f, 0.4f, 1.0f, 1});    // C
    const Rgba dc = {0.8f, 0.8f, 0.2f, 1};                                 // D: a depth ramp
    const VtxPC tl = VertPx(w, h, 232, 136, 0.3f, dc), tr = VertPx(w, h, 252, 136, 0.3f, dc);
    const VtxPC br = VertPx(w, h, 252, 248, 0.9f, dc), bl = VertPx(w, h, 232, 248, 0.9f, dc);
    for (const VtxPC& p : {tl, tr, br, tl, br, bl}) v.push_back(p);
    PushQuadPx(v, w, h, 128, 8, 248, 128, 0.4f, {0.2f, 0.6f, 0.8f, 1});    // E: the late object
    r.vb = MakeBuffer(c.dev, static_cast<UINT>(v.size() * sizeof(VtxPC)), D3D11_BIND_VERTEX_BUFFER, v.data());
    const float p[4] = {1.0f / static_cast<float>(c.width), 1.0f / static_cast<float>(c.height), 0, 0};
    r.cb = MakeBuffer(c.dev, sizeof(p), D3D11_BIND_CONSTANT_BUFFER, p);
    TX_REQ(kName, r.vb && r.cb);
    return true;
}
}  // namespace

bool Setup(SceneCtx& c, Res& r)
{
    const UINT w = c.width, h = c.height;
    TX_REQ(kName, MakeTarget(c.dev, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, r.out));
    TX_REQ(kName, MakeTarget(c.dev, w, h, DXGI_FORMAT_R16G16B16A16_FLOAT, r.hdr));
    TX_REQ(kName, MakeTarget(c.dev, w, h, DXGI_FORMAT_R16G16_FLOAT, r.motion));
    TX_REQ(kName, MakeTarget(c.dev, w, h, DXGI_FORMAT_R8G8B8A8_UNORM, r.normal));
    TX_REQ(kName, MakeTarget(c.dev, w, h, DXGI_FORMAT_R8G8_UNORM, r.mask));
    TX_REQ(kName, MakeTarget(c.dev, w, h, DXGI_FORMAT_R11G11B10_FLOAT, r.lit));
    TX_REQ(kName, MakeTarget(c.dev, w, h, DXGI_FORMAT_R10G10B10A2_UNORM, r.late));
    if (!MakeDepthViews(c, r) || !MakeGeometry(c, r)) return false;
    TX_REQ(kName, MakeColorPipe(c.dev, r.pipe));
    r.fsVs = MakeVS(c.dev, kFullscreenVS, "vs");
    r.psOpaque = MakePS(c.dev, kSrc, "psOpaque");
    r.psLit = MakePS(c.dev, kSrc, "psLit");
    r.psComposite = MakePS(c.dev, kSrc, "psComposite");
    r.rs = MakeRaster(c.dev, D3D11_CULL_NONE);
    r.dsPrepass = MakeStencilState(c.dev, {true, true, D3D11_COMPARISON_LESS, D3D11_COMPARISON_ALWAYS,
                                           D3D11_STENCIL_OP_REPLACE, 0xFF});
    r.dsOpaque = MakeDepthState(c.dev, true, false, D3D11_COMPARISON_EQUAL);
    r.dsLit = MakeStencilState(c.dev, {false, false, D3D11_COMPARISON_ALWAYS, D3D11_COMPARISON_EQUAL,
                                       D3D11_STENCIL_OP_KEEP, 0});
    r.dsLate = MakeDepthState(c.dev, true, true, D3D11_COMPARISON_LESS);
    r.blendMrt = MakeMrtBlend(c.dev);
    r.point = MakeSampler(c.dev, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    TX_REQ(kName, r.fsVs && r.psOpaque && r.psLit && r.psComposite && r.rs && r.point);
    TX_REQ(kName, r.dsPrepass && r.dsOpaque && r.dsLit && r.dsLate && r.blendMrt);
    return true;
}

}  // namespace txtest::gbuffer
