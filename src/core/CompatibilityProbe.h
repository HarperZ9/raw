#pragma once
//=============================================================================
//  CompatibilityProbe — Runtime conflict detection for universal mod compat
//
//  Detects and reports potential conflicts with other mods, overlays, and
//  graphics tools at runtime.  Goes beyond CompatDetect's DLL fingerprinting
//  by checking for actual resource conflicts:
//
//    1. SRV slot collisions — other hooks overwriting our t17-t26 slots
//    2. Hook chain integrity — D3DCompile/Present hooks still point to us
//    3. DLL fingerprinting — loaded modules with known signatures
//    4. ENB version/SDK mismatch — version vs expected
//    5. Overlay detection — DXVK, SpecialK, ReShade, Steam, Discord, MSI AB
//    6. SKSE plugin enumeration — other plugins touching the same systems
//=============================================================================

#include <cstdint>
#include <string>
#include <vector>

namespace SB
{

enum class ConflictSeverity : uint8_t
{
    Info,       // Detected, no conflict
    Warning,    // Potential conflict, likely harmless
    Error       // Active conflict — likely to cause issues
};

struct ConflictReport
{
    const char*       source;       // What was detected
    const char*       target;       // What SB resource it affects
    ConflictSeverity  severity;
    char              detail[192];  // Human-readable description
};

struct LoadedModule
{
    char     name[64];
    char     path[260];
    uint32_t sizeKB;
    bool     isKnown;      // recognized signature
    char     description[64];
};

class CompatibilityProbe
{
public:
    static CompatibilityProbe& Get()
    {
        static CompatibilityProbe inst;
        return inst;
    }

    // Run full probe (call once at startup after all systems initialized,
    // then periodically for runtime re-checks)
    void RunProbe();

    // Individual probes
    void ProbeLoadedModules();
    void ProbeOverlays();
    void ProbeSRVSlots();
    void ProbeHookChain();
    void ProbeENBVersion();
    void ProbeSKSEPlugins();

    // Results
    const std::vector<ConflictReport>& GetConflicts() const { return m_conflicts; }
    const std::vector<LoadedModule>&   GetModules() const   { return m_modules; }

    uint32_t GetInfoCount() const;
    uint32_t GetWarningCount() const;
    uint32_t GetErrorCount() const;

    // Summary string for log
    std::string GetSummary() const;

    // Re-probe interval tracking
    uint32_t GetLastProbeFrame() const { return m_lastProbeFrame; }
    void     SetLastProbeFrame(uint32_t f) { m_lastProbeFrame = f; }

private:
    CompatibilityProbe() = default;

    void AddConflict(const char* source, const char* target,
                     ConflictSeverity sev, const char* fmt, ...);
    void AddModule(const char* name, const char* path, uint32_t sizeKB,
                   bool known, const char* desc);

    std::vector<ConflictReport> m_conflicts;
    std::vector<LoadedModule>   m_modules;
    uint32_t m_lastProbeFrame = 0;
};

} // namespace SB
