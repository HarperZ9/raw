#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
#include <string>
using namespace raw;
int main() {
    std::array<std::uint8_t, 1024> backing{};
    Arena a(backing.data(), backing.size());

    float* f = arena_alloc<float>(a, 100);   // 400 bytes, aligned
    CHECK(f != nullptr);
    CHECK(reinterpret_cast<std::uintptr_t>(f) % alignof(float) == 0);
    f[0] = 1.5f; f[99] = 2.5f;               // writable across the span
    CHECK(f[0] == 1.5f && f[99] == 2.5f);

    // over budget returns nullptr (fail-closed) and the witness reports BREACHED
    double* big = arena_alloc<double>(a, 100000);
    CHECK(big == nullptr);
    std::string w = arena_witness(a.stats());
    CHECK(w.find("refusals=1") != std::string::npos);
    CHECK(w.find("verdict=BREACHED") != std::string::npos);

    // a clean arena witnesses BOUNDED
    std::array<std::uint8_t, 64> b2{};
    Arena clean(b2.data(), b2.size());
    (void)arena_alloc<int>(clean, 4);
    CHECK(arena_witness(clean.stats()).find("verdict=BOUNDED") != std::string::npos);
    return raw_test_summary();
}
