# SkyrimBridge v3.0 — ENB Integration Reference

Technical reference for ENBSeries backend integration.

---

## 1. What SkyrimBridge Does

SkyrimBridge is an SKSE plugin that reads Skyrim's engine state via CommonLibSSE-NG and delivers 122 float4 parameters (1952 bytes, 22 domains) to ENB shaders every frame. It replaces enbParmLink.dll, NativeEditorID Fix, and ENBHelper with a single integrated solution.

Two binaries:
- `SkyrimBridge_v3.dll` — SKSE plugin (C++23, CommonLibSSE-NG). Collects game state, pushes to ENB.
- `SkyrimBridge_ENB.dllplugin` — ENB external plugin (C++17, zero game deps). Reads shared memory, serves via ENBGetParameter.

---

## 2. ENB SDK Functions Used

### Initialization (kPostLoad)

```cpp
// Resolve from enbseries.dll / d3d11.dll
auto ENBGetSDKVersion = GetProcAddress(mod, "ENBGetSDKVersion");      // -> long
auto ENBGetVersion    = GetProcAddress(mod, "ENBGetVersion");          // -> long
auto ENBSetCallback   = GetProcAddress(mod, "ENBSetCallbackFunction"); // -> void(callback)
auto ENBSetParameter  = GetProcAddress(mod, "ENBSetParameter");        // -> int
auto ENBGetParameter  = GetProcAddress(mod, "ENBGetParameter");        // -> int
```

### Per-Frame Push (OnENBFrame callback, type=1)

```cpp
struct ENBParameter {
    unsigned char Data[16];  // float4 value at offset 0x00
    int           Size;      // = 16 at offset 0x10
    int           Type;      // = 6 (ENBParam_COLOR4) at offset 0x14
};

// For each of 122 parameters, for each of 9 target shaders:
ENBParameter param;
memcpy(param.Data, &float4Value, 16);
param.Size = 16;
param.Type = 6;  // ENBParam_COLOR4
ENBSetParameter(NULL, "ENBBLOOM.FX", "SB_Sun_NDC", &param);
```

### Target Shaders (UPPERCASE required)

```
ENBSUNSPRITE.FX      ENBEFFECTPREPASS.FX   ENBEFFECT.FX
ENBEFFECTPOSTPASS.FX  ENBLENS.FX            ENBUNDERWATER.FX
ENBDEPTHOFFIELD.FX    ENBBLOOM.FX           ENBADAPTATION.FX
```

Total: 122 params x 9 shaders = 1098 ENBSetParameter calls per frame.
(124 push table entries including 2 convenience aliases = 1116 actual calls.)

---

## 3. Shared Memory Binary Layout

### Named Objects

```
File Mapping:  L"SkyrimBridge_GameState"
Event:         L"SkyrimBridge_DataReady"   (signaled after each WriteFrame)
```

### SB_SharedData (2144 bytes total)

```
Offset  Size   Type              Field
──────  ─────  ────────────────  ─────────────────────────────────────
0x0000  4      uint32            magic = 0x53423031 ('SB01')
0x0004  4      uint32            version = 1
0x0008  4      uint32            structSize = sizeof(SB_SharedData)
0x000C  4      uint32            frameCount (monotonic)
0x0010  4      float             deltaTime (seconds)
0x0014  4      float             gameHour [0,24)
0x0018  4      uint32            weatherFormID (TESWeather::formID)
0x001C  1      uint8             weatherCategory (enum)
0x001D  1      uint8             isInterior (0/1)
0x001E  1      uint8             isInMenu (0/1)
0x001F  1      uint8             isLoading (0/1)
0x0020  4      float             transitionPct [0,1]
0x0024  28     uint8[28]         padding (align to 64 bytes)
────── HEADER END (64 bytes) ──────

0x0040  1952   AllData           Game state (122 float4s)
────── AllData breakdown: ──────
0x0040  128    CelestialData     8 float4s: sun/moon NDC, direction, color, time
0x00C0  128    AtmosphereData    8 float4s: sky gradient, ambient, sunlight, clouds
0x0140  64     FogData           4 float4s: near/far fog, density, height
0x0180  96     WeatherData       6 float4s: wind, precip, lightning, flags, transition, surface
0x01E0  80     PlayerData        5 float4s: position, vitals, movement, combat, water
0x0230  368    CameraData        23 float4s: info, angles, pos + 5 matrices
0x03A0  96     InteriorData      6 float4s: flags, ambient, directional, fog
0x0400  48     ShadowData        3 float4s: direction, diffuse, ambient
0x0430  64     EffectsData       4 float4s: vision, time, damage, misc
0x0470  32     RenderData        2 float4s: frame info, TAA jitter
0x0490  96     ImageSpaceData    6 float4s: HDR, cinematic, DOF, IMOD
0x04F0  112    LightData         7 float4s: 3 nearest lights + summary
0x0560  128    ActorValueData    8 float4s: resistances, combat, skills
0x05E0  48     CrosshairData     3 float4s: target info, position, actor
0x0610  64     EquipmentData     4 float4s: right/left hand, armor, flags
0x0650  32     QuestData         2 float4s: progress, tracked quest
0x0670  48     UIStateData       3 float4s: menus, HUD, detail
0x06A0  64     FeedbackData      4 float4s: luminance, scene, stats, color
0x06E0  64     ThreatData        4 float4s: nearby hostiles, danger level, combat music, detection
0x0720  64     TimeSegmentData   4 float4s: dawn/sunrise/day/sunset, dusk/night/golden/blue, blend weights, reserved
0x0760  64     DepthData         4 float4s: linearization params, near/far planes, depth stats, reserved
0x07A0  64     QualityData       4 float4s: quality scale, weather override, dungeon flag, reserved

0x07E0  128    weatherParams     16 floats + 16 padding (expansion room)
────── END (0x0860 = 2144 bytes) ──────
```

### C Reader Example (zero dependencies)

```c
#include <windows.h>
#include <stdio.h>

int main() {
    HANDLE hMap = OpenFileMappingW(FILE_MAP_READ, FALSE, L"SkyrimBridge_GameState");
    if (!hMap) return 1;

    void* p = MapViewOfFile(hMap, FILE_MAP_READ, 0, 0, 2144);
    if (!p) { CloseHandle(hMap); return 1; }

    // Validate magic
    uint32_t magic = *(uint32_t*)p;
    if (magic != 0x53423031) { UnmapViewOfFile(p); CloseHandle(hMap); return 1; }

    // Read AllData (starts at offset 64)
    float* data = (float*)((char*)p + 64);

    // data[0..3]  = SB_Sun_NDC       (celestial.SunNDC)
    // data[4..7]  = SB_Sun_Direction  (celestial.SunDirection)
    // data[8..11] = SB_Sun_Color      (celestial.SunColor)
    // ...
    // data[420..423] = SB_Computed_SceneColor (feedback.SceneColor)

    printf("Game hour: %.1f\n", *(float*)((char*)p + 0x14));
    printf("Sun elevation: %.3f rad\n", data[3]);  // SB_Sun_NDC.w
    printf("Frame: %u\n", *(uint32_t*)((char*)p + 0x0C));

    UnmapViewOfFile(p);
    CloseHandle(hMap);
    return 0;
}
```

---

## 4. D3D11 Constant Buffer (register b7)

### Buffer Creation

```cpp
D3D11_BUFFER_DESC desc{};
desc.ByteWidth      = sizeof(AllData);  // 1952 bytes
desc.Usage          = D3D11_USAGE_DYNAMIC;
desc.BindFlags      = D3D11_BIND_CONSTANT_BUFFER;
desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
device->CreateBuffer(&desc, nullptr, &m_buffer);
```

### Per-Frame Update

```cpp
D3D11_MAPPED_SUBRESOURCE mapped;
ctx->Map(m_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
memcpy(mapped.pData, &allData, sizeof(AllData));
ctx->Unmap(m_buffer, 0);
ctx->PSSetConstantBuffers(7, 1, &m_buffer);
ctx->VSSetConstantBuffers(7, 1, &m_buffer);
```

### Slot Guard (PSSetConstantBuffers vtable hook)

SkyrimBridge hooks `ID3D11DeviceContext::PSSetConstantBuffers` (vtable index 8) to prevent anything from overwriting slot b7:

```cpp
// If a call would overwrite slot 7, split it:
//   Before: set slots [startSlot .. 6]
//   Skip:   slot 7 (guarded)
//   After:  set slots [8 .. endSlot]
```

Method: `VirtualProtect` + direct pointer swap on the vtable. No detours.

---

## 5. ENB External Plugin (SkyrimBridge_ENB.dllplugin)

Standalone DLL with **zero game dependencies** (C++17, Windows headers only). Loaded by ENBSeries from the `enbseries/` directory.

### Exported Functions

```cpp
extern "C" {
    long  __stdcall ENBGetSDKVersion();           // Returns 1000
    void  __stdcall ENBModuleInit(HMODULE mod);   // Open shared memory
    void  __stdcall ENBModuleDeInit();             // Close handles
    void  __stdcall ENBOnPreReset();              // Read shared memory each frame
    void  __stdcall ENBOnPostReset();             // (unused)
    BOOL  __stdcall ENBGetParameter(const char* name, void* outValue, int* outType);
}
```

### Data Flow

```
SkyrimBridge_v3.dll                    SkyrimBridge_ENB.dllplugin
    |                                      |
    | WriteFrame() ->                      |
    |   memcpy AllData to shared mem       |
    |   SetEvent("SkyrimBridge_DataReady") |
    |                                      |
    |                                      | ENBOnPreReset() ->
    |                                      |   memcpy shared mem to local cache
    |                                      |
    |                                      | ENBGetParameter("SB_Sun_NDC", ...) ->
    |                                      |   lookup in kParamTable
    |                                      |   memcpy 16 bytes to outValue
    |                                      |   *outType = 3 (ENB_PARAM_FLOAT4)
```

---

## 6. Proposals for Native ENB Support

### 6.1 Dedicated Constant Buffer Slot

**Current:** SB occupies register(b7) by hooking `PSSetConstantBuffers` vtable — fragile, may conflict with other mods.

**Proposed:** ENB reserves a cbuffer slot (e.g., b13) for external game state data. External plugins call `ENBSetConstantBufferData(void* data, uint32_t size)` once per frame. ENB binds it automatically.

**Benefit:** Eliminates vtable hook. One memcpy per frame instead of 1116 API calls. With the expanded 22-domain scope (threat awareness, time-of-day segments, depth linearization, quality scaling), the per-frame call count will only continue to grow — a bulk path is increasingly justified.

### 6.2 Bulk Parameter API

**Current:** 1116 `ENBSetParameter` calls per frame (124 params x 9 shaders).

**Proposed:** `ENBSetParameterBlock(const char* shader, const ParamEntry* entries, int count, const void* data)` — single call pushes all parameters for a shader.

**Alternative:** ENB reads `SkyrimBridge_GameState` shared memory directly — zero API calls needed.

### 6.3 UAV Whitelisting for Material System

**Current:** SB patches BSLightingShader bytecode to inject UAV writes at u4 for per-pixel material classification. Hooks `CreatePixelShader`, `PSSetShader`, and `DrawIndexed`.

**Proposed:** ENB provides a material ID channel natively (u4 or u5) during opaque pass. External plugins register material classifiers. ENB shaders read material IDs without bytecode patching.

### 6.4 Native Shared Memory Consumer

**Current:** SkyrimBridge_ENB.dllplugin reads shared memory and serves it via ENBGetParameter.

**Proposed:** ENB reads `SkyrimBridge_GameState` directly (validated by magic 0x53423031 + version). Eliminates the need for the .dllplugin entirely. With 22 domains now covering threat awareness, time-of-day segmentation, depth linearization, and quality scaling, the shared memory approach becomes even more attractive — the data set is self-contained and richly structured for direct consumption.

---

## 7. EditorID Cache (GetFormEditorID Export)

SkyrimBridge exports `GetFormEditorID` for other SKSE plugins:

```cpp
extern "C" __declspec(dllexport)
const char* GetFormEditorID(uint32_t formID);
```

API-compatible with NativeEditorID Fix and po3_Tweaks. Backed by vtable hooks on 80+ form types that cache editor IDs the engine discards at runtime. Also populates the engine's `editorID -> form` map (enables console `help` for all forms).

---

## 8. Compatibility Detection

At `kPostLoad`, SkyrimBridge checks for loaded modules:

| Module | Detection | Behavior |
|--------|-----------|----------|
| `NativeEditorIDFix.dll` | `GetModuleHandleW` | Skip EditorID hooks, proxy via `GetFormEditorID` export |
| `NativeEditorIDFixNG.dll` | `GetModuleHandleW` | Same |
| `po3_Tweaks.dll` | `GetModuleHandleW` | Same (lower priority than NativeEditorID Fix) |
| `enbParmLink.dll` | `GetModuleHandleW` | Skip ParmLinkCompat expression evaluator |

In-game notification: *"SkyrimBridge: compatible with [names] (safe to remove)"*

---

## 9. Build System

### Dependencies (vcpkg, x64-windows-static)

| Package | Version | Used By |
|---------|---------|---------|
| CommonLibSSE-NG | 3.7.0 | SKSE plugin (game API) |
| ImGui | 1.91.9 | Debug overlay |
| spdlog | 1.16.0 | Logging |
| fmt | 12.1.0 | String formatting |

### Build Commands

```cmd
cmake -B build -S . ^
  -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows-static
cmake --build build --config Release
```

Output:
- `build/Release/SkyrimBridge_v3.dll` (SKSE plugin)
- `build/Release/SkyrimBridge_ENB.dll` -> renamed to `.dllplugin`

### Two Targets

```cmake
add_commonlibsse_plugin(SkyrimBridge_v3 ...)    # C++23, SKSE + CommonLibSSE
add_library(SkyrimBridge_ENB SHARED ...)         # C++17, Windows-only
```

---

## 10. PapyrusBridge (Native Function API for Mod Authors)

SkyrimBridge v3.1 exposes a Papyrus native function API so mod authors can push custom data into the shader pipeline without writing any C++.

### Registered Papyrus Functions

```papyrus
; Set a custom float4 value by parameter name (persists until overwritten)
Function SkyrimBridge_SetFloat4(String paramName, Float x, Float y, Float z, Float w) Global Native

; Get the current value of any SB parameter
Float[] Function SkyrimBridge_GetFloat4(String paramName) Global Native

; Query game state exposed by SkyrimBridge
Bool Function SkyrimBridge_IsActive() Global Native
Int  Function SkyrimBridge_GetDomainCount() Global Native
Int  Function SkyrimBridge_GetParamCount() Global Native

; Override weather data for scripted sequences
Function SkyrimBridge_SetWeatherOverride(Bool enable, Float transitionTime) Global Native

; Push custom threat/danger flags from quest scripts
Function SkyrimBridge_SetThreatLevel(Float level) Global Native
Function SkyrimBridge_SetDangerFlag(Bool inDanger) Global Native
```

### Usage Example (Papyrus)

```papyrus
; In a quest script — pulse a red vignette during a boss fight
Event OnCombatStateChanged(Actor akTarget, Int aeCombatState)
    if aeCombatState == 1
        SkyrimBridge_SetThreatLevel(1.0)
        SkyrimBridge_SetDangerFlag(true)
    else
        SkyrimBridge_SetThreatLevel(0.0)
        SkyrimBridge_SetDangerFlag(false)
    endif
EndEvent
```

Mod authors register their scripts via the standard Papyrus native binding system. SkyrimBridge handles thread safety and ensures values reach shaders within 1 frame.

---

## 11. Complete Parameter Table (122 float4s)

| # | Parameter Name | Domain | .x | .y | .z | .w |
|---|---------------|--------|----|----|----|----|
| 1 | SB_Sun_NDC | Celestial | NDC x | NDC y | onScreen | elevation (rad) |
| 2 | SB_Sun_Direction | Celestial | dir x | dir y | dir z | angular radius |
| 3 | SB_Sun_Color | Celestial | R | G | B | glare factor |
| 4 | SB_Masser_NDC | Celestial | NDC x | NDC y | onScreen | phase brightness |
| 5 | SB_Masser_Direction | Celestial | dir x | dir y | dir z | elevation |
| 6 | SB_Secunda_NDC | Celestial | NDC x | NDC y | onScreen | phase brightness |
| 7 | SB_Secunda_Direction | Celestial | dir x | dir y | dir z | elevation |
| 8 | SB_Time | Celestial | gameHour | sunrise | sunset | dayProgress |
| 9-16 | SB_Atmos_* | Atmosphere | (8 params, see PARAMETER_REFERENCE.md) |||
| 17-20 | SB_Fog_* | Fog | (4 params) |||
| 21-26 | SB_Wind, SB_Precipitation, SB_Lightning, SB_Weather_*, SB_Precip_* | Weather | (6 params) |||
| 27-31 | SB_Player_* | Player | (5 params) |||
| 32-54 | SB_Camera_*, SB_View_*, SB_Proj_*, SB_ViewProj_*, SB_PrevVP_*, SB_InvVP_* | Camera | (23 params) |||
| 55-60 | SB_Interior_* | Interior | (6 params) |||
| 61-63 | SB_Shadow_* | Shadow | (3 params) |||
| 64-67 | SB_FX_* | Effects | (4 params) |||
| 68-69 | SB_Render_* | Render | (2 params) |||
| 70-75 | SB_IS_* | ImageSpace | (6 params) |||
| 76-82 | SB_Light*_*, SB_Light_Summary | Lights | (7 params) |||
| 83-90 | SB_AV_* | ActorValues | (8 params) |||
| 91-93 | SB_XHair_* | Crosshair | (3 params) |||
| 94-97 | SB_Equip_* | Equipment | (4 params) |||
| 98-99 | SB_Quest_* | Quest | (2 params) |||
| 100-102 | SB_UI_* | UI State | (3 params) |||
| 103 | SB_Computed_Luminance | Feedback | smoothed lum | instant lum | center R | center G |
| 104 | SB_Computed_Scene | Feedback | center B | sceneAvgLum | lumRange | feedbackValid |
| 105 | SB_Computed_SceneStats | Feedback | keyValue | contrastRatio | peripheryLum | center/periph |
| 106 | SB_Computed_SceneColor | Feedback | avg R | avg G | avg B | colorTemp (K) |
| 107 | SB_Threat_Hostiles | Threat | nearestDist | hostileCount | avgHostileDist | maxThreatLevel |
| 108 | SB_Threat_Danger | Threat | dangerFlag | threatLevel | combatMusic | detectionLevel |
| 109 | SB_Threat_NPC0 | Threat | posX | posY | posZ | aggroLevel |
| 110 | SB_Threat_NPC1 | Threat | posX | posY | posZ | aggroLevel |
| 111 | SB_TimeSeg_Primary | TimeSegment | dawn | sunrise | day | sunset |
| 112 | SB_TimeSeg_Secondary | TimeSegment | dusk | night | goldenHour | blueHour |
| 113 | SB_TimeSeg_Blend | TimeSegment | blendWeight0 | blendWeight1 | segmentIndex | transitionPct |
| 114 | SB_TimeSeg_Reserved | TimeSegment | reserved | reserved | reserved | reserved |
| 115 | SB_Depth_Params | Depth | nearPlane | farPlane | linearCoeffA | linearCoeffB |
| 116 | SB_Depth_Stats | Depth | minDepth | maxDepth | avgDepth | medianDepth |
| 117 | SB_Depth_Planes | Depth | projA | projB | invFarPlane | reserved |
| 118 | SB_Depth_Reserved | Depth | reserved | reserved | reserved | reserved |
| 119 | SB_Quality_Scale | Quality | qualityScale | lodBias | shadowQuality | ssaoQuality |
| 120 | SB_Quality_State | Quality | weatherOverride | isDungeon | isExterior | cellLoadPct |
| 121 | SB_Quality_Perf | Quality | gpuTime | cpuTime | frameRate | frameBudget |
| 122 | SB_Quality_Reserved | Quality | reserved | reserved | reserved | reserved |
