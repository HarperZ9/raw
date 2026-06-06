# Coherence-membrane artifact schema (the seam)

The membrane's only coupling between the in-process **probes** (RAW's C++ that emits)
and the out-of-process **adjudicator** (the `tools/eyes` host readers) is this set of
`live/` artifacts. This file is the explicit contract — verified emitter↔reader against
live bytes by the 2026-06-06 pre-live audit (all 12 forward contracts agree). It is also
the interface a future `membrane-core` extraction publishes; the probes are one binding.

Conventions: emit-policy is **always** (every relevant call), **on-corruption** (only when
a violation is found → an empty file means "clean"), or **gated** (off until an INI flag).
The host reader treats a missing/empty file as clean/zero, never an error.

| Artifact | Emitter (C++) | Reader (host) | Emit | Verdict key |
|---|---|---|---|---|
| `bindings.jsonl` | `BindingLedger::Snapshot` | `bindings.py analyze/asserts` | always (per marked draw/hazard) | reader recomputes (`asserts` emitted but ignored) |
| `restores.jsonl` | `PhaseDispatcher::EmitRestoreCheck` | `bindings.read_restores` | on-corruption | empty ⇒ clean |
| `pipeline_state.jsonl` | `WrappedContext::EmitStateLine`/`EmitPipelineState` | `bindings.read_pipeline_state` | on-change + on-violation | `clean` (no assert rows) |
| `shader_reflection.jsonl` | `SB_ShaderDebug::ReflectShaderBindings` | `bindings.read_reflection` | per shader compile | `clean` (no REGISTER_MISMATCH) |
| `cb_meta.jsonl` | `CBDirtyTracker::ScanCB` + `WrappedContext::EmitCbSizeMismatch` | `raw_eyes.read_cb_dump` | on-corruption | `size_mismatch`>0 blocks; NaN/Inf **advisory** |
| `texture_inspect.jsonl` | `TextureDump::WriteTextureInspect` | `raw_eyes.read_texture_inspect` | per dump (F5/throttled) | `numeric_ok` |
| `compile_errors.jsonl` | `SB_ShaderDebug::WriteCompileJson` | `raw_eyes.read_compile_log` | per compile | `all_built` |
| `resources.jsonl` | `WrappedDevice::RAW_EmitResourceCreate` | `raw_eyes.read_resources` | **gated** `[Diagnostics] ResourceLog` | advisory (high-water) |
| `shader_variants.jsonl` | `SB_ShaderDebug::WriteShaderVariant` + `WrappedContext` shader_used | `raw_eyes.read_shader_variants` | per compile + per bind (deduped) | advisory |
| `temporal.jsonl` | `TAAManager::EmitTemporal` | `bindings.read_temporal` | per history pass (TAA) | `clean` (no READ_EQ_WRITE/NO_TOGGLE) |
| `ranges.jsonl` | `RangeOracle::EmitRanges` | `raw_eyes.read_ranges` | **gated** `[Diagnostics] GpuReadback` + wired pass | `ranges_ok` |
| `metrics.jsonl` + `latest.json` | `LiveBridge::Tick`/`EmitMetrics` | `raw_eyes.watch` | always (per frame) | trend (advisory) |

## Field reference

Every key a reader CONSUMES is listed here and is written by the emitter (audit-verified).
Extra emitted keys (noted) are harmless. Types: `i` int, `f` float, `s` string, `b` bool,
`[]` array, `{}` object.

- **bindings.jsonl** — `frame`:i `phase`:s `pass`:s `op`:s `stage`:s(PS/VS/GS/HS/DS/CS)
  `srv`:{tN:s} `rtv`:[s] `dsv`:s|null `uav`:[s] · `asserts`:[] *(emitted, reader ignores —
  it recomputes invariants host-side).* One row per populated stage per marked draw.
- **restores.jsonl** — `phase`:i(*pipeline-stage* enum, not render-phase) `assert`:"STATE_NOT_RESTORED"
  `dirty`:i `tid`:i(GetCurrentThreadId) `fields`:[s]. Only on a dirty mid-frame restore.
- **pipeline_state.jsonl** — *state rows:* `kind`:"state" `obj`:s `ptr`:s + obj-specific
  (`enable/src/dst/op/writemask/a2c` blend; `depth_enable/write/func/stencil` depth;
  `filter/addr_u/addr_v/cmp` sampler; `fill/cull/front_ccw/scissor/depth_clip` raster).
  *assert rows:* `assert`:s(VIEWPORT_SIZE_ZERO/SCISSOR_OUTSIDE_VIEWPORT) `detail`:s.
- **shader_reflection.jsonl** — `shader`:s `entry`:s `profile`:s `name`:s `type`:i(2=SRV)
  `reg`:i `count`:i. Reader flags a name suggesting effect-X at effect-Y's documented register.
- **cb_meta.jsonl** — *NaN/Inf rows:* `frame`:i `ptr`:s `size`:i `has_nan`:b `has_inf`:b
  `nan_at`:i `min`:f `max`:f `bind`:s("PS b3"|"?"). *overflow rows:* `assert`:"CB_SIZE_MISMATCH"
  `ptr`:s `wrote`:i `capacity`:i. **NaN/Inf is heuristic** (no CB layout → int/bitfield members
  read as NaN/Inf); the write-gate blocks only on CB_SIZE_MISMATCH.
- **texture_inspect.jsonl** — `file`:s(`frameNNNN_<pass>`) `format`:s `w`:i `h`:i `has_nan`:b
  `has_inf`:b `nan_count`:i `first_nan`:[i,i] `numeric_ok`:b. (`file` is frame-indexed — the
  reader's per-`file` dedup over-counts passes; findings are correct.)
- **compile_errors.jsonl** — `ts`:s `file`:s `entry`:s `profile`:s `succeeded`:b `compile_ms`:f
  `errors`:[{`line`:i `col`:i `code`:s `sev`:s `msg`:s}]. Last attempt per file wins.
- **resources.jsonl** — `op`:"create" `type`:s(buffer/texture2d) `ptr`:s `size`:i. Create-side
  only (no Release); host reports a monotonic allocation high-water.
- **shader_variants.jsonl** — *compile rows:* `hash`:s(FNV over src+**sorted** defines+entry+target)
  `file`:s `entry`:s `profile`:s `defines`:s(sorted `k=v|...`) `ndefs`:i `succeeded`:b.
  *bind rows:* `kind`:"shader_used" `ps_hash`:s `vs_hash`:s. **Different hash domains** —
  correlate by file/entry, not hash.
- **temporal.jsonl** — `frame`:i `buffer`:s(`taa.history`) `read_idx`:i `write_idx`:i `warmup`:b.
- **ranges.jsonl** — `frame`:i `pass`:s `output`:s `min`:f `max`:f `nan`:i `inf`:i
  `verdict`:s(ok/CORRUPT). min/max via order-preserving sign-flip; 0/0 when only NaN/Inf.
- **metrics.jsonl / latest.json** — `frame`:i `luma_mean`:f `gpu_ms`:f `fps`:f (+ what
  `LiveFrameStats` carries). The reader's avg-trend also lists green_index/noise_rms/
  fireflies_pct **which LiveBridge does not emit** — they are silently omitted, not averaged.

## Why this is the extraction seam
The host readers depend on **nothing in RAW's C++** — only on this schema (verified:
`tools/eyes/*.py` import only stdlib + numpy + PIL). So `membrane-core` (doctrine +
`membrane_gate` + `build_stamp` + this contract + a probe SDK) lifts out cleanly; the
graphics readers become a `membrane-d3d11` plugin; RAW keeps its C++ probes as one binding
that emits to this contract. A different target (e.g. an LLM mediator) writes different
probes emitting their own artifacts under the same emit-policy/verdict-key discipline.
