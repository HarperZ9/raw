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

The soak, puddle and snow times were short for the test. With RAW's tone map on (rt5-wx-6), the frames' mean pixel values follow
the game's: dry 88 against 91, rain 70 against 66, snow 98 against 98. One earlier session (rt5-wx-4) stopped
when the engine module did not answer its first `weather` command in 180 s; the rerun passed, and the cause
is not found. The game's cloud report sets the cloud cover.
Image: `weather-e2e-dry-rain-snow-game-vs-raw`.

**Engine lights.** `RAW_EngineLights` joins the decoded light list (`SceneLights.cpp`, `[Lighting]
EngineLights`). In Riverwood its one point light matched the decoded light at 0.0 units, with a colour ratio
of 0.999. Lights that no visible draw carries join unshadowed, and the rays shadow them. The many-light path
is the one that existed: clusters, plus three ray-traced lights a pixel. No ReSTIR is built yet.

**Rain and snow.** `Weather.hlsli` adds rain rings on puddles while rain falls: a world-fixed grid of drops
that tilts the water's normal and bends the reflection lookup. Wet ground keeps 0.75 of its albedo and puddles 0.4. At 0.6 and 0.18
the rain frame's lower half read 23 of 255 against the game's 50. Snow albedo sits near 0.56 with glints that follow the camera. At 0.8, then 0.68, snow clipped to white
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
`tools/lighting/flip.py` is FLIP written from the 2020 paper, checked against NVIDIA's reference since (below, "FLIP checked"). Run at
quarter size (16.75 pixels a degree):

| FLIP against the reference (lower is closer) | RAW improved | Game's model (match) |
|---|---|---|
| Riverwood 13:00 | 0.389 | 0.506 |
| Bannered Mare | 0.317 | 0.497 |

The inn reference ghosts a person who walked during accumulation. The reference is darker than RAW in deep
shadow, and it lacks emissive surfaces and sky radiance beyond the ambient, so it is a reference for this
light model, not ground truth.

**Not done here:** water reflection and caustics, the screen-space scattering blur and the untraced lights'
shadows (all in the next section), POM, async compute, and ENB or Community Shaders comparisons (no such setup
exists). SER and SM 6.9 are skipped, waiting on approval.

## Water, every light, skin scattering, and FLIP checked (2026-11-19)

All at 3840 x 2160 on the RTX 4090 with the game on raw::tx. Every session left ENB at 232 of 232 hashes and
restored the settings files by hash. Images: `ingame-rt6-2026-11-19-*` in the mission-control folder.

**Where the water pass runs.** The game draws its water into a colour target of its own (one RGBA16F target on
the scene depth, about 8 spans a frame in Riverwood), then one draw composites it into the HDR colour and the
water mask (`docs/FRAME_BUFFERS.md`). `BridgeHook.cpp` watches for that composite. When the target list changes
after it, `LightingAtWater` hands the frame to `D3D12Bridge::ExecuteWater` (`BridgeWater.cpp`): the HDR colour is
copied in, RAW's layer target and both water textures go to the queue in COMMON, `RtScene::RecordWater`
(`RtWater.cpp`) runs its own list on the same queue after the frame's trace, and the result is copied back. The
list uses the frame's TLAS, root signature and descriptor heap (33 descriptors of its own at the end), with its
own allocators, constants, fence and timestamps. In a frame without water the same point (the first blended
span) runs the skin scattering alone.

**What it does per water pixel** (`RtWater.hlsl`; `[Lighting] Water`, `WaterReflect`, `WaterGlint`,
`WaterCaustics`, `WaterAbsorb`, `WaterCausticCell`). The layer target gives the surface's depth and its view
normal after the game's wave maps. The water shaders write no alpha, so a water pixel is one with a depth in front
of the bed, a normal, and a normal that faces up. The first version tested the alpha and found no water.
- Reflection: one ray around the mirror direction, kept above the water's plane. On screen the hit takes its
  colour from the frame; off screen, from its own material and the radiance cache (`RtHit.hlsli`); a miss sees the
  sky. It is mixed over the game's water by Schlick Fresnel with F0 0.02.
- Glint: GGX at a width of 0.04 on the wave normal, with a shadow ray to the sun.
- Caustics: a moving net of cell walls at two scales, projected along the refracted sun ray to where it crossed the
  surface, with a shadow ray from the bed, dimmed by the water above the bed and between the bed and the eye. Red,
  green and blue sample the net 0.9 units apart.
- Rain rings from `Weather.hlsli` tilt the normal while rain falls.

Views: `WaterView=1` the reflection, `2` the caustics, `3` Fresnel, `4` the bed under the water (red: none),
`5` Fresnel in three bands. `RtWater.hlsl` and `Sss.hlsl` now hot reload with the other ray-tracing shaders.

| rt6-iso-4, Riverwood river, 13:00 | water list, GPU |
|---|---|
| Skin scattering only (no water in the frame) | 0.13 ms |
| Water | 0.38 ms |
| Water, then skin scattering | 0.50 ms |

The two D3D11 copies of the HDR colour, in and out, are not in these times.

In the river view the bank and the rock now mirror in the water, darker under the rock, where the game's water
shows its own sky. The caustics do not show yet: `WaterView=4` reads red on every water pixel (rt6-wat-4), so the
depth RAW copies at the end of the opaque pass already holds the water's surface, not the bed. The game must write
its water's depth before that point (the depth prepass is the likely place). The caustics, and any absorption by
the water's depth, need the bed's depth captured before the water's depth is written.

Two device removals were found and fixed on the way:
- The scattering wrote into the water's input texture, which had no UAV. With the water alone it never did. DRED
  (`[Translate] Dred=1`, logged once by `LogDeviceRemoved`) reported DXGI_ERROR_INVALID_CALL with no page fault,
  which pointed at an API misuse rather than a bad address.
- A barrier from COMMON on a texture the list had already promoted to a read state. Both colour textures now take
  explicit states.

**Every light shadowed** (`[Lighting] RtRest`, on by default). The trace already gave the sun and each pixel's three
strongest cluster lights their own shadow ray. The other lights of the cluster now share one more ray. On the sky
ray's turn, one pixel in four each frame, one of them is picked in proportion to the light it would give, by
weighted reservoir sampling over the whole list, so the pick is exact rather than resampled. Its visibility rides
in the sky ray's raw output (sky + 2 x visibility + 4), is filtered with the sky rays over pixels and up to 32
frames, and reaches the lighting pass in the bounce texture's alpha (`FLAG_REST`, view 14). The lighting pass
multiplies each untraced light by it. This is the ratio estimator of Heitz et al. 2018: one shadowed-over-unshadowed
ratio for the set. It is ReSTIR's first stage, exact here because a cluster holds at most 128 lights; the temporal
and spatial reuse of reservoirs is not built. Emissive surfaces still light other surfaces only through the one
bounce, which reads them from the frame where a sky ray hits them on screen. The extra ray is traced whether
`RtRest` is on or off (the key only changes what the lighting pass reads), so its cost is not isolated yet: the
inn's trace read 1.79 ms with the key off and 1.77 ms with it on in one run (rt6-tm-1). In the inn most of the
untraced lights' light arrives (view 14 reads near white), with darker patches behind the posts and the hearth.

**Skin.** The hands' and bodies' shader without the tint maps (ps `6a2383091a33f594` in the Riverwood capture:
t0, t1, t2, t11, t12, t14, sampling t2 and t12 as the body shader does) now classes as skin, so the player's arms
take class 10 (view 12). `Sss.hlsl` blurs class-10 pixels after the water: 16 taps on a golden-angle spiral,
sized in world units at the pixel's depth, a Gaussian width per channel (red 1, green 0.42, blue 0.22 of
`SssWidth`), each tap only on skin at the same depth. At `SssWidth=1` the arm's pores washed out; the default is
0.5 at strength 0.75. With the arms in the class, the skin wrap of 0.55 / 0.22 / 0.12 read orange against the
game's brown; it is now 0.30 / 0.12 / 0.06. Skin keeps the game's sun mask instead of the ray-traced sun, because
the actors' own triangles still cast blocky shadows onto their skin in the rays (the dark blocks on the arm in
rt6-wat-3). Skin also takes the game's soft-lighting map at t12, which the body and hands shaders read; without it
the lit arm read darker than the game's.

Found on the way, not caused by this work: an NPC's red hair reads blonde in improved mode, in rt5-ref-1 as well
as now. The hair shader's tint is likely applied after the point where the patch taps the albedo.

**The actor sheet, narrowed.** Three more causes are ruled out:
- the palette: at 39 actor draws the bone constant buffer copied back from the GPU equals the tracker's shadow copy
  in all 240 rows (`[RT] SkinCbCheck=1`);
- the slot: all 178 skinned vertex shaders of the session (dumped with `[RT] DumpSkinVs=1`, kept out of the repo)
  read the current palette from cb10;
- the maths: the player's skinned vertex shader blends four rows of three float4 at row = index x 765.01, with the
  camera taken off each row's translation, which is what `RtSkin.hlsl` does.

The floating corners include vertices with one bone at weight 1 that land thousands of units away, on static
(USAGE_DEFAULT) vertex buffers. The vertex stream as RtSkin reads it is the next suspect: compare the positions
RtSkin reads with the game buffer's bytes at the draw. One actor shader (vs `e183083681d618cc`, stride 16) draws
from a 4 MB dynamic ring buffer at a moving offset, so its vertices may be renamed by the time the trace reads
them.

**FLIP checked against NVIDIA's.** NVlabs/flip at commit d3ab02a4425e (2026-10-13, BSD-3-Clause) is stored with
SOURCE.txt and SHA256SUMS as data; none of its code runs. On its reference.png and test.png at 67 pixels a degree,
`tools/lighting/flip.py` gives a mean of 0.159731 against the 0.159693 its test expects, and 99.98 % of pixels
sit within one 8-bit step of its stored C++ error map (largest gap 0.012). Its filters clamp at the image border.
This tool used reflect padding, which put 13,000 border pixels off, and now clamps too. The earlier reference
scores are unchanged to three places: Riverwood 0.389 against the game's model 0.505, the inn 0.317 against 0.497.

**The tone map default.** Through each tone map, FLIP between the improved lighting (view 13) and the path-traced
reference (`RtDebug=6`, 1,000 frames) at quarter size (rt6-tm-1):

| | the game's tone map | RAW's tone map |
|---|---|---|
| Riverwood 13:00 | 0.511 | 0.522 |
| The Bannered Mare | 0.355 | 0.247 |

The result is mixed: RAW's tone map is much closer in the inn and slightly further in Riverwood, so `Tonemap`
stays off by default. The Riverwood figure under the game's tone map was 0.389 in rt5-ref-1; this run's view
or accumulation differs and the cause is not found.

**Not done:**
- POM. The vanilla parallax shaders (t3 height, about 10 variants in the Riverwood capture) offset their
  coordinates once; a marched POM needs a loop patched into their DXBC before every sample. Terrain has no height
  maps.
- Async compute. Every RAW pass reads what the pass before it wrote in the same frame (depth, G-buffer, trace,
  lighting), and the game's next draws read RAW's output, so moving AO, clouds or fog to a compute queue overlaps
  nothing without a frame of latency or a split of the engine pass's queue. Not measured.
- Water refraction: the game's water still refracts the frame RAW lit, through its own shader.

## The bed's depth, the actors' streams, and the shared ray's cost (2026-11-19, later)

Run rt6-next-1, 2160p. ENB 232 of 232, settings restored by hash.

**The bed under the water.** `BridgeHook.cpp` now copies the game's depth when the four-target opaque set ends
(`D3D12Bridge::CaptureBedDepth`, R24G8 read as R24_UNORM_X8). The game writes its water's depth after that point,
inside the span RAW's end-of-opaque copy follows. The water pass reads this copy as its bed. `WaterView=4` now reads
blue to green over the whole river (a bed at every water pixel), where it read red before. The caustics show on
the shallow bed by the shore (`caustics-shore-game-raw-view-1to1`). Red, green and blue now sample the net 0.35
units apart; at 0.9 units the colour fringes read as a rainbow.

**The actors' streams at the draw against the trace** (`[RT] SkinVbCheck=1`). Each actor draw's vertex and blend
streams are read back at the game's draw, and RtSkin's check skins the floating vertex again on the CPU from
those bytes with the draw's palette:

| Floating vertex | RtSkin on the GPU (camera-relative) | the game's bytes on the CPU (world) |
|---|---|---|
| 1381, bone 10 | 993.7, 2547.7, 1532.2 | 18228.3, -44065.2, 1464.8 |

With the camera near 17222, -46525, -100, the CPU result is about 1006, 2460, 1565 camera-relative, within about
90 units of the GPU's (the camera moves between the draw and the check's log). The other floating vertices agree in
the same way. So RtSkin reads the same streams and palette the game's draw saw, and skins them as the game's
shader does. The floating corners are where these draws' own data puts them. What remains open is which view
these draws are drawn for: the next check compares each actor draw's view-projection (cb12 rows 8 to 11) with the
main camera's, to find draws on the main depth target that belong to another pass.

**The shared shadow ray's cost.** The trace now skips the ray when `RtRest=0` (`GI_REST`). In the inn, off and on
twice in one run: 1.75 and 1.82 ms off, 1.87 and 1.87 ms on, so about 0.09 ms for the ray.

## Refraction, ReSTIR reuse, and the actors' views (2026-11-19, later)

Runs rt6-next-2 to rt6-next-5, 2160p. ENB 232 of 232 after each, settings restored by hash.

**Refraction through the bed's colour** (`[Lighting] WaterRefract`, 0.65; `WaterAbsorb`, now 0.010). When the
game binds its water composite, the HDR colour (the bed as RAW lit it) is copied (`CapturePreWater`). The water
pass reads it at the bed point bent by the wave normal in proportion to the depth (a bent look-up that lands above
the water keeps the straight one), dims it per channel (red 2.2 times green, blue 0.7 times) along the path under
the water, and fills in the water's scatter (its tint under the sky's ambient). The result replaces 0.65 of the
game's water body; Fresnel then mixes in the reflection. At 1.0 the surface read as glass, losing the game's wave
sparkle; at 0.4 and 0.008 the depth tint was faint. `WaterView=6` shows the body alone.

**The water list's cost.** `WaterBed=0` and `1`, twice each in one run (rt6-next-2): 0.608 and 0.612 ms off,
0.628 and 0.631 ms on, so the bed's depth costs about 0.02 ms. The water list reads 0.6 ms in that view in this run;
#33's 0.38 ms was another session and another view, so the difference is between runs, not the bed.

**ReSTIR reuse** (`[Lighting] RtRestReuse`, on). Every lit pixel now keeps a reservoir (the light + 1, W, M, view
depth) in RtSky's two RGBA32F textures, which swap each frame (root parameter 15: u9, u10 and the motion vectors at
t7). After the exact pick, the reservoir at the motion-vector position and two neighbours 4 to 16 pixels from it are
resampled into it: target the unshadowed light, history capped at 20, a neighbour only at the same view depth. On
the sky ray's turn the kept light gets the shadow ray, and p(y) V(y) W over the exact unshadowed sum gives the
share that arrives, sent to the filter in fifteenths. Inn, two runs each way in one session (rt6-next-4): the trace
read 1.91 and 1.93 ms without reuse and 1.74 and 1.75 ms with it; the cause of the drop is not found. The mean
absolute Laplacian of view 14 (pixel-to-pixel noise) fell from 0.997 to 0.948, and its mean from 166.8 to 161.8.

**The actors' views.** Each actor draw's view-projection (vertex cb12 rows 8 to 11) is compared with the frame's
main view (`RtActorView`): every actor draw matched, with a largest difference of 0, so the sheet's draws are not
from another pass. The floating draws' states showed nothing apart: their pixel shaders are RAW's patched variants
(unregistered, so their hash reads 0), and like the rest of the opaque pass they write colour with the depth write
off (the pass tests against the prepass depth). Skinned draws that bind no pixel shader at all (2,283 in the run) now
stay out; the floating count did not change. The skin check now also tests three points along each edge, so a
triangle that runs off the screen into the sky is caught. In the third-person primary-ray view (rt6-next-5) the
player's outline is clean; the tall magenta column above the player is the tree behind them, which carries no
material record. The floating count stays near 2,060 of 32,784 skinned triangles, and the cause is still not found.

**Hair.** The albedo tap now follows a material tint (a cb1 value) multiplied into another register before lighting,
with the destination's channels mapped (ps `6a2383091a33f594`, `18d10e9ba7f68469`). The first version read the
register after the game had reused it and turned an NPC's skin black (rt6-next-4); it now taps right after the tint
when the register is overwritten before lighting (rt6-next-5: skin correct). The NPC's red hair still reads blonde,
so the hair's tint comes some other way; not found.
