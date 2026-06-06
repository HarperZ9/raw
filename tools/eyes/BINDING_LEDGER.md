# Binding Ledger — spec (the externalized symbol table)

> Implements substrate #1 from `RAW/DOCTRINE.md`: a per-draw record of *actual* GPU
> bindings + runtime hazard asserts, so questions like "is SSR still bound to t27 when
> the compositor samples it?" are answered by **query**, not by in-head simulation.
> Companion to `LIVE_BRIDGE.md` (writes into the same `live/` dir the eyes read).
> Grounded against the real proxy as of 2026-06-05 — symbols cited are confirmed present.

## 1. Where it lives — instrumentation, not new hooks

It lives in the **proxy DLL** (`src/d3d11_proxy/`), which already overrides every method
the ledger needs. Confirmed in `WrappedContext.h`:

| Need | Already-overridden method (WrappedContext.h) |
|------|----------------------------------------------|
| RTV/DSV set | `OMSetRenderTargets` (L81), `OMSetRenderTargetsAndUnorderedAccessViews` (L82) |
| PS SRVs | `PSSetShaderResources` (L93) |
| CS SRVs/UAVs | `CSSetShaderResources` (L104), `CSSetUnorderedAccessViews` (L105) |
| VS/GS/HS/DS SRVs | `VSSetShaderResources` (L117), `GSSetShaderResources` (L120), `HSSetShaderResources` (L122) |
| Draw points | `Draw`/`DrawIndexed`/`DrawInstanced`/... (L72–78) |
| Dispatch points | `Dispatch` (L102), `DispatchIndirect` (L103) |
| Clears | `ClearDepthStencilView` (L88), `ClearRenderTargetView` (L87), `ClearUAV*` (L89–90) |

Transport: `ProxyLog.h` already does thread-safe file IO. Pass identity: the proxy
already runs `RenderPhaseDetector` (phase per draw). So this is **added instrumentation
inside existing override bodies** — zero new interception.

## 2. Data model — shadow state + snapshot-at-draw

Logging every `Set*` is high-volume and low-value. Instead the proxy keeps **shadow
arrays of current bindings** (updated in the `*Set*` overrides) and takes a **snapshot at
draw/dispatch time**, tagged with phase + pass marker:

```cpp
// proxy-global, single immediate context
struct BindState {
    ID3D11ShaderResourceView*  srv[6][128] = {};   // [stage][slot]  PS=0,VS=1,GS=2,HS=3,DS=4,CS=5
    ID3D11RenderTargetView*    rtv[8]       = {};
    ID3D11DepthStencilView*    dsv          = nullptr;
    ID3D11UnorderedAccessView* uav[8]       = {};   // OM + CS
};
```
- Each `*SetShaderResources` / `OMSetRenderTargets*` / `CSSetUnorderedAccessViews` override
  writes the relevant slots of `BindState` (it already receives exactly these args).
- Each `Draw*` / `Dispatch*` override calls `LedgerSnapshot(op)` **before** forwarding.
- Resource identity = the `ID3D11*` pointer (stable within a run; that is all the ledger
  needs to answer "same resource?"). Pointers are printed as hex; names are resolved in §3.

## 3. Pass markers + resource names (make the ledger self-describing)

A raw pointer at t27 is not enough; we want `t27 = SSRRenderer.output`. Two small additions
to the cross-DLL `ProxyInterface` (`ProxyAPI.h`):

```cpp
// RAW calls these; proxy stores current marker + a pointer->name map.
void (*SetPassMarker)(const char* name);                 // "" clears
void (*RegisterResourceName)(void* resource, const char* name);
```
- **Pass marker:** RAW brackets each effect dispatch — in `PhaseDispatcher`/`RenderPipeline`
  around `pass.execute(ctx)`, and around the compositor draw — with
  `SetPassMarker("SSR.resolve")` / `SetPassMarker("")`. Every snapshot inside the bracket is
  tagged. This is how a binding is attributed to a RAW pass.
- **Resource names:** `SRVInjector` already owns the t17–t38 registrations
  (`RegisterSRV(slot, srv)`); have it also call `RegisterResourceName(srv, "GTAO.output")`
  etc. The proxy resolves pointers to names at snapshot time. Unknown pointers print as hex
  (game-owned resources) — which is itself the signal for "this is the game's, not ours."

## 4. Output — `live/bindings.jsonl` (joins the eyes live/ dir)

One JSON object per snapshot (a marked draw/dispatch), appended:
```json
{"frame":1042,"seq":7,"phase":"PostGeometry","pass":"compositor","op":"Draw",
 "srv":{"t27":"SSR.output","t20":"GTAO.output","t19":"HiZ.depth","t7":"0x1f3a..."},
 "rtv":["game.sceneRT"],"dsv":null,"uav":[],
 "asserts":[]}
```
- Throttle: snapshot **only inside a pass marker** (RAW's ~10 effects + compositor) plus
  on phase transitions → ~12 lines/frame, tiny. Game draws are not snapshotted unless a
  marker is set. The hazard scan (§5) runs on *every* draw but only **emits on violation**.
- Rolling file, capped (e.g. last N frames) so it stays small; the eyes read the tail.

## 5. Invariants computed in-proxy (emit only on violation)

At each snapshot, before forwarding the draw:
- **HAZARD**: intersect the set of bound SRV pointers (all stages) with {current RTVs, DSV,
  bound UAVs}. Non-empty → `{"assert":"HAZARD","resource":"SSR.output","as":"SRV(t27)+RTV0"}`.
- **DSV_BOUND_DURING_SAMPLE**: the DSV's underlying texture also bound as any SRV → emit.
- **SLOT_MATCH**: for t17–t38, compare the bound name to the documented owner table
  (shipped as a small static map mirroring `ARCHITECTURE.md`); mismatch → emit.
These are O(boundSRVs × boundOutputs) per draw — a handful of pointer compares. Cheap.

## 6. Host side — `raw_eyes.py bindings`

New subcommand (mirrors `watch`/`attribute`): reads `live/bindings.jsonl` and answers the
state questions by query, not by reading C++:
```
raw_eyes.py bindings <live_dir>                  # latest frame: every pass's binding table
raw_eyes.py bindings <live_dir> --pass compositor   # one pass's slots + any asserts
raw_eyes.py bindings <live_dir> --asserts        # only HAZARD/SLOT/ DSV violations across the tail
```
Output is a plain table + an `asserts` list. The assistant reads *that*, and never claims
a binding from the source again.

## 7. The three Pascal questions, answered by query

| Question (was: reason in-head) | Now: observe |
|--------------------------------|--------------|
| Is SSR still bound to t27 when the compositor samples it? | `bindings --pass compositor` → `t27` field = name; equals `SSR.output` or not |
| Was the DSV unbound before this draw (read/write hazard)? | `--asserts` → presence/absence of `HAZARD` / `DSV_BOUND_DURING_SAMPLE` for that pass |
| Did mid-frame dispatch leave OM targets wrong? | `STATE_RESTORED` assert (RAW side, `D3D11StateBackup` diff-on-restore) in the same stream |

## 8. Integration checklist (proxy can't be compiled here — operator builds)

1. `WrappedContext`: add `BindState` shadow + write it in the existing `*Set*` overrides
   (L93/104/105/117/120/122/81/82); add `LedgerSnapshot(op)` call at top of each `Draw*`
   (L72–78) and `Dispatch*` (L102–103).
2. New `src/d3d11_proxy/BindingLedger.{h,cpp}` — `BindState`, snapshot, hazard/slot scans,
   JSONL writer (reuse `ProxyLog` IO). Add the .cpp to `CMakeLists.txt` proxy target.
3. `ProxyAPI.h` `ProxyInterface`: add `SetPassMarker` + `RegisterResourceName` fn pointers;
   implement in `proxy_main.cpp` alongside the existing `Register*` callbacks.
4. RAW side: `SRVInjector::RegisterSRV` also calls `RegisterResourceName`; `PhaseDispatcher`/
   `RenderPipeline` bracket each `pass.execute` with `SetPassMarker(name)`/`SetPassMarker("")`;
   `SceneCompositor` brackets its draw.
5. RAW side: `D3D11StateBackup` — add `VerifyRestored()` that diffs post-restore vs the saved
   snapshot and writes a `STATE_RESTORED` assert line.
6. `raw_eyes.py`: add the `bindings` subcommand (pure file read + table format).

## 9. Acceptance (ground-truth, no game needed)

Extend `raw_eyes.py selftest`: synthesize a `bindings.jsonl` with one clean pass and one
pass where `SSR.output` appears as both `srv.t27` and `rtv[0]`; assert the `bindings
--asserts` reader flags exactly the planted `HAZARD` and nothing on the clean pass. This
proves the *host* half before any C++ exists — the same pattern that already gives the
metrics layer its 12/12.

## Opt-in / safety

Off by default (gate behind the same flag as `LiveBridge`). The proxy paths are pointer
compares + throttled file appends; never throw into the frame loop (match `ProxyLog`'s
defensive style). When off, zero overhead beyond the shadow-state writes (a few pointer
stores per `Set*`).
