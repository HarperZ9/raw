// rt_adaptive_audio_director.cpp: Wave 51 Hysteresis-Smoothed Adaptive Acoustic & Soundscape Director (`raw::rt`).
// Original work, MIT.

#include "rt_adaptive_audio_director.h"

#include "rt_live_config.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_combat_ai.h"
#include "rt_stealth_survival.h"
#include "rt_view.h"
#include "rt_world_sim.h"
#endif

namespace raw::rt {
namespace {

inline float ClampF(float v, float lo, float hi) noexcept {
    if (!std::isfinite(v)) return lo;
    return v < lo ? lo : (v > hi ? hi : v);
}

struct MoodDspProfile {
    float lpfCutoffHz;
    float reverbWetMult;
};

constexpr MoodDspProfile kMoodProfiles[8] = {
    {18000.0f, 0.85f}, // ExploreDay
    {16500.0f, 1.10f}, // ExploreNightAurora
    { 4500.0f, 1.15f}, // StormBlizzard
    {11000.0f, 1.05f}, // TavernHearth
    { 9500.0f, 1.20f}, // StealthTension
    {17500.0f, 0.95f}, // CombatSkirmish
    {18000.0f, 1.25f}, // DragonBoss
    {  650.0f, 1.65f}, // UnderwaterMuffle
};

struct AudioDirectorRuntime {
    std::mutex            mu;
    bool                  initialized = false;
    AdaptiveAudioDirector director{};
    uint32_t              prevParries = 0u;
    uint32_t              prevGuardBreaks = 0u;
} g_dir;

}  // namespace

const char* AudioMoodName(AudioMood mood) noexcept {
    switch (mood) {
        case AudioMood::ExploreDay:         return "ExploreDay";
        case AudioMood::ExploreNightAurora: return "ExploreNightAurora";
        case AudioMood::StormBlizzard:      return "StormBlizzard";
        case AudioMood::TavernHearth:       return "TavernHearth";
        case AudioMood::StealthTension:     return "StealthTension";
        case AudioMood::CombatSkirmish:     return "CombatSkirmish";
        case AudioMood::DragonBoss:         return "DragonBoss";
        case AudioMood::UnderwaterMuffle:   return "UnderwaterMuffle";
        default:                            return "Unknown";
    }
}

const char* AudioStingerName(AudioStinger stinger) noexcept {
    switch (stinger) {
        case AudioStinger::None:            return "None";
        case AudioStinger::ParryDeflect:    return "ParryDeflect";
        case AudioStinger::GuardBreak:      return "GuardBreak";
        case AudioStinger::KillConfirm:     return "KillConfirm";
        case AudioStinger::DiscoveryAurora: return "DiscoveryAurora";
        default:                            return "None";
    }
}

bool IsPriorityInterruptMood(AudioMood mood) noexcept {
    return mood == AudioMood::UnderwaterMuffle ||
           mood == AudioMood::DragonBoss ||
           mood == AudioMood::CombatSkirmish;
}

AdaptiveAudioDirector::AdaptiveAudioDirector(const AudioDirectorConfig& cfg) noexcept
    : m_cfg(cfg) {
    Reset(AudioMood::ExploreDay);
}

void AdaptiveAudioDirector::SetConfig(const AudioDirectorConfig& cfg) noexcept {
    m_cfg = cfg;
    m_cfg.minHoldSeconds         = ClampF(m_cfg.minHoldSeconds,         0.2f, 30.0f);
    m_cfg.ambientDebounceSeconds = ClampF(m_cfg.ambientDebounceSeconds, 0.0f, 10.0f);
    m_cfg.ambientCrossfadeTauSec = ClampF(m_cfg.ambientCrossfadeTauSec, 0.05f, 10.0f);
    m_cfg.combatCrossfadeTauSec  = ClampF(m_cfg.combatCrossfadeTauSec,  0.02f, 3.0f);
    m_cfg.underwaterTauSec       = ClampF(m_cfg.underwaterTauSec,       0.01f, 1.0f);
    m_cfg.duckTauSec             = ClampF(m_cfg.duckTauSec,             0.01f, 2.0f);
    m_cfg.duckTargetGain         = ClampF(m_cfg.duckTargetGain,         0.05f, 1.0f);
}

void AdaptiveAudioDirector::Reset(AudioMood initialMood) noexcept {
    const uint8_t idx = (static_cast<uint8_t>(initialMood) < 8u) ? static_cast<uint8_t>(initialMood) : 0u;
    m_state = {};
    m_state.directorEnabled = 1u;
    m_state.activeMood      = idx;
    m_state.pendingMood     = idx;
    m_state.stingerPending  = 0u;
    for (uint8_t i = 0u; i < 8u; ++i) {
        m_state.moodWeights[i] = (i == idx) ? 1.0f : 0.0f;
    }
    m_state.lpfCutoffHz          = kMoodProfiles[idx].lpfCutoffHz;
    m_state.reverbWetMult        = kMoodProfiles[idx].reverbWetMult;
    m_state.dialogueDuckGain     = 1.0f;
    m_state.timeInCurrentMoodSec = 0.0f;
    m_state.stingerRemainingSec  = 0.0f;
    m_state.transitionCount      = 0u;
    m_state.interruptCount       = 0u;
    m_candidateAccumSec          = 0.0f;
    m_auroraCooldownSec          = 0.0f;
}

AudioMood AdaptiveAudioDirector::ClassifyCandidateMood(const AudioDirectorInput& in) noexcept {
    // Strict acoustic priority cascade:
    // 1. Physical submersion (UnderwaterMuffle)
    if (in.underwater) return AudioMood::UnderwaterMuffle;
    // 2. Dragon boss / active combat
    if (in.dragonBossActive) return AudioMood::DragonBoss;
    if (in.combatIntensity01 >= 0.45f) return AudioMood::CombatSkirmish;
    // 3. Stealth tension
    if (in.stealthTension01 >= 0.50f) return AudioMood::StealthTension;
    // 4. Warm interior hearth / tavern
    if (in.inInterior && in.hearthProximity01 >= 0.40f) return AudioMood::TavernHearth;
    // 5. Exterior severe storm or blizzard
    if (!in.inInterior && (in.snow01 >= 0.55f || in.rain01 >= 0.60f || in.windBeaufort >= 7.5f)) {
        return AudioMood::StormBlizzard;
    }
    // 6. Nighttime / aurora exploration
    float hour = std::isfinite(in.gameHour) ? std::fmod(in.gameHour, 24.0f) : 12.0f;
    if (hour < 0.0f) hour += 24.0f;
    const bool isNight = (hour < 5.5f || hour >= 20.5f);
    if (isNight || in.aurora01 >= 0.40f) return AudioMood::ExploreNightAurora;
    // 7. Clear daytime exploration
    return AudioMood::ExploreDay;
}

void AdaptiveAudioDirector::TriggerStinger(AudioStinger stinger, float holdSeconds) noexcept {
    if (stinger == AudioStinger::None) return;
    m_state.stingerPending      = static_cast<uint8_t>(stinger);
    m_state.stingerRemainingSec = ClampF(holdSeconds, 0.05f, 5.0f);
}

void AdaptiveAudioDirector::Step(float dtSeconds, const AudioDirectorInput& in) noexcept {
    const float dt = ClampF(dtSeconds, 0.0f, 0.5f);
    if (dt <= 0.0f) return;

    m_state.timeInCurrentMoodSec += dt;
    if (m_auroraCooldownSec > 0.0f) {
        m_auroraCooldownSec = std::max(0.0f, m_auroraCooldownSec - dt);
    }

    // 1. Stinger pulse auto-clear countdown
    if (m_state.stingerPending != 0u) {
        m_state.stingerRemainingSec -= dt;
        if (m_state.stingerRemainingSec <= 0.0f) {
            m_state.stingerRemainingSec = 0.0f;
            m_state.stingerPending      = 0u;
        }
    }

    // 2. Hysteresis & priority-interrupt state transition logic
    const AudioMood curMood  = static_cast<AudioMood>(m_state.activeMood);
    const AudioMood candMood = ClassifyCandidateMood(in);

    if (candMood == curMood) {
        m_state.pendingMood = m_state.activeMood;
        m_candidateAccumSec = 0.0f;
    } else {
        const bool candInterrupt = IsPriorityInterruptMood(candMood);
        const bool curInterrupt  = IsPriorityInterruptMood(curMood);

        // Priority interrupts (UnderwaterMuffle, DragonBoss, CombatSkirmish) override immediately!
        // Leaving UnderwaterMuffle also responds quickly once surfaced (0.25s debounce).
        bool commitSwitch = false;
        if (candInterrupt && (!curInterrupt || static_cast<uint8_t>(candMood) > static_cast<uint8_t>(curMood))) {
            commitSwitch = true;
            ++m_state.interruptCount;
        } else {
            if (m_state.pendingMood == static_cast<uint8_t>(candMood)) {
                m_candidateAccumSec += dt;
            } else {
                m_state.pendingMood = static_cast<uint8_t>(candMood);
                m_candidateAccumSec = dt;
            }

            if (curMood == AudioMood::UnderwaterMuffle) {
                commitSwitch = (m_candidateAccumSec >= 0.25f);
            } else {
                const bool heldLongEnough = (m_state.timeInCurrentMoodSec >= m_cfg.minHoldSeconds);
                const bool debounced      = (m_candidateAccumSec >= m_cfg.ambientDebounceSeconds);
                commitSwitch = heldLongEnough && debounced;
            }
        }

        if (commitSwitch) {
            m_state.activeMood           = static_cast<uint8_t>(candMood);
            m_state.pendingMood          = m_state.activeMood;
            m_state.timeInCurrentMoodSec = 0.0f;
            m_candidateAccumSec          = 0.0f;
            ++m_state.transitionCount;

            if (candMood == AudioMood::ExploreNightAurora && in.aurora01 >= 0.65f && m_auroraCooldownSec <= 0.0f) {
                TriggerStinger(AudioStinger::DiscoveryAurora, 0.85f);
                m_auroraCooldownSec = 60.0f;
            }
        }
    }

    // 3. Smooth exponential weight crossfade with exact unity sum conservation (sum == 1.0)
    const AudioMood active = static_cast<AudioMood>(m_state.activeMood);
    const float tau = (active == AudioMood::UnderwaterMuffle) ? m_cfg.underwaterTauSec
                    : (active == AudioMood::CombatSkirmish || active == AudioMood::DragonBoss)
                          ? m_cfg.combatCrossfadeTauSec
                          : m_cfg.ambientCrossfadeTauSec;
    const float alpha = 1.0f - std::exp(-dt / std::max(0.01f, tau));

    float sumW = 0.0f;
    for (uint8_t i = 0u; i < 8u; ++i) {
        const float target = (i == m_state.activeMood) ? 1.0f : 0.0f;
        float w = m_state.moodWeights[i] + (target - m_state.moodWeights[i]) * alpha;
        w = ClampF(w, 0.0f, 1.0f);
        m_state.moodWeights[i] = w;
        sumW += w;
    }
    if (sumW > 1.0e-6f) {
        const float invSum = 1.0f / sumW;
        for (uint8_t i = 0u; i < 8u; ++i) {
            m_state.moodWeights[i] *= invSum;
        }
    } else {
        for (uint8_t i = 0u; i < 8u; ++i) m_state.moodWeights[i] = 0.0f;
        m_state.moodWeights[m_state.activeMood] = 1.0f;
    }

    // 4. Compute blended DSP directives (lpfCutoffHz & reverbWetMult); immediate seal when submerged!
    if (active == AudioMood::UnderwaterMuffle) {
        m_state.lpfCutoffHz   = 650.0f;
        m_state.reverbWetMult = 1.65f;
    } else {
        float lpf = 0.0f;
        float wet = 0.0f;
        for (uint8_t i = 0u; i < 8u; ++i) {
            lpf += m_state.moodWeights[i] * kMoodProfiles[i].lpfCutoffHz;
            wet += m_state.moodWeights[i] * kMoodProfiles[i].reverbWetMult;
        }
        m_state.lpfCutoffHz   = ClampF(lpf, 400.0f, 20000.0f);
        m_state.reverbWetMult = ClampF(wet, 0.25f, 2.0f);
    }

    // 5. Dialogue / Menu smooth ducking
    const float targetDuck = (in.dialogueActive || in.menuPaused) ? m_cfg.duckTargetGain : 1.0f;
    const float duckAlpha  = 1.0f - std::exp(-dt / std::max(0.01f, m_cfg.duckTauSec));
    m_state.dialogueDuckGain = ClampF(m_state.dialogueDuckGain + (targetDuck - m_state.dialogueDuckGain) * duckAlpha,
                                      0.05f, 1.0f);
}

// --- Runtime Lifecycle & C ABI ---

std::string AudioDirectorInit() {
    const LiveEngineConfig cfg = GetLiveConfig();
    {
        std::lock_guard<std::mutex> lk(g_dir.mu);
        g_dir.initialized     = true;
        g_dir.prevParries     = 0u;
        g_dir.prevGuardBreaks = 0u;
        g_dir.director.Reset(AudioMood::ExploreDay);
    }
    char buf[192];
    std::snprintf(buf, sizeof(buf),
                  "[AudioDirector] initialized (AdaptiveAudioDirector=%u, 8 moods, minHold=4.0s, zero-alloc)",
                  cfg.adaptiveAudioDirector ? 1u : 0u);
    return buf;
}

void AudioDirectorStepDirect(float dtSec, const AudioDirectorInput& in) {
    if (!GetLiveConfig().adaptiveAudioDirector) return;
    std::lock_guard<std::mutex> lk(g_dir.mu);
    if (!g_dir.initialized) g_dir.initialized = true;
    g_dir.director.Step(dtSec, in);
}

void AudioDirectorTriggerStinger(AudioStinger stinger, float holdSeconds) {
    if (!GetLiveConfig().adaptiveAudioDirector) return;
    std::lock_guard<std::mutex> lk(g_dir.mu);
    g_dir.director.TriggerStinger(stinger, holdSeconds);
}

void AudioDirectorOnPresent(float dtSec) {
    if (!GetLiveConfig().adaptiveAudioDirector) return;
#ifndef RAW_SIM_PURE_TEST
    AudioDirectorInput in{};
    EngineView ev{};
    if (RAW_EngineView(&ev) > 0u) {
        in.gameHour   = ev.gameHour;
        in.inInterior = (ev.interior != 0u);
        in.menuPaused = (ev.menuPaused != 0u);
        const bool waterValid = std::isfinite(ev.waterHeight) && (ev.waterHeight > -1.0e6f) && (ev.waterHeight < 1.0e6f);
        in.underwater = waterValid && ((ev.playerPos[2] + 95.0f) < ev.waterHeight);
    }

    RawWorldSimTelemetry ws{};
    if (RAW_GetWorldSimTelemetry(&ws)) {
        in.rain01       = ws.synopticRain;
        in.snow01       = ws.synopticSnow;
        in.windBeaufort = ws.beaufortScale;
        if (ws.acousticEnvClass == 4u) in.underwater = true;
        // High-latitude clear-sky night aurora probability proxy
        const bool night = (in.gameHour < 5.5f || in.gameHour >= 20.5f);
        in.aurora01 = (!in.inInterior && night && ws.synopticCloud < 0.45f) ? (0.75f * (1.0f - ws.synopticCloud)) : 0.0f;
        in.combatIntensity01 = (ws.attackTokensUsed > 0u) ? 0.75f : 0.0f;
    }

    StealthSurvivalTelemetry ss{};
    if (StealthSurvivalGetTelemetry(&ss)) {
        in.hearthProximity01 = ss.hearthProximity01;
        if (ss.sneakChecksTotal > 0u && in.combatIntensity01 < 0.1f) {
            in.stealthTension01 = 0.25f;
        }
    }

    GameplayHookTelemetry gp{};
    AudioStinger autoStinger = AudioStinger::None;
    if (GameplayHooksGetStats(&gp)) {
        std::lock_guard<std::mutex> lk(g_dir.mu);
        if (gp.timedParries > g_dir.prevParries)       autoStinger = AudioStinger::ParryDeflect;
        else if (gp.guardBreaks > g_dir.prevGuardBreaks) autoStinger = AudioStinger::GuardBreak;
        g_dir.prevParries     = gp.timedParries;
        g_dir.prevGuardBreaks = gp.guardBreaks;
    }

    {
        std::lock_guard<std::mutex> lk(g_dir.mu);
        if (!g_dir.initialized) g_dir.initialized = true;
        if (autoStinger != AudioStinger::None) {
            g_dir.director.TriggerStinger(autoStinger, 0.45f);
        }
        g_dir.director.Step(dtSec, in);
    }
#else
    (void)dtSec;
#endif
}

void AudioDirectorShutdown() {
    std::lock_guard<std::mutex> lk(g_dir.mu);
    g_dir.initialized = false;
    g_dir.director.Reset(AudioMood::ExploreDay);
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetAudioDirectorState(raw::rt::RawAudioDirectorState* out) {
    if (!out) return 0;
    const raw::rt::LiveEngineConfig cfg = raw::rt::GetLiveConfig();
    std::lock_guard<std::mutex> lk(raw::rt::g_dir.mu);
    *out = raw::rt::g_dir.director.State();
    out->directorEnabled = cfg.adaptiveAudioDirector ? 1u : 0u;
    return raw::rt::g_dir.initialized ? 1 : 0;
}
