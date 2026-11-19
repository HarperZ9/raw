// Shader-resource tables already copied into the list's shader-visible descriptor pages (tx_ctx_draw.cpp).
// Original work, MIT.
//
// Binding a stage's shader resources copies a table of descriptors into the shader-visible heap; the
// game binds the same sets again and again (one material on many meshes, a pass re-binding its inputs),
// and each copy is a driver call on the application's thread. Within one command list a table copied
// before is used again: it is keyed by the views' creation ids (never reused, unlike descriptor slots and
// addresses) and the null descriptors' dimensions, and compared in full on a hit. A new list starts empty:
// the pages a list copied into are given back when that list has run.
#pragma once
#include <cstdint>
#include <cstring>
#include <vector>

namespace raw::tx {

class TableCache {
public:
    // The heap index of a table equal to keys[0..n), or ~0u.
    uint32_t Find(const uint64_t* keys, uint32_t n, uint64_t hash) const
    {
        const Slot& s = slots_[hash & (kSlots - 1)];
        if (s.epoch != epoch_ || s.hash != hash || s.n != n) return ~0u;
        return std::memcmp(&arena_[s.at], keys, n * sizeof(uint64_t)) == 0 ? s.base : ~0u;
    }
    void Put(const uint64_t* keys, uint32_t n, uint64_t hash, uint32_t base)
    {
        Slot& s = slots_[hash & (kSlots - 1)];
        s.epoch = epoch_; s.hash = hash; s.n = n; s.base = base; s.at = uint32_t(arena_.size());
        arena_.insert(arena_.end(), keys, keys + n);
    }
    void Reset() { ++epoch_; arena_.clear(); }   // a new list
    uint64_t hits = 0, misses = 0;

private:
    static constexpr size_t kSlots = 4096;
    struct Slot { uint64_t hash = 0; uint32_t epoch = 0, n = 0, base = 0, at = 0; };
    std::vector<Slot> slots_ = std::vector<Slot>(kSlots);
    std::vector<uint64_t> arena_;
    uint32_t epoch_ = 1;
};

}  // namespace raw::tx
