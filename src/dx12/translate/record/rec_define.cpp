// DEFINE records: emitted the first time a capture references an object.
#include "rec_emit.h"
#include "rec_util.h"

namespace raw::rec
{
void Recorder::Define(ID3D11DeviceChild* o, u64 id, Kind k)
{
    if (k == K_RESOURCE) {
        D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
        static_cast<ID3D11Resource*>(o)->GetType(&dim);
        k = dim == D3D11_RESOURCE_DIMENSION_BUFFER ? K_BUFFER
          : dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D ? K_TEX1D
          : dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D ? K_TEX2D
          : dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D ? K_TEX3D : K_OPAQUE;
    }
    if (k < K_COUNT) ++m_kindCounts[k];
    switch (k) {
    case K_BUFFER: DefineBuffer(static_cast<ID3D11Buffer*>(o), id); break;
    case K_TEX1D: case K_TEX2D: case K_TEX3D:
        DefineTexture(static_cast<ID3D11Resource*>(o), id); break;
    case K_SRV: case K_RTV: case K_DSV: case K_UAV: DefineView(o, id, k); break;
    case K_VS: case K_HS: case K_DS: case K_GS: case K_PS: case K_CS:
        DefineShader(o, id, k); break;
    case K_INPUT_LAYOUT: DefineInputLayout(o, id); break;
    case K_BLEND: case K_DEPTH_STENCIL: case K_RASTERIZER: case K_SAMPLER:
        DefineState(o, id, k); break;
    case K_QUERY: case K_PREDICATE: DefineQuery(o, id, k); break;
    default: { Emit e(*this, OP_DEFINE_OPAQUE); e.q(id).u(k).u(0); } break;
    }
}

void Recorder::DefineBuffer(ID3D11Buffer* b, u64 id)
{
    D3D11_BUFFER_DESC d{};
    b->GetDesc(&d);
    Emit e(*this, OP_DEFINE_BUFFER);
    e.q(id).u(d.ByteWidth).u(d.Usage).u(d.BindFlags).u(d.CPUAccessFlags).u(d.MiscFlags)
        .u(d.StructureByteStride);
    WriteBufferContents(b, d, id);
}

void Recorder::DefineTexture(ID3D11Resource* r, u64 id)
{
    TexDesc t;
    if (!GetTexDesc(r, &t)) return;
    Emit e(*this, OP_DEFINE_TEXTURE);
    e.q(id).u(t.dim).u(t.width).u(t.height).u(t.depth).u(t.mips).u(t.arraySize).u(t.format)
        .u(t.sampleCount).u(t.sampleQuality).u(t.usage).u(t.bind).u(t.cpu).u(t.misc)
        .u(t.SubCount());
    WriteTextureContents(r, t, id);
}

void Recorder::DefineView(ID3D11DeviceChild* o, u64 id, Kind k)
{
    union {
        D3D11_SHADER_RESOURCE_VIEW_DESC srv;
        D3D11_RENDER_TARGET_VIEW_DESC rtv;
        D3D11_DEPTH_STENCIL_VIEW_DESC dsv;
        D3D11_UNORDERED_ACCESS_VIEW_DESC uav;
    } d;
    memset(&d, 0, sizeof(d));
    u32 bytes = 0;
    if (k == K_SRV) { static_cast<ID3D11ShaderResourceView*>(o)->GetDesc(&d.srv); bytes = sizeof(d.srv); }
    if (k == K_RTV) { static_cast<ID3D11RenderTargetView*>(o)->GetDesc(&d.rtv); bytes = sizeof(d.rtv); }
    if (k == K_DSV) { static_cast<ID3D11DepthStencilView*>(o)->GetDesc(&d.dsv); bytes = sizeof(d.dsv); }
    if (k == K_UAV) { static_cast<ID3D11UnorderedAccessView*>(o)->GetDesc(&d.uav); bytes = sizeof(d.uav); }
    ID3D11Resource* res = nullptr;
    static_cast<ID3D11View*>(o)->GetResource(&res);
    {
        Emit e(*this, OP_DEFINE_VIEW);
        e.q(id).u(k).u(bytes).id(res, K_RESOURCE).raw(&d, bytes);
    }
    SafeRelease(res);
}

void Recorder::DefineShader(ID3D11DeviceChild* o, u64 id, Kind k)
{
    const u8 *code = nullptr, *so = nullptr;
    size_t codeBytes = 0, soBytes = 0;
    IUnknown* keepCode = FetchBlob(o, kGuidBytecode, &code, &codeBytes);
    IUnknown* keepSo = FetchBlob(o, kGuidStreamOut, &so, &soBytes);
    {
        Emit e(*this, OP_DEFINE_SHADER);
        e.q(id).u(k).u((keepCode ? 1u : 0u) | (keepSo ? 2u : 0u));
        e.q(keepCode ? Fnv1a64(code, codeBytes) : 0).blob(code, codeBytes);
        if (keepSo) e.blob(so, soBytes);
    }
    if (!keepCode) m_undefined.push_back({id, 0, WHY_NO_BYTECODE});
    SafeRelease(keepCode);
    SafeRelease(keepSo);
}

void Recorder::DefineInputLayout(ID3D11DeviceChild* o, u64 id)
{
    const u8* data = nullptr;
    size_t bytes = 0;
    IUnknown* keep = FetchBlob(o, kGuidLayout, &data, &bytes);
    {
        Emit e(*this, OP_DEFINE_INPUT_LAYOUT);
        e.q(id).u(keep ? SUB_DATA : SUB_UNDEFINED).u(0).raw(data, bytes);
    }
    if (!keep) m_undefined.push_back({id, 0, WHY_NO_LAYOUT});
    SafeRelease(keep);
}

void Recorder::DefineState(ID3D11DeviceChild* o, u64 id, Kind k)
{
    union {
        D3D11_BLEND_DESC blend;
        D3D11_DEPTH_STENCIL_DESC depth;
        D3D11_RASTERIZER_DESC raster;
        D3D11_SAMPLER_DESC sampler;
    } d;
    memset(&d, 0, sizeof(d));
    u32 bytes = 0;
    if (k == K_BLEND) { static_cast<ID3D11BlendState*>(o)->GetDesc(&d.blend); bytes = sizeof(d.blend); }
    if (k == K_DEPTH_STENCIL) { static_cast<ID3D11DepthStencilState*>(o)->GetDesc(&d.depth); bytes = sizeof(d.depth); }
    if (k == K_RASTERIZER) { static_cast<ID3D11RasterizerState*>(o)->GetDesc(&d.raster); bytes = sizeof(d.raster); }
    if (k == K_SAMPLER) { static_cast<ID3D11SamplerState*>(o)->GetDesc(&d.sampler); bytes = sizeof(d.sampler); }
    Emit e(*this, OP_DEFINE_STATE);
    e.q(id).u(k).u(bytes).raw(&d, bytes);
}

void Recorder::DefineQuery(ID3D11DeviceChild* o, u64 id, Kind k)
{
    D3D11_QUERY_DESC d{};
    static_cast<ID3D11Query*>(o)->GetDesc(&d);
    Emit e(*this, OP_DEFINE_QUERY);
    e.q(id).u(k).u(d.Query).u(d.MiscFlags);
}
} // namespace raw::rec
