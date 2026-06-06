# RAW — Development Doctrine

> Governs how RAW is built, with the same authority as `LINEAGE.md` and `STATUS.md`.
> One law, derived from a hard limitation. Written 2026-06-05.

## The premise (stated plainly)

The assistant building RAW has **no simulator underneath**. A compiler has a symbol
table with live bindings; a running program has a heap with actual values; a debugger
has both plus an execution cursor. An LLM has none of them — it does approximate
inference over text. So a question like *"what is bound to `t27` when the compositor
samples it"* is not answered by querying a state; it is pattern-matched from what
usually follows. That is **reliable for local, pure logic** (a shader's math given its
inputs) and **fails as the answer depends on state mutated far from where it is read**.
It is architectural, not a tuning gap. The dangerous part is that confidence does not
fall when accuracy does — *confident-but-wrong about a binding or a race is the default
failure, and the most expensive kind.*

D3D11's immediate context is the worst case: a giant implicit mutable state machine where
a draw's bindings are an emergent property of every `Set*`/`Clear*`/`Unbind` that ran
before it. Threads and mid-frame dispatch make it worse.

## The law

> **Observe inputs → reason locally → measure outputs.**
> **Never assert runtime state from the code in your head — instrument it.**

If a claim is about *state at a point*, *ordering*, *a binding*, *a race*, or *a numeric
range at runtime*, it must be answered by an artifact the program emits — not by reading
the source and reasoning. When such an artifact does not exist yet, the task is to build
it, then read it. We do not reason harder; we engineer the missing simulator.

## Strong zone vs dead zone

| May reason about it in-head | Must instrument and observe |
|-----------------------------|-----------------------------|
| A shader's math given known inputs | Which resource is actually bound at a slot |
| A pure function, local control flow | Whether a hazard (SRV∩RTV/DSV/UAV) exists |
| Algebraic identity (e.g. compositor identity-preservation) | Whether mid-frame dispatch restored game state |
| A self-contained data transform | Frame-to-frame temporal state, history buffers |
| Documentation / structure | Actual value ranges, NaN/Inf, overflow at runtime |

## The three substrates we engineer (to replace what the LLM lacks)

| What an LLM lacks | What RAW emits instead | Status |
|-------------------|------------------------|--------|
| symbol table (live bindings) | **binding ledger** — actual SRV/RTV/DSV/UAV per slot, per pass | spec: `tools/eyes/BINDING_LEDGER.md` (next) |
| heap (actual values) | **output metrics** — mean/min/max/NaN/green_index per pass | DONE: `tools/eyes/raw_eyes.py` (verified 12/12) |
| execution trace | **frame log** — ordered state ops + invariant assertions | folded into the ledger |

## Invariants RAW asserts at runtime (the catalog)

Each converts one of Pascal's "reason about it" questions into an engine-emitted fact:

1. **HAZARD** — any resource bound as SRV while also bound as the current RTV/DSV or a
   bound UAV. (read/write aliasing) → emitted at every draw/dispatch.
2. **DSV_BOUND_DURING_SAMPLE** — depth texture's SRV read while its DSV is bound writable.
3. **STATE_RESTORED** — after mid-frame dispatch, OM/RS/IA state diffed vs the pre-dispatch
   snapshot (`D3D11StateBackup`); mismatch logged.
4. **SLOT_MATCH** — SRV bound at t17–t38 matches the documented owner in `ARCHITECTURE.md`.
5. **RANGE / NAN** — per-pass output min/max + NaN/Inf count; expected ranges asserted
   (depth ∈ [0,1], reversed-Z monotonic, luma ≥ 0). The CB-overflow and clamp bugs were
   range failures — this is their tripwire.
6. **TEMPORAL** — which history buffer is read vs written each frame; warmup/clear honored.

## Workflow before any shader change

1. **Confirm inputs** via the binding ledger + input metrics — is the depth SRV actually
   bound? is `t27` actually the SSR output? *Most "the shader is broken" bugs are here —
   the plumbing, not the math.*
2. **Reason about the math** — only now, on known-good inputs, in the strong zone.
3. **Measure the output** with the eyes (`green_index`, `%black`, banding, diff vs vanilla).
   The change's effect is *measured*, not claimed.

## Division of labor

- **The engine is the source of truth on state** — bindings, hazards, ranges, ordering.
  The assistant never adjudicates these in prose.
- **The assistant is reliable on local/pure logic and on writing the instrumentation** —
  asserts are bounded, self-contained code, squarely in the strong zone.
- **The operator is the perceiver of last resort** — only for spatial/aesthetic calls the
  metrics cannot capture, and even there a number narrows it first (no he-said-she-said).

## Honest caveat

Instrumentation does not make the assistant infallible — a metric can be misread, an
assert can be buggy. What it does is convert *confident-but-wrong-in-head* into *a
checkable claim with a witness*. That is the entire win, and it is enough.
