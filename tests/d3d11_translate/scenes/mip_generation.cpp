// Scene 11: GenerateMips. A 128x128 texture with a full mip chain is filled at mip 0
// with UpdateSubresource (8x8 blocks of 0/255 channels, so mips 1 to 3 are exact
// under any box or bilinear filter), then every mip is shown side by side with
// SampleLevel and a point sampler. A second texture is rendered into at mip 0 and
// its generated mip 3 is shown magnified in the lower-left quarter.
#include "../scene_util.h"
#include <cstdlib>

namespace txtest {
namespace {
const char* kName = "mip_generation";
const UINT kSize = 128, kMips = 8, kBlocks = 16;   // 16x16 blocks of 8x8 texels

const char* kPsSrc = R"(
struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
Texture2D tex : register(t0);
SamplerState smp : register(s0);
cbuffer P : register(b0) { float lod; float3 pad; };
float4 ps(VO i) : SV_Target { return float4(tex.SampleLevel(smp, i.uv, lod).rgb, 1); }
)";

struct MipTex {
    ComPtr<ID3D11Texture2D> tex;
    ComPtr<ID3D11RenderTargetView> rtv;   // mip 0
    ComPtr<ID3D11ShaderResourceView> srv; // every mip
};

struct Res {
    Target out;
    MipTex a, b;
    ColorPipe pipe;
    ComPtr<ID3D11VertexShader> fsVs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> cb, vb;
    ComPtr<ID3D11SamplerState> point;
    ComPtr<ID3D11RasterizerState> rs;
    std::vector<uint8_t> blocks;   // kBlocks*kBlocks entries, bit 0/1/2 = r/g/b
};

bool MakeMipTex(ID3D11Device* dev, MipTex& t)
{
    TexDesc d;
    d.w = d.h = kSize; d.mips = kMips;
    d.bind = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    d.misc = D3D11_RESOURCE_MISC_GENERATE_MIPS;
    t.tex = MakeTex(dev, d);
    t.rtv = MakeRTV(dev, t.tex.Get());
    t.srv = MakeSRV(dev, t.tex.Get());
    return t.tex && t.rtv && t.srv;
}

bool Setup(SceneCtx& c, Res& r)
{
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, r.out));
    TX_REQ(kName, MakeMipTex(c.dev, r.a) && MakeMipTex(c.dev, r.b));
    TX_REQ(kName, MakeColorPipe(c.dev, r.pipe));
    r.fsVs = MakeVS(c.dev, kFullscreenVS, "vs");
    r.ps = MakePS(c.dev, kPsSrc, "ps");
    r.cb = MakeBuffer(c.dev, 16, D3D11_BIND_CONSTANT_BUFFER, nullptr);
    r.point = MakeSampler(c.dev, D3D11_FILTER_MIN_MAG_MIP_POINT, D3D11_TEXTURE_ADDRESS_CLAMP);
    r.rs = MakeRaster(c.dev, D3D11_CULL_NONE);
    const float s = static_cast<float>(kSize);
    std::vector<VtxPC> v;   // block-aligned rectangles for the second texture
    PushQuadPx(v, s, s, 16, 16, 64, 48, 0.5f, {1, 0, 0, 1});
    PushQuadPx(v, s, s, 72, 80, 120, 120, 0.5f, {1, 1, 0, 1});
    r.vb = MakeBuffer(c.dev, static_cast<UINT>(v.size() * sizeof(VtxPC)), D3D11_BIND_VERTEX_BUFFER, v.data());
    TX_REQ(kName, r.fsVs && r.ps && r.cb && r.point && r.rs && r.vb);
    Rng rng(0x0B10C511u);
    r.blocks.resize(kBlocks * kBlocks);
    for (uint8_t& b : r.blocks) b = static_cast<uint8_t>((rng.next() >> 9) & 7u);
    return true;
}

void FillSources(SceneCtx& c, const Res& r)
{
    std::vector<uint32_t> px(kSize * kSize);
    for (UINT y = 0; y < kSize; ++y)
        for (UINT x = 0; x < kSize; ++x) {
            const uint8_t b = r.blocks[(y / 8) * kBlocks + x / 8];
            px[y * kSize + x] = 0xFF000000u | ((b & 1) ? 0xFFu : 0u) | ((b & 2) ? 0xFF00u : 0u) |
                                ((b & 4) ? 0xFF0000u : 0u);
        }
    c.ctx->UpdateSubresource(r.a.tex.Get(), 0, nullptr, px.data(), kSize * 4, 0);

    const float blue[4] = {0, 0, 1, 1};
    c.ctx->ClearRenderTargetView(r.b.rtv.Get(), blue);
    c.ctx->OMSetRenderTargets(1, r.b.rtv.GetAddressOf(), nullptr);
    SetViewport(c.ctx, static_cast<float>(kSize), static_cast<float>(kSize));
    c.ctx->RSSetState(r.rs.Get());
    BindColorPipe(c.ctx, r.pipe);
    BindVB(c.ctx, r.vb.Get(), sizeof(VtxPC));
    c.ctx->Draw(12, 0);
    c.ctx->OMSetRenderTargets(0, nullptr, nullptr);   // not a render target while sampled
    c.ctx->GenerateMips(r.a.srv.Get());
    c.ctx->GenerateMips(r.b.srv.Get());
}

// Left edge, in 256ths of the output, of the strip cell that shows mip m.
UINT CellX(UINT m) { return 256 - (256 >> m); }

void ShowMip(SceneCtx& c, const Res& r, ID3D11ShaderResourceView* srv, UINT mip, float x, float y,
             float size)
{
    const float sx = static_cast<float>(c.width) / 256.0f, sy = static_cast<float>(c.height) / 256.0f;
    const float p[4] = {static_cast<float>(mip), 0, 0, 0};
    c.ctx->UpdateSubresource(r.cb.Get(), 0, nullptr, p, 0, 0);
    SetViewport(c.ctx, size * sx, size * sy, x * sx, y * sy);
    c.ctx->PSSetShaderResources(0, 1, &srv);
    c.ctx->Draw(3, 0);
}

bool CheckBlock(const SceneCtx& c, const Image& out, float x256, float y256, uint8_t bits)
{
    const uint32_t x = static_cast<uint32_t>(x256 * static_cast<float>(c.width) / 256.0f);
    const uint32_t y = static_cast<uint32_t>(y256 * static_cast<float>(c.height) / 256.0f);
    return PxNear(out, x, y, (bits & 1) ? 255 : 0, (bits & 2) ? 255 : 0, (bits & 4) ? 255 : 0, 1);
}

bool Check(const SceneCtx& c, const Res& r, const Image& out)
{
    int sum[3] = {0, 0, 0};
    for (UINT by = 0; by < kBlocks; ++by)
        for (UINT bx = 0; bx < kBlocks; ++bx) {
            const uint8_t bits = r.blocks[by * kBlocks + bx];
            for (int ch = 0; ch < 3; ++ch) sum[ch] += ((bits >> ch) & 1) ? 255 : 0;
            for (UINT m = 0; m < 4; ++m) {   // mips 0 to 3 keep whole blocks
                const float cell = static_cast<float>(kSize >> m) / static_cast<float>(kBlocks);
                const float fx = static_cast<float>(bx) + 0.5f, fy = static_cast<float>(by) + 0.5f;
                TX_REQ(kName, CheckBlock(c, out, static_cast<float>(CellX(m)) + fx * cell, fy * cell, bits));
            }
            // Second texture, mip 3: red and yellow rectangles on blue.
            uint8_t want = 4;
            if (bx >= 2 && bx < 8 && by >= 2 && by < 6) want = 1;
            if (bx >= 9 && bx < 15 && by >= 10 && by < 15) want = 3;
            TX_REQ(kName, CheckBlock(c, out, static_cast<float>(bx) * 8 + 4, 128 + static_cast<float>(by) * 8 + 4, want));
        }
    // The 1x1 mip holds the mean of the whole pattern.
    const uint8_t* last = Px(out, CellX(7) * c.width / 256, 0);
    for (int ch = 0; ch < 3; ++ch)
        TX_REQ(kName, std::abs(last[ch] - sum[ch] / static_cast<int>(kBlocks * kBlocks)) <= 12);
    TX_REQ(kName, IsClear(out, c.width - 3, c.height - 3));
    return true;
}
}  // namespace

bool RunMipGeneration(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    Res r;
    if (!Setup(c, r)) return false;
    FillSources(c, r);

    c.ctx->ClearRenderTargetView(r.out.rtv.Get(), kClear);
    c.ctx->OMSetRenderTargets(1, r.out.rtv.GetAddressOf(), nullptr);
    c.ctx->IASetInputLayout(nullptr);
    c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c.ctx->VSSetShader(r.fsVs.Get(), nullptr, 0);
    c.ctx->PSSetShader(r.ps.Get(), nullptr, 0);
    c.ctx->PSSetConstantBuffers(0, 1, r.cb.GetAddressOf());
    c.ctx->PSSetSamplers(0, 1, r.point.GetAddressOf());
    for (UINT m = 0; m < kMips; ++m)
        ShowMip(c, r, r.a.srv.Get(), m, static_cast<float>(CellX(m)), 0, static_cast<float>(kSize >> m));
    ShowMip(c, r, r.b.srv.Get(), 3, 0, 128, 128);

    TX_REQ(kName, Readback(c, r.out.tex.Get(), out));
    c.ctx->ClearState();
    return Check(c, r, out);
}
}  // namespace txtest
