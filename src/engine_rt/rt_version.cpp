// The game version and its address table (RAW engine runtime). Original work, MIT. See rt_version.h; the reader is
// the shared src/common/raw_versions.h.
#include "rt_version.h"

#include <algorithm>
#include <mutex>
#include <set>

#include "../common/raw_versions.h"

namespace raw::rt {
namespace {

raw::versions::Table g_table;
uint32_t g_packed = 0;
std::set<std::string> g_missing;
std::mutex g_missingM;

}  // namespace

std::string RunningExe::Version() const {
    char b[48];
    std::snprintf(b, sizeof b, "%u.%u.%u.%u", major, minor, build, sub);
    return b;
}

RunningExe ReadRunningExe(const Image&) {
    const auto e = raw::versions::ReadRunningExe();
    RunningExe r;
    r.major = e.major; r.minor = e.minor; r.build = e.build; r.sub = e.sub;
    r.timestamp = e.timestamp; r.sizeOfImage = e.sizeOfImage;
    return r;
}

bool LoadVersionTable(const Image&, const std::string& gameDir, std::string* why) {
    const auto exe = raw::versions::ReadRunningExe();
    if (!raw::versions::FindTable(gameDir + "raw_versions", exe, &g_table, why)) return false;
    g_packed = exe.Packed();
    return true;
}

const std::string& TableVersion() { return g_table.version; }
uint32_t RuntimeVersion() { return g_packed; }

uint32_t Addr(const char* name) {
    if (const uint32_t v = g_table.Get(name)) return v;
    std::lock_guard<std::mutex> g(g_missingM);
    g_missing.insert(name);
    return 0;
}
uint32_t Off(const char* name) { return Addr(name); }

std::vector<RefTarget> VersionRefs() {
    std::vector<RefTarget> out;
    for (const auto& [name, e] : g_table.entries) {
        if (e.kind != "setting" && e.kind != "global") continue;
        RefTarget r;
        r.rva = e.value; r.setting = e.kind == "setting"; r.span = r.setting ? 24 : 8; r.name = name;
        out.push_back(r);
    }
    std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.rva < b.rva; });
    return out;
}

std::string MissingEntries() {
    std::lock_guard<std::mutex> g(g_missingM);
    std::string s;
    for (const auto& m : g_missing) s += (s.empty() ? "" : ", ") + m;
    return s;
}

}  // namespace raw::rt
