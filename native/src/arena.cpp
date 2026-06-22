#include "raw/arena.hpp"
namespace raw {
static std::size_t align_up(std::size_t v, std::size_t a){
    return (a == 0) ? v : ((v + (a - 1)) & ~(a - 1));
}
Arena::Arena(void* base, std::size_t budget)
    : base_(static_cast<unsigned char*>(base)) {
    stats_.budget = base_ ? budget : 0;        // null backing => 0 budget, all requests refuse
}
void* Arena::allocate(std::size_t n, std::size_t align){
    std::size_t start = align_up(stats_.used, align);
    // overflow-safe budget gate: refuse zero-size, refuse if alignment pushed past the end,
    // refuse if n would not fit in the remaining budget.
    if (n == 0 || start > stats_.budget || n > stats_.budget - start) {
        ++stats_.refusals;
        return nullptr;
    }
    void* p = base_ + start;
    stats_.used = start + n;
    if (stats_.used > stats_.high_water) stats_.high_water = stats_.used;
    ++stats_.allocations;
    return p;
}
void Arena::reset(){ stats_.used = 0; }        // keep high_water/allocations/refusals (the witness)
}
