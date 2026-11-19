// raw_skse.dll: RAW's SKSE plugin host, loaded by the proxy from [Modules] Load. Original work, MIT.
//
// d3d11_proxy.ini, [SKSE]:
//   Host=1              load SKSE plugins when SKSE itself is not in the process (default 1)
//   Plugins=a.dll;b.dll only these files from Data/SKSE/Plugins (default: every .dll there)
//   AllowLegacy=0       1: also load plugins that export only SKSEPlugin_Query (built before 1.6.629)
//   ReportVersion=2.2.6 the SKSE version plugins are told; some refuse to load below their minimum
//   OutDir=raw_skse_out the host's log and status file, beside the game
#include "skse_host.h"

#include <cstdarg>
#include <cstdio>

#include "ProxyAPI.h"

namespace raw::skse {

Host& H() {
    static Host h;
    return h;
}

void Log(const char* fmt, ...) {
    char buf[2048];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::lock_guard g(H().lock);
    FILE* f = std::fopen((H().outDir + "\\skse_host.log").c_str(), "a");
    if (!f) return;
    std::fprintf(f, "%02u:%02u:%02u.%03u [%lu] %s\n", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                 GetCurrentThreadId(), buf);
    std::fclose(f);
}

void Event(const char* fmt, ...) {
    char buf[512];
    va_list a;
    va_start(a, fmt);
    std::vsnprintf(buf, sizeof buf, fmt, a);
    va_end(a);
    Log("event: %s", buf);
    std::lock_guard g(H().lock);
    H().events.emplace_back(buf);
}

Plugin* ByHandle(PluginHandle h) {
    auto& ps = H().plugins;
    for (auto& p : ps)
        if (p.handle == h) return &p;
    return nullptr;
}

void Count(PluginHandle h, const char* fn) {
    std::lock_guard g(H().lock);
    if (Plugin* p = ByHandle(h)) ++p->calls[fn];
}

namespace {

PluginHandle GetPluginHandleImpl() { return H().loading; }

uint32_t GetReleaseIndexImpl() {
    Count(H().loading, "GetReleaseIndex");
    return 0;   // SKSE's own release counter has no meaning here; documented as 0
}

const void* GetPluginInfoImpl(const char* name) {
    Count(H().loading, "GetPluginInfo");
    if (!name) return nullptr;
    std::lock_guard g(H().lock);
    for (auto& p : H().plugins)
        if (p.status == "loaded" && _stricmp(p.name.c_str(), name) == 0) return &p.info;
    return nullptr;
}

SKSEInterface g_intf{};

uint32_t ParseVersion(const char* s) {
    unsigned a = 0, b = 0, c = 0;
    std::sscanf(s, "%u.%u.%u", &a, &b, &c);
    return PackVersion(a, b, c);
}

uint32_t ExeVersion() {
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    DWORD dummy = 0, n = GetFileVersionInfoSizeW(exe, &dummy);
    if (!n) return 0;
    std::vector<uint8_t> buf(n);
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!GetFileVersionInfoW(exe, 0, n, buf.data()) || !VerQueryValueW(buf.data(), L"\\", (void**)&fi, &len)) return 0;
    return PackVersion(HIWORD(fi->dwFileVersionMS), LOWORD(fi->dwFileVersionMS), HIWORD(fi->dwFileVersionLS),
                       LOWORD(fi->dwFileVersionLS));
}

void OnPresent(ID3D11DeviceContext*, IDXGISwapChain*) {
    EngineOnPresent();
    PapyrusPoll();
    RunTasks();
}

}  // namespace

const SKSEInterface* Interface() { return &g_intf; }

void* QueryInterfaceImpl(uint32_t id) {
    static const char* names[] = {"Invalid", "Scaleform", "Papyrus", "Serialization", "Task", "Messaging", "Object",
                                  "Trampoline"};
    Count(H().loading, (std::string("QueryInterface:") + (id < kIdTotal ? names[id] : "unknown")).c_str());
    switch (id) {
        case kIdScaleform: return Scaleform();
        case kIdPapyrus: return Papyrus();
        case kIdSerialization: return Serialization();
        case kIdTask: return Tasks();
        case kIdMessaging: return Messaging();
        case kIdTrampoline: return Trampolines();
        default: return nullptr;   // kIdObject: SKSE's delay-functor and object registry are not provided
    }
}

}  // namespace raw::skse

using namespace raw::skse;

extern "C" __declspec(dllexport) void RAW_ModuleStart(SB::Proxy::ProxyInterface* pi) {
    char p[MAX_PATH];
    GetModuleFileNameA(nullptr, p, MAX_PATH);
    std::string dir(p);
    dir = dir.substr(0, dir.rfind('\\') + 1);
    const std::string ini = dir + "d3d11_proxy.ini";
    char out[MAX_PATH] = {}, ver[32] = {};
    GetPrivateProfileStringA("SKSE", "OutDir", "raw_skse_out", out, sizeof out, ini.c_str());
    GetPrivateProfileStringA("SKSE", "ReportVersion", "2.2.6", ver, sizeof ver, ini.c_str());
    H().gameDir = dir;
    H().outDir = dir + out;
    CreateDirectoryA(H().outDir.c_str(), nullptr);
    H().runtimeVersion = ExeVersion();
    Log("raw_skse host, build %s %s; runtime %08X", __DATE__, __TIME__, H().runtimeVersion);

    if (!GetPrivateProfileIntA("SKSE", "Host", 1, ini.c_str())) { Event("host off ([SKSE] Host=0)"); WriteStatus(); return; }
    // With SKSE in the process it owns the plugins; loading them twice would double every hook.
    if (GetModuleHandleA("skse64_1_6_1170.dll") || GetModuleHandleA("skse64_steam_loader.dll")) {
        Event("SKSE is loaded in this process; the host defers to it and loads nothing");
        WriteStatus();
        return;
    }

    g_intf.skseVersion = ParseVersion(ver);
    g_intf.runtimeVersion = H().runtimeVersion;
    g_intf.editorVersion = 0;
    g_intf.isEditor = 0;
    g_intf.QueryInterface = QueryInterfaceImpl;
    g_intf.GetPluginHandle = GetPluginHandleImpl;
    g_intf.GetReleaseIndex = GetReleaseIndexImpl;
    g_intf.GetPluginInfo = GetPluginInfoImpl;

    AddrLibLoad(dir + "Data\\SKSE\\Plugins\\");
    EngineInit();
    if (pi && pi->RegisterPrePresent) pi->RegisterPrePresent(OnPresent);
    else Log("no proxy interface: tasks and timed messages will not run");
    LoadPlugins();
    WriteStatus();
}

extern "C" __declspec(dllexport) uint64_t RAW_SKSE_AddressOf(uint64_t id) {
    const uint64_t off = AddrLibOffset(id);
    return off ? reinterpret_cast<uint64_t>(GetModuleHandleW(nullptr)) + off : 0;
}

BOOL WINAPI DllMain(HINSTANCE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) DisableThreadLibraryCalls(h);
    return TRUE;
}
