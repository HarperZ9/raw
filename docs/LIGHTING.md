# Lighting: the engine lights the game frame

With `[D3D12Bridge] Pass=lighting` the Playground engine lights Skyrim's frame itself. At the end
of the game's opaque pass the bridge hands the engine the G-buffer and the frame's decoded lights;
the engine writes its lit result into the game's HDR scene colour in place of what the game's lit
shader wrote. Water, effects, the game's AO, fog, bloom, tone map, anti-aliasing and the HUD all
run on it afterwards. `[Lighting] Mode` (or F10 in game) switches between the game's lighting (0),
ours matched to the game (1) and ours improved (2).

```
D3D11  opaque pass ends      depth, normals, motion, RAW albedo + specular, shadow mask,
                             sun cascades (one compute copy into an R32F atlas) -> shared
       the proxy             lights, sun, ambient, cascade matrices, camera: SceneLights
D3D12  the engine's RHI      [AO, improved] -> lighting (clustered) -> fog -> colour out
D3D11                        colour out -> the game's HDR scene colour; the frame goes on
```

## Files

| File | Role |
|---|---|
| `src/d3d11_proxy/SceneLights.cpp` | decodes the lights at the game's own lit draws (below) |
| `src/d3d11_proxy/BridgeHookLighting.cpp`, `BridgeHookLightingStats.cpp`, `LightingSchedule.cpp` | the lighting form of the bridge hook: config, capture steps, match statistics, coverage check |
| `src/d3d12_bridge/BridgeLighting.cpp` | the surface targets, shadow mask and cascade atlas cross to D3D12 |
| `src/d3d12_bridge/LightingPass.cpp`, `LightClusters.cpp` | the pass on the engine's RHI; clusters built on the CPU each frame |
| `src/d3d12_bridge/shaders/Lighting.hlsl`, `LightFog.hlsl`, `LightingCommon.hlsli`, `ShadowAtlas.hlsl` | the shaders |
| `tools/lighting/` | the same model in numpy on a dumped frame (`[Lighting] Dump=1`), term by term against the game |
| `tools/harness/` | the unattended runs that produced the numbers below (docs/HARNESS.md) |

The pass binds a uniform and eight textures in one draw; superframe's D3D12 backend took 8
bindings, so superframe PR 130 raises the raster limit to 16.

## What the game hands its shaders

Read from the disassembly of the game's lit pixel shader and checked on captured frames
(`tools/frame_buffers/lights.py`):

| Where | What |
|---|---|
| pixel buffer 2 of a lit draw (30 vectors) | `[0]` sun direction, `[1]` sun colour, `[2]` the shadow-mask channel of each shadowed light, `[11..13]` the directional ambient (`colour = dot(a.xyz, n) + a.w`), `[15+i]` light position and radius, `[22+i]` its colour, `[29]` lights and shadowed lights; all in model space |
| vertex buffer 2 of a lit draw | the world matrix, translation relative to the camera |
| pixel slot 14 of a lit draw | the shadow mask: sun in x, up to three shadowed point lights in y, z, w |
| pixel buffer 2 of the shadow-mask draw (25 vectors) | the cascade matrices and their bias |
| vertex buffer 12 at the end of the opaque pass | the camera: clip to world, world to clip, the view rotation |

Found while building it, each a wrong result before the fix:

- **Bound buffers** come from the context at each draw. The game binds some through paths the
  wrapped `PSSetConstantBuffers` never sees: decoding from the wrapped calls saw 1 lit draw a frame
  instead of about 1,950.
- **Only the opaque set's draws count.** The reflection cube's faces draw lit too, from their own
  camera; the first decoded camera was a cube face's and every pixel landed in the wrong place.
- **Copies of a light at a wrong position.** About 15 of 17 lights outdoors and 90 of 105 indoors
  are a real light's colour seen by a few draws whose world matrix does not explain their space
  (skinned and multi-part meshes). A light seen by fewer than 10 draws whose colour matches a light
  seen by eight times as many is dropped. Before the filter the interior was 1.8 times too bright.
- **Depth read zero indoors.** At the end of the opaque pass the depth-stencil view is still
  bound; indoors it is writable, so D3D11 refused the copy's shader view and the shared depth held
  zeros (every pixel 15 units from the camera). The hook now releases the opaque set first. The
  real-time AO pass reads depth through the same copy and was not rechecked indoors.
- **Interiors have no cascade draw**, so the shadow mask comes from slot 14 of the lit draws, and
  shadowed lights there use channel 0 too.

## The game's shading, and the match model

The game's lit shader (`Lighting.hlsl`, match mode): Lambert diffuse from the sun and up to seven
lights with the window `1 - (d / r)^2`, an unnormalised Blinn-Phong lobe `pow(n.h, s)` with no
cosine term, the directional ambient, albedo times the diffuse sum, the lobe times the normal
map's specular mask and the material's specular colour, and a soft knee
`min(x, (1 - k) x + 1)` (k = 0.8333 from pixel buffer 0) before and after the specular term.

What the G-buffer patch needed for it (`src/core/dxbc/`):

- **Vertex colour.** The lit result is multiplied by the vertex colour; the albedo target now
  carries it. Found by two signatures (the output alpha times the input's w, or the multiply right
  after albedo times light). 35 of 50 lit shaders in Riverwood, 23 of 26 indoors. Without it the
  parallax class was 41 dE76 off.
- **Which shaders have a lobe.** Gloss 0 means the shader has no exponent and no lobe. Landscape
  shaders have none (slot 1 is a colour layer there) and the exponent found in parallax shaders
  belongs to another term; a lobe on either doubles their error.

Not in the G-buffer, so approximated, each named where it is used:

- the material's specular colour: the frame's median (1.0 in every scene measured)
- the soft-lighting texture of foliage: a wrap term with a constant 0.15 fitted on one Riverwood
  frame (`tools/lighting/fitsoft.py`)
- distant LOD: lit per vertex by the game, so it keeps the game's colour
- per-object light lists: the game gives each object up to seven lights; a deferred pass lights
  every pixel in a light's radius

## Match: ours against the game's own lit buffer

The game's lit HDR colour and ours, from the same frame, on every pixel we light (sky, water,
effects, first person and distant LOD excluded). `rel` is the mean absolute difference over the
mean of the game's colour; dE76 is CIELAB after a display map x / (1 + x). Harness run,
2160p, the world's clock stopped (`tfc 1`) so every step sees the same frame.

| Scene (2160p) | ours matched: rel | mean dE76 | dE < 2 | control, game's sun mask: dE76 | half-viewport control |
|---|---|---|---|---|---|
| Riverwood, 13:00, clear | 0.097 | 2.53 | 65.9 % | 2.23 | fails coverage, as it must |
| Riverwood, turned 90 degrees | 0.105 | 2.78 | 63.3 % | | |
| Riverwood, camera moving | 0.087 | 2.01 | 75.4 % | | |
| The Bannered Mare, by the door | 0.007 | 0.21 | 99.4 % | 2.08 | fails |
| The Bannered Mare, at the counter (earlier run, same code) | 0.237 | 3.19 | 59.4 % | 5.40 | fails |
| Riverwood, 23:00, clear (moonlight) | 0.114 | 3.10 | 60.6 % | 2.89 | fails |

At 1080p the same test gave Riverwood 2.51, the inn door 0.22 and the walk 1.59. Its night row is
not counted: the cell change after the inn did not happen, so that frame is the inn.

The match is closest on plain lit surfaces: on the lit-shader class with a lobe, 91.5 % of pixels
are under dE 2 in a Riverwood dump (`tools/lighting/diag.py`). The rest of the difference sits in
foliage (the soft-lighting texture), in our 3 x 3 filter on the cascades against the game's mask,
and indoors where the game's per-object light lists leave a light off an object we light.

**Rerun after the harness fixes (2026-11-18, later), both resolutions complete, quit by `qqq`:**

| Scene | 2160p dE76 | 1080p dE76 |
|---|---|---|
| Bannered Mare, by the fire | 2.95 | 3.21 |
| Riverwood 23:00 | 2.86 | 2.93 |
| Riverwood 13:00 | 2.54 | 2.56 |
| Riverwood, turned | 2.80 | 2.99 |
| Riverwood, walking | 2.52 | 2.80 |

The match statistics compare buffers at the end of the opaque pass, so the composite switch below
does not move them; it shows in the final frames. Full-size before-and-after pairs per improvement:
`ingame-lighting-2026-11-18-<scene>-<improvement>-before-after-1to1-2160.jpg` in the plan folder.
In them "before" carries the game's AO and fog and "after" ours in their place.

Controls in every scene: `match_game_mask` uses the game's own sun mask instead of our cascade
lookup (the share of the difference our shadow filtering causes); `control_half_viewport` draws
only the top-left quarter and must fail the coverage check, and does.

**Coverage check.** For every lit step the pass must change the output in all four quadrants,
each at least half as often as the busiest. Every lit step passed; the half-viewport control
failed in every scene.

## Ours replaces the game's

After the opaque pass the game draws one full-screen composite into its HDR colour (pixel shader
with an 11-vector buffer 2, read from its disassembly): the scene copy plus reflections, times its
AO texture (t1), then its distance fog, `lerp(colour, fog colour, f)` with
`f = min(pow(saturate(z c0.y - c0.x), c0.z), c0.w)`. With `Ao` on in mode 2 the proxy binds a white
texture at t1 for that one draw; with `VolumetricFog` on it binds a copy of buffer 2 with
`c0.w = 0`. The game's own bindings go back right after the draw (`src/d3d11_proxy/GameComposite.cpp`).
The game's reflections and its volumetric light shafts stay. Matched mode leaves the composite as
the game drew it.

## Improvements, each behind its own switch

| Switch | What it does |
|---|---|
| `SoftShadows` | every point light (four a pixel) gets a screen-space shadow marched toward it through the depth buffer, with a penumbra from the ratio of clearance to distance; on top of the game's shadow-mask channel where the light has one |
| `GgxSpecular` | GGX with height-correlated Smith masking and Schlick Fresnel in place of the game's lobe; diffuse scaled by 1 - F; F0 from the specular mask, 0.04 where the game had no lobe |
| `VolumetricFog` | fog lit by the sun (12 jittered steps, each shadowed by the cascades) and by every light (the integral of the game's window along the ray in closed form) |
| `ContactShadows` | a 12-step march toward the sun over 40 units |
| `Ao` | the engine's real-time AO (half-resolution GTAO, accumulated along the motion vectors) on the ambient term |

Each improvement alone, from the same frame as the improved base (which equals the match), at
2160p, and the cost it adds:

| Improvement | Riverwood day: dE76 against the game | Inn (counter view) | Added GPU time, Riverwood |
|---|---|---|---|
| none (base) | 2.53 | 3.19 | |
| soft shadows | 2.53 (no point light in view) | 2.28 | +0.01 ms (+0.44 ms indoors) |
| GGX specular | 2.79 | 3.73 | +0.01 ms |
| volumetric fog | 3.44 | 3.22 | +0.42 ms |
| contact shadows | 3.27 | 3.29 | +0.15 ms |
| real-time AO on ambient | 3.88 | 3.47 | +0.75 ms |
| all five | 5.93 | 3.23 | +1.29 ms |

dE here is distance from the game's look, so a larger number is a bigger change, not a better or
worse picture. Soft shadows indoors moved the frame closer to the game: the game leaves lights off
objects they would otherwise reach, and the shadows block some of that light too. Pictures, before
and after per improvement: `ingame-lighting-2026-11-18-*-improvements-before-after-2160.jpg` in the
plan folder.

Where ours looks worse than the game, from the pictures:

- Dry bushes lose the game's warm, back-lit orange: the soft-lighting texture is not in the
  G-buffer and the fitted wrap constant is grey.
- Ground ivy and young leaves come out brighter and greener than the game's.
- At night the third-person player model is darker than the game draws it.
- Improved: the frame is darker overall, because the game's own AO and fog still run after ours;
  the fog adds haze on the game's haze in the distance.

## Cost

GPU timestamps on the D3D12 queue, mean over the frames of each step, RTX 4090, the game at
3840 x 2160. Before the run the GPU showed 13 % load and nvidia-smi listed only desktop processes
(no other GPU job); the game itself was running.

| Pass | Riverwood day | Riverwood night | Inn |
|---|---|---|---|
| matched: lighting + fog copy | 0.23 + 0.09 = 0.32 ms | 0.24 + 0.10 = 0.39 ms | 0.21 + 0.06 = 0.32 ms |
| improved: AO + lighting + fog | 0.74 + 0.35 + 0.50 = 1.59 ms | 0.75 + 0.36 + 0.48 = 1.59 ms | 0.72 + 0.45 + 0.14 = 1.31 ms |
| bridge (copies and the fence round trip) | 0.68 ms | 0.89 ms | 0.76 to 2.7 ms |

At 1080p: matched 0.07 ms, improved 0.43 ms in Riverwood. The bridge's cost is mostly the fence
round trip on the D3D11 queue; indoors it reached 2.7 ms in some steps.

**The indoor fence cost, what was tested.** In the inn the D3D11 queue sits idle 2.8 to 6.3 ms at
the hand-over while our pass takes 0.4 to 2.2 ms: 2.4 to 4.1 ms of "fence sync", against 0.33 to
0.56 ms outdoors. First hypothesis: the GPU reaches the hand-over before the CPU has submitted the
D3D12 list, so it waits on the CPU. The bridge now logs, every 300 frames, whether the copy-in had
already executed on the GPU at the moment of submit, and the CPU time from the D3D11 flush to the
submit. Result: 0 or 1 of 300 frames indoors and out, and 50 to 66 microseconds. Rejected. The
extra time is on the GPU side, between the D3D11 signal and the end of the D3D12 list. Not found
yet; the next measurement places the D3D12 list's start on a common clock with the D3D11
timestamps (`ID3D12CommandQueue::GetClockCalibration` against QPC samples taken beside D3D11
timestamp queries).

## Limits

- Foliage's soft-lighting texture, the specular colour per pixel, glow maps and emissive light are
  not in the G-buffer (the emissive target was empty in every scene).
- First-person models keep the game's lighting: their normals do not exist in the pixel shader.
- The light-copy filter is a rule on sighting counts: a real light seen by few draws can be
  dropped when another light shares its colour.
- One saved state, three views and one walk; rain, snow, caves and other cells may bind lights the
  decode has not seen.
