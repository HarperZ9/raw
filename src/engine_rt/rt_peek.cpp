// peek: game data along a pointer path (RAW engine runtime). Original work, MIT.
#include "rt_peek.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <sstream>

namespace raw::rt {

// peek <global name or 0xRVA> <off>... <bytes>: from the global's address, each offset dereferences the pointer
// held there and adds itself; the last number is how many bytes to show as 64-bit words. Game data only, read
// under structured exception handling.
bool ReadU64(const void* p, uint64_t* v) {
    __try { *v = *static_cast<const volatile uint64_t*>(p); return true; } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
std::string Peek(const Image& img, const std::vector<RefTarget>& refs, const std::string& name, const std::string& rest) {
    uint64_t addr = 0;
    if (name.rfind("0x", 0) == 0) addr = uint64_t(img.base) + std::strtoull(name.c_str(), nullptr, 16);
    else if (const RefTarget* r = [&]() -> const RefTarget* { for (const auto& x : refs) if (x.name == name) return &x; return nullptr; }()) addr = uint64_t(img.base) + r->rva;
    else return "{\"cmd\":\"peek\",\"error\":\"no such global\"}";
    std::vector<uint64_t> nums;
    std::istringstream in(rest);
    for (std::string t; in >> t;) nums.push_back(std::strtoull(t.c_str(), nullptr, 0));
    if (nums.empty()) return "{\"cmd\":\"peek\",\"error\":\"no byte count\"}";
    const size_t bytes = std::min<uint64_t>(nums.back(), 512);
    nums.pop_back();
    for (uint64_t off : nums) {
        uint64_t v = 0;
        if (!ReadU64(reinterpret_cast<void*>(addr), &v)) return "{\"cmd\":\"peek\",\"error\":\"unreadable on the way\"}";
        addr = v + off;
    }
    std::string out = "{\"cmd\":\"peek\",\"name\":\"" + name + "\",\"path\":\"" + rest + "\",\"words\":[";
    char b[32];
    for (size_t i = 0; i < bytes; i += 8) {
        uint64_t v = 0;
        if (!ReadU64(reinterpret_cast<void*>(addr + i), &v)) { out += i ? ",null" : "null"; continue; }
        std::snprintf(b, sizeof b, "%s\"0x%llX\"", i ? "," : "", (unsigned long long)v);
        out += b;
    }
    return out + "]}";
}

}  // namespace raw::rt
