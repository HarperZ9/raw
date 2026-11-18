// Views: D3D11 view descriptions translated to D3D12 descriptors.
#include "tx_context.h"

namespace raw::tx {

void FreeViewDescriptor(Device* dev, D3D12_DESCRIPTOR_HEAP_TYPE heap, D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (heap == D3D12_DESCRIPTOR_HEAP_TYPE_RTV) dev->cpuRtv.Free(h);
    else if (heap == D3D12_DESCRIPTOR_HEAP_TYPE_DSV) dev->cpuDsv.Free(h);
    else dev->cpuSrv.Free(h);
}

bool IsStencilPlaneView(DXGI_FORMAT f)
{
    return f == DXGI_FORMAT_X24_TYPELESS_G8_UINT || f == DXGI_FORMAT_X32_TYPELESS_G8X24_UINT;
}

DXGI_FORMAT StripSrgb(DXGI_FORMAT f)
{
    switch (f) {
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB: return DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB: return DXGI_FORMAT_B8G8R8X8_UNORM;
    default: return f;
    }
}

static void SetRange(SubRange& r, const ResCore& c, uint32_t mip0, uint32_t mipN, uint32_t s0, uint32_t sN, uint8_t planes)
{
    if (c.dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D) { s0 = 0; sN = 1; }
    if (mipN > c.mips - mip0) mipN = c.mips - mip0;
    if (sN > c.slices - s0) sN = c.slices - s0;
    r.mip0 = uint16_t(mip0); r.mipN = uint16_t(mipN); r.slice0 = uint16_t(s0); r.sliceN = uint16_t(sN);
    r.planeMask = planes;
    uint8_t allPlanes = c.planes > 1 ? 3 : 1;
    r.all = mip0 == 0 && mipN == c.mips && s0 == 0 && sN == c.slices && planes == allPlanes;
}

template <class V>
static V* NewView(Device* dev, ResCore* core, D3D12_DESCRIPTOR_HEAP_TYPE heap)
{
    V* v = new V(dev);
    v->core = core;
    core->intRef(core->self, +1);
    v->heap = heap;
    v->h = heap == D3D12_DESCRIPTOR_HEAP_TYPE_RTV ? dev->cpuRtv.Alloc()
         : heap == D3D12_DESCRIPTOR_HEAP_TYPE_DSV ? dev->cpuDsv.Alloc() : dev->cpuSrv.Alloc();
    return v;
}

static D3D11_SHADER_RESOURCE_VIEW_DESC DefaultSrv(const ResCore& c)
{
    D3D11_SHADER_RESOURCE_VIEW_DESC d{};
    d.Format = c.format;
    switch (c.dim) {
    case D3D11_RESOURCE_DIMENSION_BUFFER:
        d.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
        d.Buffer.NumElements = c.stride ? UINT(c.bytes / c.stride) : UINT(c.bytes / 4);
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D:
        d.ViewDimension = c.slices > 1 ? D3D11_SRV_DIMENSION_TEXTURE1DARRAY : D3D11_SRV_DIMENSION_TEXTURE1D;
        d.Texture1DArray.MipLevels = UINT(-1); d.Texture1DArray.ArraySize = c.slices;
        break;
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D:
        d.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE3D;
        d.Texture3D.MipLevels = UINT(-1);
        break;
    default:
        if (c.samples > 1) {
            d.ViewDimension = c.slices > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY : D3D11_SRV_DIMENSION_TEXTURE2DMS;
            d.Texture2DMSArray.ArraySize = c.slices;
        } else if (c.misc & D3D11_RESOURCE_MISC_TEXTURECUBE) {
            d.ViewDimension = c.slices > 6 ? D3D11_SRV_DIMENSION_TEXTURECUBEARRAY : D3D11_SRV_DIMENSION_TEXTURECUBE;
            d.TextureCubeArray.MipLevels = UINT(-1); d.TextureCubeArray.NumCubes = c.slices / 6;
        } else {
            d.ViewDimension = c.slices > 1 ? D3D11_SRV_DIMENSION_TEXTURE2DARRAY : D3D11_SRV_DIMENSION_TEXTURE2D;
            d.Texture2DArray.MipLevels = UINT(-1); d.Texture2DArray.ArraySize = c.slices;
        }
    }
    return d;
}

STDMETHODIMP Device::CreateShaderResourceView(ID3D11Resource* res, const D3D11_SHADER_RESOURCE_VIEW_DESC* in, ID3D11ShaderResourceView** out)
{
    ResCore* c = CoreOf(res);
    if (!c) return E_INVALIDARG;
    if (!out) return S_FALSE;
    D3D11_SHADER_RESOURCE_VIEW_DESC d = in ? *in : DefaultSrv(*c);
    if (d.Format == DXGI_FORMAT_UNKNOWN && !c->IsBuffer()) d.Format = c->format;
    D3D12_SHADER_RESOURCE_VIEW_DESC s{};
    s.Format = d.Format;
    s.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    uint8_t plane = IsStencilPlaneView(d.Format) ? 1 : 0;
    uint32_t m0 = 0, mN = c->mips, s0 = 0, sN = c->slices;
    auto mips = [&](UINT most, UINT levels) { m0 = most; mN = levels == UINT(-1) ? c->mips - most : levels; return mN; };
    switch (d.ViewDimension) {
    case D3D11_SRV_DIMENSION_BUFFER:
    case D3D11_SRV_DIMENSION_BUFFEREX:
        s.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
        s.Buffer.FirstElement = d.Buffer.FirstElement; s.Buffer.NumElements = d.Buffer.NumElements;
        s.Buffer.StructureByteStride = d.Format == DXGI_FORMAT_UNKNOWN ? c->stride : 0;
        if (d.ViewDimension == D3D11_SRV_DIMENSION_BUFFEREX && (d.BufferEx.Flags & D3D11_BUFFEREX_SRV_FLAG_RAW)) {
            s.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW; s.Format = DXGI_FORMAT_R32_TYPELESS; s.Buffer.StructureByteStride = 0;
        }
        break;
    case D3D11_SRV_DIMENSION_TEXTURE1D:
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1D;
        s.Texture1D.MostDetailedMip = d.Texture1D.MostDetailedMip; s.Texture1D.MipLevels = mips(d.Texture1D.MostDetailedMip, d.Texture1D.MipLevels);
        break;
    case D3D11_SRV_DIMENSION_TEXTURE1DARRAY:
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE1DARRAY;
        s.Texture1DArray.MostDetailedMip = d.Texture1DArray.MostDetailedMip; s.Texture1DArray.MipLevels = mips(d.Texture1DArray.MostDetailedMip, d.Texture1DArray.MipLevels);
        s.Texture1DArray.FirstArraySlice = s0 = d.Texture1DArray.FirstArraySlice; s.Texture1DArray.ArraySize = sN = d.Texture1DArray.ArraySize;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE2D:
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
        s.Texture2D.MostDetailedMip = d.Texture2D.MostDetailedMip; s.Texture2D.MipLevels = mips(d.Texture2D.MostDetailedMip, d.Texture2D.MipLevels);
        s.Texture2D.PlaneSlice = plane;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE2DARRAY:
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
        s.Texture2DArray.MostDetailedMip = d.Texture2DArray.MostDetailedMip; s.Texture2DArray.MipLevels = mips(d.Texture2DArray.MostDetailedMip, d.Texture2DArray.MipLevels);
        s.Texture2DArray.FirstArraySlice = s0 = d.Texture2DArray.FirstArraySlice; s.Texture2DArray.ArraySize = sN = d.Texture2DArray.ArraySize;
        s.Texture2DArray.PlaneSlice = plane;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMS: s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMS; break;
    case D3D11_SRV_DIMENSION_TEXTURE2DMSARRAY:
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DMSARRAY;
        s.Texture2DMSArray.FirstArraySlice = s0 = d.Texture2DMSArray.FirstArraySlice; s.Texture2DMSArray.ArraySize = sN = d.Texture2DMSArray.ArraySize;
        break;
    case D3D11_SRV_DIMENSION_TEXTURE3D:
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE3D;
        s.Texture3D.MostDetailedMip = d.Texture3D.MostDetailedMip; s.Texture3D.MipLevels = mips(d.Texture3D.MostDetailedMip, d.Texture3D.MipLevels);
        break;
    case D3D11_SRV_DIMENSION_TEXTURECUBE:
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBE;
        s.TextureCube.MostDetailedMip = d.TextureCube.MostDetailedMip; s.TextureCube.MipLevels = mips(d.TextureCube.MostDetailedMip, d.TextureCube.MipLevels);
        sN = 6;
        break;
    case D3D11_SRV_DIMENSION_TEXTURECUBEARRAY:
        s.ViewDimension = D3D12_SRV_DIMENSION_TEXTURECUBEARRAY;
        s.TextureCubeArray.MostDetailedMip = d.TextureCubeArray.MostDetailedMip; s.TextureCubeArray.MipLevels = mips(d.TextureCubeArray.MostDetailedMip, d.TextureCubeArray.MipLevels);
        s.TextureCubeArray.First2DArrayFace = s0 = d.TextureCubeArray.First2DArrayFace; s.TextureCubeArray.NumCubes = d.TextureCubeArray.NumCubes;
        sN = d.TextureCubeArray.NumCubes * 6;
        break;
    default: return E_INVALIDARG;
    }
    TSRV* v = NewView<TSRV>(this, c, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    v->desc = d; v->format = s.Format;
    SetRange(v->range, *c, m0, mN, s0, sN, uint8_t(1u << plane));
    ID3D12Resource* r = c->store == Store::Renamed ? c->chunk.res : c->Res();
    if (c->store == Store::Renamed) s.Buffer.FirstElement += c->chunk.offset / (s.Buffer.StructureByteStride ? s.Buffer.StructureByteStride : 4);
    d3d->CreateShaderResourceView(r, &s, v->h);
    *out = v;
    return S_OK;
}

STDMETHODIMP Device::CreateRenderTargetView(ID3D11Resource* res, const D3D11_RENDER_TARGET_VIEW_DESC* in, ID3D11RenderTargetView** out)
{
    ResCore* c = CoreOf(res);
    if (!c || c->IsBuffer()) return E_INVALIDARG;
    if (!out) return S_FALSE;
    D3D11_RENDER_TARGET_VIEW_DESC d{};
    if (in) d = *in;
    else {
        d.Format = c->format;
        if (c->dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D) { d.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE3D; d.Texture3D.WSize = UINT(-1); }
        else if (c->dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D) { d.ViewDimension = c->slices > 1 ? D3D11_RTV_DIMENSION_TEXTURE1DARRAY : D3D11_RTV_DIMENSION_TEXTURE1D; d.Texture1DArray.ArraySize = c->slices; }
        else if (c->samples > 1) { d.ViewDimension = c->slices > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY : D3D11_RTV_DIMENSION_TEXTURE2DMS; d.Texture2DMSArray.ArraySize = c->slices; }
        else { d.ViewDimension = c->slices > 1 ? D3D11_RTV_DIMENSION_TEXTURE2DARRAY : D3D11_RTV_DIMENSION_TEXTURE2D; d.Texture2DArray.ArraySize = c->slices; }
    }
    if (d.Format == DXGI_FORMAT_UNKNOWN) d.Format = c->format;
    D3D12_RENDER_TARGET_VIEW_DESC s{};
    s.Format = d.Format;
    uint32_t m0 = 0, s0 = 0, sN = 1;
    switch (d.ViewDimension) {
    case D3D11_RTV_DIMENSION_TEXTURE1D: s.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1D; s.Texture1D.MipSlice = m0 = d.Texture1D.MipSlice; break;
    case D3D11_RTV_DIMENSION_TEXTURE1DARRAY:
        s.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE1DARRAY; s.Texture1DArray.MipSlice = m0 = d.Texture1DArray.MipSlice;
        s.Texture1DArray.FirstArraySlice = s0 = d.Texture1DArray.FirstArraySlice; s.Texture1DArray.ArraySize = sN = d.Texture1DArray.ArraySize; break;
    case D3D11_RTV_DIMENSION_TEXTURE2D: s.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D; s.Texture2D.MipSlice = m0 = d.Texture2D.MipSlice; break;
    case D3D11_RTV_DIMENSION_TEXTURE2DARRAY:
        s.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DARRAY; s.Texture2DArray.MipSlice = m0 = d.Texture2DArray.MipSlice;
        s.Texture2DArray.FirstArraySlice = s0 = d.Texture2DArray.FirstArraySlice; s.Texture2DArray.ArraySize = sN = d.Texture2DArray.ArraySize; break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMS: s.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMS; break;
    case D3D11_RTV_DIMENSION_TEXTURE2DMSARRAY:
        s.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2DMSARRAY;
        s.Texture2DMSArray.FirstArraySlice = s0 = d.Texture2DMSArray.FirstArraySlice; s.Texture2DMSArray.ArraySize = sN = d.Texture2DMSArray.ArraySize; break;
    case D3D11_RTV_DIMENSION_TEXTURE3D:
        s.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE3D; s.Texture3D.MipSlice = m0 = d.Texture3D.MipSlice;
        s.Texture3D.FirstWSlice = d.Texture3D.FirstWSlice; s.Texture3D.WSize = d.Texture3D.WSize; break;
    default: return E_INVALIDARG;
    }
    TRTV* v = NewView<TRTV>(this, c, D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    v->desc = d; v->format = d.Format;
    SetRange(v->range, *c, m0, 1, s0, sN, 1);
    d3d->CreateRenderTargetView(c->Res(), &s, v->h);
    *out = v;
    return S_OK;
}

STDMETHODIMP Device::CreateDepthStencilView(ID3D11Resource* res, const D3D11_DEPTH_STENCIL_VIEW_DESC* in, ID3D11DepthStencilView** out)
{
    ResCore* c = CoreOf(res);
    if (!c || c->IsBuffer()) return E_INVALIDARG;
    if (!out) return S_FALSE;
    D3D11_DEPTH_STENCIL_VIEW_DESC d{};
    if (in) d = *in;
    else {
        d.Format = c->format;
        if (c->samples > 1) { d.ViewDimension = c->slices > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY : D3D11_DSV_DIMENSION_TEXTURE2DMS; d.Texture2DMSArray.ArraySize = c->slices; }
        else { d.ViewDimension = c->slices > 1 ? D3D11_DSV_DIMENSION_TEXTURE2DARRAY : D3D11_DSV_DIMENSION_TEXTURE2D; d.Texture2DArray.ArraySize = c->slices; }
    }
    if (d.Format == DXGI_FORMAT_UNKNOWN) d.Format = c->format;
    D3D12_DEPTH_STENCIL_VIEW_DESC s{};
    s.Format = d.Format;
    s.Flags = static_cast<D3D12_DSV_FLAGS>(d.Flags);   // read-only depth 1, read-only stencil 2 in both APIs
    uint32_t m0 = 0, s0 = 0, sN = 1;
    switch (d.ViewDimension) {
    case D3D11_DSV_DIMENSION_TEXTURE1D: s.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1D; s.Texture1D.MipSlice = m0 = d.Texture1D.MipSlice; break;
    case D3D11_DSV_DIMENSION_TEXTURE1DARRAY:
        s.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE1DARRAY; s.Texture1DArray.MipSlice = m0 = d.Texture1DArray.MipSlice;
        s.Texture1DArray.FirstArraySlice = s0 = d.Texture1DArray.FirstArraySlice; s.Texture1DArray.ArraySize = sN = d.Texture1DArray.ArraySize; break;
    case D3D11_DSV_DIMENSION_TEXTURE2D: s.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D; s.Texture2D.MipSlice = m0 = d.Texture2D.MipSlice; break;
    case D3D11_DSV_DIMENSION_TEXTURE2DARRAY:
        s.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DARRAY; s.Texture2DArray.MipSlice = m0 = d.Texture2DArray.MipSlice;
        s.Texture2DArray.FirstArraySlice = s0 = d.Texture2DArray.FirstArraySlice; s.Texture2DArray.ArraySize = sN = d.Texture2DArray.ArraySize; break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMS: s.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMS; break;
    case D3D11_DSV_DIMENSION_TEXTURE2DMSARRAY:
        s.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2DMSARRAY;
        s.Texture2DMSArray.FirstArraySlice = s0 = d.Texture2DMSArray.FirstArraySlice; s.Texture2DMSArray.ArraySize = sN = d.Texture2DMSArray.ArraySize; break;
    default: return E_INVALIDARG;
    }
    TDSV* v = NewView<TDSV>(this, c, D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
    v->desc = d; v->format = d.Format;
    SetRange(v->range, *c, m0, 1, s0, sN, c->planes > 1 ? 3 : 1);
    d3d->CreateDepthStencilView(c->Res(), &s, v->h);
    *out = v;
    return S_OK;
}

STDMETHODIMP Device::CreateUnorderedAccessView(ID3D11Resource* res, const D3D11_UNORDERED_ACCESS_VIEW_DESC* in, ID3D11UnorderedAccessView** out)
{
    ResCore* c = CoreOf(res);
    if (!c) return E_INVALIDARG;
    if (!out) return S_FALSE;
    D3D11_UNORDERED_ACCESS_VIEW_DESC d{};
    if (in) d = *in;
    else {
        d.Format = c->format;
        if (c->IsBuffer()) { d.ViewDimension = D3D11_UAV_DIMENSION_BUFFER; d.Buffer.NumElements = c->stride ? UINT(c->bytes / c->stride) : UINT(c->bytes / 4); }
        else if (c->dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D) { d.ViewDimension = D3D11_UAV_DIMENSION_TEXTURE3D; d.Texture3D.WSize = UINT(-1); }
        else if (c->dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D) { d.ViewDimension = c->slices > 1 ? D3D11_UAV_DIMENSION_TEXTURE1DARRAY : D3D11_UAV_DIMENSION_TEXTURE1D; d.Texture1DArray.ArraySize = c->slices; }
        else { d.ViewDimension = c->slices > 1 ? D3D11_UAV_DIMENSION_TEXTURE2DARRAY : D3D11_UAV_DIMENSION_TEXTURE2D; d.Texture2DArray.ArraySize = c->slices; }
    }
    if (d.Format == DXGI_FORMAT_UNKNOWN && !c->IsBuffer()) d.Format = c->format;
    D3D12_UNORDERED_ACCESS_VIEW_DESC s{};
    s.Format = d.Format;
    uint32_t m0 = 0, s0 = 0, sN = 1;
    switch (d.ViewDimension) {
    case D3D11_UAV_DIMENSION_BUFFER:
        s.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
        s.Buffer.FirstElement = d.Buffer.FirstElement; s.Buffer.NumElements = d.Buffer.NumElements;
        s.Buffer.StructureByteStride = d.Format == DXGI_FORMAT_UNKNOWN ? c->stride : 0;
        if (d.Buffer.Flags & D3D11_BUFFER_UAV_FLAG_RAW) { s.Buffer.Flags = D3D12_BUFFER_UAV_FLAG_RAW; s.Format = DXGI_FORMAT_R32_TYPELESS; s.Buffer.StructureByteStride = 0; }
        if (d.Buffer.Flags & (D3D11_BUFFER_UAV_FLAG_APPEND | D3D11_BUFFER_UAV_FLAG_COUNTER)) CountUnsupported(this, "UAV hidden counter (append, consume, counter)");
        break;
    case D3D11_UAV_DIMENSION_TEXTURE1D: s.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1D; s.Texture1D.MipSlice = m0 = d.Texture1D.MipSlice; break;
    case D3D11_UAV_DIMENSION_TEXTURE1DARRAY:
        s.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE1DARRAY; s.Texture1DArray.MipSlice = m0 = d.Texture1DArray.MipSlice;
        s.Texture1DArray.FirstArraySlice = s0 = d.Texture1DArray.FirstArraySlice; s.Texture1DArray.ArraySize = sN = d.Texture1DArray.ArraySize; break;
    case D3D11_UAV_DIMENSION_TEXTURE2D: s.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D; s.Texture2D.MipSlice = m0 = d.Texture2D.MipSlice; break;
    case D3D11_UAV_DIMENSION_TEXTURE2DARRAY:
        s.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2DARRAY; s.Texture2DArray.MipSlice = m0 = d.Texture2DArray.MipSlice;
        s.Texture2DArray.FirstArraySlice = s0 = d.Texture2DArray.FirstArraySlice; s.Texture2DArray.ArraySize = sN = d.Texture2DArray.ArraySize; break;
    case D3D11_UAV_DIMENSION_TEXTURE3D:
        s.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE3D; s.Texture3D.MipSlice = m0 = d.Texture3D.MipSlice;
        s.Texture3D.FirstWSlice = d.Texture3D.FirstWSlice; s.Texture3D.WSize = d.Texture3D.WSize; break;
    default: return E_INVALIDARG;
    }
    TUAV* v = NewView<TUAV>(this, c, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    v->desc = d; v->format = s.Format;
    SetRange(v->range, *c, m0, 1, s0, sN, 1);
    d3d->CreateUnorderedAccessView(c->Res(), nullptr, &s, v->h);
    *out = v;
    return S_OK;
}

}  // namespace raw::tx
