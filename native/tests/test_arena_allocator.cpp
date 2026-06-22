#include "raw/arena_allocator.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
#include <vector>
#include <new>
using namespace raw;
int main() {
    std::array<std::uint8_t, 1024> backing{};
    Arena a(backing.data(), backing.size());

    // an arena-backed vector draws its bytes from the arena
    ArenaAllocator<int> alloc(&a);
    std::vector<int, ArenaAllocator<int>> v(alloc);
    v.reserve(10);
    CHECK(a.stats().allocations >= 1);
    CHECK(a.stats().used >= 10 * sizeof(int));
    v.push_back(42);
    CHECK(v[0] == 42);

    // a default (heap) allocator works and does NOT touch the arena
    std::size_t used_before = a.stats().used;
    std::vector<int, ArenaAllocator<int>> heap;     // default ctor => arena_==nullptr
    heap.assign(100, 7);
    CHECK(heap[99] == 7);
    CHECK(a.stats().used == used_before);

    // over-budget allocation is fail-closed: throws bad_alloc, witnessed as a refusal
    std::array<std::uint8_t, 32> tinybuf{};
    Arena tiny(tinybuf.data(), tinybuf.size());
    ArenaAllocator<int> talloc(&tiny);
    std::vector<int, ArenaAllocator<int>> big(talloc);
    bool threw = false;
    try { big.reserve(1000); } catch (const std::bad_alloc&) { threw = true; }
    CHECK(threw);
    CHECK(tiny.stats().refusals >= 1);
    return raw_test_summary();
}
