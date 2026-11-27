#pragma once
// RawParamRegistry.h: Wave 87 Thread-Safe, Zero-Allocation Hot-Path Runtime Parameter
// & Buffer Introspection Registry.
// Exposes live Creation Engine parameters (GMST, INI, shader cbuffer tunables, ring/array
// capacities, hook ownership flags, VRAM/actor/meshlet/mod budgets) with:
//   - Lock-free atomic hot-path reads (zero heap allocations, zero mutexes on Get*)
//   - Typed min/max/NaN/Inf fail-closed validation on Set*
//   - Deterministic JSON snapshot export (raw_param_snapshot.json)
//   - Direct integration with Wave 86 RawModSdkHost & Wave 82 RawDebugViews
// Clean-room MIT, MSVC /W4 /WX clean.

#include "RawModSdkHost.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <string>
#include <string_view>

namespace raw::rt
{

enum class ParamDomain : uint8_t
{
    Gmst          = 0u,
    Ini           = 1u,
    ShaderCbuffer = 2u,
    RingCapacity  = 3u,
    HookOwnership = 4u,
    RuntimeBudget = 5u,
};

enum class ParamType : uint8_t
{
    Float = 0u,
    Int64 = 1u,
    Uint64 = 2u,
    Bool  = 3u,
};

enum class ParamStatus : int32_t
{
    Ok                = 0,
    TableFull         = -1,
    DuplicateName     = -2,
    NotFound          = -3,
    TypeMismatch      = -4,
    NonFiniteRejected = -5,
    OutOfBounds       = -6,
    ReadOnlyRejected  = -7,
    InvalidDebugSlot  = -8,
};

struct alignas(64) RawParamSlot
{
    char                  name[48]      = {};
    ParamDomain           domain        = ParamDomain::RuntimeBudget;
    ParamType             type          = ParamType::Float;
    bool                  readOnly      = false;
    uint8_t               occupied      = 0u;
    uint32_t              writeCount    = 0u;
    double                minVal        = 0.0;
    double                maxVal        = 0.0;
    std::atomic<uint64_t> rawBits{0ull};
    uint64_t              reserved[6]   = {};
};
static_assert(sizeof(RawParamSlot) == 128u && alignof(RawParamSlot) == 64u,
              "RawParamSlot must be 128 bytes (2 cache lines, 64B aligned)");

class RawParamRegistry
{
public:
    static constexpr size_t kMaxParams = 256u;

    RawParamRegistry() = default;

    // Registration (call at init/setup; returns slot index via outSlot if non-null).
    ParamStatus RegisterFloat(std::string_view name, ParamDomain domain, double initialVal,
                              double minVal, double maxVal, bool readOnly = false, uint32_t* outSlot = nullptr);
    ParamStatus RegisterInt(std::string_view name, ParamDomain domain, int64_t initialVal,
                            int64_t minVal, int64_t maxVal, bool readOnly = false, uint32_t* outSlot = nullptr);
    ParamStatus RegisterUint(std::string_view name, ParamDomain domain, uint64_t initialVal,
                             uint64_t minVal, uint64_t maxVal, bool readOnly = false, uint32_t* outSlot = nullptr);
    ParamStatus RegisterBool(std::string_view name, ParamDomain domain, bool initialVal,
                             bool readOnly = false, uint32_t* outSlot = nullptr);

    // Zero-allocation lock-free hot-path reads by slot index or name.
    bool GetFloatBySlot(uint32_t slotIdx, double* outVal) const noexcept;
    bool GetIntBySlot(uint32_t slotIdx, int64_t* outVal) const noexcept;
    bool GetUintBySlot(uint32_t slotIdx, uint64_t* outVal) const noexcept;
    bool GetBoolBySlot(uint32_t slotIdx, bool* outVal) const noexcept;
    bool FindSlot(std::string_view name, uint32_t* outSlot) const noexcept;

    // Typed fail-closed mutators (reject NaN, Inf, out-of-bounds, type mismatch, read-only).
    ParamStatus SetFloat(std::string_view name, double val) noexcept;
    ParamStatus SetInt(std::string_view name, int64_t val) noexcept;
    ParamStatus SetUint(std::string_view name, uint64_t val) noexcept;
    ParamStatus SetBool(std::string_view name, bool val) noexcept;

    // Integration with Wave 86 RawModSdkHost and Wave 82 RawDebugViews.
    ParamStatus BindDebugViewMode(uint32_t mode) noexcept;
    void SyncFromModSdkHost(const RawModSdkHost& host) noexcept;

    // Populates canonical RAW 2027 runtime parameters (GMST, INI, cbuffer, rings, budgets).
    void RegisterCanonicalDefaults();

    // Deterministic sorted JSON snapshot export (schema: raw.param-registry/1).
    std::string ExportSnapshotJson() const;
    bool WriteSnapshotJsonFile(const std::string& filePath) const;

    [[nodiscard]] uint32_t ParamCount() const noexcept { return m_count.load(std::memory_order_acquire); }
    [[nodiscard]] uint64_t RejectedWrites() const noexcept { return m_rejectedWrites.load(std::memory_order_relaxed); }
    [[nodiscard]] uint32_t ActiveDebugView() const noexcept { return m_activeDebugView.load(std::memory_order_acquire); }

private:
    ParamStatus AllocateSlot(std::string_view name, ParamDomain domain, ParamType type,
                             double minVal, double maxVal, bool readOnly,
                             uint64_t initialBits, uint32_t* outSlot);

    std::array<RawParamSlot, kMaxParams> m_slots{};
    std::atomic<uint32_t>                m_count{0u};
    std::atomic<uint64_t>                m_rejectedWrites{0ull};
    std::atomic<uint32_t>                m_activeDebugView{0u};
    std::atomic<uint32_t>                m_modPassCount{0u};
    std::atomic<uint32_t>                m_modDebugViewCount{0u};
};

} // namespace raw::rt
