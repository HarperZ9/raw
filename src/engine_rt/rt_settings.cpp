// Engine settings by name and the shadow atlas size at startup (RAW engine runtime). Original work, MIT.
#include <windows.h>

#include <cstdio>
#include <cstring>
#include <string>

#include "rt_control.h"
#include "rt_version.h"

namespace raw::rt {

bool SetSetting(const Image& img, const std::vector<RefTarget>& refs, const std::string& name, const std::string& value,
                std::string* before) {
    const RefTarget* r = nullptr;
    for (const auto& x : refs)
        if (x.setting && x.name == name) { r = &x; break; }
    if (!r || name.empty()) return false;
    auto* field = const_cast<uint8_t*>(img.base) + r->rva + 8;
    char old[64] = "";
    const char t = name[0];
    if (t == 'f') {
        float o; std::memcpy(&o, field, 4); std::snprintf(old, sizeof old, "%g", o);
        const float v = std::strtof(value.c_str(), nullptr); std::memcpy(field, &v, 4);
    } else if (t == 'i') {
        int32_t o; std::memcpy(&o, field, 4); std::snprintf(old, sizeof old, "%d", o);
        const int32_t v = std::atoi(value.c_str()); std::memcpy(field, &v, 4);
    } else if (t == 'u') {
        uint32_t o; std::memcpy(&o, field, 4); std::snprintf(old, sizeof old, "%u", o);
        const uint32_t v = uint32_t(std::strtoul(value.c_str(), nullptr, 10)); std::memcpy(field, &v, 4);
    } else if (t == 'b') {
        std::snprintf(old, sizeof old, "%d", int(field[0]));
        field[0] = (value == "1" || value == "true") ? 1 : 0;
    } else {
        return false;  // strings and colours are not written here
    }
    if (before) *before = old;
    return true;
}

size_t ApplyStartupSettings(const Image& img, const std::vector<RefTarget>& refs, const std::string& ini,
                            const std::string& logPath) {
    char buf[16384] = {};
    const DWORD n = GetPrivateProfileSectionA("EngineSet", buf, sizeof buf, ini.c_str());
    size_t applied = 0;
    FILE* log = std::fopen(logPath.c_str(), "a");
    for (const char* p = buf; p < buf + n && *p; p += std::strlen(p) + 1) {
        std::string kv(p);
        const size_t eq = kv.rfind('=');
        if (eq == std::string::npos) continue;
        std::string before;
        const bool ok = SetSetting(img, refs, kv.substr(0, eq), kv.substr(eq + 1), &before);
        applied += ok;
        if (log) std::fprintf(log, "startup set %s (was %s): %s\n", kv.c_str(), before.c_str(), ok ? "ok" : "not found");
    }
    if (log) std::fclose(log);
    return applied;
}

std::string ResizeShadowAtlas(const Image& img, const std::vector<RefTarget>& refs, uint32_t size) {
    // sub_14F00D0 copies iShadowMapResolution into a global at startup, before this module loads, and the
    // renderer sizes the shadow atlas from that global when it creates its targets (after this module
    // starts). Write both, the global only when it still holds the setting's value.
    const uint32_t kShadowSizeGlobal = Addr("global:ShadowMapSize");  // the renderer's copy of iShadowMapResolution
    if (!kShadowSizeGlobal) return "shadow atlas: no global:ShadowMapSize for this version; left alone";
    auto* setting = [&]() -> uint32_t* {
        for (const auto& x : refs)
            if (x.name == "iShadowMapResolution:Display") return reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(img.base) + x.rva + 8);
        return nullptr;
    }();
    auto* global = reinterpret_cast<uint32_t*>(const_cast<uint8_t*>(img.base) + kShadowSizeGlobal);
    char out[192];
    if (!setting) return "shadow atlas: iShadowMapResolution not in the refs";
    const uint32_t s0 = *setting, g0 = *global;
    if (g0 != s0) {
        std::snprintf(out, sizeof out, "shadow atlas: global 0x%X holds %u, setting %u; left alone", kShadowSizeGlobal, g0, s0);
        return out;
    }
    *setting = size;
    *global = size;
    std::snprintf(out, sizeof out, "shadow atlas: setting and global 0x%X %u -> %u", kShadowSizeGlobal, s0, size);
    return out;
}

}  // namespace raw::rt
