# Certificate Bridge — Increment 3 Plan

> **For agentic workers:** REQUIRED SUB-SKILL: superpowers:subagent-driven-development. Steps use checkbox (`- [ ]`).

**Goal:** Make raw (the C++ sight organ) emit a **Certificate** — the shared witnessed form — for its reconcile verdict, in the exact serialized *shape* of coherence-membrane's `Certificate.to_dict()`, so the rest of the organism (mind, witness channel, ledger) consumes raw's output through the one currency every organ speaks. After this, raw is *in the body*.

**Architecture:** A zero-dep `raw::Certificate` value type whose JSON serialization is structurally identical to the Python `Certificate.to_dict()` — `{"claim","verdict","oracle","evidence"}`, verdict ∈ `{"verified","refuted","unverifiable"}` (lowercase), evidence an array of `[key,value]` string pairs. Factory functions map raw's `ReconcileResult` (and the `ArenaStats` witness from increment 2) into Certificates; the CLI writes `certificate.json`. A final cross-language step proves a Python organ can load it (`coherence_membrane.Certificate`/`Verdict`) and the cdev ledger can witness it (file fact → `recall --verify` = MATCH).

**Tech Stack:** C++23, standard library only — the JSON is hand-emitted, no third-party. The cross-language *verification* step uses Python 3.12 + the existing `coherence_membrane` and `ledger` packages, but raw itself stays zero-dep and decoupled (the JSON **shape** is the contract, not a code dependency). Same MSVC multi-config build as increments 1–2.

## Global Constraints

- C++23, standard library only. No third-party, no DirectX. Everything under `native/`: headers `native/raw/`, impl `native/src/`, tests `native/tests/`, namespace `raw`.
- The emitted JSON MUST match coherence-membrane's `Certificate.to_dict()` SHAPE exactly (confirmed against `c:/dev/public/coherence-membrane/src/coherence_membrane/certificate.py:18-40`):
  - keys, in order: `"claim"`, `"verdict"`, `"oracle"`, `"evidence"`
  - `verdict` is a **lowercase** string: `"verified"`, `"refuted"`, or `"unverifiable"`
  - `evidence` is an array of 2-element `[key, value]` arrays of strings (Python emits `[list(p) for p in evidence]`)
  - byte-for-byte parity with Python `json.dumps` is **not** required (Python consumers `json.load`; the ledger hashes raw's own file) — the *shape* is the contract. raw emits compact separators (`,`/`:`, no spaces), deterministic.
- Oracle id convention `<domain>-<algorithm>-v<N>` (kebab-case): reconcile oracle = `raw-rt-ao-v1`; arena oracle = `raw-arena-v1`.
- All JSON string values MUST be escaped (`"`, `\`, and control chars `< 0x20` as `\uXXXX`) so arbitrary claims/evidence stay valid JSON.
- Build (Windows/MSVC multi-config): `cmake --build native/build --config Release`; test `ctest --test-dir native/build -C Release -R <name> --output-on-failure`. New `src/*.cpp` and `tests/test_*.cpp` are auto-picked up by the CONFIGURE_DEPENDS globs; if a new file isn't seen, run `cmake -B native/build -S native` once then rebuild.

---

### Task 1: The Certificate value type + zero-dep JSON serialization

**Files:**
- Create: `native/raw/certificate.hpp`
- Create: `native/src/certificate.cpp`
- Create: `native/tests/test_certificate.cpp`

**Interfaces:**
- Consumes: nothing (stdlib only — `<string>`, `<vector>`, `<utility>`).
- Produces:
  - `enum class raw::Verdict { Verified, Refuted, Unverifiable };`
  - `const char* raw::verdict_str(Verdict);` → `"verified"` / `"refuted"` / `"unverifiable"`
  - `struct raw::Certificate { std::string claim; Verdict verdict; std::string oracle; std::vector<std::pair<std::string,std::string>> evidence; };`
  - `std::string raw::to_json(const Certificate&);` → `{"claim":..,"verdict":"..","oracle":..,"evidence":[["k","v"],..]}`, escaped, compact.

- [ ] **Step 1: Write the failing test**

`native/tests/test_certificate.cpp`:
```cpp
#include "raw/certificate.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
int main() {
    // verdict_str maps to the exact lowercase strings the Python spine uses
    CHECK(std::string(verdict_str(Verdict::Verified)) == "verified");
    CHECK(std::string(verdict_str(Verdict::Refuted)) == "refuted");
    CHECK(std::string(verdict_str(Verdict::Unverifiable)) == "unverifiable");

    // canonical shape matches coherence-membrane Certificate.to_dict()
    Certificate c{"(A -> A)", Verdict::Verified, "raw-rt-ao-v1",
                  {{"valid", "ground truth matched"}}};
    CHECK(to_json(c) ==
        "{\"claim\":\"(A -> A)\",\"verdict\":\"verified\",\"oracle\":\"raw-rt-ao-v1\","
        "\"evidence\":[[\"valid\",\"ground truth matched\"]]}");

    // empty evidence -> []
    Certificate e{"x", Verdict::Unverifiable, "o", {}};
    CHECK(to_json(e) ==
        "{\"claim\":\"x\",\"verdict\":\"unverifiable\",\"oracle\":\"o\",\"evidence\":[]}");

    // string escaping: a quote and a backslash stay valid JSON
    Certificate q{"a\"b\\c", Verdict::Refuted, "o", {}};
    CHECK(to_json(q).find("\"claim\":\"a\\\"b\\\\c\"") != std::string::npos);

    // multiple evidence pairs preserve order
    Certificate m{"m", Verdict::Verified, "o", {{"k1","v1"},{"k2","v2"}}};
    CHECK(to_json(m).find("\"evidence\":[[\"k1\",\"v1\"],[\"k2\",\"v2\"]]") != std::string::npos);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_certificate`
Expected: FAIL — `raw/certificate.hpp` does not exist. (If not picked up: `cmake -B native/build -S native`, then rebuild.)

- [ ] **Step 3: Write minimal implementation**

`native/raw/certificate.hpp`:
```cpp
#pragma once
#include <string>
#include <vector>
#include <utility>
namespace raw {
// The shared witnessed form. JSON shape is byte-shape-compatible with
// coherence-membrane's Certificate.to_dict(): {claim, verdict, oracle, evidence}.
enum class Verdict { Verified, Refuted, Unverifiable };
const char* verdict_str(Verdict v);   // "verified" | "refuted" | "unverifiable"
struct Certificate {
    std::string claim;
    Verdict verdict;
    std::string oracle;                                       // e.g. "raw-rt-ao-v1"
    std::vector<std::pair<std::string, std::string>> evidence; // ordered (key,value) pairs
};
std::string to_json(const Certificate& c);
}
```

`native/src/certificate.cpp`:
```cpp
#include "raw/certificate.hpp"
#include <cstdio>
namespace raw {
const char* verdict_str(Verdict v){
    switch (v){
        case Verdict::Verified:     return "verified";
        case Verdict::Refuted:      return "refuted";
        case Verdict::Unverifiable: return "unverifiable";
    }
    return "unverifiable";
}
// Emit a JSON string literal (with surrounding quotes), escaping per RFC 8259.
static std::string jstr(const std::string& s){
    std::string o = "\"";
    for (unsigned char ch : s){
        switch (ch){
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (ch < 0x20){ char b[8]; std::snprintf(b, sizeof b, "\\u%04x", ch); o += b; }
                else o += static_cast<char>(ch);
        }
    }
    o += "\"";
    return o;
}
std::string to_json(const Certificate& c){
    std::string o = "{";
    o += "\"claim\":"   + jstr(c.claim)                 + ",";
    o += "\"verdict\":" + jstr(verdict_str(c.verdict))  + ",";
    o += "\"oracle\":"  + jstr(c.oracle)                + ",";
    o += "\"evidence\":[";
    for (std::size_t i = 0; i < c.evidence.size(); ++i){
        if (i) o += ",";
        o += "[" + jstr(c.evidence[i].first) + "," + jstr(c.evidence[i].second) + "]";
    }
    o += "]}";
    return o;
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_certificate --output-on-failure`
Expected: `test_certificate` PASS. Full suite stays green: `ctest --test-dir native/build -C Release` (15/15).

- [ ] **Step 5: Commit**

```bash
git add native/raw/certificate.hpp native/src/certificate.cpp native/tests/test_certificate.cpp
git commit -m "feat(native): Certificate value type + zero-dep JSON (the shared witnessed form)"
```

---

### Task 2: Map the reconcile verdict and the arena witness into Certificates

**Files:**
- Modify: `native/raw/certificate.hpp` (add the two factory declarations)
- Create: `native/src/certificate_from.cpp`
- Create: `native/tests/test_certificate_from.cpp`

**Interfaces:**
- Consumes: `raw::Certificate`, `raw::Verdict`; `raw::ReconcileResult` (from `raw/reconcile.hpp`: `{ Buffer<float> errorMap; float rmse, maxError; int pixels; bool withinTolerance; }`); `raw::ArenaStats` (from `raw/arena.hpp`: `{ size_t budget, used, high_water, allocations, refusals }`).
- Produces:
  - `raw::Certificate raw::certificate_from_reconcile(const ReconcileResult& r, float tolerance);`
    verdict = Unverifiable if `pixels==0`, else Verified if `withinTolerance`, else Refuted; oracle `"raw-rt-ao-v1"`; evidence `pixels,rmse,maxError,tolerance` (rmse/maxError/tolerance formatted `%.4f`).
  - `raw::Certificate raw::certificate_from_arena(const ArenaStats& s);`
    verdict = Verified if `refusals==0` else Refuted; oracle `"raw-arena-v1"`; evidence `budget,used,high_water,allocations,refusals`.

- [ ] **Step 1: Write the failing test**

`native/tests/test_certificate_from.cpp`:
```cpp
#include "raw/certificate.hpp"
#include "raw/reconcile.hpp"
#include "raw/arena.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
int main() {
    // DIVERGENT reconcile -> refuted, with witnessed numbers
    ReconcileResult d; d.rmse = 0.1294f; d.maxError = 0.6094f; d.pixels = 37996; d.withinTolerance = false;
    std::string jd = to_json(certificate_from_reconcile(d, 0.12f));
    CHECK(jd.find("\"oracle\":\"raw-rt-ao-v1\"") != std::string::npos);
    CHECK(jd.find("\"verdict\":\"refuted\"") != std::string::npos);
    CHECK(jd.find("[\"pixels\",\"37996\"]") != std::string::npos);
    CHECK(jd.find("[\"rmse\",\"0.1294\"]") != std::string::npos);
    CHECK(jd.find("[\"tolerance\",\"0.1200\"]") != std::string::npos);

    // within tolerance -> verified
    ReconcileResult w; w.rmse = 0.01f; w.maxError = 0.05f; w.pixels = 1000; w.withinTolerance = true;
    CHECK(to_json(certificate_from_reconcile(w, 0.12f)).find("\"verdict\":\"verified\"") != std::string::npos);

    // nothing to compare -> unverifiable (never confabulate)
    ReconcileResult z; z.pixels = 0; z.withinTolerance = false;
    CHECK(to_json(certificate_from_reconcile(z, 0.12f)).find("\"verdict\":\"unverifiable\"") != std::string::npos);

    // arena within budget -> verified (BOUNDED)
    ArenaStats ok{256, 128, 200, 3, 0};
    std::string ja = to_json(certificate_from_arena(ok));
    CHECK(ja.find("\"oracle\":\"raw-arena-v1\"") != std::string::npos);
    CHECK(ja.find("\"verdict\":\"verified\"") != std::string::npos);
    CHECK(ja.find("[\"budget\",\"256\"]") != std::string::npos);
    CHECK(ja.find("[\"refusals\",\"0\"]") != std::string::npos);

    // arena over budget -> refuted (BREACHED)
    ArenaStats bad{256, 0, 0, 0, 2};
    CHECK(to_json(certificate_from_arena(bad)).find("\"verdict\":\"refuted\"") != std::string::npos);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run to verify it fails**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_certificate_from`
Expected: FAIL — `certificate_from_reconcile` / `certificate_from_arena` not declared.

- [ ] **Step 3: Write minimal implementation**

Add to `native/raw/certificate.hpp` (inside `namespace raw`, after `to_json`):
```cpp
struct ReconcileResult;   // fwd (defined in raw/reconcile.hpp)
struct ArenaStats;        // fwd (defined in raw/arena.hpp)
Certificate certificate_from_reconcile(const ReconcileResult& r, float tolerance);
Certificate certificate_from_arena(const ArenaStats& s);
```

`native/src/certificate_from.cpp`:
```cpp
#include "raw/certificate.hpp"
#include "raw/reconcile.hpp"
#include "raw/arena.hpp"
#include <cstdio>
#include <string>
namespace raw {
static std::string f4(double v){ char b[64]; std::snprintf(b, sizeof b, "%.4f", v); return b; }
Certificate certificate_from_reconcile(const ReconcileResult& r, float tolerance){
    Verdict v = (r.pixels == 0) ? Verdict::Unverifiable
              : (r.withinTolerance ? Verdict::Verified : Verdict::Refuted);
    return Certificate{
        "screen-space AO matches ray-traced ground truth within tolerance",
        v, "raw-rt-ao-v1",
        { {"pixels", std::to_string(r.pixels)},
          {"rmse", f4(r.rmse)},
          {"maxError", f4(r.maxError)},
          {"tolerance", f4(tolerance)} }
    };
}
Certificate certificate_from_arena(const ArenaStats& s){
    Verdict v = (s.refusals == 0) ? Verdict::Verified : Verdict::Refuted;
    return Certificate{
        "arena stayed within its memory budget",
        v, "raw-arena-v1",
        { {"budget", std::to_string(s.budget)},
          {"used", std::to_string(s.used)},
          {"high_water", std::to_string(s.high_water)},
          {"allocations", std::to_string(s.allocations)},
          {"refusals", std::to_string(s.refusals)} }
    };
}
}
```

- [ ] **Step 4: Run to verify it passes**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_certificate_from --output-on-failure`
Expected: `test_certificate_from` PASS. Full suite green: `ctest --test-dir native/build -C Release` (16/16).

- [ ] **Step 5: Commit**

```bash
git add native/raw/certificate.hpp native/src/certificate_from.cpp native/tests/test_certificate_from.cpp
git commit -m "feat(native): map reconcile verdict + arena witness into Certificates"
```

---

### Task 3: The CLI emits certificate.json (end-to-end, in-process tested)

**Files:**
- Modify: `native/app/main.cpp` (emit `certificate.json` + print the certificate line)
- Create: `native/tests/test_certificate_emit.cpp` (full pipeline → Certificate → JSON, asserted in-process)

**Interfaces:**
- Consumes: the whole sight pipeline (`buildTestScene`, `rasterize`, `LinearAccel`, `computeRTAO`, `computeSSAO`, `reconcile`) + `certificate_from_reconcile` + `to_json`. All already linked via `raw_native`.
- Produces: a `certificate.json` artifact next to the existing frame/AO outputs; no new public API.

- [ ] **Step 1: Write the failing test**

`native/tests/test_certificate_emit.cpp`:
```cpp
// The real sight pipeline must produce a Certificate whose JSON the body can consume.
#include "raw/raster.hpp"
#include "raw/accel.hpp"
#include "raw/ray_ao.hpp"
#include "raw/ssao.hpp"
#include "raw/reconcile.hpp"
#include "raw/certificate.hpp"
#include "check.hpp"
#include <string>
using namespace raw;
int main() {
    const int W = 256, H = 256;
    Scene s = buildTestScene(W, H);
    GBuffer g = rasterize(s, W, H);
    LinearAccel accel; accel.build(s);
    Buffer<float> aoRT = computeRTAO(g, accel, 64, 2.0f);
    Buffer<float> aoSS = computeSSAO(g, 24, 2.0f);
    ReconcileResult rec = reconcile(aoSS, aoRT, g.mask, 0.12f);

    std::string j = to_json(certificate_from_reconcile(rec, 0.12f));
    // the built-in scene is deterministically DIVERGENT (approximation != ground truth)
    CHECK(rec.pixels > 0);
    CHECK(!rec.withinTolerance);
    CHECK(j.find("\"oracle\":\"raw-rt-ao-v1\"") != std::string::npos);
    CHECK(j.find("\"verdict\":\"refuted\"") != std::string::npos);
    CHECK(j.find("\"claim\":\"screen-space AO matches ray-traced ground truth within tolerance\"")
          != std::string::npos);
    return raw_test_summary();
}
```

- [ ] **Step 2: Run it (honest note: this is a regression guard, not a RED)**

Run: `cmake --build native/build --config Release && ctest --test-dir native/build -C Release -R test_certificate_emit --output-on-failure`
The factory + `to_json` already exist (Task 2), so this end-to-end contract test will **pass immediately** — it is not a red-first cycle. Its job is to pin the contract (real scene → deterministic `refuted` verdict → consumable JSON) so a later engine change can't silently break what the body consumes. The genuinely new behavior in this task is the CLI *writing the file*, verified by the run in Step 4. (If the new test file isn't picked up: `cmake -B native/build -S native`, then rebuild.)

- [ ] **Step 3: Write minimal implementation (wire the CLI)**

Modify `native/app/main.cpp` — add includes and emit the certificate. Add near the top includes:
```cpp
#include "raw/certificate.hpp"
#include <fstream>
```
Then, immediately after the existing `ReconcileResult rec = reconcile(aoSS, aoRT, g.mask, 0.12f);` line and before/after the existing writes, add:
```cpp
    Certificate cert = certificate_from_reconcile(rec, 0.12f);
    std::string certJson = to_json(cert);
    std::ofstream(out + "/certificate.json") << certJson;
```
And add one line after the existing `reconcile:` printf:
```cpp
    std::printf("certificate: %s\n", certJson.c_str());
```

- [ ] **Step 4: Run to verify it passes + emits the artifact**

Run:
```bash
cmake --build native/build --config Release
ctest --test-dir native/build -C Release -R test_certificate_emit --output-on-failure
./native/build/Release/raw_native_cli.exe native/build
```
Expected: `test_certificate_emit` PASS; the CLI prints a `certificate: {...}` line and writes `native/build/certificate.json`. Full suite green: `ctest --test-dir native/build -C Release` (17/17).

- [ ] **Step 5: Commit**

```bash
git add native/app/main.cpp native/tests/test_certificate_emit.cpp
git commit -m "feat(native): CLI emits certificate.json — raw speaks the organism's currency"
```

---

## Final verification — the cross-language seam (raw enters the body)

This is the increment's payoff: prove a Python organ can consume raw's Certificate and the cdev ledger can witness it. Run from the raw repo root after Task 3. (This is a one-shot proof; it is **not** committed into raw — raw stays zero-dep. The JSON shape is the contract.)

- [ ] **Emit a fresh certificate** (already done in Task 3 Step 4): `native/build/certificate.json` exists.

- [ ] **Prove a Python organ consumes it** — the shape matches `coherence_membrane.Certificate`:
```bash
python - <<'PY'
import json, sys
sys.path.insert(0, "c:/dev/public/coherence-membrane/src")
from coherence_membrane.certificate import Verdict
cert = json.load(open("native/build/certificate.json"))
assert set(cert) == {"claim","verdict","oracle","evidence"}, cert.keys()
assert cert["verdict"] in {v.value for v in Verdict}, cert["verdict"]
assert all(isinstance(p, list) and len(p) == 2 for p in cert["evidence"]), cert["evidence"]
print("SEAM OK ->", cert["verdict"], cert["oracle"], "| evidence pairs:", len(cert["evidence"]))
PY
```
Expected: `SEAM OK -> refuted raw-rt-ao-v1 | evidence pairs: 4`

- [ ] **The witness channel records it** — a file fact over the artifact, re-verifiable:
```bash
export PYTHONPATH="c:/dev/project-docs/tools"
STORE="c:/dev/project-docs/ledger/cdev-ground-truth.json"
python -m ledger --store "$STORE" record --id raw-reconcile-certificate --kind file \
  --path "c:/dev/protected/source-corpus/raw/native/build/certificate.json" \
  --claim "raw sight organ emitted a Certificate (SSAO vs RT-AO reconcile) the body consumed" \
  --tags raw certificate sight seam
python -m ledger --store "$STORE" recall --verify
```
Expected: `recorded 'raw-reconcile-certificate' (file)`; `recall --verify` lists it as **MATCH** (the file hash re-checks). Note: leave `project-docs` uncommitted (contested tree) and flag it — do not commit the ledger change there without coordination.

- [ ] Full C++ suite green: `ctest --test-dir native/build -C Release` (expect 17/17).
- [ ] Zero third-party includes in the new C++ (`<string>`, `<vector>`, `<utility>`, `<cstdio>`, `<fstream>` only).

## Deferred (increment 4 and beyond)

- **Engine retrofit onto the arena:** route `Buffer<T>`/scene/accel storage through one `Arena` so the whole frame allocates from a bounded, witnessed budget, and the CLI emits the **arena** Certificate (`certificate_from_arena`) alongside the reconcile one — both organs (sense + substrate) witnessed in one run.
- **A `raw`/`render` ledger kind:** a custom perceiver+criterion that re-runs the organ and re-checks the verdict (today the seam uses the existing file-hash kind, which witnesses the artifact, not the recomputation).
- **Certificate `compose`:** combine the reconcile + arena Certificates into one frame-level verdict (mirrors coherence-membrane's `compose`).
- **Actuator contract (Axis B):** wire `workstation`/`accountable-surface` to the same Certificate + ledger seam.
