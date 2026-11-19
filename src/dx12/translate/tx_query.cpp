// Query heaps and query objects.
#include "tx_context.h"

namespace raw::tx {

// ---- queries ----

HRESULT Device::InitQueries()
{
    static const struct { D3D12_QUERY_HEAP_TYPE type; uint32_t count, stride; } k[QH_Count] = {
        {D3D12_QUERY_HEAP_TYPE_OCCLUSION, 16384, 8},
        {D3D12_QUERY_HEAP_TYPE_TIMESTAMP, 4096, 8},
        {D3D12_QUERY_HEAP_TYPE_PIPELINE_STATISTICS, 512, sizeof(D3D12_QUERY_DATA_PIPELINE_STATISTICS)},
    };
    for (int i = 0; i < QH_Count; ++i) {
        D3D12_QUERY_HEAP_DESC hd{k[i].type, k[i].count, 0};
        HRESULT hr = d3d->CreateQueryHeap(&hd, IID_PPV_ARGS(&qh[i].heap));
        if (FAILED(hr)) return hr;
        D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_READBACK};
        D3D12_RESOURCE_DESC rd{};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
        rd.Width = UINT64(k[i].count) * k[i].stride; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = d3d->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&qh[i].readback));
        if (FAILED(hr)) return hr;
        void* p = nullptr;
        qh[i].readback->Map(0, nullptr, &p);
        qh[i].mapped = static_cast<uint8_t*>(p);
        qh[i].stride = k[i].stride;
        for (uint32_t s = k[i].count; s-- > 0;) qh[i].free.push_back(s);
    }
    return S_OK;
}

static int HeapOf(D3D11_QUERY q)
{
    switch (q) {
    case D3D11_QUERY_OCCLUSION: case D3D11_QUERY_OCCLUSION_PREDICATE: return QH_Occlusion;
    case D3D11_QUERY_TIMESTAMP: return QH_Timestamp;
    case D3D11_QUERY_PIPELINE_STATISTICS: return QH_Stats;
    default: return -1;
    }
}

uint32_t Device::AllocQuerySlot(QueryHeapKind k)
{
    std::lock_guard<std::mutex> g(queryM);
    if (qh[k].free.empty()) return ~0u;
    uint32_t s = qh[k].free.back();
    qh[k].free.pop_back();
    return s;
}

void Device::FreeQuerySlot(QueryHeapKind k, uint32_t slot)
{
    std::lock_guard<std::mutex> g(queryM);
    qh[k].free.push_back(slot);
}

TQuery::~TQuery()
{
    int h = HeapOf(desc.Query);
    if (h >= 0 && slot != ~0u) dev->FreeQuerySlot(static_cast<QueryHeapKind>(h), slot);
}

STDMETHODIMP_(UINT) TQuery::GetDataSize()
{
    switch (desc.Query) {
    case D3D11_QUERY_EVENT: case D3D11_QUERY_OCCLUSION_PREDICATE: return sizeof(BOOL);
    case D3D11_QUERY_TIMESTAMP_DISJOINT: return sizeof(D3D11_QUERY_DATA_TIMESTAMP_DISJOINT);
    case D3D11_QUERY_PIPELINE_STATISTICS: return sizeof(D3D11_QUERY_DATA_PIPELINE_STATISTICS);
    default: return sizeof(UINT64);
    }
}

STDMETHODIMP Device::CreateQuery(const D3D11_QUERY_DESC* d, ID3D11Query** out)
{
    TX_TRACE("Device::CreateQuery");
    if (!d) return E_INVALIDARG;
    if (d->Query >= D3D11_QUERY_SO_STATISTICS) {
        CountUnsupported(this, "stream-output statistics queries");
        return E_NOTIMPL;
    }
    if (!out) return S_FALSE;
    auto* q = new TQuery(this);
    q->desc = *d;
    q->slot = ~0u;
    int h = HeapOf(d->Query);
    if (h >= 0) {
        q->slot = AllocQuerySlot(static_cast<QueryHeapKind>(h));
        if (q->slot == ~0u) { q->Release(); return E_OUTOFMEMORY; }
    }
    *out = q;
    return S_OK;
}

STDMETHODIMP Device::CreatePredicate(const D3D11_QUERY_DESC* d, ID3D11Predicate** out)
{
    TX_TRACE("Device::CreatePredicate");
    if (!d || d->Query != D3D11_QUERY_OCCLUSION_PREDICATE) return E_INVALIDARG;
    ID3D11Query* q = nullptr;
    HRESULT hr = CreateQuery(d, out ? &q : nullptr);
    if (out && SUCCEEDED(hr)) *out = static_cast<TQuery*>(q);
    return hr;
}

}  // namespace raw::tx
