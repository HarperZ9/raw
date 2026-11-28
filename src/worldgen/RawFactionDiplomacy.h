#pragma once
// RawFactionDiplomacy.h: Clean-Room 9-Hold Jurisdiction & 6-Faction Reputation, Bounty Ledger & Civil-War
// Diplomacy Engine (`raw::worldgen`). Pure portable C++23 with zero Windows or D3D12 headers.
// Models:
//   - All 9 Skyrim Hold jurisdictions (`HoldId`) + 6 Major Factions (`ImperialLegion`, `Stormcloaks`,
//     `Companions`, `CollegeOfWinterhold`, `ThievesGuild`, `DarkBrotherhood`)
//   - Civil-war & faction alliance/rival spillover matrix strictly clamped to [-100..+100]
//   - Direct `ApplyEventReward(WorldEventKind, HoldId)` integration with `RawWorldEventDirector`
//   - Witnessed vs stealth-unwitnessed crime bounty ledger, guard posture escalation, merchant pricing
//     (`0.82..1.35`), bounty hunter risk, legal `PayBounty` amnesty, and slow multi-week notoriety decay.
// Original work, MIT.

#include "RawSettlementSim.h"
#include "RawWorldEventDirector.h"

#include <array>
#include <cstdint>

namespace raw::worldgen {

enum class FactionId : uint8_t {
    ImperialLegion      = 0,
    Stormcloaks         = 1,
    Companions          = 2,
    CollegeOfWinterhold = 3,
    ThievesGuild        = 4,
    DarkBrotherhood     = 5,
    Count               = 6,
};

inline constexpr size_t kFactionCount = static_cast<size_t>(FactionId::Count);

enum class GuardPosture : uint8_t {
    Peaceful      = 0,
    Watchful      = 1,
    ArrestOnSight = 2,
    KillOnSight   = 3,
};

enum class CrimeSeverity : uint8_t {
    PettyTheft = 0,
    Trespass   = 1,
    GrandTheft = 2,
    Assault    = 3,
    Murder     = 4,
    Treason    = 5,
};

struct HoldStanding {
    HoldId       id                 = HoldId::Whiterun;
    float        reputation         = 0.0f;  // [-100..+100]
    float        bountyGold         = 0.0f;  // >= 0
    GuardPosture guardPosture       = GuardPosture::Peaceful;
    float        merchantPriceMult  = 1.0f;  // [0.82..1.35]
    float        bountyHunterRisk01 = 0.0f;  // [0..1]
};

struct FactionStanding {
    FactionId id         = FactionId::ImperialLegion;
    float     reputation = 0.0f; // [-100..+100]
};

struct DiplomacyTelemetry {
    float    meanHoldReputation = 0.0f;
    double   totalBountyGold    = 0.0;
    uint32_t hostileHoldCount   = 0u; // Holds at ArrestOnSight or KillOnSight
    HoldId   mostFriendlyHold   = HoldId::Whiterun;
    HoldId   mostHostileHold    = HoldId::Whiterun;
};

const char* GetFactionName(FactionId id);
const char* GetGuardPostureName(GuardPosture posture);

class RawFactionDiplomacy {
public:
    RawFactionDiplomacy();

    void Reset();

    // Direct hold or faction standing adjustments (faction adjustments trigger alliance/rival spillover):
    bool AdjustHoldReputation(HoldId hold, float delta);
    bool AdjustFactionReputation(FactionId faction, float delta);

    // Rewards resolving an emergent RawWorldEventDirector event in `hold`:
    bool ApplyEventReward(WorldEventKind kind, HoldId hold);

    // Records a crime in `hold`. Unwitnessed stealth crimes add 0 bounty and 0 public notoriety!
    // Returns bounty gold added (0 if unwitnessed or invalid hold).
    float RecordCrime(HoldId hold, CrimeSeverity severity, bool witnessed);

    // Pays off full bounty in `hold` if `availableGold >= bountyGold`, resetting hostile guard posture.
    bool PayBounty(HoldId hold, float availableGold, float* outPaidGold = nullptr);

    // Decays mild notoriety slowly over multi-week time spans (zero/negative days = exact no-op).
    void StepDays(float days);

    const HoldStanding*    GetHoldStanding(HoldId hold) const;
    const FactionStanding* GetFactionStanding(FactionId faction) const;

    DiplomacyTelemetry ComputeTelemetry() const;

private:
    void RecomputeHoldDerived(HoldStanding& hs);
    void RecomputeAllDerived();

    std::array<HoldStanding, kHoldCount>       holds_{};
    std::array<FactionStanding, kFactionCount> factions_{};
};

}  // namespace raw::worldgen
