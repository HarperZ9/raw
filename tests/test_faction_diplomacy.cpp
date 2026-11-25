// test_faction_diplomacy.cpp: Verification Suite for `raw::worldgen::RawFactionDiplomacy`.
// Exports `int raw::worldgen::RunFactionDiplomacyTests(void (*checkFn)(bool, const char*) = nullptr)`
// and supports `#ifdef RAW_FACTION_DIPLOMACY_TEST_STANDALONE` standalone execution.
// Original work, MIT.

#include "RawFactionDiplomacy.h"

#include <cmath>
#include <cstdio>

namespace raw::worldgen {

int RunFactionDiplomacyTests(void (*checkFn)(bool, const char*)) {
    int failures = 0;
    auto check = [&](bool cond, const char* name) {
        if (!cond) {
            ++failures;
        }
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
    };

    // =========================================================================
    // Test 1: Planted-failure controls (zero/neg days, invalid IDs, unwitnessed crime, insufficient gold)
    // =========================================================================
    {
        RawFactionDiplomacy dip;
        const bool badHoldNull    = (dip.GetHoldStanding(HoldId::Count) == nullptr);
        const bool badFactionNull = (dip.GetFactionStanding(FactionId::Count) == nullptr);
        const bool badAdjFalse    = !dip.AdjustHoldReputation(HoldId::Count, 10.0f) &&
                                    !dip.AdjustFactionReputation(FactionId::Count, 10.0f) &&
                                    !dip.ApplyEventReward(WorldEventKind::Count, HoldId::Whiterun);

        // Unwitnessed stealth murder adds 0 bounty and 0 public notoriety:
        const float stealthBounty = dip.RecordCrime(HoldId::Riften, CrimeSeverity::Murder, false);
        const bool stealthOk = (stealthBounty == 0.0f) &&
                               (dip.GetHoldStanding(HoldId::Riften)->bountyGold == 0.0f) &&
                               (dip.GetHoldStanding(HoldId::Riften)->guardPosture == GuardPosture::Peaceful);

        // Paying bounty with insufficient gold is cleanly rejected:
        dip.RecordCrime(HoldId::Markarth, CrimeSeverity::GrandTheft, true); // 250g
        const float bountyBefore = dip.GetHoldStanding(HoldId::Markarth)->bountyGold;
        const bool underpayRejected = !dip.PayBounty(HoldId::Markarth, 100.0f);
        const bool bountyUnchanged  = (dip.GetHoldStanding(HoldId::Markarth)->bountyGold == bountyBefore);

        // Zero / negative days step is an exact no-op:
        const float repBefore = dip.GetHoldStanding(HoldId::Markarth)->reputation;
        dip.StepDays(0.0f);
        dip.StepDays(-14.0f);
        const bool zeroDaysNoOp = (dip.GetHoldStanding(HoldId::Markarth)->reputation == repBefore &&
                                   dip.GetHoldStanding(HoldId::Markarth)->bountyGold == bountyBefore);

        check(badHoldNull && badFactionNull && badAdjFalse && stealthOk &&
                  underpayRejected && bountyUnchanged && zeroDaysNoOp,
              "faction_diplomacy_planted_failure_controls");
    }

    // =========================================================================
    // Test 2: Alliance / Rival Civil-War Spillover Symmetry & [-100..+100] Clamping
    // =========================================================================
    {
        RawFactionDiplomacy dip;
        dip.AdjustFactionReputation(FactionId::ImperialLegion, 60.0f);

        const float impRep  = dip.GetFactionStanding(FactionId::ImperialLegion)->reputation;
        const float stmRep  = dip.GetFactionStanding(FactionId::Stormcloaks)->reputation;
        const float solRep  = dip.GetHoldStanding(HoldId::Solitude)->reputation;
        const float wndRep  = dip.GetHoldStanding(HoldId::Windhelm)->reputation;

        // Stress clamping with huge boost:
        dip.AdjustFactionReputation(FactionId::ImperialLegion, 500.0f);
        bool allClamped = true;
        for (size_t i = 0u; i < kHoldCount; ++i) {
            const float r = dip.GetHoldStanding(static_cast<HoldId>(i))->reputation;
            if (!std::isfinite(r) || r < -100.0f || r > 100.0f) allClamped = false;
        }
        for (size_t i = 0u; i < kFactionCount; ++i) {
            const float r = dip.GetFactionStanding(static_cast<FactionId>(i))->reputation;
            if (!std::isfinite(r) || r < -100.0f || r > 100.0f) allClamped = false;
        }

        std::printf("  [Civil-War Spillover] Imperial=+%.1f Stormcloaks=%.1f Solitude=+%.1f Windhelm=%.1f clamped=%d\n",
                    impRep, stmRep, solRep, wndRep, allClamped ? 1 : 0);

        check(impRep == 60.0f && stmRep < -20.0f && solRep > 20.0f && wndRep < -15.0f && allClamped,
              "faction_diplomacy_alliance_rival_spillover");
    }

    // =========================================================================
    // Test 3: Event Reward -> Merchant Discount Proof (< 0.95)
    // =========================================================================
    {
        RawFactionDiplomacy dip;
        const float baseMult = dip.GetHoldStanding(HoldId::Whiterun)->merchantPriceMult;
        const bool r1 = dip.ApplyEventReward(WorldEventKind::BlizzardCaravanRescue, HoldId::Whiterun);
        const bool r2 = dip.ApplyEventReward(WorldEventKind::WildfireContainment, HoldId::Whiterun);
        const HoldStanding* wr = dip.GetHoldStanding(HoldId::Whiterun);

        std::printf("  [Event Reward Discount] Whiterun rep=+%.1f priceMult %.3f -> %.3f\n",
                    wr->reputation, baseMult, wr->merchantPriceMult);

        check(r1 && r2 && wr->reputation >= 25.0f &&
                  wr->merchantPriceMult < 0.95f && wr->merchantPriceMult >= 0.82f,
              "faction_diplomacy_event_reward_merchant_discount");
    }

    // =========================================================================
    // Test 4: Bounty Escalation -> KillOnSight & PayBounty Amnesty Reset
    // =========================================================================
    {
        RawFactionDiplomacy dip;
        dip.RecordCrime(HoldId::Whiterun, CrimeSeverity::Assault, true); // 500g
        const GuardPosture afterAssault = dip.GetHoldStanding(HoldId::Whiterun)->guardPosture;

        dip.RecordCrime(HoldId::Whiterun, CrimeSeverity::Murder, true);  // +1500g = 2000g
        dip.RecordCrime(HoldId::Whiterun, CrimeSeverity::Murder, true);  // +1500g = 3500g
        const HoldStanding hostileState = *dip.GetHoldStanding(HoldId::Whiterun);
        const DiplomacyTelemetry telHostile = dip.ComputeTelemetry();

        float paidGold = 0.0f;
        const bool paid = dip.PayBounty(HoldId::Whiterun, 5000.0f, &paidGold);
        const HoldStanding afterAmnesty = *dip.GetHoldStanding(HoldId::Whiterun);
        const DiplomacyTelemetry telClean = dip.ComputeTelemetry();

        // Mild notoriety decay over 45 days:
        const float repAfterPay = afterAmnesty.reputation;
        dip.StepDays(45.0f);
        const float repAfterDecay = dip.GetHoldStanding(HoldId::Whiterun)->reputation;

        std::printf(
            "  [Bounty & Amnesty] assault=%s murders(bounty=%.0fg posture=%s hunterRisk=%.2f) "
            "-> paid=%.0fg posture=%s decay %.1f->%.1f\n",
            GetGuardPostureName(afterAssault), hostileState.bountyGold,
            GetGuardPostureName(hostileState.guardPosture), hostileState.bountyHunterRisk01,
            paidGold, GetGuardPostureName(afterAmnesty.guardPosture), repAfterPay, repAfterDecay);

        check(afterAssault == GuardPosture::ArrestOnSight &&
                  hostileState.guardPosture == GuardPosture::KillOnSight &&
                  hostileState.bountyHunterRisk01 >= 0.80f &&
                  telHostile.hostileHoldCount == 1u &&
                  paid && paidGold == 3500.0f &&
                  afterAmnesty.bountyGold == 0.0f &&
                  (afterAmnesty.guardPosture == GuardPosture::Peaceful ||
                   afterAmnesty.guardPosture == GuardPosture::Watchful) &&
                  telClean.hostileHoldCount == 0u &&
                  std::abs(repAfterDecay) < std::abs(repAfterPay) &&
                  std::isfinite(telClean.meanHoldReputation),
              "faction_diplomacy_bounty_escalation_and_amnesty_reset");
    }

    return failures;
}

}  // namespace raw::worldgen

#ifdef RAW_FACTION_DIPLOMACY_TEST_STANDALONE
int main() {
    const int failures = raw::worldgen::RunFactionDiplomacyTests(nullptr);
    std::printf("RawFactionDiplomacy standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
