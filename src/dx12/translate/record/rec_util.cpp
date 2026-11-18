#include "rec_util.h"
#include <atomic>

namespace raw::rec
{
namespace
{
// {5A3E0C61-91B7-4D52-A0F3-6C2B7E1D4401}..04: private to this library.
const GUID kGuidId      = {0x5a3e0c61, 0x91b7, 0x4d52, {0xa0, 0xf3, 0x6c, 0x2b, 0x7e, 0x1d, 0x44, 0x00}};
const GUID kIidBlob     = {0x5a3e0c61, 0x91b7, 0x4d52, {0xa0, 0xf3, 0x6c, 0x2b, 0x7e, 0x1d, 0x44, 0x05}};
std::atomic<u64> g_nextId{1};

class BlobObject final : public IUnknown
{
public:
    explicit BlobObject(std::vector<u8>&& b) : bytes(std::move(b)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override
    {
        if (!ppv) return E_POINTER;
        if (riid == IID_IUnknown || riid == kIidBlob) { *ppv = this; AddRef(); return S_OK; }
        *ppv = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++m_refs; }
    ULONG STDMETHODCALLTYPE Release() override
    {
        ULONG r = --m_refs;
        if (r == 0) delete this;
        return r;
    }
    std::vector<u8> bytes;

private:
    std::atomic<ULONG> m_refs{1};
};
} // namespace

const GUID kGuidBytecode  = {0x5a3e0c61, 0x91b7, 0x4d52, {0xa0, 0xf3, 0x6c, 0x2b, 0x7e, 0x1d, 0x44, 0x01}};
const GUID kGuidLayout    = {0x5a3e0c61, 0x91b7, 0x4d52, {0xa0, 0xf3, 0x6c, 0x2b, 0x7e, 0x1d, 0x44, 0x02}};
const GUID kGuidStreamOut = {0x5a3e0c61, 0x91b7, 0x4d52, {0xa0, 0xf3, 0x6c, 0x2b, 0x7e, 0x1d, 0x44, 0x03}};
const GUID kGuidRecorder  = {0x5a3e0c61, 0x91b7, 0x4d52, {0xa0, 0xf3, 0x6c, 0x2b, 0x7e, 0x1d, 0x44, 0x04}};

u64 GetId(ID3D11DeviceChild* o)
{
    u64 id = 0;
    UINT size = sizeof(id);
    if (!o || FAILED(o->GetPrivateData(kGuidId, &size, &id)) || size != sizeof(id)) return 0;
    return id;
}

u64 EnsureId(ID3D11DeviceChild* o)
{
    if (!o) return 0;
    u64 id = GetId(o);
    if (id) return id;
    id = g_nextId.fetch_add(1);
    o->SetPrivateData(kGuidId, sizeof(id), &id);
    return id;
}

u64 Fnv1a64(const void* data, size_t bytes)
{
    const u8* b = static_cast<const u8*>(data);
    u64 h = 14695981039346656037ULL;
    for (size_t i = 0; i < bytes; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}

void AttachBlob(ID3D11DeviceChild* o, const GUID& guid, std::vector<u8>&& bytes)
{
    if (!o) return;
    BlobObject* blob = new BlobObject(std::move(bytes));
    o->SetPrivateDataInterface(guid, blob);     // the object takes its own reference
    blob->Release();
}

IUnknown* FetchBlob(ID3D11DeviceChild* o, const GUID& guid, const u8** data, size_t* bytes)
{
    IUnknown* unk = nullptr;
    UINT size = sizeof(unk);
    if (!o || FAILED(o->GetPrivateData(guid, &size, &unk)) || !unk) return nullptr;
    void* self = nullptr;
    if (FAILED(unk->QueryInterface(kIidBlob, &self))) { unk->Release(); return nullptr; }
    unk->Release();                             // keep only the QueryInterface reference
    BlobObject* blob = static_cast<BlobObject*>(self);
    *data = blob->bytes.data();
    *bytes = blob->bytes.size();
    return blob;
}

FormatSize FormatInfo(DXGI_FORMAT format)
{
    const u32 f = static_cast<u32>(format);
    struct Range { u32 first, last, bytes, block; };
    static const Range ranges[] = {
        {1, 4, 16, 1},  {5, 8, 12, 1},  {9, 14, 8, 1},   {15, 22, 8, 1},  {23, 47, 4, 1},
        {48, 59, 2, 1}, {60, 65, 1, 1}, {67, 67, 4, 1},  {70, 72, 8, 4},  {73, 78, 16, 4},
        {79, 81, 8, 4}, {82, 84, 16, 4}, {85, 86, 2, 1}, {87, 93, 4, 1},  {94, 99, 16, 4},
        {115, 115, 2, 1}};
    for (const Range& r : ranges)
        if (f >= r.first && f <= r.last) return {r.bytes, r.block};
    return {0, 1};
}

bool GetTexDesc(ID3D11Resource* r, TexDesc* out)
{
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    r->GetType(&dim);
    TexDesc t;
    if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE1D) {
        D3D11_TEXTURE1D_DESC d{};
        static_cast<ID3D11Texture1D*>(r)->GetDesc(&d);
        t = {1, d.Width, 1, 1, d.MipLevels, d.ArraySize, static_cast<u32>(d.Format), 1, 0,
             static_cast<u32>(d.Usage), d.BindFlags, d.CPUAccessFlags, d.MiscFlags};
    } else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE2D) {
        D3D11_TEXTURE2D_DESC d{};
        static_cast<ID3D11Texture2D*>(r)->GetDesc(&d);
        t = {2, d.Width, d.Height, 1, d.MipLevels, d.ArraySize, static_cast<u32>(d.Format),
             d.SampleDesc.Count, d.SampleDesc.Quality, static_cast<u32>(d.Usage), d.BindFlags,
             d.CPUAccessFlags, d.MiscFlags};
    } else if (dim == D3D11_RESOURCE_DIMENSION_TEXTURE3D) {
        D3D11_TEXTURE3D_DESC d{};
        static_cast<ID3D11Texture3D*>(r)->GetDesc(&d);
        t = {3, d.Width, d.Height, d.Depth, d.MipLevels, 1, static_cast<u32>(d.Format), 1, 0,
             static_cast<u32>(d.Usage), d.BindFlags, d.CPUAccessFlags, d.MiscFlags};
    } else {
        return false;
    }
    *out = t;
    return true;
}

HRESULT CreateTexture(ID3D11Device* dev, const TexDesc& t, const D3D11_SUBRESOURCE_DATA* init,
                      ID3D11Resource** out)
{
    const DXGI_FORMAT fmt = static_cast<DXGI_FORMAT>(t.format);
    const D3D11_USAGE usage = static_cast<D3D11_USAGE>(t.usage);
    if (t.dim == 1) {
        D3D11_TEXTURE1D_DESC d{t.width, t.mips, t.arraySize, fmt, usage, t.bind, t.cpu, t.misc};
        return dev->CreateTexture1D(&d, init, reinterpret_cast<ID3D11Texture1D**>(out));
    }
    if (t.dim == 2) {
        D3D11_TEXTURE2D_DESC d{t.width, t.height, t.mips, t.arraySize, fmt,
                               {t.sampleCount, t.sampleQuality}, usage, t.bind, t.cpu, t.misc};
        return dev->CreateTexture2D(&d, init, reinterpret_cast<ID3D11Texture2D**>(out));
    }
    if (t.dim == 3) {
        D3D11_TEXTURE3D_DESC d{t.width, t.height, t.depth, t.mips, fmt, usage, t.bind, t.cpu, t.misc};
        return dev->CreateTexture3D(&d, init, reinterpret_cast<ID3D11Texture3D**>(out));
    }
    return E_INVALIDARG;
}

SubGeom TexSubGeom(const TexDesc& t, u32 subresource, const D3D11_BOX* box)
{
    SubGeom g;
    const FormatSize fs = FormatInfo(static_cast<DXGI_FORMAT>(t.format));
    if (fs.bytes == 0 || t.mips == 0) return g;
    const u32 mip = subresource % t.mips;
    u32 w = t.width >> mip, h = t.height >> mip, d = t.depth >> mip;
    if (w == 0) w = 1;
    if (h == 0) h = 1;
    if (d == 0) d = 1;
    if (box) {
        if (box->right < box->left || box->bottom < box->top || box->back < box->front) return g;
        w = box->right - box->left;
        h = t.dim >= 2 ? box->bottom - box->top : 1;
        d = t.dim == 3 ? box->back - box->front : 1;
    }
    g.rowBytes = ((w + fs.block - 1) / fs.block) * fs.bytes;
    g.rows = (h + fs.block - 1) / fs.block;
    g.slices = d;
    g.ok = true;
    return g;
}

SubGeom ResourceSubGeom(ID3D11Resource* r, u32 subresource, const D3D11_BOX* box)
{
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    r->GetType(&dim);
    if (dim == D3D11_RESOURCE_DIMENSION_BUFFER) {
        D3D11_BUFFER_DESC d{};
        static_cast<ID3D11Buffer*>(r)->GetDesc(&d);
        SubGeom g;
        if (box && box->right < box->left) return g;
        g.rowBytes = box ? box->right - box->left : d.ByteWidth;
        g.rows = g.slices = 1;
        g.ok = true;
        return g;
    }
    TexDesc t;
    if (!GetTexDesc(r, &t)) return {};
    return TexSubGeom(t, subresource, box);
}
} // namespace raw::rec
