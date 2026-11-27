// RawParamRegistry.cpp: Wave 87 Thread-Safe, Zero-Allocation Hot-Path Runtime Parameter
// & Buffer Introspection Registry. Original work, MIT.
#include "RawParamRegistry.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <vector>

namespace raw::rt
{
namespace
{

inline uint64_t DoubleToBits(double v) noexcept
{
    uint64_t b = 0ull;
    std::memcpy(&b, &v, sizeof(b));
    return b;
}

inline double BitsToDouble(uint64_t b) noexcept
{
    double v = 0.0;
    std::memcpy(&v, &b, sizeof(v));
    return v;
}

const char* DomainName(ParamDomain d) noexcept
{
    switch (d) {
        case ParamDomain::Gmst:          return "gmst";
        case ParamDomain::Ini:           return "ini";
        case ParamDomain::ShaderCbuffer: return "shader_cbuffer";
        case ParamDomain::RingCapacity:  return "ring_capacity";
        case ParamDomain::HookOwnership: return "hook_ownership";
        case ParamDomain::RuntimeBudget: return "runtime_budget";
    }
    return "unknown";
}

const char* TypeName(ParamType t) noexcept
{
    switch (t) {
        case ParamType::Float:  return "float";
        case ParamType::Int64:  return "int64";
        case ParamType::Uint64: return "uint64";
        case ParamType::Bool:   return "bool";
    }
    return "unknown";
}

} // namespace

bool RawParamRegistry::FindSlot(std::string_view name, uint32_t* outSlot) const noexcept
{
    const uint32_t n = m_count.load(std::memory_order_acquire);
    for (uint32_t i = 0u; i < n && i < kMaxParams; ++i) {
        if (m_slots[i].occupied && name == m_slots[i].name) {
            if (outSlot) *outSlot = i;
            return true;
        }
    }
    return false;
}

ParamStatus RawParamRegistry::AllocateSlot(
    std::string_view name, ParamDomain domain, ParamType type,
    double minVal, double maxVal, bool readOnly,
    uint64_t initialBits, uint32_t* outSlot)
{
    if (name.empty() || name.size() >= sizeof(RawParamSlot::name)) {
        return ParamStatus::OutOfBounds;
    }
    uint32_t existing = 0u;
    if (FindSlot(name, &existing)) {
        return ParamStatus::DuplicateName;
    }
    const uint32_t idx = m_count.load(std::memory_order_acquire);
    if (idx >= kMaxParams) {
        return ParamStatus::TableFull;
    }

    RawParamSlot& s = m_slots[idx];
    std::memset(s.name, 0, sizeof(s.name));
    std::memcpy(s.name, name.data(), name.size());
    s.domain     = domain;
    s.type       = type;
    s.readOnly   = readOnly;
    s.minVal     = minVal;
    s.maxVal     = maxVal;
    s.writeCount = 0u;
    s.rawBits.store(initialBits, std::memory_order_relaxed);
    s.occupied = 1u;

    m_count.store(idx + 1u, std::memory_order_release);
    if (outSlot) *outSlot = idx;
    return ParamStatus::Ok;
}

ParamStatus RawParamRegistry::RegisterFloat(
    std::string_view name, ParamDomain domain, double initialVal,
    double minVal, double maxVal, bool readOnly, uint32_t* outSlot)
{
    if (!std::isfinite(initialVal) || !std::isfinite(minVal) || !std::isfinite(maxVal)) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::NonFiniteRejected;
    }
    if (minVal > maxVal || initialVal < minVal || initialVal > maxVal) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::OutOfBounds;
    }
    return AllocateSlot(name, domain, ParamType::Float, minVal, maxVal, readOnly, DoubleToBits(initialVal), outSlot);
}

ParamStatus RawParamRegistry::RegisterInt(
    std::string_view name, ParamDomain domain, int64_t initialVal,
    int64_t minVal, int64_t maxVal, bool readOnly, uint32_t* outSlot)
{
    if (minVal > maxVal || initialVal < minVal || initialVal > maxVal) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::OutOfBounds;
    }
    return AllocateSlot(name, domain, ParamType::Int64,
                        static_cast<double>(minVal), static_cast<double>(maxVal),
                        readOnly, static_cast<uint64_t>(initialVal), outSlot);
}

ParamStatus RawParamRegistry::RegisterUint(
    std::string_view name, ParamDomain domain, uint64_t initialVal,
    uint64_t minVal, uint64_t maxVal, bool readOnly, uint32_t* outSlot)
{
    if (minVal > maxVal || initialVal < minVal || initialVal > maxVal) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::OutOfBounds;
    }
    return AllocateSlot(name, domain, ParamType::Uint64,
                        static_cast<double>(minVal), static_cast<double>(maxVal),
                        readOnly, initialVal, outSlot);
}

ParamStatus RawParamRegistry::RegisterBool(
    std::string_view name, ParamDomain domain, bool initialVal,
    bool readOnly, uint32_t* outSlot)
{
    return AllocateSlot(name, domain, ParamType::Bool, 0.0, 1.0, readOnly, initialVal ? 1ull : 0ull, outSlot);
}

bool RawParamRegistry::GetFloatBySlot(uint32_t slotIdx, double* outVal) const noexcept
{
    if (!outVal || slotIdx >= m_count.load(std::memory_order_acquire)) return false;
    const auto& s = m_slots[slotIdx];
    if (!s.occupied || s.type != ParamType::Float) return false;
    *outVal = BitsToDouble(s.rawBits.load(std::memory_order_acquire));
    return true;
}

bool RawParamRegistry::GetIntBySlot(uint32_t slotIdx, int64_t* outVal) const noexcept
{
    if (!outVal || slotIdx >= m_count.load(std::memory_order_acquire)) return false;
    const auto& s = m_slots[slotIdx];
    if (!s.occupied || s.type != ParamType::Int64) return false;
    *outVal = static_cast<int64_t>(s.rawBits.load(std::memory_order_acquire));
    return true;
}

bool RawParamRegistry::GetUintBySlot(uint32_t slotIdx, uint64_t* outVal) const noexcept
{
    if (!outVal || slotIdx >= m_count.load(std::memory_order_acquire)) return false;
    const auto& s = m_slots[slotIdx];
    if (!s.occupied || s.type != ParamType::Uint64) return false;
    *outVal = s.rawBits.load(std::memory_order_acquire);
    return true;
}

bool RawParamRegistry::GetBoolBySlot(uint32_t slotIdx, bool* outVal) const noexcept
{
    if (!outVal || slotIdx >= m_count.load(std::memory_order_acquire)) return false;
    const auto& s = m_slots[slotIdx];
    if (!s.occupied || s.type != ParamType::Bool) return false;
    *outVal = (s.rawBits.load(std::memory_order_acquire) != 0ull);
    return true;
}

ParamStatus RawParamRegistry::SetFloat(std::string_view name, double val) noexcept
{
    uint32_t idx = 0u;
    if (!FindSlot(name, &idx)) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::NotFound;
    }
    auto& s = m_slots[idx];
    if (s.type != ParamType::Float) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::TypeMismatch;
    }
    if (s.readOnly) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::ReadOnlyRejected;
    }
    if (!std::isfinite(val)) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::NonFiniteRejected;
    }
    if (val < s.minVal || val > s.maxVal) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::OutOfBounds;
    }
    s.rawBits.store(DoubleToBits(val), std::memory_order_release);
    ++s.writeCount;
    return ParamStatus::Ok;
}

ParamStatus RawParamRegistry::SetInt(std::string_view name, int64_t val) noexcept
{
    uint32_t idx = 0u;
    if (!FindSlot(name, &idx)) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::NotFound;
    }
    auto& s = m_slots[idx];
    if (s.type != ParamType::Int64) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::TypeMismatch;
    }
    if (s.readOnly) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::ReadOnlyRejected;
    }
    const double dv = static_cast<double>(val);
    if (dv < s.minVal || dv > s.maxVal) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::OutOfBounds;
    }
    s.rawBits.store(static_cast<uint64_t>(val), std::memory_order_release);
    ++s.writeCount;
    return ParamStatus::Ok;
}

ParamStatus RawParamRegistry::SetUint(std::string_view name, uint64_t val) noexcept
{
    uint32_t idx = 0u;
    if (!FindSlot(name, &idx)) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::NotFound;
    }
    auto& s = m_slots[idx];
    if (s.type != ParamType::Uint64) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::TypeMismatch;
    }
    if (s.readOnly) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::ReadOnlyRejected;
    }
    const double dv = static_cast<double>(val);
    if (dv < s.minVal || dv > s.maxVal) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::OutOfBounds;
    }
    s.rawBits.store(val, std::memory_order_release);
    ++s.writeCount;
    return ParamStatus::Ok;
}

ParamStatus RawParamRegistry::SetBool(std::string_view name, bool val) noexcept
{
    uint32_t idx = 0u;
    if (!FindSlot(name, &idx)) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::NotFound;
    }
    auto& s = m_slots[idx];
    if (s.type != ParamType::Bool) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::TypeMismatch;
    }
    if (s.readOnly) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::ReadOnlyRejected;
    }
    s.rawBits.store(val ? 1ull : 0ull, std::memory_order_release);
    ++s.writeCount;
    return ParamStatus::Ok;
}

ParamStatus RawParamRegistry::BindDebugViewMode(uint32_t mode) noexcept
{
    const bool validCore = (mode <= 8u);
    const bool validMod  = (mode >= RAW_MOD_DEBUG_SLOT_MIN && mode <= RAW_MOD_DEBUG_SLOT_MAX);
    if (!validCore && !validMod) {
        m_rejectedWrites.fetch_add(1ull, std::memory_order_relaxed);
        return ParamStatus::InvalidDebugSlot;
    }
    m_activeDebugView.store(mode, std::memory_order_release);
    return ParamStatus::Ok;
}

void RawParamRegistry::SyncFromModSdkHost(const RawModSdkHost& host) noexcept
{
    m_modPassCount.store(static_cast<uint32_t>(host.RegisteredPassCount()), std::memory_order_release);
    m_modDebugViewCount.store(static_cast<uint32_t>(host.RegisteredDebugViewCount()), std::memory_order_release);
}

void RawParamRegistry::RegisterCanonicalDefaults()
{
    RegisterFloat("gmst.fSneakMaxDistance", ParamDomain::Gmst, 2500.0, 200.0, 8000.0, false);
    RegisterFloat("ini.fNearDistance", ParamDomain::Ini, 22.0, 5.0, 60.0, false);
    RegisterFloat("cb.lodTargetPixelError", ParamDomain::ShaderCbuffer, 1.25, 0.85, 3.50, false);
    RegisterFloat("cb.hdrPeakNits", ParamDomain::ShaderCbuffer, 1000.0, 100.0, 4000.0, false);
    RegisterUint("ring.jobDequeCapacity", ParamDomain::RingCapacity, 4096ull, 256ull, 65536ull, true);
    RegisterUint("ring.uploadRingCapacity", ParamDomain::RingCapacity, 256ull, 32ull, 4096ull, true);
    RegisterBool("hook.depthOwnedByRaw", ParamDomain::HookOwnership, true, true);
    RegisterUint("budget.maxCrowdActors", ParamDomain::RuntimeBudget, 1024ull, 64ull, 4096ull, false);
    RegisterFloat("budget.vramResidencyCap", ParamDomain::RuntimeBudget, 0.80, 0.25, 0.80, true);
    RegisterUint("budget.meshletMaxVerts", ParamDomain::RuntimeBudget, 64ull, 16ull, 64ull, true);
}

std::string RawParamRegistry::ExportSnapshotJson() const
{
    const uint32_t n = m_count.load(std::memory_order_acquire);
    std::vector<uint32_t> order;
    order.reserve(n);
    for (uint32_t i = 0u; i < n && i < kMaxParams; ++i) {
        if (m_slots[i].occupied) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](uint32_t a, uint32_t b) {
        return std::strcmp(m_slots[a].name, m_slots[b].name) < 0;
    });

    std::ostringstream oss;
    oss << std::fixed << std::setprecision(4);
    oss << "{\n";
    oss << "  \"schema\": \"raw.param-registry/1\",\n";
    oss << "  \"param_count\": " << order.size() << ",\n";
    oss << "  \"rejected_writes\": " << m_rejectedWrites.load(std::memory_order_relaxed) << ",\n";
    oss << "  \"active_debug_view\": " << m_activeDebugView.load(std::memory_order_acquire) << ",\n";
    oss << "  \"mod_registered_passes\": " << m_modPassCount.load(std::memory_order_acquire) << ",\n";
    oss << "  \"mod_registered_debug_views\": " << m_modDebugViewCount.load(std::memory_order_acquire) << ",\n";
    oss << "  \"params\": [\n";
    for (size_t idx = 0u; idx < order.size(); ++idx) {
        const auto& s = m_slots[order[idx]];
        const uint64_t bits = s.rawBits.load(std::memory_order_acquire);
        oss << "    {\"name\": \"" << s.name
            << "\", \"domain\": \"" << DomainName(s.domain)
            << "\", \"type\": \"" << TypeName(s.type)
            << "\", \"read_only\": " << (s.readOnly ? "true" : "false")
            << ", \"min\": " << s.minVal
            << ", \"max\": " << s.maxVal
            << ", \"value\": ";
        switch (s.type) {
            case ParamType::Float:  oss << BitsToDouble(bits); break;
            case ParamType::Int64:  oss << static_cast<int64_t>(bits); break;
            case ParamType::Uint64: oss << bits; break;
            case ParamType::Bool:   oss << ((bits != 0ull) ? "true" : "false"); break;
        }
        oss << "}" << ((idx + 1u < order.size()) ? "," : "") << "\n";
    }
    oss << "  ]\n";
    oss << "}\n";
    return oss.str();
}

bool RawParamRegistry::WriteSnapshotJsonFile(const std::string& filePath) const
{
    std::ofstream ofs(filePath, std::ios::binary | std::ios::trunc);
    if (!ofs.is_open()) return false;
    const std::string json = ExportSnapshotJson();
    ofs.write(json.data(), static_cast<std::streamsize>(json.size()));
    return ofs.good();
}

} // namespace raw::rt
