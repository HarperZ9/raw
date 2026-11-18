// Memory and descriptor allocators.
//   Slab        upload-heap chunks in power-of-two classes; backs constant
//               buffers, dynamic buffers and copy scratch
//   ChunkCache  per-context front end of the slab, lock-free on the common path
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
    uint8_t cls = 0;   // size class; kDedicated: its own resource
};

class Slab {
public:
    static constexpr int kClasses = 11;          // 256 B .. 256 KiB
    static constexpr uint8_t kDedicated = 0xFF;
    void Init(ID3D12Device* d) { dev_ = d; }
    static int ClassOf(uint64_t bytes);          // -1: dedicated
    // Moves up to `n` free chunks of class `cls` into `out`.
    void Refill(int cls, std::vector<Chunk>& out, int n);
    Chunk AllocDedicated(uint64_t bytes);
    void Free(const Chunk* c, size_t n);
    void Shutdown();
private:
    void GrowLocked(int cls);
    ID3D12Device* dev_ = nullptr;
    std::mutex m_;
    std::vector<Chunk> free_[kClasses];
    std::vector<ID3D12Resource*> pages_;
};

class ChunkCache {
public:
    void Init(Slab* s) { slab_ = s; }
    Chunk Alloc(uint64_t bytes);
    void ReturnAll();
private:
    Slab* slab_ = nullptr;
    std::vector<Chunk> cache_[Slab::kClasses];
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
