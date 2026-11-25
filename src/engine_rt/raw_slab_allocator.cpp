// raw_slab_allocator.cpp: First-Principles Lock-Free Treiber-Stack Slab Allocator (`raw::core`).
// Original work, MIT.

#include "raw_slab_allocator.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace raw::core {
namespace {

constexpr uint64_t kLiveHeaderCanary = 0x524157534C414231ull; // "RAWSLAB1"
constexpr uint64_t kFreeHeaderCanary = 0x46524545534C4142ull; // "FREESLAB"
constexpr uint64_t kFooterCanary     = 0x524157454E444341ull; // "RAWENDCA"
constexpr uint32_t kLargeBinSentinel = 0xFFFFFFFFu;
constexpr size_t   kMinSlotAlign     = 32u;

// 32-byte aligned block header stored immediately preceding every returned user pointer.
struct alignas(32) BlockHeader {
    uint64_t headerCanary;
    uint32_t ownerTag;
    uint32_t binIndex;
    uint32_t slotIndex;
    uint32_t domainAndState; // low 8 bits = AllocDomain, bit 31 = live
    uint64_t requestedSize;
};
static_assert(sizeof(BlockHeader) == 32u, "BlockHeader must be 32 bytes for 32B alignment");

constexpr uint32_t kStateLiveBit = 0x80000000u;

inline size_t AlignUp(size_t v, size_t a) noexcept {
    return (v + (a - 1u)) & ~(a - 1u);
}

inline void* AlignedOsAlloc(size_t bytes, size_t alignment) noexcept {
    const size_t effAlign = std::max<size_t>(alignment, 64u);
    const size_t effBytes = AlignUp(std::max<size_t>(bytes, effAlign), effAlign);
#if defined(_MSC_VER)
    return _aligned_malloc(effBytes, effAlign);
#else
    return std::aligned_alloc(effAlign, effBytes);
#endif
}

inline void AlignedOsFree(void* p) noexcept {
    if (!p) return;
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}

inline uint64_t PackTaggedHead(uint32_t tag, uint32_t slotIdx) noexcept {
    return (uint64_t(tag) << 32) | uint64_t(slotIdx);
}

inline uint32_t UnpackTag(uint64_t packed) noexcept {
    return static_cast<uint32_t>(packed >> 32);
}

inline uint32_t UnpackSlot(uint64_t packed) noexcept {
    return static_cast<uint32_t>(packed & 0xFFFFFFFFull);
}

uint32_t NextOwnerTag() noexcept {
    static std::atomic<uint32_t> s_seq{0x41C64E6Du};
    return s_seq.fetch_add(0x9E3779B9u, std::memory_order_relaxed) | 1u;
}

uint32_t FindBinForSize(size_t sizeBytes) noexcept {
    for (uint32_t i = 0; i < kSlabBinCount; ++i) {
        if (sizeBytes <= kSlabBinSizes[i]) return i;
    }
    return kLargeBinSentinel;
}

}  // namespace

RawSlabAllocator::RawSlabAllocator(size_t frameArenaBytesPerSlot)
    : m_ownerTag(NextOwnerTag()) {
    // Configure 10 cache-line-aligned size-class slab bins.
    for (uint32_t i = 0; i < kSlabBinCount; ++i) {
        SlabBin& bin = m_bins[i];
        bin.payloadSize = kSlabBinSizes[i];
        // Each slot holds [32B BlockHeader][payloadSize][8B footerCanary], rounded to 32B boundary.
        bin.slotStride = AlignUp(sizeof(BlockHeader) + bin.payloadSize + sizeof(uint64_t), kMinSlotAlign);
        // Target ~64 KB per slab page (at least 4 slots for 16 KB bin, capped at 2048 slots/page for small bins).
        const size_t targetPageBytes = std::max<size_t>(65536u, bin.slotStride * 4u);
        bin.slotsPerPage = static_cast<uint32_t>(std::clamp<size_t>(targetPageBytes / bin.slotStride, 4u, 2048u));
        bin.taggedHead.store(PackTaggedHead(1u, kEmptySlotIndex), std::memory_order_relaxed);
        bin.pageCount.store(0u, std::memory_order_relaxed);
        for (uint32_t p = 0; p < kMaxPagesPerBin; ++p) {
            bin.pages[p].store(nullptr, std::memory_order_relaxed);
        }
    }

    // Allocate triple-buffered per-frame linear scratch arenas (64B aligned).
    const size_t arenaCap = AlignUp(std::max<size_t>(frameArenaBytesPerSlot, 65536u), 64u);
    for (uint32_t s = 0; s < kFrameArenaSlots; ++s) {
        m_frameArenas[s].buffer = static_cast<uint8_t*>(AlignedOsAlloc(arenaCap, 64u));
        m_frameArenas[s].capacity = m_frameArenas[s].buffer ? arenaCap : 0u;
        m_frameArenas[s].offset.store(0u, std::memory_order_relaxed);
    }
}

RawSlabAllocator::~RawSlabAllocator() {
    for (uint32_t i = 0; i < kSlabBinCount; ++i) {
        SlabBin& bin = m_bins[i];
        const uint32_t pc = bin.pageCount.load(std::memory_order_acquire);
        for (uint32_t p = 0; p < pc && p < kMaxPagesPerBin; ++p) {
            uint8_t* page = bin.pages[p].exchange(nullptr, std::memory_order_acq_rel);
            AlignedOsFree(page);
        }
    }
    for (uint32_t s = 0; s < kFrameArenaSlots; ++s) {
        AlignedOsFree(m_frameArenas[s].buffer);
        m_frameArenas[s].buffer = nullptr;
        m_frameArenas[s].capacity = 0u;
    }
}

uint8_t* RawSlabAllocator::SlotAddress(const SlabBin& bin, uint32_t slotIdx) const noexcept {
    const uint32_t pageIdx = slotIdx >> 12;
    const uint32_t inPage  = slotIdx & 0xFFFu;
    if (pageIdx >= kMaxPagesPerBin || inPage >= bin.slotsPerPage) return nullptr;
    uint8_t* page = bin.pages[pageIdx].load(std::memory_order_acquire);
    return page ? (page + size_t(inPage) * bin.slotStride) : nullptr;
}

bool RawSlabAllocator::GrowBin(uint32_t binIdx) {
    SlabBin& bin = m_bins[binIdx];
    std::lock_guard<std::mutex> lk(bin.growMutex);
    // Double-check if another thread already replenished the lock-free list while we waited.
    if (UnpackSlot(bin.taggedHead.load(std::memory_order_acquire)) != kEmptySlotIndex) {
        return true;
    }
    const uint32_t pageIdx = bin.pageCount.load(std::memory_order_relaxed);
    if (pageIdx >= kMaxPagesPerBin) return false;

    const size_t pageBytes = size_t(bin.slotsPerPage) * bin.slotStride;
    auto* page = static_cast<uint8_t*>(AlignedOsAlloc(pageBytes, 64u));
    if (!page) return false;

    // Initialize every slot in the new page as a clean free node linked via nextSlotIdx inside user payload.
    for (uint32_t s = 0; s < bin.slotsPerPage; ++s) {
        uint8_t* slotPtr = page + size_t(s) * bin.slotStride;
        auto* hdr = reinterpret_cast<BlockHeader*>(slotPtr);
        hdr->headerCanary   = kFreeHeaderCanary;
        hdr->ownerTag       = m_ownerTag;
        hdr->binIndex       = binIdx;
        hdr->slotIndex      = (pageIdx << 12) | s;
        hdr->domainAndState = 0u;
        hdr->requestedSize  = 0ull;

        const uint32_t nextIdx = (s + 1u < bin.slotsPerPage) ? ((pageIdx << 12) | (s + 1u)) : kEmptySlotIndex;
        std::memcpy(slotPtr + sizeof(BlockHeader), &nextIdx, sizeof(nextIdx));
    }

    bin.pages[pageIdx].store(page, std::memory_order_release);
    bin.pageCount.store(pageIdx + 1u, std::memory_order_release);
    m_slabPagesAllocated.fetch_add(1ull, std::memory_order_relaxed);
    m_slabBytesReserved.fetch_add(pageBytes, std::memory_order_relaxed);

    // Splice the new page's chain onto the lock-free Treiber stack head via CAS.
    const uint32_t firstSlot = (pageIdx << 12);
    const uint32_t lastSlot  = (pageIdx << 12) | (bin.slotsPerPage - 1u);
    uint8_t* lastSlotPtr = page + size_t(bin.slotsPerPage - 1u) * bin.slotStride;

    uint64_t oldHead = bin.taggedHead.load(std::memory_order_acquire);
    for (;;) {
        const uint32_t oldSlot = UnpackSlot(oldHead);
        const uint32_t nextTag = UnpackTag(oldHead) + 1u;
        std::memcpy(lastSlotPtr + sizeof(BlockHeader), &oldSlot, sizeof(oldSlot));
        const uint64_t newHead = PackTaggedHead(nextTag, firstSlot);
        if (bin.taggedHead.compare_exchange_weak(oldHead, newHead,
                                                 std::memory_order_acq_rel, std::memory_order_acquire)) {
            break;
        }
        m_casRetries.fetch_add(1ull, std::memory_order_relaxed);
    }
    (void)lastSlot;
    return true;
}

void* RawSlabAllocator::PopFromBin(uint32_t binIdx, uint32_t* outSlotIdx) {
    SlabBin& bin = m_bins[binIdx];
    for (int attempt = 0; attempt < 4; ++attempt) {
        uint64_t oldHead = bin.taggedHead.load(std::memory_order_acquire);
        while (UnpackSlot(oldHead) != kEmptySlotIndex) {
            const uint32_t slotIdx = UnpackSlot(oldHead);
            uint8_t* slotPtr = SlotAddress(bin, slotIdx);
            if (!slotPtr) break;
            uint32_t nextSlot = kEmptySlotIndex;
            std::memcpy(&nextSlot, slotPtr + sizeof(BlockHeader), sizeof(nextSlot));
            const uint64_t newHead = PackTaggedHead(UnpackTag(oldHead) + 1u, nextSlot);
            if (bin.taggedHead.compare_exchange_weak(oldHead, newHead,
                                                     std::memory_order_acq_rel, std::memory_order_acquire)) {
                if (outSlotIdx) *outSlotIdx = slotIdx;
                return slotPtr;
            }
            m_casRetries.fetch_add(1ull, std::memory_order_relaxed);
        }
        if (!GrowBin(binIdx)) return nullptr;
    }
    return nullptr;
}

void RawSlabAllocator::PushToBin(uint32_t binIdx, uint32_t slotIdx) {
    SlabBin& bin = m_bins[binIdx];
    uint8_t* slotPtr = SlotAddress(bin, slotIdx);
    if (!slotPtr) return;

    uint64_t oldHead = bin.taggedHead.load(std::memory_order_acquire);
    for (;;) {
        const uint32_t oldSlot = UnpackSlot(oldHead);
        std::memcpy(slotPtr + sizeof(BlockHeader), &oldSlot, sizeof(oldSlot));
        const uint64_t newHead = PackTaggedHead(UnpackTag(oldHead) + 1u, slotIdx);
        if (bin.taggedHead.compare_exchange_weak(oldHead, newHead,
                                                 std::memory_order_acq_rel, std::memory_order_acquire)) {
            return;
        }
        m_casRetries.fetch_add(1ull, std::memory_order_relaxed);
    }
}

bool RawSlabAllocator::IsInsideFrameArena(const void* ptr) const noexcept {
    const auto addr = reinterpret_cast<uintptr_t>(ptr);
    for (uint32_t s = 0; s < kFrameArenaSlots; ++s) {
        const auto base = reinterpret_cast<uintptr_t>(m_frameArenas[s].buffer);
        if (base != 0 && addr >= base && addr < base + m_frameArenas[s].capacity) {
            return true;
        }
    }
    return false;
}

void RawSlabAllocator::UpdateHighWater(uint64_t liveBytes) noexcept {
    uint64_t cur = m_highWaterBytes.load(std::memory_order_relaxed);
    while (liveBytes > cur &&
           !m_highWaterBytes.compare_exchange_weak(cur, liveBytes,
                                                   std::memory_order_relaxed, std::memory_order_relaxed)) {
    }
}

void* RawSlabAllocator::Allocate(raw::ce_rebuild::AllocDomain domain, size_t sizeBytes, size_t alignment) {
    const size_t reqSize = std::max<size_t>(sizeBytes, 1u);
    const size_t effAlign = std::max<size_t>(alignment, 16u);
    const uint32_t domIdx = std::min<uint32_t>(static_cast<uint32_t>(domain), 3u);

    // Fast path: Triple-buffered lock-free per-frame linear bump arena (replaces ScrapHeap).
    if (domain == raw::ce_rebuild::AllocDomain::FrameLinear) {
        const uint32_t slot = m_activeFrameSlot.load(std::memory_order_acquire) % kFrameArenaSlots;
        FrameLinearArena& arena = m_frameArenas[slot];
        if (arena.buffer && reqSize <= arena.capacity) {
            size_t cur = arena.offset.load(std::memory_order_relaxed);
            for (;;) {
                const size_t aligned = AlignUp(cur, effAlign);
                const size_t next = aligned + reqSize;
                if (next > arena.capacity) break;
                if (arena.offset.compare_exchange_weak(cur, next,
                                                       std::memory_order_acq_rel, std::memory_order_relaxed)) {
                    arena.allocCount.fetch_add(1ull, std::memory_order_relaxed);
                    m_totalAllocs.fetch_add(1ull, std::memory_order_relaxed);
                    return arena.buffer + aligned;
                }
                m_casRetries.fetch_add(1ull, std::memory_order_relaxed);
            }
        }
        // Overflow falls through cleanly to slab/OS allocator (never crashes on large frame scratch!).
    }

    const uint32_t binIdx = (effAlign <= kMinSlotAlign) ? FindBinForSize(reqSize) : kLargeBinSentinel;
    uint8_t* rawBlock = nullptr;
    uint32_t slotIdx  = kEmptySlotIndex;

    if (binIdx != kLargeBinSentinel) {
        rawBlock = static_cast<uint8_t*>(PopFromBin(binIdx, &slotIdx));
        if (!rawBlock) return nullptr;
        m_slabHits.fetch_add(1ull, std::memory_order_relaxed);
    } else {
        // Large or high-alignment OS fallback: allocate extra alignment padding + 32B header + payload + 8B footer.
        const size_t hdrPad = std::max<size_t>(effAlign, sizeof(BlockHeader));
        const size_t totalBytes = hdrPad + reqSize + sizeof(uint64_t);
        auto* base = static_cast<uint8_t*>(AlignedOsAlloc(totalBytes, hdrPad));
        if (!base) return nullptr;
        // Place BlockHeader immediately before the aligned user pointer at (base + hdrPad).
        rawBlock = base + hdrPad - sizeof(BlockHeader);
        // Store the shift (hdrPad - sizeof(BlockHeader)) inside slotIndex so Deallocate recovers `base` cleanly!
        slotIdx = static_cast<uint32_t>(hdrPad - sizeof(BlockHeader));
        m_largeFallbacks.fetch_add(1ull, std::memory_order_relaxed);
    }

    auto* hdr = reinterpret_cast<BlockHeader*>(rawBlock);
    hdr->headerCanary   = kLiveHeaderCanary;
    hdr->ownerTag       = m_ownerTag;
    hdr->binIndex       = binIdx;
    hdr->slotIndex      = slotIdx;
    hdr->domainAndState = kStateLiveBit | domIdx;
    hdr->requestedSize  = static_cast<uint64_t>(reqSize);

    uint8_t* userPtr = rawBlock + sizeof(BlockHeader);
    std::memcpy(userPtr + reqSize, &kFooterCanary, sizeof(kFooterCanary));

    m_totalAllocs.fetch_add(1ull, std::memory_order_relaxed);
    m_liveAllocs.fetch_add(1ull, std::memory_order_relaxed);
    const uint64_t nowLive = m_liveBytes.fetch_add(reqSize, std::memory_order_relaxed) + reqSize;
    UpdateHighWater(nowLive);

    m_domainStats[domIdx].activeAllocs.fetch_add(1ull, std::memory_order_relaxed);
    m_domainStats[domIdx].bytesInUse.fetch_add(reqSize, std::memory_order_relaxed);
    return userPtr;
}

FreeResult RawSlabAllocator::FreeChecked(void* ptr) {
    if (!ptr) return FreeResult::NullPointer;
    if (IsInsideFrameArena(ptr)) {
        // FrameLinear pointers are reclaimed en masse by ResetFrameArena().
        return FreeResult::FrameLinearNoOp;
    }

    const auto addr = reinterpret_cast<uintptr_t>(ptr);
    if (addr < sizeof(BlockHeader) + 0x1000u || (addr & 0xFu) != 0u) {
        m_foreignPtrRejects.fetch_add(1ull, std::memory_order_relaxed);
        return FreeResult::ForeignPointer;
    }

    auto* userPtr = static_cast<uint8_t*>(ptr);
    auto* hdr = reinterpret_cast<BlockHeader*>(userPtr - sizeof(BlockHeader));

    // Check owner & canary before mutating any state (detects double-free & foreign pointers!).
    if (hdr->ownerTag != m_ownerTag) {
        m_foreignPtrRejects.fetch_add(1ull, std::memory_order_relaxed);
        return FreeResult::ForeignPointer;
    }
    if (hdr->headerCanary == kFreeHeaderCanary) {
        m_doubleFreeRejects.fetch_add(1ull, std::memory_order_relaxed);
        return FreeResult::DoubleFree;
    }
    if (hdr->headerCanary != kLiveHeaderCanary || (hdr->domainAndState & kStateLiveBit) == 0u) {
        m_canaryCorruptions.fetch_add(1ull, std::memory_order_relaxed);
        return FreeResult::CanaryCorrupted;
    }

    // Atomically clear live bit to prevent concurrent double-free races across threads.
    auto* stateAtom = reinterpret_cast<std::atomic<uint32_t>*>(&hdr->domainAndState);
    const uint32_t prevState = stateAtom->fetch_and(~kStateLiveBit, std::memory_order_acq_rel);
    if ((prevState & kStateLiveBit) == 0u) {
        m_doubleFreeRejects.fetch_add(1ull, std::memory_order_relaxed);
        return FreeResult::DoubleFree;
    }

    const uint64_t reqSize = hdr->requestedSize;
    const uint32_t binIdx  = hdr->binIndex;
    const uint32_t slotIdx = hdr->slotIndex;
    const uint32_t domIdx  = prevState & 0x3u;

    // Validate 64-bit footer canary immediately following requestedSize.
    uint64_t footer = 0ull;
    std::memcpy(&footer, userPtr + reqSize, sizeof(footer));
    const bool footerOk = (footer == kFooterCanary);

    hdr->headerCanary = kFreeHeaderCanary;

    m_totalDeallocs.fetch_add(1ull, std::memory_order_relaxed);
    m_liveAllocs.fetch_sub(1ull, std::memory_order_relaxed);
    m_liveBytes.fetch_sub(reqSize, std::memory_order_relaxed);
    m_domainStats[domIdx].activeAllocs.fetch_sub(1ull, std::memory_order_relaxed);
    m_domainStats[domIdx].bytesInUse.fetch_sub(reqSize, std::memory_order_relaxed);

    if (!footerOk) {
        // Fail closed on buffer overrun: count corruption and quarantine corrupted slab slot (never recycle corrupt memory!).
        m_canaryCorruptions.fetch_add(1ull, std::memory_order_relaxed);
        if (binIdx == kLargeBinSentinel) {
            AlignedOsFree(reinterpret_cast<uint8_t*>(hdr) - slotIdx);
        }
        return FreeResult::CanaryCorrupted;
    }

    if (binIdx < kSlabBinCount) {
        PushToBin(binIdx, slotIdx);
    } else if (binIdx == kLargeBinSentinel) {
        AlignedOsFree(reinterpret_cast<uint8_t*>(hdr) - slotIdx);
    }
    return FreeResult::Ok;
}

void RawSlabAllocator::Deallocate(raw::ce_rebuild::AllocDomain /*domain*/, void* ptr) {
    (void)FreeChecked(ptr);
}

void RawSlabAllocator::ResetFrameArena(uint32_t frameSlotIndex) {
    const uint32_t slot = frameSlotIndex % kFrameArenaSlots;
    m_activeFrameSlot.store(slot, std::memory_order_release);
    m_frameArenas[slot].offset.store(0u, std::memory_order_release);
    m_frameArenas[slot].resetCount.fetch_add(1ull, std::memory_order_relaxed);
}

raw::ce_rebuild::AllocatorStats RawSlabAllocator::QueryStats(raw::ce_rebuild::AllocDomain domain) const {
    raw::ce_rebuild::AllocatorStats st{};
    if (domain == raw::ce_rebuild::AllocDomain::FrameLinear) {
        for (uint32_t s = 0; s < kFrameArenaSlots; ++s) {
            st.bytesReserved     += m_frameArenas[s].capacity;
            st.bytesCommitted    += m_frameArenas[s].capacity;
            st.bytesInUse        += m_frameArenas[s].offset.load(std::memory_order_relaxed);
            st.activeAllocations += m_frameArenas[s].allocCount.load(std::memory_order_relaxed);
            st.frameResets       += m_frameArenas[s].resetCount.load(std::memory_order_relaxed);
        }
        return st;
    }
    const uint32_t domIdx = std::min<uint32_t>(static_cast<uint32_t>(domain), 3u);
    st.bytesReserved     = m_slabBytesReserved.load(std::memory_order_relaxed);
    st.bytesCommitted    = st.bytesReserved;
    st.bytesInUse        = m_domainStats[domIdx].bytesInUse.load(std::memory_order_relaxed);
    st.activeAllocations = m_domainStats[domIdx].activeAllocs.load(std::memory_order_relaxed);
    return st;
}

void* RawSlabAllocator::Alloc(size_t sizeBytes, size_t alignment, raw::ce_rebuild::AllocDomain domain) {
    return Allocate(domain, sizeBytes, alignment);
}

bool RawSlabAllocator::ValidatePointer(const void* ptr) const noexcept {
    if (!ptr) return false;
    if (IsInsideFrameArena(ptr)) return true;
    const auto addr = reinterpret_cast<uintptr_t>(ptr);
    if (addr < sizeof(BlockHeader) + 0x1000u || (addr & 0xFu) != 0u) return false;
    const auto* userPtr = static_cast<const uint8_t*>(ptr);
    const auto* hdr = reinterpret_cast<const BlockHeader*>(userPtr - sizeof(BlockHeader));
    if (hdr->ownerTag != m_ownerTag || hdr->headerCanary != kLiveHeaderCanary) return false;
    if ((hdr->domainAndState & kStateLiveBit) == 0u) return false;
    uint64_t footer = 0ull;
    std::memcpy(&footer, userPtr + hdr->requestedSize, sizeof(footer));
    return footer == kFooterCanary;
}

SlabAllocatorMetrics RawSlabAllocator::GetMetrics() const noexcept {
    SlabAllocatorMetrics m{};
    m.liveAllocations    = m_liveAllocs.load(std::memory_order_relaxed);
    m.liveBytes          = m_liveBytes.load(std::memory_order_relaxed);
    m.highWaterBytes     = m_highWaterBytes.load(std::memory_order_relaxed);
    m.totalAllocations   = m_totalAllocs.load(std::memory_order_relaxed);
    m.totalDeallocations = m_totalDeallocs.load(std::memory_order_relaxed);
    m.slabHits           = m_slabHits.load(std::memory_order_relaxed);
    m.largeFallbacks     = m_largeFallbacks.load(std::memory_order_relaxed);
    m.casRetries         = m_casRetries.load(std::memory_order_relaxed);
    m.canaryCorruptions  = m_canaryCorruptions.load(std::memory_order_relaxed);
    m.doubleFreeRejects  = m_doubleFreeRejects.load(std::memory_order_relaxed);
    m.foreignPtrRejects  = m_foreignPtrRejects.load(std::memory_order_relaxed);
    m.slabPagesAllocated = m_slabPagesAllocated.load(std::memory_order_relaxed);
    return m;
}

}  // namespace raw::core
