#pragma once
// rt_mod_load_audit.h: Wave 50 Clean-Room TES4 Plugin Header, Master-Dependency & ESL/ESP Slot
// Integrity Auditor (`raw::rt`).
// Zero external dependencies; reads ONLY the leading TES4 header record of each plugin (<30 ms
// across full Skyrim SE Data\ folders!). Catches Missing Masters, Out-of-Order Masters,
// 254-Full-Slot Overflow, 4,096-ESL-Slot Overflow, Corrupt TES4 headers, and Oversized ESLs.
// Original work, MIT.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace raw::rt {

enum class ModAuditSeverity : uint8_t {
    Clean    = 0u,
    Warning  = 1u,
    Critical = 2u,
};

const char* ModAuditSeverityName(ModAuditSeverity sev) noexcept;

struct Tes4PluginHeader {
    std::string              fileName;
    bool                     valid          = false;
    std::string              parseError;
    uint32_t                 recordFlags    = 0u;
    uint16_t                 formVersion    = 0u;
    float                    hedrVersion    = 0.0f;
    uint32_t                 numRecords     = 0u;
    uint32_t                 nextObjectId   = 0u;
    bool                     isMaster       = false; // 0x00000001 or .esm
    bool                     isLightEsl     = false; // 0x00000200 or .esl
    bool                     isLocalized    = false; // 0x00000080
    std::string              author;                 // CNAM
    std::vector<std::string> masters;                // All MAST subrecord filenames
};

struct ModLoadAuditReport {
    ModAuditSeverity              severity              = ModAuditSeverity::Clean;
    uint32_t                      totalPlugins          = 0u;
    uint32_t                      fullPluginCount       = 0u;
    uint32_t                      lightEslCount         = 0u;
    uint32_t                      missingMasterCount    = 0u;
    uint32_t                      outOfOrderMasterCount = 0u;
    uint32_t                      corruptHeaderCount    = 0u;
    uint32_t                      oversizedEslCount     = 0u;
    bool                          fullSlotOverflow      = false; // > 254 non-ESL plugins
    bool                          lightSlotOverflow     = false; // > 4096 ESL plugins
    std::vector<Tes4PluginHeader> plugins;
    std::vector<std::string>      diagnostics;
};

// Exported C ABI summary struct for F10 dump / bridge / diagnostics.
struct RawModAuditSummary {
    uint8_t  auditEnabled          = 1u;
    uint8_t  severity              = 0u;    // ModAuditSeverity (0=Clean, 1=Warning, 2=Critical)
    uint8_t  fullSlotOverflow      = 0u;
    uint8_t  lightSlotOverflow     = 0u;
    uint16_t totalPlugins          = 0u;
    uint16_t fullPluginCount       = 0u;
    uint16_t lightEslCount         = 0u;
    uint16_t missingMasterCount    = 0u;
    uint16_t outOfOrderMasterCount = 0u;
    uint16_t corruptHeaderCount    = 0u;
    uint16_t oversizedEslCount     = 0u;
    float    auditDurationMs       = 0.0f;
};

// Clean-room TES4 header parser (reads ONLY the TES4 record; zero full-file loading!).
Tes4PluginHeader ParseTes4HeaderBytes(const uint8_t* data, size_t size, const std::string& fileName);
Tes4PluginHeader ParseTes4HeaderFile(const std::string& filePath);

// Parses SSE `plugins.txt` (`*Plugin.esp` enabled entries, ignoring `#` comments & blank lines).
std::vector<std::string> ParsePluginsTxt(const std::string& text);

// Audits an ordered sequence of parsed TES4 headers for missing/out-of-order masters,
// 254-full / 4,096-ESL slot caps, corrupt headers, and oversized ESLs (>4,096 records).
ModLoadAuditReport AuditModLoadOrder(const std::vector<Tes4PluginHeader>& orderedPlugins);

// Audits a live Skyrim SE `Data\` directory (reading `plugins.txt` if provided/available +
// implicit base masters Skyrim/Update/Dawnguard/HearthFires/Dragonborn).
ModLoadAuditReport AuditSkyrimDataDirectory(const std::string& dataDir,
                                            const std::string& pluginsTxtPath = "");

// Runtime lifecycle hooks (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string ModLoadAuditInit(const std::string& gameDir, const std::string& overridePluginsTxt = "");
void        ModLoadAuditShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetModAuditSummary(raw::rt::RawModAuditSummary* out);
