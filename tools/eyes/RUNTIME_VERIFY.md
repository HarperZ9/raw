# Runtime verification — membrane roadmap-completion session

The eight oracles added this session are **compiler-verified** (every C++ patch
built RAW.dll + d3d11.dll clean) and **host-selftested** (each reader + the 1.3
reduction math has a selftest). The one thing the build environment cannot do is
launch Skyrim, so the *runtime emission* of each oracle is confirmed the next
time the game runs with both plugins installed. This file is that procedure.

## Before you deploy (preflight)

```
python RAW/tools/eyes/verify_runtime.py preflight
```
The pre-firing gate: confirms the build is FRESH (source == built DLL), both DLLs
are present in `build/Release`, and `Shaders/` is populated -- then prints the deploy
bundle + the optional oracle-gate INI keys. Resolve any `XX` before deploying, so a
stale/incomplete bundle never wastes a firing.

## One command, after a game session

Run RAW (RAW.dll + Shaders/ + d3d11.dll deployed into the MO2 mod folder), play
for a bit so the passes execute, then:

```
python RAW/tools/eyes/verify_runtime.py "<MO2 mods>/<RAW mod>/Data/SKSE/Plugins/RAW/live"
```

It reads the `live/` capture dir and prints, per oracle:

| Status   | Meaning |
|----------|---------|
| VERIFIED | artifact present **and** the new field/record is there — runtime-confirmed |
| STALE    | artifact present but the new field is missing → an **old DLL** is deployed; rebuild + redeploy |
| PENDING  | a conditional oracle that did not trigger this session (not a failure) |
| GATED    | the oracle is OFF until an INI flag is set (below) |
| ABSENT   | expected to fire in any RAW session but no artifact → check the deploy |

Exit code is non-zero only if anything is STALE/ABSENT (the states that mean
"the new binary isn't actually running").

## What each oracle needs to show VERIFIED

| Oracle | Artifact | Fires when |
|--------|----------|------------|
| 3.1 GS/HS/DS stage | `bindings.jsonl` (`stage` field) | any session with RAW marked passes (always) |
| 3.2 shader variant | `shader_variants.jsonl` | `shader_used` on every `PSSetShader` (always); compile-variant records only when RAW compiles via `D3DCompile` |
| 3.4 temporal | `temporal.jsonl` | whenever **TAA** runs |
| thread-id witness | `restores.jsonl` (`tid` field) | only on a **dirty** mid-frame restore (conditional) |
| 1.2-p3 CB bind | `cb_meta.jsonl` (`bind` field) | only when a **NaN/Inf constant buffer** is committed (conditional) |
| 3.6 VRAM watch | `resources.jsonl` | **GATED** — set `[Diagnostics] ResourceLog=1` in `d3d11_proxy.ini` |
| 1.3 per-pass range | `ranges.jsonl` | **GATED** — set `[Diagnostics] GpuReadback=1` **and** wire one pass's `post_execute` to its output SRV (see `RangeOracle.cpp`). GPU path is authored but never runtime-exercised; validate against a known-corrupt frame. |

So a *normal* play session should already show **3.1 and 3.2 VERIFIED** (and 3.4
if TAA is on). The conditional ones (tid, CB bind) only show VERIFIED if their
fault actually occurs — that's correct, not a miss. The two GATED ones need the
flag flipped; 1.3 additionally needs a pass wired (it ships dormant by design).

## To exercise the GATED oracles deliberately

1. `[Diagnostics] ResourceLog=1` (d3d11_proxy.ini) → `resources.jsonl` next launch.
2. `[Diagnostics] GpuReadback=1` (RAW config) **and** set one pass's
   `PassDef::post_execute` to `[](PassContext& c, const char* n, ID3D11ShaderResourceView*){ RangeOracle::Inspect(c.frameIndex, n, <that pass's ManagedRT.srv>); }`
   → `ranges.jsonl` next launch. Confirm the min/max/NaN/Inf against a frame you
   know is corrupt before trusting the verdict.

## Self-check

`python RAW/tools/eyes/verify_runtime.py selftest` proves the harness logic on a
synthetic `live/` (12/12) without a game.
