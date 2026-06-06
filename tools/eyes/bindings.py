#!/usr/bin/env python3
"""bindings.py - host reader for RAW's binding ledger (live/bindings.jsonl).

Answers GPU state questions by QUERY instead of in-head simulation (see
BINDING_LEDGER.md and ../../DOCTRINE.md). The host is the single source of truth:
it RECOMPUTES invariants from each snapshot, so it is self-sufficient even if the
proxy emitted no assert (the proxy's "asserts" field is advisory and ignored here).
Pure stdlib; no game and no numpy required.

  bindings.py show    <live_dir> [--pass NAME]   # per-pass binding tables (latest frame)
  bindings.py asserts <live_dir>                 # only violations across the tail
  bindings.py selftest [--out DIR]               # synth ledger, prove the detectors
"""
import sys, os, json, glob, argparse

# Documented SRV slot owners (mirror of ARCHITECTURE.md t17-t38). Value = the
# name-prefix the bound resource is expected to carry. Only checked on RAW's own
# passes; game draws are not snapshotted and their resources are hex pointers.
SLOT_OWNERS = {
    "t17": "LuminanceHistogram", "t18": "LUT", "t19": "HiZ", "t20": "GTAO",
    "t21": "ClusteredLighting", "t22": "TAA", "t25": "MaterialClassifier",
    "t26": "SSGI", "t27": "SSR", "t28": "ContactShadow", "t29": "Skylighting",
    "t30": "BlueNoise", "t31": "LinearDepth", "t32": "IndirectSpecular",
    "t33": "VolumetricLighting", "t34": "DynamicCubemap", "t35": "LightBuffer",
    "t36": "LightIndex", "t37": "VolumetricClouds", "t38": "Celestial",
}

def _is_game(name):
    """Game-owned resources serialize as hex pointers; ours carry names."""
    return isinstance(name, str) and name.startswith("0x")

def analyze(snap):
    """Recompute invariant asserts from one binding snapshot. Returns a list.

    Authoritative: the host does not trust the proxy's emitted asserts, it derives
    them here so one code path defines what a violation is."""
    srv = snap.get("srv", {}) or {}          # {"t27": "SSR.output", ...}
    stage = snap.get("stage")                # PS/VS/GS/HS/DS/CS; absent on pre-3.1 rows
    sp = (stage + ":") if stage else ""      # cross-stage hazard-label prefix
    rtv = [x for x in (snap.get("rtv", []) or []) if x]
    dsv = snap.get("dsv")
    uav = [x for x in (snap.get("uav", []) or []) if x]
    out = []

    # outputs this draw writes: resource -> human role
    writes = {}
    for i, r in enumerate(rtv): writes.setdefault(r, "RTV%d" % i)
    for i, r in enumerate(uav): writes.setdefault(r, "UAV%d" % i)
    if dsv: writes.setdefault(dsv, "DSV")

    # HAZARD: a resource read as SRV is simultaneously written this draw
    for slot, res in srv.items():
        if res in writes:
            out.append({"assert": "HAZARD", "resource": res, "stage": stage,
                        "as": "%sSRV(%s)+%s" % (sp, slot, writes[res])})
        if dsv is not None and res == dsv:
            out.append({"assert": "DSV_BOUND_DURING_SAMPLE", "resource": res, "stage": stage,
                        "as": "%sSRV(%s)+DSV" % (sp, slot)})

    # SLOT_MATCH: ours-only; skip game pointers and unmapped/unbound slots.
    # t17-t38 owners are PS/CS-domain, so only check there (or on legacy stage-less rows).
    if stage in (None, "PS", "CS"):
        for slot, want in SLOT_OWNERS.items():
            res = srv.get(slot)
            if res and not _is_game(res) and not res.startswith(want):
                out.append({"assert": "SLOT_MISMATCH", "slot": slot, "stage": stage,
                            "expected": want, "got": res})
    return out

def _read_jsonl(path, tail=None):
    if not os.path.exists(path):
        return []
    with open(path, "r", encoding="utf-8", errors="ignore") as f:
        lines = f.readlines()
    if tail:
        lines = lines[-tail:]
    rows = []
    for ln in lines:
        ln = ln.strip()
        if ln:
            try: rows.append(json.loads(ln))
            except Exception: pass
    return rows

def _ledger_path(live_dir):
    return os.path.join(live_dir, "bindings.jsonl")

def latest_frame(rows):
    if not rows: return []
    fmax = max(r.get("frame", 0) for r in rows)
    return [r for r in rows if r.get("frame", 0) == fmax]

def show(live_dir, pass_name=None):
    rows = latest_frame(_read_jsonl(_ledger_path(live_dir)))
    if pass_name:
        rows = [r for r in rows if r.get("pass") == pass_name]
    snaps = []
    for r in rows:
        snaps.append({"pass": r.get("pass"), "op": r.get("op"),
                      "phase": r.get("phase"), "srv": r.get("srv", {}),
                      "rtv": r.get("rtv", []), "dsv": r.get("dsv"),
                      "uav": r.get("uav", []), "asserts": analyze(r)})
    return {"frame": rows[0].get("frame") if rows else None, "passes": snaps}

def asserts(live_dir, tail=600):
    rows = _read_jsonl(_ledger_path(live_dir), tail=tail)
    hits = []
    for r in rows:
        a = analyze(r)
        if a:
            hits.append({"frame": r.get("frame"), "pass": r.get("pass"),
                         "op": r.get("op"), "asserts": a})
    return {"scanned": len(rows), "violations": hits, "clean": len(hits) == 0}

# --------------------------------------------------------------------------
def selftest(outdir):
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "bindings.jsonl")
    clean = {"frame": 1, "seq": 1, "phase": "PostGeometry", "pass": "compositor",
             "op": "Draw", "srv": {"t27": "SSR.output", "t20": "GTAO.output",
             "t19": "HiZ.depth"}, "rtv": ["game.sceneRT"], "dsv": None, "uav": []}
    hazard = {"frame": 1, "seq": 2, "phase": "PostGeometry", "pass": "ssr.resolve",
              "op": "Draw", "srv": {"t27": "SSR.output"},          # reads its own output
              "rtv": ["SSR.output"], "dsv": None, "uav": []}        # while writing it
    dsvhaz = {"frame": 1, "seq": 3, "phase": "PostGeometry", "pass": "ssgi.trace",
              "op": "Dispatch", "srv": {"t31": "LinearDepth.tex"},
              "rtv": [], "dsv": "LinearDepth.tex", "uav": ["SSGI.gi"]}  # depth SRV+DSV
    slotbad = {"frame": 1, "seq": 4, "phase": "PostGeometry", "pass": "compositor",
               "op": "Draw", "srv": {"t27": "GTAO.output"},         # GTAO in SSR's slot
               "rtv": ["game.sceneRT"], "dsv": None, "uav": []}
    with open(path, "w", encoding="utf-8") as f:
        for s in (clean, hazard, dsvhaz, slotbad):
            f.write(json.dumps(s) + "\n")

    checks = []
    def expect(c, label): checks.append((bool(c), label))
    kinds = lambda a: {x["assert"] for x in a}
    a_clean, a_haz = analyze(clean), analyze(hazard)
    a_dsv, a_slot = analyze(dsvhaz), analyze(slotbad)
    expect(a_clean == [], "clean compositor pass yields no asserts")
    expect("HAZARD" in kinds(a_haz), "self read/write flagged HAZARD")
    expect(len(a_haz) == 1, "hazard not double-counted (single source of truth)")
    expect(any(x.get("resource") == "SSR.output" for x in a_haz), "hazard names SSR.output")
    expect("DSV_BOUND_DURING_SAMPLE" in kinds(a_dsv), "depth SRV+DSV flagged")
    expect("SLOT_MISMATCH" in kinds(a_slot), "GTAO in t27 flagged SLOT_MISMATCH")
    rep = asserts(outdir)
    expect(rep["scanned"] == 4, "asserts() scanned all 4 snapshots")
    expect(len(rep["violations"]) == 3, "exactly 3 of 4 snapshots violate")
    # --- 3.1: GS/HS/DS Snapshot fan-out + stage-aware analysis ----------
    gshaz = {"frame": 1, "phase": "PostGeometry", "pass": "tess.geom", "op": "Draw",
             "stage": "GS", "srv": {"t0": "Scene.color"}, "rtv": ["Scene.color"],
             "dsv": None, "uav": []}                       # GS reads an RTV it writes
    a_gs = analyze(gshaz)
    expect("HAZARD" in kinds(a_gs), "3.1: GS cross-stage hazard flagged")
    expect(any(x.get("stage") == "GS" and x["as"].startswith("GS:") for x in a_gs),
           "3.1: hazard carries GS stage prefix (not conflated with PS)")
    gsslot = {"frame": 1, "phase": "PostGeometry", "pass": "tess.geom", "op": "Draw",
              "stage": "GS", "srv": {"t27": "GTAO.output"}, "rtv": ["Scene.color"],
              "dsv": None, "uav": []}                      # GTAO in t27, but GS stage
    expect("SLOT_MISMATCH" not in kinds(analyze(gsslot)),
           "3.1: SLOT_MISMATCH suppressed off PS/CS (t17-t38 are PS/CS-domain)")
    legacy = {"frame": 1, "phase": "PostGeometry", "pass": "compositor", "op": "Draw",
              "srv": {"t27": "GTAO.output"}, "rtv": ["game.sceneRT"], "dsv": None, "uav": []}
    expect("SLOT_MISMATCH" in kinds(analyze(legacy)),
           "3.1: pre-3.1 ledger (no stage) still gets SLOT_MISMATCH (backward-compat)")
    # --- thread-id witness (rides Tier 2.2 restores.jsonl) -------------
    import tempfile
    with tempfile.TemporaryDirectory() as rtd:
        with open(os.path.join(rtd, "restores.jsonl"), "w", encoding="utf-8") as f:
            f.write(json.dumps({"phase": 3, "assert": "STATE_NOT_RESTORED", "dirty": 2,
                                "tid": 12345, "fields": ["rtv0", "dsv"]}) + "\n")
        rr = read_restores(rtd)
    expect(rr["records"] == 1 and not rr["clean"], "thread-id: dirty restore record read")
    expect(any("tid=12345" in s for s in rr["findings"]), "thread-id: tid surfaced in finding")

    # --- Tier 3.4: temporal ping-pong causality checker ----------------
    import tempfile as _tf
    with _tf.TemporaryDirectory() as ttd:
        with open(os.path.join(ttd, "temporal.jsonl"), "w", encoding="utf-8") as tf:
            for rec in [{"frame": 0, "buffer": "taa.history", "read_idx": 0, "write_idx": 0, "warmup": True},
                        {"frame": 1, "buffer": "taa.history", "read_idx": 1, "write_idx": 0, "warmup": False},
                        {"frame": 2, "buffer": "taa.history", "read_idx": 0, "write_idx": 1, "warmup": False},
                        {"frame": 3, "buffer": "bad.history", "read_idx": 0, "write_idx": 0, "warmup": False}]:
                tf.write(json.dumps(rec) + "\n")
        tmp = read_temporal(ttd)
    expect(tmp["records"] == 4, "3.4: temporal records read")
    expect(any("READ_EQ_WRITE" in v for v in tmp["violations"]), "3.4: READ_EQ_WRITE feedback flagged")
    expect(not any("taa.history" in v for v in tmp["violations"]), "3.4: clean TAA toggle not flagged")
    # --- reader coverage: read_pipeline_state (2.1) + read_reflection (3.3) -------
    import tempfile as _tfp
    with _tfp.TemporaryDirectory() as ptd:
        with open(os.path.join(ptd, "pipeline_state.jsonl"), "w", encoding="utf-8") as f:
            print(json.dumps({"kind": "state", "obj": "blend", "ptr": "0x1",
                              "enable": 1, "src": "SRC_ALPHA", "dst": "INV_SRC_ALPHA"}), file=f)
            print(json.dumps({"assert": "VIEWPORT_SIZE_ZERO", "detail": "vp0 w=0 h=0"}), file=f)
        ps = read_pipeline_state(ptd)
        expect(ps["state_records"] == 1 and "blend" in ps["current_state"],
               "2.1: read_pipeline_state parses a blend state record")
        expect((not ps["clean"]) and any("VIEWPORT_SIZE_ZERO" in v for v in ps["sanity_violations"]),
               "2.1: read_pipeline_state flags a sanity violation")
        with open(os.path.join(ptd, "shader_reflection.jsonl"), "w", encoding="utf-8") as f:
            print(json.dumps({"shader": "compositor", "type": 2, "reg": 27, "name": "GTAO.output"}), file=f)
            print(json.dumps({"shader": "compositor", "type": 2, "reg": 27, "name": "SSR.output"}), file=f)
        rf = read_reflection(ptd)
        expect((not rf["clean"]) and any("t27" in m for m in rf["register_mismatches"]),
               "3.3: read_reflection flags GTAO-named SRV at SSR register t27")
    ok = all(c for c, _ in checks)
    for c, label in checks:
        print(("PASS " if c else "FAIL ") + label)
    print("\nbindings selftest: %d/%d checks passed" % (sum(c for c, _ in checks), len(checks)))
    print("synthetic ledger at: " + path)
    return 0 if ok else 1

def main(argv=None):
    ap = argparse.ArgumentParser(prog="bindings", description="RAW binding-ledger host reader")
    sub = ap.add_subparsers(dest="cmd", required=True)
    ps = sub.add_parser("show"); ps.add_argument("live_dir"); ps.add_argument("--pass", dest="pass_name", default=None)
    pa = sub.add_parser("asserts"); pa.add_argument("live_dir"); pa.add_argument("--tail", type=int, default=600)
    pr = sub.add_parser("restores"); pr.add_argument("live_dir")
    ptm = sub.add_parser("temporal"); ptm.add_argument("live_dir")
    pst = sub.add_parser("state"); pst.add_argument("live_dir")
    prf = sub.add_parser("reflect"); prf.add_argument("live_dir")
    pt = sub.add_parser("selftest"); pt.add_argument("--out", default=None)
    args = ap.parse_args(argv)
    if args.cmd == "show":
        print(json.dumps(show(args.live_dir, args.pass_name), indent=2))
    elif args.cmd == "asserts":
        print(json.dumps(asserts(args.live_dir, args.tail), indent=2))
    elif args.cmd == "restores":
        print(json.dumps(read_restores(args.live_dir), indent=2))
    elif args.cmd == "temporal":
        print(json.dumps(read_temporal(args.live_dir), indent=2))
    elif args.cmd == "state":
        print(json.dumps(read_pipeline_state(args.live_dir), indent=2))
    elif args.cmd == "reflect":
        print(json.dumps(read_reflection(args.live_dir), indent=2))
    elif args.cmd == "selftest":
        out = args.out or os.path.join(os.path.dirname(os.path.abspath(__file__)), "_selftest")
        return selftest(out)
    return 0

def read_restores(live_dir):
    """live/restores.jsonl -> mid-frame dispatch state-restoration failures (Tier 2.2).

    A STATE_NOT_RESTORED record means RAW left the game's pipeline dirty after a pass
    (a field whose pointer identity differed before dispatch vs after Restore). Emitted
    only on mismatch, so every record is a failure; no records => clean."""
    rows = _read_jsonl(os.path.join(live_dir, "restores.jsonl"), 2000)
    out = {"records": len(rows), "clean": len(rows) == 0, "findings": []}
    for r in rows:
        out["findings"].append("phase %s tid=%s: STATE_NOT_RESTORED dirty=%s %s" % (
            r.get("phase"), r.get("tid"), r.get("dirty"), r.get("fields")))
    return out


def read_pipeline_state(live_dir):
    """live/pipeline_state.jsonl -> pipeline interpretation state + sanity (Tier 2.1).

    Two record kinds: serialized blend/raster/depth/sampler descs (kind=state) and
    viewport/scissor sanity asserts. Lets the LLM query HOW state is interpreted
    (blend op, cull, depth-compare, sampler filter) instead of blaming shader math."""
    rows = _read_jsonl(os.path.join(live_dir, "pipeline_state.jsonl"), 4000)
    states = [r for r in rows if r.get("kind") == "state"]
    asserts = [r for r in rows if r.get("assert")]
    current, samplers = {}, []
    for r in states:
        o = r.get("obj", "?")
        fields = {k: v for k, v in r.items() if k not in ("kind", "obj", "ptr")}
        if o == "sampler":
            samplers.append({"ptr": r.get("ptr"), **fields})
        else:
            current[o] = fields
    by = {}
    for r in asserts:
        k = r.get("assert", "?"); by[k] = by.get(k, 0) + 1
    seen, viol = set(), []
    for r in asserts:
        key = (r.get("assert"), r.get("detail"))
        if key in seen:
            continue
        seen.add(key); viol.append("%s: %s" % (r.get("assert"), r.get("detail")))
    return {"state_records": len(states), "samplers_seen": len(samplers),
            "current_state": current, "samplers": samplers,
            "sanity_violations": viol, "by_assert": by, "clean": len(asserts) == 0}


def read_reflection(live_dir):
    """live/shader_reflection.jsonl -> declared registers per shader + mismatches (Tier 3.3).

    D3DReflect records which register each shader actually declares (type 2 = SRV).
    Flags REGISTER_MISMATCH (high-confidence): an SRV whose NAME names one effect but
    sits at a DIFFERENT effect's documented register -- the silent slot<->register bug."""
    rows = _read_jsonl(os.path.join(live_dir, "shader_reflection.jsonl"), 8000)
    owners = {int(k[1:]): v.lower() for k, v in SLOT_OWNERS.items()}
    kws = set(owners.values())
    shaders, mism = {}, []
    for r in rows:
        if r.get("type") != 2:
            continue
        reg = r.get("reg"); name = (r.get("name") or "").lower(); sh = r.get("shader", "?")
        shaders.setdefault(sh, []).append({"reg": reg, "name": r.get("name")})
        if reg in owners:
            expected = owners[reg]
            for kw in kws:
                if kw != expected and kw in name and expected not in name:
                    mism.append("%s: SRV '%s' at register t%d (owner %s) but name suggests %s" % (
                        sh, r.get("name"), reg, expected.upper(), kw.upper()))
                    break
    return {"shaders": len(shaders),
            "srv_bindings": sum(len(v) for v in shaders.values()),
            "register_mismatches": mism, "clean": len(mism) == 0}


def read_temporal(live_dir):
    """live/temporal.jsonl -> per-renderer history ping-pong causality (Tier 3.4).

    Each ping-pong renderer (TAA today; TSR/FrameGen deferred -- not built) emits
    {frame, buffer, read_idx, write_idx, warmup} at its history-touching pass.
    Flags: READ_EQ_WRITE (sampling the buffer it writes -> feedback), NO_TOGGLE
    (write_idx failed to flip frame-to-frame), WARMUP_AFTER_STEADY (a warmup record
    after a steady one for the same buffer -> mid-session reset not handled)."""
    rows = _read_jsonl(os.path.join(live_dir, "temporal.jsonl"), 4000)
    by_buf, viol = {}, []
    for r in rows:
        b = r.get("buffer", "?"); ri = r.get("read_idx"); wi = r.get("write_idx")
        warm = bool(r.get("warmup"))
        if (not warm) and ri == wi:
            viol.append("%s frame %s: READ_EQ_WRITE idx=%s" % (b, r.get("frame"), wi))
        prev = by_buf.get(b)
        if prev is not None:
            pwi, pwarm = prev
            if (not warm) and (not pwarm) and wi == pwi:
                viol.append("%s frame %s: NO_TOGGLE write_idx stuck at %s" % (b, r.get("frame"), wi))
            if warm and not pwarm:
                viol.append("%s frame %s: WARMUP_AFTER_STEADY (mid-session reset)" % (b, r.get("frame")))
        by_buf[b] = (wi, warm)
    return {"records": len(rows), "buffers": sorted(by_buf.keys()),
            "violations": viol, "clean": len(viol) == 0}


if __name__ == "__main__":
    sys.exit(main())
