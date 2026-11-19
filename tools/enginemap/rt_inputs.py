"""Inputs for raw_engine.dll from a built engine map: addresses and names only.

    python -I tools/enginemap/rt_inputs.py --map <map dir> --out <folder>

engine_rt_refs.tsv    every setting and global the map names (rva, setting|global, name)
engine_rt_vtrace.tsv  the classes whose vtables the module traces (class, vtable rva, slots)
"""
import argparse
import csv
import os
import re

TRACE = [
    "BSShadowLight", "BSShadowDirectionalLight", "BSShadowFrustumLight", "BSShadowParabolicLight",
    "ShadowSceneNode", "BSBatchRenderer", "BSShaderAccumulator", "BSCullingProcess",
    "BSLightingShader", "BSDistantTreeShader", "BSFadeNode",
    "BSScript::Internal::CodeTasklet", "BSScript::Internal::VirtualMachine",
    "IOManager", "BSTaskManager",
]


def rows(path):
    with open(path, encoding="utf-8") as f:
        yield from csv.DictReader(f, delimiter="\t")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--map", required=True)
    ap.add_argument("--out", required=True)
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)
    with open(os.path.join(a.out, "engine_rt_refs.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("rva\tkind\tname\n")
        for r in rows(os.path.join(a.map, "settings.tsv")):
            f.write(f"{r['rva']}\tsetting\t{r['name']}\n")
        for r in rows(os.path.join(a.map, "singletons.tsv")):
            if r["kind"] == "global":
                f.write(f"{r['rva']}\tglobal\t{r['name']}\n")
    found = {}
    for fn in os.listdir(a.map):
        if not fn.endswith(".tsv") or fn in ("settings.tsv", "singletons.tsv"):
            continue
        for r in rows(os.path.join(a.map, fn)):
            if r.get("kind") != "vtable":
                continue
            cls = r.get("name", "")[: -len("::vftable")]
            m = re.match(r"(\d+) slots", r.get("refs", ""))
            if cls in TRACE and m:
                found[cls] = (r["rva"], int(m.group(1)))
    with open(os.path.join(a.out, "engine_rt_vtrace.tsv"), "w", encoding="utf-8", newline="\n") as f:
        f.write("class\tvtable\tslots\n")
        for cls in TRACE:
            if cls in found:
                f.write(f"{cls}\t{found[cls][0]}\t{found[cls][1]}\n")
    missing = [c for c in TRACE if c not in found]
    print(f"{len(found)} classes to trace" + (f"; not in the map: {missing}" if missing else ""))


if __name__ == "__main__":
    main()
