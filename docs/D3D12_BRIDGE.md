# D3D12 bridge, stage 1

Started 2026-11-17. RAW keeps the game's D3D11 device and opens its own D3D12 device on
the same adapter. The two share three textures and one fence. RAW's passes run on the
D3D12 queue and the result goes back into the game's frame before the UI.

```
D3D11  end of opaque pass   depth SRV -> DepthCopy CS -> shared R32F depth
D3D11  UI phase entered     backbuffer -> shared colour-in, Signal(n), Flush
D3D12                       Wait(n), DepthFog pass -> shared colour-out, Signal(n+1)
D3D11                       Wait(n+1), shared colour-out -> backbuffer
```

If a frame never enters the UI phase (menus, intro), the pass runs at Present instead
(`FallbackAtPresent=1`).

## Files

| file | role |
|---|---|
| `src/d3d12_bridge/D3D12Bridge.h` | public API, `PassConstants`, `IBridgePass` seam |
| `src/d3d12_bridge/D3D12Bridge.cpp` | D3D12 device by LUID, shared textures, views, queries |
| `src/d3d12_bridge/BridgeFrame.cpp` | depth capture, the round trip, GPU timestamps on both queues |
| `src/d3d12_bridge/BridgeSelfTest.cpp` | fence self-test with CPU waits only, D3D11-created fence fallback |
| `src/d3d12_bridge/DepthFogPass.cpp` | first D3D12 pass (root signature, PSO, full-screen triangle) |
| `src/d3d12_bridge/shaders/DepthFog.hlsl` | haze from linearised depth; mode 0 identity, 2 depth view; marker swatches |
| `src/d3d12_bridge/shaders/DepthCopy.hlsl` | D3D11 compute copy of depth into the shared R32F texture |
| `src/d3d11_proxy/BridgeHook.cpp` | phase hooks, ini, timing log, backbuffer capture to BMP |
| `tests/bridge_roundtrip/` | standalone round-trip test with controls |
| `config/d3d11_proxy.ini` | `[D3D12Bridge]` keys (off by default in code; on in this sample) |

Build without vcpkg: `cmake -S . -B build-bridge -G Ninja -DCMAKE_BUILD_TYPE=Release -DRAW_BUILD_PLUGIN=OFF`
from a VS developer prompt. Shaders are compiled by fxc into headers at build time. The proxy
still imports only KERNEL32; d3d12.dll and dxgi.dll are loaded at runtime.

## Design decisions found by measurement

- **D3D11 opens a D3D12 texture only when it is render-target capable.** A no-flag texture and
  a depth-stencil texture both returned E_INVALIDARG from `OpenSharedResource1` (RTX 4090,
  2026-11-17). Colour textures are shared with ALLOW_RENDER_TARGET plus simultaneous access.
  Depth crosses as R32_FLOAT written by a small D3D11 compute pass, which saves and restores the
  three compute bindings it touches. The proxy's state cache covers only graphics stages.
- **Work on the device's own immediate context**, taken with `GetImmediateContext` from the
  real device.
- **The test exe lives in its own folder.** Next to the proxy it loads RAW's d3d11.dll and gets
  a wrapped device that does not answer `ID3D11Device5`. Copied next to the proxy on purpose
  (`bridge_roundtrip_viaproxy.exe`) it takes the real device from `PG_GetProxyInterface`, which
  reproduces the in-game process layout.
- **D3D11 signals before D3D12 submits.** See "What broke, and the fix" below.
- **Self-test before any GPU wait.** A first in-game run queued D3D11 `Wait` while D3D11
  `Signal` was failing; the D3D11 queue blocked, both devices were removed (fence completed value
  became UINT64_MAX, `CreateTexture2D` returned 0x887A0005) and the game froze on the intro.
  Since then `Initialize` runs a handshake with CPU waits only and leaves the bridge off if it
  fails, and `Execute` checks the `Signal` result before it queues a `Wait`.

## Round-trip test (`bridge_roundtrip.exe`)

Checks, each must pass for exit code 0:
1. identity pass: every pixel identical after D3D11 -> D3D12 -> D3D11 (tolerance 0)
2. haze over a banded R24G8 depth texture matches a CPU reference (tolerance 2/255)
3. haze over a real D24S8 depth-stencil cleared to 0.75 matches the reference
4. comparator self-check: one corrupted reference pixel is reported
5. control: D3D12 waits for a fence value D3D11 never signals; both queues must still be
   unfinished after 750 ms (CPU wait times out, a D3D11 event query stays pending), the stall is
   released from the CPU, and a correct round trip passes afterwards
6. GPU timestamps read back on both queues at 1920x1080 and 3840x2160

Options: `--warp`, `--skyrim-like` (DXGI 1.0 adapter, `D3D11CreateDeviceAndSwapChain`, blit swap
chain on a hidden window), `--frames N`, `--out file.json`. All pass on the RTX 4090, on WARP, in
Skyrim-like mode, through the proxy, and renamed to `SkyrimSE.exe`.

## Measured cost offline (RTX 4090, 2026-11-17, `evidence/bridge-roundtrip-rtx4090.json`)

Median over 232 frames, after the submit-order fix. Another process held the GPU
at about 30 percent during the run. The p95 bridge cost is 0.381 ms at 1080p and 0.454 ms at
2160p (an earlier run under heavier contention had p95 near 50 ms).

| | 1920x1080 | 3840x2160 |
|---|---|---|
| depth copy (D3D11 CS) | 0.025 ms | 0.028 ms |
| colour in (copy) | 0.008 ms | 0.024 ms |
| colour out (copy) | 0.007 ms | 0.024 ms |
| fence round trip (stall minus D3D12 pass) | 0.316 ms | 0.305 ms |
| **bridge cost** | **0.355 ms** | **0.381 ms** |
| D3D12 haze pass | 0.007 ms | 0.024 ms |

The fence round trip dominates. Stage 2 removes it by owning the whole frame on D3D12.

## In game (Skyrim SE 1.6.1170 with SKSE, 2026-11-17)

**The D3D12 pass runs in game.** At the main menu (3840x2160) the backbuffer read from the GPU
shows the four marker swatches top-left, and the haze over every far pixel; the near logo
geometry stays clear, so the pass is reading the game's depth. The bridge ran on every frame
(7,600+ frames in one session) and the game kept running and responding.

Frame cost in game, mean over each 300-frame window, GPU timestamps, 19 windows
(`evidence/ingame-2026-11-17-bridge-running.log`):

| | first window (intro) | menu windows, range |
|---|---|---|
| bridge cost | 0.42 ms | 0.53 to 1.36 ms |
| of which fence round trip | 0.33 ms | 0.39 to 1.10 ms |
| depth copy + colour in + colour out | 0.09 ms | 0.13 to 0.34 ms |
| D3D12 haze pass | 0.026 ms | 0.034 to 0.065 ms |

The menu windows vary with whatever else the GPU is doing; another worker shared the GPU
during this session. The fence round trip is still the largest part.

### What broke, and the fix

The first in-game runs failed in `ID3D11DeviceContext4::Signal` with E_INVALIDARG for both fence
routes. The bisect (`evidence/ingame-2026-11-17-fence-probe-bisect.log`):

- `ProbeFenceSupport` right after `D3D11CreateDeviceAndSwapChain`, at the first Present, and
  between every bridge setup stage: local and shared D3D11 fences signal and complete every time.
  So the device was never in a mode that rejects fences, the context is the device's own
  immediate context (same pointer both ways, QI for Device5 and Context4 succeeds), the device
  flags are 0 at feature level 11_0, and the game renders from a different thread than the one
  that created the device without that mattering.
- The difference was ordering. The bridge queued the D3D12 `Wait(n)` and `Signal(n+1)` before
  D3D11 issued `Signal(n)`. In the game process D3D11 then rejected `Signal(n)` with E_INVALIDARG,
  apparently because a higher value was already pending on the shared fence. Offline the same
  order worked, so this depends on timing or on the process.
- Fix: the D3D12 command list is still recorded first, but it is submitted (queue `Wait`,
  `ExecuteCommandLists`, `Signal`) only after D3D11 has signalled and flushed. The self-test uses
  the same order. Keyed mutexes were not needed.

### Limits of this result

- The menu never enters the UI phase as `RenderPhaseDetector` sees it, so the pass ran at
  Present (`FallbackAtPresent`) and the haze also covers the menu text. A gameplay scene with a
  detected UI phase is the next capture.
- The capture is the backbuffer read back on the GPU (`CaptureFrame` in the ini), not a desktop
  screenshot.
- ENB backup and restore: 232 files with SHA256SUMS, restored with `restore-enb.cmd` after every
  session, all hashes matching.

## Older vertex and geometry shader work (searched 2026-11-17)

- `src/d3d11_proxy/ShaderManager.*`: FNV-1a hash of every VS and PS at creation and bind-time
  replacement by hash (`VSSetShader`/`PSSetShader` in WrappedContext).
- `src/d3d11_proxy/MaterialPipeline.*`, `src/core/DXBCPatcher.*`: pixel shader DXBC patching
  that adds G-buffer outputs to BSLighting shaders.
- Geometry shaders: `CreateGeometryShader*` and `GSSetShader` are pass-through only; no GS work
  exists. `Shaders/` holds only full-screen and debug vertex shaders.
- The skyrimbridge repo (`src/d3d11_proxy/`) carries the same proxy lineage with the same files.

## Stage 2 (full takeover) needs

1. UI-phase detection in menus, so the pass composites under the HUD everywhere.
2. A D3D11 call translation layer on D3D12: resources (textures, buffers, views) created on
   D3D12 and handed to the game through the wrapped device; state objects mapped to PSOs with a
   cache keyed by shader hashes plus state; draw recording into D3D12 command lists; constant
   buffer Map/Discard on a ring allocator; queries mapped to query heaps.
3. Shader translation: the game's DXBC SM5 runs on D3D12 as-is, so ShaderManager's hashes and
   MaterialPipeline's DXBC patching carry over; root signatures generated from DXBC reflection.
4. The swap chain owned by D3D12 (flip model) and Present moved there, so the fence round trip
   disappears.
5. superframe `native/` behind `IBridgePass`: its RHI needs an adopt-device constructor (LUID),
   import of an external texture as a `TextureHandle`, and queue-level wait and signal on an
   external fence in place of `submitAndWait`.
