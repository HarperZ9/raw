# RAW Native Engine — Design

> Date: 2026-06-21 · Status: approved design, pre-implementation · Branch: `feat/native-engine`

## 1 · Purpose

Reconstitute RAW from a D3D11/Skyrim screen-space-effects platform toward its stated
telos: a **dependency-free native rendering engine** where a rasterized/screen-space
*approximation* and a ray-traced *ground truth* are produced from one scene and
**reconciled** — "is this right?" answered by checking the approximation against truth,
carried as a witnessed, re-checkable observation. The engine verifies itself.

This spec covers the **native engine core** (telos piece 1) and its **first increment**.
It does not touch the existing D3D11 proxy / SKSE code.

## 2 · Boundary (non-negotiable)

- **Language:** C++23, standard library only. No DirectX, no vcpkg, no external libraries.
- **Output:** image files written by hand — PPM first, a hand-rolled PNG encoder later.
  No window, no GPU driver. Real-time display is telos piece 4, out of scope here.
- **Location:** a new `raw/native/` module with its own CMake target `raw_native`,
  separate from `src/core` (D3D11 renderers) and `src/d3d11_proxy`. The Skyrim coupling
  is not modified.
- **Own everything:** math, image I/O, scene format/loader. Zero third-party code.

## 3 · Architecture

One scene representation feeds two engines; their outputs are reconciled.

```
Scene (meshes · camera · lights · materials)  ── one shared representation ──┐
   │                                                                          │
   ├─► RASTERIZER ─► G-buffer (albedo · linear-depth · world-normal · pos)    │
   │                    └─► screen-space effects (SSAO/GTAO port) = APPROXIMATION
   │                                                                          │
   └─► RAY ENGINE (Accelerator over the same scene)                           │
            └─► ground-truth passes (RT AO · RT shadows · path-traced GI) = TRUTH
                                                                              │
   RECONCILE: approximation ⟷ ground-truth ─► error map + witnessed verdict ◄─┘
   COMPOSITE ─► framebuffer ─► image file (PPM→PNG) + re-checkable receipt
```

The hybrid is not two bolted-together engines: raster and ray **share one scene**, and
the reconcile measures the fast approximation against the slow truth. That comparison is
RAW's telos made literal — the verification target and the creative-improvement target on
one loop.

## 4 · Units

Layered. Lower layers know nothing of higher ones. Each unit: one purpose, narrow
interface, independently testable.

| Unit | Purpose | Depends on |
|---|---|---|
| `math/` | `vec2/3/4`, `mat4`, `quat`; primitives `Ray`, `AABB`, `Triangle`. Header-only, pure. | — |
| `image/` | `Buffer<T>` typed pixel grids (RGBA-float, depth, normal); PPM writer (PNG later). | math |
| `scene/` | `Mesh`, `Material`, `Camera`, `Light`, `Scene`; hand-written OBJ loader. | math |
| `raster/` | vertex transform → triangle setup → barycentric fill → z-buffer → **G-buffer**. | math, image, scene |
| `accel/` | `Accelerator` interface (`intersect`, `occluded`); linear impl now, **BVH** later. | math, scene |
| `ray/` | ray generation; ground-truth passes: RT AO first, then shadows, then path-traced GI. | math, accel, scene |
| `effects/` | screen-space effects on the G-buffer; first is GTAO-style **SSAO** (the approximation). | math, image |
| `reconcile/` | approximation vs ground-truth → error map + RMSE/max-error → **witnessed verdict**. | math, image |
| `composite/` | combine G-buffer + lighting + effect into final color. | math, image |
| `engine/` | orchestration + CLI: load → raster → ray → effect → reconcile → composite → write. | all |

G-buffer follows RAW's existing conventions (linear depth, world-space normals) so the
ported screen-space effects behave as they do in the D3D11 lineage.

## 5 · First increment — "the first reconciled frame"

The thinnest vertical slice that drives the whole pipeline and proves the hybrid is real.

1. `math` (vec/mat/Ray/Triangle/AABB needed for the slice) + `image` (Buffer + PPM).
2. `scene` with a small built-in test scene (ground plane + a box).
3. `raster` → G-buffer (linear-depth + world-normal + albedo).
4. `accel` **linear** impl (brute-force triangle intersection) + `ray` → **ray-traced AO** (ground truth).
5. `effects` → minimal **screen-space AO** (the approximation; GTAO's lineage).
6. `reconcile` → screen-space AO vs RT AO → error map + RMSE + verdict (within tolerance?).
7. `engine` CLI → writes shaded frame, both AO buffers, and the error map (PPM); prints the verdict.

**Definition of done (receipts):** the CLI runs on the built-in scene and produces the
images; `math` known-value tests pass; RT AO of the built-in scene matches a checked-in
reference within tolerance (itself a reconcile); the reconcile reports a numeric
approximation-vs-truth error and a verdict.

## 6 · Deferred roadmap (explicitly NOT in increment 1)

BVH acceleration · path-traced GI + Cornell box · hand-rolled PNG encoder · soft shadows ·
PBR materials · SSR/SSGI ports · multithreading · telos piece 4 (real-time two-way
cross-examination surface). Each is a later increment that deepens an existing unit's
interface, not a rewrite.

## 7 · Testing strategy

- `math`: known-value unit tests (matrix/vector identities, ray-triangle intersection).
- `raster`/`ray`/`reconcile`: golden-image + numeric tests — e.g. RT AO of the built-in
  scene matches a checked-in reference buffer within tolerance.
- Test framework: dependency-free (a tiny assert-based harness, or doctest if RAW already
  vendors one — confirm during planning; default to a minimal native harness to keep the
  zero-dep boundary).
- TDD: each unit's behavior is specified by a failing test before implementation.

## 8 · Success criteria

A person or a model can run one command and get: a rendered frame, the same frame's AO
computed two ways (fast approximation, ray-traced truth), a per-pixel error map between
them, and a witnessed verdict on whether the approximation is within tolerance — with no
DirectX, no driver, and no third-party dependency. That is RAW's telos, standing on its
own, in its first increment.
