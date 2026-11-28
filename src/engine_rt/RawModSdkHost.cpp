// RawModSdkHost.cpp: Wave 86 SemVer v2.0 Mod-Developer SDK Host & Per-Mod Budget Tracker.
// Original work, MIT.
#include "RawModSdkHost.h"

#include <sstream>
#include <unordered_set>

namespace raw::rt
{
namespace
{

int C_RegisterPass(void* ctx, const RawModPassDesc* desc)
{
    if (!ctx || !desc) return RAW_MOD_ERR_NULL_ARG;
    return static_cast<RawModSdkHost*>(ctx)->RegisterRenderPass(*desc, nullptr);
}

int C_RegisterDebugView(void* ctx, const RawModDebugViewDesc* desc)
{
    if (!ctx || !desc) return RAW_MOD_ERR_NULL_ARG;
    return static_cast<RawModSdkHost*>(ctx)->RegisterDebugView(*desc, nullptr);
}

int C_QueryWorld(void* ctx, uint32_t modId, uint32_t reqCaps, RawModReadOnlyWorldQuery* outQ)
{
    if (!ctx || !outQ) return RAW_MOD_ERR_NULL_ARG;
    return static_cast<RawModSdkHost*>(ctx)->QueryReadOnlyWorld(modId, reqCaps, outQ);
}

int C_RecordUsage(void* ctx, uint32_t modId, uint64_t cpuUs, uint64_t vramBytes)
{
    if (!ctx) return RAW_MOD_ERR_NULL_ARG;
    return static_cast<RawModSdkHost*>(ctx)->RecordModUsage(modId, cpuUs, vramBytes, nullptr);
}

int C_GetBudget(void* ctx, uint32_t modId, RawModBudgetRecord* outB)
{
    if (!ctx || !outB) return RAW_MOD_ERR_NULL_ARG;
    return static_cast<RawModSdkHost*>(ctx)->GetModBudget(modId, outB);
}

} // namespace

int RawModBudgetTracker::RegisterMod(
    uint32_t           modId,
    const std::string& modName,
    uint32_t           grantedCaps,
    uint64_t           softCpuCapUs,
    uint64_t           hardCpuCapUs,
    uint64_t           softVramCapBytes,
    uint64_t           hardVramCapBytes)
{
    if (modId == 0u || m_mods.find(modId) != m_mods.end()) {
        return RAW_MOD_ERR_DUPLICATE;
    }
    RegisteredModInfo info{};
    info.modId                   = modId;
    info.modName                 = modName.empty() ? ("mod_" + std::to_string(modId)) : modName;
    info.grantedCaps             = grantedCaps;
    info.budget.modId            = modId;
    info.budget.softCpuCapUs     = softCpuCapUs;
    info.budget.hardCpuCapUs     = (hardCpuCapUs >= softCpuCapUs) ? hardCpuCapUs : softCpuCapUs;
    info.budget.softVramCapBytes = softVramCapBytes;
    info.budget.hardVramCapBytes = (hardVramCapBytes >= softVramCapBytes) ? hardVramCapBytes : softVramCapBytes;
    m_mods[modId]                = std::move(info);
    return RAW_MOD_OK;
}

int RawModBudgetTracker::RecordModFrameUsage(
    uint32_t     modId,
    uint64_t     cpuUs,
    uint64_t     vramBytes,
    std::string* outDiagnostic)
{
    auto it = m_mods.find(modId);
    if (it == m_mods.end()) {
        return RAW_MOD_ERR_UNKNOWN_MOD;
    }
    auto& b     = it->second.budget;
    b.cpuTimeUs = cpuUs;
    b.vramBytes = vramBytes;

    if (cpuUs > b.softCpuCapUs) {
        ++b.softCpuBreaches;
    }
    bool hardBreach = false;
    std::ostringstream oss;
    if (cpuUs > b.hardCpuCapUs) {
        ++b.hardCpuBreaches;
        hardBreach = true;
        oss << "Mod budget breach: modId=" << modId << " ('" << it->second.modName
            << "') exceeded hard CPU cap (" << cpuUs << " us > " << b.hardCpuCapUs << " us). ";
    }
    if (vramBytes > b.hardVramCapBytes) {
        ++b.hardVramBreaches;
        hardBreach = true;
        oss << "Mod budget breach: modId=" << modId << " ('" << it->second.modName
            << "') exceeded hard VRAM cap (" << vramBytes << " B > " << b.hardVramCapBytes << " B). ";
    }
    if (outDiagnostic && hardBreach) {
        *outDiagnostic = oss.str();
    }
    return hardBreach ? RAW_MOD_ERR_BUDGET_EXCEEDED : RAW_MOD_OK;
}

int RawModBudgetTracker::GetBudget(uint32_t modId, RawModBudgetRecord* outBudget) const noexcept
{
    if (!outBudget) return RAW_MOD_ERR_NULL_ARG;
    const auto it = m_mods.find(modId);
    if (it == m_mods.end()) return RAW_MOD_ERR_UNKNOWN_MOD;
    *outBudget = it->second.budget;
    return RAW_MOD_OK;
}

bool RawModBudgetTracker::HasCapabilities(uint32_t modId, uint32_t requiredCaps) const noexcept
{
    const auto it = m_mods.find(modId);
    if (it == m_mods.end()) return false;
    return (it->second.grantedCaps & requiredCaps) == requiredCaps;
}

std::string RawModBudgetTracker::GetModName(uint32_t modId) const
{
    const auto it = m_mods.find(modId);
    return (it == m_mods.end()) ? std::string{} : it->second.modName;
}

uint32_t RawModBudgetTracker::GetWorstOffendingModId() const noexcept
{
    uint32_t worstId    = 0u;
    uint64_t worstScore = 0u;
    for (const auto& kv : m_mods) {
        const auto& b = kv.second.budget;
        const uint64_t score = static_cast<uint64_t>(b.hardCpuBreaches + b.hardVramBreaches) * 1000000ull + b.cpuTimeUs;
        if (score > worstScore) {
            worstScore = score;
            worstId    = kv.first;
        }
    }
    return worstId;
}

int RawModSdkHost::NegotiateAbiV2(uint32_t requestedAbiVersion, uint32_t requestedStructSize, RawModSdkApiV2* outApi)
{
    if (!outApi) {
        return RAW_MOD_ERR_NULL_ARG;
    }
    const uint32_t reqMajor = (requestedAbiVersion >> 16u) & 0xFFFFu;
    if (reqMajor != RAW_MOD_SDK_ABI_VERSION_MAJOR) {
        return RAW_MOD_ERR_ABI_MISMATCH;
    }
    if (requestedStructSize < static_cast<uint32_t>(sizeof(RawModSdkApiV2))) {
        return RAW_MOD_ERR_STRUCT_SIZE;
    }

    outApi->structSize         = static_cast<uint32_t>(sizeof(RawModSdkApiV2));
    outApi->abiVersion         = RAW_MOD_SDK_ABI_VERSION;
    outApi->hostCtx            = this;
    outApi->GetUnifiedSnapshot = nullptr;
    outApi->RegisterRenderPass = &C_RegisterPass;
    outApi->RegisterDebugView  = &C_RegisterDebugView;
    outApi->QueryReadOnlyWorld = &C_QueryWorld;
    outApi->RecordModUsage     = &C_RecordUsage;
    outApi->GetModBudget       = &C_GetBudget;
    return RAW_MOD_OK;
}

int RawModSdkHost::RegisterMod(
    uint32_t           modId,
    const std::string& modName,
    uint32_t           grantedCaps,
    uint64_t           softCpuCapUs,
    uint64_t           hardCpuCapUs,
    uint64_t           softVramCapBytes,
    uint64_t           hardVramCapBytes)
{
    return m_budgets.RegisterMod(
        modId, modName, grantedCaps, softCpuCapUs, hardCpuCapUs, softVramCapBytes, hardVramCapBytes);
}

bool RawModSdkHost::WouldCreatePassCycle(uint32_t passId, uint32_t dependsOnPassId) const noexcept
{
    if (dependsOnPassId == RAW_MOD_NO_DEPENDENCY) {
        return false;
    }
    if (passId == dependsOnPassId) {
        return true;
    }
    uint32_t cur = dependsOnPassId;
    std::unordered_set<uint32_t> visited;
    visited.insert(passId);
    while (cur != RAW_MOD_NO_DEPENDENCY) {
        if (visited.count(cur) > 0u) {
            return true;
        }
        visited.insert(cur);
        const auto it = m_passes.find(cur);
        if (it == m_passes.end()) {
            break;
        }
        cur = it->second.dependsOnPassId;
    }
    return false;
}

int RawModSdkHost::RegisterRenderPass(const RawModPassDesc& desc, std::string* outDiag)
{
    if (desc.structSize < static_cast<uint32_t>(sizeof(RawModPassDesc))) {
        return RAW_MOD_ERR_STRUCT_SIZE;
    }
    if (!m_budgets.HasCapabilities(desc.modId, RAW_MOD_CAP_RENDER_PASS | desc.requiredCaps)) {
        if (outDiag) *outDiag = "Mod lacks RAW_MOD_CAP_RENDER_PASS capability.";
        return RAW_MOD_ERR_CAPABILITY_DENIED;
    }
    if (m_passes.find(desc.passId) != m_passes.end()) {
        if (outDiag) *outDiag = "Duplicate frame-graph passId.";
        return RAW_MOD_ERR_DUPLICATE;
    }
    if (WouldCreatePassCycle(desc.passId, desc.dependsOnPassId)) {
        if (outDiag) *outDiag = "FAIL: FRAMEGRAPH_CYCLE detected for passId " + std::to_string(desc.passId);
        return RAW_MOD_ERR_CYCLE;
    }

    // Also check if any existing pass already depends on desc.passId and reaches desc.dependsOnPassId.
    m_passes[desc.passId] = desc;
    // Full graph cycle audit across all registered passes.
    for (const auto& kv : m_passes) {
        uint32_t cur = kv.second.dependsOnPassId;
        std::unordered_set<uint32_t> seen;
        seen.insert(kv.first);
        while (cur != RAW_MOD_NO_DEPENDENCY) {
            if (seen.count(cur) > 0u) {
                m_passes.erase(desc.passId);
                if (outDiag) *outDiag = "FAIL: FRAMEGRAPH_CYCLE detected during pass registration.";
                return RAW_MOD_ERR_CYCLE;
            }
            seen.insert(cur);
            const auto it = m_passes.find(cur);
            if (it == m_passes.end()) break;
            cur = it->second.dependsOnPassId;
        }
    }
    return RAW_MOD_OK;
}

int RawModSdkHost::RegisterDebugView(const RawModDebugViewDesc& desc, std::string* outDiag)
{
    if (desc.structSize < static_cast<uint32_t>(sizeof(RawModDebugViewDesc))) {
        return RAW_MOD_ERR_STRUCT_SIZE;
    }
    if (!m_budgets.HasCapabilities(desc.modId, RAW_MOD_CAP_DEBUG_VIEW)) {
        if (outDiag) *outDiag = "Mod lacks RAW_MOD_CAP_DEBUG_VIEW capability.";
        return RAW_MOD_ERR_CAPABILITY_DENIED;
    }
    if (desc.viewSlot < RAW_MOD_DEBUG_SLOT_MIN || desc.viewSlot > RAW_MOD_DEBUG_SLOT_MAX) {
        if (outDiag) *outDiag = "Debug view slot must be in [100..255] to preserve core views 0..8.";
        return RAW_MOD_ERR_INVALID_SLOT;
    }
    if (m_debugViews.find(desc.viewSlot) != m_debugViews.end()) {
        if (outDiag) *outDiag = "Debug view slot already registered.";
        return RAW_MOD_ERR_DUPLICATE;
    }
    m_debugViews[desc.viewSlot] = desc;
    return RAW_MOD_OK;
}

int RawModSdkHost::QueryReadOnlyWorld(uint32_t modId, uint32_t reqCaps, RawModReadOnlyWorldQuery* outQuery) const
{
    if (!outQuery) return RAW_MOD_ERR_NULL_ARG;
    if (outQuery->structSize < static_cast<uint32_t>(sizeof(RawModReadOnlyWorldQuery))) {
        return RAW_MOD_ERR_STRUCT_SIZE;
    }
    if (!m_budgets.HasCapabilities(modId, reqCaps)) {
        return RAW_MOD_ERR_CAPABILITY_DENIED;
    }
    *outQuery            = m_worldState;
    outQuery->structSize = static_cast<uint32_t>(sizeof(RawModReadOnlyWorldQuery));
    return RAW_MOD_OK;
}

int RawModSdkHost::RecordModUsage(uint32_t modId, uint64_t cpuUs, uint64_t vramBytes, std::string* outDiag)
{
    return m_budgets.RecordModFrameUsage(modId, cpuUs, vramBytes, outDiag);
}

int RawModSdkHost::GetModBudget(uint32_t modId, RawModBudgetRecord* outBudget) const noexcept
{
    return m_budgets.GetBudget(modId, outBudget);
}

} // namespace raw::rt
