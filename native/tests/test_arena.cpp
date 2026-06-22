#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
using namespace raw;
int main() {
    std::array<std::uint8_t, 256> backing{};
    Arena a(backing.data(), backing.size());

    // within budget: succeeds, witnessed
    void* p = a.allocate(64, 16);
    CHECK(p != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(p) % 16 == 0); // aligned
    CHECK(a.stats().allocations == 1);
    CHECK(a.stats().used >= 64);
    CHECK(a.within_budget());

    // a second allocation bumps forward
    void* q = a.allocate(64, 16);
    CHECK(q != nullptr);
    CHECK(q != p);
    CHECK(a.stats().used >= 128);
    std::size_t hw = a.stats().high_water;

    // over budget: refused (fail-closed), counted, no growth, used unchanged
    std::size_t used_before = a.stats().used;
    void* r = a.allocate(1024, 16);
    CHECK(r == nullptr);
    CHECK(a.stats().refusals == 1);
    CHECK(a.stats().used == used_before);
    CHECK(!a.within_budget());

    // zero-size is refused too
    CHECK(a.allocate(0) == nullptr);
    CHECK(a.stats().refusals == 2);

    // reset hands the space back but KEEPS the lifetime witness
    a.reset();
    CHECK(a.stats().used == 0);
    CHECK(a.stats().high_water == hw);     // lifetime high-water retained
    CHECK(a.stats().allocations == 2);     // lifetime counters retained
    CHECK(a.stats().refusals == 2);
    void* s = a.allocate(32, 8);           // can allocate again after reset
    CHECK(s != nullptr);
    return raw_test_summary();
}
