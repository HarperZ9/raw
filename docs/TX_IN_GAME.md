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

## Open

- The compass location icon (UI, most likely a blend state or a texture format in the Scaleform path).
- Captures at 3840 x 2160, the inn, and night.
- A frame-time comparison with vsync handled the same way on both sides.
- `SetMaximumFrameLatency` is not applied to the flip chain.
- The debug layer on for one run, to count errors.
