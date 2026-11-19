"""MSVC x64 RTTI scan: complete object locators, type names, vtables.

Reads only the data sections (.rdata/.data). Vtable slots are pointers into
.text, so the virtual function addresses come out even when the code bytes
on disk are encrypted.
"""
import re
import struct


def demangle_type(raw):
    """'.?AVInner@Outer@RE@@' -> 'RE::Outer::Inner' (templates kept raw)."""
    if not raw.startswith((".?AV", ".?AU")):
        return raw
    body = raw[4:]
    if body.endswith("@@"):
        body = body[:-2]
    if body.startswith("?$"):
        m = re.match(r"^\?\$(\w+)@(.*)$", body)
        if m:
            raw_args = [a for a in m.group(2).split("@") if a]
            if all(re.match(r"^[VU][A-Za-z_]\w*$", a) for a in raw_args):
                return f"{m.group(1)}<{', '.join(a[1:] for a in raw_args)}>"
        return raw
    if "?$" in body:
        return raw
    parts = [p for p in body.split("@") if p]
    return "::".join(reversed(parts))


def scan(img):
    """Return list of dicts: {col, type, name, offset, vtable, slots}."""
    rdata = next(s for s in img.sections if s["name"] == ".rdata")
    d, base_rva = rdata["data"], rdata["rva"]
    cols = {}
    for off in range(0, len(d) - 24, 4):
        sig, offs, _cd, ptd, pcd, pself = struct.unpack_from("<6I", d, off)
        if sig != 1 or pself != base_rva + off:
            continue
        raw = img.cstr(ptd + 16)
        if not raw or not raw.startswith(".?A"):
            continue
        cols[base_rva + off] = (raw, offs, _bases(img, pcd), ptd)
    # vtables: a qword equal to the COL's VA, followed by code pointers
    by_va = {img.base + rva: rva for rva in cols}
    out = []
    for off in range(0, len(d) - 8, 8):
        (q,) = struct.unpack_from("<Q", d, off)
        col = by_va.get(q)
        if col is None:
            continue
        vt = base_rva + off + 8
        slots = []
        p = vt
        while True:
            v = img.u64(p)
            if v is None:
                break
            r = v - img.base
            if not img.is_code_rva(r):
                break
            slots.append(r)
            p += 8
            nxt = img.u64(p)
            if nxt is not None and nxt in by_va:
                break
        raw, offs, bases, ptd = cols[col]
        out.append({"col": col, "type": raw, "name": demangle_type(raw),
                    "offset": offs, "vtable": vt, "slots": slots,
                    "bases": bases, "type_descriptor": ptd})
    return out



def _bases(img, pcd):
    """Class hierarchy from the RTTI class descriptor: [(name, mdisp)].

    The first entry is the class itself; the rest are its bases in
    depth-first order. mdisp 0 marks the primary (offset 0) chain.
    """
    hdr = img.read(pcd, 16)
    if hdr is None:
        return []
    _sig, _attr, count, arr = struct.unpack("<4I", hdr)
    out = []
    for i in range(min(count, 64)):
        bcd = img.u32(arr + 4 * i)
        if bcd is None:
            break
        body = img.read(bcd, 24)
        if body is None:
            break
        ptd, _n, mdisp, _pdisp, _vdisp, _a = struct.unpack("<IIiiiI", body)
        raw = img.cstr(ptd + 16)
        if raw:
            out.append((demangle_type(raw), mdisp))
    return out
