"""Compiled Papyrus scripts (.pex, Skyrim: big-endian, version 3.2): a reader and a writer. Original work, MIT.

The layout follows the public format description (UESP, "Compiled Script File Format"). The reader exists to check
the writer: a file read and written again must come back byte for byte (tools/papyrus/test_pex.py runs that on the
game's own scripts in place; no game file is copied anywhere). The writer builds RAW's test scripts
(make_load_plugin.py).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

MAGIC = 0xFA57C0DE
# opcode: (name, fixed argument count, variadic)
OPCODES = [
    ("nop", 0, False), ("iadd", 3, False), ("fadd", 3, False), ("isub", 3, False), ("fsub", 3, False),
    ("imul", 3, False), ("fmul", 3, False), ("idiv", 3, False), ("fdiv", 3, False), ("imod", 3, False),
    ("not", 2, False), ("ineg", 2, False), ("fneg", 2, False), ("assign", 2, False), ("cast", 2, False),
    ("cmp_eq", 3, False), ("cmp_lt", 3, False), ("cmp_le", 3, False), ("cmp_gt", 3, False), ("cmp_ge", 3, False),
    ("jmp", 1, False), ("jmpt", 2, False), ("jmpf", 2, False), ("callmethod", 3, True), ("callparent", 2, True),
    ("callstatic", 3, True), ("return", 1, False), ("strcat", 3, False), ("propget", 3, False), ("propset", 3, False),
    ("array_create", 2, False), ("array_length", 2, False), ("array_getelement", 3, False),
    ("array_setelement", 3, False), ("array_findelement", 4, False), ("array_rfindelement", 4, False),
]
OP = {n: i for i, (n, _, _) in enumerate(OPCODES)}
# value kinds
NONE, IDENT, STR, INT, FLOAT, BOOL = range(6)


class Reader:
    def __init__(self, data: bytes):
        self.d, self.i = data, 0

    def take(self, fmt: str):
        v = struct.unpack_from(">" + fmt, self.d, self.i)
        self.i += struct.calcsize(">" + fmt)
        return v[0] if len(v) == 1 else v

    def wstr(self) -> str:
        n = self.take("H")
        s = self.d[self.i:self.i + n].decode("cp1252")
        self.i += n
        return s


class Writer:
    def __init__(self):
        self.b = bytearray()

    def put(self, fmt: str, *v) -> None:
        self.b += struct.pack(">" + fmt, *v)

    def wstr(self, s: str) -> None:
        e = s.encode("cp1252")
        self.put("H", len(e))
        self.b += e


@dataclass
class Function:
    ret: int = 0
    doc: int = 0
    user_flags: int = 0
    flags: int = 0
    params: list = field(default_factory=list)   # (name index, type index)
    locals: list = field(default_factory=list)
    code: list = field(default_factory=list)     # (opcode, [(kind, value), ...])


def read_value(r: Reader):
    k = r.take("B")
    if k == NONE:
        return (k, None)
    if k in (IDENT, STR):
        return (k, r.take("H"))
    if k == INT:
        return (k, r.take("i"))
    if k == FLOAT:
        return (k, r.take("f"))
    if k == BOOL:
        return (k, r.take("B"))
    raise ValueError(f"value kind {k} at {r.i}")


def write_value(w: Writer, v) -> None:
    k, x = v
    w.put("B", k)
    if k in (IDENT, STR):
        w.put("H", x)
    elif k == INT:
        w.put("i", x)
    elif k == FLOAT:
        w.put("f", x)
    elif k == BOOL:
        w.put("B", x)


def read_function(r: Reader) -> Function:
    f = Function(r.take("H"), r.take("H"), r.take("I"), r.take("B"))
    f.params = [(r.take("H"), r.take("H")) for _ in range(r.take("H"))]
    f.locals = [(r.take("H"), r.take("H")) for _ in range(r.take("H"))]
    for _ in range(r.take("H")):
        op = r.take("B")
        _, n, var = OPCODES[op]
        args = [read_value(r) for _ in range(n)]
        if var:
            count = read_value(r)
            args.append(count)
            args += [read_value(r) for _ in range(count[1])]
        f.code.append((op, args))
    return f


def write_function(w: Writer, f: Function) -> None:
    w.put("HHIB", f.ret, f.doc, f.user_flags, f.flags)
    for group in (f.params, f.locals):
        w.put("H", len(group))
        for a, b in group:
            w.put("HH", a, b)
    w.put("H", len(f.code))
    for op, args in f.code:
        w.put("B", op)
        for a in args:
            write_value(w, a)


def read_object_body(r: Reader) -> dict:
    o = {"parent": r.take("H"), "doc": r.take("H"), "user_flags": r.take("I"), "auto_state": r.take("H")}
    o["vars"] = []
    for _ in range(r.take("H")):
        o["vars"].append((r.take("H"), r.take("H"), r.take("I"), read_value(r)))
    o["props"] = []
    for _ in range(r.take("H")):
        p = {"name": r.take("H"), "type": r.take("H"), "doc": r.take("H"), "user_flags": r.take("I"), "flags": r.take("B")}
        if p["flags"] & 4:
            p["auto_var"] = r.take("H")
        else:
            if p["flags"] & 1:
                p["read"] = read_function(r)
            if p["flags"] & 2:
                p["write"] = read_function(r)
        o["props"].append(p)
    o["states"] = []
    for _ in range(r.take("H")):
        name = r.take("H")
        o["states"].append((name, [(r.take("H"), read_function(r)) for _ in range(r.take("H"))]))
    return o


def write_object_body(w: Writer, o: dict) -> None:
    w.put("HHIH", o["parent"], o["doc"], o["user_flags"], o["auto_state"])
    w.put("H", len(o["vars"]))
    for name, typ, uf, val in o["vars"]:
        w.put("HHI", name, typ, uf)
        write_value(w, val)
    w.put("H", len(o["props"]))
    for p in o["props"]:
        w.put("HHHIB", p["name"], p["type"], p["doc"], p["user_flags"], p["flags"])
        if p["flags"] & 4:
            w.put("H", p["auto_var"])
        else:
            if p["flags"] & 1:
                write_function(w, p["read"])
            if p["flags"] & 2:
                write_function(w, p["write"])
    w.put("H", len(o["states"]))
    for name, funcs in o["states"]:
        w.put("HH", name, len(funcs))
        for fname, f in funcs:
            w.put("H", fname)
            write_function(w, f)


def read(data: bytes) -> dict:
    r = Reader(data)
    if r.take("I") != MAGIC:
        raise ValueError("not a Skyrim .pex (big-endian magic)")
    s = {"major": r.take("B"), "minor": r.take("B"), "game": r.take("H"), "time": r.take("Q"),
         "source": r.wstr(), "user": r.wstr(), "machine": r.wstr()}
    s["strings"] = [r.wstr() for _ in range(r.take("H"))]
    s["debug"] = None
    if r.take("B"):
        dbg = {"time": r.take("Q"), "funcs": []}
        for _ in range(r.take("H")):
            obj, st, fn, kind, n = r.take("H"), r.take("H"), r.take("H"), r.take("B"), r.take("H")
            dbg["funcs"].append((obj, st, fn, kind, [r.take("H") for _ in range(n)]))
        s["debug"] = dbg
    s["user_flags"] = [(r.take("H"), r.take("B")) for _ in range(r.take("H"))]
    s["objects"] = []
    for _ in range(r.take("H")):
        name, size = r.take("H"), r.take("I")
        start = r.i
        body = read_object_body(r)
        body["name"], body["size"], body["size_minus_body"] = name, size, size - (r.i - start)
        s["objects"].append(body)
    if r.i != len(data):
        raise ValueError(f"{len(data) - r.i} bytes left after the objects")
    return s


def write(s: dict) -> bytes:
    w = Writer()
    w.put("IBBHQ", MAGIC, s["major"], s["minor"], s["game"], s["time"])
    for k in ("source", "user", "machine"):
        w.wstr(s[k])
    w.put("H", len(s["strings"]))
    for x in s["strings"]:
        w.wstr(x)
    if s["debug"]:
        w.put("BQH", 1, s["debug"]["time"], len(s["debug"]["funcs"]))
        for obj, st, fn, kind, lines in s["debug"]["funcs"]:
            w.put("HHHBH", obj, st, fn, kind, len(lines))
            for x in lines:
                w.put("H", x)
    else:
        w.put("B", 0)
    w.put("H", len(s["user_flags"]))
    for name, idx in s["user_flags"]:
        w.put("HB", name, idx)
    w.put("H", len(s["objects"]))
    for o in s["objects"]:
        body = Writer()
        write_object_body(body, o)
        # the size field counts the body plus SIZE_EXTRA bytes (measured on the game's own files by test_pex.py)
        w.put("HI", o["name"], len(body.b) + o.get("size_minus_body", SIZE_EXTRA))
        w.b += body.b
    return bytes(w.b)


SIZE_EXTRA = 4
