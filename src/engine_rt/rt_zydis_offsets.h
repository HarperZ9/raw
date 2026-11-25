#pragma once
// rt_zydis_offsets.h: Zero-Hardcode Zydis x86-64 Member-Getter Offset Extractor & Fail-Closed Startup Verifier.
// Disassembles virtual getters and known engine functions from the live executable to verify exact [reg + disp]
// scalar and pointer field offsets directly against host machine code before allowing any struct read/write.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
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

struct OffsetVerifyRule {
    const char* name = "";
    uint32_t    fnRva = 0;
    uint32_t    expectedOffset = 0;
    uint8_t     expectedWidth = 0;   // 0 = any width (1/2/4/8)
    bool        requireFloat = false;
};

struct OffsetVerifyResult {
    std::string name;
    uint32_t    fnRva = 0;
    uint32_t    expectedOffset = 0;
    uint32_t    matchedOffset = 0;
    bool        fnReadable = false;
    bool        matched = false;
};

struct StartupOffsetVerification {
    uint32_t checked = 0;
    uint32_t passed = 0;
    uint32_t failed = 0;
    bool     failClosed = false;     // true if any active rule failed -> engine writes MUST remain disabled
    std::vector<OffsetVerifyResult> results;
};

// Decodes a trivial x64 MSVC thiscall getter (reads a field from [rcx + disp] into rax/eax/al/xmm0 and returns).
// Rejects functions with control-flow branches, calls, writes, or constant returns (planted-control safe).
DecodedFieldOffset DecodeThisGetterOffset(const uint8_t* code, size_t maxBytes);

// Scans the first maxSlots entries of an RTTI vtable in .rdata and extracts all trivial [rcx + disp] getters.
uint32_t ScanVtableGetters(const Image& img, uint32_t vtableRva, uint32_t maxSlots,
                           std::vector<std::pair<uint32_t, DecodedFieldOffset>>* out);

// Disassembles up to maxInsns instructions of a function buffer and confirms that a non-stack, non-RIP memory
// operand accesses [baseReg + expectedOffset] (with optional width/XMM check). Used for fail-closed startup gating.
bool VerifyCodeReferencesOffset(const uint8_t* code, size_t maxBytes, uint32_t expectedOffset,
                                uint8_t expectedWidth = 0, bool requireFloat = false,
                                uint32_t maxInsns = 64u);

// Verifies a table of critical struct offsets directly against the live executable image under SEH protection.
// Sets failClosed=true if any rule with a non-zero fnRva fails to match the host machine code.
StartupOffsetVerification VerifyCriticalOffsets(const Image& img, const OffsetVerifyRule* rules, size_t ruleCount);

}  // namespace raw::rt
