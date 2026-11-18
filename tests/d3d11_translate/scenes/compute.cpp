// Scene 7: compute. One compute shader writes a pattern through a UAV; a second
// reads a structured buffer and a constant buffer and writes a second UAV texture;
// a pixel pass combines both as SRVs. All shader maths is integer, so the whole
// image has an exact CPU reference.
#include "../scene_util.h"
#include <algorithm>
#include <cstdlib>

namespace txtest {
namespace {
const char* kName = "compute";
const UINT kBlobCount = 16, kShift = 1;

const char* kCsSrc = R"(
RWTexture2D<unorm float4> dst : register(u0);
[numthreads(8, 8, 1)]
void pattern(uint3 id : SV_DispatchThreadID)
{
    uint r = (id.x ^ id.y) & 255;
    uint g = (id.x * 3 + id.y * 5) & 255;
    uint b = (((id.x >> 4) + (id.y >> 4)) & 1) * 255;
    dst[id.xy] = float4(r / 255.0, g / 255.0, b / 255.0, 1);
}
struct Blob { int2 centre; int radius; uint rgb; };
StructuredBuffer<Blob> blobs : register(t0);
cbuffer Params : register(b0) { uint count; uint shift; uint2 pad; };
[numthreads(8, 8, 1)]
void splat(uint3 id : SV_DispatchThreadID)
{
    uint3 acc = uint3(0, 0, 0);
    for (uint i = 0; i < count; ++i) {
        Blob b = blobs[i];
        int2 d = int2(id.xy) - b.centre;
        if (d.x * d.x + d.y * d.y <= b.radius * b.radius)
            acc += uint3(b.rgb & 255, (b.rgb >> 8) & 255, (b.rgb >> 16) & 255) >> shift;
    }
    acc = min(acc, uint3(255, 255, 255));
    dst[id.xy] = float4(acc / 255.0, 1);
}
)";

const char* kPsSrc = R"(
Texture2D a : register(t0);
Texture2D b : register(t1);
float4 ps(float4 pos : SV_Position) : SV_Target
{
    int3 p = int3(pos.xy, 0);
    return float4(abs(a.Load(p).rgb - b.Load(p).rgb), 1);
}
)";

struct Blob { int32_t cx, cy, radius; uint32_t rgb; };

std::vector<Blob> MakeBlobs(uint32_t w, uint32_t h)
{
    std::vector<Blob> v(kBlobCount);
    Rng rng(0xC0FFEE07u);
    for (Blob& b : v) {
        b.cx = static_cast<int32_t>(rng.next() % w);
        b.cy = static_cast<int32_t>(rng.next() % h);
        b.radius = 12 + static_cast<int32_t>(rng.next() % 40);
        b.rgb = rng.next() & 0xFFFFFFu;
    }
    return v;
}

void Expected(uint32_t x, uint32_t y, const std::vector<Blob>& blobs, int rgb[3])
{
    const int a[3] = {static_cast<int>((x ^ y) & 255), static_cast<int>((x * 3 + y * 5) & 255),
                      static_cast<int>((((x >> 4) + (y >> 4)) & 1) * 255)};
    uint32_t acc[3] = {0, 0, 0};
    for (const Blob& b : blobs) {
        const int dx = static_cast<int>(x) - b.cx, dy = static_cast<int>(y) - b.cy;
        if (dx * dx + dy * dy > b.radius * b.radius) continue;
        for (int ch = 0; ch < 3; ++ch) acc[ch] += ((b.rgb >> (8 * ch)) & 255) >> kShift;
    }
    for (int ch = 0; ch < 3; ++ch)
        rgb[ch] = std::abs(a[ch] - static_cast<int>(std::min(acc[ch], 255u)));
}

bool Check(const Image& out, const std::vector<Blob>& blobs)
{
    int worst = 0;
    for (uint32_t y = 0; y < out.height; ++y)
        for (uint32_t x = 0; x < out.width; ++x) {
            int want[3];
            Expected(x, y, blobs, want);
            const uint8_t* p = Px(out, x, y);
            for (int ch = 0; ch < 3; ++ch) worst = std::max(worst, std::abs(want[ch] - p[ch]));
        }
    if (worst > 1) {
        fprintf(stderr, "[%s] image differs from the CPU reference by %d\n", kName, worst);
        return false;
    }
    return true;
}

struct Res {
    Target rt;
    ComPtr<ID3D11Texture2D> texA, texB;
    ComPtr<ID3D11UnorderedAccessView> uavA, uavB;
    ComPtr<ID3D11ShaderResourceView> srvA, srvB, blobSrv;
    ComPtr<ID3D11ComputeShader> csPattern, csSplat;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11Buffer> blobBuf, cb;
};

bool Setup(SceneCtx& c, Res& r, const std::vector<Blob>& blobs)
{
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, r.rt));
    TexDesc d;
    d.w = c.width; d.h = c.height;
    d.bind = D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_SHADER_RESOURCE;
    r.texA = MakeTex(c.dev, d);
    r.texB = MakeTex(c.dev, d);
    r.uavA = MakeUAV(c.dev, r.texA.Get());
    r.uavB = MakeUAV(c.dev, r.texB.Get());
    r.srvA = MakeSRV(c.dev, r.texA.Get());
    r.srvB = MakeSRV(c.dev, r.texB.Get());
    r.csPattern = MakeCS(c.dev, kCsSrc, "pattern");
    r.csSplat = MakeCS(c.dev, kCsSrc, "splat");
    r.vs = MakeVS(c.dev, kFullscreenVS, "vs");
    r.ps = MakePS(c.dev, kPsSrc, "ps");
    r.blobBuf = MakeBuffer(c.dev, static_cast<UINT>(blobs.size() * sizeof(Blob)), D3D11_BIND_SHADER_RESOURCE,
                           blobs.data(), D3D11_USAGE_DEFAULT, D3D11_RESOURCE_MISC_BUFFER_STRUCTURED, sizeof(Blob));
    const uint32_t params[4] = {kBlobCount, kShift, 0, 0};
    r.cb = MakeBuffer(c.dev, sizeof(params), D3D11_BIND_CONSTANT_BUFFER, params);
    TX_REQ(kName, r.texA && r.texB && r.uavA && r.uavB && r.srvA && r.srvB);
    TX_REQ(kName, r.csPattern && r.csSplat && r.vs && r.ps && r.blobBuf && r.cb);
    D3D11_SHADER_RESOURCE_VIEW_DESC sv{};
    sv.Format = DXGI_FORMAT_UNKNOWN;
    sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sv.Buffer.NumElements = kBlobCount;
    TX_HR(kName, c.dev->CreateShaderResourceView(r.blobBuf.Get(), &sv, &r.blobSrv));
    return true;
}
}  // namespace

bool RunCompute(SceneCtx& c, Image& out)
{
    c.ctx->ClearState();
    const std::vector<Blob> blobs = MakeBlobs(c.width, c.height);
    Res r;
    if (!Setup(c, r, blobs)) return false;
    const UINT gx = (c.width + 7) / 8, gy = (c.height + 7) / 8;
    ID3D11UnorderedAccessView* noUav = nullptr;
    ID3D11ShaderResourceView* noSrv = nullptr;

    c.ctx->CSSetShader(r.csPattern.Get(), nullptr, 0);
    c.ctx->CSSetUnorderedAccessViews(0, 1, r.uavA.GetAddressOf(), nullptr);
    c.ctx->Dispatch(gx, gy, 1);

    c.ctx->CSSetShader(r.csSplat.Get(), nullptr, 0);
    c.ctx->CSSetUnorderedAccessViews(0, 1, r.uavB.GetAddressOf(), nullptr);
    c.ctx->CSSetShaderResources(0, 1, r.blobSrv.GetAddressOf());
    c.ctx->CSSetConstantBuffers(0, 1, r.cb.GetAddressOf());
    c.ctx->Dispatch(gx, gy, 1);

    // Unbind the UAVs before the textures are read as SRVs.
    c.ctx->CSSetUnorderedAccessViews(0, 1, &noUav, nullptr);
    c.ctx->CSSetShaderResources(0, 1, &noSrv);
    c.ctx->CSSetShader(nullptr, nullptr, 0);

    c.ctx->ClearRenderTargetView(r.rt.rtv.Get(), kClear);
    c.ctx->OMSetRenderTargets(1, r.rt.rtv.GetAddressOf(), nullptr);
    SetViewport(c.ctx, static_cast<float>(c.width), static_cast<float>(c.height));
    c.ctx->IASetInputLayout(nullptr);
    c.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    c.ctx->VSSetShader(r.vs.Get(), nullptr, 0);
    c.ctx->PSSetShader(r.ps.Get(), nullptr, 0);
    ID3D11ShaderResourceView* srvs[2] = {r.srvA.Get(), r.srvB.Get()};
    c.ctx->PSSetShaderResources(0, 2, srvs);
    c.ctx->Draw(3, 0);

    TX_REQ(kName, Readback(c, r.rt.tex.Get(), out));
    c.ctx->ClearState();
    return Check(out, blobs);
}
}  // namespace txtest
