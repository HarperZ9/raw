// Replayer: DEFINE records for resources, views, state objects and queries.
#include "rec_replayer.h"
#include "rec_util.h"
#include <cstring>

namespace raw::rec
{
namespace
{
// Misc flags that describe the resource itself. Sharing and GDI flags tie a
// resource to the capture process and are dropped on replay.
constexpr u32 kMiscKeep = D3D11_RESOURCE_MISC_GENERATE_MIPS | D3D11_RESOURCE_MISC_TEXTURECUBE
                        | D3D11_RESOURCE_MISC_DRAWINDIRECT_ARGS | D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS
                        | D3D11_RESOURCE_MISC_BUFFER_STRUCTURED | D3D11_RESOURCE_MISC_RESOURCE_CLAMP;

template <class T> bool Load(const u8* p, u32 bytes, T* out)
{
    if (!p || bytes != sizeof(T)) return false;
    memcpy(out, p, sizeof(T));
    return true;
}
} // namespace

HRESULT Replayer::Define(const Record& r)
{
    Cursor c(r);
    switch (r.op) {
    case OP_DEFINE_BUFFER: return DefineBuffer(c);
    case OP_DEFINE_TEXTURE: return DefineTexture(c);
    case OP_DEFINE_VIEW: return DefineView(c);
    case OP_DEFINE_SHADER: return DefineShader(c);
    case OP_DEFINE_INPUT_LAYOUT: return DefineInputLayout(c);
    case OP_DEFINE_STATE: return DefineState(c);
    case OP_DEFINE_QUERY: return DefineQuery(c);
    default: break;
    }
    const u64 id = c.U64();             // OP_DEFINE_OPAQUE: known id, no object
    return c.ok ? Add(id, K_OPAQUE, nullptr, S_OK) : Short();
}

HRESULT Replayer::DefineBuffer(Cursor& c)
{
    const u64 id = c.U64();
    D3D11_BUFFER_DESC d{};
    d.ByteWidth = c.U32();
    d.Usage = static_cast<D3D11_USAGE>(c.U32());
    d.BindFlags = c.U32();
    d.CPUAccessFlags = c.U32();
    d.MiscFlags = c.U32() & kMiscKeep;
    d.StructureByteStride = c.U32();
    const SubData s = ReadSubData(c);
    if (!c.ok) return Short();
    const bool has = s.state == SUB_DATA && s.bytes == d.ByteWidth;
    const D3D11_SUBRESOURCE_DATA init{s.data, 0, 0};
    ID3D11Buffer* b = nullptr;
    const HRESULT hr = m_dev->CreateBuffer(&d, has ? &init : nullptr, &b);
    if (SUCCEEDED(hr) && !has) ++m_res.contentsNotRestored;
    return Add(id, K_BUFFER, b, hr);
}

HRESULT Replayer::DefineTexture(Cursor& c)
{
    const u64 id = c.U64();
    TexDesc t;
    u32* const fields[] = {&t.dim, &t.width, &t.height, &t.depth, &t.mips, &t.arraySize, &t.format,
                           &t.sampleCount, &t.sampleQuality, &t.usage, &t.bind, &t.cpu, &t.misc};
    for (u32* f : fields) *f = c.U32();
    const u32 subCount = c.U32();
    if (!c.ok || t.dim < 1 || t.dim > 3 || subCount != t.SubCount() || subCount > 32768)
        return Fail(E_INVALIDARG, "texture description is inconsistent");
    std::vector<SubData> subs(subCount);
    std::vector<D3D11_SUBRESOURCE_DATA> init(subCount);
    bool all = true;
    for (u32 i = 0; i < subCount; ++i) {
        subs[i] = ReadSubData(c);
        const SubGeom g = TexSubGeom(t, i, nullptr);
        if (subs[i].state != SUB_DATA || !g.ok || subs[i].rowBytes != g.rowBytes
            || subs[i].rows != g.rows || subs[i].slices != g.slices) {
            subs[i].state = SUB_UNDEFINED;
            all = false;
        }
        init[i] = {subs[i].data, g.rowBytes, g.rowBytes * g.rows};
    }
    if (!c.ok) return Short();
    t.misc &= kMiscKeep;
    ID3D11Resource* tex = nullptr;
    const bool useInit = all && (t.usage == D3D11_USAGE_IMMUTABLE || !m_opt.uploadThroughStaging);
    HRESULT hr = useInit ? CreateTexture(m_dev, t, init.data(), &tex) : E_FAIL;
    if (SUCCEEDED(hr) && (t.bind & D3D11_BIND_DEPTH_STENCIL)) m_res.depthRestored += subCount;
    if (FAILED(hr)) {   // initial data refused (depth-stencil, generated mips) or incomplete
        hr = CreateTexture(m_dev, t, nullptr, &tex);
        if (SUCCEEDED(hr)) UploadTexture(tex, t, subs);
    }
    return Add(id, K_TEX1D + t.dim - 1, tex, hr);
}

// Second route for contents: write them into a staging twin and copy whole
// subresources across. This is the route that reaches depth-stencil textures,
// which accept neither UpdateSubresource nor, on some devices, initial data.
void Replayer::UploadTexture(ID3D11Resource* tex, const TexDesc& t, const std::vector<SubData>& subs)
{
    const bool depth = (t.bind & D3D11_BIND_DEPTH_STENCIL) != 0;
    u32& lost = depth ? m_res.depthNotRestored : m_res.contentsNotRestored;
    TexDesc s = t;
    s.usage = D3D11_USAGE_STAGING;
    s.bind = 0;
    s.cpu = D3D11_CPU_ACCESS_WRITE;
    s.misc = 0;
    ID3D11Resource* twin = nullptr;
    const HRESULT hr = t.sampleCount > 1 ? E_FAIL : CreateTexture(m_dev, s, nullptr, &twin);
    for (u32 i = 0; i < subs.size(); ++i) {
        D3D11_MAPPED_SUBRESOURCE m{};
        if (FAILED(hr) || subs[i].state != SUB_DATA
            || FAILED(m_ctx->Map(twin, i, D3D11_MAP_WRITE, 0, &m))) {
            ++lost;
            continue;
        }
        CopyRowsOut(m.pData, m.RowPitch, m.DepthPitch, subs[i].data, subs[i].rowBytes, subs[i].rows,
                    subs[i].slices);
        m_ctx->Unmap(twin, i);
        m_ctx->CopySubresourceRegion(tex, i, 0, 0, 0, twin, i, nullptr);
        ++m_res.stagingUploads;
        if (depth) ++m_res.depthRestored;
    }
    SafeRelease(twin);
}

HRESULT Replayer::DefineView(Cursor& c)
{
    const u64 id = c.U64();
    const u32 kind = c.U32(), bytes = c.U32();
    const u64 resId = c.U64();
    const u8* raw = c.Raw(bytes);
    if (!c.ok) return Short();
    ID3D11Resource* res = static_cast<ID3D11Resource*>(Get(resId, K_BUFFER, K_TEX3D));
    if (!res) return Add(id, kind, nullptr, Fail(E_FAIL, "view of a resource that was not created"));
    D3D11_SHADER_RESOURCE_VIEW_DESC srv;
    D3D11_RENDER_TARGET_VIEW_DESC rtv;
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
    D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
    ID3D11DeviceChild* v = nullptr;
    HRESULT hr = E_INVALIDARG;
    if (kind == K_SRV && Load(raw, bytes, &srv))
        hr = m_dev->CreateShaderResourceView(res, &srv, reinterpret_cast<ID3D11ShaderResourceView**>(&v));
    else if (kind == K_RTV && Load(raw, bytes, &rtv))
        hr = m_dev->CreateRenderTargetView(res, &rtv, reinterpret_cast<ID3D11RenderTargetView**>(&v));
    else if (kind == K_DSV && Load(raw, bytes, &dsv))
        hr = m_dev->CreateDepthStencilView(res, &dsv, reinterpret_cast<ID3D11DepthStencilView**>(&v));
    else if (kind == K_UAV && Load(raw, bytes, &uav))
        hr = m_dev->CreateUnorderedAccessView(res, &uav, reinterpret_cast<ID3D11UnorderedAccessView**>(&v));
    else
        Fail(hr, "view description has the wrong size or kind");
    return Add(id, kind, v, hr);
}

HRESULT Replayer::DefineState(Cursor& c)
{
    const u64 id = c.U64();
    const u32 kind = c.U32(), bytes = c.U32();
    const u8* raw = c.Raw(bytes);
    if (!c.ok) return Short();
    D3D11_BLEND_DESC blend;
    D3D11_DEPTH_STENCIL_DESC depth;
    D3D11_RASTERIZER_DESC raster;
    D3D11_SAMPLER_DESC sampler;
    ID3D11DeviceChild* s = nullptr;
    HRESULT hr = E_INVALIDARG;
    if (kind == K_BLEND && Load(raw, bytes, &blend))
        hr = m_dev->CreateBlendState(&blend, reinterpret_cast<ID3D11BlendState**>(&s));
    else if (kind == K_DEPTH_STENCIL && Load(raw, bytes, &depth))
        hr = m_dev->CreateDepthStencilState(&depth, reinterpret_cast<ID3D11DepthStencilState**>(&s));
    else if (kind == K_RASTERIZER && Load(raw, bytes, &raster))
        hr = m_dev->CreateRasterizerState(&raster, reinterpret_cast<ID3D11RasterizerState**>(&s));
    else if (kind == K_SAMPLER && Load(raw, bytes, &sampler))
        hr = m_dev->CreateSamplerState(&sampler, reinterpret_cast<ID3D11SamplerState**>(&s));
    else
        Fail(hr, "state description has the wrong size or kind");
    return Add(id, kind, s, hr);
}

HRESULT Replayer::DefineQuery(Cursor& c)
{
    const u64 id = c.U64();
    const u32 kind = c.U32();
    D3D11_QUERY_DESC d{};
    d.Query = static_cast<D3D11_QUERY>(c.U32());
    d.MiscFlags = c.U32();
    if (!c.ok) return Short();
    ID3D11DeviceChild* q = nullptr;
    const HRESULT hr = kind == K_PREDICATE
        ? m_dev->CreatePredicate(&d, reinterpret_cast<ID3D11Predicate**>(&q))
        : m_dev->CreateQuery(&d, reinterpret_cast<ID3D11Query**>(&q));
    return Add(id, kind == K_PREDICATE ? K_PREDICATE : K_QUERY, q, hr);
}
} // namespace raw::rec
