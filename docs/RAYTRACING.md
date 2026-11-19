# Ray tracing on the game's own geometry

When the game runs on raw::tx (`[Translate] Enabled=1 Wrap=1`, docs/TX_IN_GAME.md), its vertex and
index buffers are D3D12 buffers on the same device as RAW's passes. RAW builds a ray-tracing scene
from them each frame. It copies nothing and decodes nothing: the BLAS reads the game's buffers in
place. `[Lighting] RtShadows=1` turns it on in improved mode. It is opt-in for now: see Open.

```
D3D11 (the game, on raw::tx)   each indexed lit draw of the opaque set: vertex and index buffer,
                               the position element of the input layout, the world matrix
                               (SceneGeometry.cpp; SceneLights decides what is a lit draw)
end of the opaque pass         raw::tx::PrepareForHost hands the layer's work to the queue, with
                               the G-buffer copies and the new meshes' buffers in COMMON
D3D12, one queue               BLAS for each new (buffer, range), straight from the game's buffers
                               TLAS from the frame's draws and their world matrices
                               RtShadow.hlsl: per pixel, one ray to the sun's disc and one to each of
                               the three strongest point lights of its cluster (inline ray queries)
                               the lighting pass reads the two outputs
```

| File | Role |
|---|---|
| `src/d3d11_proxy/SceneGeometry.cpp` | captures the draws; leaves out alpha-tested shaders (a discard in the bytecode), soft-lit foliage (class 5) and non-indexed draws |
| `src/d3d12_bridge/RtScene.cpp` | the mesh cache, BLAS and TLAS builds, the trace dispatch, timestamps |
| `src/d3d12_bridge/shaders/RtShadow.hlsl` | the rays; light choice by colour luminance x the game's window x n.l |
| `src/d3d12_bridge/shaders/Lighting.hlsl` | `FLAG_RT`: sun shadow = min(the game's mask, RT); the three traced lights take min(their mask channel, RT) and skip the screen-space march |

## Why the minimum with the game's mask

The scene holds rigid opaque geometry only. Skinned actors, alpha-tested leaves and soft-lit
grass are not in it. The game's mask still has their shadows, so taking the darker of the two
keeps them, and the rays add what the mask lacks: shadows beyond the cascades' reach, sharp
contact where the cascades are coarse, and shadows from every point light. The game shadows at
most three or four of those.

## First result (2026-11-18, 3840 x 2160, RTX 4090, the game on raw::tx)

| Scene | instances | triangles in the TLAS | BLAS heap used | builds + TLAS | trace |
|---|---|---|---|---|---|
| Riverwood 13:00 | 954 to 1,870 | 0.59 to 0.92 million | 34 MB | 0.15 to 0.18 ms | 0.47 to 0.75 ms |
| the Bannered Mare | 233 to 321 | 45,000 to 66,000 | 41 MB | 0.05 to 0.21 ms | 0.49 to 0.90 ms |
| Riverwood 23:00 | 1,873 | 0.92 million | 41 MB | 0.18 ms | 0.92 ms |

The ranges cover the steps of one run (txrt-2); the camera moved a little between steps.

Defects found on the way, each a wrong picture before its fix:

- **Everything in shadow.** The UAV table's range carried an offset of 3 on top of a table that
  already started at descriptor 3, so the shader wrote to null descriptors. The lighting pass read
  textures of zeros. Found by showing the visibility itself through the lighting pass's view 2, in a
  running game, by hot reload.
- **Slab shadows under grass.** Soft-lit grass clumps as solid triangles cast black slabs. Foliage
  is now left out.
- **A cache that grew every frame** (4,623 to 127,000 meshes over one run). Some buffers come back
  at new offsets each frame. The cache restarts at 40,000 meshes, or when the BLAS heap nears full.

## How it looks (run txrt-3, Riverwood 13:00, 2160p, foliage left out)

`ingame-rt-2026-11-18-riverwood-{off,on}-2160.png` in the plan folder. The young pine and the
tree trunks now cast contact-hard shadows on the path that the cascades smear. Two faults keep RT
opt-in:

- The sapling's branch cards cast solid card shadows. Its shader has no discard, so its alpha
  reaches the frame some other way, and the rays see whole quads.
- Low-polygon rocks get flat dark facets. The rays shadow the coarse triangles, while the shading
  uses the normal map's detail (the terminator problem). It needs a ray origin offset along the
  geometric normal, or a smooth-terminator correction.

## Open

- Indoors only a few dozen of the inn's draws were captured in the first runs (33 of the lit
  draws SceneLights counts). Most interior geometry still lacks a BLAS. Not found yet: the count
  rose to 321 instances in a later run, and the path that filters the rest is not identified.
- Alpha-tested foliage needs its alpha in the rays (an any-hit test against the game's texture, or
  opacity micromaps), and skinned actors need a per-frame BLAS from their skinned positions.
- The light choice is per pixel. With more than three strong lights in a cluster the rest keep
  the mask or the screen-space march.
- Sky visibility, RT GI with a radiance cache, RT reflections and ReSTIR use this scene next. They
  need the game's materials in the rays (bindless texture access from the hit).
