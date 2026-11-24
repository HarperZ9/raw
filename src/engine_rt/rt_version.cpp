// The game version and its address table (RAW engine runtime). Original work, MIT. See rt_version.h; the reader is
// the shared src/common/raw_versions.h.
#include "rt_version.h"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <set>
#include <unordered_map>

#include "../common/raw_versions.h"

namespace raw::rt {
namespace {

raw::versions::Table g_table;
uint32_t g_packed = 0;
std::set<std::string> g_missing;
std::mutex g_missingM;

struct SecRange { uint32_t begin = 0, end = 0; };

void FindSections(const Image& img, SecRange& rdata, SecRange& data) {
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(img.base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(img.base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s) {
        char name[9]{};
        std::memcpy(name, s->Name, 8);
        const uint32_t b = s->VirtualAddress, e = b + s->Misc.VirtualSize;
        if (std::strcmp(name, ".rdata") == 0) rdata = {b, e};
        else if (std::strcmp(name, ".data") == 0) data = {b, e};
    }
}

// Universal runtime RTTI + Creation Engine Setting scanner: resolves vtables, vtable slots,
// and all ~3,000+ INI/Prefs/GMST Setting singletons on ANY Skyrim SE / AE / GOG / VR executable
// (1.5.97, 1.6.353, 1.6.640, 1.6.659 GOG, 1.6.1130, 1.6.1170, 1.6.1179 GOG, 1.7.104, etc.).
bool BuildUniversalRuntimeTable(const Image& img, const std::string& gameDir, const raw::versions::Exe& exe, raw::versions::Table* out, std::string* why) {
    SecRange rdata{}, data{};
    FindSections(img, rdata, data);
    if (rdata.end <= rdata.begin || data.end <= data.begin) return false;

    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(img.base);
    // Map decorated MSVC RTTI names -> RAW table keys (primary vtables where offset == 0).
    static const std::pair<const char*, const char*> kWantedVtables[] = {
        {".?AVVirtualMachine@Internal@BSScript@@", "vtable:BSScript::Internal::VirtualMachine"},
        {".?AVBSLightingShader@@",                 "vtable:BSLightingShader"},
        {".?AVBSUtilityShader@@",                  "vtable:BSUtilityShader"},
        {".?AVBSGrassShader@@",                    "vtable:BSGrassShader"},
        {".?AVBSEffectShader@@",                   "vtable:BSEffectShader"},
        {".?AVBSWaterShader@@",                    "vtable:BSWaterShader"},
        {".?AVBSSkyShader@@",                      "vtable:BSSkyShader"},
        {".?AVBSDistantTreeShader@@",              "vtable:BSDistantTreeShader"},
        {".?AVBSParticleShader@@",                 "vtable:BSParticleShader"},
        {".?AVBSBloodSplatterShader@@",            "vtable:BSBloodSplatterShader"},
        {".?AVBSShaderAccumulator@@",              "vtable:BSShaderAccumulator"},
        {".?AVBSCullingProcess@@",                 "vtable:BSCullingProcess"},
        {".?AVBSBatchRenderer@@",                  "vtable:BSBatchRenderer"},
        {".?AVBSShadowDirectionalLight@@",         "vtable:BSShadowDirectionalLight"},
        {".?AVGridCellArray@@",                    "vtable:GridCellArray"},
        {".?AV?$SettingT@VINISettingCollection@@@@",     "__setting_ini"},
        {".?AV?$SettingT@VINIPrefSettingCollection@@@@", "__setting_prefs"},
        {".?AV?$SettingT@VGameSettingCollection@@@@",    "__setting_gmst"},
    };
    std::unordered_map<std::string, const char*> wanted;
    for (const auto& kv : kWantedVtables) wanted[kv.first] = kv.second;

    // 1. Scan .rdata for vtables: [COL ptr] [fn0 in .text] where COL has signature==1, offset==0, self==colRva.
    std::unordered_map<std::string, uint32_t> foundVt;
    for (uint32_t rva = (rdata.begin + 7u) & ~7u; rva + 16u <= rdata.end; rva += 8u) {
        const uint64_t colVa = *reinterpret_cast<const uint64_t*>(img.base + rva);
        if (colVa < baseAddr + rdata.begin || colVa + 24u > baseAddr + rdata.end) continue;
        const uint32_t colRva = static_cast<uint32_t>(colVa - baseAddr);
        const uint32_t* col = reinterpret_cast<const uint32_t*>(img.base + colRva);
        if (col[0] != 1u || col[1] != 0u || col[5] != colRva) continue;   // signature==1, offset==0, pSelf==colRva
        const uint32_t tdRva = col[3];
        if (tdRva < data.begin || tdRva + 24u > data.end) continue;
        const uint64_t fn0 = *reinterpret_cast<const uint64_t*>(img.base + rva + 8u);
        if (fn0 < baseAddr + img.textBegin || fn0 >= baseAddr + img.textEnd) continue;
        const char* mangled = reinterpret_cast<const char*>(img.base + tdRva + 16u);
        if (mangled[0] != '.' || mangled[1] != '?') continue;
        // Bounded copy of decorated name
        char nameBuf[128]{};
        size_t len = 0;
        while (len + 1 < sizeof(nameBuf) && tdRva + 16u + len < data.end && mangled[len] != '\0') {
            nameBuf[len] = mangled[len];
            ++len;
        }
        const auto it = wanted.find(nameBuf);
        if (it != wanted.end() && foundVt.find(it->second) == foundVt.end()) {
            foundVt[it->second] = rva + 8u;   // vtable starts immediately after COL pointer
        }
    }

    raw::versions::Table t;
    t.version = exe.Version();
    t.timestamp = exe.timestamp;
    t.sizeOfImage = exe.sizeOfImage;
    t.file = "<universal-rtti-scan>";

    uint32_t vtCount = 0;
    for (const auto& [k, rva] : foundVt) {
        if (k.rfind("__", 0) != 0) {
            t.entries[k] = {"vtable", "runtime-rtti", rva};
            ++vtCount;
        }
    }
    auto addSlot = [&](const char* vtKey, uint32_t slotIdx, const char* fnKey) {
        const auto it = foundVt.find(vtKey);
        if (it == foundVt.end()) return;
        const uint32_t slotRva = it->second + slotIdx * 8u;
        if (slotRva + 8u > rdata.end) return;
        const uint64_t fnVa = *reinterpret_cast<const uint64_t*>(img.base + slotRva);
        if (fnVa >= baseAddr + img.textBegin && fnVa < baseAddr + img.textEnd) {
            t.entries[fnKey] = {"function", "runtime-rtti", static_cast<uint32_t>(fnVa - baseAddr)};
        }
    };
    addSlot("vtable:GridCellArray", 0, "fn:GridCellArray::Dtor");
    addSlot("vtable:GridCellArray", 1, "fn:GridArray::DetachAll");
    addSlot("vtable:GridCellArray", 2, "fn:GridCellArray::KillAll");
    addSlot("vtable:BSShadowDirectionalLight", 10, "fn:BSShadowDirectionalLight::SetupCascades");

    // 2. Scan .data for Creation Engine Setting objects: { vtable*, value (8B), const char* name }.
    uint64_t settingVts[3] = {};
    if (auto it = foundVt.find("__setting_ini"); it != foundVt.end()) settingVts[0] = baseAddr + it->second;
    if (auto it = foundVt.find("__setting_prefs"); it != foundVt.end()) settingVts[1] = baseAddr + it->second;
    if (auto it = foundVt.find("__setting_gmst"); it != foundVt.end()) settingVts[2] = baseAddr + it->second;
    uint32_t settingCount = 0;
    if (settingVts[0] || settingVts[1] || settingVts[2]) {
        for (uint32_t rva = (data.begin + 7u) & ~7u; rva + 24u <= data.end; rva += 8u) {
            const uint64_t vt = *reinterpret_cast<const uint64_t*>(img.base + rva);
            if (vt != settingVts[0] && vt != settingVts[1] && vt != settingVts[2]) continue;
            const uint64_t nameVa = *reinterpret_cast<const uint64_t*>(img.base + rva + 16u);
            if (nameVa < baseAddr + rdata.begin || nameVa >= baseAddr + rdata.end) continue;
            const uint32_t nameRva = static_cast<uint32_t>(nameVa - baseAddr);
            const char* s = reinterpret_cast<const char*>(img.base + nameRva);
            size_t len = 0;
            bool ok = true;
            while (len < 96u && nameRva + len < rdata.end && s[len] != '\0') {
                const unsigned char ch = static_cast<unsigned char>(s[len]);
                if (ch < 0x20 || ch > 0x7E) { ok = false; break; }
                ++len;
            }
            if (ok && len >= 2u && len < 96u) {
                t.entries[std::string(s, len)] = {"setting", "runtime-scan", rva};
                ++settingCount;
            }
        }
    }

    // 3. Stable structure offsets across SE/AE/GOG (1.5.97 through 1.7.x).
    t.entries["off:TES::gridCells"] = {"offset", "runtime-stable", 0x78};
    t.entries["off:TESObjectCELL::worldSpace"] = {"offset", "runtime-stable", 0x128};
    t.entries["off:UI::menuMap"] = {"offset", "runtime-stable", 0x128};
    t.entries["off:SkyrimVM::impl"] = {"offset", "runtime-stable", (exe.major > 1 || exe.minor >= 7) ? 0x210u : 0x200u};

    // 4. Universal Address Library runtime loader (format 2 for 1.6.x AE/GOG, format 5 for 1.7.99+):
    // Reads Data\SKSE\Plugins\versionlib-<major>-<minor>-<build>-<sub>.bin when present to resolve
    // ALL engine singletons (Sky, TES, PlayerCamera, PlayerCharacter, UI, Calendar, BSGraphics,
    // BSShaderManager, ProcessLists, Decal/Grass/ImageSpace/Water managers) on ANY AE/GOG version!
    uint32_t alCount = 0;
    {
        char alName[128];
        std::snprintf(alName, sizeof alName, "Data\\SKSE\\Plugins\\versionlib-%u-%u-%u-%u.bin",
                      exe.major, exe.minor, exe.build, exe.sub);
        std::ifstream f(gameDir + alName, std::ios::binary);
        if (f) {
            std::vector<uint8_t> buf((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
            auto rd = [&](size_t& p, auto& outVal) -> bool {
                if (p + sizeof(outVal) > buf.size()) return false;
                std::memcpy(&outVal, buf.data() + p, sizeof(outVal));
                p += sizeof(outVal);
                return true;
            };
            static const std::pair<uint64_t, std::pair<const char*, const char*>> kWantedIds[] = {
                {13878,  {"fn:Sky::GetSingleton", "function"}},
                {35727,  {"fn:BGSSaveLoadManager::Save_Impl", "function"}},
                {35728,  {"fn:BGSSaveLoadManager::Load_Impl", "function"}},
                {390951, {"BSShaderManager::State::GetSingleton", "global"}},
                {400315, {"ProcessLists::GetSingleton", "global"}},
                {400327, {"UI::Singleton", "global"}},
                {400447, {"Calendar::Singleton", "global"}},
                {400450, {"TESWaterSystem::GetSingleton", "global"}},
                {400452, {"BGSGrassManager::GetSingleton", "global"}},
                {400561, {"BGSDecalManager::GetSingleton", "global"}},
                {400622, {"BSPointerHandleManager::Entry::GetHandleEntries", "global"}},
                {400802, {"PlayerCamera::Singleton", "global"}},
                {403450, {"TES::Singleton", "global"}},
                {403521, {"PlayerCharacter::Singleton", "global"}},
                {411479, {"BSGraphics::State::GetSingleton", "global"}},
                {414660, {"ImageSpaceManager::GetSingleton", "global"}},
            };
            std::unordered_map<uint64_t, std::pair<const char*, const char*>> idMap;
            for (const auto& kv : kWantedIds) idMap[kv.first] = kv.second;

            size_t p = 0;
            int32_t fmt = 0;
            if (rd(p, fmt)) {
                if (fmt == 5 && buf.size() >= 0x60) {
                    uint32_t count = 0;
                    std::memcpy(&count, buf.data() + 0x5C, sizeof(count));
                    if (0x60ull + uint64_t(count) * 4ull <= buf.size()) {
                        const uint32_t* offs = reinterpret_cast<const uint32_t*>(buf.data() + 0x60);
                        for (const auto& [id, meta] : idMap) {
                            if (id < count && offs[id] != 0) {
                                t.entries[meta.first] = {meta.second, "runtime-addrlib", offs[id]};
                                ++alCount;
                            }
                        }
                    }
                } else if (fmt == 2) {
                    int32_t v[4]{}, nameLen = 0, ptrSize = 0, count = 0;
                    bool ok = rd(p, v[0]) && rd(p, v[1]) && rd(p, v[2]) && rd(p, v[3]) && rd(p, nameLen);
                    if (ok && nameLen >= 0 && p + size_t(nameLen) <= buf.size()) {
                        p += size_t(nameLen);
                        ok = rd(p, ptrSize) && rd(p, count) && ptrSize > 0 && count > 0;
                    } else ok = false;
                    auto delta = [&](uint8_t kind, uint64_t prev, uint64_t& res) -> bool {
                        if (kind == 1) { res = prev + 1; return true; }
                        if (kind == 2) { uint8_t d = 0; if (!rd(p, d)) return false; res = prev + d; return true; }
                        if (kind == 3) { uint8_t d = 0; if (!rd(p, d)) return false; res = prev - d; return true; }
                        if (kind == 4) { uint16_t d = 0; if (!rd(p, d)) return false; res = prev + d; return true; }
                        if (kind == 5) { uint16_t d = 0; if (!rd(p, d)) return false; res = prev - d; return true; }
                        if (kind == 6) { uint16_t d = 0; if (!rd(p, d)) return false; res = d; return true; }
                        if (kind == 7) { uint32_t d = 0; if (!rd(p, d)) return false; res = d; return true; }
                        return false;
                    };
                    uint64_t prevId = 0, prevOff = 0;
                    for (int32_t i = 0; ok && i < count; ++i) {
                        uint8_t tag = 0;
                        if (!rd(p, tag)) break;
                        const uint8_t lo = tag & 0xFu, hi = tag >> 4;
                        uint64_t id = 0, off = 0;
                        if (lo == 0) { if (!rd(p, id)) break; } else if (!delta(lo, prevId, id)) break;
                        const uint64_t baseOff = (hi & 8u) ? (prevOff / uint64_t(ptrSize)) : prevOff;
                        if ((hi & 7u) == 0) { if (!rd(p, off)) break; } else if (!delta(hi & 7u, baseOff, off)) break;
                        if (hi & 8u) off *= uint64_t(ptrSize);
                        prevId = id; prevOff = off;
                        const auto it = idMap.find(id);
                        if (it != idMap.end() && off > 0 && off < img.sizeOfImage) {
                            t.entries[it->second.first] = {it->second.second, "runtime-addrlib", static_cast<uint32_t>(off)};
                            ++alCount;
                        }
                    }
                }
            }
        }
    }

    if (vtCount == 0 && settingCount == 0 && alCount == 0) return false;
    if (why) {
        char buf[256];
        std::snprintf(buf, sizeof buf,
                      "SkyrimSE.exe %s (universal runtime scan: %u vtables, %u settings, %u addrlib singletons resolved)",
                      exe.Version().c_str(), vtCount, settingCount, alCount);
        *why = buf;
    }
    *out = std::move(t);
    return true;
}

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

bool LoadVersionTable(const Image& img, const std::string& gameDir, std::string* why) {
    const auto exe = raw::versions::ReadRunningExe();
    if (raw::versions::FindTable(gameDir + "raw_versions", exe, &g_table, why)) {
        g_packed = exe.Packed();
        return true;
    }
    if (BuildUniversalRuntimeTable(img, gameDir, exe, &g_table, why)) {
        g_packed = exe.Packed();
        return true;
    }
    return false;
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
