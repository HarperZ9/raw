// rt_zydis_offsets.cpp: Zero-Hardcode Zydis x86-64 Member-Getter Offset Extractor.
// Original work, MIT.
#include "rt_zydis_offsets.h"

#include <Zydis/Zydis.h>

namespace raw::rt {
namespace {

bool IsReturnGpr(ZydisRegister r) {
    return r == ZYDIS_REGISTER_RAX || r == ZYDIS_REGISTER_EAX ||
           r == ZYDIS_REGISTER_AX  || r == ZYDIS_REGISTER_AL;
}

bool IsReturnXmm(ZydisRegister r) {
    return r == ZYDIS_REGISTER_XMM0;
}

bool IsLoadMnemonic(ZydisMnemonic m) {
    switch (m) {
    case ZYDIS_MNEMONIC_MOV:
    case ZYDIS_MNEMONIC_MOVZX:
    case ZYDIS_MNEMONIC_MOVSX:
    case ZYDIS_MNEMONIC_MOVSXD:
    case ZYDIS_MNEMONIC_MOVSS:
    case ZYDIS_MNEMONIC_MOVSD:
    case ZYDIS_MNEMONIC_VMOVSS:
    case ZYDIS_MNEMONIC_VMOVSD:
    case ZYDIS_MNEMONIC_LEA:
        return true;
    default:
        return false;
    }
}

}  // namespace

DecodedFieldOffset DecodeThisGetterOffset(const uint8_t* code, size_t maxBytes) {
    DecodedFieldOffset out{};
    if (!code || maxBytes < 2) return out;

    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    ZydisDecodedInstruction in;
    ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];

    size_t pos = 0;
    bool found = false;
    for (int step = 0; step < 16 && pos < maxBytes; ++step) {
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, code + pos, maxBytes - pos, &in, ops))) {
            return {};
        }
        pos += in.length;

        if (in.mnemonic == ZYDIS_MNEMONIC_RET) {
            out.valid = found;
            return out;
        }
        // Reject branches, calls, or writes to memory before return (not a pure getter).
        if (in.meta.category == ZYDIS_CATEGORY_CALL ||
            in.meta.category == ZYDIS_CATEGORY_COND_BR ||
            in.meta.category == ZYDIS_CATEGORY_UNCOND_BR ||
            in.mnemonic == ZYDIS_MNEMONIC_INT3) {
            return {};
        }
        if (in.mnemonic == ZYDIS_MNEMONIC_NOP) continue;

        if (IsLoadMnemonic(in.mnemonic) && in.operand_count_visible >= 2 &&
            ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
            ops[1].type == ZYDIS_OPERAND_TYPE_MEMORY &&
            ops[1].mem.base == ZYDIS_REGISTER_RCX &&
            ops[1].mem.index == ZYDIS_REGISTER_NONE) {
            const bool gpr = IsReturnGpr(ops[0].reg.value);
            const bool xmm = IsReturnXmm(ops[0].reg.value);
            if ((gpr || xmm) && (!ops[1].mem.disp.has_displacement || ops[1].mem.disp.value >= 0)) {
                const int64_t disp = ops[1].mem.disp.has_displacement ? ops[1].mem.disp.value : 0;
                if (disp >= 0 && disp <= 0xFFFF) {
                    out.offset = static_cast<uint32_t>(disp);
                    out.widthBytes = static_cast<uint8_t>(ops[1].size / 8);
                    out.isFloat = xmm;
                    out.isAddress = (in.mnemonic == ZYDIS_MNEMONIC_LEA);
                    found = true;
                    continue;
                }
            }
        }
        // Any other instruction overwriting RAX/XMM0 invalidates a prior read (e.g. xor eax, eax; ret).
        if (in.operand_count_visible >= 1 && ops[0].type == ZYDIS_OPERAND_TYPE_REGISTER &&
            (IsReturnGpr(ops[0].reg.value) || IsReturnXmm(ops[0].reg.value))) {
            found = false;
        }
    }
    return {};
}

uint32_t ScanVtableGetters(const Image& img, uint32_t vtableRva, uint32_t maxSlots,
                           std::vector<std::pair<uint32_t, DecodedFieldOffset>>* out) {
    if (!img.base || !vtableRva || !out) return 0;
    out->clear();
    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(img.base);
    uint32_t count = 0;
    for (uint32_t slot = 0; slot < maxSlots; ++slot) {
        const uint32_t entryRva = vtableRva + slot * 8u;
        if (entryRva + 8u > img.sizeOfImage) break;
        const uint64_t fnVa = *reinterpret_cast<const uint64_t*>(img.base + entryRva);
        if (fnVa < baseAddr + img.textBegin || fnVa >= baseAddr + img.textEnd) break;
        const uint64_t fnRva = fnVa - baseAddr;
        const size_t avail = static_cast<size_t>(std::min<uint64_t>(64ull, img.textEnd - fnRva));
        const DecodedFieldOffset d = DecodeThisGetterOffset(img.base + fnRva, avail);
        if (d.valid) {
            out->emplace_back(slot, d);
            ++count;
        }
    }
    return count;
}

}  // namespace raw::rt
