#include "tx_alloc.h"

namespace raw::tx {

static constexpr uint64_t kPageBytes = 1ull << 20;

static ID3D12Resource* MakeUpload(ID3D12Device* dev, uint64_t bytes)
{
    D3D12_HEAP_PROPERTIES hp{D3D12_HEAP_TYPE_UPLOAD};
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Width = bytes; rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* r = nullptr;
    HRESULT hr = dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd,
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr, IID_PPV_ARGS(&r));
    if (FAILED(hr)) { Log("upload buffer of %llu bytes failed: 0x%08X", bytes, hr); return nullptr; }
    return r;
}

int Slab::ClassOf(uint64_t bytes)
{
    uint64_t s = 256;
    for (int c = 0; c < kClasses; ++c, s <<= 1)
        if (bytes <= s) return c;
    return -1;
}

void Slab::GrowLocked(int cls)
{
    uint64_t size = 256ull << cls;
    uint64_t pageBytes = size > kPageBytes ? size : kPageBytes;
    ID3D12Resource* r = MakeUpload(dev_, pageBytes);
    if (!r) return;
    void* p = nullptr;
    D3D12_RANGE none{0, 0};
    r->Map(0, &none, &p);
    pages_.push_back(r);
    D3D12_GPU_VIRTUAL_ADDRESS va = r->GetGPUVirtualAddress();
    for (uint64_t off = 0; off + size <= pageBytes; off += size) {
        Chunk c;
        c.res = r; c.offset = off; c.cpu = static_cast<uint8_t*>(p) + off;
        c.va = va + off; c.size = static_cast<uint32_t>(size); c.cls = static_cast<uint8_t>(cls);
        free_[cls].push_back(c);
    }
}

void Slab::Refill(int cls, std::vector<Chunk>& out, int n)
{
    std::lock_guard<std::mutex> g(m_);
    auto& f = free_[cls];
    if (f.empty()) GrowLocked(cls);
    for (int i = 0; i < n && !f.empty(); ++i) { out.push_back(f.back()); f.pop_back(); }
}

Chunk Slab::AllocDedicated(uint64_t bytes)
{
    Chunk c;
    ID3D12Resource* r = MakeUpload(dev_, bytes);
    if (!r) return c;
    void* p = nullptr;
    D3D12_RANGE none{0, 0};
    r->Map(0, &none, &p);
    c.res = r; c.cpu = static_cast<uint8_t*>(p); c.va = r->GetGPUVirtualAddress();
    c.size = static_cast<uint32_t>(bytes > 0xFFFFFFFFull ? 0xFFFFFFFFull : bytes);
    c.cls = kDedicated;
    return c;
}

void Slab::Free(const Chunk* c, size_t n)
{
    std::lock_guard<std::mutex> g(m_);
    for (size_t i = 0; i < n; ++i) {
        if (!c[i].res) continue;
        if (c[i].cls == kDedicated) c[i].res->Release();
        else free_[c[i].cls].push_back(c[i]);
    }
}

void Slab::Shutdown()
{
    for (auto* p : pages_) p->Release();
    pages_.clear();
    for (auto& f : free_) f.clear();
}

Chunk ChunkCache::Alloc(uint64_t bytes)
{
    int cls = Slab::ClassOf(bytes);
    if (cls < 0) return slab_->AllocDedicated(bytes);
    auto& c = cache_[cls];
    if (c.empty()) slab_->Refill(cls, c, cls < 4 ? 64 : 4);
    if (c.empty()) return Chunk{};
    Chunk out = c.back();
    c.pop_back();
    return out;
}

void ChunkCache::ReturnAll()
{
    for (auto& c : cache_) {
        if (!c.empty()) slab_->Free(c.data(), c.size());
        c.clear();
    }
}

void CpuDescs::Init(ID3D12Device* d, D3D12_DESCRIPTOR_HEAP_TYPE t)
{
    dev_ = d; type_ = t;
    inc_ = d->GetDescriptorHandleIncrementSize(t);
}

D3D12_CPU_DESCRIPTOR_HANDLE CpuDescs::Alloc()
{
    std::lock_guard<std::mutex> g(m_);
    if (free_.empty()) {
        D3D12_DESCRIPTOR_HEAP_DESC hd{type_, 1024, D3D12_DESCRIPTOR_HEAP_FLAG_NONE, 0};
        ID3D12DescriptorHeap* h = nullptr;
        if (FAILED(dev_->CreateDescriptorHeap(&hd, IID_PPV_ARGS(&h)))) return {};
        heaps_.push_back(h);
        SIZE_T base = h->GetCPUDescriptorHandleForHeapStart().ptr;
        for (UINT i = 0; i < 1024; ++i) free_.push_back(base + SIZE_T(1023 - i) * inc_);
    }
    D3D12_CPU_DESCRIPTOR_HANDLE h{free_.back()};
    free_.pop_back();
    return h;
}

void CpuDescs::Free(D3D12_CPU_DESCRIPTOR_HANDLE h)
{
    if (!h.ptr) return;
    std::lock_guard<std::mutex> g(m_);
    free_.push_back(h.ptr);
}

void CpuDescs::Shutdown()
{
    for (auto* h : heaps_) h->Release();
    heaps_.clear();
    free_.clear();
}

}  // namespace raw::tx
