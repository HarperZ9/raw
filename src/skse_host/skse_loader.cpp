// The SKSE plugin host: finds, checks and loads plugins from Data/SKSE/Plugins. Original work, MIT.
//
// The order follows what SKSE documents for plugin authors: every plugin is checked first (its SKSEPlugin_Version
// data, or SKSEPlugin_Query for older plugins), then each accepted plugin's SKSEPlugin_Load runs, then PostLoad and
// PostPostLoad go to every listener. A plugin that faults inside its own exports is caught, named in the log and
// left unloaded; the game keeps running.
#include <cstdio>
#include <fstream>
#include <sstream>

#include "skse_host.h"

namespace raw::skse {
namespace {

std::string Ini(const char* key, const char* def) {
    char buf[2048] = {};
    GetPrivateProfileStringA("SKSE", key, def, buf, sizeof buf, (H().gameDir + "d3d11_proxy.ini").c_str());
    return buf;
}

std::vector<std::string> Allowed() {
    std::vector<std::string> out;
    std::stringstream ss(Ini("Plugins", ""));
    std::string s;
    while (std::getline(ss, s, ';'))
        if (!s.empty()) out.push_back(s);
    return out;
}

// SEH wrappers: no C++ objects with destructors inside __try.
int SafeQuery(QueryFn fn, const SKSEInterface* i, PluginInfo* info) {
    __try { return fn(i, info) ? 1 : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}
int SafeLoad(LoadFn fn, const SKSEInterface* i) {
    __try { return fn(i) ? 1 : 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return -1; }
}

std::string CheckVersionData(const PluginVersionData& v, uint32_t runtime, uint32_t skse) {
    if (v.dataVersion != 1) return "SKSEPlugin_Version data version " + std::to_string(v.dataVersion);
    const bool addrLib = v.flags2 & 1, sig = v.flags2 & 2, post629 = v.flags2 & 4, noStructs = v.flags1 & 1;
    bool listed = false;
    for (uint32_t c : v.compatibleVersions) {
        if (!c) break;
        if (c == runtime) listed = true;
    }
    if (!listed && !addrLib && !sig) return "not built for this runtime and not version independent";
    if (!listed && runtime >= PackVersion(1, 6, 629) && !post629 && !noStructs)
        return "built against game structures from before 1.6.629";
    if (v.xseMinimum > skse) {
        char b[96];
        std::snprintf(b, sizeof b, "needs SKSE %08X, host reports %08X ([SKSE] ReportVersion)", v.xseMinimum, skse);
        return b;
    }
    return "";
}

std::string Narrow(const std::wstring& w) {
    std::string s(WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, s.data(), int(s.size()), nullptr, nullptr);
    if (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

}  // namespace

void LoadPlugins() {
    Host& h = H();
    const std::wstring dir = std::wstring(h.gameDir.begin(), h.gameDir.end()) + L"Data\SKSE\Plugins\\";
    const auto allow = Allowed();
    const bool legacy = Ini("AllowLegacy", "0") == "1";
    WIN32_FIND_DATAW fd;
    HANDLE f = FindFirstFileW((dir + L"*.dll").c_str(), &fd);
    std::vector<std::string> files;
    if (f != INVALID_HANDLE_VALUE) {
        do files.push_back(Narrow(fd.cFileName)); while (FindNextFileW(f, &fd));
        FindClose(f);
    }
    Event("scan: %zu dll(s) in Data/SKSE/Plugins", files.size());

    // Pass 1: load the module and check what it declares.
    for (const auto& file : files) {
        Plugin p;
        p.file = file;
        p.handle = PluginHandle(h.plugins.size() + 1);   // 0 is the host itself, sender "SKSE"
        if (!allow.empty()) {
            bool ok = false;
            for (auto& a : allow) ok |= _stricmp(a.c_str(), file.c_str()) == 0;
            if (!ok) { p.status = "skipped: not in [SKSE] Plugins"; h.plugins.push_back(p); continue; }
        }
        const std::wstring path = dir + std::wstring(file.begin(), file.end());
        p.module = LoadLibraryW(path.c_str());
        if (!p.module) { p.status = "failed: LoadLibrary error " + std::to_string(GetLastError()); h.plugins.push_back(p); continue; }
        auto* vd = reinterpret_cast<const PluginVersionData*>(GetProcAddress(p.module, "SKSEPlugin_Version"));
        auto query = reinterpret_cast<QueryFn>(GetProcAddress(p.module, "SKSEPlugin_Query"));
        auto load = reinterpret_cast<LoadFn>(GetProcAddress(p.module, "SKSEPlugin_Load"));
        std::string why;
        if (!load) why = "no SKSEPlugin_Load export";
        else if (vd) {
            why = CheckVersionData(*vd, h.runtimeVersion, Interface()->skseVersion);
            p.name.assign(vd->pluginName, strnlen(vd->pluginName, sizeof vd->pluginName));
            p.version = vd->pluginVersion;
        } else if (!query) why = "neither SKSEPlugin_Version nor SKSEPlugin_Query";
        else if (!legacy) why = "only SKSEPlugin_Query (a pre-1.6.629 plugin); [SKSE] AllowLegacy=1 loads it";
        else {
            h.plugins.push_back(p);
            h.loading = p.handle;
            const int r = SafeQuery(query, Interface(), &h.plugins.back().info);
            h.loading = kInvalidHandle;
            Plugin& q = h.plugins.back();
            if (r == 1 && q.info.name) q.name = q.info.name, q.version = q.info.version;
            q.status = r == 1 ? "checked" : r == 0 ? "refused: SKSEPlugin_Query returned false" : "failed: fault in SKSEPlugin_Query";
            Log("%s: %s (legacy query)", file.c_str(), q.status.c_str());
            continue;
        }
        p.status = why.empty() ? "checked" : "refused: " + why;
        Log("%s: %s %s v%08X", file.c_str(), p.status.c_str(), p.name.c_str(), p.version);
        h.plugins.push_back(p);
    }
    for (auto& p : h.plugins) {
        if (p.name.empty()) p.name = p.file.substr(0, p.file.rfind('.'));
        if (p.info.name == nullptr) p.info = PluginInfo{1, p.name.c_str(), p.version};
    }

    // Pass 2: SKSEPlugin_Load, in folder order.
    for (auto& p : h.plugins) {
        if (p.status != "checked") {
            if (p.module && p.status.rfind("refused", 0) == 0) { FreeLibrary(p.module); p.module = nullptr; }
            continue;
        }
        auto load = reinterpret_cast<LoadFn>(GetProcAddress(p.module, "SKSEPlugin_Load"));
        h.loading = p.handle;
        const int r = SafeLoad(load, Interface());
        h.loading = kInvalidHandle;
        p.status = r == 1 ? "loaded" : r == 0 ? "refused: SKSEPlugin_Load returned false" : "failed: fault in SKSEPlugin_Load";
        Event("%s (%s): %s", p.file.c_str(), p.name.c_str(), p.status.c_str());
    }
    SendFromHost(kMsgPostLoad, nullptr, 0);
    SendFromHost(kMsgPostPostLoad, nullptr, 0);
}

void WriteStatus() {
    Host& h = H();
    std::lock_guard g(h.lock);
    std::ofstream o(h.outDir + "\skse_host.json");
    auto esc = [](const std::string& s) {
        std::string r;
        for (char c : s) { if (c == '"' || c == '\') r += '\'; r += c; }
        return r;
    };
    o << "{\"runtime\":\"" << std::hex << h.runtimeVersion << std::dec << "\",\"address_library_ids\":"
      << AddrLibCount() << ",\"plugins\":[";
    for (size_t i = 0; i < h.plugins.size(); ++i) {
        const Plugin& p = h.plugins[i];
        o << (i ? "," : "") << "{\"file\":\"" << esc(p.file) << "\",\"name\":\"" << esc(p.name) << "\",\"version\":"
          << p.version << ",\"handle\":" << p.handle << ",\"status\":\"" << esc(p.status) << "\",\"calls\":{";
        size_t j = 0;
        for (auto& [k, v] : p.calls) o << (j++ ? "," : "") << "\"" << esc(k) << "\":" << v;
        o << "}}";
    }
    o << "],\"events\":[";
    for (size_t i = 0; i < h.events.size(); ++i) o << (i ? "," : "") << "\"" << esc(h.events[i]) << "\"";
    o << "]}\n";
}

}  // namespace raw::skse
