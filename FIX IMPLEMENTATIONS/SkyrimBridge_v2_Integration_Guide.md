# SkyrimBridge v2.0 — Integration Guide

## The Problem: Why ENB Wasn't Receiving Data

SkyrimBridge.fxh v1 declared 103 `float4` parameters, but each shader only
referenced ~5 of them in actual pixel shader code. The HLSL compiler (fxc)
aggressively dead-strips unreferenced global variables from the compiled
Effect constant buffer.

**Result:** ~95% of parameters vanished from the compiled shader binary.
When the DLL called `ENBSetParameter("SB_Masser_NDC")`, ENB searched the
compiled Effect's reflection data, couldn't find the variable, and silently
returned FALSE. The data was computed correctly (visible in ImGui debug
overlay) but never reached the shaders.

```
SkyrimBridge DLL                    ENB Shader Compiler
┌─────────────────┐                 ┌──────────────────┐
│ Tracks 102 params│  ENBSetParam   │ Compiles .fx file│
│ per frame        │ ──────────────>│ Dead-strips unused│
│                  │  "SB_Wind"     │ variables         │
│ SB_Wind = 0.7   │  FAIL (FALSE)  │                   │
│ SB_Lightning=1.0 │                │ SB_Wind: REMOVED  │
│ ...              │  "SB_Lightning"│ SB_Lightning: KEPT│
│                  │  OK (TRUE)     │ (used in shader)  │
└─────────────────┘                 └──────────────────┘
```

## The Solution: KeepAlive Parameter Retention

SkyrimBridge.fxh v2 adds a `_SB_KeepAlive_Sink()` function that references
every single parameter. A wrapper function `SB_Retain(uv)` calls it through
a branch the compiler cannot eliminate (depends on runtime `Timer.x` value)
but the GPU never executes (the branch condition is always false at runtime).

```hlsl
// In SkyrimBridge.fxh:
float3 SB_Retain(float2 uv)
{
    [branch] if (Timer.x < -1.0e15)  // Never true, but compiler can't prove it
    {
        float4 sink = _SB_KeepAlive_Sink();  // References all 102 params
        return sink.rgb * uv.x * 0.0001;     // Per-pixel dependency prevents hoisting
    }
    return 0;  // GPU always takes this path — zero cost
}
```

**Cost at runtime: ZERO.** The branch prediction correctly predicts "not taken"
from the first frame onward. The parameter reads in the sink function are never
executed by the GPU. But the compiler must keep all 102 variables in the constant
buffer because it cannot prove the branch is dead.

## Integration Steps

### Step 1: Replace SkyrimBridge.fxh

Copy the new `Helper/SkyrimBridge.fxh` into your `enbseries/Helper/` directory,
replacing the v1 file.

### Step 2: Add SB_Retain() to Every Pixel Shader

In **every** `.fx` file that includes SkyrimBridge.fxh, add `SB_Retain(uv)` to
at least one pixel shader that runs every frame. The best candidate is the
final composite/output pass.

```hlsl
float4 PS_FinalComposite(VS_OUTPUT i) : SV_Target
{
    float3 color = TextureColor.Sample(SamplerLinear, i.uv).rgb;

    // ... your shader code ...

    // CRITICAL: Retain all SkyrimBridge parameters in constant buffer
    color += SB_Retain(i.uv);

    return float4(color, 1.0);
}
```

**Which shader to add it to, per file:**

| File                    | Add SB_Retain() to                        |
|-------------------------|-------------------------------------------|
| `enbeffectprepass.fx`   | PS_EffectsComposite (Tech 6, final pass)  |
| `enbeffect.fx`          | PS_Draw (the main composite technique)    |
| `enblens.fx`            | PS_LensFinal (last technique)             |
| `enbdepthoffield.fx`    | PS_DOFComposite (final composite pass)    |
| `enbsunsprite.fx`       | PS_SunFlare (main sun flare pass)         |
| `enbbloom.fx`           | PS_BloomComposite (final bloom pass)      |
| `enbadaptation.fx`      | PS_Adaptation (output pass)               |

### Step 3: Optional — Enable the Monitor Panel

To add a live data readout to the ENB GUI:

1. Set `#define SB_ENABLE_MONITOR 1` before including SkyrimBridge.fxh
2. Include `enbUI_SkyrimBridge.fxh` after the UI parameter section
3. Call `SB_UpdateMonitor()` in a pixel shader:

```hlsl
#define SB_ENABLE_MONITOR 1
#include "Helper/SkyrimBridge.fxh"
// ... (ENB external params) ...
#include "UI/enbUI_SkyrimBridge.fxh"

float4 PS_FinalComposite(VS_OUTPUT i) : SV_Target
{
    float3 color = ...;

    // Retain all params
    color += SB_Retain(i.uv);

    // Update monitor displays (only when enabled in GUI)
    if (_SBMon_Enable) SB_UpdateMonitor();

    return float4(color, 1.0);
}
```

The monitor variables appear in the ENB shader editor (Shift+Enter) under
a "SkyrimBridge Monitor v2.0" section. Toggle the enable checkbox to see
live values. If values remain at 0.0, the data pipeline is broken.

### Step 4: C++ Side — Enable Diagnostics

Replace direct `ENBSetParameter` calls with the diagnostic wrapper:

```cpp
#include "SB_ENBDiagnostics.h"

// In your ENB callback:
void WINAPI OnENBFrame(BOOL prePresent)
{
    if (!prePresent) return;

    // Update all trackers
    UpdateAllTrackers();

    // Push with diagnostics (replaces manual ENBSetParameter loop)
    SB::Diag::PushAllParams(&g_allData);
}

// During init:
SB::Diag::Init(ENBInterface::SetParameter);
```

Check `SkyrimBridge.log` for the diagnostic report:
```
╔═══════════════════════════════════════════════════╗
║  SkyrimBridge — First Frame Diagnostic Report    ║
╚═══════════════════════════════════════════════════╝
  enbeffectprepass.fx — 102 OK, 0 FAILED (of 102 params)
  enbeffect.fx — 102 OK, 0 FAILED (of 102 params)
  enblens.fx — 102 OK, 0 FAILED (of 102 params)
  enbdepthoffield.fx — 102 OK, 0 FAILED (of 102 params)
  enbsunsprite.fx — 102 OK, 0 FAILED (of 102 params)
  ────────────────────────────────────────────
  TOTAL: 510 OK / 0 FAIL (100.0% success rate)
```

If you see failures, the report identifies exactly which parameters
are being rejected by which shaders, making debugging trivial.

## Architecture Overview

```
┌─────────────────────────────────────────────────────┐
│                   SKSE Plugin (DLL)                  │
│                                                      │
│  Trackers ──> AllData struct ──> SB::Diag::PushAll  │
│  (17 domains)  (102 float4s)    │                    │
│                                  │                    │
│  SB_ENBDiagnostics.h            │                    │
│  ├─ Return value tracking       │                    │
│  ├─ First-frame report          │                    │
│  └─ Periodic summary            │                    │
└──────────────────────────────────┼────────────────────┘
                                   │ ENBSetParameter()
                                   │ per shader × per param
                                   ▼
┌─────────────────────────────────────────────────────┐
│                 ENB Effect Compiler                   │
│                                                      │
│  .fx file ──> HLSL compile ──> Constant Buffer       │
│  #include SkyrimBridge.fxh                           │
│  ├─ 102 float4 declarations (all present)            │
│  ├─ _SB_KeepAlive_Sink() references all of them     │
│  └─ SB_Retain(uv) ← called from pixel shader        │
│     └─ Timer.x branch prevents dead-stripping        │
│                                                      │
│  ENB binds float4 data to constant buffer slots      │
│  ✓ All 102 params survive compilation                │
└─────────────────────────────────────────────────────┘
                   │
                   ▼ SB_ values available in shader code
┌─────────────────────────────────────────────────────┐
│                  Pixel Shaders                       │
│                                                      │
│  SB_IsActive() ── guard clause for graceful fallback │
│  SB_GetFogColor(), SB_MotionVector(), etc.           │
│  SB_UpdateMonitor() ── optional ENB GUI readout      │
└─────────────────────────────────────────────────────┘
```

## UI Framework: Beyond ReforgedUI

The new `enbUI_Primer.fxh v2` provides:

| Feature                | ReforgedUI v1        | SkyrimBridge v2             |
|------------------------|----------------------|-----------------------------|
| Whitespace system      | ✓ (WHITESPACE_1-60)  | ✓ (same, with __LINE__ UIDs)|
| Pipe indentation       | ✓ (SPECIAL_WHITESPACE)| ✓ (compact set + __LINE__)  |
| File headers           | Basic                | Branded (title+sub+author+ver)|
| Section headers        | Manual               | UI_Section() / UI_SubSection()|
| Category labels        | Manual               | UI_Category() tree-style    |
| Separators             | None                 | Thin/Thick/Dotted           |
| DNI macros             | None                 | UI_DNI_FLOAT + DNI_LERP     |
| 7-TOD macros           | None                 | UI_7TOD_FLOAT + TOD7_LERP   |
| Parameter shortcuts    | None                 | UI_FLOAT/BOOL/INT/COLOR     |
| Tree-style params      | None                 | UI_FLOAT_TREE / UI_BOOL_TREE|
| Monitor displays       | None                 | UI_MONITOR_FLOAT/BOOL       |
| Unique name generation | None (conflicts!)    | __LINE__-based UIDs         |

The unique name generation is the most important improvement. In ReforgedUI,
calling `UI_WHITESPACE(1)` twice in the same file causes a "variable
redefinition" compiler error because both expand to the same variable name.
The v2 system uses `__LINE__` to generate unique names automatically.

## File Manifest

```
enbseries/
├── Helper/
│   ├── SkyrimBridge.fxh        ← v2.0 with KeepAlive retention
│   ├── enbUI_Primer.fxh        ← v2.0 improved UI macro system
│   └── enbHelper_Common.fxh    ← existing (unchanged)
├── UI/
│   └── enbUI_SkyrimBridge.fxh  ← NEW: monitor panel for ENB GUI
└── (shader .fx files)          ← add SB_Retain() call to each

src/
├── SB_ENBDiagnostics.h         ← NEW: C++ diagnostic wrapper
└── (existing tracker .cpp files)
```
