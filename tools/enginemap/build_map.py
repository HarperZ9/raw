"""Build the RAW engine map for SkyrimSE.exe from static, read-only sources.

Usage:
  python -I build_map.py --exe <SkyrimSE.exe> --addrlib <versionlib.bin>
      [--addrlib-alt <second .bin>] --commonlib <CommonLibSSE-NG checkout>
      [--bridge <skyrimbridge checkout>] --out <dir> [--public]

--public drops every value copied from the game (setting defaults and
string values), leaving addresses, names and notes only.
"""
import argparse
import csv
import hashlib
import json
import os
import sys
import time

# -I keeps the cwd off sys.path; add only this tool's own folder.
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import addrlib  # noqa: E402
import classify  # noqa: E402
import commonlib_scan  # noqa: E402
import control_points  # noqa: E402
import entries
import live_state
import pe_image
import rtti
import settings_scan
import techniques

COLS = ["rva", "kind", "name", "confidence", "source", "addrlib_id",
        "check", "refs", "notes"]


def write_tsv(path, rows, cols):
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f, delimiter="\t", lineterminator="\n")
        w.writerow(cols)
        for r in rows:
            w.writerow([_fmt(c, r.get(c, "")) for c in cols])


def _fmt(col, v):
    if col in ("rva", "singleton", "name_rva") and isinstance(v, int):
        return f"0x{v:07X}"
    return v


def sha256(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def main():
    ap = argparse.ArgumentParser()
    for a in ("--exe", "--addrlib", "--commonlib", "--out"):
        ap.add_argument(a, required=True)
    ap.add_argument("--addrlib-alt")
    ap.add_argument("--bridge")
    ap.add_argument("--public", action="store_true")
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)
    t0 = time.time()

    img = pe_image.Image(args.exe)
    ver, _ps, db = addrlib.load(args.addrlib)
    alt = addrlib.load(args.addrlib_alt)[2] if args.addrlib_alt else {}
    addr_to_id = {}
    for i, r in db.items():
        addr_to_id.setdefault(r, i)
    vtables = rtti.scan(img)
    ids = list(commonlib_scan.scan_ids(args.commonlib))
    virtuals = commonlib_scan.scan_virtuals(args.commonlib)

    cl_rows, cl_checks = entries.commonlib_entries(img, ids, db, alt)
    vt_rows = entries.vtable_entries(img, vtables, virtuals)
    merged = entries.merge(cl_rows + vt_rows, addr_to_id)

    stats = {"subsystems": {}}
    for sub in sorted({e["subsystem"] for e in merged}):
        rows = [e for e in merged if e["subsystem"] == sub]
        write_tsv(os.path.join(args.out, f"{sub}.tsv"), rows, COLS)
        c = {"entries": len(rows)}
        for conf in ("named", "inferred", "unknown"):
            c[conf] = sum(1 for e in rows if e["confidence"] == conf)
        c["functions"] = sum(1 for e in rows if e["kind"] in ("function", "vfunc"))
        stats["subsystems"][sub] = c

    settings = settings_scan.scan(img, vtables)
    for s in settings:
        s["subsystem"] = classify.setting_subsystem(s["key"], s["section"]) or ""
        s["addrlib_id"] = addr_to_id.get(s["rva"], "")
    scols = ["rva", "name", "file", "section", "type", "subsystem", "addrlib_id"]
    if not args.public:
        scols.append("default")
    write_tsv(os.path.join(args.out, "settings.tsv"), settings, scols)

    singletons = {}
    srows = []
    for e in cl_rows:
        if e["kind"] == "global" or e["name"].endswith("::GetSingleton"):
            key = e["name"].replace("::GetSingleton", "").replace("::Singleton", "")
            singletons.setdefault(key, e["rva"])
            singletons.setdefault(key.split("::")[-1], e["rva"])
            srows.append(e)
    write_tsv(os.path.join(args.out, "singletons.tsv"),
              sorted(srows, key=lambda e: e["name"]), COLS + ["subsystem"])

    members = commonlib_scan.scan_members(
        args.commonlib, {k: (v[0], v[1]) for k, v in live_state.CLASSES.items()})
    bridge_src = live_state.read_bridge_source(args.bridge) if args.bridge else ""
    lrows = live_state.rows(members, bridge_src, singletons)
    write_tsv(os.path.join(args.out, "live_state.tsv"), lrows,
              ["class", "field", "offset", "type", "stream", "singleton",
               "skyrimbridge", "why"])

    cps = control_points.resolve(merged + srows, settings)
    write_tsv(os.path.join(args.out, "control_points.tsv"), cps,
              ["control", "target", "rva", "kind", "confidence", "approach",
               "risk", "notes"])

    frags = techniques.lighting_fragments(img)
    write_tsv(os.path.join(args.out, "lighting_techniques.tsv"),
              [{"rva": r, "order": i, "fragment": w} for i, (r, w) in enumerate(frags)],
              ["rva", "order", "fragment"])

    stats.update({
        "exe_sha256": sha256(args.exe), "addrlib_version": ver,
        "addrlib_ids": len(db), "addrlib_alt_ids": len(alt),
        "pdata_functions": img.function_count(), "code": img.code_report(),
        "rtti_vtables": len(vtables),
        "rtti_primary_vtables": sum(1 for v in vtables if v["offset"] == 0),
        "commonlib_ids": len(ids), "commonlib_checks": dict(cl_checks),
        "commonlib_virtual_classes": len(virtuals),
        "vtable_crosscheck": entries.vtable_checks(img, vtables, ids, db),
        "settings": len(settings),
        "settings_by_file": _count(settings, "file"),
        "live_state_rows": len(lrows),
        "live_state_gaps": sum(1 for r in lrows if r["skyrimbridge"] == "GAP"),
        "live_state_streamable_gaps": sum(1 for r in lrows if r["skyrimbridge"] == "GAP" and r["stream"]),
        "lighting_fragments": len(frags),
        "control_rows": len(cps),
        "control_unresolved": sum(1 for r in cps if r["rva"] == ""), "seconds": round(time.time() - t0, 1),
    })
    with open(os.path.join(args.out, "stats.json"), "w", encoding="utf-8") as f:
        json.dump(stats, f, indent=1)
    print(json.dumps(stats["subsystems"], indent=1))
    print({k: v for k, v in stats.items() if k not in ("subsystems", "vtable_crosscheck")})
    print(stats["vtable_crosscheck"])


def _count(rows, key):
    out = {}
    for r in rows:
        out[r[key]] = out.get(r[key], 0) + 1
    return out


if __name__ == "__main__":
    main()
