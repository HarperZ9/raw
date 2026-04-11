# SkyrimBridge — Revised Compatibility Proposal

Boris, thank you for taking the time to look at this and for the honest feedback. You're right on most of it, and I want to acknowledge that directly before going further.

---

## What We Got Wrong

You already have material detection through shader hash, render flags (TREE_ANIM, LIGHTING, etc.), and shader code pattern analysis. You have multiple copies of the Z buffer for your own purposes. You have your own GUI plans. You know your own pipeline better than anyone.

We built GPU-side features (material ID g-buffer, Hi-Z depth pyramid, luminance histogram, LUT injection) that duplicate work you've already done internally, or solve problems that don't exist in your architecture. The material UAV bind/unbind around draws — you're right that it could break your optimization where you intentionally carry state between draws. And b15 doesn't even map to anything real in your pipeline (max cb14).

Those features were built for a world where ENB doesn't already handle these things. But ENB does. So they're coming out.

**We're removing ALL of the following. This is not an option — this is what we're doing regardless of anything else in this document:**

| Removed | Details |
|---------|---------|
| Material ID pipeline | DrawIndexed hook, PSSetShader hook, CreatePixelShader hook, DXBCPatcher, GBufferManager UAV/SRV, MaterialTracker, b15 CB, t15 texture, u4 UAV |
| GPU compute features | Luminance histogram (t17), Hi-Z depth pyramid (t19), LUT Texture3D injection (t18, s2) |
| Constant buffer injection | b7 constant buffer, PSSetConstantBuffers hook |
| PSSetShaderResources hook | No SRV injection of any kind |
| RenderInspector | DrawIndexed/PSSetShader capture hooks |

After these removals, SkyrimBridge has **zero D3D11 vtable hooks on your Context or Device**. The only things that remain in D3D11 are:

1. **Present hook** (SwapChain) — debug overlay, renders after everything
2. **D3DCompile IAT hook** — shader bytecode cache (discussed below)

---

## What SkyrimBridge Actually Is

With the GPU pipeline stripped, here is what SkyrimBridge does. It has two purposes, and only one of them involves ENB.

### Purpose 1: Game State for ENB Shaders

22 SKSE data trackers read Skyrim's engine state every frame through CommonLibSSE — sun/moon positions, weather type and transition state, fog distances and colors, wind speed and direction, precipitation intensity, player world position, camera matrices, interior lighting, active magic effects, time of day, and more.

**122 float4 parameters** (1952 bytes) of ground-truth engine data.

This data reaches ENB shaders through `ENBSetParameter` — your own API, the way you designed it to be used. Nothing is injected, hooked, or forced into your pipeline. The shader declares a variable with a UIName annotation, and we push data to it through your SDK.

### Purpose 2: Runtime Engine Observation for the Modding Community

SkyrimBridge is also a general-purpose engine state API. This part has nothing to do with ENB's rendering pipeline:

- **Shared memory bridge** (`SkyrimBridge_GameState` named mapping) — Any process can read 1952 bytes of live engine state — OBS overlays, LED sync, accessibility tools, companion apps. Zero D3D11 involvement, zero performance cost to ENB.

- **Papyrus script bridge** (7 native functions) — Gameplay mods can query engine state that Papyrus normally can't access — GPU frame time, weather form IDs, computed luminance, interior detection. Lets mod authors build adaptive gameplay without their own hooks.

- **EditorID cache** — Hooks 80+ form types to cache editor IDs the engine discards after load. Exports `GetFormEditorID()` for other SKSE plugins. Replaces NativeEditorID Fix and po3_Tweaks CacheEditorIDs (auto-detects and defers to them if present).

- **Write-back processor** — INI-driven system that adjusts game engine values (FOV, fog distances, lighting) based on computed state. Useful for mods that want to dynamically adjust the game world without writing their own SKSE plugin.

None of this touches D3D11. None of it has any interaction with ENB's rendering pipeline. It's pure SKSE/CommonLibSSE.

---

## What ENB Shaders Can't Do Without Game State

ENB's native parameters (Timer, ScreenSize, TimeOfDay1/2, SunDirection, Weather, matrices, etc.) cover a lot. Shader authors can derive linear depth, view-space positions, normals, sun screen position, and basic time-of-day blending from these alone.

But there are things that no amount of screen analysis can reconstruct. These are the problems SkyrimBridge solves, and they directly affect the quality of the presets your users install.

### Weather and Atmosphere

Right now, if a shader author wants to know "is it raining?", they have to guess. ENB's Weather parameter gives blend weights between weather categories, but there is no precipitation intensity, no surface wetness value, no wind speed, no lightning event, and no actual fog color from the engine.

Without SkyrimBridge, preset authors write heuristics — detecting rain from screen-space noise patterns, guessing fog density from depth buffer falloff, approximating wind from camera jitter. These heuristics are fragile, produce false positives, and frequently conflict with weather mods. When someone installs Obsidian Weathers or Cathedral Weathers, these heuristics break.

**With SkyrimBridge** (via your API):

| Parameter | What It Provides |
|-----------|-----------------|
| `SB_Precipitation.y` | Exact rain/snow intensity from the engine |
| `SB_Precip_Surface.x` | Surface wetness (0-1) |
| `SB_Precip_Surface.z` | Snow cover amount |
| `SB_Wind.x/y` | Wind speed and direction |
| `SB_Lightning.x` | Lightning flash intensity |
| `SB_Fog_NearColor.rgb` | The game's actual fog color |
| `SB_Fog_FarDist.x` | The game's actual fog far distance |

This means a shader author can write:
- Rain droplets on the lens scaled to actual precipitation intensity
- Fog that matches what the game is already rendering
- Directional bloom streaks aligned with actual wind
- Lightning-triggered flash bloom at the exact moment lightning fires
- Wet surface adjustments to specular/reflection intensity

All through parameters that arrive via `ENBSetParameter`.

### Interior Lighting

ENB provides `EInteriorFactor` (0=exterior, 1=interior), but nothing about what *kind* of interior. A warm candlelit tavern, a blue-white Dwemer ruin, and a dark Falmer cave all read as `EInteriorFactor=1`.

SkyrimBridge provides:

| Parameter | What It Provides |
|-----------|-----------------|
| `SB_Interior_Ambient.rgb` | The cell's actual ambient color |
| `SB_Interior_Direct.rgb` | Directional light color |
| `SB_Interior_FogColor.rgb` | Interior fog tint |

This lets shader authors write interior-aware color grading — warmer in taverns, cooler in ruins, desaturated in caves — all automatically from the engine's own lighting data, not hardcoded.

### Film Emulation (The Biggest Quality Leap)

Modern ENB presets increasingly pursue photochemical film emulation — emulating the look of Kodak Vision3 500T, Fuji 3513, etc. The state of the art (Dehancer, TreyM's Film Workshop, professional colorist pipelines) shows that believable film emulation requires the film response to *change* with the scene, not be a static LUT.

Real film reacts differently to different lighting conditions. A shot exposed for tungsten light looks different through the same stock than a shot in daylight. Push-processing in low light produces different grain and contrast than normal development.

SkyrimBridge enables this because it knows:
- Whether it's a 3200K interior or a 5500K exterior
- Whether the scene is high-key (bright day) or low-key (night)
- Whether combat is happening (drive contrast, desaturation)
- The actual ambient color of the light hitting the scene
- Weather state for atmosphere-reactive film response

A preset author can use these to:
- Shift the film characteristic curve's toe and shoulder per time-of-day
- Select different negative/print stock emulations per weather
- Drive bleach bypass intensity from combat state
- Adjust grain structure from exposure level (bright = fine, dark = coarse — matching real photochemistry)

None of this is possible from screen-space analysis alone. It requires knowing the game's actual state, which only SKSE can provide. And it all arrives through `ENBSetParameter`.

### Performance Optimization

SkyrimBridge reads GPU frame time via D3D11 timestamp queries (not from ENB's pipeline — from the device directly). It computes a quality governor that outputs a 0.0-1.0 scale factor. This value is pushed as a parameter via `ENBSetParameter`.

A shader author can use this to:
- Reduce AO sample count when the GPU is overloaded
- Skip expensive effects (volumetric fog, SSR) when frame budget is exceeded
- Scale blur kernel sizes down at high load
- Disable rain/frost lens effects when not raining (SB knows the actual weather — no wasted cycles on dry days)
- Skip god rays and cloud shadows in interiors (SB knows the player is inside)

This means better frame rates with no visual compromise. Effects run at full quality when the GPU has budget, and gracefully reduce when it doesn't. All driven by data that arrives through your API.

### Weather Parameter Computer (Already Built and Shipping)

SkyrimBridge includes a complete INI-driven weather interpolation system. `WeatherParams.ini` defines per-weather values for 16 shader parameters across 9 weather categories (Clear, Cloudy, Foggy, Rain, ThunderRain, Snow, Blizzard, Ash, Special).

Every frame, SkyrimBridge reads the current weather from the engine, interpolates between categories during transitions, and pushes the results as `SB_WP_*` parameters through `ENBSetParameter`:

| Parameter | Controls |
|-----------|----------|
| `SB_WP_BloomIntensity` | Per-weather bloom strength |
| `SB_WP_AdaptSpeed` | Per-weather eye adaptation rate |
| `SB_WP_Saturation` | Per-weather color saturation |
| `SB_WP_Contrast` | Per-weather scene contrast |
| `SB_WP_ColorTemperature` | Per-weather white balance shift |
| `SB_WP_Sharpening` | Per-weather CAS intensity |
| `SB_WP_GrainIntensity` | Per-weather film grain amount |
| `SB_WP_AOIntensity` | Per-weather ambient occlusion |
| `SB_WP_SSRIntensity` | Per-weather reflection intensity |
| `SB_WP_GodRayIntensity` | Per-weather light shaft strength |
| `SB_WP_DOFStrength` | Per-weather depth of field |
| `SB_WP_LensDirtIntensity` | Per-weather lens dirt amount |
| `SB_WP_RainOnLens` | Per-weather rain droplet density |
| `SB_WP_FrostOnLens` | Per-weather frost vignette |
| `SB_WP_MistDensity` | Per-weather ground mist |
| `SB_WP_ExposureBias` | Per-weather exposure compensation |

The INI is hot-reloadable — a preset author can edit values while playing and see changes in 1-2 frames. Transitions between weathers use configurable smoothing with optional smoothstep.

All of this uses `ENBSetParameter` exclusively. Zero D3D11 interaction. It's just data flowing through the API you already built.

---

## The Options

With the GPU pipeline already removed, the remaining question is how game state reaches ENB shaders, and whether the shader cache stays.

### Option A: Pure ENBSetParameter

We push 122 float4 parameters per frame through `ENBSetParameter` to 9 shader files. That's ~1098 calls per frame using your API exactly as designed.

We optimize this path:
- Parameters that don't change frame-to-frame are skipped (quest data, equipment, UI state rarely change — camera and weather change every frame)
- Dirty tracking reduces the typical per-frame call count to ~200
- All calls go through your SDK — you control what happens with them

**What you gain:**
- Shader authors get reliable, named game state parameters without writing heuristic detection code. Instead of 50 different preset authors each writing their own "is it raining?" heuristic (some of which will conflict with your pipeline), they all read `SB_Precipitation.y` through your own parameter system.
- Better presets make ENBSeries more attractive. Weather-reactive bloom, film-grade color grading, combat-aware visual effects, adaptive performance scaling — all driven by ground-truth data instead of guesses. These are the features that show up in YouTube comparison videos and Nexus download pages.
- The WeatherParameterComputer alone adds 16 per-weather shader controls via your API — bloom, contrast, saturation, color temp, AO, SSR, and more — automatically interpolated during weather transitions. Preset authors configure it through an INI file. No shader code changes needed, no D3D11 hooks involved.
- You don't have to do anything. No code changes on your side. No slots reserved. No hooks to worry about. We use your public API and stay out of your way.
- If our `ENBSetParameter` calls ever cause any issue, you tell us and we fix it. Or you can block our parameters. We're on your API, so you have full control.

---

### Option B: ENBSetParameter + Shader Cache

Everything from Option A, plus the D3DCompile shader cache.

The shader cache works at the compiler DLL level, not D3D11:
1. Hooks `D3DCompile` via IAT on `d3dcompiler_47.dll`
2. Hashes source + defines + entry point + target profile
3. Returns cached bytecode if the exact same compilation was seen before; otherwise compiles normally and caches the result
4. Bytecode is byte-for-byte identical — the compiler just doesn't run again

This eliminates the 5-15 second shader compilation that happens on every game launch. ENBSeries currently recompiles all `.fx` files from source each time the game starts. With the cache, the second launch (and every launch after) skips that wait entirely.

**What you gain:**
- Everything from Option A.
- Every ENBSeries user gets noticeably faster game startup. This is the single most common complaint about ENB in casual user forums: "why does it take so long to load?" The cache eliminates that.
- The cache is transparent to your code. You call `D3DCompile`, you get bytecode back. Whether it came from the compiler or from disk is invisible to ENB. Your shaders work exactly the same way.
- If a user edits their `.fx` files, the hash changes and the cache misses — it recompiles normally. No stale bytecode, no manual cache clearing needed.
- If you ever build your own shader cache, we'll detect it and disable ours automatically. No conflict.

**What it costs you:**
- An IAT hook on `d3dcompiler_47.dll`. This does not touch the D3D11 device, context, or swap chain. It intercepts the compiler, not your rendering pipeline. If you'd rather we not hook the compiler DLL at all, we'll remove it — but this is the one feature that directly and measurably benefits every ENBSeries user for free.

---

## What We'd Like From You

Nothing, if neither option sounds right. We'll implement Option A (pure ENBSetParameter, zero hooks beyond Present) and stay out of your way.

But if you're open to it, two small things would help shader authors:

1. **Confirmation that ENBSetParameter with ~100-200 calls per frame** (after dirty-tracking optimization) is fine from your side. If there's a call budget or a preferred batching pattern, we'll follow it.

2. **If the shader cache (Option B) sounds acceptable**, a quick "yes, that's fine" is all we need. If not, we drop it.

That's it. No slot reservations, no API changes, no work on your side.

---

## Our Recommendation

**Option B.** Your users get faster startup. Your shader authors get weather-reactive, combat-aware, performance-adaptive presets built on ground-truth data instead of screen-space heuristics. Your pipeline is untouched. And if anything we do ever causes a problem, every part of SkyrimBridge can be disabled individually through its INI configuration without uninstalling anything.

We're not trying to extend your software — we're trying to feed it data it can't get on its own, through the API you built for exactly that purpose. The quality of the presets that run on your engine benefits everyone.

— Zain
