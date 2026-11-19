// Memory and descriptor allocators.
//   Slab        upload-heap chunks in power-of-two classes; backs constant
//               buffers, dynamic buffers and copy scratch
//   ChunkCache  per-context front end of the slab, lock-free on the common path; the immediate context's
//               also owns a linear ring for buffer renames
//   RingPage    a persistently mapped upload page the ring bumps through
//   CpuDescs    free list of CPU descriptors for views
//   Retire      things to give back once a submission has finished on the GPU
#pragma once
#include "tx_common.h"

namespace raw::tx {

struct Chunk {
    ID3D12Resource* res = nullptr;
    uint64_t offset = 0;
    uint8_t* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS va = 0;
    uint32_t size = 0;
    uint8_t cls = 0;   // size class; kDedicated: its own resource; kRing: part of a ring page
    uint16_t page = 0; // kRing: the page index
};

// A ring page goes back to the free pages once the ring has moved past it and every chunk cut from it
// has been freed. A chunk is freed only after the GPU finished with it (the retire queue), and a buffer's
// current contents count as a live chunk, so a page under a buffer that is never written again stays.
// The cutting side counts in the ChunkCache with no atomic: a locked instruction there made every rename
// wait for the write-combined bytes of the last upload to drain. `cut`, `freed` and `active` change only
// under the slab's lock.
struct RingPage {
    ID3D12Resource* res = nullptr;
    uint8_t* cpu = nullptr;
    D3D12_GPU_VIRTUAL_ADDRESS va = 0;
    uint64_t size = 0;
    uint32_t cut = 0;      // chunks cut from it, known once it stops being cut from
    uint32_t freed = 0;    // chunks given back
    bool active = false;   // the ring is still cutting from it
};

class Slab {
public:
    static constexpr int kClasses = 11;          // 256 B .. 256 KiB
    static constexpr uint8_t kDedicated = 0xFF;
    static constexpr uint8_t kRing = 0xFE;
    static constexpr uint64_t kRingPageBytes = 4ull << 20;
    static constexpr uint64_t kRingMax = 64ull << 10;   // larger renames take slab chunks
    void Init(ID3D12Device* d, bool ringWriteBack = false) { dev_ = d; ringWriteBack_ = ringWriteBack; }
    static int ClassOf(uint64_t bytes);          // -1: dedicated
    // Moves up to `n` free chunks of class `cls` into `out`.
    void Refill(int cls, std::vector<Chunk>& out, int n);
    Chunk AllocDedicated(uint64_t bytes);
    void Free(const Chunk* c, size_t n);
    // The ring's next page: `prev` (or null) stops being cut from; returns a free page or a new one.
    RingPage* NextRingPage(RingPage* prev, uint16_t* index, uint32_t prevCut);
    void EndRingPage(RingPage* p, uint16_t index, uint32_t cut);
    size_t RingPages() { std::lock_guard<std::mutex> g(m_); return ring_.size(); }
    void Shutdown();
private:
    void GrowLocked(int cls);
    void RetirePageLocked(uint16_t index);
    void EndRingPageLocked(RingPage* p, uint16_t index, uint32_t cut);
    std::vector<std::unique_ptr<RingPage>> ring_;
    bool ringWriteBack_ = false;
    std::vector<uint16_t> ringFree_;
    ID3D12Device* dev_ = nullptr;
    std::mutex m_;
    std::vector<Chunk> free_[kClasses];
    std::vector<ID3D12Resource*> pages_;
};

class ChunkCache {
public:
    void Init(Slab* s, bool ring = false) { slab_ = s; ring_ = ring; }
    Chunk Alloc(uint64_t bytes);
    // Memory for a buffer rename: a bump in the ring when this cache has one (256-byte aligned, as a
    // constant buffer view needs), otherwise a slab chunk.
    Chunk AllocRename(uint64_t bytes)
    {
        if (!ring_ || bytes > Slab::kRingMax) return Alloc(bytes);
        const uint64_t n = (bytes + 255) & ~uint64_t(255);
        if (!page_ || off_ + n > page_->size) {
            page_ = slab_->NextRingPage(page_, &pageIndex_, cut_);
            off_ = 0;
            cut_ = 0;
            if (!page_) return Alloc(bytes);
        }
        Chunk c;
        c.res = page_->res; c.offset = off_; c.cpu = page_->cpu + off_; c.va = page_->va + off_;
        c.size = uint32_t(n); c.cls = Slab::kRing; c.page = pageIndex_;
        ++cut_;
        off_ += n;
        return c;
    }
    void ReturnAll();
private:
    Slab* slab_ = nullptr;
    std::vector<Chunk> cache_[Slab::kClasses];
    bool ring_ = false;
    RingPage* page_ = nullptr;
    uint16_t pageIndex_ = 0;
    uint64_t off_ = 0;
    uint32_t cut_ = 0;   // chunks cut from page_ so far
};

class CpuDescs {
public:
    void Init(ID3D12Device* d, D3D12_DESCRIPTOR_HEAP_TYPE t);
    D3D12_CPU_DESCRIPTOR_HANDLE Alloc();
    void Free(D3D12_CPU_DESCRIPTOR_HANDLE h);
    void Shutdown();
private:
    ID3D12Device* dev_ = nullptr;
    D3D12_DESCRIPTOR_HEAP_TYPE type_{};
    UINT inc_ = 0;
    std::mutex m_;
    std::vector<ID3D12DescriptorHeap*> heaps_;
    std::vector<SIZE_T> free_;
};

struct TrackedRes;
struct Retire {
    uint64_t serial = 0;
    std::vector<Chunk> chunks;
    std::vector<uint32_t> descPages;
    std::vector<std::shared_ptr<TrackedRes>> keep;
    std::vector<ComPtr<IUnknown>> objs;
    bool Empty() const
    {
        return chunks.empty() && descPages.empty() && keep.empty() && objs.empty();
    }
};

}  // namespace raw::tx
