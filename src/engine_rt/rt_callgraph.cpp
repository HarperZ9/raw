// In-process call graph of the game's code (RAW engine runtime). Original work, MIT.
//
// Every .pdata function, plus every call target without an unwind entry (leaf functions), is
// decoded once with Zydis. The output holds addresses and counts only, never instruction bytes:
//   cg_edges.tsv       caller, call site, callee, call|tail
//   cg_vcalls.tsv      function, site, vtable slot (call qword ptr [reg+disp], disp/8)
//   cg_refs.tsv        function, site, target, access, for the settings and globals named in the input
//   cg_clamps.tsv      a compare, min or max within 16 instructions after a setting read
//   cg_jumptables.tsv  function, site, table, entries (switch statements; the Papyrus opcode loop is one)
#include "rt_callgraph.h"

#include <Zydis/Zydis.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <deque>
#include <unordered_map>
#include <unordered_set>

namespace raw::rt {
namespace {

struct Decoded {
    uint64_t rva;
    ZydisDecodedInstruction in;
    ZydisDecodedOperand op[ZYDIS_MAX_OPERAND_COUNT];
};

struct Ctx {
    const Image& img;
    const std::vector<RefTarget>& refs;
    FILE *edges, *vcalls, *xrefs, *clamps, *tables;
    ZydisDecoder dec;
    std::unordered_set<uint32_t> decodedLeaf;
    std::deque<uint32_t> leafQueue;
    CallGraphStats st{};
};

const RefTarget* FindRef(const std::vector<RefTarget>& refs, uint64_t rva) {
    // refs is sorted by rva; a Setting is {vtable, value, name}, so any of its 24 bytes counts
    auto it = std::upper_bound(refs.begin(), refs.end(), rva, [](uint64_t v, const RefTarget& r) { return v < r.rva; });
    if (it == refs.begin()) return nullptr;
    --it;
    return rva < uint64_t(it->rva) + it->span ? &*it : nullptr;
}

bool Decode(Ctx& c, uint64_t rva, uint64_t limit, Decoded& d) {
    if (rva >= limit) return false;
    d.rva = rva;
    return ZYAN_SUCCESS(ZydisDecoderDecodeFull(&c.dec, c.img.base + rva, size_t(limit - rva), &d.in, d.op));
}

uint64_t Abs(const Decoded& d, const ZydisDecodedOperand& o) {
    ZyanU64 a = 0;
    if (!ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&d.in, &o, d.rva, &a))) return 0;
    return a;
}

void Clamp(Ctx& c, uint32_t fn, const Decoded& site, const RefTarget& r, uint64_t end) {
    Decoded d;
    uint64_t p = site.rva + site.in.length;
    for (int i = 0; i < 16 && Decode(c, p, end, d); ++i, p += d.in.length) {
        const char* m = ZydisMnemonicGetString(d.in.mnemonic);
        const bool hit = d.in.mnemonic == ZYDIS_MNEMONIC_MINSS || d.in.mnemonic == ZYDIS_MNEMONIC_MAXSS ||
                         d.in.mnemonic == ZYDIS_MNEMONIC_MINSD || d.in.mnemonic == ZYDIS_MNEMONIC_MAXSD ||
                         d.in.mnemonic == ZYDIS_MNEMONIC_COMISS || d.in.mnemonic == ZYDIS_MNEMONIC_UCOMISS ||
                         d.in.mnemonic == ZYDIS_MNEMONIC_CMP || (m && std::strncmp(m, "cmov", 4) == 0);
        if (!hit) {
            if (d.in.meta.category == ZYDIS_CATEGORY_RET || d.in.meta.category == ZYDIS_CATEGORY_CALL) break;
            continue;
        }
        // The compared value: an immediate, or a constant read RIP-relative (kept in the local file only).
        char value[64] = "reg";
        for (unsigned k = 0; k < d.in.operand_count_visible; ++k) {
            const auto& o = d.op[k];
            if (o.type == ZYDIS_OPERAND_TYPE_IMMEDIATE) std::snprintf(value, sizeof value, "imm %lld", (long long)o.imm.value.s);
            if (o.type == ZYDIS_OPERAND_TYPE_MEMORY && o.mem.base == ZYDIS_REGISTER_RIP) {
                const uint64_t t = Abs(d, o);
                if (t && t + 4 <= c.img.sizeOfImage) {
                    float f; std::memcpy(&f, c.img.base + t, 4);
                    std::snprintf(value, sizeof value, "const@0x%llX %g", (unsigned long long)t, f);
                }
            }
        }
        std::fprintf(c.clamps, "%s\t0x%X\t0x%llX\t0x%llX\t%s\t%d\t%s\n", r.name.c_str(), fn,
                     (unsigned long long)site.rva, (unsigned long long)d.rva, m ? m : "?", i, value);
        ++c.st.clampSites;
    }
}

void JumpTable(Ctx& c, uint32_t fn, uint64_t fnEnd, const Decoded* hist, int nh, const Decoded& jmp) {
    for (int k = nh - 1; k >= 0 && k >= nh - 8; --k) {
        const Decoded& d = hist[k];
        for (unsigned i = 0; i < d.in.operand_count_visible; ++i) {
            const auto& o = d.op[i];
            if (o.type != ZYDIS_OPERAND_TYPE_MEMORY || o.mem.scale != 4 || o.mem.base == ZYDIS_REGISTER_RIP ||
                !o.mem.disp.has_displacement)
                continue;
            const uint64_t table = uint64_t(o.mem.disp.value);
            if (table == 0 || table + 4 > c.img.sizeOfImage) continue;
            unsigned n = 0;
            for (; n < 2048 && table + 4 * (n + 1) <= c.img.sizeOfImage; ++n) {
                uint32_t e; std::memcpy(&e, c.img.base + table + 4 * n, 4);
                if (e < fn || e >= fnEnd) break;
            }
            if (n >= 2) {
                std::fprintf(c.tables, "0x%X\t0x%llX\t0x%llX\t%u\n", fn, (unsigned long long)jmp.rva,
                             (unsigned long long)table, n);
                ++c.st.jumpTables;
            }
            return;
        }
    }
}

// One function body; leaf: no unwind entry, so stop at the first ret or unconditional jump.
void Walk(Ctx& c, uint32_t fn, uint64_t end, bool leaf) {
    Decoded hist[8];
    int nh = 0;
    Decoded d;
    for (uint64_t p = fn; Decode(c, p, end, d); p += d.in.length) {
        ++c.st.instructions;
        const auto cat = d.in.meta.category;
        if ((cat == ZYDIS_CATEGORY_CALL || cat == ZYDIS_CATEGORY_UNCOND_BR) && d.in.operand_count_visible >= 1) {
            const auto& o = d.op[0];
            if (o.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && o.imm.is_relative) {
                const uint64_t t = Abs(d, o);
                const bool call = cat == ZYDIS_CATEGORY_CALL;
                const bool outside = t < fn || t >= end;
                if (c.img.InText(t) && (call || outside)) {
                    std::fprintf(c.edges, "0x%X\t0x%llX\t0x%llX\t%s\n", fn, (unsigned long long)d.rva,
                                 (unsigned long long)t, call ? "call" : "tail");
                    ++c.st.edges;
                    if (c.img.FuncIndex(t) < 0 && c.decodedLeaf.insert(uint32_t(t)).second)
                        c.leafQueue.push_back(uint32_t(t));
                }
            } else if (o.type == ZYDIS_OPERAND_TYPE_MEMORY && o.mem.base != ZYDIS_REGISTER_RIP &&
                       o.mem.index == ZYDIS_REGISTER_NONE && cat == ZYDIS_CATEGORY_CALL) {
                std::fprintf(c.vcalls, "0x%X\t0x%llX\t%lld\n", fn, (unsigned long long)d.rva,
                             (long long)(o.mem.disp.value / 8));
                ++c.st.vcalls;
            } else if (o.type == ZYDIS_OPERAND_TYPE_REGISTER && cat == ZYDIS_CATEGORY_UNCOND_BR) {
                JumpTable(c, fn, end, hist, nh, d);
            }
        }
        for (unsigned i = 0; i < d.in.operand_count_visible; ++i) {
            const auto& o = d.op[i];
            if (o.type != ZYDIS_OPERAND_TYPE_MEMORY || o.mem.base != ZYDIS_REGISTER_RIP) continue;
            const uint64_t t = Abs(d, o);
            const RefTarget* r = FindRef(c.refs, t);
            if (!r) continue;
            const char* acc = d.in.mnemonic == ZYDIS_MNEMONIC_LEA ? "addr"
                              : (o.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) ? "write" : "read";
            std::fprintf(c.xrefs, "0x%X\t0x%llX\t0x%llX\t%s\t%s\n", fn, (unsigned long long)d.rva,
                         (unsigned long long)t, acc, r->name.c_str());
            ++c.st.refs;
            if (r->setting && acc[0] == 'r') Clamp(c, fn, d, *r, end);
        }
        if (nh == 8) { std::memmove(hist, hist + 1, sizeof(Decoded) * 7); nh = 7; }
        hist[nh++] = d;
        if (leaf && (cat == ZYDIS_CATEGORY_RET || cat == ZYDIS_CATEGORY_UNCOND_BR)) break;
    }
}

}  // namespace

std::string ProbeFunction(const Image& img, uint32_t rva) {
    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    const long fi = img.FuncIndex(rva);
    const uint64_t end = fi >= 0 ? img.funcs[size_t(fi)].second : std::min<uint64_t>(uint64_t(rva) + 4096, img.textEnd);
    std::string out = "{\"cmd\":\"probe\",\"function\":\"0x" + [&] { char b[16]; std::snprintf(b, sizeof b, "%X", rva); return std::string(b); }() + "\",\"ops\":[";
    ZydisDecodedInstruction in;
    ZydisDecodedOperand op[ZYDIS_MAX_OPERAND_COUNT];
    bool first = true;
    for (uint64_t p = rva; p < end && ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, img.base + p, size_t(end - p), &in, op)); p += in.length) {
        for (unsigned i = 0; i < in.operand_count_visible; ++i) {
            const auto& o = op[i];
            const bool mem = o.type == ZYDIS_OPERAND_TYPE_MEMORY && o.mem.base == ZYDIS_REGISTER_RIP;
            const bool rel = o.type == ZYDIS_OPERAND_TYPE_IMMEDIATE && o.imm.is_relative && in.meta.category == ZYDIS_CATEGORY_CALL;
            if (!mem && !rel) continue;
            ZyanU64 t = 0;
            ZydisCalcAbsoluteAddress(&in, &o, p, &t);
            const char* acc = rel ? "call" : in.mnemonic == ZYDIS_MNEMONIC_LEA ? "addr" : (o.actions & ZYDIS_OPERAND_ACTION_MASK_WRITE) ? "write" : "read";
            char b[128];
            std::snprintf(b, sizeof b, "%s[\"0x%llX\",\"0x%llX\",\"%s\",\"%s\",%u]", first ? "" : ",", (unsigned long long)p,
                          (unsigned long long)t, acc, ZydisMnemonicGetString(in.mnemonic), unsigned(o.size / 8));
            out += b;
            first = false;
        }
    }
    return out + "]}";
}

CallGraphStats RunCallGraph(const Image& img, const std::vector<RefTarget>& refs, const std::string& dir) {
    const auto t0 = std::chrono::steady_clock::now();
    auto open = [&](const char* n) { return std::fopen((dir + "\\" + n).c_str(), "w"); };
    Ctx c{img, refs, open("cg_edges.tsv"), open("cg_vcalls.tsv"), open("cg_refs.tsv"), open("cg_clamps.tsv"),
          open("cg_jumptables.tsv"), {}, {}, {}, {}};
    if (!c.edges || !c.vcalls || !c.xrefs || !c.clamps || !c.tables) return c.st;
    std::fputs("caller\tsite\tcallee\tkind\n", c.edges);
    std::fputs("function\tsite\tslot\n", c.vcalls);
    std::fputs("function\tsite\ttarget\taccess\tname\n", c.xrefs);
    std::fputs("setting\tfunction\tread_site\tcompare_site\tmnemonic\tdistance\tvalue\n", c.clamps);
    std::fputs("function\tsite\ttable\tentries\n", c.tables);
    ZydisDecoderInit(&c.dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    for (const auto& f : img.funcs) { Walk(c, f.first, f.second, false); ++c.st.functions; }
    while (!c.leafQueue.empty()) {
        const uint32_t f = c.leafQueue.front();
        c.leafQueue.pop_front();
        Walk(c, f, std::min<uint64_t>(uint64_t(f) + 4096, img.textEnd), true);
        ++c.st.leafFunctions;
    }
    for (FILE* f : {c.edges, c.vcalls, c.xrefs, c.clamps, c.tables}) std::fclose(f);
    c.st.ms = double(std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - t0).count()) / 1000.0;
    return c.st;
}

}  // namespace raw::rt
