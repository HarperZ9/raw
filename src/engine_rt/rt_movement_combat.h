#pragma once
// rt_movement_combat.h: Wave 43 Live Creation Engine 1.7.104.0 Tactile Directional Dodge-Step
// with 260ms i-Frame Evasion + Smooth Critically-Damped Combat/Sprint Camera FOV Breath.
// 100% binary-verified (PlayerCamera::Ctor 0x8F79F0 -> worldFOV +0x13C, firstPersonFOV +0x140,
// PlayerCamera::Singleton 0x31A5478, ActorState::actorState1 +0xC8 bit 0x100 sprinting),
// Zydis-checked, fail-closed, SEH-guarded (Zero SKSE).
// Original work, MIT.

#include <cstdint>
#include <string>

namespace raw::rt {
struct Image;

// =================================================================================================
// 1. Tactile Directional Dodge-Step & 260ms Wall-Clock i-Frame Evasion Controller
// =================================================================================================
inline constexpr float    kDodgeStaminaCost       = 18.0f;
inline constexpr uint64_t kDodgeIFrameDurationMs  = 260ull;
inline constexpr uint64_t kDodgeCooldownMs        = 550ull;
inline constexpr uint64_t kDodgeDoubleTapWindowMs = 260ull;
inline constexpr uint64_t kDodgeEvasionFeelMs     = 35ull;

enum class DodgeDirection : uint8_t {
    None    = 0,
    Forward = 1,
    Back    = 2,
    Left    = 3,
    Right   = 4,
};

struct DodgeTriggerInput {
    uint64_t nowMs          = 0;
    float    currentStamina = 100.0f;
    bool     menuPaused     = false;
    bool     altEdge        = false;       // Rising edge on Alt / Left-Alt key
    bool     wasdEdge[4]    = {};          // [W, S, A, D] rising edges for quick double-tap dodge
    bool     wasdHeld[4]    = {};          // [W, S, A, D] held state for directional selection
};

struct DodgeTriggerResult {
    bool           triggered        = false;
    bool           rejectedStamina  = false;
    bool           rejectedCooldown = false;
    float          staminaCost      = 0.0f;
    DodgeDirection direction        = DodgeDirection::None;
};

struct DodgeControllerState {
    bool           evadingNow             = false;
    uint64_t       dodgeStartMs           = 0;
    uint64_t       iFrameEndMs            = 0;
    uint64_t       cooldownEndMs          = 0;
    uint64_t       evasionFeelEndMs       = 0;
    uint64_t       lastWasdTapMs[4]       = {};
    DodgeDirection activeDir              = DodgeDirection::None;
    uint32_t       dodgesTriggered        = 0;
    uint32_t       dodgesRejectedStamina  = 0;
    uint32_t       dodgesRejectedCooldown = 0;
    uint32_t       evadedHits             = 0;
};

DodgeTriggerResult StepDodgeController(DodgeControllerState& st, const DodgeTriggerInput& in);
bool IsDodgeEvadingAt(const DodgeControllerState& st, uint64_t nowMs);
bool IsDodgeEvasionFeelActive(const DodgeControllerState& st, uint64_t nowMs);

// Pure deterministic hit-evasion resolver called from HookHitDataPopulate (0x7F09D0) & unit tests.
// When i-frames are active, zeroes totalDamage & physicalDamage, sets percentBlocked = 1.0f,
// opens the crisp 35 ms evasion feel window, increments evadedHits, and returns true.
bool TryEvadeIncomingHit(DodgeControllerState& st, uint64_t nowMs,
                         float* inOutTotalDamage, float* inOutPhysicalDamage, float* inOutPercentBlocked);

// =================================================================================================
// 2. Smooth Critically-Damped Combat / Sprint Camera FOV Breath
// =================================================================================================
inline constexpr float kFovSprintOffsetDeg      = 4.2f;
inline constexpr float kFovDodgeCombatOffsetDeg = 2.6f;
inline constexpr float kFovEvasionPulseDeg      = 0.8f;
inline constexpr float kFovSpringOmega          = 14.0f;

struct FovSpringState {
    bool     initialized         = false;
    bool     hasWrittenLive      = false;
    float    baselineFovDeg      = 80.0f;
    float    currentOffsetDeg    = 0.0f;
    float    velocityDegPerSec   = 0.0f;
    float    lastWrittenFovDeg   = 80.0f;
    uint32_t baselineResyncCount = 0;
};

struct FovStepInput {
    float liveReadFovDeg        = 80.0f;
    float dtSec                 = 1.0f / 60.0f;
    bool  sprinting             = false;
    bool  dodging               = false;
    bool  inCombatOrWeaponDrawn = false;
    bool  evasionFeelActive     = false;
    bool  menuPaused            = false;
};

struct FovStepOutput {
    float targetOffsetDeg    = 0.0f;
    float appliedOffsetDeg   = 0.0f;
    float desiredWorldFovDeg = 80.0f;
    bool  needWrite          = false;
    bool  baselineUpdated    = false;
};

FovStepOutput StepFovBreathSpring(FovSpringState& st, const FovStepInput& in);

// =================================================================================================
// 3. Zydis Binary Verification of PlayerCamera::Ctor (0x8F79F0 -> Singleton, +0x13C, +0x140)
// =================================================================================================
bool VerifyPlayerCameraFovCodeSite(const Image& img, uint32_t fnCtorRva,
                                   uint32_t expectedSingletonRva,
                                   uint32_t expectedWorldFovOff,
                                   uint32_t expectedFirstPersonFovOff);

// =================================================================================================
// 4. Live Engine Runtime Lifecycle & HitData Evasion Hook Bridge
// =================================================================================================
struct MovementCombatTelemetry {
    uint8_t  verified           = 0;
    uint8_t  evadingNow         = 0;
    uint8_t  sprintingNow       = 0;
    uint8_t  weaponDrawnNow     = 0;
    uint32_t dodgesTriggered    = 0;
    uint32_t dodgesRejectedStam = 0;
    uint32_t evadedHits         = 0;
    uint32_t fovBaselineResyncs = 0;
    float    baselineFovDeg     = 80.0f;
    float    appliedFovOffset   = 0.0f;
};

#ifndef RAW_SIM_PURE_TEST
std::string MovementCombatInit(const Image& img);
void        MovementCombatOnTick();
void        MovementCombatShutdown();
bool        MovementCombatIsPlayerEvading();
bool        MovementCombatTryEvadePlayerHit(float* inOutTotalDmg, float* inOutPhysDmg, float* inOutPctBlocked);
bool        MovementCombatGetTelemetry(MovementCombatTelemetry* out);
#endif

}  // namespace raw::rt
