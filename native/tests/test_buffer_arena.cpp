#include "raw/image.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
using namespace raw;
int main() {
    // default Buffer is heap-backed and behaves exactly as before
    Buffer<float> heap;
    heap.resize(4, 4);
    heap.at(2, 2) = 3.5f;
    CHECK(heap.w == 4 && heap.h == 4);
    CHECK(heap.at(2, 2) == 3.5f);
    CHECK(heap.px.size() == 16);              // .px still a usable/iterable vector

    // an arena Buffer draws from the arena, is zero-filled, and is writable
    std::array<std::uint8_t, 4096> backing{};
    Arena a(backing.data(), backing.size());
    Buffer<float> ab(&a);
    ab.resize(8, 8);
    CHECK(a.stats().used >= 8 * 8 * sizeof(float));
    CHECK(ab.at(0, 0) == 0.0f);               // zero-filled like the heap path
    CHECK(ab.at(7, 7) == 0.0f);
    ab.at(7, 7) = 9.0f;
    CHECK(ab.at(7, 7) == 9.0f);

    // a Vec3 buffer too (alignment via alignof(Vec3))
    Buffer<Vec3> vb(&a);
    vb.resize(2, 2);
    CHECK(vb.at(1, 1).x == 0.0f);
    vb.at(1, 1) = Vec3{1, 2, 3};
    CHECK(vb.at(1, 1).y == 2.0f);
    return raw_test_summary();
}
