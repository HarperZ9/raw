# SB_ShaderDebug — Shader Compilation Diagnostic Tool
## Integration Guide

---

## What It Does

Hooks every call to `D3DCompile` / `D3DCompile2` made by ENB (or any other D3D11 shader compiler in the process), and when a shader fails to compile:

1. **Captures** the error blob from the HLSL compiler
2. **Parses** each error into structured records: filename, line number, column, error code (e.g., `X3004`), human-readable message
3. **Extracts source snippets** — the actual HLSL code around each error line (±3 lines by default)
4. **Writes a formatted log file** to `Data/SKSE/Plugins/SkyrimBridge_ShaderErrors.log`
5. **Renders an in-game overlay** showing all errors with color-coded severity, line numbers, error codes, and source context
6. **Infers source filenames** from ENB naming conventions when the compiler doesn't provide them

---

## Architecture

```
                    ┌──────────────────────────────────────────┐
                    │           d3dcompiler_47.dll             │
                    │                                          │
                    │  D3DCompile() ──── IAT Hook ────┐       │
                    │  D3DCompile2() ─── IAT Hook ──┐ │       │
                    └──────────────────────────────┼─┼───────┘
                                                   │ │
                    ┌──────────────────────────────▼─▼───────┐
                    │         SB::Debug::ShaderDebug          │
                    │                                          │
                    │  HookD3DCompile()                        │
                    │    ├─ Call original D3DCompile            │
                    │    ├─ If FAILED:                          │
                    │    │   ├─ ParseErrorBlob()               │
                    │    │   │   ├─ regex: file(line,col)      │
                    │    │   │   ├─ regex: (line,col)          │
                    │    │   │   └─ regex: bare error          │
                    │    │   ├─ ExtractSnippet() from source   │
                    │    │   ├─ WriteLogEntry() → .log file    │
                    │    │   └─ Auto-show overlay              │
                    │    └─ Return original HRESULT            │
                    │                                          │
                    │  HookPresent()                            │
                    │    ├─ ProcessInput() (hotkeys)            │
                    │    ├─ RenderOverlay()                     │
                    │    │   ├─ DrawRect() background/headers   │
                    │    │   ├─ DrawText() error entries        │
                    │    │   └─ Upload VB → Draw()             │
                    │    └─ Call original Present               │
                    └──────────────────────────────────────────┘
```

---

## File Inventory

| File | Purpose |
|------|---------|
| `SB_ShaderDebug.h` | Header — `ShaderError`, `CompilationAttempt`, `OverlayConfig`, `ShaderDebug` singleton |
| `SB_ShaderDebug.cpp` | Implementation — hooks, parsing, logging, D3D11 overlay rendering |

---

## Integration Into Your SKSE Plugin

### Step 1 — Add Files to Your Build

Add both `SB_ShaderDebug.h` and `SB_ShaderDebug.cpp` to your CMakeLists.txt or vcxproj alongside `ShaderHooks.h/cpp`.

Required link libraries (should already be present):
```
d3d11.lib
d3dcompiler.lib
dxgi.lib
```

### Step 2 — Install During Plugin Init

In your SKSE plugin's `SKSEPlugin_Load` or equivalent initialization:

```cpp
#include "SB_ShaderDebug.h"

// After you've obtained the D3D11 device, context, and swap chain
// (typically from hooking CreateDeviceAndSwapChain, or from ENB's API,
// or from the game's BSGraphics::Renderer singleton):

void OnD3D11Ready(ID3D11Device* device,
                  ID3D11DeviceContext* context,
                  IDXGISwapChain* swapChain)
{
    // Install your existing ShaderHooks (draw interception)
    SB::Render::ShaderHooks::Get().Install(context);

    // Install ShaderDebug (compilation diagnostics)
    SB::Debug::ShaderDebug::Get().Install(device, context, swapChain);
}
```

### Step 3 — Obtaining the D3D11 Device

If you don't already have access to the D3D11 objects, there are several approaches:

**Option A — From BSGraphics::Renderer (recommended for Skyrim SE)**

```cpp
#include <RE/Skyrim.h>

void InstallAfterRenderer()
{
    auto* renderer = RE::BSGraphics::Renderer::GetSingleton();
    if (!renderer) return;

    // These are the raw D3D11 pointers from the game's renderer
    auto* device  = renderer->GetRuntimeData().forwarder;
    auto* context = renderer->GetRuntimeData().context;

    // For the swap chain, hook IDXGIFactory::CreateSwapChain
    // or retrieve from the renderer's internal state
    IDXGISwapChain* swapChain = /* your swap chain pointer */;

    SB::Debug::ShaderDebug::Get().Install(device, context, swapChain);
}
```

**Option B — Hook CreateDeviceAndSwapChain**

```cpp
// Hook D3D11CreateDeviceAndSwapChain to capture all three at creation time
static HRESULT WINAPI HookCreateDevice(
    IDXGIAdapter* pAdapter, D3D_DRIVER_TYPE DriverType,
    HMODULE Software, UINT Flags,
    const D3D_FEATURE_LEVEL* pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC* pSwapChainDesc,
    IDXGISwapChain** ppSwapChain, ID3D11Device** ppDevice,
    D3D_FEATURE_LEVEL* pFeatureLevel, ID3D11DeviceContext** ppContext)
{
    HRESULT hr = origCreateDevice(pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion, pSwapChainDesc,
        ppSwapChain, ppDevice, pFeatureLevel, ppContext);

    if (SUCCEEDED(hr) && ppDevice && ppContext && ppSwapChain)
    {
        SB::Debug::ShaderDebug::Get().Install(*ppDevice, *ppContext, *ppSwapChain);
    }

    return hr;
}
```

### Step 4 — Shutdown (Optional)

```cpp
void OnPluginUnload()
{
    SB::Debug::ShaderDebug::Get().Shutdown();
}
```

---

## Hooking Strategy: IAT Patching vs. Inline Hooks

The implementation provides two hooking mechanisms:

### IAT Patching (Default)

Scans the import address table of every loaded module, finds imports from `d3dcompiler_47.dll`, and redirects them to our hook. This is safe and doesn't modify code memory, but only catches imports that go through the IAT (most do).

### Inline Hooks (Optional, Higher Coverage)

For maximum coverage (catching dynamically resolved `GetProcAddress` calls), replace the `InstallInlineHook` template with your preferred hooking library:

**MinHook** (recommended):
```cpp
#include <MinHook.h>

template<typename FnPtr>
static void InstallInlineHook(FnPtr target, FnPtr hook, FnPtr& original)
{
    MH_CreateHook(reinterpret_cast<void*>(target),
                  reinterpret_cast<void*>(hook),
                  reinterpret_cast<void**>(&original));
    MH_EnableHook(reinterpret_cast<void*>(target));
}
```

**Microsoft Detours**:
```cpp
#include <detours.h>

template<typename FnPtr>
static void InstallInlineHook(FnPtr target, FnPtr hook, FnPtr& original)
{
    original = target;
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(reinterpret_cast<void**>(&original),
                 reinterpret_cast<void*>(hook));
    DetourTransactionCommit();
}
```

---

## Error Parsing Details

The parser handles all D3DCompile error blob formats:

### Format 1: Full location
```
enbeffect.fx(142,17-28): error X3004: undeclared identifier 'SB_Fog_Color'
```
→ `file="enbeffect.fx"`, `line=142`, `col=17`, `code="X3004"`, `message="undeclared identifier 'SB_Fog_Color'"`

### Format 2: Line only
```
enbeffect.fx(142): error X3004: undeclared identifier 'SB_Fog_Color'
```
→ `file="enbeffect.fx"`, `line=142`, `col=-1`

### Format 3: No filename (common for #include'd files)
```
(87,5): error X3018: invalid subscript 'xyz'
```
→ `file=<source name from D3DCompile>`, `line=87`, `col=5`

### Format 4: Bare error (linker errors, etc.)
```
error X4000: variable 'SB_Weather_Flags' used but not assigned
```
→ `file=<source name>`, `line=-1`, `col=-1`

### Common ENB Error Codes

| Code | Meaning | Common Cause |
|------|---------|-------------|
| `X3000` | Syntax error | Missing semicolon, mismatched braces |
| `X3004` | Undeclared identifier | Missing `#include`, typo in parameter name |
| `X3013` | Too many arguments | Wrong function signature |
| `X3017` | Cannot implicitly convert | Type mismatch (float3 vs float4) |
| `X3018` | Invalid subscript | `.xyzw` on wrong type |
| `X3025` | Global variables not allowed | Trying to write to a cbuffer slot |
| `X3078` | Recursive #include | Circular header dependency |
| `X4000` | Used but not assigned | KeepAlive issue — parameter dead-stripped |
| `X4502` | Too many instructions | Shader exceeds instruction limit |

---

## Log File Format

```
╔══════════════════════════════════════════════════════════════════╗
║       SkyrimBridge — Shader Compilation Diagnostic Log         ║
╚══════════════════════════════════════════════════════════════════╝

  Session: 2026-02-28 18:45:03
  Log:     C:\Games\Skyrim SE\Data\SKSE\Plugins\SkyrimBridge_ShaderErrors.log

┌─ COMPILATION FAILED ─────────────────────────────────────────────
│  Time:    18:45:07
│  File:    enbeffect.fx
│  Entry:   PS_Draw
│  Profile: ps_5_0
│  Compile: 12.3 ms
│
│  ERRORS (2):
│
│  [1] enbeffect.fx(142,17): X3004 — undeclared identifier 'SB_Fog_Colorr'
│  [2] enbeffect.fx(287,5): X3018 — invalid subscript 'w'
│
│  SOURCE CONTEXT:
│
│      140 │     float3 fogColor = SB_Fog_FarColor.rgb;
│      141 │     float density = SB_Fog_Density.x;
│  >>> 142 │     float3 tint = SB_Fog_Colorr.rgb;   // <-- typo!
│      143 │     color.rgb = lerp(color.rgb, tint, density);
│      144 │
│
│      285 │     float4 result;
│      286 │     result.rgb = finalColor;
│  >>> 287 │     result.w = alpha;   // <-- should be .a for float4
│      288 │     return result;
│
│  RAW ERROR BLOB:
│    enbeffect.fx(142,17-28): error X3004: undeclared identifier 'SB_Fog_Colorr'
│    enbeffect.fx(287,5): error X3018: invalid subscript 'w'
└──────────────────────────────────────────────────────────────
```

---

## In-Game Overlay

The overlay renders directly to the backbuffer after the game frame is complete, using a self-contained D3D11 rendering pipeline:

- **Procedurally generated 8×8 bitmap font** — no external texture files required
- **Color-coded severity** — errors in red, warnings in yellow, filenames in green, line numbers in gold, error codes in purple
- **Source code snippets** — shows ±3 lines around each error with the error line highlighted
- **Scrollable** — Page Up/Down to navigate through many errors
- **Auto-show** — overlay appears automatically when the first error is captured
- **Hotkey toggle** — F10 to show/hide, F11 to clear all errors

### Overlay Colors (Customizable)

| Element | Default Color | Config Field |
|---------|--------------|-------------|
| Background | Dark blue-black (88% opacity) | `colorBg` |
| Header bar | Dark red | `colorHeaderBg` |
| Error text | Bright red | `colorError` |
| Warning text | Yellow | `colorWarning` |
| Filename | Green | `colorFilename` |
| Line number | Gold | `colorLineNum` |
| Error code | Purple | `colorCode` |
| Source code | Gray | `colorSourceLine` |
| Error line highlight | Translucent red | `colorSourceErr` |

---

## Configuration

All settings are adjustable at runtime through the `OverlayConfig` struct:

```cpp
auto& config = SB::Debug::ShaderDebug::Get().Config();

// Change hotkeys
config.toggleKey = VK_F9;

// Adjust panel size/position
config.panelX = 0.02f;  // 2% from left
config.panelY = 0.02f;  // 2% from top
config.panelW = 0.96f;  // 96% width
config.panelH = 0.50f;  // 50% height

// Font size
config.fontSize = 14.0f;

// Disable source snippets for a more compact view
config.showSourceSnippets = false;

// Disable auto-show (overlay only appears when manually toggled)
config.autoShow = false;
```

---

## API Reference

```cpp
namespace SB::Debug {

class ShaderDebug {
    // Lifecycle
    void Install(ID3D11Device*, ID3D11DeviceContext*, IDXGISwapChain*);
    void Shutdown();
    bool IsInstalled() const;

    // Error queries
    bool   HasErrors() const;
    bool   HasWarnings() const;
    size_t ErrorCount() const;
    size_t WarningCount() const;
    size_t TotalAttempts() const;

    const std::vector<CompilationAttempt>& GetAttempts() const;
    std::vector<const CompilationAttempt*> GetFailedAttempts() const;

    void ClearAll();

    // Overlay control
    void SetOverlayVisible(bool);
    void ToggleOverlay();
    bool IsOverlayVisible() const;
    void ScrollUp() / ScrollDown() / ScrollToTop() / ScrollToBottom();

    // Configuration
    OverlayConfig& Config();

    // Log
    void FlushLog();
    const std::filesystem::path& LogPath() const;
};

} // namespace SB::Debug
```
