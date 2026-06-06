# BindingLedger — integration (operator applies; uncompilable in the authoring env)

New files `BindingLedger.{h,cpp}` are self-contained. The edits below wire them into
the proxy. Every WrappedContext insertion goes **right after the `if (PG_IsSafeMode())`
guard** (uniform across all methods) and uses the method's own args. The module is
`namespace SB::Proxy`, so `BindingLedger`, `LS_PS`, etc. resolve unqualified inside
WrappedContext (same as its existing `RenderPhaseDetector::Get()` calls).

## 1. CMakeLists.txt — add to the d3d11_proxy target (after line ~152)
```
    src/d3d11_proxy/BindingLedger.cpp
```

## 2. WrappedContext.cpp — `#include "BindingLedger.h"` at top, then one call per method
```cpp
// shader-resource setters (after the PG_IsSafeMode guard, before m_real->...):
PSSetShaderResources : BindingLedger::Get().OnSRV(LS_PS, StartSlot, NumViews, ppSRVs);
CSSetShaderResources : BindingLedger::Get().OnSRV(LS_CS, StartSlot, NumViews, ppSRVs);
VSSetShaderResources : BindingLedger::Get().OnSRV(LS_VS, StartSlot, NumViews, ppSRVs);  // GS/HS/DS optional

// output merger — use the DSV that is ACTUALLY bound (actualDSV), not the game's pDSV:
OMSetRenderTargets : BindingLedger::Get().OnRTV(NumViews, ppRTVs, actualDSV);
OMSetRenderTargetsAndUnorderedAccessViews :
    BindingLedger::Get().OnRTV(NumRTVs, ppRTVs, actualDSV);
    BindingLedger::Get().OnOMUAV(UAVStartSlot, NumUAVs, ppUAVs);
CSSetUnorderedAccessViews : BindingLedger::Get().OnCSUAV(StartSlot, NumUAVs, ppUAVs);

// draw / dispatch sites — call before m_real->Draw*/Dispatch:
Draw                  : BindingLedger::Get().OnDraw("Draw");
DrawIndexed           : BindingLedger::Get().OnDraw("DrawIndexed");
DrawInstanced         : BindingLedger::Get().OnDraw("DrawInstanced");
DrawIndexedInstanced  : BindingLedger::Get().OnDraw("DrawIndexedInstanced");
Dispatch              : BindingLedger::Get().OnDispatch("Dispatch");
DispatchIndirect      : BindingLedger::Get().OnDispatch("DispatchIndirect");
```

## 3. Frame boundary — in `WrappedContext::ResetFrameStats()` (called from Present)
```cpp
BindingLedger::Get().BeginFrame();
```

## 4. Init + enable — once, where the proxy sets up (e.g. after the context is wrapped)
```cpp
BindingLedger::Get().Initialize(nullptr);            // -> Data/SKSE/Plugins/RAW/live/bindings.jsonl
BindingLedger::Get().SetEnabled(/* from d3d11_proxy.ini [Diagnostics] BindingLedger */ false);
```
Default OFF. When off the Set* shadow writes still run (a few pointer stores) but no scan
and no IO. Flip the ini flag for a diagnostic session.

## 5. Read it (host, no game needed once a session has run)
```
python tools/eyes/bindings.py show    <game>/Data/SKSE/Plugins/RAW/live --pass compositor
python tools/eyes/bindings.py asserts <game>/Data/SKSE/Plugins/RAW/live
```

## Optional enrichment (RAW side, separate task) — names instead of hex pointers
Add to `ProxyInterface` (ProxyAPI.h) + implement in proxy_main.cpp:
```cpp
void (*SetPassMarker)(const char* name);                 // -> BindingLedger::Get().SetPassMarker
void (*RegisterResourceName)(void* res, const char* name);
```
Then RAW: `SRVInjector::RegisterSRV` also calls `RegisterResourceName(srv, "GTAO.output")`;
`PhaseDispatcher`/`RenderPipeline` bracket each `pass.execute` with `SetPassMarker(name)` /
`SetPassMarker("")`. Without this the ledger still works — game/unnamed resources just print
as `0x...`, which is itself the "this is the game's, not ours" signal.

## Verification done in the authoring env (the C++ could not be compiled there)
- Output contract proven: a hand-authored line in `Snapshot()`'s exact format parses and the
  host flags the planted HAZARD / DSV_BOUND_DURING_SAMPLE (round-trip through `bindings.py`).
- Host detectors: `bindings.py selftest` = 8/8.
- What remains for the operator: compile the proxy, run one frame, confirm `bindings.jsonl`
  appears and `bindings.py asserts` is clean on a known-good scene.
