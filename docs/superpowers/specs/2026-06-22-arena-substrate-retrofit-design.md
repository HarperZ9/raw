# Deep Arena Retrofit — Increment 4 Design

> Date: 2026-06-22 · Status: design (operator-approved direction: hand-rolled allocator, full geometry) · Home: raw repo.

## 0 · Goal

Put the renderer's **whole frame on the accountable Layer-0 substrate**: every byte the
frame allocates — the G-buffer planes, both AO buffers, the error map, the shaded frame,
**and** the scene + accelerator geometry — draws from one bounded, witnessed `raw::Arena`
(increment 2). The CLI then emits the **arena Certificate** (BOUNDED/BREACHED) alongside the
reconcile Certificate: both organs — sense and substrate — witnessed in one run. "Memory as a
gated actuator," made real inside the renderer. A too-small budget makes the frame **actually
BREACH** (fail-closed), proven by a test — not accounting theater.

## 1 · Mechanism — `ArenaAllocator<T>`, our own, over `raw::Arena`

The operator's choice: hand-rolled, our `Arena` directly, **no `std::pmr`**. One small
`std::`-allocator-compatible type wraps the arena and is applied to the `std::vector` inside
`Buffer<T>` **and** to the geometry vectors. This unifies both storage families under one
mechanism while keeping every public type name stable.

```cpp
// native/raw/arena_allocator.hpp
#pragma once
#include "raw/arena.hpp"
#include <cstddef>
#include <new>
namespace raw {
// A std::allocator that draws bytes from a raw::Arena (arena_ != null) or the heap
// (arena_ == null, the default — so existing containers are byte-for-byte unchanged).
// Over-budget arena allocation is fail-closed: it throws std::bad_alloc.
template<class T>
struct ArenaAllocator {
    using value_type = T;
    // Propagate the allocator on every container operation so move/copy/swap with a
    // stateful allocator is well-defined (no UB on unequal allocators).
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::false_type;

    Arena* arena_{nullptr};
    ArenaAllocator() noexcept = default;                       // heap mode
    explicit ArenaAllocator(Arena* a) noexcept : arena_(a) {}  // arena mode
    template<class U> ArenaAllocator(const ArenaAllocator<U>& o) noexcept : arena_(o.arena_) {}

    T* allocate(std::size_t n){
        void* p = arena_ ? arena_->allocate(n * sizeof(T), alignof(T))
                         : ::operator new(n * sizeof(T));
        if (!p) throw std::bad_alloc();      // arena refusal (over budget) => fail-closed
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept {
        if (!arena_) ::operator delete(p);   // heap frees; arena is bump (no-op free)
    }
    template<class U> bool operator==(const ArenaAllocator<U>& o) const noexcept { return arena_ == o.arena_; }
    template<class U> bool operator!=(const ArenaAllocator<U>& o) const noexcept { return arena_ != o.arena_; }
};
}
```

Notes that the plan must honor:
- **`n * sizeof(T)` overflow** is not guarded here; counts are frame-bounded (≤ W·H, ≤ tri
  count). The arena's own budget gate is the backstop. Documented bound.
- The arena is a **bump allocator with no per-block free** — `deallocate` is a no-op in arena
  mode. Intra-frame temporaries are therefore not reclaimed; `high_water` = the frame's
  **cumulative** footprint (the true peak it touched). This is how frame allocators work and is
  the honest number to witness.

## 2 · The allocation map (what changes)

Every `std::vector` that holds frame or geometry data gains `ArenaAllocator<T>` and an
arena-aware construction path. Public type *names* (`Buffer<float>`, `Scene`, `Mesh`,
`LinearAccel`, `GBuffer`) stay stable — only their internal vector type + an optional
`Arena*` constructor change — so the five render-function **signatures gain one `Arena*`
parameter** (defaulted) and nothing else ripples.

| Type / site | Today | After |
|---|---|---|
| `Buffer<T>::px` (`image.hpp`) | `std::vector<T>` | `std::vector<T, ArenaAllocator<T>>` + `explicit Buffer(Arena*)` |
| `GBuffer` (`gbuffer.hpp`) | 5 default Buffers | `explicit GBuffer(Arena*)` constructs the 5 planes arena-aware |
| `Mesh` (`scene.hpp`) | `vector<Vec3>`,`vector<int>` | `…, ArenaAllocator<…>` + `explicit Mesh(Arena*)` |
| `Scene` (`scene.hpp`) | `vector<Mesh>`,`vector<Light>` | `…, ArenaAllocator<…>` + `explicit Scene(Arena*)` |
| `LinearAccel::tris` (`accel.hpp`) | `vector<Tri>` | `…, ArenaAllocator<Tri>`; `build(s, Arena*)` |
| `buildTestScene` (`scene.cpp`) | `(w,h)` | `(w,h, Arena* = nullptr)`, constructs in place with the resource |
| `rasterize/computeRTAO/computeSSAO/reconcile/shade` | — | each gains a trailing `Arena* = nullptr`, constructs its output Buffer with it |
| CLI (`app/main.cpp`) | heap pipeline | one budgeted `Arena` threaded through the whole frame; emits both Certificates |

**Backward compatibility is total:** every new `Arena*` parameter and constructor defaults to
heap (`nullptr`). Every existing test constructs the default (heap) form and is unchanged.
`Buffer::px` remains a `std::vector` (range-iterable), so the two `.px` sites (`raster.cpp:9`,
`test_raster.cpp:9`) and the `.at()`-based writers (`image.cpp`) compile untouched.

## 3 · Threading & construction (the careful bits)

- **Keep the default constructor.** Declaring an `explicit T(Arena*)` ctor suppresses the
  implicit default ctor, which every existing heap caller relies on. Each retrofitted type
  (`Buffer`, `GBuffer`, `Mesh`, `Scene`) must therefore explicitly re-add `T() = default;`
  (and `GBuffer(Arena* a) : depth(a), normal(a), position(a), albedo(a), mask(a) {}`).
- **`Buffer<T>(Arena* a)`** → `px(ArenaAllocator<T>(a))`; `resize` stays `px.assign(W*H, T{})`
  (zero-fill preserved in both modes; the allocator just changes where the bytes come from).
- **Returning Buffers by value** (every render fn) relies on move: the vector's move ctor
  carries its allocator (arena pointer), so an arena-backed result moves out pointing into
  arena memory. The CLI's `Arena` outlives the whole frame, so all results stay valid.
- **`Scene`/`Mesh` are nested**: construct in place. `buildTestScene` does `Scene s(arena);`,
  builds each `Mesh m(arena);` then `s.meshes.push_back(std::move(m));` — **move, never copy a
  heap mesh in** (a copy would not carry the arena allocator). `reserve()` the known mesh/light
  counts to avoid outer-vector regrowth.
- **`LinearAccel::build(const Scene& s, Arena* a)`** rebuilds `tris` as
  `TriVec(ArenaAllocator<Tri>(a))`, `reserve`s the triangle count, then fills it. The POC* =
  `true_type` traits make the move-assignment into the member well-defined.

## 4 · The witness & fail-closed proof (CLI, `app/main.cpp`)

The budget is **measured, not a magic constant** — a two-pass run grounds it in the frame's
real footprint:

```text
render_frame(Arena* arena) -> { rec, frame, aoRT, aoSS } :
    Scene s        = buildTestScene(W, H, arena)
    GBuffer g      = rasterize(s, W, H, arena)
    LinearAccel a; a.build(s, arena)
    aoRT           = computeRTAO(g, a, 64, 2.0f, arena)
    aoSS           = computeSSAO(g, 24, 2.0f, arena)
    rec            = reconcile(aoSS, aoRT, g.mask, 0.12f, arena)
    frame          = shade(g, aoRT, s, arena)

// PASS 1 — MEASURE: a computed generous slab (no magic 32 MB); read the true footprint.
slabUB = (size_t)W*H * PER_PIXEL_UPPER * 2 + 1<<20      // PER_PIXEL_UPPER = 65 B (all 9 planes)
vector<unsigned char> slab1(slabUB); Arena measure(slab1.data(), slabUB)
render_frame(&measure)                                  // discard outputs; we want the number
H = measure.stats().high_water                          // the MEASURED frame footprint

// PASS 2 — TIGHT, WITNESSED: budget == exactly the measured footprint H.
vector<unsigned char> slab2(H); Arena arena(slab2.data(), H)   // the ONE real malloc per pass
auto out = render_frame(&arena)                         // deterministic => fits exactly
write frame/AO/error images from `out`
Certificate aoCert    = certificate_from_reconcile(out.rec, 0.12f)  // refuted (DIVERGENT)
Certificate arenaCert = certificate_from_arena(arena.stats())       // verified (BOUNDED), used==H==budget
write certificate.json (aoCert) + arena_certificate.json (arenaCert)
print both; print arena_witness(arena.stats())          // budget=H used=H high_water=H refusals=0
assert arena.stats().refusals == 0                      // pass-2 must fit at exactly H
```

- **Measured-budget demo:** pass 1 measures `H` (the true cumulative footprint, ≈ 4.3 MB at
  256²); pass 2 renders within a budget of **exactly `H`** → `refusals == 0`, `used == budget ==
  high_water == H` → `arenaCert` verified (BOUNDED). The frame fitting in exactly its measured
  footprint is the witness — no arbitrary constant. Determinism (fixed scene, hash-based
  sampling, `reserve()`d geometry) guarantees pass 2 reproduces pass 1's `H` exactly.
- **Fail-closed:** any single arena allocation that would exceed the budget throws
  `std::bad_alloc`; the CLI catches it, emits `certificate_from_arena(...)` (refuted / BREACHED,
  `refusals > 0`), and exits non-zero. (Reachable in the demo only if measurement is bypassed;
  it is the contract the BREACH test exercises directly.)
- **BREACH proof (test):** a deliberately tiny budget → an allocation throws `std::bad_alloc`
  mid-frame → the test asserts the throw **and** `arena.stats().refusals > 0` **and**
  `certificate_from_arena(...)` → `refuted` / `verdict=BREACHED`. This is the real gate: the
  substrate refuses, the frame cannot silently over-allocate.

## 5 · Tasks (TDD, ~7) — each ends green before the next

1. **`ArenaAllocator<T>`** — the allocator above. Test: `std::vector<int, ArenaAllocator<int>>`
   built with an arena allocator grows `arena.stats().used`; a vector that exceeds a tiny
   budget throws `std::bad_alloc` and increments `refusals`; the default (heap) allocator works
   and frees.
2. **`Buffer<T>` arena-aware** — `px` gains the allocator; `explicit Buffer(Arena*)`. Test:
   `Buffer<float> b(&arena); b.resize(...)` draws from the arena and is zero-filled and
   writable via `at()`; a default `Buffer` is unchanged; `.px` iteration still compiles.
3. **`GBuffer` arena-aware** — `explicit GBuffer(Arena*)` threads the resource into all five
   planes. Test: an arena GBuffer's planes all draw from the arena; default GBuffer unchanged.
4. **`Scene`/`Mesh` + `buildTestScene(w,h,Arena*)`** — typedefs, in-place arena construction.
   Test: `buildTestScene(W,H,&arena)` puts mesh/light geometry in the arena (used grows);
   `buildTestScene(W,H)` is heap and identical to today (same triangle count / first vertex).
5. **`LinearAccel::build(s, Arena*)`** — `tris` arena-aware. Test: arena build puts tris in the
   arena; `occluded` results identical to the heap build (same scene → same occlusion).
6. **Render functions thread `Arena*`** — `rasterize/computeRTAO/computeSSAO/reconcile/shade`
   each gain a trailing `Arena* = nullptr` and construct their output with it. Test: each
   output Buffer is arena-backed when an arena is passed; pixel values are identical to the
   heap path (the arena changes *where* memory lives, never *what* is computed).
7. **CLI: measured-budget frame + both Certificates + the gate tests** — wire §4 (two-pass:
   measure `H`, then render at budget `H`). Test (in-process, `test_frame_arena.cpp`):
   (a) **measured-budget BOUNDED** — render the frame in a generous slab to get `H`, then a
   fresh `Arena(H)` renders it again with `refusals == 0` and `used == high_water == H` and
   `certificate_from_arena` → `verified`; (b) **fail-closed BREACH** — `Arena(tiny)` makes the
   frame throw `std::bad_alloc`, and after catching, `refusals > 0` and `certificate_from_arena`
   → `refuted`/BREACHED; (c) **pixel-identical** — a frame rendered through the arena is
   byte-for-byte equal to the same frame rendered on the heap (memory moved, render unchanged).
   The CLI emits `arena_certificate.json` alongside `certificate.json`.

## 6 · Success criteria

- The full frame (geometry + buffers) renders with **zero heap allocations on the frame path
  except the single backing slab** — verified by `arena.stats().allocations` accounting for the
  frame's vectors. The budget is **measured** (`high_water` of a measure pass), and pass 2
  renders at exactly that budget with `used == budget == high_water == H` and `refusals == 0`.
- **Identical pixels**: the arena path produces byte-identical `frame.ppm` / AO outputs to the
  heap path (the retrofit moves memory, it does not change the render). A test compares a
  heap-rendered buffer against an arena-rendered one pixel-for-pixel.
- Both Certificates emitted in one CLI run: reconcile → `refuted` (DIVERGENT), arena →
  `verified` (BOUNDED); a tiny budget flips the arena to `refuted` (BREACHED) and exits
  non-zero.
- Full suite green (24/24 expected: 17 today + ~7 new), zero third-party includes (only
  `<new>`, `<cstddef>`, `<type_traits>` added).
- Backward compatible: every pre-existing test passes unchanged.

## 7 · Honest bounds (carried into the plan)

- Bump arena = no intra-frame free → `high_water` is cumulative, not live-peak. Stated, by design.
- `ArenaAllocator::allocate` does not guard `n * sizeof(T)` overflow; counts are frame-bounded
  and the arena budget is the backstop. Stated.
- Vector growth inside the arena (geometry `push_back`, before `reserve`) leaks superseded
  blocks into the bump arena; `reserve()` at known counts keeps this near zero. Stated.
- One real heap allocation remains: the arena's **backing slab** (and the heap default path for
  every non-CLI caller). The claim is "no hidden malloc on the frame path," not "zero malloc in
  the process."

## 8 · Deferred (increment 5+)

- A `reset()`-per-frame loop (render N frames reusing one arena) to show the bump arena's real
  payoff (zero re-allocation across frames).
- Sub-arenas / tagged regions (geometry vs frame-scratch) for finer witness granularity.
- `compose([reconcile, arena])` → one frame-level Certificate (mirrors coherence-membrane's
  `compose`), and the standing cross-language shape-gate flagged in increment 3's final review.
