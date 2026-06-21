# RAW Eyes — host-side perception for rendering validation

The bottleneck in validating a renderer with an AI assistant is perception: the
assistant cannot see the screen and the human can only describe so much. `raw_eyes.py`
closes that gap. It turns captures into **objective numbers** and **viewable diff
images**, and **attributes** artifacts to specific passes — so "it looks too green"
becomes "`gi.bmp` green_index=1.58 → source: SSGI".

Requires only `numpy` + `Pillow`. No game, no GPU, no recompile.

## The loop (works TODAY with existing F5 dumps — no C++ change)

1. In-game: isolate a pass with the compositor Debug View dropdown, press **F5**.
   RAW writes `frame%04u_<name>.bmp` to `Data/SKSE/Plugins/RAW/Captures/`.
2. Host: point the eyes at that folder.
   ```
   python tools/eyes/raw_eyes.py attribute "<path>/Captures"
   ```
3. The assistant reads the JSON (metrics + attribution) and opens the diff heatmaps,
   then reasons about the actual pixels — no verbal relay needed.

Because dump filenames carry the pass name (`frame0042_GTAO.bmp`, `..._SSR.bmp`,
`..._SSGI.bmp`, `..._Scene.bmp`), `attribute` classifies them automatically.

## Commands

| Command | Purpose |
|---------|---------|
| `analyze IMG` | One capture → JSON: dims, mean RGB, luma min/mean/max/std, %black, %clipped, **green_index**, edge_density, 16-bin luma histogram, per-quadrant luma, **flags**. |
| `compare A B [--out d.png]` | Two captures → mean/max abs diff, PSNR, luma delta (global + per quadrant), %changed, **verdict** (identical/subtle/significant), and a heatmap PNG showing *where* they differ. |
| `attribute DIR` | Every capture in a folder → per-pass metrics + **attribution** (which pass is the likely source of green cast / black output). Reads .bmp/.png/.jpg. |
| `selftest` | Generates synthetic scene / green-tint / black / blown captures and verifies every metric + the attribution fire correctly. 8/8 expected. |

## Health receipts

`raw_health.py` emits the compact receipt consumed by ORCA and the wider organ
exchange.

```
python tools/eyes/raw_health.py receipt "<path>/live"
python tools/eyes/raw_health.py receipt "<path>/RAW"
```

The first form reads runtime telemetry from a live directory and stays strict:
missing always-on runtime artifacts are reported as failures. The second form
detects a RAW source root and emits `mode: source-state`, including source hash,
build-manifest freshness, and DLL presence. A source-state receipt reports
overall `warn` when no live runtime telemetry was measured, which lets ORCA say
"RAW source/build is available, but no live session was observed" instead of
misreading the source tree as a broken live renderer.

## Flags (objective artifact detectors)

- `GREEN_CAST` / `MAGENTA_CAST` — `green_index` (mean G ÷ mean R,B) outside [0.85, 1.15].
  Directly catches the known SSGI green tint.
- `MOSTLY_BLACK` — >60% near-black → a pass producing no output (SSR/AO dead).
- `BLOWN_OUT` — >30% clipped → exposure / tonemap / bloom over-contribution.
- `FLAT_NO_SIGNAL` — luma std < 0.01 → uniform output, effect not varying.

## Validating the MVP stack (Step 1)

Capture each isolated debug view + the composited result, then:
```
python tools/eyes/raw_eyes.py compare Captures/frameNNNN_Scene.bmp Captures/frameNNNN_Final.bmp --out eyes_out/compose.png
python tools/eyes/raw_eyes.py attribute Captures
```
- Gate 2A.1 (compositor identity, effects off): `compare` scene vs final → expect
  `verdict: identical` (or `subtle`), `luma_delta_mean ≈ 0`.
- GTAO: `analyze` the AO view → expect contact darkening (luma < 1 in crevices,
  not `FLAT_NO_SIGNAL`).
- SSGI: `attribute` → expect NO `GREEN_CAST` once fixed (Step 2).

## Optional C++ enhancement (makes capture seamless — later, not required now)

Today's loop needs one F5 per isolated view. To make it one keypress:
- Add `stb_image_write.h` (RAW already vendors `stb_image.h`) and a `DumpPNG` path
  in `TextureDump` (smaller, lossless, alpha-correct vs BMP).
- Add a "dump validation set" hotkey that, in one frame, writes `Scene`, `GTAO`,
  `SSR`, `SSGI`, `ContactShadow`, `Skylighting`, `Bloom`, `Final` using the existing
  `frame%04u_<name>` convention, so `attribute` ingests the whole MVP stack at once.

This is scaffolding to build *before* writing more shaders: build the eyes, then the
shader work becomes measure → change → measure, objectively.

## Shader-quality discernment (not just anomalies)

`analyze` now includes a `quality` block and `quality` is also its own command:
`noise_rms`, `fireflies_pct`, `banding_index`, `sharpness`, `halo_index`, `contrast`,
`dynamic_range`, with flags `NOISY / FIREFLIES / BANDING / HALOING / BLURRY`.
`temporal PREV CUR` measures flicker/ghosting between consecutive frames.
These localize a defect numerically; the assistant then reads the frame to confirm it
perceptually. Numbers + eyes together = real fidelity discernment.

## Live mode (tune while the game runs — no alt-tab)

With `LiveBridge` wired in (see LIVE_BRIDGE.md), RAW writes `live/latest.json` every
frame, `live/metrics.jsonl` ~10 Hz, `live/frame.bmp` ~1 Hz, and polls `live/control.ini`.

- See the live state at any moment:
  ```
  python tools/eyes/raw_eyes.py watch "<path>/live"
  ```
  (returns the latest metrics, a windowed trend, luma jitter = live flicker, and full
  analyze+quality of the latest frame — which the assistant also opens and reads).
- Tune from outside the game:
  ```
  python tools/eyes/raw_eyes.py control "<path>/live" --off ssgi --set gtao.radius=2.0 --reload
  ```
  RAW applies it next frame. We change one thing, watch the metrics + frame move, iterate.

## Roadmap — toward the "editor's corner"

This is a rudimentary RenderDoc-style analysis bay; the scaffold extends to a live
colorist/DI suite:
1. **Per-pass live capture** — frame.bmp per enabled pass, not just final → live attribution.
2. **A/B + regression** — pin a reference frame; every change reports delta vs reference.
3. **Param sweeps** — `control` drives a parameter across a range; eyes pick the best by metric.
4. **Shader editing loop** — assistant edits `.hlsl` → `--reload` → reads result → iterates,
   all while the game runs. (Editing already works today via files + F12/reload.)
5. **Live dashboard** — `watch --follow` streaming a compact HUD of every pass's health.
