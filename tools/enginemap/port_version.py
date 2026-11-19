"""Carry RAW's address table from a reference runtime to a new one. Original work, MIT.

    python -I tools/enginemap/port_version.py --ref-exe <reference SkyrimSE.exe> --new-exe <new SkyrimSE.exe>
        --version 1.7.104.0 --entries data/versions/entries.tsv --ref-table data/versions/skyrimse-1.6.1170.tsv
        --out data/versions/skyrimse-1.7.104.tsv [--ref-cg <raw_engine_out of a CallGraph=1 run on the reference>]
        [--new-features <features.pkl from structure_features.py>] [--al-ref <versionlib of the reference>]
        [--al-new <versionlib of the new runtime>] [--report <folder>]

Sources, in the order they are trusted:
  ours    RTTI vtables and their slots, Setting objects by name (both read from the exe's data sections), and functions
          and globals paired by structure: the reference's in-process call graph (addresses only) against the new exe's
          functions (structure_features.py: sizes, direct calls, RIP-relative references, vtable-call slots).
  al      an installed Address Library, used two ways: the reference value's id looked up in the new runtime's file is
          a cross-check of every value of ours, and a fallback where ours has none. Its files stay local data.
Every row records its source and confidence, and the report gives the agreement with the Address Library per kind.

Pairing functions by structure:
  1. seeds: the same slot of the same class's primary vtable, skipping stubs shared by many vtables;
  2. propagation: two paired functions pair their callees by position, aligned on callee size where the lists differ;
  3. agreement: a pair proposed more than once with no rival, or with sizes within 5 %, is high confidence.
A global is the target the paired functions reference in the same way (read, write or address) most often.
"""
from __future__ import annotations

import argparse
import bisect
import csv
import difflib
import math
import os
import pickle
import sys
from collections import Counter, defaultdict

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import addrlib  # noqa: E402
import pe_image  # noqa: E402
import rtti  # noqa: E402
import settings_scan  # noqa: E402

SHARED_STUB = 40


def tsv(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        yield from csv.DictReader((l for l in f if not l.startswith("#")), delimiter="\t")


def hx(s):
    return int(s, 16) if s and s.startswith("0x") else 0


def read_table(path):
    head, rows = {}, []
    with open(path, encoding="utf-8") as f:
        lines = [l.rstrip("\r\n") for l in f if l.strip() and not l.startswith("#")]
    for l in lines:
        p = l.split("\t")
        if p[0] in ("version", "timestamp", "size_of_image"):
            head[p[0]] = p[1]
        elif p[0] != "name":
            rows.append({"name": p[0], "kind": p[1], "value": hx(p[2]) if len(p) > 2 else 0})
    return head, rows


class Side:
    def __init__(self, img, vts):
        self.img = img
        self.size = {b: e - b for b, e in img._funcs}
        self.starts = [b for b, _ in img._funcs]
        self.calls, self.vcalls, self.refs = defaultdict(list), defaultdict(list), defaultdict(list)
        self.vts = vts

    def finish(self):
        for d in (self.calls, self.vcalls, self.refs):
            for f in d:
                d[f].sort()

    def func_of(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        return self.starts[i] if i >= 0 and rva < self.starts[i] + self.size[self.starts[i]] else None


def load_ref(img, cg, vts):
    s = Side(img, vts)
    for r in tsv(os.path.join(cg, "cg_edges.tsv")):
        s.calls[hx(r["caller"])].append((hx(r["site"]), hx(r["callee"]), r["kind"]))
    for r in tsv(os.path.join(cg, "cg_vcalls.tsv")):
        s.vcalls[hx(r["function"])].append((hx(r["site"]), int(r["slot"])))
    for r in tsv(os.path.join(cg, "cg_refs.tsv")):
        s.refs[hx(r["function"])].append((hx(r["site"]), hx(r["target"]), r["access"]))
    s.finish()
    return s


def load_new(img, feats, vts):
    s = Side(img, vts)
    for b, (size, _n, calls, refs, vcalls) in pickle.load(open(feats, "rb")).items():
        s.size[b] = size
        s.calls[b] = [(b + o, t, k) for o, t, k in calls]
        s.refs[b] = [(b + o, t, a) for o, t, a in refs]
        s.vcalls[b] = [(b + o, sl) for o, sl in vcalls]
    s.finish()
    return s


def primary_vtables(vts):
    by = defaultdict(list)
    for v in vts:
        if v["offset"] == 0:
            by[v["name"]].append(v)
    return {k: v[0] for k, v in by.items() if len(v) == 1}


def slot_use(vts):
    c = Counter()
    for v in vts:
        for f in set(v["slots"]):
            c[f] += 1
    return c


class Matcher:
    def __init__(self, ref, new):
        self.ref, self.new = ref, new
        self.votes = defaultdict(Counter)
        self.match, self.back = {}, {}

    def ratio(self, a, b):
        sa, sb = self.ref.size.get(a), self.new.size.get(b)
        return min(sa, sb) / max(sa, sb) if sa and sb else 0.0

    def propose(self, a, b, w=1):
        if self.ratio(a, b) >= 0.6:
            self.votes[a][b] += w

    def seeds(self):
        rp, np_ = primary_vtables(self.ref.vts), primary_vtables(self.new.vts)
        ru, nu = slot_use(self.ref.vts), slot_use(self.new.vts)
        for name, rv in rp.items():
            nv = np_.get(name)
            if not nv or len(nv["slots"]) != len(rv["slots"]):
                continue
            for a, b in zip(rv["slots"], nv["slots"]):
                if ru[a] <= SHARED_STUB and nu[b] <= SHARED_STUB:
                    self.propose(a, b, 3)

    def settle(self):
        changed = False
        for a, c in self.votes.items():
            if a in self.match:
                continue
            top = c.most_common(2)
            b, n = top[0]
            second = top[1][1] if len(top) > 1 else 0
            if n > second and b not in self.back:
                self.match[a], self.back[b] = b, a
                changed = True
        return changed

    def tok(self, side, f):
        s = side.size.get(f)
        return int(math.log2(s) * 2) if s else -1

    def spread(self):
        for a, b in list(self.match.items()):
            ca, cb = self.ref.calls.get(a, []), self.new.calls.get(b, [])
            if not ca or not cb:
                continue
            if len(ca) == len(cb):
                pairs = [(x[1], y[1]) for x, y in zip(ca, cb) if x[2] == y[2]]
            else:
                ta = [self.tok(self.ref, x[1]) for x in ca]
                tb = [self.tok(self.new, y[1]) for y in cb]
                sm = difflib.SequenceMatcher(None, ta, tb, autojunk=False)
                pairs = [(ca[i + k][1], cb[j + k][1]) for i, j, n in sm.get_matching_blocks() for k in range(n)]
            for x, y in pairs:
                if x not in self.match:
                    self.propose(x, y)

    def run(self, rounds=16):
        self.seeds()
        self.settle()
        for _ in range(rounds):
            self.spread()
            if not self.settle():
                break

    def confidence(self, a):
        b = self.match.get(a)
        if b is None:
            return None
        c = self.votes[a]
        n, total = c[b], sum(c.values())
        if n == total and (n >= 2 or self.ratio(a, b) >= 0.95):
            return "high"
        return "medium" if n > total - n else "low"


def match_global(m, g):
    users = [(f, acc) for f, rs in m.ref.refs.items() for _s, t, acc in rs if t == g]
    votes, seen = Counter(), 0
    for f, acc in users:
        b = m.match.get(f)
        if b is None:
            continue
        seen += 1
        for t in {t for _s, t, a in m.new.refs.get(b, []) if a == acc}:
            votes[t] += 1
    if not votes:
        return None, 0, len(users), "unknown"
    top = votes.most_common(2)
    t, n = top[0]
    if len(top) > 1 and top[1][1] == n:
        return None, n, len(users), "unknown"
    conf = "high" if n >= 3 and n >= 0.6 * seen else "medium" if n >= 2 or seen == 1 else "low"
    return t, n, len(users), conf


def static(exe, cache):
    if cache and os.path.exists(cache):
        return pickle.load(open(cache, "rb"))
    im = pe_image.Image(exe)
    vt = rtti.scan(im)
    out = {"vt": vt, "st": settings_scan.scan(im, vt)}
    if cache:
        pickle.dump(out, open(cache, "wb"))
    return out


def main():
    ap = argparse.ArgumentParser()
    for k in ("ref-exe", "new-exe", "version", "entries", "ref-table", "out"):
        ap.add_argument("--" + k, required=True)
    for k in ("ref-cg", "new-features", "al-ref", "al-new", "report", "ref-static", "new-static"):
        ap.add_argument("--" + k)
    a = ap.parse_args()
    nimg = pe_image.Image(a.new_exe)
    rs, ns = static(a.ref_exe, a.ref_static), static(a.new_exe, a.new_static)
    m = None
    if a.ref_cg and a.new_features:
        m = Matcher(load_ref(pe_image.Image(a.ref_exe), a.ref_cg, rs["vt"]), load_new(nimg, a.new_features, ns["vt"]))
        m.run()
        conf = Counter(m.confidence(x) for x in m.match)
        print(f"functions paired by structure: {len(m.match)} of {len(m.ref.size)}; {dict(conf)}")
    al_map = None
    if a.al_ref and a.al_new:
        _, _, r_ids = addrlib.load(a.al_ref)
        _, _, n_ids = addrlib.load(a.al_new)
        inv = {o: i for i, o in r_ids.items()}
        al_map = lambda rva: n_ids.get(inv.get(rva, -1))  # noqa: E731

    rimg = pe_image.Image(a.ref_exe)
    rimg_funcs = rimg._funcs
    rimg_starts = [b for b, _ in rimg_funcs]
    _, ref_rows = read_table(a.ref_table)
    ref_by = {r["name"]: r for r in ref_rows}
    nset = {s["name"]: s["rva"] for s in ns["st"]}
    nvt = primary_vtables(ns["vt"])
    spec = {e["name"]: e for e in tsv(a.entries)}
    out = []   # name, kind, value, confidence, source, al value
    for r in ref_rows:
        e = spec.get(r["name"])
        kind, rv = r["kind"], r["value"]
        val, conf, src = None, "unknown", ""
        rule = e["rule"].split() if e else (["setting"] if kind == "setting" else ["match"])
        if rule[0] == "setting":
            val = nset.get(r["name"])
            conf, src = ("high", "settings scan") if val else ("unknown", "settings scan: not found")
        elif rule[0] == "rtti":
            v = nvt.get(" ".join(rule[1:]))
            val, conf, src = (v["vtable"], "high", "rtti") if v else (None, "unknown", "rtti: no single primary vtable")
        elif rule[0] == "slot":
            v = nvt.get(rule[1])
            i = int(rule[2], 16)
            if v and i < len(v["slots"]):
                val, conf, src = v["slots"][i], "high", f"rtti slot {rule[2]}"
        elif rule[0] == "given":
            val, conf, src = int(rule[1], 16), rule[2], " ".join(rule[3:])
        elif rule[0] == "same":
            val, conf, src = rv, "carried", "the reference's offset; checked in game"
        elif m and kind == "function":
            val, conf, src = m.match.get(rv), m.confidence(rv) or "unknown", "structure match"
        elif m and kind == "global":
            val, n, u, conf = match_global(m, rv)
            src = f"structure match: {n} of {u} users agree"
        elif m and kind in ("range", "site"):
            f0 = m.ref.func_of(rv)
            if f0 in m.match:
                val = m.match[f0] + (rv - f0) if kind == "site" else m.match[f0]
                conf, src = "low", "offset in the paired function" if kind == "site" else "start of the paired function"
        al = al_map(rv) if al_map and kind not in ("offset", "range", "site") else None
        if kind in ("range", "site") and al_map:
            # through the function holding the address: a site keeps its offset in that function; a range's begin is
            # the first function at or after it, its end the end of the last function before it
            fi = bisect.bisect_right(rimg_starts, rv) - 1
            if kind == "range" and r["name"].endswith(".begin") and rimg_funcs[fi][0] < rv:
                fi += 1
            step = 1 if r["name"].endswith(".begin") else -1
            for _ in range(64):   # the nearest function the library names, inward from the boundary
                f0, e0 = rimg_funcs[fi]
                mapped = al_map(f0)
                if mapped or kind == "site":
                    break
                fi += step
            if mapped:
                if kind == "site" and rv < e0:
                    al = mapped + (rv - f0)
                elif kind == "range" and r["name"].endswith(".begin"):
                    al = mapped
                elif kind == "range":
                    al = mapped + (e0 - f0)
        if not val and al:
            val, conf, src = al, "al", "Address Library fallback"
        out.append((r["name"], kind, val, conf if val else "unknown", src, al))

    exe = nimg.pe
    with open(a.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(f"# RAW address table for SkyrimSE.exe {a.version}. Generated by tools/enginemap/port_version.py from the\n"
                f"# reference table {os.path.basename(a.ref_table)}; addresses and names only. Confidence: high, medium, low\n"
                f"# (structure match), carried (an offset kept from the reference), al (Address Library fallback).\n")
        f.write(f"version\t{a.version}\ntimestamp\t0x{exe.FILE_HEADER.TimeDateStamp:08X}\n"
                f"size_of_image\t0x{exe.OPTIONAL_HEADER.SizeOfImage:X}\n")
        f.write("name\tkind\tvalue\tconfidence\tsource\n")
        for name, kind, val, conf, src, _al in out:
            f.write(f"{name}\t{kind}\t{'0x%X' % val if val else ''}\t{conf}\t{src}\n")
    by_kind = defaultdict(Counter)
    for name, kind, val, conf, src, al in out:
        c = by_kind[kind]
        c["rows"] += 1
        c["ours"] += bool(val) and conf not in ("al", "unknown")
        if al and val and conf not in ("al",):
            c["checked"] += 1
            c["agree"] += val == al
        c["al_fallback"] += conf == "al"
        c["unknown"] += not val
    lines = ["kind\trows\tours\tal_fallback\tunknown\tchecked_against_al\tagree\tagreement"]
    for k, c in sorted(by_kind.items()):
        lines.append(f"{k}\t{c['rows']}\t{c['ours']}\t{c['al_fallback']}\t{c['unknown']}\t{c['checked']}\t{c['agree']}\t"
                     f"{c['agree'] / c['checked']:.4f}" if c["checked"] else f"{k}\t{c['rows']}\t{c['ours']}\t{c['al_fallback']}\t{c['unknown']}\t0\t0\t")
    print("\n".join(lines))
    if a.report:
        os.makedirs(a.report, exist_ok=True)
        with open(os.path.join(a.report, "agreement.tsv"), "w", encoding="utf-8") as f:
            f.write("\n".join(lines) + "\n")
        with open(os.path.join(a.report, "disagreements.tsv"), "w", encoding="utf-8") as f:
            f.write("name\tkind\tours\tal\tconfidence\tsource\n")
            for name, kind, val, conf, src, al in out:
                if al and val and val != al and conf != "al":
                    f.write(f"{name}\t{kind}\t0x{val:X}\t0x{al:X}\t{conf}\t{src}\n")
        if m:
            with open(os.path.join(a.report, "functions.tsv"), "w", encoding="utf-8") as f:
                f.write("ref\tnew\tvotes\tproposals\tsize_ratio\tconfidence\tal\n")
                for x in sorted(m.match):
                    c, y = m.votes[x], m.match[x]
                    alv = al_map(x) if al_map else None
                    f.write(f"0x{x:X}\t0x{y:X}\t{c[y]}\t{sum(c.values())}\t{m.ratio(x, y):.3f}\t{m.confidence(x)}\t"
                            f"{'0x%X' % alv if alv else ''}\n")


if __name__ == "__main__":
    main()
