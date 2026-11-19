"""Structure of every .pdata function of a SkyrimSE.exe whose code is readable on disk. Original work, MIT.

    python -I tools/enginemap/structure_features.py <SkyrimSE.exe> <features.pkl>

Per function: size, instruction count, direct calls and tail jumps (offset, target), RIP-relative references (offset,
target, read|write|addr) and vtable-call slots. Addresses and slot numbers only, never code bytes; the output is local
analysis data for port_version.py. An exe whose code is encrypted on disk is refused (pe_image reports it), and nothing
is decrypted. Two worker processes (the workstation's limit for heavy jobs).
"""
import os, sys, pickle, multiprocessing as mp
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import pe_image
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, CS_AC_WRITE
from capstone.x86 import X86_OP_MEM, X86_OP_IMM, X86_REG_RIP, X86_REG_INVALID

EXE = sys.argv[1]; OUT = sys.argv[2]
def work(args):
    lo, hi = args
    im = pe_image.Image(EXE)
    t = im.text; data = t['data']; trva = t['rva']
    md = Cs(CS_ARCH_X86, CS_MODE_64); md.detail = True
    res = {}
    for b, e in im._funcs[lo:hi]:
        if not im.is_code_rva(b): continue
        calls, refs, vcalls, n = [], [], [], 0
        code = data[b - trva:e - trva]
        for ins in md.disasm(code, b):
            n += 1
            m = ins.mnemonic
            ops = ins.operands
            if m in ('call', 'jmp') and len(ops) == 1:
                o = ops[0]
                if o.type == X86_OP_IMM:
                    tgt = o.imm
                    if m == 'call': calls.append((ins.address - b, tgt, 'call'))
                    elif not (b <= tgt < e): calls.append((ins.address - b, tgt, 'tail'))
                    continue
                if o.type == X86_OP_MEM and o.mem.base not in (X86_REG_RIP, X86_REG_INVALID) and o.mem.index == X86_REG_INVALID and m == 'call':
                    vcalls.append((ins.address - b, o.mem.disp // 8))
                    continue
            for i, o in enumerate(ops):
                if o.type == X86_OP_MEM and o.mem.base == X86_REG_RIP:
                    tgt = ins.address + ins.size + o.mem.disp
                    acc = 'addr' if m == 'lea' else ('write' if (o.access & CS_AC_WRITE) else 'read')
                    refs.append((ins.address - b, tgt, acc))
        res[b] = (e - b, n, calls, refs, vcalls)
    return res
if __name__ == '__main__':
    im = pe_image.Image(EXE); N = len(im._funcs)
    if pe_image.entropy(im.text['data'][:1 << 20]) > 7.5:
        raise SystemExit(f'{EXE}: code section looks encrypted on disk; use the in-process call graph instead')
    del im
    step = 4000
    chunks = [(i, min(N, i + step)) for i in range(0, N, step)]
    out = {}
    with mp.Pool(2) as p:
        for r in p.imap_unordered(work, chunks): out.update(r)
    pickle.dump(out, open(OUT, 'wb'))
    print(len(out), 'functions', sum(v[1] for v in out.values()), 'instructions')
