"""Read-only view of a PE image on disk: sections, pointers, .pdata functions.

Nothing here executes or patches the image. Code sections that are
encrypted on disk (a packed or DRM-wrapped executable) are reported, never
decrypted.
"""
import bisect
import math
import struct
from collections import Counter

import pefile


def entropy(blob):
    if not blob:
        return 0.0
    n = len(blob)
    return -sum(c / n * math.log2(c / n) for c in Counter(blob).values())


class Image:
    def __init__(self, path):
        self.path = path
        self.pe = pefile.PE(path, fast_load=True)
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.sections = []
        for s in self.pe.sections:
            name = s.Name.rstrip(b"\0").decode("ascii", "replace")
            self.sections.append({
                "name": name, "rva": s.VirtualAddress,
                "vsize": s.Misc_VirtualSize, "data": s.get_data(),
                "exec": bool(s.Characteristics & 0x20000000),
            })
        self.text = self.sections[0]
        self._funcs = self._load_pdata()
        self._starts = [f[0] for f in self._funcs]

    # -- addressing ---------------------------------------------------
    def section_of(self, rva):
        for s in self.sections:
            if s["rva"] <= rva < s["rva"] + max(s["vsize"], len(s["data"])):
                return s
        return None

    def read(self, rva, size):
        s = self.section_of(rva)
        if s is None:
            return None
        off = rva - s["rva"]
        blob = s["data"][off:off + size]
        return blob if len(blob) == size else None

    def u32(self, rva):
        b = self.read(rva, 4)
        return None if b is None else struct.unpack("<I", b)[0]

    def u64(self, rva):
        b = self.read(rva, 8)
        return None if b is None else struct.unpack("<Q", b)[0]

    def va_to_rva(self, va):
        return va - self.base

    def is_code_rva(self, rva):
        t = self.text
        return t["rva"] <= rva < t["rva"] + t["vsize"]

    def cstr(self, rva, limit=512):
        b = self.read(rva, 1)
        if b is None:
            return None
        s = self.section_of(rva)
        off = rva - s["rva"]
        end = s["data"].find(b"\0", off, off + limit)
        if end < 0:
            return None
        raw = s["data"][off:end]
        try:
            return raw.decode("ascii")
        except UnicodeDecodeError:
            return None

    # -- functions from the exception directory -----------------------
    def _load_pdata(self):
        for s in self.sections:
            if s["name"] == ".pdata":
                d = s["data"]
                out = []
                for i in range(0, len(d) - 11, 12):
                    b, e, _u = struct.unpack_from("<III", d, i)
                    if b == 0 and e == 0:
                        break
                    out.append((b, e))
                out.sort()
                return out
        return []

    def function_count(self):
        return len(self._funcs)

    def is_function_start(self, rva):
        i = bisect.bisect_left(self._starts, rva)
        return i < len(self._starts) and self._starts[i] == rva

    def function_containing(self, rva):
        i = bisect.bisect_right(self._starts, rva) - 1
        if i >= 0 and self._funcs[i][0] <= rva < self._funcs[i][1]:
            return self._funcs[i]
        return None

    # -- protection report ---------------------------------------------
    def code_report(self):
        t = self.text
        ep = self.pe.OPTIONAL_HEADER.AddressOfEntryPoint
        ep_sec = self.section_of(ep)
        ent = entropy(t["data"])
        return {
            "text_entropy": round(ent, 3),
            "entry_section": ep_sec["name"] if ep_sec else "?",
            "has_bind": any(s["name"] == ".bind" for s in self.sections),
            "code_encrypted_on_disk": ent > 7.5,
        }
