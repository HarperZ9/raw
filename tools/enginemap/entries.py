"""Turn the raw scans into subsystem map entries.

Confidence:
  named     the name comes from CommonLibSSE-NG (an Address Library ID it
            names, or a virtual method it names at that slot)
  inferred  the class comes from the binary's own RTTI and the slot index
            is exact, but no source names the method
  unknown   the address is exact, ownership is not (a stub shared by many
            unrelated vtables)
"""
from collections import defaultdict

import classify

SHARED_STUB = 40  # a slot target in more vtables than this is a generic stub


def commonlib_entries(img, ids, db, alt_db):
    out, checks = [], defaultdict(int)
    seen = set()
    for x in ids:
        if x["kind"] not in ("function", "global", "id"):
            continue
        key = (x["ae_id"], x["name"])
        if key in seen:
            continue
        seen.add(key)
        rva = db.get(x["ae_id"])
        if rva is None:
            checks["id_missing_from_db"] += 1
            continue
        sub = classify.subsystem(x["name"])
        kind = x["kind"] if x["kind"] != "id" else "function"
        if kind == "function" and not img.is_code_rva(rva):
            kind = "global"  # CommonLib wraps a data address in a function
        check = []
        if kind == "function":
            ok = img.is_function_start(rva)
            checks["fn_pdata_start" if ok else "fn_no_pdata_entry"] += 1
            check.append("pdata_start" if ok else "no_pdata_entry")
        else:
            sec = img.section_of(rva)
            check.append(f"in_{sec['name'] if sec else 'none'}")
        alt = alt_db.get(x["ae_id"])
        checks["alt_db_same" if alt == rva else
               "alt_db_missing" if alt is None else "alt_db_differs"] += 1
        out.append({
            "subsystem": sub, "rva": rva, "kind": kind, "name": x["name"],
            "confidence": "named", "source": "commonlib+addrlib",
            "addrlib_id": x["ae_id"], "check": ",".join(check),
            "refs": "", "notes": f"{x['file']}:{x['line']}",
        })
    return out, checks


def vtable_checks(img, vtables, ids, db):
    """Cross-check Address Library + CommonLib against the binary's RTTI.

    VTABLE_X[k]: the resolved address must be the start of a vtable the
    RTTI scan found. RTTI_X: the resolved address must be the type
    descriptor of a class the RTTI scan found.
    """
    vt_rvas = {v["vtable"] for v in vtables}
    td_rvas = {v["type_descriptor"] for v in vtables}
    res = {"vtable_hit": 0, "vtable_miss": 0, "rtti_hit": 0, "rtti_miss": 0,
           "unresolved": 0, "miss_examples": []}
    for x in ids:
        if x["kind"] not in ("vtable", "rtti"):
            continue
        rva = db.get(x["ae_id"])
        if rva is None:
            res["unresolved"] += 1
            continue
        if x["kind"] == "vtable":
            hit = rva in vt_rvas
        else:
            hit = rva in td_rvas or (img.cstr(rva + 16) or "").startswith(".?A")
        res[f"{x['kind']}_{'hit' if hit else 'miss'}"] += 1
        if not hit and len(res["miss_examples"]) < 12:
            res["miss_examples"].append((x["name"], hex(rva)))
    return res


def _method_name(cls_bases, slot, virtuals, slot_len):
    for base, mdisp in cls_bases:
        if mdisp != 0:
            continue
        name = virtuals.get(base.split("::")[-1], {}).get(slot)
        if name and slot < slot_len.get(base, 1 << 30):
            return base.split("::")[-1], name
    return None, None


def vtable_entries(img, vtables, virtuals):
    primary = [v for v in vtables if v["offset"] == 0]
    slot_len = {v["name"]: len(v["slots"]) for v in primary}
    users = defaultdict(list)          # fn rva -> [(class, slot, nbases)]
    for v in primary:
        for i, fn in enumerate(v["slots"]):
            users[fn].append((v["name"], i, len(v["bases"])))
    out = []
    for v in primary:
        sub = classify.subsystem(v["name"])
        if sub is None:
            continue
        out.append({
            "subsystem": sub, "rva": v["vtable"], "kind": "vtable",
            "name": f"{v['name']}::vftable", "confidence": "named",
            "source": "rtti", "addrlib_id": "",
            "check": f"col@{v['col']:#x}", "refs": f"{len(v['slots'])} slots",
            "notes": " > ".join(b for b, m in v["bases"][1:] if m == 0)[:200],
        })
    by_name = {v["name"]: v for v in primary}
    for fn, us in users.items():
        owner = min(us, key=lambda u: (u[2], u[0]))
        cls, slot, _n = owner
        sub = classify.subsystem(cls)
        if sub is None:
            subs = {classify.subsystem(u[0]) for u in us}
            if len(subs) != 1 or None in subs:
                continue
            sub = subs.pop()
        decl, meth = _method_name(by_name[cls]["bases"], slot, virtuals, slot_len)
        if len(us) > SHARED_STUB:
            conf, name = "unknown", f"shared_stub_{fn:x}"
        elif meth and not meth.lower().startswith("unk"):
            conf, name = "named", f"{cls}::{meth}"
        elif meth:
            conf, name = "inferred", f"{cls}::{meth}"
        else:
            conf, name = "inferred", f"{cls}::vfunc_{slot:02X}"
        refs = ", ".join(f"{u[0]}[{u[1]:02X}]" for u in sorted(us)[:6])
        if len(us) > 6:
            refs += f", +{len(us) - 6} more"
        out.append({
            "subsystem": sub, "rva": fn, "kind": "vfunc", "name": name,
            "confidence": conf, "source": "rtti" + ("+commonlib" if meth else ""),
            "addrlib_id": "",
            "check": "pdata_start" if img.is_function_start(fn) else "no_pdata_entry",
            "refs": f"vtables({len(us)}): {refs}",
            "notes": f"declared in {decl}" if decl and decl != cls else "",
        })
    return out


def merge(entries, addr_to_id):
    """One row per (rva, kind); prefer named over inferred over unknown."""
    rank = {"named": 0, "inferred": 1, "unknown": 2}
    best = {}
    for e in entries:
        if e["subsystem"] is None:
            continue
        k = (e["rva"], "fn" if e["kind"] in ("function", "vfunc") else e["kind"])
        cur = best.get(k)
        if cur is None or rank[e["confidence"]] < rank[cur["confidence"]]:
            if cur and cur["refs"] and not e["refs"]:
                e["refs"] = cur["refs"]
            best[k] = e
        elif cur and not cur["refs"] and e["refs"]:
            cur["refs"] = e["refs"]
    for e in best.values():
        if not e["addrlib_id"] and e["rva"] in addr_to_id:
            e["addrlib_id"] = addr_to_id[e["rva"]]
    return sorted(best.values(), key=lambda e: (e["subsystem"], e["rva"]))
