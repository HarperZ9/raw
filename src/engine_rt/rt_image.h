#pragma once
// The game's own image as it sits in memory (RAW engine runtime). Original work, MIT.
//
// Section bounds and the exception directory come from the loaded headers. By the time the proxy
// loads this module the game has decrypted its code in memory; nothing here touches the file on disk.

#include <windows.h>

#include <algorithm>
#include <cstdint>
#include <utility>
#include <vector>

namespace raw::rt {

struct Image {
    const uint8_t* base = nullptr;
    uint32_t sizeOfImage = 0;
    uint32_t textBegin = 0, textEnd = 0;
    std::vector<std::pair<uint32_t, uint32_t>> funcs;  // [begin, end) RVAs from .pdata, sorted

    bool Load() {
        base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
        sizeOfImage = nt->OptionalHeader.SizeOfImage;
        const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
        for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
            if ((s->Characteristics & IMAGE_SCN_MEM_EXECUTE) && textEnd == 0) {
                textBegin = s->VirtualAddress;
                textEnd = s->VirtualAddress + s->Misc.VirtualSize;
            }
        }
        const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXCEPTION];
        auto* rf = reinterpret_cast<const IMAGE_RUNTIME_FUNCTION_ENTRY*>(base + dir.VirtualAddress);
        const size_t n = dir.Size / sizeof(IMAGE_RUNTIME_FUNCTION_ENTRY);
        funcs.reserve(n);
        for (size_t i = 0; i < n; ++i)
            if (rf[i].BeginAddress < rf[i].EndAddress) funcs.emplace_back(rf[i].BeginAddress, rf[i].EndAddress);
        std::sort(funcs.begin(), funcs.end());
        return textEnd > textBegin && !funcs.empty();
    }

    bool InText(uint64_t rva) const { return rva >= textBegin && rva < textEnd; }

    // index of the .pdata function holding rva, or -1
    long FuncIndex(uint64_t rva) const {
        auto it = std::upper_bound(funcs.begin(), funcs.end(), std::make_pair(uint32_t(rva), UINT32_MAX));
        if (it == funcs.begin()) return -1;
        --it;
        return (rva >= it->first && rva < it->second) ? long(it - funcs.begin()) : -1;
    }
    uint32_t FuncStart(uint64_t rva) const {
        const long i = FuncIndex(rva);
        return i < 0 ? 0 : funcs[size_t(i)].first;
    }
};

}  // namespace raw::rt
