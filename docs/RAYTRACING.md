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

## Agility SDK and opacity micromaps (2026-11-19)

On the Agility SDK 1.620.1 runtime (docs/TX_IN_GAME.md), the RTX 4090 reports shader model 6.9 and
DXR tier 1.2, with opacity micromaps and shader execution reordering (and reordering that reorders). On the
system runtime it reports 6.8 and 1.2.

**Opacity micromaps** (`RtOmm`, `RtOmmBake.hlsl`, `[Lighting] RtOmm=1`). Each new alpha-tested mesh gets
a level-4, 4-state micromap array: 256 micro-triangles per triangle, each classified from its alpha mask
at its corners and centre (opaque, transparent, or unknown, which the ray query resolves as before), and
built in the same frame as the BLAS that links to it. The order in which a micromap lists its
micro-triangles is a space-filling curve that the DXR specification only draws; `tests/omm_check
--derive` reads it from the GPU into `OmmTable.h`, and the plain `omm_check` places a random pattern
with that table and confirms every ray in both the 2-state and 4-state packings, levels 1 to 5
(1,364 micro-triangles each way). A closed-form curve written from memory matched only at levels 1 and 2.

| Riverwood 13:00, 3840 x 2160, one run each | trace (sun, 3 lights, sky, bounce, reflections) |
|---|---|
| system runtime (rt2-p1) | 2.26 ms |
| Agility runtime, no micromaps (rt2-ag-1) | 1.84 ms |
| Agility runtime, micromaps (rt2-omm-1, 80 arrays) | 1.64 ms |

The sun-visibility view is the same with and without micromaps (leaf patterns match; the remaining pixel
differences follow the camera and the ray jitter). Single runs: the spread between runs is not measured.

**Cell changes.** After a loading screen the mesh cache is dropped (more than 0.5 s without a world
frame). Before this the night view, reached after the inn, traced in 7.95 ms and read mean dE76 12.6
(stale shapes from the previous cell); after it, 1.70 ms and 5.75.

Not done: shader execution reordering needs the traces moved from inline ray queries in compute to a
ray-generation pipeline (or shader model 6.9 `MaybeReorderThread` where it applies to compute); shader
model 6.9 long and cooperative vectors are not used yet.

## Materials and the radiance cache (2026-11-18)

A sky ray or a reflection ray that lands off screen used to get a flat guess (a quarter of the cell's
ambient, or nothing for reflections). Now every mesh keeps its material for the rays (`RtAlpha`,
`RtAlphaTex.cpp`, `RtAlphaUv.hlsl`): its texture coordinates, one face normal per triangle, and an RGBA8
copy of its colour texture (t0 of the lit shaders) at up to `RtMatSide` (128) texels a side. An
off-screen hit is shaded as the game shades a surface (`RtHit.hlsli`): its colour times the sun with a
shadow ray, every point light in reach, and the directional ambient, scaled by the sky share and lifted
by the bounce that a world-space radiance cache (`RtCache`, `RtCache.hlsl`) keeps for the hit's cell.

The cache is a hash table of 2^20 cells: cubes of the world `RtCacheCell` (16) units across near the
camera and doubling with distance, one set per facing axis, keyed by absolute position (the per-frame
constants' float4 40 is the camera's world position: 17225, -47204, -8 at Riverwood). Each visible
pixel's sky ray adds a sample to its own cell, and each off-screen hit traces one more ray for the hit's
cell, so cells behind the camera fill and light that bounced more than once reaches the pixels. A cell
keeps the running mean of up to 32 frames and is freed after 600 frames without a sample. Interiors now
take the bounce too, at `RtBounceInterior` (0.5) on top of the cell's fill.

| Riverwood 13:00, 3840 x 2160, run rt3-gi-3 | trace | cache resolve |
|---|---|---|
| materials off (as before) | 2.07 ms | |
| materials on, cache off | 2.22 ms | |
| materials and cache on | 2.47 ms | 0.04 ms (139,000 live cells) |
| the Bannered Mare, materials off / on | 1.46 / 1.74 ms | 0.02 ms |

Riverwood holds 795 material meshes, 156 colour copies (8.4 MB) and 11.5 MB of coordinates. The
same configuration traced in 1.50 ms in an earlier run (rt3-gi-1) and the previous work's figure was
1.64 ms; the spread between runs is not measured, so the deltas within one run are the figures to read.
Found on the way: the debug views in the main kernel raised the trace from 2.5 to 7.9 ms (register
pressure); they now build into a second pipeline used only while `RtDebug` is set.

Debug views (`View=7`): `RtDebug=3` the colour copies on the primary ray (an unlit scene: the copies are
right), `RtDebug=2` the light the cache gives each pixel (cells show as 16-unit blocks near the camera),
`RtDebug=4` a mirror for every pixel showing only off-screen hits, `RtDebug=5` the whole mirror. Switching
`RtMaterials` live drops and rebuilds every mesh.

Not done: the visible change in a daylit exterior is small (most bounce rays land on screen, where the
game's frame is already the right answer); point lights at off-screen hits are unshadowed; meshes over
the 40,000-record limit are shaded grey.

## Skinned meshes and trees (2026-11-18)

`RtSkin` builds a BLAS each frame for every skinned draw of the opaque set: `RtSkin.hlsl` blends the
bind-pose positions by up to four bones of the draw's palette (vertex constant buffer 10, 240 float4,
row = blend index unorm x 765) and takes off the camera's place (the bones are in world space; the
first run, which took them as camera-relative, found nothing on screen). Riverwood: 60 draws, 26,277
vertices, 0.21 ms of builds; the trace does not change measurably within a run. What it brought in
first is the trees: their trunks and branches are skinned (wind bones), which is why the old capture
saw "coordinates in a second stream with vertex-shader motion". They now show in the primary-ray view
(magenta: no material record) and shadow through the rays. Alpha-tested skinned draws (leaves) stay out.

Not done: people and creatures. In Riverwood 68,412 skinned draws joined over the run and 789,071 were
drawn outside the opaque set (shadow maps and, from the third-person test, the actors themselves: the
player's body is missing from the primary-ray view). They need a capture by the main depth target and
one frame of latency. `RtSkinBones=9` gave the same picture as 10 (the trees barely move between frames).

## Rain on surfaces (2026-11-18)

`Weather.hlsli`, shared by the lighting pass and the trace: `[Lighting] Wetness` and `Puddles` (0 to 1,
off by default until the game's weather drives them). Wet surfaces darken (albedo x 0.6 at full) and
turn glossy (GGX width 0.12); only where the ray-traced sky visibility says rain reaches. Puddles pool
on ground whose geometric normal (from depth, not the normal map) is flat, where a world-fixed noise
field rises above the puddle level; they are flat mirrors that take a ray-traced reflection. The first
version gated puddles by the shading normal and found none on the cobbled path. Images:
`ingame-rain-2026-11-18-riverwood-{dry,soaked,puddles}-2160.png`.

## Timing between runs

Identical settings traced in 1.7 to 2.5 ms in some sessions and 5.6 to 7.9 ms in others (rt3-skin-1,
rt3-wet-2), and the cause is not found. It is not the code under test: within each run the dry and
off-switch baselines move with it. Compare the deltas within one run.

## Weather, clouds, tone map, skin and a reference (2026-11-19)

All at 3840 x 2160 on the RTX 4090 with the game on raw::tx; every session left ENB at 232 of 232 hashes and
restored the settings files by hash. Images: `ingame-rt5-2026-11-19-*` in the mission-control folder.

**The sheet beside the player.** `RtSkinCheck.hlsl` runs after the skinning. It copies each skinned draw's
indices into RAW's own 32-bit pool and makes a triangle degenerate when either check fails:
- stretched: an edge longer than `RtSkinRatio` (4) times its bind length and `RtSkinMin` (24) units;
- floating: a corner that lands on screen in front of the nearest surface the game drew within 5 x 5 pixels,
  by more than 8 units plus 2 % of its distance.

The check logs its counts every 120 frames, worst draws first, with one bad corner in full. In third person
at Riverwood (rt5-sc-6) about 2,200 of 89,000 skinned triangles are stretched and 5,700 floating. The sheet
is gone from the primary-ray view, and the body keeps its outline (`actors-sheet-before-after-game`).

The cause is not found. The bad corners sit 200 to 500 units in front of the game's surface on the player's
body draw (1,896 vertices, float positions, stride 40). Their weights sum to 1 and their indices are in range;
corners on bones 34 and 39 go wrong while bones 2, 3 and 18 place right. Three causes are ruled out:
- the bone slot: palettes from slots 9 and 10 give the same counts, and slot 8 gives three times as many;
- an index past the vertex count: none found;
- the ratio test: at 1.2 times the bind length it flags 43 % of all triangles, so skinned edges grow past
  their bind lengths in normal poses and that test cannot be tightened.

Off-screen bad corners are not caught, so a shadow cast by one stays.

**Weather end to end** (rt5-wx-3, test `rtweather2`). The engine module reads `Sky` each present and
reports to the proxy, and `WeatherDrive` drives the surfaces. `fw 10a241` is a rain weather (flags 4) and
`fw c8221` a snow weather (flags 8); both were read back in game, with the candidates in `wxfind`.

The surfaces followed the game's weather:

| Stage | Wetness | Puddles | Snow |
|---|---|---|---|
| Dry | 0 | 0 | 0 |
| Rain | 0.96 | 0.48 | 0 |
| Rain, later | 1 | 0.88 | 0 |
| Snow | 0.90 (slowly drying) | | 1 |

The soak, puddle and snow times were short for the test. The game's cloud report sets the cloud cover.
Image: `weather-e2e-dry-rain-snow-game-vs-raw`.

**Engine lights.** `RAW_EngineLights` joins the decoded light list (`SceneLights.cpp`, `[Lighting]
EngineLights`). In Riverwood its one point light matched the decoded light at 0.0 units, with a colour ratio
of 0.999. Lights that no visible draw carries join unshadowed, and the rays shadow them. The many-light path
is the one that existed: clusters, plus three ray-traced lights a pixel. No ReSTIR is built yet.

**Rain and snow.** `Weather.hlsli` adds rain rings on puddles while rain falls: a world-fixed grid of drops
that tilts the water's normal and bends the reflection lookup. Puddles hide more of the ground (albedo times
0.18). Snow albedo sits near 0.56 with glints that follow the camera. At 0.8, then 0.68, snow clipped to white
in the game's tone map. In full sun it still reads near white without RAW's tone map.

**Clouds** (`Clouds.hlsl`, `CloudCommon.hlsli`, `[Lighting] Clouds`, `CloudCover` and related keys). One
layer of world height 36,000 to 50,000 units is marched at quarter size: 40 steps, 4 sun steps, three
multiple-scattering octaves and a two-lobe phase, with temporal history found by the ray's direction. It
composites on the sky pixels, which this pass owns (view 10). The same density field gives three more things:
- the clouds' shadow on the ground (SKY-02, view 11);
- one sample every third step of the fog's sun march, so shafts form between clouds (SKY-04);
- a fading shadow as the weather's cloud cover rises, because an overcast weather's sun is already dimmed
  by the game.

| rt5-cl-3, Riverwood 11:00 | Lighting stage | Fog stage |
|---|---|---|
| Clouds off | 0.58 ms | 0.50 ms |
| Default cover (0.35) | 0.98 ms | 0.70 ms |
| Cover 0.75 | 1.05 ms | 0.70 ms |

**Tone map and exposure** (`Tonemap.h`, `TonemapPs.h`, `[Lighting] Tonemap=1`, off by default). The game's
tone map shader (ps `99d22a3d198a3b69`, found in a frame capture) is replaced by hash with RAW's, which keeps
the same bindings. The replica control, the game's exposure and curve through RAW's shader, matches the game
frame's channel means to 0.03 of 255; per-pixel differences follow wind and anti-aliasing.

In camera mode the metered average maps to a key of 0.4, with a floor of 0.3. In Riverwood at 13:00 the
game's adapted average and its target both read 0.46. A filmic curve (the fitted ACES curve, Narkowicz 2015)
follows, then the game's imagespace grade, applied as the game applies it. Riverwood mean pixel values:

| View | Game | RAW (improved lighting and tone map) |
|---|---|---|
| Day | 98.6 | 87.6 |
| Night | 39.5 | 30.0 |

**Skin.** Lit shaders that read 2D maps at t3 and t4 are material class 10. The face shader is one of them
(ps `c49dc9fe5ca9dd4c`); hands and body still class as soft-lit. Skin gets a red-weighted wrap past the
terminator, a reddened shadow edge and skin's own specular (view 12 colours the classes). There is no
screen-space scattering blur yet.

**A path-traced reference** (`RtDebug=6`, view 7). It renders the light each pixel receives:
- the sun and one point light, by next-event estimation with shadow rays;
- three diffuse bounces on the meshes' colour copies;
- the game's directional ambient as the sky;
- no radiance cache and no screen lookups.

Samples accumulate over 1,000 held frames. View 13 gives the same quantity from the lighting pass.
`tools/lighting/flip.py` is FLIP written from the 2020 paper; it is not checked against NVIDIA's code. Run at
quarter size (16.75 pixels a degree):

| FLIP against the reference (lower is closer) | RAW improved | Game's model (match) |
|---|---|---|
| Riverwood 13:00 | 0.389 | 0.506 |
| Bannered Mare | 0.317 | 0.497 |

The inn reference ghosts a person who walked during accumulation. The reference is darker than RAW in deep
shadow, and it lacks emissive surfaces and sky radiance beyond the ambient, so it is a reference for this
light model, not ground truth.

**Not done:** water reflection, refraction and caustics, because water is drawn after this pass. Also not
done: POM, the screen-space scattering blur, async compute, ReSTIR, and ENB or Community Shaders comparisons
(no such setup exists). SER and SM 6.9 are skipped, waiting on approval.
