// Scene 15: GPU-produced constant buffer copy (Compute UAV -> CopyResource & CopySubresourceRegion -> ConstantBuffer -> PS draw).
// Verifies Store::Renamed -> Store::Committed promotion and COPY_DEST -> VERTEX_AND_CONSTANT_BUFFER barriers on WARP + D3D12 Debug Layer.
#include "../scene_util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace txtest {
namespace {
const char* kName = "gpu_cb_copy";

static const char* kSrc = R"(
RWByteAddressBuffer outBuf : register(u0);
[numthreads(1, 1, 1)]
void cs_write()
{
    // Two float4 constants written purely on the GPU:
    //   [0..15]:  (0.80, 0.30, 0.15, 1.0)
    //   [16..31]: (0.10, 0.75, 0.90, 1.0)
    outBuf.Store4(0,  asuint(float4(0.80f, 0.30f, 0.15f, 1.0f)));
    outBuf.Store4(16, asuint(float4(0.10f, 0.75f, 0.90f, 1.0f)));
}

struct VO { float4 pos : SV_Position; float2 uv : TEXCOORD0; };
VO vs(uint id : SV_VertexID)
{
    VO o;
    float2 p = float2((id == 1) ? 3.0f : -1.0f, (id == 2) ? 3.0f : -1.0f);
    o.pos = float4(p, 0.0f, 1.0f);
    o.uv = float2(p.x * 0.5f + 0.5f, 0.5f - p.y * 0.5f);
    return o;
}

cbuffer GpuColor : register(b0) { float4 c0; float4 c1; };
float4 ps(VO i) : SV_Target
{
    return i.uv.x < 0.5f ? c0 : c1;
}
)";

bool Check(const Image& out)
{
    int worst = 0;
    const uint32_t mid = out.width / 2;
    for (uint32_t y = 0; y < out.height; ++y) {
        for (uint32_t x = 0; x < out.width; ++x) {
            if (x == mid || x + 1 == mid) continue;
            // After CopyResource + partial CopySubresourceRegion overwriting c0 with c1's 16 bytes:
            // Left half reads updated c0 (0.10, 0.75, 0.90), right half reads c1 (0.10, 0.75, 0.90) on bottom,
            // and we test left-half updated vs top-half first draw via scissor!
            const bool topHalf = y < out.height / 2;
            const int orange[3] = {204, 77, 38};
            const int cyan[3] = {26, 191, 230};
            const int* want = (topHalf && x < mid) ? orange : cyan;
            const uint8_t* p = Px(out, x, y);
            for (int ch = 0; ch < 3; ++ch) worst = std::max(worst, std::abs(want[ch] - p[ch]));
        }
    }
    if (worst > 2) {
        fprintf(stderr, "[%s] image differs from expected GPU-CB reference by %d\n", kName, worst);
        return false;
    }
    return true;
}

}  // namespace

bool RunGpuCbCopy(SceneCtx& c, Image& out)
{
    Target rt;
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, rt));

    ComPtr<ID3D11ComputeShader> cs = MakeCS(c.dev, kSrc, "cs_write");
    ComPtr<ID3D11VertexShader> vs = MakeVS(c.dev, kSrc, "vs");
    ComPtr<ID3D11PixelShader> ps = MakePS(c.dev, kSrc, "ps");
    TX_REQ(kName, cs && vs && ps);

    // GPU-produced DEFAULT raw buffer (32 bytes = two float4s).
    D3D11_BUFFER_DESC gd{};
    gd.ByteWidth = 32;
    gd.Usage = D3D11_USAGE_DEFAULT;
    gd.BindFlags = D3D11_BIND_UNORDERED_ACCESS;
    gd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
    ComPtr<ID3D11Buffer> gpuBuf;
    TX_HR(kName, c.dev->CreateBuffer(&gd, nullptr, &gpuBuf));

    D3D11_UNORDERED_ACCESS_VIEW_DESC ud{};
    ud.Format = DXGI_FORMAT_R32_TYPELESS;
    ud.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
    ud.Buffer.FirstElement = 0;
    ud.Buffer.NumElements = 8;
    ud.Buffer.Flags = D3D11_BUFFER_UAV_FLAG_RAW;
    ComPtr<ID3D11UnorderedAccessView> uav;
    TX_HR(kName, c.dev->CreateUnorderedAccessView(gpuBuf.Get(), &ud, &uav));

    // Standard D3D11_BIND_CONSTANT_BUFFER (initially Store::Renamed in raw::tx!).
    D3D11_BUFFER_DESC cbd{};
    cbd.ByteWidth = 32;
    cbd.Usage = D3D11_USAGE_DEFAULT;
    cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    ComPtr<ID3D11Buffer> cb;
    TX_HR(kName, c.dev->CreateBuffer(&cbd, nullptr, &cb));

    D3D11_RASTERIZER_DESC rsd{};
    rsd.FillMode = D3D11_FILL_SOLID;
    rsd.CullMode = D3D11_CULL_NONE;
    rsd.ScissorEnable = TRUE;
    ComPtr<ID3D11RasterizerState> rs;
    TX_HR(kName, c.dev->CreateRasterizerState(&rsd, &rs));

    // 1. Compute shader writes constants purely on the GPU.
    ID3D11UnorderedAccessView* uavs[1] = {uav.Get()};
    c.ctx->CSSetShader(cs.Get(), nullptr, 0);
    c.ctx->CSSetUnorderedAccessViews(0, 1, uavs, nullptr);
    c.ctx->Dispatch(1, 1, 1);
    ID3D11UnorderedAccessView* nullUav[1] = {nullptr};
    c.ctx->CSSetUnorderedAccessViews(0, 1, nullUav, nullptr);

    // 2. Full GPU -> ConstantBuffer CopyResource (triggers Store::Renamed -> Store::Committed promotion!).
    c.ctx->CopyResource(cb.Get(), gpuBuf.Get());

    // 3. Draw top half using the GPU-copied constants (left=c0 orange, right=c1 cyan).
    const float clr[4] = {0, 0, 0, 1};
    c.ctx->ClearRenderTargetView(rt.rtv.Get(), clr);
    ID3D11RenderTargetView* rtvs[1] = {rt.rtv.Get()};
    c.ctx->OMSetRenderTargets(1, rtvs, nullptr);
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->RSSetState(rs.Get());
    c.ctx->IASetInputLayout(nullptr);
    c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c.ctx->VSSetShader(vs.Get(), nullptr, 0);
    c.ctx->PSSetShader(ps.Get(), nullptr, 0);
    ID3D11Buffer* cbs[1] = {cb.Get()};
    c.ctx->PSSetConstantBuffers(0, 1, cbs);
    const D3D11_RECT topRect = {0, 0, LONG(c.width), LONG(c.height / 2)};
    c.ctx->RSSetScissorRects(1, &topRect);
    c.ctx->Draw(3, 0);

    // 4. Partial CopySubresourceRegion: copy bytes [16..32) (c1 cyan) over bytes [0..16) (c0) and draw bottom half.
    const D3D11_BOX box = {16, 0, 0, 32, 1, 1};
    c.ctx->CopySubresourceRegion(cb.Get(), 0, 0, 0, 0, gpuBuf.Get(), 0, &box);
    const D3D11_RECT botRect = {0, LONG(c.height / 2), LONG(c.width), LONG(c.height)};
    c.ctx->RSSetScissorRects(1, &botRect);
    c.ctx->Draw(3, 0);

    TX_REQ(kName, Readback(c, rt.tex.Get(), out));
    return Check(out);
}

}  // namespace txtest
