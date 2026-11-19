# Skyrim on raw::tx: the game rendering on Direct3D 12

With `[Translate] Enabled=1` in `d3d11_proxy.ini`, the proxy answers the game's
`D3D11CreateDeviceAndSwapChain` with the translation layer (`src/dx12/translate/`, docs/D3D11_TO_D3D12.md):
every D3D11 call the game makes becomes D3D12 work on one device, and frames go out through a
D3D12 flip-model swap chain. Nothing else in the proxy runs in this mode. `Native=1` gives the game
the system runtime's objects instead, behind the same thin swap-chain wrapper, so the same harness
views can be captured on native D3D11 for comparison.

```
[Translate]
Enabled=1
Native=0        ; 1: system D3D11 behind the same wrapper (the reference run)
DebugLayer=0    ; 1: the D3D12 debug layer; its errors are counted
```

## Files

| File | Role |
|---|---|
| `src/d3d11_proxy/TxMode.cpp` | the create path, the layer's log routed into the proxy log, a fault logger (module offset, registers, the code bytes around the fault and before each caller, the last 48 calls into the layer) |
| `src/d3d11_proxy/TxHarness.cpp` | the swap-chain wrapper: harness heartbeat, world frames counted by a pipeline-statistics query (more than 100,000 primitives), a cell signature from the primitive count, captures on request |
| `src/dx12/translate/tx_dxgi_device.cpp` | `IDXGIDevice2` on the layer's device |
| `tools/harness/tests/tx.txt` | the test: main menu, the `rawharness` save in Riverwood, a turn, the Bannered Mare |
| `tools/harness/compare.py` | CIELAB dE76 between two captures, with a heat map |

Run: `python tools/harness/run.py tx ... --ini <a tx ini>` (docs/HARNESS.md), once with `Native=1`
and once without.

## Log of what broke, in the order the game hit it

| Run | First failure | What it took |
|---|---|---|
| tx-1, tx-2 | Access violation at `SkyrimSE.exe+0xE426DA`, reading address 0, before the first Present. The game's code is encrypted on disk, so the fault logger dumps it from memory. Disassembled: `QueryInterface` on the device for `{77DB970F-6276-48BA-BA28-070143B4392C}` (read from the exe's `.rdata`): `IDXGIDevice1`. The result is not checked; the next call is `SetMaximumFrameLatency(2)`. | The layer's device now answers `IDXGIObject`, `IDXGIDevice`, `IDXGIDevice1` and `IDXGIDevice2` from a part of the same object (shared reference count). `GetAdapter` returns the D3D12 adapter; the frame latency is stored and answered, not yet applied to the flip chain. |
| tx-3 | none in the game. Main menu, the save loads, Riverwood renders, `coc` into the Bannered Mare loads. The harness stopped at the inn because its cell check saw no loading screen. | The heartbeat now carries a cell signature from the frame's primitive count (Riverwood about 3.4 million, the inn about 130,000). |

## First result (2026-11-18, 1920 x 1080, RTX 4090)

Run tx-3, the layer under the whole game:

| | Riverwood, start view | Riverwood, turned |
|---|---|---|
| primitives a frame | 3,434,083 | 2,521,552 |
| draws a frame | 7,876 | 5,210 |
| pipeline states made by then | 263 | 273 |
| unsupported calls | 0 | 0 |
| D3D12 debug errors (layer off) | not measured | not measured |
| mean frame time over 60 frames | 12.26 ms | 8.69 ms |

The native run gave 16.67 ms on both views. That run looks held to 60 Hz by vsync, so the two
times do not compare speed yet.

Against native D3D11 on the same view (run tx-native-1), start view: mean dE76 3.34, 57.4 % of
pixels under dE 2, 93.2 % under dE 10. The heat map puts the difference on foliage moving in the
wind, people who walked, and one real gap: the compass's location icon is drawn natively and
missing through the layer. The turned view is not comparable: the mouse turn landed differently
(17.5 dE). Both captures come through the same wrapper and the same BMP path, and the colour
counts of the sky regions match (17,489 native, 17,627 through the layer), so the capture does not
quantise either frame. The banding visible on the mountains in some stills is cloud shadow in the
scene.

## The whole proxy and RAW's lighting on the layer (Wrap=1)

`[Translate] Wrap=1` runs the normal proxy on top of the layer: the wrapped device, context and
swap chain forward to raw::tx where they used to forward to the system runtime. Run txwrap-1
(Riverwood, 1080p): the G-buffer patches built on the layer (92 patched shaders, 0 refused) and
the frame matched native D3D11 as closely as the plain layer did (mean dE76 3.15 against 3.34).

The bridge then attaches to the layer (`BridgeConfig::layer`, `src/dx12/translate/tx_host.cpp`).
Its textures are the layer's own. The lighting pass runs on the layer's D3D12 device and queue.
`raw::tx::PrepareForHost` submits the layer's recorded work with those textures in COMMON, the
pass follows on the same queue, and the copy-out goes into the layer's next list. No second
device, no shared texture and no fence crossing. Run txlight-1, 1080p:

| | Riverwood day | Bannered Mare |
|---|---|---|
| lighting pass, matched | 0.12 ms | 0.18 ms |
| lighting pass, improved (AO, soft shadows, GGX, fog, contact) | 0.57 ms | 0.89 ms |
| matched against the game's own frame, final image | mean dE76 4.80 | mean dE76 0.93 |

Pictures: `ingame-tx-2026-11-18-attached-{riverwood,inn}-{game,improved}-1080.png` in the plan
folder. Two gaps in attached mode: the match statistics read their buffers through the shared-
fence path, so they do not run, and the bridge's D3D11 stall timer no longer measures a hand-over.

## Open

- The compass location icon (UI, most likely a blend state or a texture format in the Scaleform path).
- Captures at 3840 x 2160, the inn, and night.
- A frame-time comparison with vsync handled the same way on both sides.
- `SetMaximumFrameLatency` is not applied to the flip chain.
- The debug layer on for one run, to count errors.

## The Agility SDK (2026-11-19)

`[Translate] AgilityPath` (the folder holding the DirectX 12 Agility SDK's `D3D12Core.dll`) and
`AgilityVersion` (its SDK version, 620 for 1.620.1) put the layer's device on the Agility runtime.
SkyrimSE.exe exports neither `D3D12SDKVersion` nor `D3D12SDKPath`, so the device comes from
`ID3D12SDKConfiguration1::CreateDeviceFactory`; the debug layer, when asked, comes from the same factory.
If the factory fails, the system runtime is used and the log says why. Build with
`-DRAW_AGILITY_DIR=<the SDK's build/native folder>` for its headers (DXR 1.2, shader model 6.9).
The translate suite runs on it with `RAW_TX_AGILITY_PATH` and `RAW_TX_AGILITY_VERSION` set:
38 of 38 on WARP and on the RTX 4090, with the D3D12 debug layer on and 0 debug errors.
No SDK binary is in the repository; the SDK's terms for redistributing `D3D12Core.dll` apply to any release that ships it.
