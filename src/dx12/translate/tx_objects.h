// The D3D11 child objects the translation layer hands to the application.
#pragma once
#include "tx_alloc.h"
#include "tx_track.h"
#include "tx_holds.h"

namespace raw::tx {

// How a resource's memory lives on D3D12.
enum class Store : uint8_t {
    Committed,   // default-heap resource with tracked states
    Renamed,     // an upload-heap chunk, swapped for a new one on every discard
    Staging,     // CPU-visible buffer; textures use a footprint layout inside it
};

struct ResCore {
    D3D11_RESOURCE_DIMENSION dim = D3D11_RESOURCE_DIMENSION_UNKNOWN;
    Store store = Store::Committed;
    D3D11_USAGE usage = D3D11_USAGE_DEFAULT;
    UINT bind = 0, cpu = 0, misc = 0;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    uint32_t width = 1, height = 1, depth = 1;
    uint32_t mips = 1, slices = 1, planes = 1, samples = 1, quality = 0;
    uint32_t stride = 0;                       // structured buffers
    uint64_t bytes = 0;                        // buffers: size; staging: total
    std::shared_ptr<TrackedRes> tr;            // Committed, Staging
    Chunk chunk;                               // Renamed: current contents
    std::vector<D3D12_PLACED_SUBRESOURCE_FOOTPRINT> fp;   // staging textures
    uint8_t* mapped = nullptr;                 // staging: persistent map
    Chunk mapChunk;                            // a write map in progress on a committed resource
    UINT mapSub = 0;
    ID3D11Resource* self = nullptr;
    void (*intRef)(ID3D11Resource*, int) = nullptr;
    ImmHold* imm = nullptr;                                  // the object's plain count (tx_holds.h)
    void (*hold)(BindHolds&, ID3D11Resource*) = nullptr;     // a plain hold of the typed object

    ID3D12Resource* Res() const { return tr ? tr->res.Get() : nullptr; }
    uint32_t Sub(uint32_t mip, uint32_t slice, uint32_t plane = 0) const
    {
        return mip + slice * mips + plane * mips * slices;
    }
    uint32_t SubCount() const { return mips * slices * planes; }
    bool IsBuffer() const { return dim == D3D11_RESOURCE_DIMENSION_BUFFER; }
};

void DestroyCore(Device* dev, ResCore& core);

template <class I, class D, D3D11_RESOURCE_DIMENSION Dim>
struct TRes final : Child<I> {
    ResCore core;
    D desc{};
    explicit TRes(Device* d) : Child<I>(d)
    {
        core.dim = Dim;
        core.self = this;
        core.intRef = [](ID3D11Resource* r, int delta) {
            auto* t = static_cast<TRes*>(r);
            if (delta > 0) t->IntAddRef(); else t->IntRelease();
        };
        core.imm = &this->imm;
        core.hold = [](BindHolds& h, ID3D11Resource* r) { h.Add(static_cast<TRes*>(static_cast<I*>(r))); };
    }
    ~TRes() override { DestroyCore(this->dev, core); }
    bool QIExtra(REFIID r, void** o) override
    {
        if (r != __uuidof(ID3D11Resource)) return false;
        *o = static_cast<ID3D11Resource*>(this);
        return true;
    }
    STDMETHODIMP_(void) GetType(D3D11_RESOURCE_DIMENSION* t) override { *t = Dim; }
    STDMETHODIMP_(void) SetEvictionPriority(UINT) override {}
    STDMETHODIMP_(UINT) GetEvictionPriority() override { return 0; }
    STDMETHODIMP_(void) GetDesc(D* d) override { *d = desc; }
};
using TBuffer = TRes<ID3D11Buffer, D3D11_BUFFER_DESC, D3D11_RESOURCE_DIMENSION_BUFFER>;
using TTex1D = TRes<ID3D11Texture1D, D3D11_TEXTURE1D_DESC, D3D11_RESOURCE_DIMENSION_TEXTURE1D>;
using TTex2D = TRes<ID3D11Texture2D, D3D11_TEXTURE2D_DESC, D3D11_RESOURCE_DIMENSION_TEXTURE2D>;
using TTex3D = TRes<ID3D11Texture3D, D3D11_TEXTURE3D_DESC, D3D11_RESOURCE_DIMENSION_TEXTURE3D>;

ResCore* CoreOf(ID3D11Resource* r);

template <class I, class D>
struct TView final : Child<I> {
    ResCore* core = nullptr;
    D3D12_CPU_DESCRIPTOR_HANDLE h{};
    D3D12_DESCRIPTOR_HEAP_TYPE heap = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    SubRange range;
    DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
    D desc{};
    uint64_t uid = 0;   // never reused (a descriptor slot or an address may be): the table cache's key
    explicit TView(Device* d) : Child<I>(d) {}
    ~TView() override;
    bool QIExtra(REFIID r, void** o) override
    {
        if (r != __uuidof(ID3D11View)) return false;
        *o = static_cast<ID3D11View*>(this);
        return true;
    }
    STDMETHODIMP_(void) GetResource(ID3D11Resource** r) override
    {
        *r = core->self;
        (*r)->AddRef();
    }
    STDMETHODIMP_(void) GetDesc(D* d) override { *d = desc; }
};
using TSRV = TView<ID3D11ShaderResourceView, D3D11_SHADER_RESOURCE_VIEW_DESC>;
using TRTV = TView<ID3D11RenderTargetView, D3D11_RENDER_TARGET_VIEW_DESC>;
using TDSV = TView<ID3D11DepthStencilView, D3D11_DEPTH_STENCIL_VIEW_DESC>;
using TUAV = TView<ID3D11UnorderedAccessView, D3D11_UNORDERED_ACCESS_VIEW_DESC>;

void FreeViewDescriptor(Device* dev, D3D12_DESCRIPTOR_HEAP_TYPE heap, D3D12_CPU_DESCRIPTOR_HANDLE h);
template <class I, class D>
TView<I, D>::~TView()
{
    FreeViewDescriptor(this->dev, heap, h);
    if (core) core->intRef(core->self, -1);
}

// What a shader binds, from its bytecode.
struct StageBind {   // 8 bytes with no padding: it is hashed as bytes
    uint16_t cbMask = 0;
    uint8_t srvCount = 0, sampCount = 0, uavCount = 0;
    uint8_t pad[3] = {};
};
struct ShaderData {
    std::vector<uint8_t> code;
    uint64_t hash = 0;
    uint32_t id = 0;
    StageBind bind;
    std::vector<uint8_t> srvDim;   // D3D12_SRV_DIMENSION per t-register
    std::vector<uint8_t> uavDim;   // D3D12_UAV_DIMENSION per u-register
    bool reflected = false;
};

template <class I>
struct TShader final : Child<I> {
    ShaderData* sd = nullptr;   // owned by the device
    explicit TShader(Device* d) : Child<I>(d) {}
};

// State objects carry the id of their deduplicated description.
template <class I, class D>
struct TState final : Child<I> {
    D desc{};
    uint32_t id = 0;
    explicit TState(Device* d) : Child<I>(d) {}
    STDMETHODIMP_(void) GetDesc(D* d) override { *d = desc; }
};
using TBlend = TState<ID3D11BlendState, D3D11_BLEND_DESC>;
using TDepthStencil = TState<ID3D11DepthStencilState, D3D11_DEPTH_STENCIL_DESC>;
using TRaster = TState<ID3D11RasterizerState, D3D11_RASTERIZER_DESC>;
using TSampler = TState<ID3D11SamplerState, D3D11_SAMPLER_DESC>;

struct TInputLayout final : Child<ID3D11InputLayout> {
    uint32_t id = 0;
    explicit TInputLayout(Device* d) : Child<ID3D11InputLayout>(d) {}
};

struct TQuery final : Child<ID3D11Predicate> {
    D3D11_QUERY_DESC desc{};
    uint32_t slot = 0;                    // index in the type's query heap
    std::atomic<uint64_t> serial{0};      // submission that holds the result; ~0: not submitted
    bool issued = false;
    explicit TQuery(Device* d) : Child<ID3D11Predicate>(d) {}
    ~TQuery() override;
    bool QIExtra(REFIID r, void** o) override
    {
        if (r != __uuidof(ID3D11Query) && r != __uuidof(ID3D11Asynchronous)) return false;
        *o = static_cast<ID3D11Query*>(this);
        return true;
    }
    STDMETHODIMP_(UINT) GetDataSize() override;
    STDMETHODIMP_(void) GetDesc(D3D11_QUERY_DESC* d) override { *d = desc; }
};

}  // namespace raw::tx
