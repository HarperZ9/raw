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
| `src/d3d12_bridge/D3D12Bridge.h` | public API, `PassConstants`, `HostResources`, `IBridgePass` seam |
| `src/d3d12_bridge/EnginePass.cpp` | the Playground engine's RHI and GTAO behind the seam |
| `src/d3d12_bridge/RealtimeAoPass.cpp` | the engine's real-time AO behind the seam (`AoMode=realtime`) |
| `src/d3d12_bridge/LightingPass.cpp`, `LightClusters.cpp`, `BridgeLighting.cpp` | the engine's deferred lighting of the frame (`Pass=lighting`, docs/LIGHTING.md) |
| `src/d3d11_proxy/SceneLights.cpp` | the frame's lights, sun, ambient and cascades, decoded from the game's constants at its draws |
| `tools/harness/` | the unattended test harness (docs/HARNESS.md) |
| `src/d3d12_bridge/BridgeShare.cpp`, `BridgeReadback.cpp` | on-demand sharing of any game buffer, D3D12 reads |
| `src/d3d12_bridge/D3D12Bridge.cpp` | D3D12 device by LUID, shared textures, views, queries |
| `src/d3d12_bridge/BridgeFrame.cpp` | depth capture, the round trip, GPU timestamps on both queues |
| `src/d3d12_bridge/BridgeSelfTest.cpp` | fence self-test with CPU waits only, D3D11-created fence fallback |
| `src/d3d12_bridge/DepthFogPass.cpp` | first D3D12 pass (root signature, PSO, full-screen triangle) |
| `src/d3d12_bridge/shaders/DepthFog.hlsl` | haze from linearised depth; mode 0 identity, 2 depth view; marker swatches |
| `src/d3d12_bridge/shaders/DepthCopy.hlsl` | D3D11 compute copy of depth into the shared R32F texture |
| `src/d3d11_proxy/BridgeHook.cpp` | phase hooks, ini, timing log, backbuffer capture to BMP |
| `tests/bridge_roundtrip/` | standalone round-trip test with controls |
| `tests/ao_realtime/` | the real-time AO against the 16 x 16 reference, with cost at each step and controls |
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

Items 2 to 4 now exist as an offline-proven layer: `src/dx12/translate/`, described in
`docs/D3D11_TO_D3D12.md`, with the call-stream recorder in `docs/D3D11_CALL_STREAM.md`. It has
not run a game frame yet.

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

## The Playground engine behind the seam (2026-11-18)

The pass seam now hands a pass the bridge's device, queue, fence and shared textures
(`HostResources` in `D3D12Bridge.h`). A pass records and closes its own commands, the bridge
signals the shared fence from D3D11 and flushes, and only then the pass submits: record,
D3D11 Signal and Flush, submit. `DepthFogPass` moved onto this seam with its own command
list, views and timestamps. `EnginePass` puts the engine's RHI (superframe `native/`) on the
same seam: `adoptDevice`, `importTexture`, `endRecording`, `submit` with the bridge's fence,
`retire`. D3D12 timestamps now come from the pass; for the engine pass that is the RHI.

Build with the engine: `-DRAW_ENGINE_DIR=<superframe>/native`. The proxy cannot link without it
(the bridge, the hook layer and hot reload call the engine's passes directly), so configuring
without it stops with that message. It needs superframe main with
host frames and with post passes that index across dispatch rows (a 3840 x 2160 frame has more
pixels than one dispatch row covers). With the engine linked the proxy delay-loads d3d12.dll
and dxgi.dll; its load-time imports are KERNEL32 and USER32.

The first engine pass is the engine's checked GTAO, unchanged. `GamePack.hlsl` builds the
view-space G-buffer the engine reads from the game's depth and normal targets
(`docs/FRAME_BUFFERS.md`), `post_gtao` computes visibility, `AoComposite.hlsl` multiplies the
frame by it in linear light. The pass runs after the game's final blit and before the HUD.
Without normals or a camera the pass leaves the frame unchanged; it never falls back to depth
alone.

```ini
[D3D12Bridge]
Enabled=1
Pass=engine      ; haze keeps the built-in depth haze
AoRadius=35      ; game units
AoSlices=4       ; 0 keeps the engine's 16
AoSteps=8        ; 0 keeps the engine's 16
```

### Checks

`tests/engine_gtao` (`engine_gtao_check.exe`) sends a depth buffer, a normal target and a frame
through the real path (D3D11 textures, the bridge, the engine pass on D3D12, back) and compares
the visibility with the engine's CPU reference `raw::post::gtao` at the engine's own bound:
1e-3 a pixel on 99.9 % of drawn pixels. A control with another radius must fail.

| Input | Size | Slices x steps | Outside 1e-3, first build | Outside 1e-3, after the fix | Bound 0.1 % |
|---|---|---|---|---|---|
| synthetic scene | 640 x 360 | 16 x 16 | 0.015 % | 0.0009 % | met |
| captured gameplay frame | 3840 x 2160 | 16 x 16 | 0.440 % | 0.040 % | met after the fix |
| captured gameplay frame | 3840 x 2160 | 4 x 8 | 0.500 % | 0.013 % | met after the fix |
| visibility computed in the game, same frame's shared buffers | 3840 x 2160 | 4 x 8 | 0.505 % | 0.013 % | met after the fix |

**The cause of the first build's miss, and the fix.** The inputs were not the cause: the GPU pass
and the reference read the same packed G-buffer, positions are view-space (rebuilt from depth,
never world-space), and the depth and normal decoding pass their own checks
(`docs/FRAME_BUFFERS.md`). The miss was inside the pass. `tests/engine_gtao/gtao_precision.*` is
a copy of the pass with the floating-point type of each stage as a parameter; all in double it
equals the reference exactly. On the in-game buffers, against the reference:

| Variant | Outside 1e-3 |
|---|---|
| all float (the GPU's arithmetic) | 0.463 % |
| sample coordinates in double, the rest float | 0.459 % |
| horizon math in double, the rest float | 0.462 % |
| slice direction in double, the rest float | 0.011 % |
| all float, slice direction from the projection's derivative | 0.011 % |

One stage carried the whole error: the slice's screen direction. The pass projected the pixel
and a point 1e-3 of its depth away and subtracted the two. In float32 that subtraction turns the
direction by about 1e-4, and over a radius of a few hundred pixels that moves a sample into the
next pixel. Large pixel coordinates and the half-texel convention were not the cause: sample
coordinates in double changed nothing. The engine's shader now takes the direction from the
derivative of the projection along the slice (HarperZ9/superframe#119), which points the same way
and subtracts no nearly equal numbers. The bound stays at 0.1 %.

The round-trip test and its wrong-fence control still pass, with six on-demand share cases
and their controls added.

### Cost (RTX 4090, 3840 x 2160, GPU timestamps)

| | Engine pass | of which GTAO | Bridge cost |
|---|---|---|---|
| offline, 16 x 16, GPU otherwise idle | 199.4 ms | 198.5 ms | 1.07 ms |
| offline, 4 x 8, GPU idle at the start of the run | 25.3 ms | 24.4 ms | 0.62 ms |
| offline, 4 x 8, another process at 95 to 100 % | 25.9 to 29.3 ms | 25.0 to 28.0 ms | 0.56 to 0.78 ms, and 339 and 794 ms in two runs |
| in game, 4 x 8, another process at 99 to 100 % | 24.2 to 27.5 ms | 24.1 to 26.6 ms | 0.56 and 0.58 ms in two windows, 9.9 and 30.6 ms in two |

In-game values are means over 60-frame windows in a loaded outdoor scene
(`evidence/frame-buffers-2026-11-18/ingame-engine-gtao.log`). No in-game number was taken on
an idle GPU: another job held it during every session. The engine's GTAO is a reference-grade
pass (512 depth samples a pixel at its defaults); at 4K it is far from a real-time budget. The
real-time form (half-resolution trace, temporal accumulation on the game's motion vectors, a
depth hierarchy) is not built yet; the sample-count curve below is its starting point.

Sample-count curve on the captured frame, 3840 x 2160, GPU idle at the start
(`tools/frame_buffers/ao_curve.py`, `evidence/frame-buffers-2026-11-18/gtao-sample-curve-4k.json`).
Accuracy is against the pass at its defaults; the last column is the GPU against the CPU
reference at the same setting, which stays under the 0.1 % bound everywhere.

| Slices x steps | Depth samples a pixel | Pass | Mean abs. difference to 16 x 16 | Pixels over 0.05 away | Outside 1e-3 of the reference |
|---|---|---|---|---|---|
| 16 x 16 | 512 | 192.9 ms | 0 | 0 % | 0.039 % |
| 8 x 16 | 256 | 97.7 ms | 0.028 | 18.7 % | 0.026 % |
| 8 x 8 | 128 | 50.7 ms | 0.062 | 31.3 % | 0.021 % |
| 4 x 8 | 64 | 25.5 ms | 0.072 | 33.1 % | 0.014 % |
| 2 x 8 | 32 | 13.2 ms | 0.104 | 49.7 % | 0.008 % |
| 4 x 4 | 32 | 13.2 ms | 0.129 | 43.2 % | 0.012 % |
| 2 x 4 | 16 | 7.0 ms | 0.152 | 55.9 % | 0.006 % |
| 1 x 4 | 8 | 4.0 ms | 0.322 | 82.2 % | 0.009 % |

Cost is linear in samples, about 0.38 ms for each sample a pixel at this size, plus 0.8 ms to
pack the G-buffer. Fewer fixed directions lose accuracy fast: the pass has no noise, so a cut
in directions is a bias and not a variance. Under 1.5 ms at 2160p needs a quarter of the
pixels (half-resolution trace with a depth-aware upsample), about 8 samples a pixel with
per-pixel rotated directions, and accumulation over frames with the game's motion vectors.
That pass is built now: see "Real-time AO" below.

The pack step costs 0.75 ms and the composite 0.08 ms. The normal copy on D3D11 costs 0.04 ms.

### What broke, and the fix

The first in-game run with the engine pass at 16 x 16 froze the game. The frame before had not
retired within the pass's 2 s limit, the bridge treated that as fatal and turned itself off, and
the game's D3D11 queue was left waiting for a fence value nobody would signal. Windows logged a
display driver event (nvlddmkm 153) at the same time; which of the two came first is unknown.
With another process loading the GPU, hand-offs between the two devices were later measured at
hundreds of milliseconds.

Changes: the pass waits up to 15 s for the frame before; when a pass fails, the bridge signals
the awaited fence value from the CPU before it turns off, so the game keeps rendering; a bridge
that turned itself off stays off. The 4 x 8 run after these changes ran 277 gameplay frames
with no failure.

The release was then tested in game on purpose (`TestStallAtBridgeFrame=40`, `RetireTimeoutMs=400`):
at the 40th gameplay frame the pass waited for a fence value nobody signals, the next frame gave
up after 400 ms, the bridge signalled the awaited value from the CPU and turned itself off, and
the game went on presenting (600 further frames logged, `evidence/frame-buffers-2026-11-18/ingame-fence-release-test.log`).

### Limits

- The engine's RHI retires one submission before it records the next, so the CPU waits for
  the GPU once a frame. A ring of command lists in the RHI would remove that wait.
- The engine's camera model has no principal-point offset. Positions are rebuilt from depth
  with the frame's projection scale and depth terms; the sub-pixel jitter of temporal
  anti-aliasing (under half a pixel) is not applied.
- The composite runs on the tone-mapped frame and darkens direct light too. Applying
  visibility to ambient light only needs the HDR scene before the game's own post chain.
- The hook follows the game's targets. A frame with no world (menus) does not bind them, so
  menus run the pass at Present.

## Real-time AO (2026-11-18)

`Pass=engine` with `AoMode=realtime` runs the engine's real-time AO (`RealtimeAoPass.cpp`; the
pass itself is superframe `native/raw/renderer/ao_realtime.hpp`). It computes the same GTAO
integral as the reference pass at a frame budget. On the captured gameplay frame at
3840 x 2160 the pass takes **0.66 ms** on the RTX 4090, plus 0.18 ms for the D3D11 copies
that feed it: **0.84 ms** of AO in the frame, under the 1.5 ms target.

```ini
[D3D12Bridge]
Enabled=1
Pass=engine
AoMode=realtime   ; reference (default) keeps the 16 x 16 pass above
AoRadius=35       ; game units
AoSlices=2        ; 0 means 2
AoSteps=16        ; a side; 0 means 16
AoNoise=1         ; blue-noise slice turn and step offset
AoTemporal=1      ; accumulate along the game's motion vectors
AoLevels=1        ; far steps read coarser depth levels
AoHistory=32      ; frames the running mean holds
AoDepthTolerance=0.1
;AoMotionScaleX=1 ; previous uv = uv + motion * scale; change only to test
;AoMotionScaleY=1
```

### How a frame runs

1. **Inputs, no pack step.** The engine's RHI reads the shared textures directly: depth as
   R32F, the normal target in the game's own encoding, the motion-vector target (shared
   every frame like the normals; its copy runs inside the normal copy's timestamps) and
   the frame. The 0.75 ms pack and the two texture-to-buffer copies of the reference path
   are gone.
2. **Depth levels (0.04 ms).** View depth at half resolution, one pixel of each 2 x 2 block,
   then three coarser levels. A level texel is the mean of the texels within 10 % of the
   nearest one, so it stands for the front surface of its block.
3. **Trace at half resolution (0.47 ms).** Two slices a pixel, 16 steps a side. The slice
   turn and the step offset come from a 64 x 64 blue-noise field, generated at start by
   void and cluster and moved each frame by a rank-1 sequence. Depth at a step is
   interpolated in 1 / depth at the exact position; where the four texels span more than
   10 % the nearest one answers alone. A step reads the level whose texels are about half
   the step spacing.
4. **Resolve at full resolution (0.15 ms).** A depth-weighted upsample, then a running mean
   along the game's motion vectors. The traced pixel of each block cycles through all four
   in four frames; a pixel's own trace weighs 1 and a value borrowed from its neighbours
   0.15, so the history keeps full-resolution detail. History is rejected where it is off
   screen or held a surface at another depth (10 %). A pixel with under 7 frames of history
   takes a 4 x 4 depth-weighted mean of this frame. The same pass writes the frame times
   the visibility into the shared output.

**The depth hierarchy is built on the D3D12 side.** The proxy lineage has `src/core/HiZPyramid`,
which was not reused, for three measured or read reasons. It keeps the maximum of the raw
depth value, written for reversed depth; this game's depth is 0 near and 1 far, so its
levels hold the farthest surface, and a horizon search needs the nearest. It lives in the
SKSE plugin sources, which the proxy build does not compile. And it is one mip-chained D3D11
texture, while the bridge shares single-level textures: every level would cross with its own
copy (0.02 to 0.03 ms each at this size) after the D3D11 build. Building four levels on D3D12
costs 0.037 ms in total and cuts the trace from 0.68 to 0.47 ms.

### Cost and accuracy, step by step

`tests/ao_realtime` (`ao_realtime_check.exe`) sends the captured buffers through the real
path and scores the visibility against the engine's reference GTAO at 16 slices by 16 steps
on the same buffers: mean absolute difference over the 7,698,584 drawn pixels, and the share
of those pixels more than 0.05 away. Times are D3D12 timestamps from the RHI, median of 60
frames, with the 10th and 90th percentiles. Another process was on the GPU: `nvidia-smi` read
100 % before the run, and a second run read the same medians within 0.01 ms
(`evidence/realtime-ao-2026-11-18/ao-realtime-captured-4k.json`). No run on an idle GPU was
possible in this session.

| Step | Pass, ms (p10 to p90) | Levels, trace, resolve | Mean difference to 16 x 16 | Over 0.05 | Mean difference to 48 x 48 |
|---|---|---|---|---|---|
| Reference pass, 16 x 16, full resolution (table above) | 192.9 | | 0 | 0 % | 0.062 |
| Reference pass, 4 x 8, full resolution (table above) | 25.5 | | 0.072 | 33.1 % | |
| 1. Half resolution, 2 fixed slices x 16, depth-aware upsample, one frame | 0.567 (0.564 to 0.569) | 0.037, 0.433, 0.096 | 0.091 | 47.3 % | 0.107 |
| 2. Blue noise, 1 slice x 16, one frame | 0.613 (0.611 to 0.618) | 0.038, 0.432, 0.144 | 0.086 | 49.9 % | 0.102 |
| 2. Blue noise, 2 slices x 16, one frame | 0.861 (0.858 to 0.868) | 0.038, 0.679, 0.144 | 0.071 | 41.1 % | 0.090 |
| 3. Accumulation, 4 frames | 0.868 (0.861 to 0.877) | 0.038, 0.677, 0.151 | 0.062 | 37.5 % | 0.082 |
| 3. Accumulation, 32 frames | 0.865 (0.860 to 0.869) | 0.037, 0.680, 0.149 | 0.044 | 26.9 % | 0.065 |
| **4. Depth levels, 32 frames: the final configuration** | **0.659** (0.657 to 0.661) | 0.038, 0.473, 0.149 | **0.050** | 29.9 % | 0.060 |
| Final, first frame: every pixel without history | 0.661 (0.658 to 0.669) | 0.037, 0.473, 0.153 | 0.077 | 43.0 % | 0.086 |
| Final, 64 frames | 0.659 (0.658 to 0.661) | 0.038, 0.473, 0.149 | 0.048 | 28.3 % | 0.058 |
| 1 slice x 6 | 0.296 (0.296 to 0.297) | 0.037, 0.108, 0.150 | 0.088 | 44.0 % | 0.126 |
| 1 slice x 16 | 0.446 (0.445 to 0.448) | 0.037, 0.259, 0.150 | 0.059 | 36.5 % | 0.068 |
| 1 slice x 24 | 0.645 (0.642 to 0.649) | 0.037, 0.458, 0.149 | 0.069 | 43.1 % | 0.058 |
| 1 slice x 32 | 0.893 (0.889 to 0.900) | 0.037, 0.706, 0.149 | 0.072 | 42.2 % | 0.049 |
| 2 slices x 24 | 0.928 (0.924 to 0.933) | 0.037, 0.741, 0.149 | 0.057 | 33.7 % | 0.045 |

The D3D11 copies that feed the pass (depth through the compute copy, normals, motion
vectors) add 0.18 ms in every row. The fence round trip and the two frame copies are the
bridge's own cost and are the same for any pass.

**What the last column shows.** On this frame the 16 x 16 reference is not at its own limit.
Against the same pass at 48 x 48 it differs by 0.062 on average and reads brighter (signed
mean +0.059): near geometry has an AO radius of several hundred pixels, and 16 steps a side
pass over occluders. So a mean difference to the 16 x 16 reference near 0.05 is the floor
for a pass that finds more of them. The final configuration is 0.050 from the 16 x 16
reference and 0.060 from the 48 x 48 one, a little closer to the dense result than the
16 x 16 reference is. More steps move the pass towards the dense reference and away from
the 16 x 16 one (the 1 x 32 and 2 x 24 rows). The depth levels do the same: they cost
accuracy against 16 x 16 (0.044 to 0.050) and gain it against 48 x 48 (0.065 to 0.060).

### Controls

| Control | Result |
|---|---|
| Twice the radius | 0.112 against 0.050: reported |
| Pan of 12 frames at 8 pixels a frame, right motion vectors | 0.058 over the frame, 0.077 where the reference is occluded (4,272,942 pixels under 0.9) |
| The same pan, zero motion vectors, depth rejection on | 0.119 and 0.152 |
| The same pan, zero motion vectors, depth rejection off | 0.154 and 0.184: ghosting, reported |

The pan is synthesised: each frame shows the captured buffers moved 8 pixels to the right,
with the edge repeated, and the reference is computed on the moved buffers. The captured
frame has a still camera, so no real pair of frames exists yet. With the right vectors the
error after 12 frames of motion (0.058) sits between the first frame (0.077) and 32 still
frames (0.050). A wrong vector smears the history across the frame; the depth rejection
catches part of it, and the metric shows the rest.

The test also runs on a synthetic scene with the D3D12 debug layer on
(`evidence/realtime-ao-2026-11-18/ao-realtime-synthetic.json`); `bridge_roundtrip` and
`engine_gtao_check` still pass.

### Limits

- **Not yet run in game.** The pass, the motion-vector share and the ini keys are built and
  tested offline through the bridge; the in-game capture is the next step.
- **The motion-vector sign is taken from the decoded shader** (previous uv = uv + motion) and
  is untested on a moving camera. `AoMotionScaleX` and `AoMotionScaleY` flip it without a
  rebuild.
- **One frame of one outdoor scene**, heavy with grass and foliage. Interiors will score
  differently.
- **Blended surfaces, water and the first-person model** have no depth or normal in the
  buffers yet (`docs/FRAME_BUFFERS.md`), so the pass sees the surface behind them.
- The motion vector has no depth component: a camera moving fast along its view axis can
  trip the 10 % depth rejection and restart the history.
- Under motion the noise sequence a surface sees is no longer low-discrepancy in time, so
  convergence is slower than on a still frame.
- The composite still runs on the tone-mapped frame and darkens direct light too.

## Real-time AO in game (2026-11-18)

`Pass=engine`, `AoMode=realtime`, `AoRadius=35`, `FallbackAtPresent=0`, the rest at its defaults,
3840 x 2160, with the G-buffer patches on (`docs/FRAME_BUFFERS.md`). Means over 300-frame windows,
GPU timestamps (`evidence/gbuffer-2026-11-18/ingame-*.log`). The game itself kept the GPU near
100 %, and another process used it during parts of both sessions.

| | Riverwood, day (9,303 frames) | The Bannered Mare (7,203 frames) |
|---|---|---|
| AO pass on D3D12, median of the windows | 0.70 ms (0.70 to 0.74) | 0.45 ms (0.45 to 0.50) |
| of which depth levels, trace, resolve | 0.04, 0.48, 0.19 ms | 0.02, 0.30, 0.14 ms |
| depth and normal copies on D3D11 | 0.14 ms | 0.08 ms |
| **AO in the frame** | **0.84 ms** | **0.53 ms** |
| bridge hand-off (fence), median | 0.35 ms | 0.33 ms |

The hand-off is the bridge's own cost and leaves with the D3D11 device. In a few windows it
averaged 8 to 25 ms: single long stalls while another process held the GPU.

The moving-camera check in `docs/FRAME_BUFFERS.md` confirms the pass's motion convention
(previous uv = uv + motion) on game frames.

### Open defect: the D3D12 device is lost at the main menu

Status: open, cause not found. Workaround: `FallbackAtPresent=0`.

Two of the first four sessions with the real-time pass lost the D3D12 device at the main menu
(`DXGI_ERROR_DEVICE_RESET`, 0x887A0007, after 519 and 277 bridge frames; the display driver logged
event 153 at the same time). The bridge released its fence and turned itself off, the screen
stayed black and the session ended without gameplay. In the menu the pass has no camera and no
normals and still ran its three stages at Present. The cause is not found, and the logs of the two
sessions were not kept. With `FallbackAtPresent=0` the pass runs only in frames with a world; the
four sessions after that change ran without a failure (up to 9,303 frames). Until the cause is
found, use `FallbackAtPresent=0` with `AoMode=realtime`.
