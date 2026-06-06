# RAW Coherence Membrane — v2 Theorycraft Roadmap

> Status: 2026-06-05. Audience: project owner (RE/graphics) + the LLM that builds this.
> This supersedes the inventory note. It is the canonical plan for the oracle layer.
> Every file:symbol below was verified against the live tree, not memory. Where a
> surviving idea's grounding was wrong, the correction is stated inline.

---

## 1. The Membrane, Defined

The **coherence membrane** is the oracle layer that compensates for the LLM's
single, structural disability: **state-blindness**. A human engineer debugging a
D3D11 renderer carries an implicit interpreter — a symbol table (what is bound to
`t27` right now), a heap view (what bytes are in that constant buffer), an
execution trace (which pass ran, in what order, on which thread). The LLM has none
of this. It has source text and its own confident prior. When it asserts "SSGI
samples the HiZ pyramid at t27," that is a *guess about runtime state* dressed as
a fact. The membrane exists to **falsify or confirm that guess with ground truth**,
so the model reasons locally on observed inputs and measured outputs instead of
simulating a GPU in its head.

**The principle: interpreter of ground truth, not source.**

> The membrane does not re-derive what the code *should* do. It emits what the
> hardware and toolchain *actually did*. The host readers (`bindings.py`,
> `raw_eyes.py`) recompute every assertion independently and treat the proxy's
> own verdict as advisory — the proxy reports, the host adjudicates. A claim the
> model cannot ground against an emitted artifact is a claim it must label
> "unknown," not assert.

Three properties make a membrane oracle legitimate, and every idea below is
filtered through them:

1. **Externalized.** It writes a machine-readable artifact (JSONL / image / log)
   that an out-of-process tool can read after the game closes. State that lives
   only inside a C++ class (e.g., `FrameGenerator::m_writeIdx`,
   `LiveFrameStats::sawNaN`) is *not* an oracle — it is low-observable.
2. **Witnessed, not inferred.** It reports a value the hardware produced
   (a bound SRV pointer, a CB byte, a NaN bit pattern at float precision), not a
   value reconstructed from a downstream artifact (NaN guessed from an 8-bit BMP
   that already clamped it away).
3. **Independently checkable.** The host reader re-runs the invariant from raw
   data and can disagree with the proxy. `bindings.py selftest` (8/8) and
   `raw_eyes.py selftest` (12/12) gate this.

**Why this is the project's defensible core.** RAW competes in a space crowded
with "neural rendering" and "AI-enhanced" graphics claims that are, on
inspection, vaporware — a model emitting plausible HLSL with no witness that it
ran, no measurement that it improved anything, no oracle that it didn't corrupt
the frame. The membrane is the opposite bet: **the durable asset is not the
shaders the LLM writes, it is the instrumented substrate that lets an LLM build a
D3D11 renderer at all without hallucinating its way into silent corruption.** The
shaders are replaceable. A proxy that turns every dead zone into a falsifiable
artifact is the moat.

---

## 1.5 The Governing Doctrine — read-gate built, write-gate owed

> Source: the parallel ideas piece *The Coherence Membrane* (2026-06-05). Per its
> own trust clause, this is a witness to corroborate, not canon to obey.

**Externalize the missing organs.** A transformer is a stateless pure function with
no symbol table, no heap, no execution cursor. The fix is not a smarter mind but a
*body*: persistence (an addressable store), perception (faithful projections of
ground truth into bytes), actuation-with-impedance (effects gated before they land),
and temporality (a clock stamping every fact as-of-when). Every "reason about it"
becomes "read the artifact."

**Two gates, and RAW has built only one.**
- **Read-gate** — nothing enters the mind as fact unless witnessed by the store.
  *This is what exists today*: the binding ledger, `raw_eyes`, the capture feed.
- **Write-gate** — nothing leaves the mind as effect unless witnessed by the
  invariants. *This is the missing organ* (see Tier W). It is not redundant with the
  read-gate: the model's pretraining prior is a second, un-gatable read path, so a
  loud prior overrides a quietly-staged fact. Only the write-gate makes a
  confidently-wrong assertion *impossible within the membrane's reach*.

**The five limits (honest about where it leaks).**
1. A scalar clock is wrong for true concurrency; RAW's immediate context is
   single-threaded, so the frame-index partial order suffices — but a real race
   (deferred contexts, threads) must escalate to a model checker, not the ledger.
2. The membrane controls presentation, not belief — hence the write-gate.
3. Sound only within *named* invariants: "passed all checks" means "tripped no
   declared failure," not "correct." Promote every escaped bug to a new invariant.
4. Requires a world you can pause — RAW satisfies this (frame capture + replay);
   real-time-only state can only be shown post-hoc.
5. An unverified membrane is net-negative — it launders falsehood with ground-truth
   authority. This is why every oracle ships with a host selftest (`bindings.py`
   8/8, `raw_eyes.py` 12/12) and the proxy's verdict is advisory, host-adjudicated.

**The trust root (terminate the regress, do not escape it).** Trust is earned by
re-derivability, never asserted by authority: hash the *source* bytes, never a
calibrated view; corroborate one fact across N disjoint paths — method diversity
(the binding ledger + the D3D11 debug layer + a RenderDoc capture), not N copies of
one model; tamper-evident append-only logs; falsifiability; and smallness, bottoming
out where verification is local pure logic (a hash compare). *Pointed local
corollary:* this workstation's `safe_read`/`safe_write` calibration layer is itself a
faithless membrane — it modulates the view. The trust root is `git show` / the raw
bytes, as this session proved when injected "[SCOPE CONTEXT]" strings present in a
read-view were absent from the real file.

**Provenance of this very document.** It was synthesized by 82 same-base-model
agents. Their agreement is an *echo*, not corroboration. Its trust comes only from
the agents that grepped the live source and overturned the same-model consensus —
the `HazardScan`-single-stage, `sawNaN`-has-no-producer, and
`StateBackup::Restore`-nulls-before-diff corrections in the map below are method
diversity in action. Treat any claim here not tied to a file:symbol as a hypothesis.

**Prime directive:** no unwitnessed transit across any boundary — mind/world or
session/session — in either direction. When something breaks, leak-hunt the
membrane; never tune the mind. This governs *is*, not *ought*: goals and taste are
brought to the loop, not produced by it.

---

## 2. Dead-Zone Coverage Map

Every dead zone the membrane must eventually cover. "Covered" means an artifact is
emitted *and* host-verified today. "Partial" means the substrate exists but the
emission or producer is missing. "OPEN" means no witness at all.

| Dead zone | Class | Status | Oracle / where it breaks |
|---|---|---|---|
| Bound SRV/RTV/DSV/UAV per draw | Binding | **Covered** | BindingLedger; `bindings.py analyze()` 8/8 |
| Read/write aliasing (HAZARD), depth-sampled-while-writable (DSV_BOUND_DURING_SAMPLE), wrong-owner slot (SLOT_MISMATCH) | Binding | **Covered** | `BindingLedger::HazardScan` (BindingLedger.cpp:114) — **PS+CS stages only** |
| Output pixel values: cast / black / blown / flatness / noise / banding / flicker | Numeric (post-clamp) | **Covered** | `raw_eyes.py metrics/quality/temporal` 12/12 |
| Per-pass capture feed (depth/GTAO/SSR/SSGI/Bloom/Composite) | Numeric (source images) | **Covered** | `TextureDump::DumpAllEffects` — 9 DXGI formats, lossy half-float |
| Shader compile syntax / codegen errors | Type/toolchain | **Covered** | Tier 0.2 shipped: `SB_ShaderDebug` overlay + `WriteCompileJson` -> `live/compile_errors.jsonl`; `raw_eyes compile` reports all_built + error lines |
| GS/HS/DS stage SRV bindings + hazards | Binding | **Covered** | Tier 3.1 shipped: `OnSRV(LS_GS/HS/DS)` in WrappedContext + `OnDraw` fan-out over {PS,VS,GS,HS,DS} (skips empty) + `"stage"` field; `bindings.py` analyze() stage-aware. Value-capped on Skyrim. |
| Sampler / blend / rasterizer / depth-stencil / viewport / scissor state | Pipeline state | **Covered** | Tier 2.1 full: blend/raster/depth/sampler descs serialized on first sight (deduped) + viewport/scissor sanity -> `live/pipeline_state.jsonl`; `bindings.py state` reports current_state + violations |
| Constant-buffer contents (overflow, NaN, range) | Numeric (heap) | **Covered** | Tier 1.2 shipped: `CBDirtyTracker::InterceptUnmap` scans the CPU shadow for NaN/Inf+min/max -> `live/cb_meta.jsonl`; `WrappedContext::UpdateSubresource` emits CB_SIZE_MISMATCH; `raw_eyes cb`. (Slot attribution = follow-on) |
| Live per-frame telemetry (luma/gpuMs/fps/depthValid trend) | Numeric (trend) | **Covered** | Tier 0.1 shipped: `LiveBridge::Tick` wired in `main.cpp DoFrameUpdate` + `.cpp` added to build; `raw_eyes watch()` verified; control.ini round-trip |
| NaN/Inf at float precision before clamp | Numeric (source) | **Covered** (sampled) | Tier 1.1 shipped: `TextureDump` bitwise float scan -> `live/texture_inspect.jsonl`; `raw_eyes numeric`; feeds `sawNaN`. Per-frame inline scan still future |
| STATE_RESTORED after mid-frame dispatch | Concurrency/state | **Covered** | Tier 2.2 shipped: PhaseDispatcher probes live state before dispatch + after Restore, diffs identities -> `live/restores.jsonl`; `bindings.py restores`. Fires in-game |
| Shader `#define` permutation live per draw | Type/variant | **Covered** | Tier 3.2 shipped: `WriteShaderVariant` captures pDefines at both D3DCompile hooks -> `live/shader_variants.jsonl` (sorted defines, canonical hash) + bind-time `shader_used` from PSSetShader; `raw_eyes read_shader_variants`. Silent for precompiled game shaders. |
| DXBC<->HLSL register binding (slot->register) | Type | **Covered** | Tier 3.3 shipped: `D3DReflect` at the post-compile blob (SB_ShaderDebug) -> `live/shader_reflection.jsonl`; `bindings.py reflect` cross-checks SLOT_OWNERS, flags REGISTER_MISMATCH |
| History-buffer ping-pong / warmup correctness | Temporal | **Covered** (TAA) | Tier 3.4 shipped: TAAManager emits `{frame,buffer,read_idx,write_idx,warmup}` -> `live/temporal.jsonl`; `bindings.py read_temporal` flags READ_EQ_WRITE/NO_TOGGLE/WARMUP_AFTER_STEADY. TSR/FrameGenerator deferred (CMake-disabled); SSGI a built follow-on. |
| Build freshness (does the DLL match source?) | Build/toolchain | **Covered** | Tier 3.5 shipped: CMake POST_BUILD writes a source-content-hash manifest (`build_stamp.py`); `build_stamp.py check src Shaders` -> fresh/STALE. mtime-free |
| Resource lifetime / VRAM leak / refcount | Lifetime | **Covered** (create-side) | Tier 3.6 shipped: WrappedDevice CreateBuffer/CreateTexture2D -> `live/resources.jsonl`, gated `[Diagnostics] ResourceLog` (default off); `raw_eyes read_resources`. Release-side/UAF deferred (needs vtable wrap). |
| Deferred-context / thread-id witness | Concurrency | **Partial** | thread-id witness shipped: `GetCurrentThreadId` "tid" on STATE_NOT_RESTORED records. CONTEXT_MISMATCH assert still deferred (CreateDeferredContext returns unwrapped -> would never fire on the real threat surface). |
| Descriptor heaps | N/A | n/a | D3D11 has no descriptor heaps — not a gap |

> **Update 2026-06-06 - roadmap OPEN items closed.** All five OPEN dead zones above
> are now Covered (3.1 GS/HS/DS, 3.2 #define, 3.4 temporal-TAA, 3.6 VRAM-create-side,
> thread-id Partial), plus 1.2-p3 CB slot attribution, 1.3 per-pass range (scaffold +
> gated GPU path, operator-validated), and W.1 claim-binding lint. A 3-agent pre-live
> audit verified all 12 emitter<->reader schema contracts agree and fixed a HIGH
> Snapshot stack-overflow + the heuristic-CB false-positive write-gate. Runtime
> emission is confirmed per oracle by `tools/eyes/verify_runtime.py` on the next live
> run. The paragraph below describes the pre-session state.

**The shape of the gap.** The membrane sees **4 view types on 2 of 6 shader
stages** (`OnDraw`->`LS_PS`, `OnDispatch`->`LS_CS`; VS/GS/HS/DS are populated only
if explicitly snapshotted, which they are not), and **zero pipeline state**. The
two biggest honesty gaps are (a) LiveBridge's in-game tick is built but unwired,
and (b) the STATE_RESTORED / RANGE / NAN / TEMPORAL invariants are documented but
emit nothing.

---

## 3. The v2 Roadmap

Ordered **most-expensive-failure-first**: a dead zone where a bug corrupts the
frame *silently and is mis-attributed by the LLM* outranks one that merely
produces low-frequency signal. Near-duplicate ideas are merged. Each item names
the dead zone, the verified mechanism, and effort. Line numbers are corrected
against the live tree.

### Tier 0 — Wire what is already built (hours, not days)

These are the highest leverage in the entire backlog: fully-implemented substrate
with a confirmed zero-call-site dead zone. Pure plumbing, maximum coverage gain.

**0.1 — LiveBridge tick + init wiring** *(merges _i 5, 12, 41, 53)*
- **Closes:** live per-frame telemetry trend (luma drift, gpuMs, fps, depthValid)
  and the out-of-game `control.ini` round-trip — currently a permanently silent
  dead zone.
- **Mechanism:** `LiveBridge::Tick` and `EmitMetrics` are complete
  (LiveBridge.cpp:110/51). They have **zero callers**. Add `LiveBridge::Initialize`
  + `SetEnabled` at plugin startup (`main.cpp`, data-loaded message) and a single
  `LiveBridge::Get().Tick(stats)` once per frame in the Present path
  (`WrappedSwapChain::Present`, or `DebugGUI::Render`'s overlay tick which is
  already in scope). Populate `LiveFrameStats` from the existing singletons:
  `LuminanceHistogram::Get()` (note: getter is `avgLuminance`, **not**
  `GetAverage`) and `GPUProfiler::Get()`. Host `raw_eyes.py watch()` (raw_eyes.py:358)
  already consumes `live/latest.json` + `live/metrics.jsonl`.
- **Effort:** ~10-15 lines C++. No host change. **Do this first.**

**0.2 — Shader compile-error persistence as JSONL** *(_i 66)*
- **Closes:** post-hoc visibility of hot-reload compile failures. Today the error
  shows in-game then is lost when the game closes; the LLM cannot verify "the
  shader built."
- **Mechanism:** `ShaderDebug::WriteLogEntry` (SB_ShaderDebug.cpp:860) already
  writes `RAW_ShaderErrors.log` in human format, gated on
  `m_config.persistLog && !attempt.succeeded` (:499). Add one branch that appends a
  single JSON object — `{ts, file, line, column, error, severity, succeeded}` — to
  `live/compile_errors.jsonl`. No new hook; the interception path
  (`HookD3DCompile`->`RecordCompilation`->`WriteLogEntry`) is live. Add
  `read_compile_log()` to `raw_eyes.py`.
- **Effort:** ~20 lines C++, ~15 Python. Low.

### Tier 1 — Numeric ground truth at the source (the silent-corruption tier)

A CB-overflow or NaN that the LLM mis-attributes to "shader math" is the most
expensive failure class — it cost two debugging sessions already (commits
16c7d5b/295dd0e) with no oracle. These close that.

**1.1 — sawNaN producer via float-precision NaN/Inf scan** *(merges _i 13, 25)*
- **Closes:** NaN/Inf/subnormal at float precision *before* the 8-bit clamp
  destroys the bit pattern. `sawNaN` is a declared field with **no producer**
  (verified: only LiveBridge.h:26 + LiveBridge.cpp:47).
- **Mechanism:** `TextureDump::SaveTexture2D` already walks mapped
  R32/R16/R16G16B16A16/R11G11B10 float pixels before the uint8 conversion. The
  R32_FLOAT path contains an explicit `v < 1e20f` guard that **discards** NaN/Inf
  rather than flagging it — that is the exact information loss to fix. Add a
  bitwise NaN/Inf accumulator (exponent all-1s; mantissa nonzero=NaN, zero=Inf)
  and emit `{format, has_nan, has_inf, nan_count, first_nan_xy}` to
  `live/texture_inspect_<pass>.json`. Set `LiveFrameStats.sawNaN` from it. Note:
  R11G11B10 needs an explicit `exp==31` bit check (its decoder uses integer
  mantissa extraction). `raw_eyes.py metrics()` gains a `numeric_ok` field.
- **Caveat (honest):** this fires on the throttled dump path, not every frame.
  For true per-frame coverage, the scan must also run inline in the PrePresent
  luma readback. Ship the dump-path version first (sampled ground truth still
  beats inferred-from-clamped).
- **Effort:** Low. Hook point is exact.

**1.2 — Constant-buffer content oracle (NaN + size-mismatch + range)** *(merges _i 2, 8, 21, 29, 34, 40, 52, 58, 64)*
- **Closes:** the entire CB-overflow / out-of-range / NaN-in-uniform bug class.
  RANGE/NAN invariant #5, currently spec-only.
- **Mechanism — corrected from the proposals.** Multiple surviving ideas proposed
  a GPU `CopyResource`+staging `Map(D3D11_MAP_READ)` readback. **Reject that
  path:** RAW's own CBs are `D3D11_USAGE_DYNAMIC`/`CPU_ACCESS_WRITE` and cannot be
  `Map`-read (returns E_INVALIDARG), and the data already lives on the CPU.
  `CBDirtyTracker` maintains a host-side shadow of every committed dynamic CB
  (`GetShadowData`/`GetShadowSize`, CBDirtyTracker.cpp:131/139; `kMaxTrackedSize`
  cap at :34). The real work is three cheap pieces:
  1. In `CBDirtyTracker::InterceptUnmap` (:70), on a dirty commit, scan the shadow
     bytes for NaN/Inf and append `{frame, ptr, size, has_nan, min, max}` to
     `live/cb_meta.jsonl`. No new readback.
  2. In `WrappedContext::UpdateSubresource` (currently a naked forward), query the
     buffer desc and emit `CB_SIZE_MISMATCH` when the upload size exceeds
     `desc.ByteWidth` — the direct witness for the overflow class.
  3. To attribute a CB to a slot/stage, add a CB pointer shadow array to the
     ledger (the `*SetConstantBuffers` overrides are passthroughs today — there is
     **no** existing CB binding table; do not claim BindingLedger already has one).
- **Drop:** the per-field layout-contract decoder (depends on hand-maintained
  comment annotations that will silently drift) and the per-frame hex flood. Ship
  NaN/range/size-mismatch on shadow bytes only. Add `read_cb_dump()` to
  `raw_eyes.py`.
- **Effort:** Medium (~45 lines for the slot shadow + emit, ~60 Python). The
  "high / new readback pipeline" framing in the proposals is overstated.

**1.3 — Per-pass numeric range assertions (pre-clamp, GPU reduction)** *(merges _i 20, 49)*
- **Closes:** per-pass attribution of bad numbers — distinguishing a NaN in GTAO
  from one in SSR, which LiveBridge's frame-level luma/sawNaN cannot.
- **Mechanism:** a small compute reduction (1x1 dispatch) after each marked pass
  writes its RTV/UAV, computing min/max/NaN-count/Inf-count into a 16-float
  staging buffer, read back and emitted as `{frame, pass, output, min, max,
  nan, inf, verdict}` to `live/ranges.jsonl`. CS + staging-readback +
  `SaveCSState`/`RestoreCSState`/`SaveOMState`/`RestoreOMState` infrastructure
  already exists in `ComputeManager`; the established mid-frame interception
  pattern is `D3D11StateBackup`.
- **Corrections:** D3D11 has **no async compute queue** — this is an
  immediate-context dispatch with per-pass GPU cost; gate behind
  `RAW_ENABLE_GPU_READBACK`. "Reads the bound RTV desc" is **not** generically
  possible — `RenderPassManager`/`PassDef` has no "after write, before unbind"
  hook, so each pass must expose its output SRV via a new optional
  `post_execute` callback or a synthetic stats pass. Effort is **medium-high**,
  not medium.
- **Effort:** Medium-high. Build after 1.1/1.2 prove the readback discipline.

### Tier 2 — Pipeline-state visibility (the "wrong interpretation" tier)

The ledger sees *what is bound* but nothing about *how it is interpreted* — wrong
blend op, inverted depth compare, clamp-vs-wrap sampler. These produce visually
wrong frames with no error, and the LLM confidently blames shader math.

**2.1 — Pipeline-state ledger (blend / raster / depth-stencil / sampler / viewport / scissor)** *(merges _i 0, 9, 17, 22, 26, 32, 38, 44, 50, 56, 62)*
- **Closes:** the largest single dead zone by surface area — all pipeline state
  outside SRV/RTV/DSV/UAV.
- **Mechanism:** `WrappedContext` already **caches** the state-object pointers for
  redundancy suppression at the intercept points (`OMSetBlendState`,
  `OMSetDepthStencilState`, `RSSetState`, `RSSetViewports`) but never feeds the
  ledger. Thread those cached pointers into `BindingLedger::Snapshot`; call
  `GetDesc()` on each at snapshot time to serialize
  `D3D11_BLEND_DESC`/`_RASTERIZER_DESC`/`_DEPTH_STENCIL_DESC`/`_SAMPLER_DESC`
  fields into a new `pipeline_state` JSON key. **Three legs are cheap** (pointer
  already cached); the **sampler and scissor legs are genuinely new
  instrumentation** — `PSSetSamplers`/`CSSetSamplers`/`RSSetScissorRects` are pure
  passthroughs with no cache. Emit **on state change**, not per draw, to bound
  volume. `bindings.py` gains a `--state` view.
- **Corrections to the proposals:** D3D11 has no state "GUIDs" (serialize desc
  fields); there is no `D3D11StateBackup.h` blend/sampler shadow to reuse for this
  (it backs up pointers for restore, not descs); the method is `RSSetState` not
  `RSSetRasterizerState`; **drop `DEPTH_FUNC_INVERTED`** as an auto-assert — RAW
  has no registered reversed-Z context to know LESS-vs-GREATER intent, so it would
  false-positive. Ship serialization + viewport/scissor sanity (`VIEWPORT_SIZE_ZERO`,
  `SCISSOR_OUTSIDE_VIEWPORT`) first; defer per-pass `BLEND_STATE_MISMATCH` until an
  intent table exists.
- **Optional GPU-native augment (_i 0):** if a debug device is ever enabled, drain
  `ID3D11InfoQueue` per draw into a `gpu_warnings` field, emitting a
  `gpu_oracle_active` sentinel so silence-as-no-hazard is distinguishable from
  silence-as-no-debug-layer. Skyrim SE ships a non-debug device, so treat this as a
  dev-only instrument. Drop the `GetDeviceRemovedReason` mention (device-loss API,
  not hazards).
- **Effort:** Medium. Three legs reuse cached pointers + one `GetDesc` each; two
  legs are new taps.

**2.2 — STATE_RESTORED verification oracle** *(merges _i 3, 28, 35, 42, 67)*
- **Closes:** mid-frame dispatch leaving the game's pipeline dirty — silent
  downstream corruption a full frame removed from its cause. INVARIANT #3,
  unverified.
- **Mechanism:** `PhaseDispatcher` calls `backup.Save(m_realContext)` (:146) and
  `backup.Restore(m_realContext)` (:225) fire-and-forget. The Save struct holds
  `rtvs`, `dsv`, and the rest (members are `backup.dsv`, `backup.rtvs[0]` —
  verified at :150/:222, **not** the `m_prevOM/m_prevRS` named in one proposal).
  After Restore, query the live context (`OMGetRenderTargets`/`OMGetBlendState`/
  `RSGetState`/`IAGetInputLayout`) and diff against a snapshot.
- **The load-bearing correction every proposal except one missed:** `Restore()`
  calls `Release()` internally, **nulling the backup pointers before any diff can
  run**. You must capture a pre-Restore snapshot of the desired state, *then*
  Restore, *then* query live and compare against the snapshot — the naive
  "compare struct to live after Restore" compares nulls. Emit
  `{phase, pass, field, match}` to `bindings.jsonl`; `bindings.py` gains a
  `restores` query.
- **Audience note:** a mismatch here almost always indicts RAW's own effect code,
  not the game — narrow but high-value during effect development.
- **Effort:** Medium (~60-80 lines comparator + emit). Call site is one line after :225.

### Tier 3 — Completion of coverage (low-frequency but real gaps)

Real dead zones with lower live signal density. Cheap hygiene; do alongside
adjacent edits.

**3.1 — GS/HS/DS SRV ledger + Snapshot fan-out** *(merges _i 1, 10, 18, 27, 33, 39, 45, 51, 57, 63)*
- **Closes:** tessellation/geometry-stage SRV hazards (3 of 6 stages blind).
- **Mechanism:** add `BindingLedger::Get().OnSRV(LS_GS/LS_HS/LS_DS, ...)` before the
  forwards at WrappedContext.cpp:577/587/602 (mirror the PS guard +
  `OnSRV` pattern at :424-425). `LS_GS/LS_HS/LS_DS` exist; `m_srv` is dimensioned
  for all stages.
- **The correction the "3 one-liners" framing gets wrong (verified):**
  `HazardScan(int stage,...)` (BindingLedger.cpp:114) scans **one** stage, and
  `OnDraw`->`Snapshot(op, LS_PS)` (:176), `OnDispatch`->`Snapshot(op, LS_CS)` (:177)
  are hardwired. Populating the GS/HS/DS shadows does nothing until you **also fan
  out the Snapshot** at draw sites — a loop over the populated stages, or explicit
  per-stage Snapshot calls. ~5-7 lines, not 3. For cross-stage hazards (GS reads
  while PS writes the same RTV) the host's flat slot dict also needs a stage prefix.
- **Effort:** Low-plus. **Value capped:** Skyrim SE drives GS/HS/DS almost only for
  shadow-cube and terrain tessellation, which rarely bind RAW's t17-t38 range —
  expect sparse signal. Worth it as hygiene riding another WrappedContext edit.

**3.2 — Shader variant / `#define` permutation oracle** *(merges _i 4, 11, 54, 59)*
- **Closes:** which permutation is live per draw (compile-time witness).
- **Mechanism:** `HookD3DCompile`/`HookD3DCompile2` receive `pDefines`
  (SB_ShaderDebug.cpp:298/368) and feed `RecordCompilation` (:416), which **drops
  the defines**. Serialize them (sorted `key=val|...`) into the record and emit
  `{hash, defines, source, profile}` to `live/shader_variants.jsonl`. Add a
  bind-time `shader_used` emit after the existing `psHash` capture in
  `PSSetShader`. `raw_eyes.py` gains `read_shader_variants()`.
- **Corrections:** the DXBC `#define` *values* are erased from the binary — capture
  them at the `D3DCompile` hook, not by reflecting the blob. The proposed
  `ShaderCache::GetCachedOrCompile` entry point does not exist (it is
  `TryGetCached`/`StoreCached`); the enrichment belongs in `HookD3DCompile` where
  `pDefines` is in scope.
- **Value capped (verified):** Skyrim's stock shaders arrive as **precompiled
  DXBC** via `CreatePixelShader`, never through `D3DCompile` — so this oracle is
  silent for game shaders and loud only for RAW's own compute/post-process. Real
  but bounded.
- **Effort:** Medium.

**3.3 — DXBC<->HLSL register reflection oracle** *(_i 61)*
- **Closes:** silent slot->register mismatch (shader declares `register(t15)`,
  injector binds `t27`). No `D3DReflect` exists anywhere in src (verified).
- **Mechanism:** at the post-compile blob in `HookD3DCompile`, call `D3DReflect` +
  `GetResourceBindingDesc` to enumerate `{name, type, register, count}`; emit to
  `live/shader_reflection.jsonl`. `bindings.py` cross-checks against `SLOT_OWNERS`
  and the live ledger, flagging `REGISTER_MISMATCH`. `d3dcompiler_47` is already in
  the IAT via `SB_ShaderDebug` — expose `D3DReflect` to the hook.
- **Effort:** Medium. ~4 lines of reflection + cache lifecycle + host cross-check.
  Highest-value during integration of *new* passes authored independently of the
  injector.

**3.4 — Temporal / history-buffer correctness oracle** *(merges _i 14, 19, 24, 60)*
- **Closes:** ping-pong index + warmup/clear correctness (TEMPORAL invariant #6).
  `FrameGenerator::m_writeIdx`, `TAAManager` indices, and `m_firstFrame` guards are
  private with no emission path.
- **Mechanism:** register history textures via `RegisterResourceName` in the 2-3
  renderers that actually ping-pong (TAAManager, TemporalSuperRes — **not**
  DenoiseManager, which owns no ping-pong state), then have those renderers emit
  `{frame, buffer, read_idx, write_idx, warmup}` to `live/temporal.jsonl` at each
  history-touching pass. `bindings.py` gains a `temporal` causality checker.
- **Corrections:** `BindingLedger` has no include of `FrameGenerator` — threading
  `m_writeIdx` in needs an explicit dependency-injection decision, it is not free.
  `expected_write_idx = frame%2` is **wrong** for renderers that can be toggled
  mid-session; track per-renderer state. The flicker-to-violation correlation will
  be noisy (flicker has many causes).
- **Effort:** Medium, two-step (register names first, then emit). Lower priority —
  spec-only invariant, real but not silent-corruption class.

**3.5 — Build-freshness ledger** *(_i 7)*
- **Closes:** "does the running DLL match my edit?" — currently unobservable.
- **Mechanism:** CMake post-build step writes `live/build_manifest.json`
  `{commit, source_sha, compiler, flags, ts, dll_mtime}`. **Primary signal must be
  the source-tree SHA, not mtime** — `git checkout` and NTFS make mtime lie.
  `SKSEPlugin_Load` (main.cpp) reads it; `LiveBridge::Tick` surfaces `build_state`
  into `latest.json`; `raw_eyes.py watch()` warns on hash mismatch.
- **Effort:** Medium. Depends on 0.1 (LiveBridge wired).

**3.6 — Resource-lifetime / VRAM-leak watch (create-side only)** *(_i 37)*
- **Closes:** VRAM leak from unmatched creates. STATUS Step 4.
- **Mechanism:** `WrappedDevice::CreateBuffer`/`CreateTexture2D` (passthroughs
  today) append `{op:create, type, ptr, size}` to `live/resources.jsonl`; Python
  sums live allocation.
- **Correction:** intercepting COM `Release` on arbitrary resources requires vtable
  wrapping — far more than the claimed 80 lines. **Scope to create-logging only**
  (VRAM sum from unmatched creates); mark Release-side / UAF detection as a
  follow-on. Gate behind an INI flag, default off.
- **Effort:** Medium (create-side). Lowest priority.

**Deferred / partial-coverage note — concurrency thread-id witness** *(_i 16)*: a
real dead zone (zero `GetCurrentThreadId` in src), but Skyrim SE renders on a
single-threaded immediate context and `CreateDeferredContext` returns **unwrapped**
(WrappedDevice), so a thread-id field would only ever witness the already-wrapped
immediate context and miss the actual deferred-context threat. Add the thread-id
field cheaply alongside 2.2 (it answers "did the render thread own the CB when we
patched it?"), but do **not** build the `CONTEXT_MISMATCH` assert until
`CreateDeferredContext` is wrapped — it would never fire on the real threat surface.

---

### Tier W — The Write-Gate (the membrane's missing half)

Every tier above is read-gate: it makes ground truth *available*. The write-gate
makes confident-wrong *impossible within reach* by refusing unwitnessed output. RAW
has none of it yet; it is the highest-leverage *new axis* — orthogonal to, not
competing with, the read-side verdict in the next section.

- **W.1 Claim-binding lint** — when the assistant asserts a runtime-state fact
  ("t27 = SSR.output when the compositor samples"), require a matching `bindings.py`
  snapshot to exist; absent the witness, the claim is labelled `unknown`, not
  asserted. Mechanism: a checklist/hook mapping each class of stateful claim to the
  oracle that would confirm it. Dead zone: unwitnessed assertion. Effort: low (host).
- **W.2 Commit gate** *(SHIPPED 2026-06-05, commit 59cae7a)* — a git **commit-msg** hook (`tools/eyes/hooks/commit-msg` + `membrane_gate.py`, installed via `core.hooksPath`) that runs the membrane selftests and,
  when a capture exists, `bindings.py asserts` / `raw_eyes attribute`, and blocks a
  commit whose message claims a fix the oracle does not confirm. Dead zone:
  unverified "fixed" claims entering history. Effort: low.
- **W.3 Build-freshness seal** *(SHIPPED 2026-06-05)* — refuse the words "verified
  in-game" unless the DLL's embedded build-id equals the current source hash;
  stale-binary claims become unspeakable. Dead zone: stale-artifact claims. Effort:
  med (build-id emission + check).

This is the second-gate organ of the ideas piece: the read-gate already lets the
model *see* t27; the write-gate stops it *shipping* a claim about t27 that the
ledger contradicts.

## 4. Anti-Goals — The Vaporware to Refuse

The surviving ideas carry their own warnings; these are the failure modes to
reject by name.

- **GPU `Map(D3D11_MAP_READ)` readback of dynamic CBs.** Multiple proposals
  reached for a `CopyResource`+staging readback to recover CB contents. It is
  **technically wrong** (dynamic/write-only CBs reject `Map` READ with E_INVALIDARG)
  and **redundant** (`CBDirtyTracker` shadow already has the bytes on the CPU).
  *"The idea re-engineers a stalling staging copy to recover data that already
  lives in CPU memory for free."* Use the shadow.

- **Per-field CB layout-contract decoding from comment annotations.** *"depends on
  user-maintained comment annotations that will silently drift."* An oracle whose
  ground truth is a hand-edited `// offset=64 float emissive` comment is not an
  oracle. Ship NaN/range/size-mismatch on raw bytes; reflect register layout
  (3.3) for structure — never trust prose.

- **Auto-asserting `DEPTH_FUNC_INVERTED` / `BLEND_STATE_MISMATCH` without an intent
  table.** *"DEPTH_FUNC_INVERTED has no reversed-Z context to fire correctly."*
  Serializing state is ground truth; judging it "wrong" against an imaginary
  per-pass expectation is a confident hallucination wearing an assert's clothes.
  Emit the state; defer the verdict until a real intent table exists.

- **`#define` permutation recovery from the DXBC blob.** *"defines are
  compile-time-erased from DXBC."* You cannot reflect what the compiler deleted.
  Capture at `D3DCompile`, not from the binary.

- **"HazardScan already iterates all 6 stages."** It does not (BindingLedger.cpp:114
  takes one stage; OnDraw/OnDispatch hardwire LS_PS/LS_CS). Any plan that adds
  OnSRV calls and declares the hazard closed *without fanning out Snapshot* ships a
  silent no-op. Verify the emit path, not just the shadow write.

- **`GetDeviceRemovedReason` as a hazard oracle.** Device-loss API, not a pipeline
  hazard stream. Different problem; drop it.

- **The meta anti-goal:** any "neural / AI rendering" capability that emits shaders
  or pixels with **no witness it ran, no measurement it improved the frame, and no
  oracle it didn't corrupt state.** The membrane's whole thesis is that the LLM is
  state-blind; an LLM feature that adds output *without* adding observability is the
  disease, not the cure.

---

## 5. Verdict — The Single Highest-Leverage Next Build

> **Update 2026-06-05 — Tier 0 SHIPPED & compiler-verified** (commits 1f477e8, e99a406):
> 0.1 LiveBridge telemetry+control and 0.2 compile-error JSONL are built into RAW.dll
> and host-verified by execution. The next build is **1.1 (the `sawNaN` float-precision
> producer)** per the verdict below, or **Tier W.2 (the commit gate)** for the write-gate.


**Wire LiveBridge (0.1), then ship the `sawNaN` float-precision producer (1.1) on
the same readback discipline.** LiveBridge is the rarest thing in the backlog: a
fully-implemented oracle (Tick/EmitMetrics complete, host `watch()` complete,
`control.ini` round-trip complete) gated behind a single missing call site —
roughly ten lines converts a permanently silent dead zone into a live per-frame
numeric stream and an out-of-game control channel. It is the substrate that 1.1
(per-frame NaN), 3.5 (build freshness), and every future trend oracle plug into,
so wiring it first is not just cheap — it unblocks the tier above it. Pairing it
immediately with the `sawNaN` producer closes the membrane's most embarrassing
honesty gap (a field that has been emitting `false` on every frame since it was
declared) and lands the first true *source-level* numeric witness, replacing
NaN-inferred-from-a-clamped-8-bit-BMP with NaN-read-at-float-precision. Two days of
work, and the membrane goes from "two static substrates plus aspirations" to "a
live, falsifiable, per-frame oracle the host can watch in real time."
