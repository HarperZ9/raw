"""Structure of every .pdata function of a SkyrimSE.exe whose code is readable on disk. Original work, MIT.

    python tools/enginemap/structure_features.py <SkyrimSE.exe> <features.pkl>

Per function: size, instruction count, direct calls and tail jumps (offset, target), RIP-relative references (offset,
target, read|write|addr) and vtable-call slots. Addresses and slot numbers only, never code bytes; the output is local
analysis data for port_version.py. An exe whose code is encrypted on disk is refused (pe_image reports it), and nothing
is decrypted.
"""
import os, pickle, struct, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe_image
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_AC_WRITE
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP, X86_REG_INVALID



KINDS, ACCESS = ("call", "tail"), ("read", "write", "addr")
REC = struct.Struct("<IIB")     # offset in the function, target, kind or access
VREC = struct.Struct("<Ii")     # offset, vtable slot


def features(im, b, e, md):
    """(size, instructions, calls, refs, vcalls) for one function; the three lists packed (REC, REC, VREC)."""
    t = im.text
    calls, refs, vcalls, n = bytearray(), bytearray(), bytearray(), 0
    for ins in md.disasm(t["data"][b - t["rva"]:e - t["rva"]], b):
        n += 1
        m, ops = ins.mnemonic, ins.operands
        if m in ("call", "jmp") and len(ops) == 1:
            o = ops[0]
            if o.type == X86_OP_IMM:
                if m == "call":
                    calls += REC.pack(ins.address - b, o.imm & 0xFFFFFFFF, 0)
                elif not (b <= o.imm < e):
                    calls += REC.pack(ins.address - b, o.imm & 0xFFFFFFFF, 1)
                continue
            if (m == "call" and o.type == X86_OP_MEM and o.mem.base not in (X86_REG_RIP, X86_REG_INVALID)
                    and o.mem.index == X86_REG_INVALID):
                vcalls += VREC.pack(ins.address - b, o.mem.disp // 8)
                continue
        for o in ops:
            if o.type == X86_OP_MEM and o.mem.base == X86_REG_RIP:
                tgt = (ins.address + ins.size + o.mem.disp) & 0xFFFFFFFF
                acc = 2 if m == "lea" else (1 if (o.access & CS_AC_WRITE) else 0)
                refs += REC.pack(ins.address - b, tgt, acc)
    return e - b, n, bytes(calls), bytes(refs), bytes(vcalls)


def unpack(rec):
    """features() packed lists back to tuples: calls/refs (offset, target, name), vcalls (offset, slot)."""
    size, n, calls, refs, vcalls = rec
    return (size, n, [(o, t, KINDS[k]) for o, t, k in REC.iter_unpack(calls)],
            [(o, t, ACCESS[k]) for o, t, k in REC.iter_unpack(refs)], list(VREC.iter_unpack(vcalls)))


def main():
    exe, out_path = sys.argv[1], sys.argv[2]
    im = pe_image.Image(exe)
    if pe_image.entropy(im.text["data"][:1 << 20]) > 7.5:
        raise SystemExit(f"{exe}: code section looks encrypted on disk; use the in-process call graph instead")
    md = Cs(CS_ARCH_X86, CS_MODE_64)
    md.detail = True
    out, insns = {}, 0
    for b, e in im._funcs:   # one process, packed results: about 1 GB less than a pool of tuple lists
        if im.is_code_rva(b):
            out[b] = features(im, b, e, md)
            insns += out[b][1]
    with open(out_path, "wb") as f:
        pickle.dump({"format": "packed-1", "functions": out}, f)
    print(len(out), "functions", insns, "instructions")


if __name__ == "__main__":
    main()
