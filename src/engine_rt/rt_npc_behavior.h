#pragma once
// rt_npc_behavior.h: Wave 49 Weather, Season, Time-of-Day & Economy-Driven NPC Daily Behavior &
// Crowd Ecology Controller (`raw::rt`).
// Couples live rt_world_sim (time, season, wind-chill, rain/snow/fog/thunder), rt_economy_ledger
// (regional scarcity), and rt_magic_physics (elemental weather synergy) across 6 NPC archetypes
// (Guard, Villager, Merchant, Hunter, Mage, Bandit). Zero per-frame heap allocations.
// Original work, MIT.

#include <array>
#include <cstdint>
#include <string>

namespace raw::rt {

enum class NpcArchetype : uint8_t {
    Guard    = 0u,
    Villager = 1u,
    Merchant = 2u,
    Hunter   = 3u,
    Mage     = 4u,
    Bandit   = 5u,
    Count    = 6u,
};

enum class RoutineDirective : uint8_t {
    WorkOutdoor = 0u,
    Patrol      = 1u,
    MarketStall = 2u,
    SeekShelter = 3u,
    HearthRest  = 4u,
    HuntTrack   = 5u,
    AmbushWatch = 6u,
};

enum class WorldSeason : uint8_t {
    Spring = 0u,
    Summer = 1u,
    Autumn = 2u,
    Winter = 3u,
};

enum class PreferredElement : uint8_t {
    Physical = 0u,
    Fire     = 1u,
    Frost    = 2u,
    Shock    = 3u,
};

const char* NpcArchetypeName(NpcArchetype arch) noexcept;
const char* RoutineDirectiveName(RoutineDirective dir) noexcept;
const char* PreferredElementName(PreferredElement elem) noexcept;
WorldSeason SeasonFromSkyrimMonth(float month0To11) noexcept;

struct NpcBehaviorEnvInput {
    float       gameHour     = 12.0f; // 0..24
    WorldSeason season       = WorldSeason::Summer;
    float       surfaceTempC = 15.0f; // ambient surface temperature in Celsius
    float       windBeaufort = 2.0f;  // 0..12 physical Beaufort scale
    float       rain01       = 0.0f;  // 0..1
    float       snow01       = 0.0f;  // 0..1
    float       fog01        = 0.0f;  // 0..1
    float       thunder01    = 0.0f;  // 0..1
    float       scarcity01   = 0.0f;  // 0..1 regional economy scarcity (from rt_economy_ledger)
    bool        inInterior   = false;
};

struct NpcArchetypeDirective {
    RoutineDirective directive        = RoutineDirective::WorkOutdoor;
    PreferredElement preferredElement = PreferredElement::Physical;
    bool             carryTorch       = false;
    bool             lightCampfire    = false;
    float            alertnessMult    = 1.0f;
    float            patrolRadiusMult = 1.0f;
    float            activity01       = 1.0f;
};

// C ABI summary struct exported via RAW_GetNpcBehaviorState.
struct RawNpcBehaviorState {
    uint8_t behaviorEnabled        = 1u;
    uint8_t season                 = 1u; // WorldSeason 0..3
    uint8_t preferredBanditElement = 0u; // 0=Phys, 1=Fire, 2=Frost, 3=Shock
    uint8_t preferredMageElement   = 1u; // 0=Phys, 1=Fire, 2=Frost, 3=Shock
    uint8_t activeDirective[6]{};        // RoutineDirective per NpcArchetype (0..5)
    uint8_t carryTorchFlags        = 0u; // bitmask (1u << NpcArchetype)
    uint8_t campfireFlags          = 0u; // bitmask (1u << NpcArchetype)
    float   effectiveWindChillC    = 15.0f;
    float   daylightStartHour      = 6.0f;
    float   daylightEndHour        = 20.0f;
    float   marketOpenHour         = 7.0f;
    float   marketCloseHour        = 20.0f;
    float   shelterPressure01      = 0.0f;
    float   torchDeploymentRatio   = 0.0f;
    float   guardAlertnessMult     = 1.0f;
    float   marketActivity01       = 1.0f;
    float   wildlifeActivity01     = 1.0f;
    float   hunterForageRangeMult  = 1.0f;
};

struct NpcBehaviorEvaluation {
    RawNpcBehaviorState                 summary{};
    std::array<NpcArchetypeDirective, 6> archetypes{};
};

// Pure deterministic environmental NPC behavior & crowd ecology evaluator (zero allocations).
NpcBehaviorEvaluation EvaluateNpcBehavior(const NpcBehaviorEnvInput& in) noexcept;

// Runtime lifecycle hooks (fast cached evaluation, safe in BOTH raw_engine.dll and RAW_SIM_PURE_TEST=1).
std::string NpcBehaviorInit();
void        NpcBehaviorUpdateEnv(const NpcBehaviorEnvInput& env);
void        NpcBehaviorOnPresent();
void        NpcBehaviorShutdown();

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetNpcBehaviorState(raw::rt::RawNpcBehaviorState* out);
