// rt_combat_impact.cpp: Wave 41 Live Creation Engine 1.7.104.0 Tactile Combat Hitstop
// (BSTimer::SetGlobalTimeMultiplier 0xCE3970 / g_globalTimeMultiplier 0x20CCAC8)
// and 32-Slot 3D World-Space Combat Impact Decal Ring Buffer (RAW_GetImpactDecals C ABI).
// Original work, MIT.
#include "rt_combat_impact.h"
#include "rt_image.h"

#include <Zydis/Zydis.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_version.h"
#include <windows.h>
#endif

namespace raw::rt {

// =================================================================================================
// 1. Pure Deterministic Hitstop State Machine
// =================================================================================================
HitstopProfile GetHitstopProfile(HitstopEventKind kind) {
    switch (kind) {
        case HitstopEventKind::TimedParry:      return {0.18f, 65.0f};
        case HitstopEventKind::HeavyGuardBreak: return {0.28f, 55.0f};
        case HitstopEventKind::LethalKillBlow:  return {0.35f, 80.0f};
        default:                                return {1.00f,  0.0f};
    }
}

bool TriggerHitstop(HitstopState& st, HitstopEventKind kind, uint64_t nowMs, float liveExternalTimeMult) {
    if (kind == HitstopEventKind::None) return false;
    // Never overwrite user-set slow-time shouts/mods (anything outside ~1.0 when our hitstop isn't already running):
    if (!st.active && (!std::isfinite(liveExternalTimeMult) || std::fabs(liveExternalTimeMult - 1.0f) > 0.03f)) {
        ++st.skippedSlowTimeCount;
        return false;
    }
    const HitstopProfile prof = GetHitstopProfile(kind);
    if (prof.durationMs <= 0.0f) return false;

    st.active           = true;
    st.kind             = kind;
    st.startMs          = nowMs;
    st.peakTimeScale    = std::clamp(prof.timeScale, 0.10f, 0.90f);
    st.durationMs       = std::clamp(prof.durationMs, 20.0f, 85.0f);   // hard-capped so total decay <= 95 ms
    st.appliedTimeScale = st.peakTimeScale;
    ++st.triggeredCount;
    return true;
}

void StepHitstop(HitstopState& st, uint64_t nowMs, bool menuPaused, bool* outNeedWrite, float* outTimeMult) {
    if (outNeedWrite) *outNeedWrite = false;
    if (outTimeMult)  *outTimeMult  = 1.0f;
    if (!st.active) return;

    if (menuPaused) {
        st.active           = false;
        st.kind             = HitstopEventKind::None;
        st.appliedTimeScale = 1.0f;
        ++st.canceledByPauseCount;
        if (outNeedWrite) *outNeedWrite = true;
        if (outTimeMult)  *outTimeMult  = 1.0f;
        return;
    }

    const float elapsedMs = (nowMs >= st.startMs) ? static_cast<float>(nowMs - st.startMs) : 999.0f;
    const float totalMs   = std::min(st.durationMs + 12.0f, 95.0f);  // hold + 12 ms smooth ease-out, strictly <= 95 ms
    if (elapsedMs >= totalMs) {
        st.active           = false;
        st.kind             = HitstopEventKind::None;
        st.appliedTimeScale = 1.0f;
        if (outNeedWrite) *outNeedWrite = true;
        if (outTimeMult)  *outTimeMult  = 1.0f;
        return;
    }

    float targetScale = st.peakTimeScale;
    if (elapsedMs > st.durationMs) {
        const float easeSpan = std::max(totalMs - st.durationMs, 1.0f);
        const float t = std::clamp((elapsedMs - st.durationMs) / easeSpan, 0.0f, 1.0f);
        const float smooth = t * t * (3.0f - 2.0f * t);
        targetScale = st.peakTimeScale + (1.0f - st.peakTimeScale) * smooth;
    }
    targetScale = std::clamp(targetScale, 0.10f, 1.0f);
    if (std::fabs(targetScale - st.appliedTimeScale) > 0.005f || elapsedMs < 2.0f) {
        st.appliedTimeScale = targetScale;
        if (outNeedWrite) *outNeedWrite = true;
        if (outTimeMult)  *outTimeMult  = targetScale;
    }
}

// =================================================================================================
// 2. Pure Deterministic 32-Slot Impact Decal Ring Buffer
// =================================================================================================
void ImpactDecalRing::Push(const float pos[3], float radiusU, ImpactDecalKind kind, uint64_t nowMs,
                           float normalAngle, float intensity, float maxLifeSec) {
    if (!pos) return;
    if (!std::isfinite(pos[0]) || !std::isfinite(pos[1]) || !std::isfinite(pos[2])) return;
    auto& slot = slots[writeCursor % kCapacity];
    slot.pos[0]      = pos[0];
    slot.pos[1]      = pos[1];
    slot.pos[2]      = pos[2];
    slot.radiusU     = std::clamp(std::isfinite(radiusU) ? radiusU : 36.0f, 8.0f, 180.0f);
    slot.kind        = kind;
    slot.birthMs     = nowMs;
    slot.normalAngle = std::isfinite(normalAngle) ? normalAngle : 0.0f;
    slot.intensity   = std::clamp(std::isfinite(intensity) ? intensity : 1.0f, 0.05f, 2.0f);
    slot.maxLifeSec  = std::clamp(std::isfinite(maxLifeSec) ? maxLifeSec : 90.0f, 2.0f, 240.0f);
    slot.active      = true;
    writeCursor      = (writeCursor + 1u) % kCapacity;
    ++totalPushed;
}

uint32_t ImpactDecalRing::PackForGpu(float* outBuf8PerDecal, uint32_t maxCount, uint64_t nowMs) const {
    if (!outBuf8PerDecal || maxCount == 0u) return 0u;
    uint32_t written = 0u;
    // Walk newest -> oldest so the most recent combat impacts always win priority in the 16-slot GPU cbuffer:
    for (uint32_t step = 0u; step < kCapacity && written < maxCount; ++step) {
        const uint32_t idx = (writeCursor + kCapacity - 1u - step) % kCapacity;
        const auto& s = slots[idx];
        if (!s.active) continue;
        const float ageSec = (nowMs >= s.birthMs) ? static_cast<float>(nowMs - s.birthMs) * 0.001f : 0.0f;
        if (ageSec >= s.maxLifeSec) continue;
        // Smooth fade-out across final 15% of lifetime:
        const float fadeStart = s.maxLifeSec * 0.85f;
        float fade = 1.0f;
        if (ageSec > fadeStart) {
            fade = std::clamp(1.0f - (ageSec - fadeStart) / std::max(s.maxLifeSec - fadeStart, 0.1f), 0.0f, 1.0f);
        }
        const float effIntensity = s.intensity * fade;
        if (effIntensity <= 0.01f) continue;

        float* dst = outBuf8PerDecal + static_cast<size_t>(written) * 8u;
        dst[0] = s.pos[0];
        dst[1] = s.pos[1];
        dst[2] = s.pos[2];
        dst[3] = s.radiusU;
        dst[4] = static_cast<float>(static_cast<uint8_t>(s.kind));
        dst[5] = ageSec;
        dst[6] = s.normalAngle;
        dst[7] = effIntensity;
        ++written;
    }
    return written;
}

void ImpactDecalRing::Clear() {
    for (auto& s : slots) s = {};
    writeCursor = 0;
}

// =================================================================================================
// 3. Zydis Machine-Code Verification of BSTimer::SetGlobalTimeMultiplier (0xCE3970)
// =================================================================================================
bool VerifyGlobalTimeMultiplierCodeSite(const Image& img, uint32_t fnSetGtmRva,
                                        uint32_t expectedMultRva, uint32_t expectedTargetRva) {
    if (!img.base || !fnSetGtmRva || !expectedMultRva || !expectedTargetRva) return false;
    constexpr uint32_t kSetGtmDecodeWindowBytes = 0x50u;
    static_assert(kSetGtmDecodeWindowBytes >= 64u, "BSTimer::SetGlobalTimeMultiplier decode window guard");
    if (fnSetGtmRva < img.textBegin || fnSetGtmRva + kSetGtmDecodeWindowBytes > img.textEnd) return false;

    ZydisDecoder dec;
    ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
    bool sawMult = false;
    bool sawTarget = false;
    bool sawDelta = false;
    size_t off = 0;
    while (off < 0x58u) {
        ZydisDecodedInstruction in;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, img.base + fnSetGtmRva + off, 16, &in, ops))) break;
        const uint64_t rip = static_cast<uint64_t>(fnSetGtmRva) + off;
        for (uint8_t i = 0; i < in.operand_count_visible; ++i) {
            if (ops[i].type == ZYDIS_OPERAND_TYPE_MEMORY) {
                if (ops[i].mem.base == ZYDIS_REGISTER_RIP) {
                    ZyanU64 absAddr = 0;
                    if (ZYAN_SUCCESS(ZydisCalcAbsoluteAddress(&in, &ops[i], rip, &absAddr))) {
                        if (absAddr == expectedMultRva)   sawMult   = true;
                        if (absAddr == expectedTargetRva) sawTarget = true;
                    }
                } else if (ops[i].mem.base == ZYDIS_REGISTER_RCX && ops[i].mem.disp.has_displacement &&
                           ops[i].mem.disp.value == 0x18) {
                    sawDelta = true;
                }
            }
        }
        off += in.length;
        if (in.meta.category == ZYDIS_CATEGORY_INTERRUPT) break;
    }
    return sawMult && sawTarget && sawDelta;
}

// =================================================================================================
// 4. Live Engine Runtime State & C ABI Export
// =================================================================================================
namespace {

using SetGlobalTimeMultFn = void (*)(void* bsTimer, float mult, uint8_t immediate);

struct LiveCombatImpactRuntime {
    std::mutex          mu;
    ImpactDecalRing     decals{};
    HitstopState        hitstop{};
    const uint8_t*      base = nullptr;
    void*               bsTimerPtr = nullptr;
    float*              pGlobalTimeMult = nullptr;
    float*              pGlobalTimeTarget = nullptr;
    SetGlobalTimeMultFn fnSetGtm = nullptr;
    std::atomic<bool>   hitstopVerified{false};
    std::atomic<bool>   active{false};
} g_ci;

#ifndef RAW_SIM_PURE_TEST
static uint64_t NowQpcMs() {
    LARGE_INTEGER f{}, c{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return f.QuadPart > 0 ? uint64_t((c.QuadPart * 1000ll) / f.QuadPart) : GetTickCount64();
}

static bool SehReadFloat(const float* p, float* out) {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SehApplyGlobalTimeMult(float mult) {
    if (!g_ci.hitstopVerified.load(std::memory_order_acquire)) return;
    __try {
        if (g_ci.fnSetGtm && g_ci.bsTimerPtr) {
            g_ci.fnSetGtm(g_ci.bsTimerPtr, mult, 1u);
        } else {
            if (g_ci.pGlobalTimeTarget) *g_ci.pGlobalTimeTarget = mult;
            if (g_ci.pGlobalTimeMult)   *g_ci.pGlobalTimeMult   = mult;
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}
#endif

}  // namespace

#ifndef RAW_SIM_PURE_TEST
std::string CombatImpactInit(const Image& img) {
    std::lock_guard<std::mutex> lk(g_ci.mu);
    g_ci.decals.Clear();
    g_ci.hitstop = {};
    g_ci.base = img.base;
    g_ci.bsTimerPtr = nullptr;
    g_ci.pGlobalTimeMult = nullptr;
    g_ci.pGlobalTimeTarget = nullptr;
    g_ci.fnSetGtm = nullptr;
    g_ci.hitstopVerified.store(false, std::memory_order_release);
    g_ci.active.store(true, std::memory_order_release);

    if (!AreBinaryOffsetsVerified()) {
        return "[CombatImpact] decals=ACTIVE (32-slot) hitstop=LOCKED (fail-closed: startup Zydis verify failed)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[CombatImpact] decals=ACTIVE hitstop=skipped (non-CE1 or online profile)";
    }

    const uint32_t rvaFnSetGtm  = Addr("fn:BSTimer::SetGlobalTimeMultiplier");
    const uint32_t rvaTimer     = Addr("global:BSTimer::Singleton");
    const uint32_t rvaGtm       = Addr("global:GlobalTimeMultiplier");
    const uint32_t rvaGtmTarget = Addr("global:GlobalTimeMultiplierTarget");

    const bool codeOk = VerifyGlobalTimeMultiplierCodeSite(img, rvaFnSetGtm, rvaGtm, rvaGtmTarget);
    if (codeOk && rvaTimer && rvaGtm && rvaGtmTarget) {
        auto* mutBase = const_cast<uint8_t*>(img.base);
        g_ci.bsTimerPtr        = mutBase + rvaTimer;
        g_ci.pGlobalTimeMult   = reinterpret_cast<float*>(mutBase + rvaGtm);
        g_ci.pGlobalTimeTarget = reinterpret_cast<float*>(mutBase + rvaGtmTarget);
        g_ci.fnSetGtm          = reinterpret_cast<SetGlobalTimeMultFn>(mutBase + rvaFnSetGtm);
        g_ci.hitstopVerified.store(true, std::memory_order_release);
    }

    char buf[256];
    std::snprintf(buf, sizeof buf,
                  "[CombatImpact] decals=ACTIVE(32-slot RAW_GetImpactDecals) hitstop=%s (fn=0x%X mult=0x%X tgt=0x%X)",
                  codeOk ? "ACTIVE" : "LOCKED", rvaFnSetGtm, rvaGtm, rvaGtmTarget);
    return buf;
}

void CombatImpactOnHit(const float worldPos[3], ImpactDecalKind decalKind,
                       float radiusU, float intensity, HitstopEventKind hitstopKind,
                       bool involvesPlayer) {
    if (!g_ci.active.load(std::memory_order_acquire)) return;
    const uint64_t nowMs = NowQpcMs();
    bool applyNow = false;
    float multNow = 1.0f;

    {
        std::lock_guard<std::mutex> lk(g_ci.mu);
        if (worldPos) {
            // Deterministic pseudo-random rotation seed from spatial coordinates + push counter:
            const float seedAng = std::fmod(std::fabs(worldPos[0] * 0.13f + worldPos[1] * 0.29f +
                                                      float(g_ci.decals.totalPushed) * 1.6180339f),
                                            6.2831853f);
            const float lifeSec = (decalKind == ImpactDecalKind::ShockBurn) ? 18.0f :
                                  (decalKind == ImpactDecalKind::FireScorch ? 45.0f : 90.0f);
            g_ci.decals.Push(worldPos, radiusU, decalKind, nowMs, seedAng, intensity, lifeSec);
        }
        // Only engage tactile micro-time-dilation when player is directly involved (never stutter on distant NPC skirmishes):
        if (involvesPlayer && hitstopKind != HitstopEventKind::None &&
            g_ci.hitstopVerified.load(std::memory_order_acquire)) {
            float liveMult = 1.0f;
            if (SehReadFloat(g_ci.pGlobalTimeMult, &liveMult)) {
                if (TriggerHitstop(g_ci.hitstop, hitstopKind, nowMs, liveMult)) {
                    applyNow = true;
                    multNow  = g_ci.hitstop.appliedTimeScale;
                }
            }
        }
    }

    if (applyNow) {
        SehApplyGlobalTimeMult(multNow);
    }
}

void CombatImpactOnPresent(bool menuPaused) {
    if (!g_ci.active.load(std::memory_order_acquire)) return;
    if (!g_ci.hitstopVerified.load(std::memory_order_acquire)) return;

    const uint64_t nowMs = NowQpcMs();
    bool needWrite = false;
    float mult = 1.0f;
    {
        std::lock_guard<std::mutex> lk(g_ci.mu);
        StepHitstop(g_ci.hitstop, nowMs, menuPaused, &needWrite, &mult);
    }
    if (needWrite) {
        SehApplyGlobalTimeMult(mult);
    }
}

void CombatImpactShutdown() {
    g_ci.active.store(false, std::memory_order_release);
    bool wasActive = false;
    {
        std::lock_guard<std::mutex> lk(g_ci.mu);
        wasActive = g_ci.hitstop.active;
        g_ci.hitstop.active = false;
        g_ci.hitstop.appliedTimeScale = 1.0f;
        g_ci.decals.Clear();
    }
    if (wasActive && g_ci.hitstopVerified.load(std::memory_order_acquire)) {
        SehApplyGlobalTimeMult(1.0f);
    }
}

bool CombatImpactGetTelemetry(CombatImpactTelemetry* out) {
    if (!out) return false;
    const uint64_t nowMs = NowQpcMs();
    std::lock_guard<std::mutex> lk(g_ci.mu);
    out->hitstopVerified  = g_ci.hitstopVerified.load(std::memory_order_relaxed) ? 1u : 0u;
    out->hitstopActive    = g_ci.hitstop.active ? 1u : 0u;
    out->hitstopTriggers  = g_ci.hitstop.triggeredCount;
    out->hitstopSkipped   = g_ci.hitstop.skippedSlowTimeCount;
    out->decalsTotal      = g_ci.decals.totalPushed;
    out->currentTimeScale = g_ci.hitstop.appliedTimeScale;
    uint32_t liveCount = 0;
    for (const auto& s : g_ci.decals.slots) {
        if (s.active && nowMs >= s.birthMs && static_cast<float>(nowMs - s.birthMs) * 0.001f < s.maxLifeSec) {
            ++liveCount;
        }
    }
    out->decalsActiveNow = liveCount;
    return true;
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) uint32_t RAW_GetImpactDecals(float* outBuf8PerDecal, uint32_t maxCount) {
#ifndef RAW_SIM_PURE_TEST
    if (!outBuf8PerDecal || maxCount == 0u) return 0u;
    const uint64_t nowMs = raw::rt::NowQpcMs();
    std::lock_guard<std::mutex> lk(raw::rt::g_ci.mu);
    return raw::rt::g_ci.decals.PackForGpu(outBuf8PerDecal, maxCount, nowMs);
#else
    (void)outBuf8PerDecal;
    (void)maxCount;
    return 0u;
#endif
}
