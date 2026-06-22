# RAW Native Engine — First Increment Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build "the first reconciled frame" — render a small scene to a G-buffer, compute ambient occlusion two ways (a fast screen-space approximation and a ray-traced ground truth), reconcile them into an error map + verdict, and write the results as image files — with zero third-party dependencies.

**Architecture:** One shared scene feeds a software rasterizer (→ G-buffer) and a ray engine (→ ground-truth AO). A screen-space effect produces an approximate AO from the G-buffer. A reconcile unit measures approximation vs truth. A CLI orchestrates and writes PPM images. All units are layered; lower layers do not know higher ones.

**Tech Stack:** C++23, standard library only. CMake + CTest. No DirectX, no vcpkg, no external libraries. Image output is hand-written PPM (P6/P5).

## Global Constraints

- **Language:** C++23, standard library only. No third-party headers or libraries. (verbatim from spec §2)
- **No DirectX, no GPU driver, no window.** Output is image files. (spec §2)
- **Location:** everything under `native/` in the RAW repo. Do not modify `src/core`, `src/d3d11_proxy`, or `Shaders/`. (spec §2)
- **Coordinate system:** right-handed; camera looks down −Z; world-space normals stored in the G-buffer. (spec §4)
- **Determinism:** AO sampling uses a hash of (pixel, sample-index), never global RNG state, so results are reproducible and testable. (spec §7)
- **Include style:** headers under `native/raw/`, included as `#include "raw/<unit>.hpp"`. Implementations under `native/src/`. Tests under `native/tests/`.
- **Namespace:** all engine code in `namespace raw`.

---

### Task 1: Build scaffold + zero-dep test harness

**Files:**
- Create: `native/CMakeLists.txt`
- Create: `native/tests/check.hpp`
- Create: `native/src/raw_native.cpp`
- Create: `native/raw/version.hpp`
- Create: `native/tests/test_sanity.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `raw::version()` → `const char*`; test macros `CHECK(cond)`, `CHECK_NEAR(a,b,eps)`, and `int raw_test_summary()` returning process exit code (0 = all passed).

- [ ] **Step 1: Write the failing test**

`native/tests/test_sanity.cpp`:
```cpp
#include "raw/version.hpp"
#include "check.hpp"
#include <cstring>

int main() {
    CHECK(std::strlen(raw::version()) > 0);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake -B native/build -S native && cmake --build native/build`
Expected: FAIL — `raw/version.hpp` and `check.hpp` do not exist (compile error).

- [ ] **Step 3: Write minimal implementation**

`native/tests/check.hpp`:
```cpp
#pragma once
#include <cmath>
#include <cstdio>
inline int& raw_test_failures() { static int f = 0; return f; }
#define CHECK(cond) do { if (!(cond)) { \
    std::printf("CHECK failed: %s (%s:%d)\n", #cond, __FILE__, __LINE__); \
    ++raw_test_failures(); } } while (0)
#define CHECK_NEAR(a, b, eps) do { double da=(a), db=(b); \
    if (std::fabs(da-db) > (eps)) { \
    std::printf("CHECK_NEAR failed: %s=%g vs %s=%g eps=%g (%s:%d)\n", \
    #a, da, #b, db, (double)(eps), __FILE__, __LINE__); \
    ++raw_test_failures(); } } while (0)
inline int raw_test_summary() {
    if (raw_test_failures() == 0) { std::printf("OK\n"); return 0; }
    std::printf("%d CHECK(s) failed\n", raw_test_failures()); return 1;
}
```

`native/raw/version.hpp`:
```cpp
#pragma once
namespace raw { const char* version(); }
```

`native/src/raw_native.cpp`:
```cpp
#include "raw/version.hpp"
namespace raw { const char* version() { return "raw-native 0.1.0"; } }
```

`native/CMakeLists.txt`:
```cmake
cmake_minimum_required(VERSION 3.24)
project(raw_native LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB LIB_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(raw_native STATIC ${LIB_SRCS})
target_include_directories(raw_native PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")

enable_testing()
file(GLOB TEST_SRCS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_*.cpp")
foreach(t ${TEST_SRCS})
    get_filename_component(name ${t} NAME_WE)
    add_executable(${name} ${t})
    target_link_libraries(${name} PRIVATE raw_native)
    target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_test(NAME ${name} COMMAND ${name})
endforeach()

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/app/main.cpp")
    add_executable(raw_native_cli app/main.cpp)
    target_link_libraries(raw_native_cli PRIVATE raw_native)
endif()
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake -B native/build -S native && cmake --build native/build && ctest --test-dir native/build --output-on-failure`
Expected: `test_sanity` PASS (`OK`).

- [ ] **Step 5: Commit**

```bash
git add native/CMakeLists.txt native/tests/check.hpp native/src/raw_native.cpp native/raw/version.hpp native/tests/test_sanity.cpp
git commit -m "feat(native): zero-dep build scaffold + test harness"
```

---

### Task 2: math — vectors

**Files:**
- Create: `native/raw/vec.hpp`
- Create: `native/tests/test_vec.cpp`

**Interfaces:**
- Consumes: nothing.
- Produces: `raw::Vec3{float x,y,z}` with `+ - *(scalar)`, `dot(a,b)`, `cross(a,b)`, `length(v)`, `normalize(v)`; `raw::Vec4{float x,y,z,w}`.

- [ ] **Step 1: Write the failing test**

`native/tests/test_vec.cpp`:
```cpp
#include "raw/vec.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Vec3 a{1,2,3}, b{4,5,6};
    CHECK_NEAR(dot(a,b), 32.0, 1e-6);
    Vec3 c = cross(Vec3{1,0,0}, Vec3{0,1,0});
    CHECK_NEAR(c.x,0,1e-6); CHECK_NEAR(c.y,0,1e-6); CHECK_NEAR(c.z,1,1e-6);
    CHECK_NEAR(length(Vec3{3,4,0}), 5.0, 1e-6);
    Vec3 n = normalize(Vec3{0,3,0});
    CHECK_NEAR(n.y, 1.0, 1e-6);
    Vec3 s = (a + b) * 0.5f;
    CHECK_NEAR(s.x, 2.5, 1e-6);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_vec`
Expected: FAIL — `raw/vec.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/vec.hpp`:
```cpp
#pragma once
#include <cmath>
namespace raw {
struct Vec3 { float x{}, y{}, z{}; };
struct Vec4 { float x{}, y{}, z{}, w{}; };
inline Vec3 operator+(Vec3 a, Vec3 b){ return {a.x+b.x,a.y+b.y,a.z+b.z}; }
inline Vec3 operator-(Vec3 a, Vec3 b){ return {a.x-b.x,a.y-b.y,a.z-b.z}; }
inline Vec3 operator*(Vec3 a, float s){ return {a.x*s,a.y*s,a.z*s}; }
inline float dot(Vec3 a, Vec3 b){ return a.x*b.x+a.y*b.y+a.z*b.z; }
inline Vec3 cross(Vec3 a, Vec3 b){
    return {a.y*b.z-a.z*b.y, a.z*b.x-a.x*b.z, a.x*b.y-a.y*b.x}; }
inline float length(Vec3 v){ return std::sqrt(dot(v,v)); }
inline Vec3 normalize(Vec3 v){ float l=length(v); return l>0 ? v*(1.0f/l) : v; }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_vec --output-on-failure`
Expected: `test_vec` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/vec.hpp native/tests/test_vec.cpp
git commit -m "feat(native): vec3/vec4 math"
```

---

### Task 3: math — matrices & transforms

**Files:**
- Create: `native/raw/mat.hpp`
- Create: `native/tests/test_mat.cpp`

**Interfaces:**
- Consumes: `raw::Vec3`, `raw::Vec4` (vec.hpp).
- Produces: `raw::Mat4` (row-major `float m[16]`, index `r*4+c`); `mul(Mat4,Mat4)`; `mul(Mat4,Vec4)`; `translate(Vec3)`; `perspective(fovyRad,aspect,near,far)`; `lookAt(eye,center,up)`.

- [ ] **Step 1: Write the failing test**

`native/tests/test_mat.cpp`:
```cpp
#include "raw/mat.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    // translate moves a point
    Vec4 p = mul(translate({10,0,0}), Vec4{1,2,3,1});
    CHECK_NEAR(p.x, 11, 1e-5); CHECK_NEAR(p.y, 2, 1e-5);
    // lookAt: a point in front of the camera maps to negative view-space z
    Mat4 view = lookAt({0,0,5}, {0,0,0}, {0,1,0});
    Vec4 v = mul(view, Vec4{0,0,0,1});
    CHECK_NEAR(v.z, -5, 1e-4);
    // perspective: a point on the near plane center projects near NDC z in [-1,1]
    Mat4 proj = perspective(1.5708f, 1.0f, 0.1f, 100.0f);
    Vec4 c = mul(proj, Vec4{0,0,-1,1});
    CHECK(c.w > 0); // w = -view.z = 1
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_mat`
Expected: FAIL — `raw/mat.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/mat.hpp`:
```cpp
#pragma once
#include "raw/vec.hpp"
#include <cmath>
namespace raw {
struct Mat4 { float m[16]{}; }; // row-major: element(r,c) = m[r*4+c]
inline Mat4 identity(){ Mat4 r; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1; return r; }
inline Mat4 mul(const Mat4& a, const Mat4& b){
    Mat4 r;
    for (int i=0;i<4;++i) for (int j=0;j<4;++j){
        float s=0; for (int k=0;k<4;++k) s += a.m[i*4+k]*b.m[k*4+j];
        r.m[i*4+j]=s; }
    return r; }
inline Vec4 mul(const Mat4& a, Vec4 v){
    return {
        a.m[0]*v.x+a.m[1]*v.y+a.m[2]*v.z+a.m[3]*v.w,
        a.m[4]*v.x+a.m[5]*v.y+a.m[6]*v.z+a.m[7]*v.w,
        a.m[8]*v.x+a.m[9]*v.y+a.m[10]*v.z+a.m[11]*v.w,
        a.m[12]*v.x+a.m[13]*v.y+a.m[14]*v.z+a.m[15]*v.w }; }
inline Mat4 translate(Vec3 t){ Mat4 r=identity(); r.m[3]=t.x; r.m[7]=t.y; r.m[11]=t.z; return r; }
inline Mat4 perspective(float fovy, float aspect, float n, float f){
    float t = 1.0f/std::tan(fovy*0.5f);
    Mat4 r;
    r.m[0]=t/aspect; r.m[5]=t;
    r.m[10]=(f+n)/(n-f); r.m[11]=(2*f*n)/(n-f);
    r.m[14]=-1; // w_clip = -z_view
    return r; }
inline Mat4 lookAt(Vec3 eye, Vec3 center, Vec3 up){
    Vec3 f = normalize(center - eye);
    Vec3 s = normalize(cross(f, up));
    Vec3 u = cross(s, f);
    Mat4 r=identity();
    r.m[0]=s.x; r.m[1]=s.y; r.m[2]=s.z; r.m[3]=-dot(s,eye);
    r.m[4]=u.x; r.m[5]=u.y; r.m[6]=u.z; r.m[7]=-dot(u,eye);
    r.m[8]=-f.x; r.m[9]=-f.y; r.m[10]=-f.z; r.m[11]=dot(f,eye);
    return r; }
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_mat --output-on-failure`
Expected: `test_mat` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/mat.hpp native/tests/test_mat.cpp
git commit -m "feat(native): mat4 + perspective/lookAt/translate"
```

---

### Task 4: math — primitives & ray-triangle intersection

**Files:**
- Create: `native/raw/primitives.hpp`
- Create: `native/tests/test_primitives.cpp`

**Interfaces:**
- Consumes: `raw::Vec3` (vec.hpp).
- Produces: `raw::Ray{Vec3 o,d}`; `raw::Tri{Vec3 a,b,c}`; `bool intersectTri(const Ray&, const Tri&, float& t, float& u, float& v)` (Möller–Trumbore, t>1e-4); `raw::AABB{Vec3 mn,mx}` with `grow(Vec3)`.

- [ ] **Step 1: Write the failing test**

`native/tests/test_primitives.cpp`:
```cpp
#include "raw/primitives.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Tri tri{{-1,-1,0},{1,-1,0},{0,1,0}};
    float t,u,v;
    // ray from +z toward -z hits the triangle at t=5
    CHECK(intersectTri(Ray{{0,0,5},{0,0,-1}}, tri, t,u,v));
    CHECK_NEAR(t, 5.0, 1e-4);
    // ray that misses
    CHECK(!intersectTri(Ray{{5,5,5},{0,0,-1}}, tri, t,u,v));
    AABB box{{1e30f,1e30f,1e30f},{-1e30f,-1e30f,-1e30f}};
    box.grow({1,2,3}); box.grow({-1,0,5});
    CHECK_NEAR(box.mn.x,-1,1e-6); CHECK_NEAR(box.mx.z,5,1e-6);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_primitives`
Expected: FAIL — `raw/primitives.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/primitives.hpp`:
```cpp
#pragma once
#include "raw/vec.hpp"
#include <algorithm>
namespace raw {
struct Ray { Vec3 o, d; };
struct Tri { Vec3 a, b, c; };
struct AABB {
    Vec3 mn, mx;
    void grow(Vec3 p){
        mn = {std::min(mn.x,p.x),std::min(mn.y,p.y),std::min(mn.z,p.z)};
        mx = {std::max(mx.x,p.x),std::max(mx.y,p.y),std::max(mx.z,p.z)}; }
};
// Möller–Trumbore. Returns true and fills t (distance), u,v (barycentrics).
inline bool intersectTri(const Ray& r, const Tri& tri, float& t, float& u, float& v){
    const float EPS = 1e-7f;
    Vec3 e1 = tri.b - tri.a, e2 = tri.c - tri.a;
    Vec3 p = cross(r.d, e2);
    float det = dot(e1, p);
    if (det > -EPS && det < EPS) return false;
    float inv = 1.0f/det;
    Vec3 tv = r.o - tri.a;
    u = dot(tv, p) * inv;
    if (u < 0 || u > 1) return false;
    Vec3 q = cross(tv, e1);
    v = dot(r.d, q) * inv;
    if (v < 0 || u + v > 1) return false;
    t = dot(e2, q) * inv;
    return t > 1e-4f;
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_primitives --output-on-failure`
Expected: `test_primitives` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/primitives.hpp native/tests/test_primitives.cpp
git commit -m "feat(native): ray/tri/aabb + Moller-Trumbore intersection"
```

---

### Task 5: image — typed buffers & PPM output

**Files:**
- Create: `native/raw/image.hpp`
- Create: `native/src/image.cpp`
- Create: `native/tests/test_image.cpp`

**Interfaces:**
- Consumes: `raw::Vec3` (vec.hpp).
- Produces: `template<class T> raw::Buffer<T>{ int w,h; std::vector<T> px; T& at(int x,int y); const T& at(...) const; }`; `void writePPM(const Buffer<Vec3>&, const std::string& path)` (P6, values clamped 0..1 → 0..255); `void writePGM(const Buffer<float>&, const std::string& path)` (P5, clamped 0..1).

- [ ] **Step 1: Write the failing test**

`native/tests/test_image.cpp`:
```cpp
#include "raw/image.hpp"
#include "check.hpp"
#include <cstdio>
#include <fstream>
using namespace raw;
int main() {
    Buffer<Vec3> img; img.resize(2,1);
    img.at(0,0) = {1,0,0}; img.at(1,0) = {0,1,0};
    writePPM(img, "test_out.ppm");
    std::ifstream f("test_out.ppm", std::ios::binary);
    std::string magic; int w,h,mx; f >> magic >> w >> h >> mx;
    CHECK(magic == "P6"); CHECK(w == 2); CHECK(h == 1); CHECK(mx == 255);
    f.get(); // single whitespace after maxval
    unsigned char px[6]; f.read((char*)px, 6);
    CHECK(px[0]==255 && px[1]==0 && px[2]==0); // red
    CHECK(px[3]==0 && px[4]==255 && px[5]==0); // green
    std::remove("test_out.ppm");
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_image`
Expected: FAIL — `raw/image.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/image.hpp`:
```cpp
#pragma once
#include "raw/vec.hpp"
#include <vector>
#include <string>
namespace raw {
template<class T> struct Buffer {
    int w{0}, h{0};
    std::vector<T> px;
    void resize(int W, int H){ w=W; h=H; px.assign((size_t)W*H, T{}); }
    T& at(int x,int y){ return px[(size_t)y*w + x]; }
    const T& at(int x,int y) const { return px[(size_t)y*w + x]; }
};
void writePPM(const Buffer<Vec3>& img, const std::string& path);
void writePGM(const Buffer<float>& img, const std::string& path);
}
```

`native/src/image.cpp`:
```cpp
#include "raw/image.hpp"
#include <fstream>
#include <algorithm>
namespace raw {
static unsigned char to8(float v){
    v = std::clamp(v, 0.0f, 1.0f); return (unsigned char)(v*255.0f + 0.5f); }
void writePPM(const Buffer<Vec3>& img, const std::string& path){
    std::ofstream f(path, std::ios::binary);
    f << "P6\n" << img.w << " " << img.h << "\n255\n";
    for (int y=0;y<img.h;++y) for (int x=0;x<img.w;++x){
        const Vec3& c = img.at(x,y);
        unsigned char rgb[3] = {to8(c.x), to8(c.y), to8(c.z)};
        f.write((char*)rgb, 3); }
}
void writePGM(const Buffer<float>& img, const std::string& path){
    std::ofstream f(path, std::ios::binary);
    f << "P5\n" << img.w << " " << img.h << "\n255\n";
    for (int y=0;y<img.h;++y) for (int x=0;x<img.w;++x){
        unsigned char v = to8(img.at(x,y)); f.write((char*)&v, 1); }
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_image --output-on-failure`
Expected: `test_image` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/image.hpp native/src/image.cpp native/tests/test_image.cpp
git commit -m "feat(native): typed Buffer<T> + PPM/PGM writers"
```

---

### Task 6: scene — meshes, camera, lights, built-in test scene

**Files:**
- Create: `native/raw/scene.hpp`
- Create: `native/src/scene.cpp`
- Create: `native/tests/test_scene.cpp`

**Interfaces:**
- Consumes: `raw::Vec3` (vec.hpp), `raw::Mat4`/`lookAt`/`perspective` (mat.hpp).
- Produces: `raw::Material{Vec3 albedo}`; `raw::Mesh{ std::vector<Vec3> positions, normals; std::vector<int> indices; Material material; }`; `raw::Light{Vec3 dir; float intensity}`; `raw::Camera{Vec3 eye,center,up; float fovy,aspect,nearZ,farZ; Mat4 view() const; Mat4 proj() const;}`; `raw::Scene{ std::vector<Mesh> meshes; Camera camera; std::vector<Light> lights; }`; `Scene buildTestScene(int w, int h)` (a ground plane + a box above it, a directional light, a camera looking at the box).

- [ ] **Step 1: Write the failing test**

`native/tests/test_scene.cpp`:
```cpp
#include "raw/scene.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(64, 64);
    CHECK(s.meshes.size() >= 2);          // plane + box
    CHECK(!s.lights.empty());
    // every mesh has indices that are a multiple of 3 and normals per vertex
    for (const auto& m : s.meshes){
        CHECK(m.indices.size() % 3 == 0);
        CHECK(m.normals.size() == m.positions.size());
    }
    CHECK_NEAR(s.camera.aspect, 1.0, 1e-6);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_scene`
Expected: FAIL — `raw/scene.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/scene.hpp`:
```cpp
#pragma once
#include "raw/vec.hpp"
#include "raw/mat.hpp"
#include <vector>
namespace raw {
struct Material { Vec3 albedo{0.8f,0.8f,0.8f}; };
struct Mesh {
    std::vector<Vec3> positions, normals;
    std::vector<int> indices;
    Material material;
};
struct Light { Vec3 dir{0,-1,0}; float intensity{1.0f}; };
struct Camera {
    Vec3 eye{0,0,5}, center{0,0,0}, up{0,1,0};
    float fovy{1.0f}, aspect{1.0f}, nearZ{0.1f}, farZ{100.0f};
    Mat4 view() const { return lookAt(eye, center, up); }
    Mat4 proj() const { return perspective(fovy, aspect, nearZ, farZ); }
};
struct Scene { std::vector<Mesh> meshes; Camera camera; std::vector<Light> lights; };
Scene buildTestScene(int w, int h);
}
```

`native/src/scene.cpp`:
```cpp
#include "raw/scene.hpp"
namespace raw {
// Append an axis-aligned quad (two triangles) with a flat normal.
static void addQuad(Mesh& m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n){
    int base = (int)m.positions.size();
    for (Vec3 p : {a,b,c,d}){ m.positions.push_back(p); m.normals.push_back(n); }
    for (int i : {0,1,2, 0,2,3}) m.indices.push_back(base + i);
}
// Append an axis-aligned box centered at c with half-size h.
static void addBox(Mesh& m, Vec3 c, float h){
    Vec3 p000{c.x-h,c.y-h,c.z-h}, p111{c.x+h,c.y+h,c.z+h};
    addQuad(m, {p000.x,p000.y,p111.z},{p111.x,p000.y,p111.z},{p111.x,p111.y,p111.z},{p000.x,p111.y,p111.z}, {0,0,1});  // +z
    addQuad(m, {p111.x,p000.y,p000.z},{p000.x,p000.y,p000.z},{p000.x,p111.y,p000.z},{p111.x,p111.y,p000.z}, {0,0,-1}); // -z
    addQuad(m, {p000.x,p111.y,p111.z},{p111.x,p111.y,p111.z},{p111.x,p111.y,p000.z},{p000.x,p111.y,p000.z}, {0,1,0});  // +y
    addQuad(m, {p000.x,p000.y,p000.z},{p111.x,p000.y,p000.z},{p111.x,p000.y,p111.z},{p000.x,p000.y,p111.z}, {0,-1,0}); // -y
    addQuad(m, {p111.x,p000.y,p111.z},{p111.x,p000.y,p000.z},{p111.x,p111.y,p000.z},{p111.x,p111.y,p111.z}, {1,0,0});  // +x
    addQuad(m, {p000.x,p000.y,p000.z},{p000.x,p000.y,p111.z},{p000.x,p111.y,p111.z},{p000.x,p111.y,p000.z}, {-1,0,0}); // -x
}
Scene buildTestScene(int w, int h){
    Scene s;
    Mesh plane; plane.material.albedo = {0.7f,0.7f,0.7f};
    addQuad(plane, {-5,0,-5},{5,0,-5},{5,0,5},{-5,0,5}, {0,1,0}); // ground at y=0
    s.meshes.push_back(plane);
    Mesh box; box.material.albedo = {0.8f,0.3f,0.2f};
    addBox(box, {0,1,0}, 1.0f); // sits on the plane
    s.meshes.push_back(box);
    s.lights.push_back(Light{ normalize(Vec3{-0.4f,-1.0f,-0.3f}), 1.0f });
    s.camera.eye = {4,4,6}; s.camera.center = {0,1,0}; s.camera.up = {0,1,0};
    s.camera.fovy = 0.9f; s.camera.aspect = (float)w/(float)h;
    return s;
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_scene --output-on-failure`
Expected: `test_scene` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/scene.hpp native/src/scene.cpp native/tests/test_scene.cpp
git commit -m "feat(native): scene model + built-in plane+box test scene"
```

---

### Task 7: raster — G-buffer fill

**Files:**
- Create: `native/raw/gbuffer.hpp`
- Create: `native/raw/raster.hpp`
- Create: `native/src/raster.cpp`
- Create: `native/tests/test_raster.cpp`

**Interfaces:**
- Consumes: `Vec3`/`Vec4` (vec.hpp), `Mat4`/`mul` (mat.hpp), `Buffer<T>` (image.hpp), `Scene`/`Mesh` (scene.hpp).
- Produces: `raw::GBuffer{ int w,h; Buffer<float> depth; Buffer<Vec3> normal, position, albedo; Buffer<uint8_t> mask; }` (depth = view-space distance, larger = farther; mask=1 where covered); `GBuffer rasterize(const Scene&, int w, int h)`.

- [ ] **Step 1: Write the failing test**

`native/tests/test_raster.cpp`:
```cpp
#include "raw/raster.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(64,64);
    GBuffer g = rasterize(s, 64, 64);
    CHECK(g.w==64 && g.h==64);
    // at least some pixels covered, and not the whole frame (scene doesn't fill it)
    int covered=0; for (auto m : g.mask.px) covered += m;
    CHECK(covered > 100);
    CHECK(covered < 64*64);
    // covered normals are unit length
    for (int y=0;y<64;++y) for (int x=0;x<64;++x) if (g.mask.at(x,y)){
        float len = length(g.normal.at(x,y));
        CHECK_NEAR(len, 1.0, 1e-3); break; }
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_raster`
Expected: FAIL — `raw/raster.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/gbuffer.hpp`:
```cpp
#pragma once
#include "raw/vec.hpp"
#include "raw/image.hpp"
#include <cstdint>
namespace raw {
struct GBuffer {
    int w{0}, h{0};
    Buffer<float> depth;
    Buffer<Vec3> normal, position, albedo;
    Buffer<uint8_t> mask;
    void resize(int W,int H){ w=W;h=H;
        depth.resize(W,H); normal.resize(W,H); position.resize(W,H);
        albedo.resize(W,H); mask.resize(W,H); }
};
}
```

`native/raw/raster.hpp`:
```cpp
#pragma once
#include "raw/gbuffer.hpp"
#include "raw/scene.hpp"
namespace raw { GBuffer rasterize(const Scene& scene, int w, int h); }
```

`native/src/raster.cpp`:
```cpp
#include "raw/raster.hpp"
#include "raw/mat.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace raw {
GBuffer rasterize(const Scene& scene, int w, int h){
    GBuffer g; g.resize(w,h);
    for (auto& d : g.depth.px) d = std::numeric_limits<float>::infinity();
    Mat4 view = scene.camera.view();
    Mat4 proj = scene.camera.proj();
    Mat4 vp = mul(proj, view);
    for (const Mesh& m : scene.meshes){
        for (size_t i=0; i+2 < m.indices.size(); i+=3){
            int i0=m.indices[i], i1=m.indices[i+1], i2=m.indices[i+2];
            Vec3 wp[3] = { m.positions[i0], m.positions[i1], m.positions[i2] };
            Vec3 wn[3] = { m.normals[i0],   m.normals[i1],   m.normals[i2]   };
            // clip-space then perspective divide
            Vec4 cs[3]; Vec3 ndc[3]; float invw[3];
            bool behind=false;
            for (int k=0;k<3;++k){
                cs[k] = mul(vp, Vec4{wp[k].x,wp[k].y,wp[k].z,1});
                if (cs[k].w <= 1e-6f){ behind=true; break; }
                invw[k] = 1.0f/cs[k].w;
                ndc[k] = { cs[k].x*invw[k], cs[k].y*invw[k], cs[k].z*invw[k] };
            }
            if (behind) continue;
            // to screen
            Vec3 sp[3];
            for (int k=0;k<3;++k){
                sp[k].x = (ndc[k].x*0.5f+0.5f)*w;
                sp[k].y = (1.0f-(ndc[k].y*0.5f+0.5f))*h;
                sp[k].z = cs[k].w; // view-space distance proxy (-z_view = w)
            }
            int minx = std::max(0, (int)std::floor(std::min({sp[0].x,sp[1].x,sp[2].x})));
            int maxx = std::min(w-1, (int)std::ceil (std::max({sp[0].x,sp[1].x,sp[2].x})));
            int miny = std::max(0, (int)std::floor(std::min({sp[0].y,sp[1].y,sp[2].y})));
            int maxy = std::min(h-1, (int)std::ceil (std::max({sp[0].y,sp[1].y,sp[2].y})));
            float area = (sp[1].x-sp[0].x)*(sp[2].y-sp[0].y) - (sp[1].y-sp[0].y)*(sp[2].x-sp[0].x);
            if (std::fabs(area) < 1e-9f) continue;
            for (int y=miny;y<=maxy;++y) for (int x=minx;x<=maxx;++x){
                float px=x+0.5f, py=y+0.5f;
                float w0 = ((sp[1].x-px)*(sp[2].y-py)-(sp[1].y-py)*(sp[2].x-px))/area;
                float w1 = ((sp[2].x-px)*(sp[0].y-py)-(sp[2].y-py)*(sp[0].x-px))/area;
                float w2 = 1.0f - w0 - w1;
                if (w0<0||w1<0||w2<0) continue;
                // perspective-correct interpolation
                float iw = w0*invw[0] + w1*invw[1] + w2*invw[2];
                float depth = (w0*sp[0].z + w1*sp[1].z + w2*sp[2].z); // ~view distance
                if (depth >= g.depth.at(x,y)) continue;
                auto pc = [&](Vec3 a,Vec3 b,Vec3 c){
                    Vec3 r{ (w0*a.x*invw[0]+w1*b.x*invw[1]+w2*c.x*invw[2])/iw,
                            (w0*a.y*invw[0]+w1*b.y*invw[1]+w2*c.y*invw[2])/iw,
                            (w0*a.z*invw[0]+w1*b.z*invw[1]+w2*c.z*invw[2])/iw }; return r; };
                g.depth.at(x,y) = depth;
                g.position.at(x,y) = pc(wp[0],wp[1],wp[2]);
                g.normal.at(x,y) = normalize(pc(wn[0],wn[1],wn[2]));
                g.albedo.at(x,y) = m.material.albedo;
                g.mask.at(x,y) = 1;
            }
        }
    }
    return g;
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_raster --output-on-failure`
Expected: `test_raster` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/gbuffer.hpp native/raw/raster.hpp native/src/raster.cpp native/tests/test_raster.cpp
git commit -m "feat(native): triangle rasterizer -> G-buffer"
```

---

### Task 8: accel — linear accelerator

**Files:**
- Create: `native/raw/accel.hpp`
- Create: `native/src/accel.cpp`
- Create: `native/tests/test_accel.cpp`

**Interfaces:**
- Consumes: `Ray`/`Tri`/`intersectTri` (primitives.hpp), `Scene`/`Mesh` (scene.hpp).
- Produces: `raw::LinearAccel{ void build(const Scene&); bool occluded(const Ray&, float maxDist) const; std::vector<Tri> tris; }`.

- [ ] **Step 1: Write the failing test**

`native/tests/test_accel.cpp`:
```cpp
#include "raw/accel.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(8,8);
    LinearAccel accel; accel.build(s);
    CHECK(accel.tris.size() > 10);
    // a ray straight down from high above the box origin is occluded by the box top
    CHECK(accel.occluded(Ray{{0,10,0},{0,-1,0}}, 100.0f));
    // a ray pointing up into empty sky from above everything is not occluded
    CHECK(!accel.occluded(Ray{{0,10,0},{0,1,0}}, 100.0f));
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_accel`
Expected: FAIL — `raw/accel.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/accel.hpp`:
```cpp
#pragma once
#include "raw/primitives.hpp"
#include "raw/scene.hpp"
#include <vector>
namespace raw {
struct LinearAccel {
    std::vector<Tri> tris;
    void build(const Scene& s);
    bool occluded(const Ray& r, float maxDist) const;
};
}
```

`native/src/accel.cpp`:
```cpp
#include "raw/accel.hpp"
namespace raw {
void LinearAccel::build(const Scene& s){
    tris.clear();
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

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_accel --output-on-failure`
Expected: `test_accel` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/accel.hpp native/src/accel.cpp native/tests/test_accel.cpp
git commit -m "feat(native): linear ray-occlusion accelerator"
```

---

### Task 9: ray — ray-traced ambient occlusion (ground truth)

**Files:**
- Create: `native/raw/ray_ao.hpp`
- Create: `native/src/ray_ao.cpp`
- Create: `native/tests/test_ray_ao.cpp`

**Interfaces:**
- Consumes: `GBuffer` (gbuffer.hpp), `LinearAccel` (accel.hpp), `Vec3`/`cross`/`normalize` (vec.hpp), `Ray` (primitives.hpp).
- Produces: `Buffer<float> computeRTAO(const GBuffer&, const LinearAccel&, int samples, float radius)` — for each covered pixel, fraction of cosine-weighted hemisphere rays (around the surface normal) that are NOT occluded within `radius`; uncovered pixels = 1. Deterministic via `hash(x,y,sampleIndex)`.

- [ ] **Step 1: Write the failing test**

`native/tests/test_ray_ao.cpp`:
```cpp
#include "raw/ray_ao.hpp"
#include "raw/raster.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(48,48);
    GBuffer g = rasterize(s,48,48);
    LinearAccel accel; accel.build(s);
    Buffer<float> ao = computeRTAO(g, accel, 32, 2.0f);
    // average AO over covered pixels is in (0,1] and not fully open
    double sum=0; int n=0; float mn=1;
    for (int y=0;y<48;++y) for (int x=0;x<48;++x) if (g.mask.at(x,y)){
        float a=ao.at(x,y); sum+=a; ++n; if(a<mn)mn=a; }
    CHECK(n>0);
    double avg = sum/n;
    CHECK(avg > 0.0 && avg <= 1.0);
    CHECK(mn < 0.95);   // the crease where box meets plane is occluded
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_ray_ao`
Expected: FAIL — `raw/ray_ao.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/ray_ao.hpp`:
```cpp
#pragma once
#include "raw/gbuffer.hpp"
#include "raw/accel.hpp"
namespace raw {
Buffer<float> computeRTAO(const GBuffer& g, const LinearAccel& accel,
                          int samples, float radius);
}
```

`native/src/ray_ao.cpp`:
```cpp
#include "raw/ray_ao.hpp"
#include "raw/primitives.hpp"
#include <cmath>
#include <cstdint>
namespace raw {
// Deterministic hash -> float in [0,1). No global RNG state.
static float hash01(uint32_t x, uint32_t y, uint32_t s){
    uint32_t h = x*374761393u + y*668265263u + s*2246822519u;
    h = (h ^ (h>>13)) * 1274126177u; h ^= h>>16;
    return (h & 0xFFFFFFu) / float(0x1000000);
}
// Build an orthonormal basis around n.
static void basis(Vec3 n, Vec3& t, Vec3& b){
    Vec3 a = std::fabs(n.x) > 0.9f ? Vec3{0,1,0} : Vec3{1,0,0};
    t = normalize(cross(a, n)); b = cross(n, t);
}
Buffer<float> computeRTAO(const GBuffer& g, const LinearAccel& accel,
                          int samples, float radius){
    Buffer<float> ao; ao.resize(g.w, g.h);
    for (int y=0;y<g.h;++y) for (int x=0;x<g.w;++x){
        if (!g.mask.at(x,y)){ ao.at(x,y)=1.0f; continue; }
        Vec3 p = g.position.at(x,y);
        Vec3 n = g.normal.at(x,y);
        Vec3 t,b; basis(n,t,b);
        Vec3 origin = p + n*1e-3f; // offset to avoid self-hit
        int open=0;
        for (int sidx=0; sidx<samples; ++sidx){
            float u1 = hash01(x,y,(uint32_t)(2*sidx));
            float u2 = hash01(x,y,(uint32_t)(2*sidx+1));
            // cosine-weighted hemisphere
            float r = std::sqrt(u1); float phi = 6.2831853f*u2;
            float lx = r*std::cos(phi), ly = r*std::sin(phi);
            float lz = std::sqrt(std::max(0.0f, 1.0f - u1));
            Vec3 dir = normalize(t*lx + b*ly + n*lz);
            if (!accel.occluded(Ray{origin, dir}, radius)) ++open;
        }
        ao.at(x,y) = (float)open / (float)samples;
    }
    return ao;
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_ray_ao --output-on-failure`
Expected: `test_ray_ao` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/ray_ao.hpp native/src/ray_ao.cpp native/tests/test_ray_ao.cpp
git commit -m "feat(native): ray-traced ambient occlusion (ground truth)"
```

---

### Task 10: effects — screen-space AO (approximation)

**Files:**
- Create: `native/raw/ssao.hpp`
- Create: `native/src/ssao.cpp`
- Create: `native/tests/test_ssao.cpp`

**Interfaces:**
- Consumes: `GBuffer` (gbuffer.hpp), `Vec3`/`dot`/`normalize`/`length` (vec.hpp).
- Produces: `Buffer<float> computeSSAO(const GBuffer&, int samples, float radius)` — a depth/position-based screen-space AO: for each covered pixel, sample nearby pixels; a neighbor occludes if it is in front along the normal hemisphere within `radius`. Uncovered pixels = 1. Deterministic.

- [ ] **Step 1: Write the failing test**

`native/tests/test_ssao.cpp`:
```cpp
#include "raw/ssao.hpp"
#include "raw/raster.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(48,48);
    GBuffer g = rasterize(s,48,48);
    Buffer<float> ao = computeSSAO(g, 16, 2.0f);
    double sum=0; int n=0; float mn=1;
    for (int y=0;y<48;++y) for (int x=0;x<48;++x) if (g.mask.at(x,y)){
        float a=ao.at(x,y); CHECK(a>=0.0f && a<=1.0001f); sum+=a; ++n; if(a<mn)mn=a; }
    CHECK(n>0);
    CHECK(sum/n > 0.0 && sum/n <= 1.0);
    CHECK(mn < 0.99);  // some darkening somewhere
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_ssao`
Expected: FAIL — `raw/ssao.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/ssao.hpp`:
```cpp
#pragma once
#include "raw/gbuffer.hpp"
namespace raw {
Buffer<float> computeSSAO(const GBuffer& g, int samples, float radius);
}
```

`native/src/ssao.cpp`:
```cpp
#include "raw/ssao.hpp"
#include <cmath>
#include <cstdint>
namespace raw {
static float hash01(uint32_t x, uint32_t y, uint32_t s){
    uint32_t h = x*374761393u + y*668265263u + s*2246822519u;
    h = (h ^ (h>>13)) * 1274126177u; h ^= h>>16;
    return (h & 0xFFFFFFu) / float(0x1000000);
}
Buffer<float> computeSSAO(const GBuffer& g, int samples, float radius){
    Buffer<float> ao; ao.resize(g.w,g.h);
    const int R = 6; // screen-space sampling radius in pixels
    for (int y=0;y<g.h;++y) for (int x=0;x<g.w;++x){
        if (!g.mask.at(x,y)){ ao.at(x,y)=1.0f; continue; }
        Vec3 p = g.position.at(x,y);
        Vec3 n = g.normal.at(x,y);
        int occ=0, used=0;
        for (int sidx=0; sidx<samples; ++sidx){
            float a = hash01(x,y,(uint32_t)sidx)*6.2831853f;
            float rad = (0.3f + 0.7f*hash01(x,y,(uint32_t)(sidx+97)))*R;
            int sx = x + (int)std::lround(std::cos(a)*rad);
            int sy = y + (int)std::lround(std::sin(a)*rad);
            if (sx<0||sy<0||sx>=g.w||sy>=g.h) continue;
            if (!g.mask.at(sx,sy)) continue;
            ++used;
            Vec3 d = g.position.at(sx,sy) - p;
            float dist = length(d);
            if (dist < 1e-4f || dist > radius) continue;
            float ndl = dot(n, normalize(d));
            if (ndl > 0.15f) ++occ; // neighbor rises into the hemisphere => occluding
        }
        float a = used>0 ? 1.0f - (float)occ/(float)used : 1.0f;
        ao.at(x,y) = a < 0 ? 0 : a;
    }
    return ao;
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_ssao --output-on-failure`
Expected: `test_ssao` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/ssao.hpp native/src/ssao.cpp native/tests/test_ssao.cpp
git commit -m "feat(native): screen-space AO approximation"
```

---

### Task 11: reconcile — approximation vs ground truth

**Files:**
- Create: `native/raw/reconcile.hpp`
- Create: `native/src/reconcile.cpp`
- Create: `native/tests/test_reconcile.cpp`

**Interfaces:**
- Consumes: `Buffer<float>`/`Buffer<uint8_t>` (image.hpp).
- Produces: `raw::ReconcileResult{ Buffer<float> errorMap; float rmse; float maxError; int pixels; bool withinTolerance; }`; `ReconcileResult reconcile(const Buffer<float>& approx, const Buffer<float>& truth, const Buffer<uint8_t>& mask, float tolerance)` — per covered pixel `|approx-truth|` into errorMap; rmse/maxError over covered pixels; `withinTolerance = rmse <= tolerance`.

- [ ] **Step 1: Write the failing test**

`native/tests/test_reconcile.cpp`:
```cpp
#include "raw/reconcile.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Buffer<float> a,b; Buffer<uint8_t> mask;
    a.resize(2,1); b.resize(2,1); mask.resize(2,1);
    mask.at(0,0)=1; mask.at(1,0)=1;
    a.at(0,0)=0.5f; b.at(0,0)=0.5f;     // identical
    a.at(1,0)=0.8f; b.at(1,0)=0.4f;     // diff 0.4
    ReconcileResult r = reconcile(a,b,mask,0.1f);
    CHECK_NEAR(r.errorMap.at(0,0), 0.0, 1e-6);
    CHECK_NEAR(r.errorMap.at(1,0), 0.4, 1e-6);
    CHECK_NEAR(r.maxError, 0.4, 1e-6);
    CHECK(r.pixels == 2);
    CHECK(!r.withinTolerance);                 // rmse ~0.283 > 0.1
    // identical buffers -> zero error, within tolerance
    ReconcileResult r2 = reconcile(a,a,mask,0.1f);
    CHECK_NEAR(r2.rmse, 0.0, 1e-6);
    CHECK(r2.withinTolerance);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_reconcile`
Expected: FAIL — `raw/reconcile.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/reconcile.hpp`:
```cpp
#pragma once
#include "raw/image.hpp"
#include <cstdint>
namespace raw {
struct ReconcileResult {
    Buffer<float> errorMap;
    float rmse{0}, maxError{0};
    int pixels{0};
    bool withinTolerance{false};
};
ReconcileResult reconcile(const Buffer<float>& approx, const Buffer<float>& truth,
                          const Buffer<uint8_t>& mask, float tolerance);
}
```

`native/src/reconcile.cpp`:
```cpp
#include "raw/reconcile.hpp"
#include <cmath>
namespace raw {
ReconcileResult reconcile(const Buffer<float>& approx, const Buffer<float>& truth,
                          const Buffer<uint8_t>& mask, float tolerance){
    ReconcileResult r; r.errorMap.resize(approx.w, approx.h);
    double sumSq=0; int n=0;
    for (int y=0;y<approx.h;++y) for (int x=0;x<approx.w;++x){
        if (!mask.at(x,y)){ r.errorMap.at(x,y)=0; continue; }
        float e = std::fabs(approx.at(x,y) - truth.at(x,y));
        r.errorMap.at(x,y) = e;
        if (e > r.maxError) r.maxError = e;
        sumSq += (double)e*e; ++n;
    }
    r.pixels = n;
    r.rmse = n>0 ? (float)std::sqrt(sumSq/n) : 0.0f;
    r.withinTolerance = r.rmse <= tolerance;
    return r;
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_reconcile --output-on-failure`
Expected: `test_reconcile` PASS.

- [ ] **Step 5: Commit**

```bash
git add native/raw/reconcile.hpp native/src/reconcile.cpp native/tests/test_reconcile.cpp
git commit -m "feat(native): reconcile approximation vs ground-truth AO"
```

---

### Task 12: composite + engine CLI — the first reconciled frame

**Files:**
- Create: `native/raw/composite.hpp`
- Create: `native/src/composite.cpp`
- Create: `native/app/main.cpp`
- Create: `native/tests/test_composite.cpp`

**Interfaces:**
- Consumes: `GBuffer` (gbuffer.hpp), `Buffer<float>` (image.hpp), `Scene`/`Light` (scene.hpp), `rasterize`, `LinearAccel`, `computeRTAO`, `computeSSAO`, `reconcile`, `writePPM`/`writePGM`.
- Produces: `Buffer<Vec3> shade(const GBuffer&, const Buffer<float>& ao, const Scene&)` (lambert × ao × albedo); a CLI `raw_native_cli [outDir]` that renders the built-in scene, writes `frame.ppm`, `ao_rt.pgm`, `ao_ss.pgm`, `ao_error.pgm`, prints the reconcile verdict, and exits 0.

- [ ] **Step 1: Write the failing test**

`native/tests/test_composite.cpp`:
```cpp
#include "raw/composite.hpp"
#include "raw/raster.hpp"
#include "raw/ray_ao.hpp"
#include "check.hpp"
using namespace raw;
int main() {
    Scene s = buildTestScene(32,32);
    GBuffer g = rasterize(s,32,32);
    LinearAccel accel; accel.build(s);
    Buffer<float> ao = computeRTAO(g, accel, 8, 2.0f);
    Buffer<Vec3> img = shade(g, ao, s);
    CHECK(img.w==32 && img.h==32);
    // covered pixels are darker-or-equal with AO than the raw albedo would be,
    // and background stays black
    bool sawCovered=false, bgBlack=true;
    for (int y=0;y<32;++y) for (int x=0;x<32;++x){
        Vec3 c = img.at(x,y);
        if (g.mask.at(x,y)){ sawCovered=true; CHECK(c.x>=0 && c.x<=1); }
        else if (c.x>0||c.y>0||c.z>0) bgBlack=false;
    }
    CHECK(sawCovered); CHECK(bgBlack);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_composite`
Expected: FAIL — `raw/composite.hpp` not found.

- [ ] **Step 3: Write minimal implementation**

`native/raw/composite.hpp`:
```cpp
#pragma once
#include "raw/gbuffer.hpp"
#include "raw/scene.hpp"
namespace raw {
Buffer<Vec3> shade(const GBuffer& g, const Buffer<float>& ao, const Scene& s);
}
```

`native/src/composite.cpp`:
```cpp
#include "raw/composite.hpp"
#include <algorithm>
namespace raw {
Buffer<Vec3> shade(const GBuffer& g, const Buffer<float>& ao, const Scene& s){
    Buffer<Vec3> img; img.resize(g.w,g.h);
    Vec3 ldir = s.lights.empty() ? Vec3{0,-1,0} : s.lights[0].dir;
    float li = s.lights.empty() ? 1.0f : s.lights[0].intensity;
    for (int y=0;y<g.h;++y) for (int x=0;x<g.w;++x){
        if (!g.mask.at(x,y)){ img.at(x,y)={0,0,0}; continue; }
        Vec3 n = g.normal.at(x,y);
        float ndl = std::max(0.0f, dot(n, ldir*-1.0f)) * li;
        float ambient = 0.2f;
        float a = ao.at(x,y);
        float lit = (ambient + ndl) * a;
        Vec3 alb = g.albedo.at(x,y);
        img.at(x,y) = { std::clamp(alb.x*lit,0.0f,1.0f),
                        std::clamp(alb.y*lit,0.0f,1.0f),
                        std::clamp(alb.z*lit,0.0f,1.0f) };
    }
    return img;
}
}
```

`native/app/main.cpp`:
```cpp
#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/composite.hpp"
#include <string>
#include <cstdio>
using namespace raw;
int main(int argc, char** argv){
    std::string out = argc>1 ? argv[1] : ".";
    const int W=256, H=256;
    Scene s = buildTestScene(W,H);
    GBuffer g = rasterize(s, W, H);
    LinearAccel accel; accel.build(s);
    Buffer<float> aoRT = computeRTAO(g, accel, 64, 2.0f);
    Buffer<float> aoSS = computeSSAO(g, 24, 2.0f);
    ReconcileResult rec = reconcile(aoSS, aoRT, g.mask, 0.12f);
    Buffer<Vec3> frame = shade(g, aoRT, s);
    writePPM(frame, out + "/frame.ppm");
    writePGM(aoRT,  out + "/ao_rt.pgm");
    writePGM(aoSS,  out + "/ao_ss.pgm");
    writePGM(rec.errorMap, out + "/ao_error.pgm");
    std::printf("reconcile: pixels=%d rmse=%.4f maxError=%.4f verdict=%s\n",
        rec.pixels, rec.rmse, rec.maxError,
        rec.withinTolerance ? "WITHIN-TOLERANCE" : "DIVERGENT");
    return 0;
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build && ctest --test-dir native/build -R test_composite --output-on-failure`
Expected: `test_composite` PASS. Then run the CLI end-to-end:
Run: `native/build/raw_native_cli native/build` (or `native/build/Release/raw_native_cli.exe native/build` on MSVC)
Expected: prints a `reconcile: ... verdict=...` line and writes `frame.ppm`, `ao_rt.pgm`, `ao_ss.pgm`, `ao_error.pgm`.

- [ ] **Step 5: Commit**

```bash
git add native/raw/composite.hpp native/src/composite.cpp native/app/main.cpp native/tests/test_composite.cpp
git commit -m "feat(native): composite + CLI — the first reconciled frame"
```

---

## Final verification (after Task 12)

- [ ] Run the full suite: `ctest --test-dir native/build --output-on-failure` — all of `test_sanity, test_vec, test_mat, test_primitives, test_image, test_scene, test_raster, test_accel, test_ray_ao, test_ssao, test_reconcile, test_composite` PASS.
- [ ] Run the CLI and open `frame.ppm` (any image viewer) — a lit box on a plane, darkened in the crease; `ao_rt.pgm` vs `ao_ss.pgm` visibly differ; `ao_error.pgm` shows where the screen-space approximation departs from ground truth.
- [ ] Confirm zero third-party includes: `grep -rhoE "#include <[^>]+>" native/ | sort -u` — every line must be a C++ standard header (e.g. `<vector>`, `<cmath>`, `<cstdint>`, `<fstream>`, `<algorithm>`, `<string>`, `<limits>`, `<cstdio>`, `<cstring>`); no DirectX (`<d3d11.h>`), no external libraries.

## Spec coverage check

- §2 boundary (C++23, stdlib-only, PPM output, `native/` isolation) → Tasks 1–12, Global Constraints.
- §3 architecture (one scene → raster + ray → reconcile → composite) → Tasks 6–12.
- §4 units (math/image/scene/raster/accel/ray/effects/reconcile/composite/engine) → Tasks 2–12 (BVH deferred per §6; linear accel in Task 8).
- §5 first increment ("first reconciled frame") → Tasks 1–12 end-to-end.
- §6 deferred (BVH, GI, PNG, PBR, real-time) → explicitly NOT in this plan.
- §7 testing (known-value math, numeric AO checks, reconcile self-check) → each task's test.
- §8 success criteria (one command → frame + two AO + error map + verdict) → Task 12 CLI + Final verification.
