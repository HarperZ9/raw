# Frame buffers: everything the game writes in one frame

RAW reads the game's frame from the game's own buffers. This page lists every buffer Skyrim
Special Edition 1.6.1170 wrote in one gameplay frame, what each holds, how to decode it, which
ones reach the D3D12 device, and which pixels of the frame have no surface data in any buffer.

The numbers come from one captured frame: Riverwood exterior in daylight, 3840 x 2160, vanilla
content, temporal anti-aliasing, the game's ambient occlusion, reflections and volumetric
lighting on, two shadow cascades at 4096. Another scene or another settings file changes the
counts. The rules that name the buffers do not depend on this scene.

## How a frame is captured

`[FrameLedger]` in `d3d11_proxy.ini` arms the proxy for one frame (`src/d3d11_proxy/FrameLedger.*`).
For that frame the proxy records every span between two render-target binds: the targets, the
depth-stencil, each draw with its shaders and output state, each clear, copy and compute
dispatch. It copies every target out after its last write, and every full-size target after
every span. It also writes the constant buffers of each draw and the bytecode of each pixel
shader a draw used.

```ini
[FrameLedger]
Enabled=1
OutDir=D:\capture          ; absolute, outside the game folder
AfterGameplayFrames=260    ; frames in a row with more than MinDraws draws
History=1                  ; keep full-size targets after every span (the coverage report needs it)
```

The tools in `tools/frame_buffers/` read a capture:

| Tool | Output |
|---|---|
| `inventory.py <capture> <out>` | the table below as `buffers.md` and `buffers.json`, and a PNG of every buffer |
| `check_decode.py <capture>` | the decode checks, with controls |
| `coverage.py <capture>` | the coverage report and a class map |
| `shaders.py <capture>` | which outputs the frame's pixel shaders write, per kind of draw span |

Captured images show game content, so they stay out of this repository. The image names in the
table are the files `inventory.py` writes.

## The frame, in order

| Step | Draws | What happens |
|---|---|---|
| Reflection cube | 316 | Some faces of a 512 cube map are redrawn with their own depth. |
| Shadow maps | 3,007 | A two-layer 512 depth array, then two 4096 cascades. Depth only. |
| Depth prepass | 2,013 | The scene into the depth buffer. The game copies the depth buffer right after. |
| Shadow mask | 1 | One full-screen draw resolves the sun's shadow into an 8-bit target. |
| Opaque pass | 2,190 | Four targets at once: HDR colour, motion vectors, normals with masks, a two-channel mask. |
| Normals and motion only | 18 | One shader writes normals and motion with depth writes, no colour. |
| The game's reflections and AO | 19 | Half-size normals, reflection trace and filters, a view-depth pyramid, AO and two blurs, one composite into the HDR colour. |
| Water | 22 | A water colour target, then one draw into the HDR colour and a water mask. |
| Blended draws | 295 | Clouds, mist, effects: HDR colour plus a mask and a second colour target, depth read-only for most. |
| Late draws | 6 | Draws that bind the prepass depth copy as their depth buffer. |
| Volumetric lighting | compute | Two 3D volumes written by compute dispatches and resolved to the screen (98 dispatches in the frame, reflections filters included). |
| Bloom, exposure, tone map, anti-aliasing | 37 | Downsample chain, luminance chain, tone map to 8 bits, temporal resolve. |
| Final blit, then HUD | 1 + 16 | The frame goes to the backbuffer; the HUD draws on top. |

RAW's pass runs between the last two: after the final blit, before the HUD.

## The buffers

55 textures were written in the frame. All 55 were captured. 53 reach the D3D12 device: depth,
normals and the frame cross every frame, and 50 more cross on demand, each one read back on
D3D12 and compared with its D3D11 bytes in this capture. The two 3D volumes do not cross yet.

| # | Buffer | Size | Format | Written by | Shared with D3D12 | What it holds | Image |
|---|---|---|---|---|---|---|---|
| 1 | Reflection cube map (t3) | 512x512, 6 layers | R16G16B16A16_FLOAT | 316 draws | on demand; D3D12 read equals D3D11 | Six HDR faces; the game refreshes some faces each frame. | `03_reflection_cube_0_rgb.png` (+10) |
| 2 | Reflection cube depth (t4) | 512x512 | R24G8_TYPELESS | 316 draws | on demand; D3D12 read equals D3D11 (as R32F depth) | Depth and stencil for the cube map faces. | `04_reflection_cube_depth_depth.png` (+1) |
| 3 | Second shadow map set (t5) | 512x512, 2 layers | R16_TYPELESS | 254 draws | on demand; D3D12 read equals D3D11 | 16-bit depth array at low resolution, drawn before the cascades. | `05_shadow_maps_small_0_depth.png` (+1) |
| 4 | Sun shadow cascades (t6) | 4096x4096, 2 layers | R16_TYPELESS | 2753 draws | on demand; D3D12 read equals D3D11 | 16-bit depth, one array layer per cascade, drawn before the scene. | `06_shadow_cascades_0_depth.png` (+1) |
| 5 | Volumetric lighting volume (t7) | 320x192x90 | R16_FLOAT | 46 dispatches | not yet (3D) | 3D float texture written by compute dispatches. | `07_volumetric_volume_45_value.png` |
| 6 | Volumetric lighting volume (t8) | 320x192x90 | R16_FLOAT | 46 dispatches | not yet (3D) | 3D float texture written by compute dispatches. | `08_volumetric_volume_45_value.png` |
| 7 | Depth and stencil (t2) | 3840x2160 | R24G8_TYPELESS | 4285 draws | every frame, as R32F depth | The scene's depth buffer (24-bit, 0 near, 1 far) and 8-bit stencil. | `02_depth_main_depth.png` (+1) |
| 8 | Depth after the depth prepass (t9) | 3840x2160 | R24G8_TYPELESS | 6 draws, copy | on demand; D3D12 read equals D3D11 (as R32F depth) | A copy of the depth buffer taken right after the prepass; later bound as depth for a few late draws. | `09_depth_copy_prepass_depth.png` (+1) |
| 9 | Shadow mask (t10) | 3840x2160 | R8G8B8A8_UNORM | 1 draw | on demand; D3D12 read equals D3D11 | One full-screen draw before the opaque pass. R: 1 lit, 0 in the sun's shadow. | `10_shadow_mask_rgb.png` |
| 10 | Motion vectors (t1) | 3840x2160 | R16G16_FLOAT | 2229 draws | on demand; D3D12 read equals D3D11 | Opaque pass target 1. Screen-space velocity: (current NDC - previous NDC) * (-0.5, 0.5), from the game's lighting shader. | `01_motion_vectors_xy.png` |
| 11 | Normals and masks (t11) | 3840x2160 | R8G8B8A8_UNORM | 2208 draws | every frame | Opaque pass target 2. RG: view-space normal, n.xy / sqrt(8 - 8 n.z) + 0.5. A: specular mask the game's reflections read. B: a small per-material value. | `11_normals_masks_encoded.png` (+3) |
| 12 | Scene colour, HDR (t12) | 3840x2160 | R16G16B16A16_FLOAT | 2493 draws | on demand; D3D12 read equals D3D11 | Lit scene in linear light. Opaque pass target 0; blended draws, the game's own AO, fog and bloom land here later. | `12_scene_hdr_rgb.png` (+1) |
| 13 | Two-channel opaque mask (t13) | 3840x2160 | R8G8_UNORM | 2190 draws | on demand; D3D12 read equals D3D11 | Opaque pass target 3. G marks snow-shaded surfaces in this frame; R stays near zero. | `13_snow_mask_r.png` (+1) |
| 14 | Normals and masks, half size (t14) | 1920x1080 | R8G8B8A8_UNORM | 1 draw | on demand; D3D12 read equals D3D11 | Downsample of the normal target for the game's reflections. | `14_half_normals_encoded.png` (+3) |
| 15 | Screen-space reflections (t15) | 1920x1080 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Half-size HDR targets of the game's reflection passes (trace, then filters). | `15_ssr_rgb.png` (+1) |
| 16 | Screen-space reflections (t16) | 1920x1080 | R16G16B16A16_FLOAT | 1 dispatches | on demand; D3D12 read equals D3D11 | Half-size HDR targets of the game's reflection passes (trace, then filters). | `16_ssr_rgb.png` (+1) |
| 17 | Screen-space reflections (t17) | 1920x1080 | R16G16B16A16_FLOAT | 1 dispatches | on demand; D3D12 read equals D3D11 | Half-size HDR targets of the game's reflection passes (trace, then filters). | `17_ssr_rgb.png` (+1) |
| 18 | View depth pyramid (t18) | 3840x2160, 12 mips | R32_FLOAT | 12 draws | on demand; D3D12 read equals D3D11 | View depth as float with a full mip chain, built for the game's own ambient occlusion. | `18_linear_depth_pyramid_value.png` |
| 19 | The game's ambient occlusion (t19) | 3840x2160 | R8G8B8A8_UNORM | 1 draw | on demand; D3D12 read equals D3D11 | Full-size 8-bit targets of the game's own AO: packed result, then two blur passes. | `19_game_ao_rgb.png` |
| 20 | The game's ambient occlusion (t20) | 3840x2160 | R8G8B8A8_UNORM | 1 draw | on demand; D3D12 read equals D3D11 | Full-size 8-bit targets of the game's own AO: packed result, then two blur passes. | `20_game_ao_rgb.png` |
| 21 | The game's ambient occlusion (t21) | 3840x2160 | R8G8B8A8_UNORM | 1 draw | on demand; D3D12 read equals D3D11 | Full-size 8-bit targets of the game's own AO: packed result, then two blur passes. | `21_game_ao_rgb.png` (+1) |
| 22 | Scene colour, packed float (t22) | 3840x2160 | R11G11B10_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | One full-screen draw into an R11G11B10 target between the game's AO and its composite. | `22_scene_r11_rgb.png` |
| 23 | Scene colour copy (t23) | 3840x2160 | R16G16B16A16_FLOAT | copy | on demand; D3D12 read equals D3D11 | A copy of the scene colour taken before the blended draws; the source for refraction. | `23_scene_hdr_copy_rgb.png` |
| 24 | Water colour target (t24) | 3840x2160 | R16G16B16A16_FLOAT | 21 draw | on demand; D3D12 read equals D3D11 | HDR target drawn with the scene depth; holds the visible water surface in this frame. | `24_water_target_rgb.png` |
| 25 | Water mask target (t25) | 3840x2160 | R10G10B10A2_UNORM | 1 draw | on demand; D3D12 read equals D3D11 | 10-bit target written together with the scene colour by one draw; alpha marks the water area. | `25_water_normals_mask_rgb.png` (+1) |
| 26 | Blended-draw mask (t26) | 3840x2160 | R8G8_UNORM | 293 draws | on demand; D3D12 read equals D3D11 | Two-channel target bound with the scene colour for every blended draw. | `26_blend_mask_r.png` (+1) |
| 27 | Blended-draw colour (t27) | 3840x2160 | R16G16B16A16_FLOAT | 293 draws | on demand; D3D12 read equals D3D11 | HDR target bound with the scene colour for every blended draw (clouds, mist, effects). | `27_blend_colour_rgb.png` |
| 28 | Volumetric lighting, screen (t28) | 3840x2160 | R16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Full-size single-channel float: the volume resolved to the screen (draw, then compute filters). | `28_volumetric_resolve_value.png` |
| 29 | Depth after the scene (t29) | 3840x2160 | R24G8_TYPELESS | copy | on demand; D3D12 read equals D3D11 (as R32F depth) | A copy of the depth buffer taken after the last scene draw, for the post chain. | `29_depth_copy_scene_depth.png` (+1) |
| 30 | Volumetric lighting, screen (t30) | 3840x2160 | R16_FLOAT | 1 dispatches | on demand; D3D12 read equals D3D11 | Full-size single-channel float: the volume resolved to the screen (draw, then compute filters). | `30_volumetric_resolve_value.png` |
| 31 | Volumetric lighting, screen (t31) | 3840x2160 | R16_FLOAT | 1 dispatches | on demand; D3D12 read equals D3D11 | Full-size single-channel float: the volume resolved to the screen (draw, then compute filters). | `31_volumetric_resolve_value.png` |
| 32 | HDR post chain (t32) | 960x540 | R16G16B16A16_FLOAT | 1 dispatches | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `32_post_hdr_rgb.png` (+1) |
| 33 | HDR post chain (t33) | 960x540 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `33_post_hdr_rgb.png` |
| 34 | HDR post chain (t34) | 240x540 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `34_post_hdr_rgb.png` |
| 35 | HDR post chain (t35) | 240x540 | R16G16B16A16_FLOAT | 5 draws | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `35_post_hdr_rgb.png` |
| 36 | HDR post chain (t36) | 120x540 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `36_post_hdr_rgb.png` |
| 37 | HDR post chain (t37) | 960x540 | R16G16B16A16_FLOAT | 2 draws | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `37_post_hdr_rgb.png` |
| 38 | HDR post chain (t38) | 960x540 | R16G16B16A16_FLOAT | 4 draws | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `38_post_hdr_rgb.png` (+1) |
| 39 | HDR post chain (t39) | 240x135 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `39_post_hdr_rgb.png` |
| 40 | HDR post chain (t40) | 240x135 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `40_post_hdr_rgb.png` |
| 41 | HDR post chain (t41) | 240x135 | R16G16B16A16_FLOAT | 2 draws | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `41_post_hdr_rgb.png` |
| 42 | HDR post chain (t42) | 240x540 | R16G16B16A16_FLOAT | 3 draws | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `42_post_hdr_rgb.png` |
| 43 | HDR post chain (t43) | 120x540 | R16G16B16A16_FLOAT | 3 draws | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `43_post_hdr_rgb.png` |
| 44 | HDR post chain (t44) | 120x540 | R16G16B16A16_FLOAT | 3 draws | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `44_post_hdr_rgb.png` |
| 45 | Luminance chain (t45) | 60x34 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Shrinking HDR targets down to a few pixels: the exposure measurement. | `45_luminance_rgb.png` |
| 46 | Luminance chain (t46) | 15x9 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Shrinking HDR targets down to a few pixels: the exposure measurement. | `46_luminance_rgb.png` |
| 47 | Luminance chain (t47) | 4x3 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Shrinking HDR targets down to a few pixels: the exposure measurement. | `47_luminance_rgb.png` |
| 48 | Luminance chain (t48) | 2x2 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Shrinking HDR targets down to a few pixels: the exposure measurement. | `48_luminance_rgb.png` |
| 49 | HDR post chain (t49) | 960x540 | R16G16B16A16_FLOAT | 1 draw | on demand; D3D12 read equals D3D11 | Downsampled HDR targets: bloom and blur steps. | `49_post_hdr_rgb.png` (+1) |
| 50 | Display-referred post chain (t50) | 3840x2160 | R8G8B8A8_UNORM | 1 draw | on demand; D3D12 read equals D3D11 | 8-bit targets after the tone map: tone-mapped frame, temporal anti-aliasing outputs, reduced copies. | `50_post_ldr_rgb.png` |
| 51 | Display-referred post chain (t51) | 3840x2160 | R8G8B8A8_UNORM | 1 draw | on demand; D3D12 read equals D3D11 | 8-bit targets after the tone map: tone-mapped frame, temporal anti-aliasing outputs, reduced copies. | `51_post_ldr_rgb.png` |
| 52 | Display-referred post chain (t52) | 3840x2160 | R8G8B8A8_UNORM | 1 draw | on demand; D3D12 read equals D3D11 | 8-bit targets after the tone map: tone-mapped frame, temporal anti-aliasing outputs, reduced copies. | `52_post_ldr_rgb.png` |
| 53 | Display-referred post chain (t53) | 960x540 | R8G8B8A8_UNORM | 1 draw, 1 dispatches | on demand; D3D12 read equals D3D11 | 8-bit targets after the tone map: tone-mapped frame, temporal anti-aliasing outputs, reduced copies. | `53_post_ldr_rgb.png` (+1) |
| 54 | Display-referred post chain (t54) | 960x540 | R8G8B8A8_UNORM | 1 dispatches | on demand; D3D12 read equals D3D11 | 8-bit targets after the tone map: tone-mapped frame, temporal anti-aliasing outputs, reduced copies. | `54_post_ldr_rgb.png` (+1) |
| 55 | Frame (backbuffer) (t0) | 3840x2160 | R8G8B8A8_UNORM | 17 draws | every frame | The finished frame: the game's final blit, then the HUD on top. | `00_backbuffer_rgb.png` |

Names come from rules in `tools/frame_buffers/roles.py`: the format, the size against the
backbuffer, which targets were bound together, what was copied into what, and the order of the
passes. Texture numbers (t0, t1, ...) change between runs; the rules do not use them.

## Decoding

**Depth.** 24-bit depth, 0 at the near plane and 1 at the far plane, with an 8-bit stencil.
The game's projection gives `depth = A - B / z` for view depth `z`, where `A` is row 2,
column 2 of the matrix and `-B` is row 2, column 3. So `z = B / (A - depth)`. In the captured
frame A = 1.00004244 and B = 15.0006361: a near plane at 15 units and a far plane near 353,468.
The game computes the same view depth itself for its ambient occlusion (the float pyramid,
clamped at 10,000 units); the decode check compares the two.

**Normals.** Target 2 of the opaque pass, 8 bits a channel. Red and green hold the view-space
normal as the game's lighting shaders write it:

```
enc = n.xy / sqrt(8 - 8 * n.z) + 0.5
```

The game's view space has x right, y up and z into the screen, so a surface facing the viewer
has a negative `n.z`. With `s = |enc - 0.5|^2`, the inverse is `n.z = 8 s - 1` and
`n.xy = 4 (enc - 0.5) sqrt(1 - 4 s)`. This formula was read from the disassembly of the game's
own compiled lighting shader, from the instructions that write output 2. Alpha is a specular
mask the game's reflections read. Blue is a small per-material value. The clear value is
(127, 127, 0, 0). About 13 % of drawn pixels in this frame carry a normal that points away from
the viewer: two-sided foliage keeps its front-face normal on back faces.

**Motion vectors.** Target 1 of the opaque pass, two 16-bit floats:
`(current NDC.xy - previous NDC.xy) * (-0.5, 0.5)`, from the same shader, which projects the
current and the previous position with the two unjittered view-projection matrices of the
per-frame constants.

**Shadow mask.** Red is 1 where the sun reaches the surface and 0 in its shadow.

**Scene colour.** Linear light, 16-bit float. Alpha is the draw's output alpha.

## The camera

Every draw of the opaque pass has the game's per-frame constant buffer at slot 12 (720 bytes).
It starts with ten 4 x 4 float matrices, stored for column vectors. From the captured values:

| Index | Matrix |
|---|---|
| 0 | view, rotation only (world positions are relative to the camera) |
| 1 | view to clip, with the sub-pixel jitter of temporal anti-aliasing |
| 2 | world to clip, jittered |
| 3 | world to clip, unjittered |
| 4 | previous frame's world to clip, unjittered |
| 5 | inverse of 6 |
| 6 | view to clip, unjittered |
| 7 | inverse view |
| 8, 9 | inverses of 2 and 1 |

The proxy keeps a CPU copy of every dynamic constant buffer the game maps, so the bridge reads
matrix 1 at the end of the opaque pass with no GPU read. The capture also stores vertex slots
0, 1, 2 and 12 and pixel slots 0, 1, 2 and 12 for every draw (`drawcb.bin`): world matrices,
material constants and the light matrices of the shadow passes are in there. Only the slot 12
layout above has been decoded and checked so far.

## Sharing with D3D12

D3D11 opens a D3D12 texture only when it is render-target capable, so each buffer crosses in a
format with the same bits:

| Game format | Crosses as | How |
|---|---|---|
| colour formats | the same format | copy |
| R16 depth (shadow maps) | R16_UNORM | copy of one array layer |
| D24S8 | R32_FLOAT depth | a small compute copy; the stencil byte does not cross yet |
| cube map | one face at a time | copy of one face |
| 3D volumes | not yet | |

Depth, normals and the frame have their own shared textures and cross every frame
(`CaptureDepth`, `CaptureNormal`, `Execute`). Every other buffer crosses on demand through
`D3D12Bridge::ShareAndRead`, which copies one layer into a shared texture and reads it on the
D3D12 device. During a capture the proxy does this for every texture it dumps and compares the
D3D12 read with the D3D11 bytes. In the captured frame all 53 two-dimensional textures compared
equal, every layer and every face.

## Coverage: which pixels have real surface data

`coverage.py` gives every pixel the class of the last scene draw span that changed it. A
span's pixels are the ones whose scene colour differs between the copies taken before and
after it. Nothing is estimated from depth.

| Class | Pixels | Share | In the game's buffers | Missing |
|---|---|---|---|---|
| Opaque, normal written | 6,939,770 | 83.67 % | depth, normal, motion | albedo, specular and gloss, emissive, material class |
| Sky | 569,753 | 6.87 % | nothing to describe | |
| Blended draws | 683,905 | 8.25 % | the surface behind | depth, normal and motion of the blended surface |
| Opaque, normal left at its clear value | 51,151 | 0.62 % | depth, motion | normal |
| Water | 49,821 | 0.60 % | the bed under the water | water depth, normal, motion |
| Late draws on the prepass depth copy | 0 | 0 % | | colour and motion only when present |

Real surface data for 90.54 % of the frame (opaque with a normal, and sky). Missing for 9.46 %.
The target is zero missing.

Two more gaps do not show up per pixel:

- The game never writes albedo, specular and gloss, emissive or a material class to any
  target. They are missing for every opaque pixel.
- The game's own view depth comes from the depth copy taken after the prepass. 1.8 % of pixels
  got their final depth later in the opaque pass, so the game's own AO does not see them. RAW
  reads the depth buffer at the end of the opaque pass.

No first-person geometry was on screen in this frame (no weapon drawn), so that class has no
measurement yet.

## Closing the gaps: shader-patch routes

`shaders.py` shows what each kind of draw writes today. The proxy already replaces and patches
pixel shaders by hash (`ShaderManager`, `DXBCPatcher`, `MaterialPipeline`), so each route adds
outputs to shaders that already hold the data and binds extra targets for those spans.

| Gap | What the shaders write now | Route |
|---|---|---|
| Albedo, specular and gloss, emissive, material class on opaque pixels | 2,190 draws with 54 shaders; all write colour, motion and normals; 69 also write the mask | Bind three more targets with the opaque set (D3D11 allows eight). Patch each lighting shader to copy its diffuse sample, its specular terms and its emissive term to them before lighting is applied, and to write a class constant chosen per shader hash. `MaterialPipeline` has the patcher; its extra-target bind is unfinished. |
| Opaque pixels with no normal (0.62 %) | Every opaque-pass shader writes output 2, so these pixels were not shaded by the opaque pass: depth comes from the prepass or from the 18 normal-and-motion draws | Find the draws by diffing the normal target per draw, then patch the shader that leaves them, or write the normal in the prepass shader for those meshes. |
| Blended surfaces (8.25 %) | 293 draws with 21 shaders write colour, a mask and a second colour; depth is read-only for 285 of them | Bind a second, small G-buffer for this span (depth, normal, motion). Patch the 21 shaders to write `SV_Position.z`, their normal and their motion to it. Keep the nearest layer with a MIN blend on depth; a pass that needs all layers reads the per-draw list instead. |
| Water (0.60 %) | One draw writes colour and the water mask | Patch the water shader to write its surface depth, its normal after the wave normal maps, and motion into the same second G-buffer. |
| First-person and other late draws | 6 draws write colour and motion against the prepass depth copy | Patch them to write a normal, and record their own projection from their constants, since a first-person model can use another field of view. Needs a frame with a weapon drawn to measure. |
| The game's stale view depth | not a shader gap | RAW's pass already reads the final depth. |

## Checks

`python tools/frame_buffers/check_decode.py <capture>` on the captured frame:

| Check | Result |
|---|---|
| Every captured buffer reads back at its stated size with finite floats | 55 buffers |
| View depth from the depth buffer equals the game's own view depth, relative 1e-4 | 7,065,324 pixels, all inside; median relative error 2.0e-6 |
| Control: a projection term off by 1 % | fails, 0 % inside |
| Decoded normals have unit length | worst deviation 2.2e-16 |
| encode(decode(texel)) returns the texel | identical for all 7,596,965 texels the game's write can produce; 0.10 % of texels lie outside that set (blended writes) |
| On flat patches of the depth buffer the decoded normal is near the plane | median 15.0 degrees over 422 patches (the target carries normal maps and smoothed vertex normals) |
| Control: x and y swapped | fails, median 69.1 degrees |
| Control: z flipped, on patches that look at the camera | fails, median 105.4 degrees |
| Motion vectors equal the reprojection with the game's matrices, 0.5 px | 99.78 % of opaque pixels; the rest moved on their own |

The motion check is weak in this frame: the camera stood still, so the expected camera motion
is zero. A frame with the camera moving would test the matrices.

Sharing is checked in the round-trip test (`tests/bridge_roundtrip`, six on-demand cases with
controls) and in the capture itself, as described above.

## Limits

- One frame of one outdoor scene. Interiors, night, rain, snow, underwater, menus and the
  first-person view can bind other targets.
- A buffer the game writes several times is captured after its last write. Full-size targets
  are also kept after every span.
- Mip levels above 0 are not captured (the view-depth pyramid has 12).
- The stencil byte and the 3D volumes do not cross to D3D12 yet.
- Role names describe what a buffer held in this frame. Four are named from their content
  alone and not from a decoded shader: the two-channel opaque mask, the water mask target, and
  the two targets of the blended span.
- Shadow map matrices and the light list are in the per-draw constants of the capture. Their
  layout is not decoded yet.
