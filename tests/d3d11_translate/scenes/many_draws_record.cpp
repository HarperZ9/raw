// Scene 8: resources, per-draw constant-buffer updates and the CPU reference.
#include "many_draws.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

namespace txtest::manydraws {
namespace {
// The shader adds the two constant buffers. Each draw rewrites only one of them,
// with (wanted value - the other buffer's current value), so a stale or lost update
// in either buffer moves or recolours the quad.
const char* kSrc = R"(
cbuffer A : register(b0) { float4 psA; float4 colA; };
cbuffer B : register(b1) { float4 psB; float4 colB; };
struct VO { float4 pos : SV_Position; float4 col : COLOR0; };
VO vs(float2 corner : POSITION)
{
    float4 ps = psA + psB;
    VO o;
    o.pos = float4(ps.xy + corner * ps.zw, 0.5, 1);
    o.col = colA + colB;
    return o;
}
float4 ps(VO i) : SV_Target { return i.col; }
)";

DrawData DataFor(uint32_t i, uint32_t rows)
{
    const float cellX = 2.0f / static_cast<float>(kCols), cellY = 2.0f / static_cast<float>(rows);
    Rng rng(i * 2654435761u + 0x1234567u);
    DrawData d;
    d.ps[0] = -1.0f + (static_cast<float>(i % kCols) + 0.5f) * cellX;
    d.ps[1] = 1.0f - (static_cast<float>((i / kCols) % rows) + 0.5f) * cellY;
    d.ps[2] = 0.75f * cellX;
    d.ps[3] = 0.75f * cellY;
    for (int ch = 0; ch < 3; ++ch) {
        uint32_t k = rng.next() & 255u;
        if (k == 128) k = 129;   // 128/256 would land exactly between two 8-bit codes
        d.col[ch] = static_cast<float>(k) / 256.0f;
    }
    d.col[3] = 1.0f;
    return d;
}

DrawData Sub(const DrawData& x, const DrawData& y)
{
    DrawData r;
    for (int i = 0; i < 4; ++i) { r.ps[i] = x.ps[i] - y.ps[i]; r.col[i] = x.col[i] - y.col[i]; }
    return r;
}

bool WriteMapped(ID3D11DeviceContext* x, ID3D11Buffer* b, const DrawData& d)
{
    D3D11_MAPPED_SUBRESOURCE m{};
    HRESULT hr = x->Map(b, 0, D3D11_MAP_WRITE_DISCARD, 0, &m);
    if (FAILED(hr)) return Fail(kName, "Map(WRITE_DISCARD) constant buffer", hr);
    memcpy(m.pData, &d, sizeof(d));
    x->Unmap(b, 0);
    return true;
}

bool WriteB(ID3D11DeviceContext* x, ID3D11Buffer* b, const DrawData& d, bool deferred)
{
    if (deferred) return WriteMapped(x, b, d);
    x->UpdateSubresource(b, 0, nullptr, &d, 0, 0);
    return true;
}

void SetState(ID3D11DeviceContext* x, const Shared& s, const CbPair& cb)
{
    const UINT stride = 8, off = 0;
    ID3D11Buffer* cbs[2] = {cb.a.Get(), cb.b.Get()};
    x->OMSetRenderTargets(1, s.rt.rtv.GetAddressOf(), nullptr);
    x->OMSetDepthStencilState(s.ds.Get(), 0);
    SetViewport(x, s.w, s.h);
    x->RSSetState(s.rs.Get());
    x->IASetInputLayout(s.layout.Get());
    x->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    x->IASetVertexBuffers(0, 1, s.vb.GetAddressOf(), &stride, &off);
    x->VSSetShader(s.vs.Get(), nullptr, 0);
    x->VSSetConstantBuffers(0, 2, cbs);
    x->PSSetShader(s.ps.Get(), nullptr, 0);
}
}  // namespace

uint32_t RowsFor(uint32_t drawCount)
{
    const uint32_t need = std::max(1u, (drawCount + kCols - 1) / kCols);
    uint32_t rows = 1;
    while (rows < need && rows < 4096) rows *= 2;
    return rows;
}

bool MakeShared(SceneCtx& c, Shared& s)
{
    TX_REQ(kName, MakeTarget(c.dev, c.width, c.height, DXGI_FORMAT_R8G8B8A8_UNORM, s.rt));
    const D3D11_INPUT_ELEMENT_DESC el[1] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0}};
    ComPtr<ID3DBlob> blob;
    s.vs = MakeVS(c.dev, kSrc, "vs", &blob);
    s.ps = MakePS(c.dev, kSrc, "ps");
    s.layout = MakeLayout(c.dev, el, 1, blob.Get());
    const float strip[8] = {-1, 1, 1, 1, -1, -1, 1, -1};
    s.vb = MakeBuffer(c.dev, sizeof(strip), D3D11_BIND_VERTEX_BUFFER, strip);
    s.rs = MakeRaster(c.dev, D3D11_CULL_NONE);
    s.ds = MakeDepthState(c.dev, false, false, D3D11_COMPARISON_ALWAYS);
    TX_REQ(kName, s.vs && s.ps && s.layout && s.vb && s.rs && s.ds);
    s.w = static_cast<float>(c.width);
    s.h = static_cast<float>(c.height);
    s.rows = RowsFor(c.drawCount);
    s.draws.resize(c.drawCount);
    for (uint32_t i = 0; i < c.drawCount; ++i) s.draws[i] = DataFor(i, s.rows);
    return true;
}

bool MakeCbPair(ID3D11Device* dev, bool deferred, CbPair& out)
{
    const UINT bind = D3D11_BIND_CONSTANT_BUFFER;
    out.a = MakeBuffer(dev, sizeof(DrawData), bind, nullptr, D3D11_USAGE_DYNAMIC);
    out.b = MakeBuffer(dev, sizeof(DrawData), bind, nullptr,
                       deferred ? D3D11_USAGE_DYNAMIC : D3D11_USAGE_DEFAULT);
    return out.a && out.b;
}

bool RecordRange(ID3D11DeviceContext* x, const Shared& s, const CbPair& cb, uint32_t begin,
                 uint32_t end, bool deferred)
{
    SetState(x, s, cb);
    DrawData a{}, b{};   // what each buffer holds right now
    for (uint32_t i = begin; i < end; ++i) {
        const DrawData& d = s.draws[i];
        bool ok = true;
        if (i == begin) {            // both buffers are undefined at the start of a range
            a = d;
            b = DrawData{};
            ok = WriteMapped(x, cb.a.Get(), a) && WriteB(x, cb.b.Get(), b, deferred);
        } else if ((i & 1u) == 0) {
            a = Sub(d, b);
            ok = WriteMapped(x, cb.a.Get(), a);
        } else {
            b = Sub(d, a);
            ok = WriteB(x, cb.b.Get(), b, deferred);
        }
        if (!ok) return false;
        x->Draw(4, 0);
    }
    return true;
}

static bool PixelExact(uint32_t w, uint32_t h, uint32_t rows)
{
    // Quad edges sit on pixel boundaries when the half-cell is a whole pixel count.
    return w % (kCols * 4) == 0 && h % (rows * 4) == 0;
}

bool CheckImage(const SceneCtx& c, const Shared& s, const Image& out)
{
    if (c.drawCount == 0) return IsClear(out, 0, 0);
    if (!PixelExact(c.width, c.height, s.rows)) {
        // Fallback: the last quad drawn owns its own centre pixel.
        const DrawData& d = s.draws.back();
        const uint32_t x = static_cast<uint32_t>((d.ps[0] * 0.5f + 0.5f) * s.w);
        const uint32_t y = static_cast<uint32_t>((0.5f - d.ps[1] * 0.5f) * s.h);
        return PxNear(out, x, y, static_cast<int>(std::lround(d.col[0] * 255.0f)),
                      static_cast<int>(std::lround(d.col[1] * 255.0f)),
                      static_cast<int>(std::lround(d.col[2] * 255.0f)), 1) ||
               Fail(kName, "last quad centre colour");
    }
    Image ref;
    ref.width = c.width; ref.height = c.height;
    ref.rgba.resize(static_cast<size_t>(c.width) * c.height * 4);
    for (size_t i = 0; i < ref.rgba.size(); i += 4) {
        ref.rgba[i] = 51; ref.rgba[i + 1] = 102; ref.rgba[i + 2] = 153; ref.rgba[i + 3] = 255;
    }
    for (const DrawData& d : s.draws) {
        const int x0 = static_cast<int>(std::lround((d.ps[0] - d.ps[2] + 1.0f) * 0.5f * s.w));
        const int x1 = static_cast<int>(std::lround((d.ps[0] + d.ps[2] + 1.0f) * 0.5f * s.w));
        const int y0 = static_cast<int>(std::lround((1.0f - d.ps[1] - d.ps[3]) * 0.5f * s.h));
        const int y1 = static_cast<int>(std::lround((1.0f - d.ps[1] + d.ps[3]) * 0.5f * s.h));
        for (int y = std::max(y0, 0); y < std::min(y1, static_cast<int>(c.height)); ++y)
            for (int x = std::max(x0, 0); x < std::min(x1, static_cast<int>(c.width)); ++x) {
                uint8_t* p = &ref.rgba[(static_cast<size_t>(y) * c.width + x) * 4];
                for (int ch = 0; ch < 3; ++ch)
                    p[ch] = static_cast<uint8_t>(std::lround(d.col[ch] * 255.0f));
            }
    }
    const int diff = MaxDiff(out, ref);
    if (diff < 0 || diff > 1) {
        fprintf(stderr, "[%s] image differs from the painter's-order reference by %d\n", kName, diff);
        return false;
    }
    return true;
}

}  // namespace txtest::manydraws
