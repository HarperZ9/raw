#pragma once
#include <cstddef>
namespace raw {
// The witness: a re-checkable record of the arena's lifetime behavior.
struct ArenaStats {
    std::size_t budget{0};       // total bytes the arena may hand out (its backing span)
    std::size_t used{0};         // bytes currently handed out (the bump offset)
    std::size_t high_water{0};   // max `used` ever reached (lifetime)
    std::size_t allocations{0};  // successful allocations (lifetime)
    std::size_t refusals{0};     // over-budget / zero-size requests refused (lifetime, fail-closed)
};
// A bounded, witnessed, fail-closed bump allocator over caller-provided backing memory.
// Memory as a gated actuator: every request is gated by the budget; over-budget => refuse
// (nullptr), never grow, never touch memory outside the backing span.
class Arena {
public:
    Arena(void* base, std::size_t budget);
    Arena(const Arena&) = delete;
    Arena& operator=(const Arena&) = delete;
    void* allocate(std::size_t n, std::size_t align = alignof(std::max_align_t));
    void reset();                              // used -> 0; lifetime witness retained
    const ArenaStats& stats() const { return stats_; }
    bool within_budget() const { return stats_.refusals == 0; }
private:
    unsigned char* base_;
    ArenaStats stats_;
};
}
