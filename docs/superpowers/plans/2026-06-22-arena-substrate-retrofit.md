# Deep Arena Retrofit — Increment 4 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`).

**Goal:** Put the renderer's whole frame — G-buffer, AO buffers, error map, shaded frame, **and** scene + accelerator geometry — on the bounded, witnessed `raw::Arena`, and have the CLI emit the arena Certificate (BOUNDED, budget measured from `high_water`) beside the reconcile Certificate.

**Architecture:** One hand-written `ArenaAllocator<T>` (a `std::`-allocator wrapping `raw::Arena`, heap-default, fail-closed) is applied to the `std::vector` inside `Buffer<T>` and to the geometry vectors, so every public type name stays stable and arena-ness is a runtime property of the allocator instance. The five render functions each gain one trailing `Arena* = nullptr`. The CLI runs two passes — measure the footprint `H`, then render within a budget of exactly `H`.

**Tech Stack:** C++23, standard library only. Same MSVC multi-config build as increments 1–3.

## Global Constraints

- C++23, standard library only. No third-party, no DirectX. Everything under `native/`; headers `native/raw/`, impl `native/src/`, tests `native/tests/`, namespace `raw`.
- **Backward compatibility is total:** every new `Arena*` parameter and arena constructor defaults to heap (`nullptr`); every pre-existing test must pass unchanged.
- `ArenaAllocator<T>` is fail-closed: arena refusal (over budget) → `throw std::bad_alloc`. All `propagate_on_container_*` traits are `std::true_type`; `is_always_equal` is `std::false_type`.
- The retrofit moves *where* memory lives, never *what* is computed: the arena render must be **byte-for-byte pixel-identical** to the heap render.
- The CLI budget is **measured** (`high_water` of a measure pass), never a magic constant; pass 2 renders at exactly that budget with `refusals == 0`.
- Honest bounds (already in the spec): bump arena ⇒ `high_water` is the cumulative frame footprint; the arena's backing slab is the one remaining real malloc; `n*sizeof(T)` overflow is unguarded (counts are frame-bounded, the budget is the backstop).
- Build (Windows/MSVC multi-config): `cmake --build native/build --config Release`; test `ctest --test-dir native/build -C Release -R <name> --output-on-failure`. New `src/*.cpp` and `tests/test_*.cpp` are auto-globbed; if a new file isn't picked up, run `cmake -B native/build -S native` once then rebuild.

---

### Task 1: `ArenaAllocator<T>` — the hand-written allocator over `raw::Arena`

**Files:**
- Create: `native/raw/arena_allocator.hpp`
- Create: `native/tests/test_arena_allocator.cpp`

**Interfaces:**
- Consumes: `raw::Arena` (`raw/arena.hpp`: `void* allocate(std::size_t n, std::size_t align)`, `const ArenaStats& stats() const`).
- Produces: `template<class T> struct raw::ArenaAllocator { value_type=T; Arena* arena_; ArenaAllocator(); explicit ArenaAllocator(Arena*); T* allocate(std::size_t n); void deallocate(T*, std::size_t); }` with `propagate_on_container_*` = true, `is_always_equal` = false.

- [ ] **Step 1: Write the failing test**

`native/tests/test_arena_allocator.cpp`:
```cpp
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_arena_allocator`
Expected: FAIL — `raw/arena_allocator.hpp` does not exist. (If not picked up: `cmake -B native/build -S native`, then rebuild.)

- [ ] **Step 3: Write minimal implementation**

`native/raw/arena_allocator.hpp`:
```cpp
#pragma once
#include "raw/arena.hpp"
#include <cstddef>
#include <new>
#include <type_traits>
namespace raw {
// A std::allocator that draws bytes from a raw::Arena (arena_ != null) or the heap
// (arena_ == null, the default — so existing containers are byte-for-byte unchanged).
// Over-budget arena allocation is fail-closed: it throws std::bad_alloc.
template<class T>
struct ArenaAllocator {
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::false_type;

    Arena* arena_{nullptr};
    ArenaAllocator() noexcept = default;                          // heap mode
    explicit ArenaAllocator(Arena* a) noexcept : arena_(a) {}     // arena mode
    template<class U> ArenaAllocator(const ArenaAllocator<U>& o) noexcept : arena_(o.arena_) {}

    T* allocate(std::size_t n){
        void* p = arena_ ? arena_->allocate(n * sizeof(T), alignof(T))
                         : ::operator new(n * sizeof(T));
        if (!p) throw std::bad_alloc();      // arena refusal (over budget) => fail-closed
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept {
        if (!arena_) ::operator delete(p);   // heap frees; arena is bump (free is a no-op)
    }
    template<class U> bool operator==(const ArenaAllocator<U>& o) const noexcept { return arena_ == o.arena_; }
    template<class U> bool operator!=(const ArenaAllocator<U>& o) const noexcept { return arena_ != o.arena_; }
};
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_arena_allocator --output-on-failure`
Expected: `test_arena_allocator` PASS. Full suite: `ctest --test-dir native/build -C Release` (18/18).

- [ ] **Step 5: Commit**

```bash
git add native/raw/arena_allocator.hpp native/tests/test_arena_allocator.cpp
git commit -m "feat(native): ArenaAllocator<T> — hand-written std::allocator over raw::Arena"
```

---

### Task 2: `Buffer<T>` arena-aware

**Files:**
- Modify: `native/raw/image.hpp`
- Create: `native/tests/test_buffer_arena.cpp`

**Interfaces:**
- Consumes: `raw::ArenaAllocator<T>`, `raw::Arena`.
- Produces: `Buffer<T>` now holds `std::vector<T, ArenaAllocator<T>> px`; adds `Buffer() = default;` and `explicit Buffer(Arena*)`. `resize`, `at`, `.px` unchanged in API.

- [ ] **Step 1: Write the failing test**

`native/tests/test_buffer_arena.cpp`:
```cpp
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
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_buffer_arena`
Expected: FAIL — `Buffer<float>(&a)` does not compile (no `Arena*` constructor yet).

- [ ] **Step 3: Write minimal implementation**

Replace `native/raw/image.hpp` with:
```cpp
#pragma once
#include "raw/vec.hpp"
#include "raw/arena_allocator.hpp"
#include <vector>
#include <string>
namespace raw {
template<class T> struct Buffer {
    int w{0}, h{0};
    std::vector<T, ArenaAllocator<T>> px;
    Buffer() = default;                                  // heap (default allocator)
    explicit Buffer(Arena* a) : px(ArenaAllocator<T>(a)) {}  // arena-backed
    void resize(int W, int H){ w=W; h=H; px.assign((size_t)W*H, T{}); }
    T& at(int x,int y){ return px[(size_t)y*w + x]; }
    const T& at(int x,int y) const { return px[(size_t)y*w + x]; }
};
void writePPM(const Buffer<Vec3>& img, const std::string& path);
void writePGM(const Buffer<float>& img, const std::string& path);
}
```
(The copy/move special members remain implicitly generated — `Buffer` stays copyable and movable, so functions can return it by value and the allocator travels with the move.)

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_buffer_arena --output-on-failure`
Expected: `test_buffer_arena` PASS. Full suite green (`ctest --test-dir native/build -C Release`, 19/19) — confirm `test_image`, `test_raster`, `test_scene`, `test_reconcile` etc. still pass (the `.px` sites and `.at()` writers are unchanged).

- [ ] **Step 5: Commit**

```bash
git add native/raw/image.hpp native/tests/test_buffer_arena.cpp
git commit -m "feat(native): Buffer<T> arena-aware (heap default, opt-in Arena*)"
```

---

### Task 3: `GBuffer` arena-aware

**Files:**
- Modify: `native/raw/gbuffer.hpp`
- Create: `native/tests/test_gbuffer_arena.cpp`

**Interfaces:**
- Consumes: `Buffer<T>(Arena*)`, `raw::Arena`.
- Produces: `GBuffer` adds `GBuffer() = default;` and `explicit GBuffer(Arena* a)` constructing all five planes arena-aware; `resize(int,int)` unchanged.

- [ ] **Step 1: Write the failing test**

`native/tests/test_gbuffer_arena.cpp`:
```cpp
#include "raw/gbuffer.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
using namespace raw;
int main() {
    std::array<std::uint8_t, 65536> backing{};
    Arena a(backing.data(), backing.size());
    GBuffer g(&a);
    g.resize(16, 16);
    CHECK(g.w == 16 && g.h == 16);
    // all five planes drew from the arena
    CHECK(a.stats().used >= 16 * 16 * (sizeof(float) + 3 * sizeof(Vec3) + sizeof(std::uint8_t)));
    CHECK(g.mask.at(0, 0) == 0);              // zero-filled
    g.mask.at(1, 1) = 1;  CHECK(g.mask.at(1, 1) == 1);
    g.depth.at(2, 2) = 5.0f;  CHECK(g.depth.at(2, 2) == 5.0f);

    // default GBuffer is still heap and works
    GBuffer h; h.resize(4, 4);
    CHECK(h.w == 4 && h.h == 4);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_gbuffer_arena`
Expected: FAIL — `GBuffer(&a)` does not compile (no `Arena*` constructor yet).

- [ ] **Step 3: Write minimal implementation**

Replace `native/raw/gbuffer.hpp` with:
```cpp
#pragma once
#include "raw/vec.hpp"
#include "raw/image.hpp"
#include "raw/arena.hpp"
#include <cstdint>
namespace raw {
struct GBuffer {
    int w{0}, h{0};
    Buffer<float> depth;
    Buffer<Vec3> normal, position, albedo;
    Buffer<uint8_t> mask;
    GBuffer() = default;
    explicit GBuffer(Arena* a)
        : depth(a), normal(a), position(a), albedo(a), mask(a) {}
    void resize(int W,int H){ w=W;h=H;
        depth.resize(W,H); normal.resize(W,H); position.resize(W,H);
        albedo.resize(W,H); mask.resize(W,H); }
};
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_gbuffer_arena --output-on-failure`
Expected: `test_gbuffer_arena` PASS. Full suite green (20/20).

- [ ] **Step 5: Commit**

```bash
git add native/raw/gbuffer.hpp native/tests/test_gbuffer_arena.cpp
git commit -m "feat(native): GBuffer arena-aware (all five planes)"
```

---

### Task 4: `Scene`/`Mesh` arena-aware + `buildTestScene(w,h,Arena*)`

**Files:**
- Modify: `native/raw/scene.hpp`
- Modify: `native/src/scene.cpp`
- Create: `native/tests/test_scene_arena.cpp`

**Interfaces:**
- Consumes: `raw::ArenaAllocator<T>`, `raw::Arena`.
- Produces: `Mesh` holds `std::vector<Vec3, ArenaAllocator<Vec3>> positions, normals` and `std::vector<int, ArenaAllocator<int>> indices`, with `Mesh()=default` and `explicit Mesh(Arena*)`. `Scene` holds `std::vector<Mesh, ArenaAllocator<Mesh>> meshes` and `std::vector<Light, ArenaAllocator<Light>> lights`, with `Scene()=default` and `explicit Scene(Arena*)`. `Scene buildTestScene(int w, int h, Arena* arena = nullptr);`

- [ ] **Step 1: Write the failing test**

`native/tests/test_scene_arena.cpp`:
```cpp
#include "raw/scene.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
using namespace raw;
int main() {
    // heap scene (default) — unchanged behavior
    Scene heap = buildTestScene(256, 256);
    CHECK(heap.meshes.size() == 2);
    CHECK(heap.lights.size() == 1);
    std::size_t tris = 0;
    for (auto& m : heap.meshes) tris += m.indices.size() / 3;
    CHECK(tris > 0);

    // arena scene — geometry drawn from the arena, identical structure
    std::array<std::uint8_t, 65536> backing{};
    Arena a(backing.data(), backing.size());
    Scene as = buildTestScene(256, 256, &a);
    CHECK(as.meshes.size() == 2);
    CHECK(a.stats().used > 0);                 // geometry consumed arena bytes
    std::size_t tris2 = 0;
    for (auto& m : as.meshes) tris2 += m.indices.size() / 3;
    CHECK(tris2 == tris);                       // same geometry, different memory
    CHECK(as.meshes[0].positions[0].x == heap.meshes[0].positions[0].x);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_scene_arena`
Expected: FAIL — `buildTestScene(256,256,&a)` does not compile (no 3-arg form / no arena constructors yet).

- [ ] **Step 3: Write minimal implementation**

Replace `native/raw/scene.hpp` with:
```cpp
#pragma once
#include "raw/vec.hpp"
#include "raw/mat.hpp"
#include "raw/arena_allocator.hpp"
#include <vector>
namespace raw {
struct Material { Vec3 albedo{0.8f,0.8f,0.8f}; };
struct Mesh {
    std::vector<Vec3, ArenaAllocator<Vec3>> positions, normals;
    std::vector<int,  ArenaAllocator<int>>  indices;
    Material material;
    Mesh() = default;
    explicit Mesh(Arena* a)
        : positions(ArenaAllocator<Vec3>(a)), normals(ArenaAllocator<Vec3>(a)),
          indices(ArenaAllocator<int>(a)) {}
};
struct Light { Vec3 dir{0,-1,0}; float intensity{1.0f}; };
struct Camera {
    Vec3 eye{0,0,5}, center{0,0,0}, up{0,1,0};
    float fovy{1.0f}, aspect{1.0f}, nearZ{0.1f}, farZ{100.0f};
    Mat4 view() const { return lookAt(eye, center, up); }
    Mat4 proj() const { return perspective(fovy, aspect, nearZ, farZ); }
};
struct Scene {
    std::vector<Mesh,  ArenaAllocator<Mesh>>  meshes;
    std::vector<Light, ArenaAllocator<Light>> lights;
    Camera camera;
    Scene() = default;
    explicit Scene(Arena* a)
        : meshes(ArenaAllocator<Mesh>(a)), lights(ArenaAllocator<Light>(a)) {}
};
Scene buildTestScene(int w, int h, Arena* arena = nullptr);
}
```

Replace `native/src/scene.cpp` with (only the signature + the `Scene s`/`Mesh`/`reserve`/`move` lines change; the quad/box geometry is identical):
```cpp
#include "raw/scene.hpp"
#include <utility>
namespace raw {
static void addQuad(Mesh& m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n){
    int base = (int)m.positions.size();
    for (Vec3 p : {a,b,c,d}){ m.positions.push_back(p); m.normals.push_back(n); }
    for (int i : {0,1,2, 0,2,3}) m.indices.push_back(base + i);
}
static void addBox(Mesh& m, Vec3 c, float h){
    Vec3 p000{c.x-h,c.y-h,c.z-h}, p111{c.x+h,c.y+h,c.z+h};
    addQuad(m, {p000.x,p000.y,p111.z},{p111.x,p000.y,p111.z},{p111.x,p111.y,p111.z},{p000.x,p111.y,p111.z}, {0,0,1});
    addQuad(m, {p111.x,p000.y,p000.z},{p000.x,p000.y,p000.z},{p000.x,p111.y,p000.z},{p111.x,p111.y,p000.z}, {0,0,-1});
    addQuad(m, {p000.x,p111.y,p111.z},{p111.x,p111.y,p111.z},{p111.x,p111.y,p000.z},{p000.x,p111.y,p000.z}, {0,1,0});
    addQuad(m, {p000.x,p000.y,p000.z},{p111.x,p000.y,p000.z},{p111.x,p000.y,p111.z},{p000.x,p000.y,p111.z}, {0,-1,0});
    addQuad(m, {p111.x,p000.y,p111.z},{p111.x,p000.y,p000.z},{p111.x,p111.y,p000.z},{p111.x,p111.y,p111.z}, {1,0,0});
    addQuad(m, {p000.x,p000.y,p000.z},{p000.x,p000.y,p111.z},{p000.x,p111.y,p111.z},{p000.x,p111.y,p000.z}, {-1,0,0});
}
Scene buildTestScene(int w, int h, Arena* arena){
    Scene s(arena);
    s.meshes.reserve(2);
    Mesh plane(arena); plane.material.albedo = {0.7f,0.7f,0.7f};
    addQuad(plane, {-5,0,-5},{5,0,-5},{5,0,5},{-5,0,5}, {0,1,0});
    s.meshes.push_back(std::move(plane));
    Mesh box(arena); box.material.albedo = {0.8f,0.3f,0.2f};
    addBox(box, {0,1,0}, 1.0f);
    s.meshes.push_back(std::move(box));
    s.lights.reserve(1);
    s.lights.push_back(Light{ normalize(Vec3{-0.4f,-1.0f,-0.3f}), 1.0f });
    s.camera.eye = {4,4,6}; s.camera.center = {0,1,0}; s.camera.up = {0,1,0};
    s.camera.fovy = 0.9f; s.camera.aspect = (float)w/(float)h;
    return s;
}
}
```
(`Scene(arena)`/`Mesh(arena)` with `arena==nullptr` yield heap allocators, so the default `buildTestScene(w,h)` is byte-identical to today. Meshes are built in place and **moved**, so each mesh's inner vectors keep their arena allocators; `reserve(2)` avoids the outer-vector regrow.)

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_scene_arena --output-on-failure`
Expected: `test_scene_arena` PASS. Full suite green (21/21) — `test_scene` (the existing heap test) must still pass unchanged.

- [ ] **Step 5: Commit**

```bash
git add native/raw/scene.hpp native/src/scene.cpp native/tests/test_scene_arena.cpp
git commit -m "feat(native): Scene/Mesh arena-aware + buildTestScene(w,h,Arena*)"
```

---

### Task 5: `LinearAccel::build(s, Arena*)`

**Files:**
- Modify: `native/raw/accel.hpp`
- Modify: `native/src/accel.cpp`
- Create: `native/tests/test_accel_arena.cpp`

**Interfaces:**
- Consumes: `raw::ArenaAllocator<Tri>`, `raw::Arena`, `Scene`.
- Produces: `LinearAccel::tris` is `std::vector<Tri, ArenaAllocator<Tri>>`; `void build(const Scene& s, Arena* arena = nullptr);` `occluded` unchanged.

- [ ] **Step 1: Write the failing test**

`native/tests/test_accel_arena.cpp`:
```cpp
#include "raw/accel.hpp"
#include "raw/scene.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <array>
#include <cstdint>
using namespace raw;
int main() {
    Scene s = buildTestScene(64, 64);          // heap scene is a fine input
    LinearAccel h; h.build(s);                  // heap accel
    CHECK(h.tris.size() > 0);

    std::array<std::uint8_t, 65536> backing{};
    Arena a(backing.data(), backing.size());
    LinearAccel ab; ab.build(s, &a);            // arena accel
    CHECK(ab.tris.size() == h.tris.size());     // same triangles
    CHECK(a.stats().used >= ab.tris.size() * sizeof(Tri));

    // occlusion is identical: a downward ray over the box hits in both
    Ray probe{ Vec3{0,5,0}, Vec3{0,-1,0} };
    CHECK(ab.occluded(probe, 100.0f) == h.occluded(probe, 100.0f));
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_accel_arena`
Expected: FAIL — `build(s, &a)` does not compile (no 2-arg form yet).

- [ ] **Step 3: Write minimal implementation**

Replace `native/raw/accel.hpp` with:
```cpp
#pragma once
#include "raw/primitives.hpp"
#include "raw/scene.hpp"
#include "raw/arena_allocator.hpp"
#include <vector>
namespace raw {
struct LinearAccel {
    std::vector<Tri, ArenaAllocator<Tri>> tris;
    void build(const Scene& s, Arena* arena = nullptr);
    bool occluded(const Ray& r, float maxDist) const;
};
}
```

Replace `native/src/accel.cpp` with:
```cpp
#include "raw/accel.hpp"
namespace raw {
void LinearAccel::build(const Scene& s, Arena* arena){
    tris = std::vector<Tri, ArenaAllocator<Tri>>(ArenaAllocator<Tri>(arena));
    std::size_t count = 0;
    for (const Mesh& m : s.meshes) count += m.indices.size() / 3;
    tris.reserve(count);
    for (const Mesh& m : s.meshes)
        for (size_t i=0;i+2<m.indices.size();i+=3)
            tris.push_back(Tri{ m.positions[m.indices[i]],
                                m.positions[m.indices[i+1]],
                                m.positions[m.indices[i+2]] });
}
bool LinearAccel::occluded(const Ray& r, float maxDist) const {
    float t,u,v;
    for (const Tri& tri : tris)
        if (intersectTri(r, tri, t,u,v) && t < maxDist) return true;
    return false;
}
}
```
(The move-assignment `tris = vector(alloc)` is well-defined because `propagate_on_container_move_assignment` is `true`; `reserve(count)` avoids growth churn in the arena.)

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_accel_arena --output-on-failure`
Expected: `test_accel_arena` PASS. Full suite green (22/22) — `test_accel` (existing) still passes.

- [ ] **Step 5: Commit**

```bash
git add native/raw/accel.hpp native/src/accel.cpp native/tests/test_accel_arena.cpp
git commit -m "feat(native): LinearAccel triangles arena-aware (build threads Arena*)"
```

---

### Task 6: The five render functions thread `Arena*`

**Files:**
- Modify: `native/raw/raster.hpp`, `native/src/raster.cpp`
- Modify: `native/raw/ray_ao.hpp`, `native/src/ray_ao.cpp`
- Modify: `native/raw/ssao.hpp`, `native/src/ssao.cpp`
- Modify: `native/raw/reconcile.hpp`, `native/src/reconcile.cpp`
- Modify: `native/raw/composite.hpp`, `native/src/composite.cpp`
- Create: `native/tests/test_render_arena.cpp`

**Interfaces:**
- Consumes: `GBuffer(Arena*)`, `Buffer<T>(Arena*)`, `buildTestScene(...,Arena*)`, `LinearAccel::build(...,Arena*)`.
- Produces (each gains a trailing `Arena* arena = nullptr` and constructs its output with it):
  - `GBuffer rasterize(const Scene& scene, int w, int h, Arena* arena = nullptr);`
  - `Buffer<float> computeRTAO(const GBuffer& g, const LinearAccel& accel, int samples, float radius, Arena* arena = nullptr);`
  - `Buffer<float> computeSSAO(const GBuffer& g, int samples, float radius, Arena* arena = nullptr);`
  - `ReconcileResult reconcile(const Buffer<float>& approx, const Buffer<float>& truth, const Buffer<uint8_t>& mask, float tolerance, Arena* arena = nullptr);`
  - `Buffer<Vec3> shade(const GBuffer& g, const Buffer<float>& ao, const Scene& s, Arena* arena = nullptr);`

- [ ] **Step 1: Write the failing test**

`native/tests/test_render_arena.cpp`:
```cpp
#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <vector>
#include <cstdint>
using namespace raw;
int main() {
    const int W = 64, H = 64;
    // heap render
    Scene sh = buildTestScene(W, H);
    GBuffer gh = rasterize(sh, W, H);
    LinearAccel ah; ah.build(sh);
    Buffer<float> aoRTh = computeRTAO(gh, ah, 16, 2.0f);
    Buffer<float> aoSSh = computeSSAO(gh, 12, 2.0f);
    ReconcileResult rh = reconcile(aoSSh, aoRTh, gh.mask, 0.12f);
    Buffer<Vec3> fh = shade(gh, aoRTh, sh);

    // arena render (8 MB heap-backed slab is plenty for 64x64)
    std::vector<std::uint8_t> slab(8u << 20);
    Arena a(slab.data(), slab.size());
    Scene sa = buildTestScene(W, H, &a);
    GBuffer ga = rasterize(sa, W, H, &a);
    LinearAccel aa; aa.build(sa, &a);
    Buffer<float> aoRTa = computeRTAO(ga, aa, 16, 2.0f, &a);
    Buffer<float> aoSSa = computeSSAO(ga, 12, 2.0f, &a);
    ReconcileResult ra = reconcile(aoSSa, aoRTa, ga.mask, 0.12f, &a);
    Buffer<Vec3> fa = shade(ga, aoRTa, sa, &a);
    CHECK(a.stats().refusals == 0);

    // PIXEL-IDENTICAL: the arena moves memory, it does not change the render
    bool identical = true;
    for (int y = 0; y < H && identical; ++y) for (int x = 0; x < W; ++x){
        Vec3 p = fh.at(x,y), q = fa.at(x,y);
        if (p.x != q.x || p.y != q.y || p.z != q.z){ identical = false; break; }
    }
    CHECK(identical);
    CHECK(ra.rmse == rh.rmse);
    CHECK(ra.pixels == rh.pixels);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_render_arena`
Expected: FAIL — the 4-/5-arg arena overloads do not compile yet.

- [ ] **Step 3: Write minimal implementation**

Edit each header to add the trailing `Arena* arena = nullptr` (signatures above). Then edit each impl so the OUTPUT buffer is constructed with `arena`:

`native/src/raster.cpp` — change the signature and the first two lines of the body:
```cpp
GBuffer rasterize(const Scene& scene, int w, int h, Arena* arena){
    GBuffer g(arena); g.resize(w,h);
    for (auto& d : g.depth.px) d = std::numeric_limits<float>::infinity();
    // ... the entire rest of the function is unchanged ...
```
`native/src/ray_ao.cpp` — signature + the output line:
```cpp
Buffer<float> computeRTAO(const GBuffer& g, const LinearAccel& accel,
                          int samples, float radius, Arena* arena){
    Buffer<float> ao(arena); ao.resize(g.w, g.h);
    // ... unchanged ...
```
`native/src/ssao.cpp` — signature + the output line:
```cpp
Buffer<float> computeSSAO(const GBuffer& g, int samples, float radius, Arena* arena){
    Buffer<float> ao(arena); ao.resize(g.w,g.h);
    // ... unchanged ...
```
`native/src/reconcile.cpp` — signature + arena-back `errorMap`:
```cpp
ReconcileResult reconcile(const Buffer<float>& approx, const Buffer<float>& truth,
                          const Buffer<uint8_t>& mask, float tolerance, Arena* arena){
    ReconcileResult r;
    r.errorMap = Buffer<float>(arena);
    r.errorMap.resize(approx.w, approx.h);
    // ... the rest (sumSq loop, rmse, withinTolerance) is unchanged ...
```
`native/src/composite.cpp` — signature + the output line:
```cpp
Buffer<Vec3> shade(const GBuffer& g, const Buffer<float>& ao, const Scene& s, Arena* arena){
    Buffer<Vec3> img(arena); img.resize(g.w,g.h);
    // ... unchanged ...
```
Add `#include "raw/arena.hpp"` to any of the five headers that does not already transitively include it (raster.hpp via gbuffer→arena is covered; ray_ao.hpp/ssao.hpp/reconcile.hpp/composite.hpp should each `#include "raw/arena.hpp"` to name `Arena`).

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_render_arena --output-on-failure`
Expected: `test_render_arena` PASS (pixel-identical holds). Full suite green (23/23) — every existing render test still passes through the defaulted heap path.

- [ ] **Step 5: Commit**

```bash
git add native/raw/raster.hpp native/src/raster.cpp native/raw/ray_ao.hpp native/src/ray_ao.cpp native/raw/ssao.hpp native/src/ssao.cpp native/raw/reconcile.hpp native/src/reconcile.cpp native/raw/composite.hpp native/src/composite.cpp native/tests/test_render_arena.cpp
git commit -m "feat(native): render functions thread Arena* (whole frame on the substrate)"
```

---

### Task 7: CLI measured-budget frame + both Certificates + the gate tests

**Files:**
- Modify: `native/app/main.cpp`
- Create: `native/tests/test_frame_arena.cpp`

**Interfaces:**
- Consumes: every arena-threaded function above + `certificate_from_reconcile`, `certificate_from_arena`, `to_json`, `arena_witness`.
- Produces: the CLI renders the whole frame through one measured-budget `Arena`, writing `certificate.json` (reconcile) and `arena_certificate.json` (arena), and printing both verdicts + the arena witness.

- [ ] **Step 1: Write the failing test**

`native/tests/test_frame_arena.cpp`:
```cpp
#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include "raw/certificate.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <vector>
#include <cstdint>
#include <string>
#include <new>
using namespace raw;
// Render the whole frame through `arena`; returns the shaded frame.
static Buffer<Vec3> renderFrame(int W, int H, Arena* arena){
    Scene s = buildTestScene(W, H, arena);
    GBuffer g = rasterize(s, W, H, arena);
    LinearAccel accel; accel.build(s, arena);
    Buffer<float> aoRT = computeRTAO(g, accel, 16, 2.0f, arena);
    Buffer<float> aoSS = computeSSAO(g, 12, 2.0f, arena);
    ReconcileResult rec = reconcile(aoSS, aoRT, g.mask, 0.12f, arena);
    return shade(g, aoRT, s, arena);
}
int main() {
    const int W = 64, H = 64;

    // (a) MEASURED-BUDGET BOUNDED: measure H, then render at exactly H.
    std::vector<std::uint8_t> slab1(8u << 20);
    Arena measure(slab1.data(), slab1.size());
    renderFrame(W, H, &measure);
    std::size_t Hbytes = measure.stats().high_water;
    CHECK(Hbytes > 0);
    std::vector<std::uint8_t> slab2(Hbytes);
    Arena tight(slab2.data(), Hbytes);
    renderFrame(W, H, &tight);
    CHECK(tight.stats().refusals == 0);
    CHECK(tight.stats().used == Hbytes);
    CHECK(tight.stats().high_water == Hbytes);
    CHECK(to_json(certificate_from_arena(tight.stats())).find("\"verdict\":\"verified\"") != std::string::npos);

    // (b) FAIL-CLOSED BREACH: a tiny budget throws and is witnessed BREACHED.
    std::vector<std::uint8_t> tinyslab(4096);
    Arena tiny(tinyslab.data(), tinyslab.size());
    bool threw = false;
    try { renderFrame(W, H, &tiny); } catch (const std::bad_alloc&) { threw = true; }
    CHECK(threw);
    CHECK(tiny.stats().refusals > 0);
    CHECK(to_json(certificate_from_arena(tiny.stats())).find("\"verdict\":\"refuted\"") != std::string::npos);

    // (c) PIXEL-IDENTICAL: arena frame == heap frame.
    Buffer<Vec3> heapFrame = renderFrame(W, H, nullptr);
    std::vector<std::uint8_t> slab3(8u << 20);
    Arena a3(slab3.data(), slab3.size());
    Buffer<Vec3> arenaFrame = renderFrame(W, H, &a3);
    bool identical = true;
    for (int y = 0; y < H && identical; ++y) for (int x = 0; x < W; ++x){
        Vec3 p = heapFrame.at(x,y), q = arenaFrame.at(x,y);
        if (p.x != q.x || p.y != q.y || p.z != q.z){ identical = false; break; }
    }
    CHECK(identical);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_frame_arena`
Expected: the test COMPILES and PASSES immediately — every arena function exists (Tasks 4–6). This is the contract guard for the two-pass measured budget + fail-closed breach + pixel-identity; the new behavior in this task is the CLI wiring (Step 3), verified by running the CLI in Step 4. (If it does not build because the file isn't picked up: `cmake -B native/build -S native`, then rebuild.)

- [ ] **Step 3: Write minimal implementation (wire the CLI)**

Replace `native/app/main.cpp` with:
```cpp
#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include "raw/certificate.hpp"
#include "raw/arena.hpp"
#include <string>
#include <vector>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <new>
#include <utility>
using namespace raw;
struct FrameOut { Buffer<Vec3> frame; Buffer<float> aoRT, aoSS; ReconcileResult rec; };
static FrameOut renderFrame(int W, int H, Arena* arena){
    Scene s = buildTestScene(W, H, arena);
    GBuffer g = rasterize(s, W, H, arena);
    LinearAccel accel; accel.build(s, arena);
    Buffer<float> aoRT = computeRTAO(g, accel, 64, 2.0f, arena);
    Buffer<float> aoSS = computeSSAO(g, 24, 2.0f, arena);
    ReconcileResult rec = reconcile(aoSS, aoRT, g.mask, 0.12f, arena);
    Buffer<Vec3> frame = shade(g, aoRT, s, arena);
    return FrameOut{ std::move(frame), std::move(aoRT), std::move(aoSS), std::move(rec) };
}
int main(int argc, char** argv){
    std::string out = argc > 1 ? argv[1] : ".";
    const int W = 256, H = 256;

    // PASS 1 — MEASURE the footprint in a computed generous slab (no magic constant).
    const std::size_t PER_PIXEL_UPPER =
        sizeof(float) + 3*sizeof(Vec3) + sizeof(std::uint8_t)   // gbuffer: depth+nrm/pos/alb+mask
        + 3*sizeof(float) + sizeof(Vec3);                       // aoRT + aoSS + errorMap + frame
    std::size_t slabUB = (std::size_t)W*H*PER_PIXEL_UPPER*2 + (1u<<20);
    std::vector<std::uint8_t> slab1(slabUB);
    Arena measure(slab1.data(), slabUB);
    try { (void)renderFrame(W, H, &measure); }
    catch (const std::bad_alloc&){ std::printf("measure pass overflowed slab — raise PER_PIXEL_UPPER\n"); return 2; }
    std::size_t Hbytes = measure.stats().high_water;

    // PASS 2 — render within a budget of EXACTLY the measured footprint.
    std::vector<std::uint8_t> slab2(Hbytes);
    Arena arena(slab2.data(), Hbytes);
    FrameOut o;
    try { o = renderFrame(W, H, &arena); }
    catch (const std::bad_alloc&){
        Certificate br = certificate_from_arena(arena.stats());
        std::ofstream(out + "/arena_certificate.json") << to_json(br);
        std::printf("arena: %s\n", to_json(br).c_str());     // BREACHED, fail-closed
        return 1;
    }
    writePPM(o.frame, out + "/frame.ppm");
    writePGM(o.aoRT,  out + "/ao_rt.pgm");
    writePGM(o.aoSS,  out + "/ao_ss.pgm");
    writePGM(o.rec.errorMap, out + "/ao_error.pgm");

    Certificate aoCert    = certificate_from_reconcile(o.rec, 0.12f);
    Certificate arenaCert = certificate_from_arena(arena.stats());
    std::ofstream(out + "/certificate.json")        << to_json(aoCert);
    std::ofstream(out + "/arena_certificate.json")  << to_json(arenaCert);

    std::printf("reconcile: pixels=%d rmse=%.4f maxError=%.4f verdict=%s\n",
        o.rec.pixels, o.rec.rmse, o.rec.maxError,
        o.rec.withinTolerance ? "WITHIN-TOLERANCE" : "DIVERGENT");
    std::printf("certificate: %s\n", to_json(aoCert).c_str());
    std::printf("arena: %s\n", arena_witness(arena.stats()).c_str());
    std::printf("arena-certificate: %s\n", to_json(arenaCert).c_str());
    return 0;
}
```

- [ ] **Step 4: Run to verify it passes + emits both Certificates**

Run:
```bash
cmake --build native/build --config Release
ctest --test-dir native/build -C Release -R test_frame_arena --output-on-failure
./native/build/Release/raw_native_cli.exe native/build
```
Expected: `test_frame_arena` PASS; the CLI prints `reconcile: … verdict=DIVERGENT`, `arena: … verdict=BOUNDED`, and an `arena-certificate: {…"verdict":"verified"…}` line; it writes `native/build/arena_certificate.json` and `native/build/certificate.json`. Full suite green: `ctest --test-dir native/build -C Release` (24/24).

- [ ] **Step 5: Commit**

```bash
git add native/app/main.cpp native/tests/test_frame_arena.cpp
git commit -m "feat(native): CLI renders the whole frame on a measured-budget arena + emits the arena Certificate"
```

---

## Final verification

- [ ] Full suite green: `ctest --test-dir native/build -C Release` (expect 24/24).
- [ ] CLI emits BOTH `certificate.json` (reconcile → `refuted`/DIVERGENT) and `arena_certificate.json` (arena → `verified`/BOUNDED); the arena witness shows `budget == used == high_water` with `refusals=0`.
- [ ] Cross-language seam still holds: re-run the increment-3 Python check on `certificate.json` (verdict ∈ `coherence_membrane.Verdict`); optionally the same on `arena_certificate.json`.
- [ ] Zero third-party includes (only `<new>`, `<cstddef>`, `<type_traits>`, `<utility>` added).
- [ ] Every pre-existing test passes unchanged (backward compatibility).

## Deferred (increment 5+)

- `reset()`-per-frame loop (render N frames reusing one arena) to show zero re-allocation across frames.
- Sub-arenas / tagged regions (geometry vs frame-scratch) for finer witness granularity.
- `compose([reconcile, arena])` → one frame-level Certificate; the standing cross-language shape-gate flagged in increment 3's final review.
