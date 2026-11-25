#pragma once
// rt_combat_impact.h: Wave 41 Live Creation Engine 1.7.104.0 Tactile Combat Hitstop
// (BSTimer::SetGlobalTimeMultiplier 0xCE3970 / g_globalTimeMultiplier 0x20CCAC8)
// and 32-Slot 3D World-Space Combat Impact Decal Ring Buffer (RAW_GetImpactDecals C ABI).
// 100% binary-verified, Zydis-checked, fail-closed, SEH-guarded (Zero SKSE).
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {
struct Image;

// =================================================================================================
// 1. Tactile Combat Hitstop / Micro-Time-Dilation Controller
// =================================================================================================
enum class HitstopEventKind : uint8_t {
    None            = 0,
    TimedParry      = 1,   // 0.18x time scale for 65 ms
    HeavyGuardBreak = 2,   // 0.28x time scale for 55 ms
    LethalKillBlow  = 3,   // 0.35x time scale for 80 ms
};

struct HitstopProfile {
    float timeScale  = 1.0f;
    float durationMs = 0.0f;
};
HitstopProfile GetHitstopProfile(HitstopEventKind kind);

struct HitstopState {
    bool             active               = false;
    HitstopEventKind kind                 = HitstopEventKind::None;
    uint64_t         startMs              = 0;
    float            peakTimeScale        = 1.0f;
    float            durationMs           = 0.0f;
    float            appliedTimeScale     = 1.0f;
    uint32_t         triggeredCount       = 0;
    uint32_t         canceledByPauseCount = 0;
    uint32_t         skippedSlowTimeCount = 0;
};

// Triggers hitstop only if no non-1.0 external slow-time shout/mod is active. Returns true if engaged.
bool TriggerHitstop(HitstopState& st, HitstopEventKind kind, uint64_t nowMs, float liveExternalTimeMult);

// Advances wall-clock hitstop; sets *outNeedWrite = true and *outTimeMult when g_globalTimeMultiplier must be updated.
// Guarantees smooth decay back to 1.0f within <= 95 ms and instant cancel to 1.0f on menuPaused.
void StepHitstop(HitstopState& st, uint64_t nowMs, bool menuPaused, bool* outNeedWrite, float* outTimeMult);

// =================================================================================================
// 2. 32-Slot 3D World-Space Combat Impact Decal Ring Buffer
// =================================================================================================
enum class ImpactDecalKind : uint8_t {
    BloodSplatter = 0,
    StoneChip     = 1,
    FireScorch    = 2,
    FrostIce      = 3,
    ShockBurn     = 4,
};

struct ImpactDecalSlot {
    float           pos[3]      = {};
    float           radiusU     = 0.0f;
    ImpactDecalKind kind        = ImpactDecalKind::BloodSplatter;
    uint64_t        birthMs     = 0;
    float           normalAngle = 0.0f;
    float           intensity   = 1.0f;
    float           maxLifeSec  = 90.0f;
    bool            active      = false;
};

struct ImpactDecalRing {
    static constexpr uint32_t kCapacity = 32u;
    ImpactDecalSlot slots[kCapacity]{};
    uint32_t        writeCursor = 0;
    uint32_t        totalPushed = 0;

    void Push(const float pos[3], float radiusU, ImpactDecalKind kind, uint64_t nowMs,
              float normalAngle, float intensity, float maxLifeSec = 90.0f);
    // Packs up to maxCount newest non-expired decals into outBuf8PerDecal (8 floats each:
    // [x, y, z, radiusU, kindFloat, ageSec, normalAngle, intensity]).
    uint32_t PackForGpu(float* outBuf8PerDecal, uint32_t maxCount, uint64_t nowMs) const;
    void     Clear();
};

// =================================================================================================
// 3. Zydis Verification of BSTimer::SetGlobalTimeMultiplier (0xCE3970 -> 0x20CCAC8 / 0x20CCACC)
// =================================================================================================
bool VerifyGlobalTimeMultiplierCodeSite(const Image& img, uint32_t fnSetGtmRva,
                                        uint32_t expectedMultRva, uint32_t expectedTargetRva);

// =================================================================================================
// 4. Live Engine Runtime Lifecycle & Hit Notification
// =================================================================================================
struct CombatImpactTelemetry {
    uint8_t  hitstopVerified  = 0;
    uint8_t  hitstopActive    = 0;
    uint32_t hitstopTriggers  = 0;
    uint32_t hitstopSkipped   = 0;
    uint32_t decalsTotal      = 0;
    uint32_t decalsActiveNow  = 0;
    float    currentTimeScale = 1.0f;
};

#ifndef RAW_SIM_PURE_TEST
std::string CombatImpactInit(const Image& img);
void        CombatImpactOnHit(const float worldPos[3], ImpactDecalKind decalKind,
                              float radiusU, float intensity, HitstopEventKind hitstopKind,
                              bool involvesPlayer);
void        CombatImpactOnPresent(bool menuPaused);
void        CombatImpactShutdown();
bool        CombatImpactGetTelemetry(CombatImpactTelemetry* out);
#endif

}  // namespace raw::rt

// Wave 41 C ABI for D3D12Bridge / LightingPass / RawDecals3D.hlsli:
// Packs up to maxCount active 3D world-space combat impact decals (8 floats per decal:
// [x, y, z, radiusU, kindFloat, ageSec, normalAngle, intensity]) and returns count written.
extern "C" __declspec(dllexport) uint32_t RAW_GetImpactDecals(float* outBuf8PerDecal, uint32_t maxCount);
