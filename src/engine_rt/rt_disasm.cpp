// disasm: a listing of one function as the game holds it in memory (RAW engine runtime). Original work, MIT.
//
// For local analysis only. The listing goes to the run's output folder (E:/raw-capture/<run>/raw_engine_out), is read
// there and is never committed: the repository holds addresses, offsets and RAW's own code, never the game's.
#include "rt_disasm.h"

#include <cstdio>
#include <string>

#include <Zydis/Zydis.h>

namespace raw::rt {

std::string DisasmFunction(const Image& img, uint32_t rva, uint32_t maxBytes, const std::string& outDir) {
    const long fi = img.FuncIndex(rva);
    uint32_t end = rva + (maxBytes ? maxBytes : 0x400);
    if (fi >= 0 && !maxBytes) end = img.funcs[size_t(fi)].second;
    if (!img.InText(rva) || !img.InText(end - 1)) return "{\"cmd\":\"disasm\",\"error\":\"outside the code section\"}";
    char name[64];
    std::snprintf(name, sizeof name, "\\disasm_%X.txt", rva);
    FILE* f = std::fopen((outDir + name).c_str(), "w");
    if (!f) return "{\"cmd\":\"disasm\",\"error\":\"cannot write\"}";
    ZydisDisassembledInstruction in;
    uint32_t p = rva, n = 0;
    while (p < end) {
        const uint64_t ip = 0x140000000ull + p;   // listed at the image's preferred base, so RVAs read straight off
        if (!ZYAN_SUCCESS(ZydisDisassembleIntel(ZYDIS_MACHINE_MODE_LONG_64, ip, img.base + p, end - p, &in))) {
            std::fprintf(f, "%08X  (undecodable)\n", p);
            ++p;
            continue;
        }
        std::fprintf(f, "%08X  %s\n", p, in.text);
        p += in.info.length;
        ++n;
    }
    std::fclose(f);
    char b[160];
    std::snprintf(b, sizeof b, "{\"cmd\":\"disasm\",\"rva\":\"0x%X\",\"end\":\"0x%X\",\"instructions\":%u}", rva, end, n);
    return b;
}

}  // namespace raw::rt
