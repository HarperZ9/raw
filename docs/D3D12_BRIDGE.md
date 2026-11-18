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

## Measured cost (RTX 4090, 2026-11-17, `evidence/bridge-roundtrip-rtx4090.json`)

Median over 472 frames. Another process held the GPU at 30 to 99 percent during every run, so
treat the medians as under load; the p95 (about 45 to 52 ms) is dominated by that contention.

| | 1920x1080 | 3840x2160 |
|---|---|---|
| depth copy (D3D11 CS) | 0.024 ms | 0.028 ms |
| colour in (copy) | 0.007 ms | 0.023 ms |
| colour out (copy) | 0.007 ms | 0.023 ms |
| fence round trip (stall minus D3D12 pass) | 0.332 ms | 0.345 ms |
| **bridge cost** | **0.371 ms** | **0.420 ms** |
| D3D12 haze pass | 0.007 ms | 0.024 ms |

The fence round trip dominates. Stage 2 removes it by owning the whole frame on D3D12.

## In game (Skyrim SE 1.6.1170 with SKSE, 2026-11-17)

- The ENB files were backed up to a dated backup folder (232 files, SHA256SUMS,
  `restore-enb.cmd`) and restored after testing; all 232 hashes matched.
- The proxy loads, the bridge creates its D3D12 device on the game's adapter and its shared
  textures, and the game reaches data-loaded with the bridge present.
- **Stopped at the fence:** in the game process `ID3D11DeviceContext4::Signal` returns
  E_INVALIDARG for both fence routes (created on D3D12 and opened on D3D11, and created on D3D11
  and opened on D3D12). The vtable entry resolves to the NVIDIA driver (`nvwgf2umx.dll`), so the
  call is not hooked. The self-test catches it and the bridge stays off; the game keeps running
  and responding. Logs: `evidence/ingame-2026-11-17-*.log`.
- Ruled out offline (each passes): a DXGI 1.0 adapter with `D3D11CreateDeviceAndSwapChain` and a
  blit swap chain, running through the proxy, the exe named `SkyrimSE.exe`, an open
  TIMESTAMP_DISJOINT query on the context (`RAW_TEST_OPEN_DISJOINT=1`). The only compat layer
  on the game exe is HIGHDPIAWARE. Not yet tested: whatever else in the game process changes the
  device (Steam overlay, the game's own device use from more than one thread, an NVIDIA profile
  keyed on the full path).
- Next probe: call `Signal` once right after `D3D11CreateDeviceAndSwapChain` returns, inside
  the proxy, before the game touches the device. If it works there, something the game does
  later breaks it; if not, the device is created in a mode that rejects monitored fences. The
  fallback if fences stay unusable: keyed mutexes (`IDXGIKeyedMutex`) on the shared textures.

## Older vertex and geometry shader work (searched 2026-11-17)

- `src/d3d11_proxy/ShaderManager.*`: FNV-1a hash of every VS and PS at creation and bind-time
  replacement by hash (`VSSetShader`/`PSSetShader` in WrappedContext).
- `src/d3d11_proxy/MaterialPipeline.*`, `src/core/DXBCPatcher.*`: pixel shader DXBC patching
  that adds G-buffer outputs to BSLighting shaders.
- Geometry shaders: `CreateGeometryShader*` and `GSSetShader` are pass-through only; no GS work
  exists. `Shaders/` holds only full-screen and debug vertex shaders.
- The skyrimbridge repo (`src/d3d11_proxy/`) carries the same proxy lineage with the same files.

## Stage 2 (full takeover) needs

1. A working fence in the game process (above), or keyed mutexes as the fallback.
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
