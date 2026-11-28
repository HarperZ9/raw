#pragma once
// raw_slab_allocator.h: First-Principles Lock-Free Treiber-Stack Slab Allocator & IAllocatorBackend (`raw::core`).
// Replaces Creation Engine's contended MemoryManager (0x369A810), ScrapHeap (0x1A21930), and 37-slot
// LoadedLandData spinlock pool with:
//   - 10 cache-line-aligned (64B) ABA-immune lock-free Treiber-stack size-class slab bins
//     (16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 16384 bytes)
//   - Direct aligned fallback for >16 KB or >32B alignment allocations
//   - Triple-buffered lock-free per-frame linear scratch arenas (AllocDomain::FrameLinear)
//   - 64-bit header & footer corruption canaries + O(1) double-free & foreign-pointer detection
//   - Full atomic live/high-water/CAS-contention telemetry implementing raw::ce_rebuild::IAllocatorBackend.
// Original work, MIT.

#include "ce_rebuild_arch.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace raw::core {

inline constexpr size_t kSlabBinCount = 10u;
inline constexpr std::array<size_t, kSlabBinCount> kSlabBinSizes = {
    16u, 32u, 64u, 128u, 256u, 512u, 1024u, 2048u, 4096u, 16384u
};

struct SlabAllocatorMetrics {
    uint64_t liveAllocations      = 0ull;
    uint64_t liveBytes            = 0ull;
    uint64_t highWaterBytes       = 0ull;
    uint64_t totalAllocations     = 0ull;
    uint64_t totalDeallocations   = 0ull;
    uint64_t slabHits             = 0ull;
    uint64_t largeFallbacks       = 0ull;
    uint64_t casRetries           = 0ull;
    uint64_t canaryCorruptions    = 0ull;
    uint64_t doubleFreeRejects    = 0ull;
    uint64_t foreignPtrRejects    = 0ull;
    uint64_t slabPagesAllocated   = 0ull;
};

enum class FreeResult : uint8_t {
    Ok                = 0,
    NullPointer       = 1,
    ForeignPointer    = 2,
    DoubleFree        = 3,
    CanaryCorrupted   = 4,
    FrameLinearNoOp   = 5,
};

class RawSlabAllocator final : public raw::ce_rebuild::IAllocatorBackend {
public:
    static constexpr uint32_t kFrameArenaSlots = 3u;
    static constexpr size_t   kDefaultFrameArenaBytes = 4u * 1024u * 1024u; // 4 MB per frame slot

    explicit RawSlabAllocator(size_t frameArenaBytesPerSlot = kDefaultFrameArenaBytes);
    ~RawSlabAllocator() override;

    RawSlabAllocator(const RawSlabAllocator&) = delete;
    RawSlabAllocator& operator=(const RawSlabAllocator&) = delete;

    // raw::ce_rebuild::IAllocatorBackend interface:
    [[nodiscard]] void* Allocate(raw::ce_rebuild::AllocDomain domain,
                                 size_t sizeBytes,
                                 size_t alignment = 16u) override;
    void Deallocate(raw::ce_rebuild::AllocDomain domain, void* ptr) override;
    void ResetFrameArena(uint32_t frameSlotIndex) override;
    [[nodiscard]] raw::ce_rebuild::AllocatorStats QueryStats(raw::ce_rebuild::AllocDomain domain) const override;

    // Direct convenience & diagnostic API:
    [[nodiscard]] void* Alloc(size_t sizeBytes, size_t alignment = 16u,
                              raw::ce_rebuild::AllocDomain domain = raw::ce_rebuild::AllocDomain::Simulation);
    FreeResult FreeChecked(void* ptr);
    [[nodiscard]] bool ValidatePointer(const void* ptr) const noexcept;
    [[nodiscard]] SlabAllocatorMetrics GetMetrics() const noexcept;

private:
    static constexpr uint32_t kMaxPagesPerBin = 4096u;
    static constexpr uint32_t kEmptySlotIndex = 0xFFFFFFFFu;

#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4324) // Intentional cache-line padding for lock-free false-sharing elimination
#endif
    struct alignas(64) SlabBin {
        size_t                          payloadSize  = 0u;
        size_t                          slotStride   = 0u;
        uint32_t                        slotsPerPage = 0u;
        std::atomic<uint64_t>           taggedHead{0x00000000FFFFFFFFull}; // high32=ABA tag, low32=slotIdx
        std::atomic<uint32_t>           pageCount{0u};
        std::atomic<uint8_t*>           pages[kMaxPagesPerBin]{};
        std::mutex                      growMutex;
    };

    struct alignas(64) FrameLinearArena {
        uint8_t*             buffer   = nullptr;
        size_t               capacity = 0u;
        std::atomic<size_t>  offset{0u};
        std::atomic<uint64_t> allocCount{0ull};
        std::atomic<uint64_t> resetCount{0ull};
    };

    struct alignas(64) DomainCounters {
        std::atomic<uint64_t> bytesInUse{0ull};
        std::atomic<uint64_t> activeAllocs{0ull};
    };
#ifdef _MSC_VER
#pragma warning(pop)
#endif

    bool GrowBin(uint32_t binIdx);
    void* PopFromBin(uint32_t binIdx, uint32_t* outSlotIdx);
    void PushToBin(uint32_t binIdx, uint32_t slotIdx);
    uint8_t* SlotAddress(const SlabBin& bin, uint32_t slotIdx) const noexcept;
    bool IsInsideFrameArena(const void* ptr) const noexcept;
    void UpdateHighWater(uint64_t liveBytes) noexcept;

    uint32_t                            m_ownerTag = 0u;
    std::array<SlabBin, kSlabBinCount>  m_bins{};
    std::array<FrameLinearArena, kFrameArenaSlots> m_frameArenas{};
    std::atomic<uint32_t>               m_activeFrameSlot{0u};
    std::array<DomainCounters, 4>       m_domainStats{};

    alignas(64) std::atomic<uint64_t>   m_liveAllocs{0ull};
    std::atomic<uint64_t>               m_liveBytes{0ull};
    std::atomic<uint64_t>               m_highWaterBytes{0ull};
    std::atomic<uint64_t>               m_totalAllocs{0ull};
    std::atomic<uint64_t>               m_totalDeallocs{0ull};
    std::atomic<uint64_t>               m_slabHits{0ull};
    std::atomic<uint64_t>               m_largeFallbacks{0ull};
    std::atomic<uint64_t>               m_casRetries{0ull};
    std::atomic<uint64_t>               m_canaryCorruptions{0ull};
    std::atomic<uint64_t>               m_doubleFreeRejects{0ull};
    std::atomic<uint64_t>               m_foreignPtrRejects{0ull};
    std::atomic<uint64_t>               m_slabPagesAllocated{0ull};
    std::atomic<uint64_t>               m_slabBytesReserved{0ull};
};

}  // namespace raw::core
