// Scene 5: render to an R16G16B16A16_FLOAT target, a two-pass separable blur that
// ping-pongs through a second float target, then a tonemap to RGBA8. Full-screen
// passes use an SV_VertexID triangle with no vertex buffer and a null input layout.
// The tonemap pass uses an inset viewport and a scissor rectangle.
#include "../scene_util.h"

namespace txtest {
namespace {
const char* kName = "rtt_post";
const UINT kInset = 16, kCut = 48;   // viewport inset; scissor cuts this much off right/bottom

const char* kPostSrc = R"(
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
Texture2D src : register(t0);
SamplerState smp : register(s0);
cbuffer Blur : register(b0) { float2 dir; float2 pad; };
float4 blur(VO i) : SV_Target
{
    static const float w[3] = {0.375, 0.25, 0.0625};
    float4 c = src.Sample(smp, i.uv) * w[0];
    for (int k = 1; k < 3; ++k)
        c += (src.Sample(smp, i.uv + dir * k) + src.Sample(smp, i.uv - dir * k)) * w[k];
    return c;
}
float4 tonemap(VO i) : SV_Target
{
    float3 c = src.Sample(smp, i.uv).rgb;
    c = c / (1.0 + c);
    return float4(sqrt(c), 1);
}
)";

struct Res {
    Target out, hdrA, hdrB;
    ColorPipe pipe;
    ComPtr<ID3D11VertexShader> fsVs;
    ComPtr<ID3D11PixelShader> blurPs, tonePs;
    ComPtr<ID3D11Buffer> vb, blurCb;
    ComPtr<ID3D11SamplerState> point, linear;
    ComPtr<ID3D11RasterizerState> rs, rsScissor;
    UINT vertCount = 0;
};

bool Setup(SceneCtx& c, Res& r)
{
    const DXGI_FORMAT hdr = DXGI_FORMAT_R16G16B16A16_FLOAT;
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, r.out));
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, hdr, r.hdrA));
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, hdr, r.hdrB));
    TX_REQ(kName, MakeColorPipe(c.dev, r.pipe));
    r.fsVs = MakeVS(c.dev, kFullscreenVS, "vs");
    r.blurPs = MakePS(c.dev, kPostSrc, "blur");
    r.tonePs = MakePS(c.dev, kPostSrc, "tonemap");
    const float w = static_cast<float>(c.width), h = static_cast<float>(c.height);
    const float sx = w / 256.0f, sy = h / 256.0f;
    std::vector<VtxPC> v;
    PushQuadPx(v, w, h, 100 * sx, 100 * sy, 156 * sx, 156 * sy, 0.5f, {8, 8, 8, 1});
    PushQuadPx(v, w, h, 24 * sx, 30 * sy, 90 * sx, 70 * sy, 0.5f, {4, 1, 0.2f, 1});
    PushQuadPx(v, w, h, 150 * sx, 180 * sy, 230 * sx, 200 * sy, 0.5f, {0.2f, 3, 5, 1});
    PushQuadPx(v, w, h, 40 * sx, 150 * sy, 60 * sx, 230 * sy, 0.5f, {0.6f, 0.1f, 0.9f, 1});
    r.vertCount = static_cast<UINT>(v.size());
    r.vb = MakeBuffer(c.dev, r.vertCount * sizeof(VtxPC), D3D11_BIND_VERTEX_BUFFER, v.data());
    r.blurCb = MakeBuffer(c.dev, 16, D3D11_BIND_CONSTANT_BUFFER, nullptr);
    r.point = MakeSampler(c.dev, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    r.linear = MakeSampler(c.dev, D3D11_FILTER_MIN_MAG_LINEAR_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    r.rs = MakeRaster(c.dev, D3D11_CULL_NONE);
    r.rsScissor = MakeRaster(c.dev, D3D11_CULL_NONE, 0, 0.0f, true);
    TX_REQ(kName, r.fsVs && r.blurPs && r.tonePs && r.vb && r.blurCb);
    TX_REQ(kName, r.point && r.linear && r.rs && r.rsScissor);
    return true;
}

// One full-screen pass: src -> dst. The source is unbound afterwards so the next
// pass can render into it.
void FullscreenPass(SceneCtx& c, const Target& dst, const Target& src, ID3D11PixelShader* ps,
                    ID3D11SamplerState* smp)
{
    ID3D11ShaderResourceView* none = nullptr;
    c.ctx->OMSetRenderTargets(1, dst.rtv.GetAddressOf(), nullptr);
    c.ctx->PSSetShader(ps, nullptr, 0);
    c.ctx->PSSetShaderResources(0, 1, src.srv.GetAddressOf());
    c.ctx->PSSetSamplers(0, 1, &smp);
    c.ctx->Draw(3, 0);
    c.ctx->PSSetShaderResources(0, 1, &none);
}

void BlurPasses(SceneCtx& c, const Res& r)
{
    const float w = static_cast<float>(c.width), h = static_cast<float>(c.height);
    c.ctx->IASetInputLayout(nullptr);
    c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c.ctx->VSSetShader(r.fsVs.Get(), nullptr, 0);
    c.ctx->PSSetConstantBuffers(0, 1, r.blurCb.GetAddressOf());
    const float dirH[4] = {1.5f / w, 0, 0, 0}, dirV[4] = {0, 1.5f / h, 0, 0};
    c.ctx->UpdateSubresource(r.blurCb.Get(), 0, nullptr, dirH, 0, 0);
    FullscreenPass(c, r.hdrB, r.hdrA, r.blurPs.Get(), r.linear.Get());
    c.ctx->UpdateSubresource(r.blurCb.Get(), 0, nullptr, dirV, 0, 0);
    FullscreenPass(c, r.hdrA, r.hdrB, r.blurPs.Get(), r.linear.Get());
}

bool Check(const SceneCtx& c, const Image& out)
{
    TX_REQ(kName, IsClear(out, 4, 4));                                // outside the viewport
    TX_REQ(kName, IsClear(out, c.width - 20, c.height - 20));         // inside it, scissored
    TX_REQ(kName, !IsClear(out, c.width / 2, c.height / 2));
    const uint8_t* mid = Px(out, c.width / 2, c.height / 2);
    TX_REQ(kName, mid[0] > 220 && mid[1] > 220 && mid[2] > 220);      // 8.0 tonemapped
    // Blur reach: output pixel about 2 source texels right of the white quad's edge.
    const float scale = static_cast<float>(c.width - 2 * kInset) / static_cast<float>(c.width);
    const float edge = 156.0f * static_cast<float>(c.width) / 256.0f;
    const uint32_t x = kInset + static_cast<uint32_t>((edge + 2.0f) * scale);
    TX_REQ(kName, Px(out, x, c.height / 2)[0] > 80);
    // Far from every quad the background stays dark (0.02 tonemapped is about 36).
    const uint8_t* bg = Px(out, c.width * 120 / 256, c.height * 30 / 256);
    TX_REQ(kName, bg[0] < 60 && bg[0] > 20);
    return true;
}
}  // namespace

bool RunRttPost(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    Res r;
    if (!Setup(c, r)) return false;
    const float w = static_cast<float>(c.width), h = static_cast<float>(c.height);
    const float hdrClear[4] = {0.02f, 0.02f, 0.05f, 1.0f};
    c.ctx->ClearRenderTargetView(r.hdrA.rtv.Get(), hdrClear);
    c.ctx->ClearRenderTargetView(r.out.rtv.Get(), kClear);
    c.ctx->OMSetRenderTargets(1, r.hdrA.rtv.GetAddressOf(), nullptr);
    SetViewport(c.ctx, w, h);
    c.ctx->RSSetState(r.rs.Get());
    BindColorPipe(c.ctx, r.pipe);
    BindVB(c.ctx, r.vb.Get(), sizeof(VtxPC));
    c.ctx->Draw(r.vertCount, 0);

    BlurPasses(c, r);

    const float in = static_cast<float>(kInset);
    SetViewport(c.ctx, w - 2 * in, h - 2 * in, in, in);
    const D3D11_RECT sc = {0, 0, static_cast<LONG>(c.width - kCut), static_cast<LONG>(c.height - kCut)};
    c.ctx->RSSetScissorRects(1, &sc);
    c.ctx->RSSetState(r.rsScissor.Get());
    FullscreenPass(c, r.out, r.hdrA, r.tonePs.Get(), r.point.Get());

    TX_REQ(kName, Readback(c, r.out.tex.Get(), out));
    c.ctx->ClearState();
    return Check(c, out);
}
}  // namespace txtest
