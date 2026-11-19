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
    if fmt == 5:
        return _load5(r)
    if fmt != 2:
        raise ValueError(f"expected address library format 2 or 5, got {fmt}")
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


def _load5(r):
    """Format 5 (1.7.99 and later): a 96-byte header (format, four version numbers, the exe name, the pointer size at
    0x54, the id count at 0x5C), then one uint32 RVA per id, indexed by id; 0 marks an id with no address. Layout read
    from the files themselves; no reader code copied."""
    version = struct.unpack_from("<4i", r.d, 4)
    ptr_size, count = struct.unpack_from("<i", r.d, 0x54)[0], struct.unpack_from("<I", r.d, 0x5C)[0]
    offs = struct.unpack_from(f"<{count}I", r.d, 0x60)
    return tuple(version), ptr_size, {i: o for i, o in enumerate(offs) if o}
