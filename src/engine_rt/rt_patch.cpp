// The patch ledger (RAW engine runtime). Original work, MIT. See rt_patch.h.
#include "rt_patch.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

namespace raw::rt {
namespace {

struct Entry { uint8_t* at; std::vector<uint8_t> original; };

std::mutex g_mutex;
std::vector<Entry> g_entries;
std::atomic<int> g_threads{0};

bool Known(const uint8_t* at) {
    for (const auto& e : g_entries)
        if (e.at == at) return true;
    return false;
}

bool Write(uint8_t* at, const void* bytes, size_t n) {
    DWORD old;
    if (!VirtualProtect(at, n, PAGE_EXECUTE_READWRITE, &old)) return false;
    if (n == 8 && (uintptr_t(at) & 7) == 0) {
        LONG64 v;
        std::memcpy(&v, bytes, 8);
        InterlockedExchange64(reinterpret_cast<volatile LONG64*>(at), v);
    } else {
        std::memcpy(at, bytes, n);
    }
    VirtualProtect(at, n, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, n);
    return true;
}

}  // namespace

bool PatchWrite(void* at, const void* bytes, size_t n) {
    auto* p = static_cast<uint8_t*>(at);
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!Known(p)) g_entries.push_back({p, std::vector<uint8_t>(p, p + n)});
    return Write(p, bytes, n);
}

bool PatchPointer(void* entry, const void* value, void** old) {
    if (old) std::memcpy(old, entry, 8);
    return PatchWrite(entry, &value, 8);
}

size_t PatchRestoreAll(std::string* report) {
    std::lock_guard<std::mutex> lock(g_mutex);
    size_t n = 0;
    for (auto it = g_entries.rbegin(); it != g_entries.rend(); ++it) {
        if (Write(it->at, it->original.data(), it->original.size())) {
            ++n;
        } else if (report) {
            char b[96];
            std::snprintf(b, sizeof b, "restore failed at %p (%zu bytes); ", static_cast<void*>(it->at), it->original.size());
            *report += b;
        }
    }
    g_entries.clear();
    return n;
}

size_t PatchCount() {
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_entries.size();
}

void ThreadBegin() { ++g_threads; }
void ThreadEnd() { --g_threads; }
int ThreadsRunning() { return g_threads.load(); }

}  // namespace raw::rt
