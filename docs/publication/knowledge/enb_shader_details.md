# ENB Shader & Addon Details

## ENB Shader Architecture (updated 2026-03-06)
- **All 9 .fx shaders include `enbglobals.fxh`** (theme system) + `SkyrimBridge_CB.fxh` (SB extern params)
- **Theme system:** `enbglobals.fxh` -> `Helper/EotE_ThemeSystem.fxh` (8 presets, TF/TI/TB helpers)
- **Multi-pipeline:** `ui_PipelineMode` (0=Digital, 1=Film, 2=Hybrid, 3=Full, 4=Custom) gates FILM/GRADE stages
- **8 tonemappers:** Lin, Reinhard, Hejl, Hable, ACES, AgX, Lottes, Gran Turismo (Uchimura)
- Active SB integrations per shader:
  - **enbeffect.fx**: Sun white balance (SB_Atmos_Sunlight), lightning flash, feedback-driven contrast
  - **enbbloom.fx**: Feedback-adaptive threshold (SB_Computed_Luminance), weather-responsive bloom (fog/rain mip boost)
  - **enbdepthoffield.fx**: Crosshair-driven autofocus (SB_XHair_Info), underwater/menu DOF suppression
  - **enbadaptation.fx**: Torch adaptation bias (SB_HasTorchEquipped)
  - **enbsunsprite.fx**: Game sun color tinting (SB_Sun_Color), weather suppression (cloud/precip)
  - **enbunderwater.fx**: Game water color (SB_ShallowWaterColor), player submersion depth
  - **enblens.fx**: Menu suppression, lightning/precip intensity, feedback scaling, sun tint, wet dirt
  - **enbeffectpostpass.fx**: Fog color, lightning, wet surface
  - **enbeffectprepass.fx**: Snow cover, particle field, wind, ambient
- All features behind `[branch] if(ui_SB_*)` toggles + `SB_HasFeedback()` guards
- **enblens.fx output = lens effects ONLY** -- enbeffect.fx ADDS TextureLens to scene
- **enbbloom.fx output = bloom data ONLY** -- enbeffect.fx adds TextureBloom to scene
- **DOF CoC: use direct depth-difference formula**, NOT hyperfocal normalization

## Addon Wiring Architecture (as of 2026-03-03)
- **enblens.fx**: ProceduralLensDirt + ProceduralWeatherFX (rain droplets, frost vignette)
  - AMON compat: `#define Linear_Sampler Sampler1`, `Point_Sampler Sampler0`, inline VertexShaderInput
  - SB integration: menu suppression, lightning/precip/interior intensity, feedback scaling, sun tint, wet dirt
  - Weather FX use **delta encoding**: `modified - original` added to lens output (HDR float16 supports negative)
  - Rain driven by `SB_RainFlag() * SB_Precipitation.y`, frost by `SB_SnowFlag() * .y`
  - UI via SHADERGROUP 1 (weather detail), SHADERGROUP 99 (SB lens enhancements)
- **enbeffectpostpass.fx**: AtmosphericFog + CRTShader + CinematicFX (8 effects)
  - AMON compat: `#define Linear_Sampler smpLinear`, `Point_Sampler smpPoint`
  - 11 technique passes: Fog -> Diffusion -> Halation -> Leaks -> Weave -> Letterbox -> Anamorphic -> Vignette -> Damage -> CRT -> PostPass
  - All effects have `[branch] if(!UI*_Enable)` passthrough guards
  - Fog has SB integration built-in (game fog color, lightning, wet surface)
  - UI via SHADERGROUPs 12-22 from enbUI_Lens.fxh
- **EotE_Common.fxh CANNOT be included in host .fx files** -- redeclares `static const float2 PixelSize` (conflicts with host). Use inline AMON compat defines instead.
- **enbeffectprepass.fx**: 4 AMON PrePass addons (Snow, Photo, Style, Particles)
  - NO AMON compat needed -- addons use native prepass names (smpLinear, smpPoint, PixelSize, GetLinearDepth, etc.)
  - SB integration: SnowCover (SB_Precip_Surface.z), ParticleField (SB_Weather_Flags, SB_Wind, SB_Time, SB_Atmos_Ambient)
  - PrePassAddonTechniques.fxh codegen: generates technique blocks 3+ for all 16 on/off combinations
  - Technique names: `KitsuunePrePass3+` (coexists with `EotE_PrePass0-2`)
- **No SkyrimBridge_AddonCompat.fxh** -- replaced by per-host inline `#define` blocks

## ENB Technique Naming Rules (CRITICAL -- verified 2026-03-04)
- **Multi-technique pipeline (ALL shaders):** Techniques MUST use sequential naming: `BaseName`, `BaseName1`, `BaseName2`, etc. Only base technique should have `UIName`. Sub-techniques without UIName run automatically as part of chain. Different base names = separate effects.
- **CRITICAL: RenderTarget annotations ONLY work on base techniques (those with UIName).** Sub-techniques without UIName ALWAYS write to TextureColor regardless of any RenderTarget annotation.
- **enbeffectprepass.fx:** Single technique `EotE_PrePass` (base, UIName) with ALL effects inlined in PS_PrePass.
- **enbdepthoffield.fx:** MUST use `ReadFocus` and `Focus` as first two technique names (hardcoded in ENB). UIName goes on 3rd+ technique.
- **enbadaptation.fx:** MUST use `Downsample` and `Draw` technique names
- **enbbloom.fx:** Custom technique names work fine
- **enblens.fx:** Custom names work BUT requires full texture declarations
- **DOF VS input:** Struct-based VS must use `float3 pos : POSITION` input (not SV_POSITION)
- **Sampler naming:** `Sampler0`/`Sampler1` or `smpPoint`/`smpLinear` both work

## ENB ScreenSize Convention (verified 2026-03-04)
- `ScreenSize.x` = width, `.y` = 1/width, `.z` = aspect (w/h), `.w` = 1/aspect (h/w)
- `PixelSize = float2(ScreenSize.y, ScreenSize.y * ScreenSize.z)` = (1/width, 1/height)
- `ScreenRes = float2(ScreenSize.x, ScreenSize.x * ScreenSize.w)` = (width, height)
- `FieldOfView` is in **degrees**
- Phase functions: use raw HG formula WITHOUT 4pi normalization (SchlickPhase ~250x too small)
