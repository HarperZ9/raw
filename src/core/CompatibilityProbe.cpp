#include "CompatibilityProbe.h"
#include <Windows.h>
#include <Psapi.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <algorithm>
#include <cctype>

#pragma comment(lib, "psapi.lib")

namespace SB
{

// Known module signatures — DLLs we recognize and their descriptions
struct KnownModule {
    const char* pattern;     // case-insensitive substring match
    const char* description;
    bool        isOverlay;   // potential rendering conflict
};

static constexpr KnownModule kKnownModules[] = {
    // Graphics injectors
    { "d3d11.dll",           "D3D11 wrapper (ENB/SB/DXVK)",   true  },
    { "dxgi.dll",            "DXGI wrapper",                   true  },
    { "reshade",             "ReShade post-processing",        true  },
    { "specialk",            "Special K Swiss Army Knife",     true  },
    { "dxvk",                "DXVK Vulkan translation",        true  },

    // Overlays
    { "gameoverlay",         "Steam Overlay",                  true  },
    { "discord",             "Discord Overlay",                true  },
    { "rtss",                "MSI Afterburner RTSS",           true  },
    { "nvinject",            "NVIDIA Overlay",                 true  },
    { "fraps",               "Fraps recording",                true  },

    // SKSE plugins (known)
    { "communityshaders",    "Community Shaders",              false },
    { "enbparmlink",         "ENB ParmLink",                   false },
    { "skyrimbridge",        "Playground (self)",              false },
    { "nativeeditoridfix",   "Native EditorID Fix",            false },
    { "po3_tweaks",          "powerofthree Tweaks",            false },
    { "sse_engine_fixes",    "SSE Engine Fixes",               false },
    { "betterthirdperson",   "SmoothCam / Better 3P",         false },
    { "address_library",     "Address Library",                false },

    // ENB
    { "enbhost",             "ENBSeries Host",                 true  },
    { "enbseries",           "ENBSeries core",                 true  },

    // Shader tools
    { "d3dcompiler_47",      "D3DCompiler (system/proxy)",     false },
    { "d3dcompiler_46e",     "D3DCompiler Boris custom",       false },
    { "d3dcompiler_43",      "D3DCompiler legacy",             false },
};

void CompatibilityProbe::RunProbe()
{
    m_conflicts.clear();
    m_modules.clear();

    ProbeLoadedModules();
    ProbeOverlays();
    ProbeENBVersion();
    ProbeHookChain();
    ProbeSRVSlots();
    ProbeSKSEPlugins();
}

void CompatibilityProbe::ProbeLoadedModules()
{
    HMODULE hMods[512];
    DWORD cbNeeded = 0;
    HANDLE hProc = GetCurrentProcess();

    if (!EnumProcessModules(hProc, hMods, sizeof(hMods), &cbNeeded))
        return;

    uint32_t modCount = cbNeeded / sizeof(HMODULE);
    for (uint32_t i = 0; i < modCount; ++i) {
        char modName[MAX_PATH] = {};
        char modPath[MAX_PATH] = {};

        GetModuleBaseNameA(hProc, hMods[i], modName, MAX_PATH);
        GetModuleFileNameExA(hProc, hMods[i], modPath, MAX_PATH);

        // Compute module size
        MODULEINFO mi = {};
        GetModuleInformation(hProc, hMods[i], &mi, sizeof(mi));
        uint32_t sizeKB = static_cast<uint32_t>(mi.SizeOfImage / 1024);

        // Match against known signatures
        char lowerName[MAX_PATH];
        strncpy(lowerName, modName, MAX_PATH - 1);
        lowerName[MAX_PATH - 1] = '\0';
        for (char* p = lowerName; *p; ++p)
            *p = static_cast<char>(tolower(static_cast<unsigned char>(*p)));

        bool known = false;
        const char* desc = "";
        for (const auto& km : kKnownModules) {
            if (strstr(lowerName, km.pattern)) {
                known = true;
                desc = km.description;

                // If it's an overlay and not us, flag potential conflict
                if (km.isOverlay) {
                    // Skip self-detection for our own DLLs
                    if (strstr(lowerName, "skyrimbridge") ||
                        (strstr(lowerName, "d3d11.dll") && sizeKB < 300)) {
                        // Our proxy — not a conflict
                    } else if (strstr(lowerName, "reshade")) {
                        AddConflict("ReShade", "D3D11 pipeline",
                            ConflictSeverity::Warning,
                            "ReShade detected (%s, %u KB) — may intercept SRV/RT bindings",
                            modName, sizeKB);
                    } else if (strstr(lowerName, "specialk")) {
                        AddConflict("Special K", "D3D11 pipeline",
                            ConflictSeverity::Warning,
                            "Special K detected (%s) — shared swap chain hooks",
                            modName);
                    } else if (strstr(lowerName, "dxvk")) {
                        AddConflict("DXVK", "D3D11 API",
                            ConflictSeverity::Error,
                            "DXVK translates D3D11 to Vulkan — compute shaders may not work correctly");
                    }
                }
                break;
            }
        }

        AddModule(modName, modPath, sizeKB, known, desc);
    }
}

void CompatibilityProbe::ProbeOverlays()
{
    // Check for common overlay DLLs by name
    const struct { const char* dll; const char* name; } overlays[] = {
        { "GameOverlayRenderer64.dll", "Steam Overlay" },
        { "DiscordHook64.dll",         "Discord Overlay" },
        { "RTSSHooks64.dll",           "RTSS/MSI Afterburner" },
        { "nvinject.dll",              "NVIDIA GeForce Experience" },
    };

    for (const auto& ov : overlays) {
        if (GetModuleHandleA(ov.dll)) {
            AddConflict(ov.name, "Present hook chain",
                ConflictSeverity::Info,
                "%s loaded — rendering overlay active (usually harmless)", ov.name);
        }
    }
}

void CompatibilityProbe::ProbeENBVersion()
{
    HMODULE enbMod = nullptr;

    // ENB's d3d11.dll or enbseries.dll
    // Check for ENBGetSDKVersion export
    auto checkENB = [&](const char* dll) -> bool {
        HMODULE h = GetModuleHandleA(dll);
        if (!h) return false;
        auto fn = (long(*)())GetProcAddress(h, "ENBGetSDKVersion");
        if (!fn) return false;
        enbMod = h;
        long ver = fn();
        if (ver < 1000 || ver > 1100) {
            AddConflict("ENBSeries", "SDK version",
                ConflictSeverity::Warning,
                "ENB SDK version %ld — expected 1001-1002 (v504). Some APIs may differ.", ver);
        } else {
            AddConflict("ENBSeries", "SDK",
                ConflictSeverity::Info,
                "ENB SDK v%ld detected — compatible", ver);
        }
        return true;
    };

    // Try known ENB module names
    if (!checkENB("d3d11.dll"))
        checkENB("d3d11_original.dll");
}

void CompatibilityProbe::ProbeHookChain()
{
    // Verify our D3DCompile IAT hook is still in place
    HMODULE d3dc = GetModuleHandleA("d3dcompiler_47.dll");
    if (!d3dc) {
        AddConflict("D3DCompiler", "ShaderCache/ShaderDebug",
            ConflictSeverity::Info,
            "d3dcompiler_47.dll not loaded — IAT hooks may use proxy or ENB's internal compiler");
    }

    // Check if our proxy d3d11.dll is loaded
    HMODULE d3d11 = GetModuleHandleA("d3d11.dll");
    if (d3d11) {
        auto pgFn = GetProcAddress(d3d11, "PG_GetProxyInterface");
        auto sbFn = GetProcAddress(d3d11, "SB_GetProxyInterface");
        if (pgFn || sbFn) {
            AddConflict("SB Proxy", "D3D11",
                ConflictSeverity::Info,
                "Playground d3d11 proxy active (PG=%s, SB=%s)",
                pgFn ? "yes" : "no", sbFn ? "yes" : "no");
        } else {
            // d3d11.dll exists but isn't ours — could be ENB or DXVK
            auto enbFn = GetProcAddress(d3d11, "ENBGetSDKVersion");
            if (enbFn) {
                AddConflict("ENB d3d11.dll", "D3D11 pipeline",
                    ConflictSeverity::Info,
                    "ENB's d3d11.dll is the active wrapper (SB proxy not loaded)");
            }
        }
    }
}

void CompatibilityProbe::ProbeSRVSlots()
{
    // This is a design-time check — we document our slot usage.
    // Runtime collision detection would require hooking PSSetShaderResources
    // and checking if other systems write to our slots.
    // For now, report our claimed slots.
    AddConflict("SRV Allocation", "Shader Resources",
        ConflictSeverity::Info,
        "SB claims t17-t26: t17=Histogram, t18=LUT, t19=HiZ, t20=GTAO, t21=SSR, t22=TAA, t26=SSGI");
}

void CompatibilityProbe::ProbeSKSEPlugins()
{
    // Check for known SKSE plugins that touch similar systems
    struct SKSECheck {
        const char* dll;
        const char* name;
        const char* overlap;
        ConflictSeverity sev;
    };

    const SKSECheck checks[] = {
        { "CommunityShaders.dll", "Community Shaders", "Lighting/Materials/Shadows",
          ConflictSeverity::Warning },
        { "ENBHelperSE.dll",      "ENB Helper SE (3rd party)", "ENB time/weather data",
          ConflictSeverity::Info },
        { "enbparmlink.dll",      "ENB ParmLink", "Weather-reactive parameters",
          ConflictSeverity::Info },
    };

    for (const auto& c : checks) {
        // Check both the DLL name and common variations
        if (GetModuleHandleA(c.dll)) {
            AddConflict(c.name, c.overlap, c.sev,
                "%s detected — %s overlap with Playground",
                c.name, c.overlap);
        }
    }
}

uint32_t CompatibilityProbe::GetInfoCount() const
{
    uint32_t n = 0;
    for (const auto& c : m_conflicts)
        if (c.severity == ConflictSeverity::Info) ++n;
    return n;
}

uint32_t CompatibilityProbe::GetWarningCount() const
{
    uint32_t n = 0;
    for (const auto& c : m_conflicts)
        if (c.severity == ConflictSeverity::Warning) ++n;
    return n;
}

uint32_t CompatibilityProbe::GetErrorCount() const
{
    uint32_t n = 0;
    for (const auto& c : m_conflicts)
        if (c.severity == ConflictSeverity::Error) ++n;
    return n;
}

std::string CompatibilityProbe::GetSummary() const
{
    char buf[256];
    snprintf(buf, sizeof(buf),
        "CompatProbe: %zu modules, %zu reports (%u info, %u warn, %u error)",
        m_modules.size(), m_conflicts.size(),
        GetInfoCount(), GetWarningCount(), GetErrorCount());
    return buf;
}

void CompatibilityProbe::AddConflict(const char* source, const char* target,
    ConflictSeverity sev, const char* fmt, ...)
{
    ConflictReport r;
    r.source = source;
    r.target = target;
    r.severity = sev;

    va_list args;
    va_start(args, fmt);
    vsnprintf(r.detail, sizeof(r.detail), fmt, args);
    va_end(args);

    m_conflicts.push_back(r);
}

void CompatibilityProbe::AddModule(const char* name, const char* path,
    uint32_t sizeKB, bool known, const char* desc)
{
    LoadedModule m;
    strncpy(m.name, name, sizeof(m.name) - 1);
    m.name[sizeof(m.name) - 1] = '\0';
    strncpy(m.path, path, sizeof(m.path) - 1);
    m.path[sizeof(m.path) - 1] = '\0';
    m.sizeKB = sizeKB;
    m.isKnown = known;
    strncpy(m.description, desc, sizeof(m.description) - 1);
    m.description[sizeof(m.description) - 1] = '\0';

    m_modules.push_back(m);
}

} // namespace SB
