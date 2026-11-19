# Ray tracing on the game's own geometry

When the game runs on raw::tx (`[Translate] Enabled=1 Wrap=1`, docs/TX_IN_GAME.md), its vertex and
index buffers are D3D12 buffers on the same device as RAW's passes. RAW builds a ray-tracing scene
from them each frame. It copies nothing and decodes nothing: the BLAS reads the game's buffers in
place. Ray-traced shadows are on by default in improved mode on raw::tx (`[Lighting] RtShadows=0` turns
them off). The same scene gives sky visibility and one bounce of indirect light (below).

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
| `src/d3d11_proxy/SceneGeometry.cpp` | captures the draws; alpha-tested ones (a discard, alpha to coverage, soft-lit foliage) with their texture coordinate element and t0 texture; counts every reason a draw stays out |
| `src/d3d11_proxy/GBufferVariants.cpp` | gives each patched G-buffer shader its original's alpha classification (the variant stays bound for later draws) |
| `src/d3d12_bridge/RtScene.cpp` | the mesh cache, BLAS and TLAS builds, the trace dispatch, timestamps |
| `src/d3d12_bridge/RtAlpha.cpp`, `shaders/RtAlphaUv.hlsl` | per alpha-tested mesh: its texture coordinates in a pool, an R8 alpha mask per game texture, made once |
| `src/d3d12_bridge/RtSky.cpp`, `shaders/RtSkyFilter.hlsl` | sky visibility and the bounce: temporal and spatial filter, outputs |
| `src/d3d12_bridge/shaders/RtShadow.hlsl` | the rays: sun, three lights, one sky ray; alpha test at candidate hits; facet correction |
| `src/d3d12_bridge/shaders/Lighting.hlsl` | `FLAG_RT`: sun shadow = min(the game's mask, RT) outdoors; the three traced lights take min(their mask channel, RT); `FLAG_SKY`, `FLAG_BOUNCE` on the exterior ambient; views 2 (sun), 4 (sky), 5 (bounce) |

## Alpha-tested geometry

Leaves, branch cards, grass clumps and clover are quads whose texture's alpha cuts the shape. As
solid triangles they cast card and slab shadows. Each alpha-tested draw is non-opaque in the scene.
When its mesh is first seen, a compute pass expands its texture coordinates, three per triangle, into
a pool, and another samples the alpha of the draw's t0 texture into an R8 mask of at most 512 x 512
(one per game texture, from the mip whose size matches). At a candidate hit the ray query finds the
mesh's record from the instance ID, interpolates the coordinates with the hit's barycentrics, reads
the mask (bilinear, wrapping) and commits the hit at alpha 0.5 or more (`RtAlphaRef`). After that
first frame the rays read nothing of the game's.

Found on the way: the G-buffer patch binds a patched copy of a lit shader and the game, which binds a
shader only when it changes, draws its next meshes with that copy still bound. The capture looked the
copy up, found nothing, and took the sapling (its shader does discard) for opaque. The copy now gets
the original's classification.

## Facets

A low-polygon rock shaded with a smooth normal map faces the sun where its triangles have already
turned away, and the next triangle shadows it in flat dark patches. The ray origin now moves along
the geometric normal reconstructed from the depth buffer, and where that normal turns from the sun
(n.l under 0.35) the sun ray starts up to `RtTermDist` (48 units) further out. Occluders nearer than
that are still in the game's mask, which the lighting pass takes the minimum with.

## Interiors

Interiors have no cascades; their directional light is the cell's fill, which the game leaves
unshadowed. The rays cut it into shafts from the windows and darkened the room. The sun ray now runs
outdoors only.

## Sky visibility and one bounce

One extra ray a pixel, cosine-weighted around the surface normal, on a checkerboard that alternates
every frame. A ray below the horizon counts as seeing: the game's ambient carries the ground term
itself. A ray that travels `RtSkyLength` (6000 units) sees sky. A ray that hits geometry finds a
surface the game has already lit this frame: where that point is on screen and not hidden, its colour
in the game's frame is the light it sends back, otherwise a quarter of the cell's ambient stands in.
`RtSkyFilter.hlsl` accumulates both over up to 32 frames along the game's motion vectors, each history
tap accepted only at the pixel's own view depth, with a depth-aware 3 x 3 mean (5 x 5 while the
history is short). Outdoors the lighting pass scales the ambient by the visibility (`RtSkyStrength`)
and adds the bounce (`RtBounce`).

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
