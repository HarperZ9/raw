"""Join raw_engine.dll's in-process output with the engine map.

    python -I tools/enginemap/cg_analyze.py --map <map dir> --rt <raw_engine_out> --out <dir> [--public]

Writes:
  callgraph_named.tsv   each named function: callers and callees (counts and the first names)
  clamps.tsv            compares, mins and maxes right after reads of LOD, grid, shadow and Papyrus settings
  papyrus_dispatch.tsv  jump tables reachable from the Papyrus CodeTasklet and VirtualMachine slots
  vtable_slots.tsv      the traced vtable slots: calls per frame in the world, callers, callees
--public drops the compared constants (values read from the game), leaving addresses and names.
"""
import argparse
import csv
import glob
import os
import re
from collections import defaultdict


def tsv(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        yield from csv.DictReader(f, delimiter="\t")


def hx(s):
    return int(s, 16)


def load_names(map_dir):
    names = {}
    for fn in glob.glob(os.path.join(map_dir, "*.tsv")):
        base = os.path.basename(fn)
        if base in ("settings.tsv", "live_state.tsv", "control_points.tsv", "lighting_techniques.tsv"):
            continue
        for r in tsv(fn):
            if r.get("kind") in ("function", "vfunc") and r.get("rva", "").startswith("0x"):
                names.setdefault(hx(r["rva"]), r["name"])
    return names


def write(path, header, rows):
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write("\t".join(header) + "\n")
        for r in rows:
            f.write("\t".join(str(x) for x in r) + "\n")


def name_of(names, rva):
    return names.get(rva, f"sub_{rva:X}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", required=True)
    ap.add_argument("--rt", required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--public", action="store_true")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    names = load_names(a.map)

    callees, callers = defaultdict(set), defaultdict(set)
    for r in tsv(os.path.join(a.rt, "cg_edges.tsv")):
        c, t = hx(r["caller"]), hx(r["callee"])
        callees[c].add(t)
        callers[t].add(c)
    rows = []
    for rva, nm in sorted(names.items()):
        cr, ce = sorted(callers.get(rva, ())), sorted(callees.get(rva, ()))
        show = lambda xs: ", ".join(name_of(names, x) for x in xs[:6]) + (f", +{len(xs) - 6}" if len(xs) > 6 else "")
        rows.append((f"0x{rva:07X}", nm, len(cr), len(ce), show(cr), show(ce)))
    write(os.path.join(a.out, "callgraph_named.tsv"),
          ["rva", "name", "callers", "callees", "first_callers", "first_callees"], rows)

    want = re.compile(r"(LOD|Lod|Block|TreeLoad|Grids|LargeRef|SplitDistance|Shadow|NumSplits|Papyrus|FadeOut|SkyCell)")
    crow = []
    for r in tsv(os.path.join(a.rt, "cg_clamps.tsv")):
        if not want.search(r["setting"]):
            continue
        fn = hx(r["function"])
        value = "" if a.public else r["value"]
        if a.public and r["value"].startswith("imm"):
            value = "immediate"
        elif a.public and r["value"].startswith("const"):
            value = "constant " + r["value"].split()[0].split("@")[1]
        crow.append((r["setting"], r["function"], name_of(names, fn), r["read_site"], r["compare_site"],
                     r["mnemonic"], r["distance"], value))
    write(os.path.join(a.out, "clamps.tsv"),
          ["setting", "function", "function_name", "read_site", "compare_site", "mnemonic", "distance", "value"], crow)

    tables = defaultdict(list)
    for r in tsv(os.path.join(a.rt, "cg_jumptables.tsv")):
        tables[hx(r["function"])].append((int(r["entries"]), r["site"], r["table"]))
    roots = [rva for rva, nm in names.items() if nm.startswith(("BSScript::Internal::CodeTasklet::",
                                                                "BSScript::Internal::VirtualMachine::"))]
    prow, seen, frontier = [], {}, [(r, 0) for r in roots]
    while frontier:
        f, d = frontier.pop()
        if f in seen or d > 4:
            continue
        seen[f] = d
        for t in callees.get(f, ()):
            frontier.append((t, d + 1))
    for f, d in sorted(seen.items(), key=lambda x: x[1]):
        for n, site, table in tables.get(f, ()):
            prow.append((f"0x{f:07X}", name_of(names, f), d, site, table, n))
    prow.sort(key=lambda r: -r[5])
    write(os.path.join(a.out, "papyrus_dispatch.tsv"),
          ["function", "name", "depth_from_vm", "jump_site", "table", "entries"], prow)

    # A return address sits just past its call: find the indirect call site right before it.
    import bisect
    vsites = sorted((hx(r["site"]), hx(r["function"])) for r in tsv(os.path.join(a.rt, "cg_vcalls.tsv")))
    vkeys = [x[0] for x in vsites]

    def site_fn(ret):
        i = bisect.bisect_left(vkeys, ret) - 1
        return vsites[i][1] if i >= 0 and 0 < ret - vkeys[i] <= 7 else None

    vt = os.path.join(a.rt, "vt_trace.tsv")
    if os.path.exists(vt):
        last = {}
        series = defaultdict(list)
        for r in tsv(vt):
            key = (r["class"], int(r["slot"]))
            last[key] = r
            series[key].append(int(r["calls_window"]))
        vrow = []
        for (cls, slot), r in sorted(last.items()):
            target = hx(r["target"])
            win = series[(cls, slot)]
            peak = max(win) if win else 0
            callers_named = []
            for c in filter(None, r["callers"].split(",")):
                fn = site_fn(hx(c))
                callers_named.append(f"{c} in {name_of(names, fn)}" if fn else c)
            vrow.append((cls, f"{slot:02X}", r["target"], name_of(names, target), r["calls_total"], r["frames_with_calls"],
                         round(peak / 300.0, 2), ";".join(callers_named),
                         ", ".join(name_of(names, x) for x in sorted(callees.get(target, ()))[:8])))
        write(os.path.join(a.out, "vtable_slots.tsv"),
              ["class", "slot", "target", "target_name", "calls_total", "frames_with_calls", "peak_calls_per_frame",
               "caller_return_sites", "callees"], vrow)
    print(f"named {len(rows)}, clamps {len(crow)}, papyrus tables {len(prow)}")


if __name__ == "__main__":
    main()
