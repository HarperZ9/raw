#pragma once
// rt_zydis_offsets.h: Zero-Hardcode Zydis x86-64 Member-Getter Offset Extractor.
// Disassembles virtual getters from RTTI-discovered vtables (Skyrim SE/AE/VR, Fallout 4/VR, Fallout 76,
// Starfield CE2) to extract exact [rcx + disp] scalar and pointer field offsets directly from host machine code.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "rt_image.h"

namespace raw::rt {

struct DecodedFieldOffset {
    uint32_t offset = 0;
    uint8_t  widthBytes = 0;
    bool     isFloat = false;
    bool     isAddress = false;   // true for lea rax, [rcx + disp]
    bool     valid = false;
};

// Decodes a trivial x64 MSVC thiscall getter (reads a field from [rcx + disp] into rax/eax/al/xmm0 and returns).
// Rejects functions with control-flow branches, calls, writes, or constant returns (planted-control safe).
DecodedFieldOffset DecodeThisGetterOffset(const uint8_t* code, size_t maxBytes);

// Scans the first maxSlots entries of an RTTI vtable in .rdata and extracts all trivial [rcx + disp] getters.
uint32_t ScanVtableGetters(const Image& img, uint32_t vtableRva, uint32_t maxSlots,
                           std::vector<std::pair<uint32_t, DecodedFieldOffset>>* out);

}  // namespace raw::rt
