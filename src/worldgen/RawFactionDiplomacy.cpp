// RawFactionDiplomacy.cpp: Clean-Room 9-Hold Jurisdiction & 6-Faction Reputation, Bounty Ledger
// & Civil-War Diplomacy Engine. Original work, MIT. Zero Windows / D3D12 headers.

#include "RawFactionDiplomacy.h"

#include <algorithm>
#include <cmath>

namespace raw::worldgen {
namespace {

inline float ClampF(float v, float lo, float hi) noexcept {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

inline float Clamp01F(float v) noexcept {
    return ClampF(v, 0.0f, 1.0f);
}

constexpr std::array<const char*, kFactionCount> kFactionNames = {
    "ImperialLegion",
    "Stormcloaks",
    "Companions",
    "CollegeOfWinterhold",
    "ThievesGuild",
    "DarkBrotherhood",
};

constexpr std::array<const char*, 4> kPostureNames = {
    "Peaceful", "Watchful", "ArrestOnSight", "KillOnSight"
};

}  // namespace

const char* GetFactionName(FactionId id) {
    const size_t idx = static_cast<size_t>(id);
    return (idx < kFactionCount) ? kFactionNames[idx] : "UnknownFaction";
}

const char* GetGuardPostureName(GuardPosture posture) {
    const size_t idx = static_cast<size_t>(posture);
    return (idx < kPostureNames.size()) ? kPostureNames[idx] : "UnknownPosture";
}

RawFactionDiplomacy::RawFactionDiplomacy() {
    Reset();
}

void RawFactionDiplomacy::Reset() {
    for (size_t i = 0u; i < kHoldCount; ++i) {
        holds_[i]    = HoldStanding{};
        holds_[i].id = static_cast<HoldId>(i);
    }
    for (size_t i = 0u; i < kFactionCount; ++i) {
        factions_[i]    = FactionStanding{};
        factions_[i].id = static_cast<FactionId>(i);
    }
    RecomputeAllDerived();
}

const HoldStanding* RawFactionDiplomacy::GetHoldStanding(HoldId hold) const {
    const size_t idx = static_cast<size_t>(hold);
    return (idx < kHoldCount) ? &holds_[idx] : nullptr;
}

const FactionStanding* RawFactionDiplomacy::GetFactionStanding(FactionId faction) const {
    const size_t idx = static_cast<size_t>(faction);
    return (idx < kFactionCount) ? &factions_[idx] : nullptr;
}

void RawFactionDiplomacy::RecomputeHoldDerived(HoldStanding& hs) {
    hs.reputation = ClampF(std::isfinite(hs.reputation) ? hs.reputation : 0.0f, -100.0f, 100.0f);
    hs.bountyGold = std::max(0.0f, std::isfinite(hs.bountyGold) ? hs.bountyGold : 0.0f);

    // Guard posture escalation from bounty ledger + deep hostility:
    if (hs.bountyGold >= 2500.0f || hs.reputation <= -85.0f) {
        hs.guardPosture = GuardPosture::KillOnSight;
    } else if (hs.bountyGold >= 500.0f || hs.reputation <= -65.0f) {
        hs.guardPosture = GuardPosture::ArrestOnSight;
    } else if (hs.bountyGold >= 100.0f || hs.reputation <= -35.0f) {
        hs.guardPosture = GuardPosture::Watchful;
    } else {
        hs.guardPosture = GuardPosture::Peaceful;
    }

    // Merchant price multiplier [0.82..1.35] smoothly driven by reputation & bounty:
    const float repTerm    = (hs.reputation >= 0.0f)
        ? (-0.0022f * hs.reputation)
        : (-0.0024f * hs.reputation);
    const float bountyTerm = 0.16f * Clamp01F(hs.bountyGold / 2500.0f);
    hs.merchantPriceMult   = ClampF(1.0f + repTerm + bountyTerm, 0.82f, 1.35f);

    // Bounty hunter dispatch risk [0..1]:
    const float bountyRisk = 0.85f * Clamp01F(hs.bountyGold / 2500.0f);
    const float repRisk    = (hs.reputation < -40.0f) ? (0.25f * Clamp01F((-hs.reputation - 40.0f) / 60.0f)) : 0.0f;
    hs.bountyHunterRisk01  = Clamp01F(bountyRisk + repRisk);
}

void RawFactionDiplomacy::RecomputeAllDerived() {
    for (HoldStanding& hs : holds_) {
        RecomputeHoldDerived(hs);
    }
}

bool RawFactionDiplomacy::AdjustHoldReputation(HoldId hold, float delta) {
    const size_t idx = static_cast<size_t>(hold);
    if (idx >= kHoldCount || !std::isfinite(delta)) {
        return false;
    }
    holds_[idx].reputation = ClampF(holds_[idx].reputation + delta, -100.0f, 100.0f);
    RecomputeHoldDerived(holds_[idx]);
    return true;
}

bool RawFactionDiplomacy::AdjustFactionReputation(FactionId faction, float delta) {
    const size_t fIdx = static_cast<size_t>(faction);
    if (fIdx >= kFactionCount || !std::isfinite(delta)) {
        return false;
    }

    factions_[fIdx].reputation = ClampF(factions_[fIdx].reputation + delta, -100.0f, 100.0f);

    auto bumpFaction = [&](FactionId f, float d) {
        const size_t i = static_cast<size_t>(f);
        factions_[i].reputation = ClampF(factions_[i].reputation + d, -100.0f, 100.0f);
    };
    auto bumpHold = [&](HoldId h, float d) {
        const size_t i = static_cast<size_t>(h);
        holds_[i].reputation = ClampF(holds_[i].reputation + d, -100.0f, 100.0f);
    };

    // Civil-war & Tamriel faction alliance/rival spillover matrix:
    switch (faction) {
        case FactionId::ImperialLegion:
            bumpFaction(FactionId::Stormcloaks, -0.55f * delta);
            bumpHold(HoldId::Solitude,   0.50f * delta);
            bumpHold(HoldId::Markarth,   0.35f * delta);
            bumpHold(HoldId::Falkreath,  0.35f * delta);
            bumpHold(HoldId::Morthal,    0.25f * delta);
            bumpHold(HoldId::Windhelm,  -0.40f * delta);
            bumpHold(HoldId::Dawnstar,  -0.25f * delta);
            bumpHold(HoldId::Winterhold,-0.25f * delta);
            bumpHold(HoldId::Riften,    -0.25f * delta);
            break;

        case FactionId::Stormcloaks:
            bumpFaction(FactionId::ImperialLegion, -0.55f * delta);
            bumpHold(HoldId::Windhelm,   0.50f * delta);
            bumpHold(HoldId::Dawnstar,   0.35f * delta);
            bumpHold(HoldId::Winterhold, 0.35f * delta);
            bumpHold(HoldId::Riften,     0.35f * delta);
            bumpHold(HoldId::Solitude,  -0.40f * delta);
            bumpHold(HoldId::Markarth,  -0.25f * delta);
            bumpHold(HoldId::Falkreath, -0.25f * delta);
            bumpHold(HoldId::Morthal,   -0.20f * delta);
            break;

        case FactionId::Companions:
            bumpHold(HoldId::Whiterun, 0.45f * delta);
            bumpFaction(FactionId::ThievesGuild,    -0.12f * delta);
            bumpFaction(FactionId::DarkBrotherhood, -0.18f * delta);
            break;

        case FactionId::CollegeOfWinterhold:
            bumpHold(HoldId::Winterhold, 0.55f * delta);
            bumpHold(HoldId::Morthal,    0.20f * delta);
            break;

        case FactionId::ThievesGuild:
            bumpHold(HoldId::Riften, 0.25f * delta);
            bumpFaction(FactionId::ImperialLegion, -0.12f * delta);
            bumpFaction(FactionId::Stormcloaks,    -0.12f * delta);
            break;

        case FactionId::DarkBrotherhood:
            bumpHold(HoldId::Falkreath, 0.10f * delta);
            bumpFaction(FactionId::ImperialLegion, -0.20f * delta);
            bumpFaction(FactionId::Companions,     -0.15f * delta);
            break;

        default:
            break;
    }

    RecomputeAllDerived();
    return true;
}

bool RawFactionDiplomacy::ApplyEventReward(WorldEventKind kind, HoldId hold) {
    if (static_cast<size_t>(hold) >= kHoldCount ||
        static_cast<size_t>(kind) >= kWorldEventKindCount) {
        return false;
    }

    switch (kind) {
        case WorldEventKind::BlizzardCaravanRescue:
            AdjustHoldReputation(hold, 12.0f);
            break;
        case WorldEventKind::WildfireContainment:
            AdjustHoldReputation(hold, 15.0f);
            break;
        case WorldEventKind::CaravanBanditAmbush:
            AdjustHoldReputation(hold, 10.0f);
            AdjustFactionReputation(FactionId::Companions, 4.0f);
            break;
        case WorldEventKind::AuroraCelestialGathering:
            AdjustHoldReputation(hold, 6.0f);
            AdjustFactionReputation(FactionId::CollegeOfWinterhold, 5.0f);
            break;
        case WorldEventKind::SeasonalHarvestMarket:
            AdjustHoldReputation(hold, 5.0f);
            break;
        case WorldEventKind::PredatorMigrationAlert:
            AdjustHoldReputation(hold, 8.0f);
            AdjustFactionReputation(FactionId::Companions, 3.0f);
            break;
        default:
            return false;
    }
    return true;
}

float RawFactionDiplomacy::RecordCrime(HoldId hold, CrimeSeverity severity, bool witnessed) {
    const size_t idx = static_cast<size_t>(hold);
    if (idx >= kHoldCount) {
        return 0.0f;
    }
    // Unwitnessed stealth crimes leave zero public bounty or hold notoriety:
    if (!witnessed) {
        return 0.0f;
    }

    float bountyDelta = 0.0f;
    float repPenalty  = 0.0f;
    switch (severity) {
        case CrimeSeverity::PettyTheft: bountyDelta =   40.0f; repPenalty =  -3.0f; break;
        case CrimeSeverity::Trespass:   bountyDelta =   50.0f; repPenalty =  -4.0f; break;
        case CrimeSeverity::GrandTheft: bountyDelta =  250.0f; repPenalty = -10.0f; break;
        case CrimeSeverity::Assault:    bountyDelta =  500.0f; repPenalty = -16.0f; break;
        case CrimeSeverity::Murder:     bountyDelta = 1500.0f; repPenalty = -32.0f; break;
        case CrimeSeverity::Treason:    bountyDelta = 2600.0f; repPenalty = -45.0f; break;
        default: return 0.0f;
    }

    holds_[idx].bountyGold += bountyDelta;
    holds_[idx].reputation  = ClampF(holds_[idx].reputation + repPenalty, -100.0f, 100.0f);
    RecomputeHoldDerived(holds_[idx]);
    return bountyDelta;
}

bool RawFactionDiplomacy::PayBounty(HoldId hold, float availableGold, float* outPaidGold) {
    const size_t idx = static_cast<size_t>(hold);
    if (idx >= kHoldCount || !std::isfinite(availableGold) || availableGold < 0.0f) {
        return false;
    }
    HoldStanding& hs = holds_[idx];
    if (hs.bountyGold <= 0.0f || availableGold < hs.bountyGold) {
        return false;
    }
    if (outPaidGold) {
        *outPaidGold = hs.bountyGold;
    }
    hs.bountyGold = 0.0f;
    // Paying legal wergild/amnesty lifts deep arrest/kill hostility back to Watchful/Peaceful standing:
    if (hs.reputation < -30.0f) {
        hs.reputation = -20.0f;
    }
    RecomputeHoldDerived(hs);
    return true;
}

void RawFactionDiplomacy::StepDays(float days) {
    if (!std::isfinite(days) || days <= 0.0f) {
        return;
    }
    const float mildNotorietyDecay = std::exp(-days / 45.0f);
    const float pettyBountyDecay   = std::exp(-days / 90.0f);

    for (HoldStanding& hs : holds_) {
        if (hs.reputation < 0.0f && hs.reputation > -45.0f) {
            hs.reputation *= mildNotorietyDecay;
            if (hs.reputation > -0.1f) hs.reputation = 0.0f;
        }
        if (hs.bountyGold > 0.0f && hs.bountyGold < 100.0f) {
            hs.bountyGold *= pettyBountyDecay;
            if (hs.bountyGold < 1.0f) hs.bountyGold = 0.0f;
        }
        RecomputeHoldDerived(hs);
    }
}

DiplomacyTelemetry RawFactionDiplomacy::ComputeTelemetry() const {
    DiplomacyTelemetry t{};
    double repSum    = 0.0;
    float  bestRep   = -1000.0f;
    float  worstScore =  1000.0f;

    for (const HoldStanding& hs : holds_) {
        repSum           += static_cast<double>(hs.reputation);
        t.totalBountyGold += static_cast<double>(hs.bountyGold);
        if (hs.guardPosture == GuardPosture::ArrestOnSight ||
            hs.guardPosture == GuardPosture::KillOnSight) {
            ++t.hostileHoldCount;
        }
        if (hs.reputation > bestRep) {
            bestRep            = hs.reputation;
            t.mostFriendlyHold = hs.id;
        }
        // Hostility score combines low reputation and active bounty:
        const float score = hs.reputation - hs.bountyGold * 0.02f;
        if (score < worstScore) {
            worstScore        = score;
            t.mostHostileHold = hs.id;
        }
    }
    t.meanHoldReputation = static_cast<float>(repSum / static_cast<double>(kHoldCount));
    return t;
}

}  // namespace raw::worldgen
