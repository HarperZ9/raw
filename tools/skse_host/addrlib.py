"""Reads an Address Library database (versionlib-<version>.bin, format 2) and prints the offsets for given IDs.
Original work, MIT. The record encoding follows CommonLibSSE-NG's IDDatabase reader (MIT, Ryan McKenzie and
contributors), which documents the format; no Address Library file is copied or shipped. RAW uses this tool once
per game build to write the built-in table in src/skse_host/skse_addresses.h, and the host reads the database
itself at run time as a cross-check when one is installed.

    python -I tools/skse_host/addrlib.py <versionlib-1-6-1170-0.bin> 400475 400269 ...
"""
from __future__ import annotations

import struct
import sys


def read(path: str) -> tuple[tuple[int, ...], dict[int, int]]:
    data = open(path, "rb").read()
    pos = 0

    def take(fmt: str):
        nonlocal pos
        v = struct.unpack_from("<" + fmt, data, pos)
        pos += struct.calcsize("<" + fmt)
        return v if len(v) > 1 else v[0]

    if take("i") != 2:
        raise ValueError("not an Address Library format 2 file")
    version = take("4i")
    name_len = take("i")
    pos += name_len
    ptr_size, count = take("i"), take("i")
    out: dict[int, int] = {}
    prev_id = prev_off = 0
    for _ in range(count):
        t = take("B")
        lo, hi = t & 0xF, t >> 4
        idv = {0: lambda: take("Q"), 1: lambda: prev_id + 1, 2: lambda: prev_id + take("B"),
               3: lambda: prev_id - take("B"), 4: lambda: prev_id + take("H"), 5: lambda: prev_id - take("H"),
               6: lambda: take("H"), 7: lambda: take("I")}[lo]()
        tmp = prev_off // ptr_size if hi & 8 else prev_off
        off = {0: lambda: take("Q"), 1: lambda: tmp + 1, 2: lambda: tmp + take("B"), 3: lambda: tmp - take("B"),
               4: lambda: tmp + take("H"), 5: lambda: tmp - take("H"), 6: lambda: take("H"),
               7: lambda: take("I")}[hi & 7]()
        if hi & 8:
            off *= ptr_size
        out[idv] = off
        prev_id, prev_off = idv, off
    return version, out


def main() -> int:
    version, table = read(sys.argv[1])
    print(f"# version {'.'.join(map(str, version))}, {len(table)} ids")
    for a in sys.argv[2:]:
        i = int(a)
        print(f"{i}\t0x{table[i]:X}" if i in table else f"{i}\tmissing")
    return 0


if __name__ == "__main__":
    sys.exit(main())
