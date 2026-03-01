# Frans Bouma (Otis_Inf) — Complete Technical Analysis for SkyrimBridge

## Overview

Frans Bouma is a Dutch software engineer whose contributions span three interconnected domains: **ReShade shader authoring** (OtisFX), **camera system engineering** (IGCS — Injectable Generic Camera System), and **cross-system data bridging** (IGCSConnector). His work is distinguished from other shader authors by a core architectural insight: **the camera is a shader input, not just a viewport**. By exposing camera matrices, orientation, and position as shader uniforms, he enables techniques (accumulation-buffer DOF, panoramic stitching, lightfield capture) that are architecturally impossible in conventional post-processing pipelines. This philosophy directly parallels SkyrimBridge's approach of treating game state as shader input.

---

## 1. Cinematic DOF — State-of-the-Art Disc-Blur Architecture

### 1.1 Core Algorithm

Cinematic DOF uses a **ring-based disc blur** with configurable quality (5–12 concentric rings). For each pixel:

1. **CoC calculation** uses a physical thin-lens model:
   - HyperFocal distance `H = (f² / (N * CoC_pixel)) + f` where `f` = focal length, `N` = f-number
   - Focus depth read from a 1×1 render target holding the current focus point's depth
   - Depth buffer linearized with assumption: 1.0 depth = 1000 world units = 1000 meters
   - CoC sign determines near (negative) vs far (positive) plane membership

2. **Ring gathering** iterates concentric rings around each pixel:
   ```hlsl
   ringRadiusDeltaCoords = (PixelSize * maxBlurInPixels * fragmentRadius) / numberOfRings;
   for(ringIndex = 0; ringIndex < numberOfRings; ringIndex++) {
       // Sample points around ring circumference
       for(pointIndex = 0; pointIndex < pointsOnRing; pointIndex++) {
           sampleCoord = texcoord + currentRadius * bokehShape[pointIndex];
           // Apply anamorphic stretch + rotation
           sampleCoord = mul(anamorphicRotationMatrix, sampleCoord) * anamorphicFactors;
       }
       currentRadius += ringRadiusDelta;
   }
   ```

3. **Weight accumulation** uses `float4(color.rgb * radius, radius)` — RGB weighted by the sample's CoC radius, with radius as the divisor weight. This ensures samples with larger blur circles contribute proportionally more to the final average.

### 1.2 Near-Plane Bleed (Nilsson2012 + Jimenez2014 Hybrid)

The near-plane blur uses fundamentally different semantics from the far plane. Based on Nilsson's 2012 paper and a variant of Jimenez's 2014 SIGGRAPH presentation:

- **Far/in-focus pixels receive higher weight** so they bleed INTO the near plane (not the reverse)
- **Tile-based CoC dilation**: the screen is divided into tiles, each tile stores the minimum CoC value (maximum near-plane blur), then a tile neighbor gather pass dilates this to ensure narrow near-plane objects properly bleed
- **Alpha compositing**: near-plane result alpha encodes blur strength, enabling proper layering over the far-plane result
- Alpha calculation: `saturate(2 * (radius > 0.1 ? (farRadius <= 0 ? 2 : 1) * radius : max(radius, -farRadius)))`

### 1.3 Highlight System with Reinhard De/Re-Tonemapping

v1.2.1 replaced the original highlight amplification with proper tonemapping-based highlights (credited to tips from MartyMcFly):

- `AccentuateWhites()` function applies **inverse Reinhard** to extract HDR-like highlight information from the LDR buffer
- During disc blur, the gained (inverse-tonemapped) tap colors are tracked alongside the standard accumulation
- `maxLuma` tracking per ring identifies the brightest contributing sample
- After blur, **forward Reinhard** recompresses the highlights back to displayable range
- A `HighlightBoostFactor` provides additional gain for dimly-lit scenes using simple levels math
- Separate highlight thresholds for near and far planes prevent cross-plane highlight bleeding

### 1.4 Anamorphic Bokeh

v1.1.8 added anamorphic distortion — bokeh shapes stretch and rotate based on distance from screen center:

```hlsl
float4 CalculateAnamorphicFactor(float2 offsetFromCenter) {
    float2 up = float2(0, 1);
    float2 right = float2(1, 0);
    float distFromCenter = length(offsetFromCenter);
    float anamorphicFactor = lerp(1.0, AnamorphicAmount, distFromCenter);
    return float4(up * anamorphicFactor, right / anamorphicFactor);
}
```

A 2×2 rotation matrix orients the stretch toward/away from center, producing the characteristic elliptical bokeh of anamorphic cinema lenses. This is applied per-sample during the ring gather loop.

### 1.5 Anti-Undersampling

A **9-tap tent filter** (from KinoBokeh by Keijiro Takahashi, referenced as Jimenez2014) is applied as a post-smooth pass to mitigate the visible sampling pattern dots that appear with low ring counts and large blur radii. prod80 contributed dithering in the combiner pass to prevent banding in low-luminance blurred regions.

### 1.6 Multi-Pass Architecture

```
Pass 1: PS_DetermineCurrentFocus → 1×1 RT (depth at focus point)
Pass 2: PS_CopyCurrentFocus → previous focus RT (for temporal smoothing)
Pass 3: PS_CalculateCoCValues → CoC per pixel (R=signed CoC, G=far radius)
Pass 4: PS_CoCTile1 → 1/4 res CoC tiles (min CoC per tile)
Pass 5: PS_CoCTile2 → tile neighbor gather (dilate CoC for bleed)
Pass 6: PS_CoCGaussian1 → Gaussian blur on CoC tiles (smooth transitions)
Pass 7: PS_PreBlur → pre-blur separating near/far (Jimenez2014)
Pass 8: PS_BokehBlur → main disc blur (near + far plane, half-res)
Pass 9: PS_TentFilter → 9-tap undersampling mitigation
Pass 10: PS_Combiner → upscale half-res + composite + dithering
Pass 11: PS_PostSmoothing → optional post-blur smooth
Pass 12: PS_PostSmoothing2AndFocusing → final composite + focus plane overlay
```

This 12-pass architecture is notable for its clean separation of concerns and the half-resolution optimization in the main blur pass with a dedicated upscale combiner.

---

## 2. IGCS Depth of Field — Accumulation-Buffer Ground Truth

### 2.1 Core Concept

IGCS DOF is fundamentally different from any screen-space DOF: it **physically moves the camera** across an aperture disc, captures the scene from each viewpoint, and **accumulates the frames** into a single result. This produces mathematically correct bokeh with:

- No edge artifacts (no CoC leaking)
- Proper occlusion handling (objects behind the subject are correctly revealed/hidden)
- Physically accurate bokeh shapes (including cateye, spherical aberration)
- No depth buffer required — works purely from rendered frames

The technical term is **accumulation-buffer based Depth of Field**, the same technique used in offline rendering (Pixar's RenderMan) and some shipping games (Forza series).

### 2.2 Rendering Pipeline

1. **Session setup**: User positions camera, sets focus point and max bokeh size
2. **Shape generation**: The addon computes camera positions on the aperture disc:
   - Circular: concentric rings with configurable vertex count
   - Aperture-shaped: polygonal shapes (3–10 vertices) with inner ring rotation
   - Ring angle offsets for artistic control
3. **Frame capture**: For each aperture sample point:
   - Addon moves the camera to the offset position via IGCS API
   - Waits N frames for the game engine to settle (configurable, handles TAA/in-flight frames)
   - IgcsDOF.fx shader captures and accumulates the frame
4. **Blending**: Accumulated frames are averaged (with HDR-aware weighting since v2.3.0)
5. **Final composite**: User takes screenshot of the accumulated result

### 2.3 Advanced Optical Simulation

The IGCS DOF system simulates real lens imperfections:

- **Spherical aberration**: reimplemented in v2.3.0 to preserve light across highlights (MartyMcFly)
- **Cateye bokeh**: added in v2.5.0 (MartyMcFly), produces Helios 40-2 style swirly bokeh increasing toward frame edges
- **Chromatic aberration**: 4 types of R/G/B displacement on highlight edges (Otis_Inf)
- **Fringe**: separate thin outline fringing on highlights (MartyMcFly)
- **Custom aperture shapes**: arbitrary polygon with rotation, inner ring offset, vertex count

### 2.4 Performance Optimizations

- v2.4.0: "Way faster rendering, up to full framerate" (Otis_Inf) — likely uses frame scheduling instead of waiting
- v2.5.0: Compute shader rewrite of IgcsDOF.fx (MartyMcFly) — much faster accumulation
- First-frame flash elimination (v2.5.0) — prevents initial blend artifact
- Edge fade correction — prevents darkened borders from camera movement

---

## 3. IGCS Connector — Camera State as Shader Uniform API

### 3.1 Exposed Camera Uniforms

The IGCSConnector (v2.4+) exposes the following to any ReShade shader:

| Uniform | Type | Notes |
|---------|------|-------|
| `IGCS_cameraDataAvailable` | bool | Gate for all camera-dependent code |
| `IGCS_cameraEnabled` | bool | Is freecam active |
| `IGCS_cameraMovementLocked` | bool | Is camera locked |
| `IGCS_cameraFoV` | float | Field of view in degrees |
| `IGCS_cameraWorldPosition` | float3 | World-space camera XYZ |
| `IGCS_cameraOrientation` | float4 | Quaternion (x,y,z,w) |
| `IGCS_cameraViewMatrix4x4` | float4x4 | Full view matrix |
| `IGCS_cameraProjectionMatrix4x4LH` | float4x4 | LH projection (near=0.1, far=10000) |
| `IGCS_cameraUp/Right/Forward` | float3 | Basis vectors from view matrix |
| `IGCS_cameraRotationPitch/Yaw/Roll` | float | Euler angles in radians |

### 3.2 ReShade State Interpolation Along Camera Paths

The IGCSConnector records the complete ReShade state (which effects are enabled, all float parameter values) at each camera path node, then **interpolates all floating-point parameters** between nodes during playback. This creates smooth transitions of DOF focus, fog density, color grading, etc. along cinematic camera movements.

---

## 4. Adaptive Fog — Bloom-Powered Light Diffusion

### 4.1 Algorithm

The key insight: real fog doesn't just add flat color — it **scatters light**, causing bright objects to bloom into surrounding fog. Adaptive Fog approximates this by:

1. **Bloom pass**: Generate an intentionally over-bloomed version of the framebuffer (SweetFX-derived CeeJay bloom)
2. **Depth-based fog factor**: `fogFactor = clamp(pow(depth * curve, maxFactor) * clamp(depth - fogStart, 0, 1), 0, maxFactor)`
3. **Dual blend**: `lerp(scene, lerp(bloom, fogColor, fogFactor), fogFactor)`

The dual-lerp is the critical innovation: at low fog density, the bloom (light scatter) dominates. At high fog density, the flat fog color dominates. This naturally produces:
- Light halos around bright objects in light fog
- Complete whiteout in heavy fog
- Proper light diffusion behavior without volumetric ray marching

### 4.2 SkyrimBridge Relevance

This bloom+fog dual blend concept could be integrated into SB_VolumetricFog as a complementary cheap path for weather states where full ray marching is too expensive. The fog color could be driven by `SB_Fog_Color`, fog density by `SB_Fog_Density`, and the bloom scatter amount could scale with `SB_Precipitation` (rain/snow increase light scatter).

---

## 5. Depth Haze — Atmospheric Perspective Simulation

### 5.1 Algorithm

Depth Haze models atmospheric perspective — the phenomenon where distant objects lose contrast and shift toward the ambient sky color due to light scattering in the atmosphere:

1. **Separable block blur**: Horizontal then vertical Gaussian blur of the full scene
2. **Depth blend**: `lerp(scene, blurred, clamp(depth * EffectStrength, 0, 1))`
3. **Screen-position fog gradient**: Fog intensity varies with Y coordinate — maximum at the horizon line (y ≈ 0.5), reducing toward top (sky) and bottom (ground near camera):
   ```hlsl
   float yFactor = clamp(texcoord.y > 0.5 ? 
       1.0 - (texcoord.y - 0.5) * 2.0 :  // Above center: reduce toward top
       texcoord.y * 2.0,                    // Below center: reduce toward bottom  
       0.0, 1.0);
   ```
4. **Fog color overlay**: `lerp(blended, float4(FogColor, 1), clamp((depth - FogStart) * yFactor * FogFactor, 0, 1))`

### 5.2 SkyrimBridge Relevance

The Y-gradient fog concept is a valuable addition to atmospheric rendering — it prevents fog from appearing on the sky dome (which should be clear above the fog layer) and reduces ground-level fog near the camera. This could enhance SB_VolumetricFog's screen-space compositing pass.

---

## 6. Height Fog — World-Space Fog Plane via Inverse Projection

### 6.1 Algorithm (Co-authored with MartyMcFly)

Height Fog inserts an infinite plane into the 3D scene and applies distance-based fog relative to that plane. The critical technique:

1. **Inverse projection**: Reconstruct world-space position from depth + UV using the inverse of the camera's projection and view matrices
2. **Plane intersection**: Calculate each pixel's distance from the fog plane in world space
3. **Fog density**: Apply exponential or linear falloff based on world-space distance from the plane
4. **Camera-relative**: The fog plane moves with the camera (since true world-space positioning requires engine data)

### 6.2 SkyrimBridge Advantage

With SkyrimBridge providing `SB_ViewProjection` (and its inverse), `SB_CameraPos`, and the camera's view/projection matrices, Height Fog could be made **truly world-space** — the fog plane would stay at a fixed world height (like y = -500 for valley fog) regardless of camera position. Combined with `SB_Weather_Flags` for fog weather detection and `SB_Fog_NearFar` for game-consistent fog distances, this becomes a dramatically more convincing effect.

---

## 7. Emphasize — Depth-Based Focus Isolation

### 7.1 Algorithm

Emphasize uses the depth buffer to create a "focus isolation" effect — making the subject pop while de-emphasizing the background/foreground:

1. Read depth at focus point (configurable: auto, mouse-driven, or manual)
2. Calculate focus falloff based on depth difference from focus point
3. For out-of-focus pixels: desaturate toward grayscale + blend toward a configurable "blend color"
4. In-focus pixels are left completely unmodified

### 7.2 SkyrimBridge Relevance

The Emphasize concept maps directly to **combat-reactive focus isolation**. When `SB_Player_Combat.x` indicates active combat, the shader could automatically emphasize the player's immediate surroundings (using `SB_Player_Position` for focus depth) while de-emphasizing distant terrain. This creates a subtle cinematic "tunnel focus" during combat that enhances immersion without requiring DOF blur.

---

## 8. Cross-Cutting Architectural Insights

### 8.1 Camera State as First-Class Shader Input

Bouma's IGCS architecture proves that exposing camera data (position, orientation, FoV, view/projection matrices) as shader uniforms enables an entirely new class of effects. SkyrimBridge already provides view/projection matrices — the key takeaway is to ensure these are:
- **Always available** (not gated behind optional flags)
- **Temporally consistent** (previous-frame matrices for motion vector computation)
- **Fully decomposed** (individual Euler angles, position components, basis vectors — not just packed matrices)

### 8.2 Accumulation-Buffer as Quality Ceiling

IGCS DOF demonstrates that accumulation-buffer techniques (rendering multiple frames with slight camera offsets and blending) produce ground-truth results impossible in single-frame screen-space. While real-time gameplay DOF must remain screen-space, SkyrimBridge could provide an **"enhanced screenshot mode"** flag (`SB_Render_ScreenshotMode`) that signals shaders to use higher-quality paths — more GTAO samples, more SSR rays, higher bloom quality — since the user has paused for a screenshot.

### 8.3 Bloom as Fog Ingredient

Adaptive Fog's dual-blend `lerp(scene, lerp(bloom, fogColor, fogFactor), fogFactor)` is a computationally cheap way to fake light scattering in atmospheric effects. This approach could augment SB_VolumetricFog: rather than computing full in-scattering, the fog compositing pass could blend the HDR bloom texture into fog-affected regions, creating convincing light halos around torches and streetlamps in foggy weather at minimal additional cost.

### 8.4 Physical Lens Model as DOF Standard

Cinematic DOF's thin-lens model (focal length, aperture/f-number, hyperfocal distance) provides a proven parameter interface for DOF. SkyrimBridge's DOF could adopt this same model, with SkyrimBridge-specific enhancements:
- Focal length responds to `SB_Player_State` (draw bow → narrow FOV → increase focal length)
- Aperture responds to interior/exterior (`SB_Interior_Type`) — wider aperture indoors
- Auto-focus point driven by `SB_Crosshair_Depth` or combat target position

### 8.5 Proper Highlight Handling in LDR

The evolution from naive highlight boosting to Reinhard de/re-tonemapping in Cinematic DOF v1.2.1 is a solved problem that applies to any effect processing bright pixels in Skyrim's LDR output. SB_HDRBloom and SB_SSR should both apply inverse tonemapping before processing highlights and forward tonemapping after — the same approach used in production DOF implementations (UE4, CryEngine).

---

## 9. Technique Priority for SkyrimBridge Integration

| Priority | Technique | Source | Integration Path |
|----------|-----------|--------|-----------------|
| P0 | Reinhard de/re-tonemapping for highlights | CinematicDOF v1.2.1 | Apply to SB_HDRBloom threshold, SB_SSR highlight handling |
| P0 | Bloom+fog dual blend | AdaptiveFog | Add to SB_VolumetricFog compositing pass |
| P1 | Physical thin-lens DOF model | CinematicDOF | Replace ad-hoc DOF parameters with focal length + f-number |
| P1 | Camera uniform decomposition | IGCS Connector | Ensure SkyrimBridge exposes Euler angles, basis vectors, FoV |
| P1 | Y-gradient fog masking | DepthHaze | Add to SB_VolumetricFog screen-space fog to prevent sky contamination |
| P2 | Anamorphic bokeh distortion | CinematicDOF v1.1.8 | Add to SB_DOF as optional cinematic mode |
| P2 | Tile-based near-plane CoC dilation | CinematicDOF | Improve SB_DOF near-plane bleed quality |
| P2 | Depth-based emphasis/isolation | Emphasize | New combat-reactive focus isolation effect |
| P3 | Accumulation-buffer DOF mode | IGCS DOF | Screenshot-mode ground-truth DOF |
| P3 | ReShade state interpolation concept | IGCS Connector | SkyrimBridge could interpolate shader params along weather transitions |
