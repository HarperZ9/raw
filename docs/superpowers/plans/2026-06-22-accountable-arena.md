# Accountable Arena Allocator — Increment 2 Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`).

**Goal:** Give the organism Layer 0 — a native, bounded, **witnessed, fail-closed** memory substrate: an arena (bump) allocator where every request is gated by a budget, over-budget is **refused** (never grow), and the lifetime allocation record is a re-checkable witness. "Memory as a gated actuator."

**Architecture:** A caller-backed bump allocator (the caller owns the backing bytes — a static/stack buffer or mmap — so there is **no hidden malloc** on the path). Each allocation aligns, checks the budget overflow-safely, and either succeeds (updating used / high-water / allocation count) or refuses (incrementing a refusal counter and returning nullptr). `ArenaStats` is the witness.

**Tech Stack:** C++23, standard library only. Lives under `native/raw/` + `native/src/` + `native/tests/`, namespace `raw`. Same build as increment 1.

## Global Constraints

- C++23, standard library only. No third-party, no DirectX. (verbatim from the engine spec §2)
- Everything under `native/`. Headers `native/raw/`, impl `native/src/`, tests `native/tests/`, namespace `raw`.
- **Fail-closed:** over-budget or zero-size requests are refused (nullptr) and counted; the arena never grows or touches memory outside its backing span.
- **No hidden malloc on the allocation path:** the backing bytes are caller-provided; the arena only bumps a pointer within them.
- Build (Windows/MSVC multi-config): `cmake --build native/build --config Release`; test `ctest --test-dir native/build -C Release -R <name> --output-on-failure`.

---

### Task 1: The Arena (bounded, witnessed, fail-closed bump allocator)

**Files:**
- Create: `native/raw/arena.hpp`
- Create: `native/src/arena.cpp`
- Create: `native/tests/test_arena.cpp`

**Interfaces:**
- Consumes: nothing (stdlib only).
- Produces: `raw::ArenaStats{ size_t budget, used, high_water, allocations, refusals }`; `raw::Arena` with `Arena(void* base, size_t budget)`, `void* allocate(size_t n, size_t align = alignof(max_align_t))`, `void reset()`, `const ArenaStats& stats() const`, `bool within_budget() const` (true iff refusals==0).

- [ ] **Step 1: Write the failing test**

`native/tests/test_arena.cpp`:
```cpp
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_arena`
Expected: FAIL — `raw/arena.hpp` does not exist. (If the new test/src aren't picked up, run `cmake -B native/build -S native` once, then rebuild.)

- [ ] **Step 3: Write minimal implementation**

`native/raw/arena.hpp`:
```cpp
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
```

`native/src/arena.cpp`:
```cpp
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
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_arena --output-on-failure`
Expected: `test_arena` PASS. Then the full suite stays green: `ctest --test-dir native/build -C Release` (13/13).

- [ ] **Step 5: Commit**

```bash
git add native/raw/arena.hpp native/src/arena.cpp native/tests/test_arena.cpp
git commit -m "feat(native): bounded witnessed fail-closed arena allocator (Layer 0)"
```

---

### Task 2: Typed allocation helper + the witness record

**Files:**
- Modify: `native/raw/arena.hpp` (add `arena_alloc<T>` + `arena_witness` decl)
- Create: `native/src/arena_witness.cpp`
- Create: `native/tests/test_arena_typed.cpp`

**Interfaces:**
- Consumes: `raw::Arena`, `raw::ArenaStats`.
- Produces: `template<class T> T* raw::arena_alloc(Arena&, std::size_t count)` (aligned `alignof(T)`, `count*sizeof(T)` bytes, nullptr on refuse); `std::string raw::arena_witness(const ArenaStats&)` — a compact re-checkable line: `arena budget=B used=U high_water=H allocations=A refusals=R verdict=BOUNDED|BREACHED` (BOUNDED iff refusals==0).

- [ ] **Step 1: Write the failing test**

`native/tests/test_arena_typed.cpp`:
```cpp
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_arena_typed`
Expected: FAIL — `arena_alloc` / `arena_witness` not declared.

- [ ] **Step 3: Write minimal implementation**

Add to `native/raw/arena.hpp` (inside `namespace raw`, after the `Arena` class), and add `#include <string>` at the top:
```cpp
template<class T>
T* arena_alloc(Arena& a, std::size_t count){
    return static_cast<T*>(a.allocate(count * sizeof(T), alignof(T)));
}
std::string arena_witness(const ArenaStats& s);
```

`native/src/arena_witness.cpp`:
```cpp
#include "raw/arena.hpp"
#include <string>
namespace raw {
std::string arena_witness(const ArenaStats& s){
    auto n = [](std::size_t v){ return std::to_string(v); };
    return "arena budget=" + n(s.budget) + " used=" + n(s.used) +
           " high_water=" + n(s.high_water) + " allocations=" + n(s.allocations) +
           " refusals=" + n(s.refusals) +
           " verdict=" + (s.refusals == 0 ? "BOUNDED" : "BREACHED");
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_arena_typed --output-on-failure`
Expected: `test_arena_typed` PASS. Full suite green: `ctest --test-dir native/build -C Release` (14/14).

- [ ] **Step 5: Commit**

```bash
git add native/raw/arena.hpp native/src/arena_witness.cpp native/tests/test_arena_typed.cpp
git commit -m "feat(native): typed arena_alloc + arena_witness record"
```

---

## Final verification

- [ ] Full suite green: `ctest --test-dir native/build -C Release` (expect 14/14).
- [ ] Zero third-party includes: the two new headers/impls use only `<cstddef>`, `<string>` (+ test-only `<array>`, `<cstdint>`, `<string>`).
- [ ] The witness line round-trips the lifetime record and reports BOUNDED vs BREACHED — the accountability verdict for "memory as a gated actuator."

## Deferred (increment 3 and beyond)

- Emit the `ArenaStats` witness as the canonical **Certificate** (verified=BOUNDED / refuted=BREACHED) and record it to the cdev ledger — the Certificate bridge.
- Retrofit the engine's `Buffer<T>` / scene / accel storage to draw from an `Arena` so the renderer's whole frame allocates from one bounded, witnessed budget (the sense organ on the accountable substrate end-to-end).
- mmap/static-buffer backing variants; per-allocation tagging; SIMD-aligned pools.
