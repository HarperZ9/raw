// Buffers and textures: D3D11 descriptions mapped onto D3D12 storage.
#include "tx_context.h"

namespace raw::tx {

ResCore* CoreOf(ID3D11Resource* r)
{
    if (!r) return nullptr;
    D3D11_RESOURCE_DIMENSION dim;
    r->GetType(&dim);
    switch (dim) {
    case D3D11_RESOURCE_DIMENSION_BUFFER: return &static_cast<TBuffer*>(r)->core;
    case D3D11_RESOURCE_DIMENSION_TEXTURE1D: return &static_cast<TTex1D*>(r)->core;
    case D3D11_RESOURCE_DIMENSION_TEXTURE2D: return &static_cast<TTex2D*>(r)->core;
    case D3D11_RESOURCE_DIMENSION_TEXTURE3D: return &static_cast<TTex3D*>(r)->core;
    default: return nullptr;
    }
}

void DestroyCore(Device* dev, ResCore& core)
{
    Retire r;
    if (core.store == Store::Renamed && core.chunk.res) r.chunks.push_back(core.chunk);
    if (core.mapChunk.res) r.chunks.push_back(core.mapChunk);
    if (core.tr) r.keep.push_back(std::move(core.tr));
    dev->RetireLater(std::move(r));
}

uint32_t Device::PlaneCount(DXGI_FORMAT f)
{
    std::lock_guard<std::mutex> g(cacheM_);
    auto it = planes_.find(f);
    if (it != planes_.end()) return it->second;
    D3D12_FEATURE_DATA_FORMAT_INFO fi{f, 0};
    uint32_t n = 1;
    if (SUCCEEDED(d3d->CheckFeatureSupport(D3D12_FEATURE_FORMAT_INFO, &fi, sizeof(fi))) && fi.PlaneCount) n = fi.PlaneCount;
    planes_.emplace(f, n);
    return n;
}

static D3D12_RESOURCE_DESC BufferDesc(uint64_t bytes, D3D12_RESOURCE_FLAGS flags)
{
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bytes ? bytes : 1; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR; rd.Flags = flags;
    return rd;
}

static HRESULT MakeTracked(Device* dev, const D3D12_RESOURCE_DESC& rd, bool cpuVisible, uint32_t subs, ResCore& core)
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_DEFAULT};
    if (cpuVisible) {
        hp.Type = D3D12_HEAP_TYPE_CUSTOM;
        hp.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_WRITE_BACK;
        hp.MemoryPoolPreference = D3D12_MEMORY_POOL_L0;
    }
    auto tr = std::make_shared<TrackedRes>();
    HRESULT hr = dev->d3d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&tr->res));
    if (FAILED(hr)) return hr;
    tr->subs = subs;
    tr->global.assign(subs, D3D12_RESOURCE_STATE_COMMON);
    core.tr = std::move(tr);
    if (cpuVisible) {
        void* p = nullptr;
        if (FAILED(hr = core.tr->res->Map(0, nullptr, &p))) return hr;
        core.mapped = static_cast<uint8_t*>(p);
    }
    return S_OK;
}

static D3D12_RESOURCE_FLAGS FlagsFor(UINT bind)
{
    D3D12_RESOURCE_FLAGS f = D3D12_RESOURCE_FLAG_NONE;
    if (bind & D3D11_BIND_RENDER_TARGET) f |= D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
    if (bind & D3D11_BIND_UNORDERED_ACCESS) f |= D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    if (bind & D3D11_BIND_DEPTH_STENCIL) {
        f |= D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
        if (!(bind & D3D11_BIND_SHADER_RESOURCE)) f |= D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE;
    }
    return f;
}

STDMETHODIMP Device::CreateBuffer(const D3D11_BUFFER_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Buffer** out)
{
    TX_TRACE("Device::CreateBuffer");
    if (!d || d->ByteWidth == 0) return E_INVALIDARG;
    if (!out) return S_FALSE;
    *out = nullptr;
    auto* b = new TBuffer(this);
    b->desc = *d;
    ResCore& c = b->core;
    c.usage = d->Usage; c.bind = d->BindFlags; c.cpu = d->CPUAccessFlags; c.misc = d->MiscFlags;
    c.bytes = d->ByteWidth; c.stride = d->StructureByteStride; c.width = d->ByteWidth;
    const UINT viewBinds = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_UNORDERED_ACCESS | D3D11_BIND_STREAM_OUTPUT;
    HRESULT hr = S_OK;
    if (d->Usage == D3D11_USAGE_STAGING) {
        c.store = Store::Staging;
        hr = MakeTracked(this, BufferDesc(c.bytes, D3D12_RESOURCE_FLAG_NONE), true, 1, c);
        if (SUCCEEDED(hr) && init && init->pSysMem) memcpy(c.mapped, init->pSysMem, c.bytes);
    } else if (d->BindFlags == D3D11_BIND_CONSTANT_BUFFER ||
               (d->Usage == D3D11_USAGE_DYNAMIC && !(d->BindFlags & viewBinds))) {
        c.store = Store::Renamed;
        int cls = Slab::ClassOf(c.bytes);
        if (cls < 0) c.chunk = slab.AllocDedicated(c.bytes);
        else { std::vector<Chunk> one; slab.Refill(cls, one, 1); if (!one.empty()) c.chunk = one[0]; }
        if (!c.chunk.res) hr = E_OUTOFMEMORY;
        else if (init && init->pSysMem) memcpy(c.chunk.cpu, init->pSysMem, c.bytes);
    } else {
        c.store = Store::Committed;
        hr = MakeTracked(this, BufferDesc(c.bytes, FlagsFor(d->BindFlags & D3D11_BIND_UNORDERED_ACCESS)), false, 1, c);
        if (SUCCEEDED(hr)) {
            c.chunk.res = c.Res();
            c.chunk.va = c.Res()->GetGPUVirtualAddress();
            c.chunk.size = d->ByteWidth;
            if (init && init->pSysMem) InitBufferData(c, init->pSysMem);
        }
    }
    if (FAILED(hr)) { b->Release(); return hr; }
    *out = b;
    return S_OK;
}

void Device::InitBufferData(ResCore& core, const void* data)
{
    std::lock_guard<std::mutex> g(initM_);
    Chunk ch = initChunks_.Alloc(core.bytes);
    if (!ch.res) return;
    memcpy(ch.cpu, data, core.bytes);
    init_.tracker.Require(core.tr, 0, D3D12_RESOURCE_STATE_COPY_DEST);
    init_.tracker.Flush(init_.cl.Get());
    init_.cl->CopyBufferRegion(core.Res(), 0, ch.res, ch.offset, core.bytes);
    init_.retire.chunks.push_back(ch);
    ++initCmds_;
}

static void MipSize(const ResCore& c, uint32_t mip, uint32_t& w, uint32_t& h, uint32_t& d)
{
    w = (c.width >> mip) ? (c.width >> mip) : 1;
    h = (c.height >> mip) ? (c.height >> mip) : 1;
    d = (c.depth >> mip) ? (c.depth >> mip) : 1;
}

void Device::InitTextureData(ResCore& core, const D3D11_SUBRESOURCE_DATA* data)
{
    std::lock_guard<std::mutex> g(initM_);
    for (uint32_t sub = 0; sub < core.mips * core.slices; ++sub) {
        if (!data[sub].pSysMem) continue;
        uint32_t w, h, d;
        MipSize(core, sub % core.mips, w, h, d);
        if (core.store == Store::Staging) {
            const auto& fp = core.fp[sub];
            UINT rows = 0; UINT64 rowBytes = 0;
            D3D12_RESOURCE_DESC one = BufferDesc(1, D3D12_RESOURCE_FLAG_NONE);
            one.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
            one.Width = fp.Footprint.Width; one.Height = fp.Footprint.Height; one.Format = fp.Footprint.Format;
            one.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
            d3d->GetCopyableFootprints(&one, 0, 1, 0, nullptr, &rows, &rowBytes, nullptr);
            for (uint32_t z = 0; z < d; ++z)
                for (UINT y = 0; y < rows; ++y)
                    memcpy(core.mapped + fp.Offset + (size_t(z) * rows + y) * fp.Footprint.RowPitch,
                           static_cast<const uint8_t*>(data[sub].pSysMem) + size_t(z) * data[sub].SysMemSlicePitch +
                               size_t(y) * data[sub].SysMemPitch, size_t(rowBytes));
            continue;
        }
        UploadRegion(this, init_, initChunks_, core, sub, 0, 0, 0, w, h, d,
                     data[sub].pSysMem, data[sub].SysMemPitch, data[sub].SysMemSlicePitch);
        ++initCmds_;
    }
}

// Shared by the three texture dimensions.
static HRESULT BuildTexture(Device* dev, ResCore& c, D3D12_RESOURCE_DIMENSION dim, UINT quality,
                            const D3D11_SUBRESOURCE_DATA* init)
{
    if (c.mips == 0) {
        uint32_t m = c.width > c.height ? c.width : c.height;
        if (c.depth > m) m = c.depth;
        while (m) { ++c.mips; m >>= 1; }
    }
    c.planes = dev->PlaneCount(c.format);
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = dim;
    rd.Width = c.width; rd.Height = c.height;
    rd.DepthOrArraySize = UINT16(dim == D3D12_RESOURCE_DIMENSION_TEXTURE3D ? c.depth : c.slices);
    rd.MipLevels = UINT16(c.mips); rd.Format = c.format;
    rd.SampleDesc.Count = c.samples; rd.SampleDesc.Quality = quality;
    rd.Flags = FlagsFor(c.bind);
    HRESULT hr;
    if (c.usage == D3D11_USAGE_STAGING) {
        c.store = Store::Staging;
        uint32_t n = c.mips * c.slices;   // plane 0 only: packed depth-stencil staging is not translated
        c.fp.resize(n);
        UINT64 total = 0;
        rd.Flags = D3D12_RESOURCE_FLAG_NONE;
        dev->d3d->GetCopyableFootprints(&rd, 0, n, 0, c.fp.data(), nullptr, nullptr, &total);
        c.bytes = total;
        hr = MakeTracked(dev, BufferDesc(total, D3D12_RESOURCE_FLAG_NONE), true, 1, c);
    } else {
        c.store = Store::Committed;
        hr = MakeTracked(dev, rd, false, c.SubCount(), c);
    }
    if (SUCCEEDED(hr) && init) dev->InitTextureData(c, init);
    return hr;
}

template <class T, class D>
static void FillCore(T* t, const D& d)
{
    t->desc = d;
    t->core.usage = d.Usage; t->core.bind = d.BindFlags; t->core.cpu = d.CPUAccessFlags; t->core.misc = d.MiscFlags;
    t->core.format = d.Format; t->core.width = d.Width; t->core.mips = d.MipLevels;
}

STDMETHODIMP Device::CreateTexture1D(const D3D11_TEXTURE1D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture1D** out)
{
    TX_TRACE("Device::CreateTexture1D");
    if (!d) return E_INVALIDARG;
    if (!out) return S_FALSE;
    *out = nullptr;
    auto* t = new TTex1D(this);
    FillCore(t, *d);
    t->core.slices = d->ArraySize;
    HRESULT hr = BuildTexture(this, t->core, D3D12_RESOURCE_DIMENSION_TEXTURE1D, 0, init);
    if (FAILED(hr)) { t->Release(); return hr; }
    t->desc.MipLevels = t->core.mips;
    *out = t;
    return S_OK;
}

STDMETHODIMP Device::CreateTexture2D(const D3D11_TEXTURE2D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture2D** out)
{
    TX_TRACE("Device::CreateTexture2D");
    if (!d) return E_INVALIDARG;
    if (!out) return S_FALSE;
    *out = nullptr;
    auto* t = new TTex2D(this);
    FillCore(t, *d);
    t->core.height = d->Height; t->core.slices = d->ArraySize; t->core.samples = d->SampleDesc.Count; t->core.quality = d->SampleDesc.Quality;
    HRESULT hr = BuildTexture(this, t->core, D3D12_RESOURCE_DIMENSION_TEXTURE2D, d->SampleDesc.Quality, init);
    if (FAILED(hr)) { t->Release(); return hr; }
    t->desc.MipLevels = t->core.mips;
    *out = t;
    return S_OK;
}

STDMETHODIMP Device::CreateTexture3D(const D3D11_TEXTURE3D_DESC* d, const D3D11_SUBRESOURCE_DATA* init, ID3D11Texture3D** out)
{
    TX_TRACE("Device::CreateTexture3D");
    if (!d) return E_INVALIDARG;
    if (!out) return S_FALSE;
    *out = nullptr;
    auto* t = new TTex3D(this);
    FillCore(t, *d);
    t->core.height = d->Height; t->core.depth = d->Depth;
    HRESULT hr = BuildTexture(this, t->core, D3D12_RESOURCE_DIMENSION_TEXTURE3D, 0, init);
    if (FAILED(hr)) { t->Release(); return hr; }
    t->desc.MipLevels = t->core.mips;
    *out = t;
    return S_OK;
}

void UploadRegion(Device* dev, Recording& rec, ChunkCache& chunks, ResCore& core, UINT sub,
                  UINT x, UINT y, UINT z, UINT w, UINT h, UINT d,
                  const void* data, UINT rowPitch, UINT depthPitch)
{
    D3D12_RESOURCE_DESC rd = BufferDesc(1, D3D12_RESOURCE_FLAG_NONE);
    rd.Dimension = d > 1 ? D3D12_RESOURCE_DIMENSION_TEXTURE3D : D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = w; rd.Height = h; rd.DepthOrArraySize = UINT16(d); rd.Format = core.format;
    rd.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};
    UINT rows = 0; UINT64 rowBytes = 0, total = 0;
    dev->d3d->GetCopyableFootprints(&rd, 0, 1, 0, &fp, &rows, &rowBytes, &total);
    Chunk ch = chunks.Alloc(total);
    if (!ch.res || total == UINT64(-1)) { Log("texture upload of %ux%ux%u failed", w, h, d); return; }
    if (!rowPitch) rowPitch = UINT(rowBytes);
    if (!depthPitch) depthPitch = rowPitch * rows;
    for (UINT zz = 0; zz < d; ++zz)
        for (UINT yy = 0; yy < rows; ++yy)
            memcpy(ch.cpu + (size_t(zz) * rows + yy) * fp.Footprint.RowPitch,
                   static_cast<const uint8_t*>(data) + size_t(zz) * depthPitch + size_t(yy) * rowPitch, size_t(rowBytes));
    fp.Offset = ch.offset;
    rec.tracker.Require(core.tr, sub, D3D12_RESOURCE_STATE_COPY_DEST);
    rec.tracker.Flush(rec.cl.Get());
    D3D12_TEXTURE_COPY_LOCATION dst{core.Res(), D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX};
    dst.SubresourceIndex = sub;
    D3D12_TEXTURE_COPY_LOCATION src{ch.res, D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT};
    src.PlacedFootprint = fp;
    rec.cl->CopyTextureRegion(&dst, x, y, z, &src, nullptr);
    rec.retire.chunks.push_back(ch);
    ++rec.cmdCount;
}

}  // namespace raw::tx
