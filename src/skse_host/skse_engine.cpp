// The game side of the SKSE host: addresses, the timed messages, and the save and load hooks. Original work, MIT.
//
// InputLoaded: the first present that finds BSInputDeviceManager made (at once when it already exists).
// DataLoaded:  the first present that finds the main menu made in the UI's menu map; the game builds that menu
//              after its data files are loaded. SKSE sends it a little earlier, before the menu is made.
// PreLoadGame, PostLoadGame: around BGSSaveLoadManager::Load_Impl, with the co-save's revert and load callbacks
//              between them. SaveGame: before BGSSaveLoadManager::Save_Impl, with the co-save written then.
// NewGame and DeleteGame are not sent yet (no hook).
#include <shlobj.h>

#include <cstring>

#include "rt_detour.h"
#include "raw_versions.h"
#include "rt_image.h"
#include "skse_addresses.h"
#include "skse_host.h"

namespace raw::skse {
namespace {

raw::rt::Image g_img;
uintptr_t g_base = 0;
bool g_inputSent = false, g_dataSent = false;

using GameFn = uint64_t (*)(void*, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);
GameFn g_save = nullptr, g_load = nullptr;

uint32_t g_rva[kAddrCount] = {};    // resolved for the running build; 0: unknown, feature off
uint32_t g_offv[kOffCount] = {};

uintptr_t Addr(int i) { return g_rva[i] ? g_base + g_rva[i] : 0; }

uintptr_t ReadPtr(uintptr_t at) {
    if (!at) return 0;
    __try { return *reinterpret_cast<uintptr_t*>(at); } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

void CopyCString(char* dst, size_t n, uintptr_t p) {
    __try {
        if (p) strncpy_s(dst, n, reinterpret_cast<const char*>(p), _TRUNCATE);
    } __except (EXCEPTION_EXECUTE_HANDLER) { dst[0] = 0; }
}

// A C string the game passed, copied out; empty when the pointer is not readable.
std::string SafeString(uintptr_t p) {
    char buf[520] = {};
    CopyCString(buf, sizeof buf, p);
    return buf;
}

int CompareName(uintptr_t key, const char* want) {
    __try { return key && strcmp(reinterpret_cast<const char*>(key), want) == 0; } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}

// UI::menuMap (BSTHashMap<BSFixedString, MenuEntry>, at UI+0x128): capacity +0x0C, entries +0x28. Each entry is
// key (BSFixedString), menu (GPtr<IMenu>), create function, next; an entry is empty when next is null.
bool MenuMade(const char* name) {
    const uintptr_t ui = ReadPtr(Addr(kAddrUI));
    if (!ui || !g_offv[kOffUiMenuMap]) return false;
    const uintptr_t map = ui + g_offv[kOffUiMenuMap];
    const uint32_t cap = uint32_t(ReadPtr(map + 0x08) >> 32);
    const uintptr_t entries = ReadPtr(map + 0x28);
    if (!entries || cap == 0 || cap > 4096) return false;
    for (uint32_t i = 0; i < cap; ++i) {
        const uintptr_t e = entries + uintptr_t(i) * 0x20;
        if (!ReadPtr(e + 0x18)) continue;
        if (CompareName(ReadPtr(e), name)) return ReadPtr(e + 0x08) != 0;
    }
    return false;
}

uint64_t SaveHook(void* self, uintptr_t device, uintptr_t stats, uintptr_t file, uintptr_t a5, uintptr_t a6) {
    const std::string name = SafeString(file);
    Log("Save_Impl(device %lld, stats %llu, '%s')", (long long)int32_t(device), (unsigned long long)uint32_t(stats), name.c_str());
    SendFromHost(kMsgSaveGame, const_cast<char*>(name.c_str()), uint32_t(name.size()));
    CosaveSave(name.c_str());
    return g_save(self, device, stats, file, a5, a6);
}

uint64_t LoadHook(void* self, uintptr_t file, uintptr_t device, uintptr_t stats, uintptr_t checkMods, uintptr_t a6) {
    const std::string name = SafeString(file);
    const ULONGLONG t0 = GetTickCount64();
    Log("Load_Impl('%s', device %lld)", name.c_str(), (long long)int32_t(device));
    SendFromHost(kMsgPreLoadGame, const_cast<char*>(name.c_str()), uint32_t(name.size()));
    const uint64_t r = g_load(self, file, device, stats, checkMods, a6);
    const bool ok = (r & 0xFF) != 0;
    Log("Load_Impl returned %d after %llu ms", ok ? 1 : 0, GetTickCount64() - t0);
    CosaveRevert();
    CosaveLoad(name.c_str());
    SendFromHost(kMsgPostLoadGame, reinterpret_cast<void*>(uintptr_t(ok)), 0);
    return r;
}

std::string IniString(const std::string& file, const char* section, const char* key) {
    char buf[MAX_PATH] = {};
    GetPrivateProfileStringA(section, key, "", buf, sizeof buf, file.c_str());
    return buf;
}

}  // namespace

std::string SaveDirectory() {
    PWSTR docs = nullptr;
    std::string root;
    if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &docs))) {
        char b[MAX_PATH] = {};
        WideCharToMultiByte(CP_UTF8, 0, docs, -1, b, MAX_PATH, nullptr, nullptr);
        root = std::string(b) + "\\My Games\\Skyrim Special Edition\\";
    }
    CoTaskMemFree(docs);
    std::string sub = IniString(root + "SkyrimCustom.ini", "General", "sLocalSavePath");
    if (sub.empty()) sub = IniString(root + "Skyrim.ini", "General", "sLocalSavePath");
    if (sub.empty()) sub = "Saves\\";
    if (sub.back() != '\\' && sub.back() != '/') sub += '\\';
    return root + sub;
}

void* VirtualMachine() {
    const uintptr_t vm = ReadPtr(Addr(kAddrSkyrimVM));
    if (!vm || !g_offv[kOffVmImpl]) return nullptr;
    const uintptr_t impl = ReadPtr(vm + g_offv[kOffVmImpl]);
    // An IVirtualMachine starts with its vtable, which lives in the game's image; anything else means the offset
    // is wrong for this build, and no plugin gets the pointer.
    const uintptr_t vtbl = ReadPtr(impl);
    static bool warned = false;
    if (impl && (vtbl < g_base || vtbl >= g_base + g_img.sizeOfImage)) {
        if (!warned) Event("VM check failed: impl %p has no vtable in the game image; Papyrus registrations held", (void*)impl);
        warned = true;
        return nullptr;
    }
    return reinterpret_cast<void*>(impl);
}

// Every address and offset comes from the version layer's table for the running exe (raw_versions/ beside the
// game); an address the table lacks comes from an installed Address Library database for the same build. Without a
// table that names this exe, nothing is hooked. Whatever stays unknown turns its feature off.
bool EngineInit() {
    if (!g_img.Load()) { Log("game image: no code section found"); return false; }
    g_base = reinterpret_cast<uintptr_t>(g_img.base);
    raw::versions::Table table;
    std::string why;
    const bool found = raw::versions::FindTable(H().gameDir + "raw_versions", raw::versions::ReadRunningExe(), &table, &why);
    Event("version layer: %s", why.c_str());
    if (!found) {
        Event("no game hooks and no timed messages on this exe; plugins still load");
        return false;
    }
    for (int i = 0; i < kAddrCount; ++i) {
        const auto& a = g_addr[i];
        const uint32_t fromTable = table.Get(a.name);
        const uint64_t db = AddrLibOffset(a.id);
        if (fromTable && db && db != fromTable)
            Log("address %s: table 0x%X, Address Library 0x%llX; the table is used", a.name, fromTable, db);
        g_rva[i] = fromTable ? fromTable : uint32_t(db);
        Log("address %s: %s", a.name, fromTable ? "from the table" : db ? "from the Address Library" : "unknown, its feature is off");
    }
    for (int i = 0; i < kOffCount; ++i) {
        g_offv[i] = table.Get(g_off[i].name);
        Log("offset %s: %s", g_off[i].name, g_offv[i] ? "from the table" : "unknown, its feature is off");
    }
    if (!g_offv[kOffUiMenuMap] || !g_rva[kAddrUI] || !g_rva[kAddrDataHandler]) Event("DataLoaded will not be sent on this build");
    if (!g_offv[kOffVmImpl] || !g_rva[kAddrSkyrimVM]) Event("Papyrus registrations will stay queued on this build");
    if (!g_rva[kAddrInputManager]) Event("InputLoaded will not be sent on this build");
    auto hook = [](int i, void* fn, void** orig, const char* what) {
        if (!g_rva[i]) { Event("%s hook: off (address unknown on this build)", what); return; }
        const std::string r = raw::rt::InstallDetour(g_img, g_rva[i], fn, orig);
        Event("%s hook: %s", what, r.c_str());
        if (r.rfind("ok", 0) != 0) *orig = nullptr;
    };
    hook(kAddrSaveImpl, reinterpret_cast<void*>(SaveHook), reinterpret_cast<void**>(&g_save), "save");
    hook(kAddrLoadImpl, reinterpret_cast<void*>(LoadHook), reinterpret_cast<void**>(&g_load), "load");
    return true;
}

void EngineOnPresent() {
    if (!g_base) return;
    if (!g_inputSent && ReadPtr(Addr(kAddrInputManager))) {
        g_inputSent = true;
        SendFromHost(kMsgInputLoaded, nullptr, 0);
    }
    if (!g_dataSent && ReadPtr(Addr(kAddrDataHandler)) && MenuMade("Main Menu")) {
        g_dataSent = true;
        SendFromHost(kMsgDataLoaded, nullptr, 0);
    }
}

}  // namespace raw::skse
