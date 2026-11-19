// Address Library database reader (versionlib-<version>.bin, formats 2 and 5). Original work, MIT.
// Format 2 (1.6.x): the record encoding follows CommonLibSSE-NG's IDDatabase reader (MIT, 2024), which documents it.
// Format 5 (1.7.x): a 96-byte header (format, version[4], name[64], pointer size, data format, offset count) and then
// one uint32 offset per id, 0 for an id the build lacks; layout as described by the maintained CommonLibSSE-NG
// (alandtse, GPL-3.0). Only the layout was taken; this reader is written fresh. The host
// needs no database to run: it carries its own table (skse_addresses.h) and reads an installed database only to
// check that table and to answer RAW_SKSE_AddressOf. The database file is never copied or shipped by RAW.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>

#include "skse_host.h"

namespace raw::skse {
namespace {

std::vector<std::pair<uint64_t, uint64_t>> g_ids;   // sorted by id

struct Reader {
    std::vector<uint8_t> d;
    size_t p = 0;
    bool ok = true;
    template <class T> T Get() {
        T v{};
        if (p + sizeof(T) > d.size()) { ok = false; return v; }
        memcpy(&v, &d[p], sizeof(T));
        p += sizeof(T);
        return v;
    }
};

uint64_t Delta(Reader& r, unsigned code, uint64_t prev) {
    switch (code) {
        case 0: return r.Get<uint64_t>();
        case 1: return prev + 1;
        case 2: return prev + r.Get<uint8_t>();
        case 3: return prev - r.Get<uint8_t>();
        case 4: return prev + r.Get<uint16_t>();
        case 5: return prev - r.Get<uint16_t>();
        case 6: return r.Get<uint16_t>();
        default: return r.Get<uint32_t>();
    }
}

}  // namespace

bool AddrLibLoad(const std::string& folder) {
    const uint32_t v = H().runtimeVersion;
    char name[96];
    std::snprintf(name, sizeof name, "versionlib-%u-%u-%u-%u.bin", v >> 24, (v >> 16) & 0xFF, (v >> 4) & 0xFFF, v & 0xF);
    std::ifstream f(folder + name, std::ios::binary);
    if (!f) { Log("Address Library: %s not installed; the built-in table is used unchecked", name); return false; }
    Reader r;
    r.d.assign(std::istreambuf_iterator<char>(f), {});
    const int32_t format = r.Get<int32_t>();
    if (format == 5) {
        for (int i = 0; i < 4; ++i) r.Get<uint32_t>();
        r.p += 64;
        r.Get<int32_t>();
        r.Get<int32_t>();
        const int32_t n = r.Get<int32_t>();
        if (!r.ok || n <= 0 || r.p + size_t(n) * 4 > r.d.size()) { Log("Address Library: %s (format 5) truncated", name); return false; }
        for (int32_t id = 0; id < n; ++id) {
            const uint32_t off = r.Get<uint32_t>();
            if (off) g_ids.emplace_back(uint64_t(id), uint64_t(off));
        }
        Log("Address Library: %s, format 5, %zu ids", name, g_ids.size());
        return true;
    }
    if (format != 2) { Log("Address Library: %s is format %d, not 2 or 5", name, format); return false; }
    for (int i = 0; i < 4; ++i) r.Get<int32_t>();
    r.p += size_t(r.Get<int32_t>());
    const int32_t ptrSize = r.Get<int32_t>(), count = r.Get<int32_t>();
    if (!r.ok || ptrSize <= 0 || count <= 0) { Log("Address Library: %s header unreadable", name); return false; }
    g_ids.reserve(size_t(count));
    uint64_t prevId = 0, prevOff = 0;
    for (int32_t i = 0; i < count && r.ok; ++i) {
        const uint8_t t = r.Get<uint8_t>();
        const unsigned lo = t & 0xF, hi = t >> 4;
        const uint64_t id = Delta(r, lo, prevId);
        const uint64_t base = (hi & 8) ? prevOff / uint64_t(ptrSize) : prevOff;
        uint64_t off = Delta(r, hi & 7, base);
        if (hi & 8) off *= uint64_t(ptrSize);
        g_ids.emplace_back(id, off);
        prevId = id;
        prevOff = off;
    }
    if (!r.ok) { Log("Address Library: %s truncated", name); g_ids.clear(); return false; }
    std::sort(g_ids.begin(), g_ids.end());
    Log("Address Library: %s, %zu ids", name, g_ids.size());
    return true;
}

uint64_t AddrLibOffset(uint64_t id) {
    auto it = std::lower_bound(g_ids.begin(), g_ids.end(), std::make_pair(id, uint64_t(0)));
    return it != g_ids.end() && it->first == id ? it->second : 0;
}

size_t AddrLibCount() { return g_ids.size(); }

}  // namespace raw::skse
