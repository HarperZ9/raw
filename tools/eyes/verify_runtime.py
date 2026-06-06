#!/usr/bin/env python3
"""verify_runtime.py - runtime verification for the membrane roadmap-completion session.

The build environment cannot launch Skyrim, so the eight oracles added this
session are compiler-verified (C++) and host-selftested (their readers/math).
This harness closes the loop the NEXT time the game runs: point it at the live/
capture dir produced by a session (RAW.dll + d3d11.dll installed) and it reports,
per oracle, whether the runtime artifact actually appeared.

Status legend:
  VERIFIED  - artifact present and the new field/record is there (runtime-confirmed)
  STALE     - artifact present but the new field is missing -> old DLL deployed; rebuild+redeploy
  PENDING   - conditional oracle that simply did not trigger this session (not a failure)
  GATED     - oracle is OFF until an INI flag is set (see notes)
  ABSENT    - expected to fire in any RAW session but no artifact -> check deploy

Usage:
  python verify_runtime.py <live_dir>
  python verify_runtime.py selftest
"""
import sys, os, json, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
if HERE not in sys.path:
    sys.path.insert(0, HERE)


def _rows(live_dir, name, n=8000):
    p = os.path.join(live_dir, name)
    if not os.path.exists(p):
        return None
    out = []
    try:
        lines = open(p, encoding="utf-8", errors="ignore").read().splitlines()
    except Exception:
        return None
    for ln in lines[-n:]:
        ln = ln.strip()
        if ln:
            try:
                out.append(json.loads(ln))
            except Exception:
                pass
    return out


def verify(live_dir):
    """Return a list of {tier, oracle, status, detail} runtime checks."""
    R = []

    def add(tier, oracle, status, detail):
        R.append({"tier": tier, "oracle": oracle, "status": status, "detail": detail})

    # 3.1 - GS/HS/DS SRV ledger via Snapshot fan-out (stage field on bindings.jsonl)
    b = _rows(live_dir, "bindings.jsonl")
    if b is None:
        add("3.1", "bindings.jsonl stage", "ABSENT",
            "no bindings.jsonl - RAW should snapshot marked draws; check deploy")
    elif any("stage" in r for r in b):
        stages = sorted({r.get("stage") for r in b if r.get("stage")})
        extra = [s for s in stages if s in ("GS", "HS", "DS", "VS")]
        add("3.1", "bindings.jsonl stage", "VERIFIED",
            "stages seen: %s%s" % (stages, (" (incl new %s)" % extra) if extra else " (PS/CS only)"))
    else:
        add("3.1", "bindings.jsonl stage", "STALE",
            "%d rows but no 'stage' field - pre-3.1 DLL deployed" % len(b))

    # thread-id witness (tid on restores.jsonl; only on a DIRTY restore)
    r = _rows(live_dir, "restores.jsonl")
    if not r:
        add("tid", "restores.jsonl tid", "PENDING",
            "no STATE_NOT_RESTORED records (clean restores) - nothing to witness")
    elif any("tid" in x for x in r):
        add("tid", "restores.jsonl tid", "VERIFIED", "%d dirty-restore record(s), tid present" % len(r))
    else:
        add("tid", "restores.jsonl tid", "STALE", "present but no 'tid' field - pre-fix DLL deployed")

    # 3.6 - create-side VRAM watch (gated [Diagnostics] ResourceLog=1)
    rs = _rows(live_dir, "resources.jsonl")
    if rs is None:
        add("3.6", "resources.jsonl", "GATED", "set [Diagnostics] ResourceLog=1 in d3d11_proxy.ini")
    else:
        import raw_eyes
        rep = raw_eyes.read_resources(live_dir)
        add("3.6", "resources.jsonl", "VERIFIED",
            "%d creates, %.1f MB, %s" % (rep["creates"], rep["total_mb"], rep["by_type"]))

    # 3.2 - shader variant / #define oracle (shader_used fires on PSSetShader)
    sv = _rows(live_dir, "shader_variants.jsonl")
    if sv is None:
        add("3.2", "shader_variants.jsonl", "ABSENT",
            "shader_used should fire on PSSetShader in any session - check deploy")
    else:
        used = sum(1 for x in sv if x.get("kind") == "shader_used")
        variants = sum(1 for x in sv if x.get("kind") != "shader_used")
        add("3.2", "shader_variants.jsonl", "VERIFIED",
            "%d shader_used + %d compile-variant records" % (used, variants))

    # 1.2-p3 - CB slot/stage attribution (bind field; only on a NaN/Inf CB)
    cb = _rows(live_dir, "cb_meta.jsonl")
    if not cb:
        add("1.2-p3", "cb_meta.jsonl bind", "PENDING",
            "no NaN/Inf constant buffer this session - nothing to attribute")
    elif any("bind" in x for x in cb):
        add("1.2-p3", "cb_meta.jsonl bind", "VERIFIED", "%d CB record(s), bind present" % len(cb))
    else:
        add("1.2-p3", "cb_meta.jsonl bind", "STALE", "present but no 'bind' field - pre-1.2p3 DLL deployed")

    # 3.4 - temporal ping-pong oracle (TAA emits if it runs)
    tm = _rows(live_dir, "temporal.jsonl")
    if not tm:
        add("3.4", "temporal.jsonl", "PENDING", "no records - TAA may be disabled this session")
    else:
        import bindings
        rep = bindings.read_temporal(live_dir)
        add("3.4", "temporal.jsonl", "VERIFIED",
            "buffers=%s, %s" % (rep["buffers"],
                                "clean toggle" if rep["clean"] else "VIOL: %s" % rep["violations"][:3]))

    # 1.3 - per-pass range oracle (gated GpuReadback + a pass must wire post_execute)
    rg = _rows(live_dir, "ranges.jsonl")
    if rg is None:
        add("1.3", "ranges.jsonl", "GATED",
            "set [Diagnostics] GpuReadback=1 AND wire a pass post_execute (see RangeOracle.cpp)")
    else:
        import raw_eyes
        rep = raw_eyes.read_ranges(live_dir)
        add("1.3", "ranges.jsonl", "VERIFIED",
            "%d outputs, %s" % (rep["outputs_seen"],
                                "ok" if rep["ranges_ok"] else "CORRUPT: %s" % rep["findings"][:3]))
    return R


def _print(results):
    order = {"VERIFIED": 0, "STALE": 1, "ABSENT": 2, "GATED": 3, "PENDING": 4}
    print("\n=== RAW membrane runtime verification ===")
    for r in sorted(results, key=lambda x: order.get(x["status"], 9)):
        print("  [%-8s] %-7s %-22s %s" % (r["status"], r["tier"], r["oracle"], r["detail"]))
    v = sum(1 for r in results if r["status"] == "VERIFIED")
    soft = sum(1 for r in results if r["status"] in ("PENDING", "GATED"))
    bad = [r for r in results if r["status"] in ("STALE", "ABSENT")]
    print("\n  %d/%d runtime-VERIFIED; %d PENDING/GATED (expected); %d need attention" % (
        v, len(results), soft, len(bad)))
    if bad:
        print("  ATTENTION: " + "; ".join("%s %s" % (r["tier"], r["status"]) for r in bad))
    return 1 if bad else 0


def selftest():
    checks = []

    def expect(c, label):
        checks.append((bool(c), label))

    with tempfile.TemporaryDirectory() as td:
        res = {r["oracle"]: r["status"] for r in verify(td)}
        expect(res["resources.jsonl"] == "GATED", "empty: 3.6 GATED")
        expect(res["ranges.jsonl"] == "GATED", "empty: 1.3 GATED")
        expect(res["cb_meta.jsonl bind"] == "PENDING", "empty: 1.2-p3 PENDING")
        expect(res["restores.jsonl tid"] == "PENDING", "empty: tid PENDING")
        expect(res["bindings.jsonl stage"] == "ABSENT", "empty: 3.1 ABSENT")

        def w(name, recs):
            with open(os.path.join(td, name), "w", encoding="utf-8") as f:
                for rec in recs:
                    f.write(json.dumps(rec) + "\n")
        w("bindings.jsonl", [{"frame": 1, "pass": "p", "op": "Draw", "stage": "GS",
                              "srv": {"t0": "x"}, "rtv": [], "dsv": None, "uav": [], "asserts": []}])
        w("restores.jsonl", [{"phase": 3, "assert": "STATE_NOT_RESTORED", "dirty": 1, "tid": 99, "fields": ["dsv"]}])
        w("resources.jsonl", [{"op": "create", "type": "buffer", "ptr": "0x1", "size": 1024}])
        w("shader_variants.jsonl", [{"kind": "shader_used", "ps_hash": "0x1", "vs_hash": "0x2"},
                                    {"hash": "0xa", "file": "f.hlsl", "entry": "main", "profile": "cs_5_0", "defines": "Q=1"}])
        w("cb_meta.jsonl", [{"frame": 1, "ptr": "0xC", "bind": "PS b3", "size": 64,
                             "has_nan": True, "has_inf": False, "nan_at": 0, "min": 0.0, "max": 1.0}])
        w("temporal.jsonl", [{"frame": 0, "buffer": "taa.history", "read_idx": 0, "write_idx": 0, "warmup": True},
                             {"frame": 1, "buffer": "taa.history", "read_idx": 1, "write_idx": 0, "warmup": False}])
        w("ranges.jsonl", [{"frame": 64, "pass": "gtao", "output": "gtao", "min": 0.0, "max": 1.0,
                            "nan": 0, "inf": 0, "verdict": "ok"}])
        res2 = {r["oracle"]: r["status"] for r in verify(td)}
        for k in res2:
            expect(res2[k] == "VERIFIED", "populated: %s VERIFIED (got %s)" % (k, res2[k]))

    ok = all(c for c, _ in checks)
    for c, label in checks:
        print(("PASS " if c else "FAIL ") + label)
    print("\nverify_runtime selftest: %d/%d checks passed" % (sum(c for c, _ in checks), len(checks)))
    return 0 if ok else 1


def preflight():
    """Pre-firing gate: run BEFORE deploying to confirm the bundle is fresh + complete."""
    raw = os.path.dirname(os.path.dirname(HERE))
    checks = []
    def chk(ok, label, detail=""):
        checks.append((bool(ok), label, detail))
    try:
        import build_stamp
        man = json.load(open(os.path.join(HERE, "build_manifest.json"), encoding="utf-8"))
        cur, _ = build_stamp.hash_sources([os.path.join(raw, "src"), os.path.join(raw, "Shaders")])
        fresh = (cur == man.get("source_sha"))
        chk(fresh, "build is FRESH (source == built DLL)",
            "" if fresh else "current=%s built=%s -- REBUILD" % (cur, man.get("source_sha")))
    except Exception as e:
        chk(False, "build freshness check ran", "error: %s" % e)
    rel = os.path.join(raw, "build", "Release")
    for dll in ("RAW.dll", "d3d11.dll"):
        chk(os.path.isfile(os.path.join(rel, dll)), "%s built" % dll, rel)
    sh = os.path.join(raw, "Shaders")
    n = len(os.listdir(sh)) if os.path.isdir(sh) else 0
    chk(n > 0, "Shaders/ present (%d files)" % n, sh)
    ok = all(c for c, _, _ in checks)
    print()
    print("=== RAW pre-firing checklist ===")
    for c, label, detail in checks:
        print("  [%s] %s%s" % ("OK " if c else "XX ", label, ("  -- " + detail) if (detail and not c) else ""))
    print()
    print("  Deploy -> <MO2 mod>/ : build/Release/RAW.dll + build/Release/d3d11.dll + Shaders/")
    print("  Optional gates: [Diagnostics] ResourceLog=1 -> resources.jsonl;")
    print("                  [Diagnostics] GpuReadback=1 + [GTAO] Enabled=1 -> ranges.jsonl")
    print("  After playing: python tools/eyes/verify_runtime.py <MO2>/Data/SKSE/Plugins/RAW/live")
    print()
    print("  " + ("READY to deploy + fire." if ok else "NOT READY -- resolve the XX items."))
    return 0 if ok else 1


def main(argv):
    if len(argv) >= 2 and argv[1] == "selftest":
        return selftest()
    if len(argv) >= 2 and argv[1] == "preflight":
        return preflight()
    if len(argv) >= 2:
        return _print(verify(argv[1]))
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
