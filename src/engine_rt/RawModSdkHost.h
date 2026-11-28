#pragma once
// RawModSdkHost.h: Wave 86 SemVer v2.0 Mod-Developer SDK Host & Per-Mod CPU/VRAM Budget Tracker.
// Enforces fail-closed ABI negotiation, acyclic frame-graph render pass registration,
// capability-gated read-only queries, and per-mod CPU-microsecond + VRAM-byte attribution
// naming the exact offending mod ID on budget breach.
// Clean-room MIT, MSVC /W4 /WX clean.

#include "../../include/raw_mod_sdk_abi.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::rt
{

struct RegisteredModInfo
{
    uint32_t           modId       = 0u;
    std::string        modName;
    uint32_t           grantedCaps = 0u;
    RawModBudgetRecord budget{};
};

class RawModBudgetTracker
{
public:
    int RegisterMod(
        uint32_t           modId,
        const std::string& modName,
        uint32_t           grantedCaps,
        uint64_t           softCpuCapUs,
        uint64_t           hardCpuCapUs,
        uint64_t           softVramCapBytes,
        uint64_t           hardVramCapBytes);

    int RecordModFrameUsage(
        uint32_t     modId,
        uint64_t     cpuUs,
        uint64_t     vramBytes,
        std::string* outDiagnostic = nullptr);

    int GetBudget(uint32_t modId, RawModBudgetRecord* outBudget) const noexcept;
    bool HasCapabilities(uint32_t modId, uint32_t requiredCaps) const noexcept;
    std::string GetModName(uint32_t modId) const;
    uint32_t GetWorstOffendingModId() const noexcept;

private:
    std::unordered_map<uint32_t, RegisteredModInfo> m_mods;
};

class RawModSdkHost
{
public:
    // Fail-closed SemVer v2.0 negotiation: rejects mismatched major version or undersized struct.
    int NegotiateAbiV2(uint32_t requestedAbiVersion, uint32_t requestedStructSize, RawModSdkApiV2* outApi);

    int RegisterMod(
        uint32_t           modId,
        const std::string& modName,
        uint32_t           grantedCaps,
        uint64_t           softCpuCapUs,
        uint64_t           hardCpuCapUs,
        uint64_t           softVramCapBytes,
        uint64_t           hardVramCapBytes);

    int RegisterRenderPass(const RawModPassDesc& desc, std::string* outDiag = nullptr);
    int RegisterDebugView(const RawModDebugViewDesc& desc, std::string* outDiag = nullptr);
    int QueryReadOnlyWorld(uint32_t modId, uint32_t reqCaps, RawModReadOnlyWorldQuery* outQuery) const;
    int RecordModUsage(uint32_t modId, uint64_t cpuUs, uint64_t vramBytes, std::string* outDiag = nullptr);
    int GetModBudget(uint32_t modId, RawModBudgetRecord* outBudget) const noexcept;

    void SetWorldSnapshot(const RawModReadOnlyWorldQuery& q) noexcept { m_worldState = q; }

    [[nodiscard]] size_t RegisteredPassCount() const noexcept { return m_passes.size(); }
    [[nodiscard]] size_t RegisteredDebugViewCount() const noexcept { return m_debugViews.size(); }
    [[nodiscard]] const RawModBudgetTracker& Budgets() const noexcept { return m_budgets; }

private:
    bool WouldCreatePassCycle(uint32_t passId, uint32_t dependsOnPassId) const noexcept;

    RawModBudgetTracker                              m_budgets;
    std::unordered_map<uint32_t, RawModPassDesc>     m_passes;
    std::unordered_map<uint32_t, RawModDebugViewDesc> m_debugViews;
    RawModReadOnlyWorldQuery                         m_worldState{};
};

} // namespace raw::rt
