"""Decode an Address Library (versionlib-*.bin, format 2) into {id: rva}.

The format is documented by its reader in CommonLibSSE-NG (MIT), REL/ID.h,
IDDatabase::unpack_file. This is a fresh Python reader of that data format.
The .bin is the user's own data file; it is read here and never copied.
"""
import struct


class _Reader:
    def __init__(self, data):
        self.d, self.p = data, 0

    def take(self, fmt):
        v = struct.unpack_from(fmt, self.d, self.p)
        self.p += struct.calcsize(fmt)
        return v[0] if len(v) == 1 else v


def _delta(r, kind, prev):
    if kind == 1:
        return prev + 1
    if kind == 2:
        return prev + r.take("<B")
    if kind == 3:
        return prev - r.take("<B")
    if kind == 4:
        return prev + r.take("<H")
    if kind == 5:
        return prev - r.take("<H")
    if kind == 6:
        return r.take("<H")
    if kind == 7:
        return r.take("<I")
    raise ValueError(f"unhandled delta kind {kind}")


def load(path):
    """Return (version tuple, pointer size, {id: rva})."""
    with open(path, "rb") as f:
        r = _Reader(f.read())
    fmt = r.take("<i")
    if fmt != 2:
        raise ValueError(f"expected address library format 2, got {fmt}")
    version = r.take("<4i")
    name_len = r.take("<i")
    r.p += name_len
    ptr_size, count = r.take("<i"), r.take("<i")
    out, prev_id, prev_off = {}, 0, 0
    for _ in range(count):
        t = r.take("<B")
        lo, hi = t & 0xF, t >> 4
        ident = r.take("<Q") if lo == 0 else _delta(r, lo, prev_id)
        base = prev_off // ptr_size if hi & 8 else prev_off
        off = r.take("<Q") if (hi & 7) == 0 else _delta(r, hi & 7, base)
        if hi & 8:
            off *= ptr_size
        out[ident] = off
        prev_id, prev_off = ident, off
    return tuple(version), ptr_size, out
