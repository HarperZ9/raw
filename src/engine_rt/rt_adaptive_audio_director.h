#pragma once
// rt_adaptive_audio_director.h: Wave 51 Hysteresis-Smoothed Adaptive Acoustic & Soundscape Director (`raw::rt`).
// Fuses live weather, time-of-day, aurora, hearth warmth, water submersion, stealth tension, and
// combat intensity into anti-thrashing 8-mood crossfade weights (strictly summing to 1.0), DSP
// filter directives (lpfCutoffHz, reverbWetMult, dialogueDuckGain), and auto-clearing stingers.
// Zero per-frame heap allocations; zero external dependencies.
// Original work, MIT.

#include <array>
#include <cstdint>
#include <string>

namespace raw::rt {

enum class AudioMood : uint8_t {
    ExploreDay         = 0u,
    ExploreNightAurora = 1u,
    StormBlizzard      = 2u,
    TavernHearth       = 3u,
    StealthTension     = 4u,
    CombatSkirmish     = 5u,
    DragonBoss         = 6u,
    UnderwaterMuffle   = 7u,
    Count              = 8u,
};

enum class AudioStinger : uint8_t {
    None            = 0u,
    ParryDeflect    = 1u,
    GuardBreak      = 2u,
    KillConfirm     = 3u,
    DiscoveryAurora = 4u,
};

const char* AudioMoodName(AudioMood mood) noexcept;
const char* AudioStingerName(AudioStinger stinger) noexcept;
bool        IsPriorityInterruptMood(AudioMood mood) noexcept;

struct AudioDirectorInput {
    float gameHour          = 12.0f; // 0..24
    float rain01            = 0.0f;  // 0..1
    float snow01            = 0.0f;  // 0..1
    float windBeaufort      = 2.0f;  // 0..12
    float aurora01          = 0.0f;  // 0..1 northern lights intensity at night
    float hearthProximity01 = 0.0f;  // 0..1 nearby hearth/tavern fire
    float stealthTension01  = 0.0f;  // 0..1 active sneaking / enemy searching
    float combatIntensity01 = 0.0f;  // 0..1 active skirmish intensity
    bool  inInterior        = false;
    bool  underwater        = false;
    bool  dragonBossActive  = false;
    bool  dialogueActive    = false;
    bool  menuPaused        = false;
};

struct AudioDirectorConfig {
    float minHoldSeconds         = 4.0f;  // Minimum dwell time before non-interrupt mood switch
    float ambientDebounceSeconds = 1.25f; // Candidate persistence required for ambient transitions
    float ambientCrossfadeTauSec = 1.10f; // Smooth crossfade time constant for ambient moods
    float combatCrossfadeTauSec  = 0.30f; // Fast crossfade for CombatSkirmish / DragonBoss
    float underwaterTauSec       = 0.08f; // Immediate acoustic seal on submersion
    float duckTauSec             = 0.10f; // Smooth dialogue/menu ducking time constant
    float duckTargetGain         = 0.32f; // Target gain while dialogueActive or menuPaused (< 0.45)
};

// Exported C ABI state struct (RAW_GetAudioDirectorState).
struct RawAudioDirectorState {
    uint8_t  directorEnabled      = 1u;
    uint8_t  activeMood           = 0u;  // AudioMood 0..7
    uint8_t  pendingMood          = 0u;  // Candidate mood currently debouncing
    uint8_t  stingerPending       = 0u;  // AudioStinger 0..4
    float    moodWeights[8]{1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f}; // Strictly sums to 1.0
    float    lpfCutoffHz          = 18000.0f;
    float    reverbWetMult        = 0.85f;
    float    dialogueDuckGain     = 1.0f;
    float    timeInCurrentMoodSec = 0.0f;
    float    stingerRemainingSec  = 0.0f;
    uint32_t transitionCount      = 0u;
    uint32_t interruptCount       = 0u;
};

// Pure deterministic hysteresis-smoothed adaptive soundscape state machine (zero heap allocations).
class AdaptiveAudioDirector {
public:
    explicit AdaptiveAudioDirector(const AudioDirectorConfig& cfg = {}) noexcept;

    void Reset(AudioMood initialMood = AudioMood::ExploreDay) noexcept;
    void SetConfig(const AudioDirectorConfig& cfg) noexcept;

    // Classifies raw instantaneous mood candidate from environmental/gameplay inputs.
    [[nodiscard]] static AudioMood ClassifyCandidateMood(const AudioDirectorInput& in) noexcept;

    // Advances hysteresis, priority interrupts, weight crossfade, DSP filters, ducking, and stinger timer.
    void Step(float dtSeconds, const AudioDirectorInput& in) noexcept;

    // Pulses a one-shot musical/acoustic stinger that auto-clears after `holdSeconds`.
    void TriggerStinger(AudioStinger stinger, float holdSeconds = 0.45f) noexcept;

    [[nodiscard]] const RawAudioDirectorState& State() const noexcept { return m_state; }

private:
    AudioDirectorConfig   m_cfg{};
    RawAudioDirectorState m_state{};
    float                 m_candidateAccumSec  = 0.0f;
    float                 m_auroraCooldownSec  = 0.0f;
};

// Runtime lifecycle hooks (safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string AudioDirectorInit();
void        AudioDirectorStepDirect(float dtSec, const AudioDirectorInput& in);
void        AudioDirectorTriggerStinger(AudioStinger stinger, float holdSeconds = 0.45f);
void        AudioDirectorOnPresent(float dtSec);
void        AudioDirectorShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetAudioDirectorState(raw::rt::RawAudioDirectorState* out);
