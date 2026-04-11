# ENB Weather Config & Preset Overlay System (KreatE / Elder ENB)

## ENB Native Weather System (built into d3d11.dll by Boris)

### `_weatherlist.ini` — Master weather mapping
- Format: `[WEATHERNNNN]` sections with `FileName=Weather/Name.ini` and `WeatherIDs=hexFormID`
- ENB reads this at runtime to find which weather config to apply based on current weather form ID
- Elder ENB has 612 weather-specific .ini files

### `Weather/*.ini` — Per-weather parameter overrides
- Sections match ENB's built-in categories: BLOOM, LENS, SKY, ENVIRONMENT, SKYLIGHTING, OBJECT, VEGETATION, VOLUMETRICFOG, FIRE, PARTICLE, WATER, RAYS, SUNGLARE, RAIN, SNOW, SSAO_SSIL, UNDERWATER, etc.
- 8 ToD slots per parameter: Dawn, Sunrise, Day, Sunset, Dusk, Night, InteriorDay, InteriorNight
- Value types: float (`IntensityDay=0.45`), RGB color (`ColorFilterDawn=1, 1, 1`)
- Named by weather EditorID (e.g., `Clear.ini`, `SkyrimCloudy.ini`) not by FormID

### `_locationweather.ini` — Location-to-weather mapping
- Format: `[WorldspaceHexID]` section with `LocationHexID=WeatherHexID` pairs
- Allows location-specific visual presets (Blackreach, Soul Cairn, etc.)
- Interiors use worldspace `00000000`

### `Interiors/*.ini` — Per-interior ENB configs
- Named by location (e.g., `Blackreach.ini`, `HighHrothgar.ini`)

## ENB Preset Overlay System (Boris feature, ENB v504+)

### `enbpresetoverlays.ini` — Master config
- `[PRESETOVERLAYS]` Active = (current overlay name)
- `[BLACKLISTENTRY*]` FileName/Category pairs to exclude from overlay modification

### `enbpresetoverlays/*.ini` — Individual overlay definitions
```ini
[OVERLAYINFO]
UIName = ENV - Alternative Baseline 1
UIGroups = 14 - Environment Lighting
UIDesc = Description for ENB GUI
UIOrdering = 0
NeedsNoApply = false
IncompatibleOverlays =
AmountMin = 0.00
AmountMax = 1.00

[OVERLAYPARAM1]
FileName = enbseries.ini
Category = VOLUMETRICFOG
Name = IntensityDawn
Operation = "+ -0.35"
```

### Operations
- `"= value"` — set to fixed value
- `"+ value"` — add (negative for subtract)
- `"* value"` — multiply
- Amount slider (0-1) blends between original and modified value

### Overlay categories in Elder ENB
- ENV (environment lighting presets): Alt Baseline, Cinematic Drama, Fantasy Vivid, etc.
- QUALITY (performance tiers): Low/Medium/High/Very High/Ultra/Extreme
- PREFERENCES: Disable DOF, NoSharp, etc.

## Comparison with SkyrimBridge WeatherSeparationEngine
- ENB native system: Works on ENB built-in params (BLOOM, SKY, ENVIRONMENT, etc.)
- SB WeatherSeparationEngine: Works on custom shader extern params (SB_* float4s)
- Both use per-weather .ini files with ToD slots
- ENB uses 8 ToD slots; SB supports 4-slot and 6-slot
- ENB overlays use additive/multiplicative operations; SB uses direct value override + weather transition blend
