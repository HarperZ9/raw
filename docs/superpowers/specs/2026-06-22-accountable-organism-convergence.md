# The Accountable Organism — Convergence Design

> Date: 2026-06-22 · Status: architecture (the bringing-together) · Home: raw repo for now; promote to `project-docs/atlas` when that tree lands.

## 0 · Thesis (operator-set)

We are not building a spatial engine. We are building **one cohesive organism in the
machine, where every organ is accountable for its actions** — the senses, the mind, and
**especially the actuators**. raw (sight) is the first native organ to wire into the body.
Accountability is not a feature bolted on; it is the membrane every organ breathes through.

This is the same operation everywhere — *the reconcile*: perceive an artifact to a
witnessed form, check it against a criterion it did not author, carry a re-checkable proof,
say UNVERIFIABLE when you can't. The organism is that operation, instantiated per organ,
with the verdict carried in one shared form.

## 1 · The accountability currency — the Certificate

Every organ, on every action, emits a **Certificate** (already defined in
`coherence-membrane/certificate.py`):

```
Certificate { verdict: VERIFIED | REFUTED | UNVERIFIABLE,
              oracle:  versioned id (the tool's own provenance, e.g. "raw-rt-ao-v1"),
              claim,   evidence,  proof }
```

The Certificate is the shared witnessed form — the cross-organ IR seed (the long-deferred
L0/L1: *every organ emits the IR*). The **cdev ledger** witnesses it (`record_fact`); the
ledger's `recall --verify` re-checks it later → MATCH / DRIFT / UNVERIFIABLE. An organ that
cannot produce a Certificate for what it did has not finished its action.

## 2 · The three organ classes, graded by reversibility

Accountability is **graded by how hard the action is to undo.** This is the load-bearing
asymmetry the operator named.

### Senses — perception (read-only; accountable for honest witnessing)

**All senses, and every type of input data and information** (operator-set). The sensorium
is modality-general: any input becomes a witnessed form carrying a Certificate. raw (sight)
is the first, not the whole.

- **Modalities (open set):** image/render (`raw`) · audio/waveform · video/sequence
  (`statechain` diff-chains) · text/document · structured/relational (`qdb`/`qsql`,
  tabular, JSON) · signals & time-series (`signal-kernels`, `anomaly-kernels`) ·
  filesystem/OS state (`perceivers`) · web/network responses (guarded fetch) · provenance
  chains (`provenance-sensorium`) · geometry/field/graph (the sensory-transform-algebra
  spine: Field · Geometry · Graph planes). New modalities are new sense organs on the same
  contract — never a new spine.
- **Contract:** turn any input into a witnessed form, **never confabulate**. When the state
  cannot be witnessed, the verdict is **UNVERIFIABLE**, not a plausible guess. (Native
  answer to "AI can't say 'I don't know'" — Welch Labs 109♥.)
- **Native, per-modality:** witness the artifact in its *own* form (pixels, samples, bytes,
  rows, fs facts) — do not re-route everything through a text manifold (Welch Labs 91♥).
  One uncertainty-carrying transform algebra underlies them all (the ratified L0 frontier);
  each sense is a binding of it to a modality.

### Mind — cognition (accountable for sound checking; **bilateral**)
`coherence-membrane` reconcile/refine · the verifier ladder (DPLL / truth-table /
resolution / Farkas-LRA / DPLL(T)) · the criteria (novelty / origin / fitness) ·
the grounded critics (render-critic).
- Contract: the criterion is **not authored by the artifact being judged**; the proof is
  **re-checkable**. The ladder already carries deductive proofs, not tokens.
- **Bilateral:** the mind also holds the *operator's* decisions/code/ethics accountable at
  closure checkpoints (the equalizer). The mind is not exempt from the membrane.

### Actuators — action (**hardest; gated; the operator's emphasis**)
`accountable-surface` (native fs / web / os channels) · `workstation` (bounded real
actuation) · `proof-surface` (the gate).
- Contract — **every** actuator action passes this loop before it counts:
  1. **Gate first** — `allow / deny / needs-human`, **default-deny**, **fail-closed**.
     Argv-allowlist, realpath+commonpath bounds, `--execute` off by default, empty-grants
     inert. (Already shipped + opus-proven in `workstation`.)
  2. **Act bounded** — reversible-preferred; stay inside the grant.
  3. **Re-perceive the effect** — witness what actually changed (not a claim that it did).
  4. **Witness** — emit the Certificate; record to the ledger. An actuator that cannot
     witness its effect **must not claim success** — it returns UNVERIFIABLE.
- This is the native answer to AI acting without accountability: an action is not "done"
  until it is gated, performed, re-perceived, and witnessed. Hard-to-reverse / outward-facing
  actions require `needs-human`.

## 3 · How raw joins the body

raw is the **sight** organ. Increment 1 (in progress) builds its internal reconcile (a
rendered approximation checked against a ray-traced ground truth it did not author). The
convergence step (increment 2) makes raw emit the **Certificate** for that verdict and
record it to the cdev ledger — so the mind (render-critic) and the witness channel
(`perceptv`) consume raw's output through the same form every other organ uses. raw stops
being a standalone renderer and becomes the eye of an accountable body.

```
WORLD ──► [SENSES: raw·perceptv·perceivers] ──Certificate──► [MIND: reconcile/refine/critics]
                                                                   │ verdict + proof
                                                                   ▼
                                              [GATE: proof-surface] allow/deny/needs-human
                                                                   │ allow
                                                                   ▼
                                   [ACTUATORS: fs/web/os] ──act──► re-perceive ──Certificate──► WORLD
                                                                   │
   every Certificate ───────────────────────────────────────────► [WITNESS: cdev ledger] (append-only, re-verifiable)
                                                                   │
                                              [BILATERAL CRITIC] ──► holds the operator accountable too
```

## 4 · The gaps this attacks natively (from the 5-video synthesis)

| Gap (video) | Native organ answer |
|---|---|
| "AI can't say 'I don't know'" (Welch Labs 109♥) | UNVERIFIABLE as a first-class organ output |
| text is a lossy downsample of language (Welch 91♥) | native per-modality senses (raw=pixels, perceivers=fs/os) |
| LLM eval broken / judge from same distribution (CS229) | mind's criterion is independent of the artifact |
| hallucination / unverifiable provenance (CS229) | provenance organ checks claims vs sources the model didn't generate |
| reward over-optimization (CS229) | criterion ≠ self-authored (the reconcile's independence law) |
| GPU correctness = "compare vs CPU reference" (CUDA) | that comparison IS the reconcile — named as the spine, not hygiene |
| measurement = forced witnessing (Bell) | perception forces a witnessed form; the pattern is discovered, not invented |

Not crossed (honest): the entropy floor, the bandwidth wall, Bell nonlocality. The organism
does not make computation more powerful — it makes it **accountable**. Different axis.

## 5 · Build order

1. **raw engine core** (increment 1, in progress) — the sight organ's internal reconcile.
2. **raw → Certificate + ledger bridge** (increment 2) — wire sight into the body using the
   shared form. First cross-organ, cross-language seam (native C++ organ → witnessed form →
   Python spine).
3. **Actuator accountability contract** (increment 3) — formalize the gate→act→re-perceive→
   witness loop as the shared contract; the `workstation`/`accountable-surface` organs
   already implement it — wire them to the same Certificate + ledger, actuators gated hardest.
4. **Generalize** — each organ-class's accountability contract documented in the atlas;
   the Certificate becomes the organism's bloodstream.

## 6 · Honest bounds

- Cross-language: the Certificate is the seam; raw (C++) emits JSON matching
  `coherence-membrane`'s `Certificate.to_dict()`; the Python organs consume it. Confirm the
  exact JSON shape against the live `certificate.py` during increment 2 (do not assume).
- This is a body plan, not a finished body. Coverage is demonstrated by organs wired, not
  asserted whole. Today: sight is being built; mind and actuators exist but are not yet on
  the shared Certificate. The convergence is the wiring, one organ at a time.
