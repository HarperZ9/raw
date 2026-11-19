// Inline detours (RAW engine runtime). Original work, MIT. See rt_detour.h.
#include "rt_detour.h"

#include <windows.h>

#include <Zydis/Zydis.h>

#include <cstdio>
#include <cstring>

namespace raw::rt {
namespace {

// jmp [rip+0]; dq target
void WriteAbsJump(uint8_t* at, const void* target) {
    at[0] = 0xFF; at[1] = 0x25;
    std::memset(at + 2, 0, 4);
    const uint64_t t = uint64_t(target);
    std::memcpy(at + 6, &t, 8);
}

}  // namespace

std::string InstallDetour(const Image& img, uint32_t rva, void* hook, void** original) {
    char b[192];
    if (!img.InText(rva)) return "refused: not in the code section";
    uint8_t* fn = const_cast<uint8_t*>(img.base) + rva;
    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    size_t len = 0;
    while (len < 14) {
        ZydisDecodedInstruction in;
        ZydisDecodedOperand op[ZYDIS_MAX_OPERAND_COUNT];
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, fn + len, 32, &in, op))) return "refused: undecodable prologue";
        if (in.attributes & ZYDIS_ATTRIB_IS_RELATIVE) {
            std::snprintf(b, sizeof b, "refused: relative operand at +%zu in the prologue", len);
            return b;
        }
        if (in.meta.category == ZYDIS_CATEGORY_RET || in.meta.category == ZYDIS_CATEGORY_INTERRUPT) {
            std::snprintf(b, sizeof b, "refused: function shorter than a jump (ends at +%zu)", len + in.length);
            return b;
        }
        len += in.length;
    }
    auto* tramp = static_cast<uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!tramp) return "refused: no trampoline memory";
    std::memcpy(tramp, fn, len);
    WriteAbsJump(tramp + len, fn + len);
    FlushInstructionCache(GetCurrentProcess(), tramp, 64);
    *original = tramp;
    DWORD old;
    if (!VirtualProtect(fn, len, PAGE_EXECUTE_READWRITE, &old)) return "refused: VirtualProtect failed";
    uint8_t patch[32];
    std::memset(patch, 0xCC, sizeof patch);
    WriteAbsJump(patch, hook);
    // the entry is overwritten whole; a thread inside the first 14 bytes at this instant is the residual risk,
    // the same one every inline hook carries
    std::memcpy(fn, patch, len);
    VirtualProtect(fn, len, old, &old);
    FlushInstructionCache(GetCurrentProcess(), fn, len);
    std::snprintf(b, sizeof b, "ok: 0x%X, %zu bytes moved", rva, len);
    return b;
}

}  // namespace raw::rt
