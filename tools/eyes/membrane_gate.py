#!/usr/bin/env python3
"""membrane_gate.py - the coherence membrane's WRITE-gate (Tier W.2).

The read-gate makes ground truth available; this refuses output the oracle
contradicts. Wired as a git commit-msg hook: it (1) requires the membrane's own
selftests to be green - an unverified membrane is net-negative - and (2) if the
commit message asserts a fix/verification, refuses the commit when an available
oracle (live/ captures) reports a CONTRADICTING violation. Degrades gracefully:
missing deps or no artifact => warn, never block (absence of proof != disproof).

  membrane_gate.py check <msgfile> [live_dir]  -> commit-msg gate (0=allow, 1=block)
  membrane_gate.py gate-selftest               -> prove the gate's own logic (no git)
  membrane_gate.py claim-lint <text> [live_dir] -> W.1: label unwitnessed claims unknown
"""
import sys, os, re, io, json, contextlib, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)

CLAIM_RE = re.compile(
    r"\b(fix(e[ds]|ing)?|verif(y|ied|ies)|resolv(e|ed|es)|works?|passing|"
    r"no\s+(hazard|nan)|clean|in-?game|confirmed)\b", re.I)

IN_GAME_RE = re.compile(r"\bin[- ]?game\b", re.I)


# --- W.1 claim-binding lint: map each stateful-claim class to the oracle that
# witnesses it. Absent a witness the claim must be labelled "unknown", never
# asserted. Advisory like W.2 (UNKNOWN never blocks; only CONTRADICTED does).
# Each later oracle tier appends its (class, module, reader, kw, key, artifact).
CLAIM_ORACLES = [
    ("binding",  "bindings", "asserts",              r"\b(t\d{1,2}|slot|bound|binding|hazard|srv|rtv|uav)\b", "clean",      "bindings.jsonl"),
    ("restore",  "bindings", "read_restores",        r"\b(restored?|state[- ]?restor|dirty pipeline)\b",     "clean",      "restores.jsonl"),
    ("pipeline", "bindings", "read_pipeline_state",  r"\b(blend|raster|cull|depth[- ]?(test|compare|func)|sampler|viewport|scissor)\b", "clean", "pipeline_state.jsonl"),
    ("register", "bindings", "read_reflection",      r"\b(register|reflect|dxbc|slot[- ]?to[- ]?register)\b", "clean",      "shader_reflection.jsonl"),
    ("cb",       "raw_eyes", "read_cb_dump",         r"\b(constant ?buffer|cbuffer|\bcb\b|uniform|updatesubresource)\b", "clean", "cb_meta.jsonl"),
    ("numeric",  "raw_eyes", "read_texture_inspect", r"\b(nan|inf|denorm|subnormal|numeric|float precision)\b", "numeric_ok", "texture_inspect.jsonl"),
    ("compile",  "raw_eyes", "read_compile_log",     r"\b(shader (compil|build)|hlsl error|fxc|d3dcompile)\b", "all_built", "compile_errors.jsonl"),
    ("ranges",   "raw_eyes", "read_ranges",           r"\b(range|pre-?clamp|out of range|per-?pass (min|max|nan))\b", "ranges_ok", "ranges.jsonl"),
    ("temporal", "bindings", "read_temporal",         r"\b(temporal|ping-?pong|history buffer|warmup|ghost(ing)?)\b", "clean", "temporal.jsonl"),
    ("resources","raw_eyes", "read_resources",        r"\b(vram|resource leak|allocation|createbuffer|createtexture)\b", None, "resources.jsonl"),
    ("shader",   "raw_eyes", "read_shader_variants",  r"\b(#?define|permutation|shader variant)\b", None, "shader_variants.jsonl"),
]


def claim_lint(text, live_dir=None):
    """W.1 write-gate: for each stateful-claim class in `text`, report whether a
    live/ oracle witnesses it. No witness => label "unknown", never assert.
    Advisory like W.2: UNKNOWN never blocks; only a CONTRADICTION does.
    Returns {results, must_label_unknown, contradicted, clean}."""
    text = text or ""
    try:
        mods = dict(zip(("bindings", "raw_eyes"), _imports()))
    except Exception:
        mods = {}
    results, unknown, contra = [], [], []
    for cls, mod, fn, kw, key, art in CLAIM_ORACLES:
        if not re.search(kw, text, re.I):
            continue
        witnessed = bool(live_dir and os.path.isfile(os.path.join(live_dir, art)))
        clean = None
        if witnessed and mods.get(mod) is not None:
            try:
                rep = getattr(mods[mod], fn)(live_dir)
                clean = bool(rep.get(key, True)) if rep else None
            except Exception:
                witnessed = False
        if not witnessed:
            status = "UNKNOWN"; unknown.append(cls)
        elif clean is False:
            status = "CONTRADICTED"; contra.append(cls)
        else:
            status = "WITNESSED_CLEAN"
        results.append({"class": cls, "artifact": art, "status": status})
    if re.search(r"\b(rebuilt|the dll|in-?game|build (is )?fresh|matches source)\b", text, re.I):
        fresh = build_freshness()
        if fresh is True:
            status = "WITNESSED_CLEAN"
        elif fresh is False:
            status = "CONTRADICTED"; contra.append("build")
        else:
            status = "UNKNOWN"; unknown.append("build")
        results.append({"class": "build", "artifact": "build_manifest.json", "status": status})
    return {"results": results, "must_label_unknown": unknown,
            "contradicted": contra, "clean": len(contra) == 0}


def _imports():
    import bindings, raw_eyes
    return bindings, raw_eyes

def run_membrane_selftests():
    """('pass'|'fail'|'unavailable', detail)."""
    try:
        bindings, raw_eyes = _imports()
        with tempfile.TemporaryDirectory() as td, contextlib.redirect_stdout(io.StringIO()):
            rb = bindings.selftest(os.path.join(td, "b"))
            rr = raw_eyes.selftest(os.path.join(td, "r"))
        if rb == 0 and rr == 0:
            return ("pass", "bindings + raw_eyes selftests green")
        return ("fail", "membrane selftest failed (bindings=%s raw_eyes=%s)" % (rb, rr))
    except Exception as e:
        return ("unavailable", "selftests could not run (%s)" % e.__class__.__name__)

def oracle_violations(live_dir):
    """Contradictions from available oracles. None => no artifact to verify against."""
    if not live_dir or not os.path.isdir(live_dir):
        return None
    try:
        bindings, raw_eyes = _imports()
    except Exception:
        return None
    v = []
    def safe(fn):
        try: return fn()
        except Exception: return None
    a = safe(lambda: bindings.asserts(live_dir))
    if a and not a.get("clean", True):
        v += ["binding: %s -> %s" % (x.get("pass"), [d["assert"] for d in x.get("asserts", [])])
              for x in a.get("violations", [])[:5]]
    c = safe(lambda: raw_eyes.read_compile_log(live_dir))
    if c and not c.get("all_built", True):
        v += ["compile: %s" % e for e in c.get("errors", [])[:5]]
    n = safe(lambda: raw_eyes.read_texture_inspect(live_dir))
    if n and not n.get("numeric_ok", True):
        v += ["numeric: %s" % f for f in n.get("findings", [])[:5]]
    cb = safe(lambda: raw_eyes.read_cb_dump(live_dir))
    if cb and cb.get("size_mismatch", 0) > 0:
        # Block only on CB_SIZE_MISMATCH (true overflow). CB NaN/Inf is heuristic -- ScanCB
        # reads unknown CB layout as floats, so int/bitfield members (0xFFFFFFFF, -1, packed
        # colors) read as NaN/Inf -> advisory, never blocks a commit (audit B).
        v += ["cb: %s" % f for f in cb.get("findings", []) if "OVERFLOW" in f][:5]
    rg = safe(lambda: raw_eyes.read_ranges(live_dir))
    if rg and not rg.get("ranges_ok", True):
        v += ["ranges: %s" % f for f in rg.get("findings", [])[:5]]
    return v

def build_freshness(manifest=None, roots=None):
    """True=fresh, False=stale, None=cannot tell. Source-content hash vs manifest."""
    try:
        import build_stamp
    except Exception:
        return None
    if manifest is None:
        manifest = os.path.join(HERE, "build_manifest.json")
    if roots is None:
        raw_root = os.path.dirname(os.path.dirname(HERE))
        roots = [os.path.join(raw_root, "src"), os.path.join(raw_root, "Shaders")]
    if not os.path.exists(manifest):
        return None
    try:
        built = json.load(open(manifest, encoding="utf-8"))
        cur, _n = build_stamp.hash_sources(roots)
        return cur == built.get("source_sha")
    except Exception:
        return None


def check(msgfile, live_dir=None, _emit=True):
    msg = ""
    try: msg = open(msgfile, encoding="utf-8", errors="ignore").read()
    except Exception: pass
    out, block = [], False
    status, detail = run_membrane_selftests()
    if status == "fail":
        out.append("BLOCK: " + detail); block = True
    elif status == "unavailable":
        out.append("warn: " + detail + " (membrane soundness unchecked)")
    if CLAIM_RE.search(msg):
        viol = oracle_violations(live_dir)
        if viol is None:
            out.append("warn: message claims a fix but no live/ oracle artifact to witness it")
        elif viol:
            out.append("BLOCK: message claims a fix the oracle CONTRADICTS:")
            out += ["    - " + x for x in viol]; block = True
        else:
            out.append("ok: fix-claim corroborated (available oracles clean)")
    if IN_GAME_RE.search(msg):
        fresh = build_freshness()
        if fresh is False:
            out.append("BLOCK: claims in-game verification but the build is STALE (rebuild before trusting)")
            block = True
        elif fresh is None:
            out.append("warn: in-game claim but build-freshness unknown (no manifest)")
    if _emit:
        for line in out:
            print("[membrane write-gate] " + line)
    return 1 if block else 0

def gate_selftest():
    checks = []
    def expect(c, label): checks.append((bool(c), label))
    with tempfile.TemporaryDirectory() as td:
        def mk(name, text):
            p = os.path.join(td, name); open(p, "w", encoding="utf-8").write(text); return p
        expect(check(mk("a", "docs: update readme"), None, _emit=False) == 0,
               "non-claim message is allowed")
        expect(check(mk("c", "fix: resolved the SSR hazard"), None, _emit=False) == 0,
               "fix-claim with no artifact is allowed (warn, not block)")
        ld = os.path.join(td, "live"); os.makedirs(ld)
        with open(os.path.join(ld, "bindings.jsonl"), "w") as f:
            f.write(json.dumps({"frame": 1, "pass": "ssr.resolve", "op": "Draw",
                "srv": {"t27": "SSR.output"}, "rtv": ["SSR.output"], "dsv": None,
                "uav": [], "asserts": []}) + "\n")
        expect(check(mk("b", "fix: SSR hazard resolved"), ld, _emit=False) == 1,
               "fix-claim CONTRADICTED by a HAZARD blocks")
        os.remove(os.path.join(ld, "bindings.jsonl"))
        with open(os.path.join(ld, "bindings.jsonl"), "w") as f:
            f.write(json.dumps({"frame": 1, "pass": "compositor", "op": "Draw",
                "srv": {"t27": "SSR.output"}, "rtv": ["game.sceneRT"], "dsv": None,
                "uav": [], "asserts": []}) + "\n")
        expect(check(mk("d", "fix: verified SSR clean"), ld, _emit=False) == 0,
               "fix-claim corroborated by a clean oracle is allowed")
        import build_stamp
        bsrc = os.path.join(td, 'bsrc'); os.makedirs(bsrc)
        open(os.path.join(bsrc, 'x.cpp'), 'w', encoding='utf-8').write('int x;')
        man = os.path.join(td, 'bm.json'); build_stamp.write(man, [bsrc])
        expect(build_freshness(man, [bsrc]) is True, "build_freshness True when source unchanged")
        open(os.path.join(bsrc, 'x.cpp'), 'w', encoding='utf-8').write('int x; edited')
        expect(build_freshness(man, [bsrc]) is False, "build_freshness False after edit (W.3 seal)")
        # --- W.1 claim-lint checks (reuse td + the clean compositor ld) -----
        empty = os.path.join(td, "live_empty"); os.makedirs(empty)
        r_unk = claim_lint("t27 = SSR.output when the compositor samples", empty)
        expect("binding" in r_unk["must_label_unknown"],
               "W.1 claim-lint: unwitnessed binding claim labelled UNKNOWN")
        r_ok = claim_lint("the t27 binding hazard is clean", ld)
        expect(any(x["class"] == "binding" and x["status"] == "WITNESSED_CLEAN"
                   for x in r_ok["results"]),
               "W.1 claim-lint: witnessed-clean binding claim -> WITNESSED_CLEAN")
        haz = os.path.join(td, "live_haz"); os.makedirs(haz)
        with open(os.path.join(haz, "bindings.jsonl"), "w") as f:
            f.write(json.dumps({"frame": 1, "pass": "ssr.resolve", "op": "Draw",
                "srv": {"t27": "SSR.output"}, "rtv": ["SSR.output"], "dsv": None,
                "uav": [], "asserts": []}) + "\n")
        r_bad = claim_lint("the t27 hazard is fixed", haz)
        expect("binding" in r_bad["contradicted"],
               "W.1 claim-lint: claim contradicted by a HAZARD -> CONTRADICTED")
        # Tier 1.3 wire-in: a CORRUPT range contradicts a fix-claim (write-gate)
        rgd = os.path.join(td, "live_ranges"); os.makedirs(rgd)
        with open(os.path.join(rgd, "ranges.jsonl"), "w") as f:
            f.write(json.dumps({"frame": 1, "pass": "ssr", "output": "ssr",
                "min": 0.0, "max": 1.0, "nan": 5, "inf": 0, "verdict": "CORRUPT"}) + "\n")
        expect(check(mk("rg", "fix: ssr range is clean now"), rgd, _emit=False) == 1,
               "W.2: fix-claim contradicted by a CORRUPT range blocks (1.3 wire-in)")
        cbn = os.path.join(td, "live_cbnan"); os.makedirs(cbn)
        with open(os.path.join(cbn, "cb_meta.jsonl"), "w") as f:
            print(json.dumps({"frame": 1, "ptr": "0x1", "size": 64, "has_nan": True,
                "has_inf": False, "nan_at": 0, "min": 0.0, "max": 1.0, "bind": "PS b0"}), file=f)
        expect(check(mk("cbn", "fix: cb is clean"), cbn, _emit=False) == 0,
               "W.2: heuristic CB NaN/Inf does NOT block (advisory)")
        cbo = os.path.join(td, "live_cbof"); os.makedirs(cbo)
        with open(os.path.join(cbo, "cb_meta.jsonl"), "w") as f:
            print(json.dumps({"frame": 1, "ptr": "0x1", "assert": "CB_SIZE_MISMATCH",
                "wrote": 128, "capacity": 64}), file=f)
        expect(check(mk("cbo", "fix: cb overflow resolved"), cbo, _emit=False) == 1,
               "W.2: CB_SIZE_MISMATCH (reliable) DOES block")
    ok = all(c for c, _ in checks)
    for c, l in checks:
        print(("PASS " if c else "FAIL ") + l)
    print("\nmembrane gate selftest: %d/%d checks passed" % (sum(c for c, _ in checks), len(checks)))
    return 0 if ok else 1

def main(argv):
    if len(argv) >= 2 and argv[1] == "check":
        return check(argv[2], argv[3] if len(argv) > 3 else None)
    if len(argv) >= 2 and argv[1] == "gate-selftest":
        return gate_selftest()
    if len(argv) >= 2 and argv[1] == "claim-lint":
        rep = claim_lint(argv[2] if len(argv) > 2 else "",
                         argv[3] if len(argv) > 3 else None)
        for r in rep["results"]:
            print("[claim-lint] %-9s %-24s %s" % (r["class"], r["artifact"], r["status"]))
        if rep["must_label_unknown"]:
            print("LABEL-UNKNOWN (no witness, do not assert): " + ", ".join(rep["must_label_unknown"]))
        if rep["contradicted"]:
            print("CONTRADICTED by oracle: " + ", ".join(rep["contradicted"]))
        return 1 if rep["contradicted"] else 0
    print(__doc__); return 2

if __name__ == "__main__":
    sys.exit(main(sys.argv))
