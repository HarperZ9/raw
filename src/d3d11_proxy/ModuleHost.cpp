// Engine modules behind the proxy: load, live reload, deferred free. See ModuleHost.h. Original work, MIT.
#include "ModuleHost.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"

#include <cstdio>
#include <string>
#include <vector>

namespace SB::Proxy::modules
{
namespace
{
struct Module { std::string logical, file; HMODULE h = nullptr; };
struct Retired { HMODULE h; std::string file; uint32_t atFrame; ULONGLONG atMs; };

std::vector<Module> s_mods;
std::vector<Retired> s_retired;
ProxyInterface* s_pi = nullptr;
uint32_t s_generation = 0, s_presents = 0;

std::string GameDir()
{
    char exe[MAX_PATH];
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    std::string dir(exe);
    return dir.substr(0, dir.rfind('\\') + 1);
}

// [base, base + SizeOfImage) from the module's own PE header
void Range(HMODULE h, const uint8_t** lo, const uint8_t** hi)
{
    const auto* base = reinterpret_cast<const uint8_t*>(h);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    *lo = base;
    *hi = base + nt->OptionalHeader.SizeOfImage;
}

// loads one file from the game folder and starts it; null with the reason logged when it fails
HMODULE Start(const std::string& file, std::string* why)
{
    HMODULE m = LoadLibraryA((GameDir() + file).c_str());
    if (!m) { *why = "LoadLibrary " + file + " failed (" + std::to_string(GetLastError()) + ")"; return nullptr; }
    using StartFn = void (*)(ProxyInterface*);
    auto start = reinterpret_cast<StartFn>(GetProcAddress(m, "RAW_ModuleStart"));
    Log("Module %s loaded%s (generation %u)", file.c_str(), start ? ", starting" : " (no RAW_ModuleStart)", s_generation);
    if (start) start(s_pi);
    return m;
}

void Answer(const std::string& text)
{
    FILE* f = nullptr;
    if (fopen_s(&f, (GameDir() + "raw_module_done.txt").c_str(), "w") == 0 && f) { fprintf(f, "%s\n", text.c_str()); fclose(f); }
    Log("[Modules] %s", text.c_str());
}

std::string Reload(const std::string& logical, const std::string& file)
{
    Module* mod = nullptr;
    for (auto& m : s_mods) if (_stricmp(m.logical.c_str(), logical.c_str()) == 0) mod = &m;
    if (!mod) return "refused: " + logical + " is not a module of this session ([Modules] Load)";
    if (file.empty() || file.find_first_of("\\/:") != std::string::npos) return "refused: file must be a bare name in the game folder";
    if (GetFileAttributesA((GameDir() + file).c_str()) == INVALID_FILE_ATTRIBUTES) return "refused: " + file + " is not in the game folder";
    LARGE_INTEGER f, t0, t1;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&t0);
    char why[512] = {};
    size_t dropped = 0;
    if (mod->h) {   // a copy that failed to start earlier has nothing to stop
        using StopFn = int (*)(char*, unsigned);
        auto stop = reinterpret_cast<StopFn>(GetProcAddress(mod->h, "RAW_ModuleStop"));
        if (!stop) return "refused: " + mod->file + " has no RAW_ModuleStop; it stays loaded";
        if (!stop(why, sizeof why)) return std::string("refused by ") + mod->file + ": " + why;
        const uint8_t *lo, *hi;
        Range(mod->h, &lo, &hi);
        dropped = PG_DropCallbacksIn(lo, hi);
        s_retired.push_back({mod->h, mod->file, s_presents, GetTickCount64()});
    }
    ++s_generation;
    if (s_pi && s_pi->version >= 3) s_pi->moduleGeneration = s_generation;
    std::string err;
    const std::string oldFile = mod->file;
    mod->h = Start(file, &err);
    mod->file = file;
    QueryPerformanceCounter(&t1);
    char b[768];
    std::snprintf(b, sizeof b, "%s %.1f ms: %s stopped (%s), %zu callbacks dropped, %s %s, generation %u",
                  mod->h ? "ok" : "failed", double(t1.QuadPart - t0.QuadPart) * 1000.0 / double(f.QuadPart), oldFile.c_str(),
                  why[0] ? why : "no detail", dropped, file.c_str(), mod->h ? "started" : err.c_str(), s_generation);
    return b;
}

void Poll()
{
    const std::string go = GameDir() + "raw_module_go.txt";
    FILE* f = nullptr;
    if (fopen_s(&f, go.c_str(), "r") != 0 || !f) return;
    std::string logical, file;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        std::string l(line);
        while (!l.empty() && (l.back() == '\n' || l.back() == '\r' || l.back() == ' ')) l.pop_back();
        const size_t eq = l.find('=');
        if (eq == std::string::npos) continue;
        if (l.substr(0, eq) == "reload") logical = l.substr(eq + 1);
        else if (l.substr(0, eq) == "file") file = l.substr(eq + 1);
    }
    fclose(f);
    DeleteFileA(go.c_str());
    Answer(Reload(logical, file));
}

// an old copy leaves the process once no hook can still be returning through it
void FreeRetired()
{
    for (size_t i = 0; i < s_retired.size();) {
        const Retired& r = s_retired[i];
        if (s_presents - r.atFrame >= 120 && GetTickCount64() - r.atMs >= 2000) {
            const BOOL ok = FreeLibrary(r.h);
            Log("[Modules] %s freed (%s)", r.file.c_str(), ok ? "ok" : "FreeLibrary failed");
            s_retired.erase(s_retired.begin() + long(i));
        } else {
            ++i;
        }
    }
}
} // namespace

void LoadAll(ProxyInterface* pi)
{
    s_pi = pi;
    char list[1024] = {};
    GetPrivateProfileStringA("Modules", "Load", "", list, sizeof(list), (GameDir() + "d3d11_proxy.ini").c_str());
    std::string all(list);
    for (size_t p = 0; p < all.size();) {
        size_t q = all.find(';', p);
        if (q == std::string::npos) q = all.size();
        const std::string name = all.substr(p, q - p);
        p = q + 1;
        if (name.empty()) continue;
        std::string why;
        HMODULE h = Start(name, &why);
        if (!h) { Log("Module %s: %s", name.c_str(), why.c_str()); continue; }
        s_mods.push_back({name, name, h});
    }
}

void OnPresent()
{
    ++s_presents;
    if (s_mods.empty()) return;
    if (s_presents % 15 == 0) Poll();
    if (!s_retired.empty()) FreeRetired();
}

HMODULE Find(const char* logicalName)
{
    for (auto& m : s_mods) if (_stricmp(m.logical.c_str(), logicalName) == 0) return m.h;
    return nullptr;
}

uint32_t Generation() { return s_generation; }

} // namespace SB::Proxy::modules
