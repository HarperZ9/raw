// test_game_theory_ai.cpp: Verification Suite for `raw::worldgen::RawGameTheoryAi`.
// Exports BOTH `int run_game_theory_ai_tests()` and
// `int raw::worldgen::RunGameTheoryAiTests(void (*checkFn)(bool, const char*) = nullptr)`
// plus `#ifdef RAW_GAME_THEORY_AI_TEST_STANDALONE` standalone `main()`.
// Original work, MIT.

#include "RawGameTheoryAi.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <vector>

namespace raw::worldgen {

namespace {

inline uint32_t TestRng(uint32_t& s) {
    s ^= s << 13u;
    s ^= s >> 17u;
    s ^= s << 5u;
    return s;
}

inline float TestUniform01(uint32_t& s) {
    return static_cast<float>(TestRng(s) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

}  // namespace

int RunGameTheoryAiTests(void (*checkFn)(bool, const char*)) {
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

    // -------------------------------------------------------------------------
    // Test 1: Planted Controls + 64-Game Bounded-Regret (<= 0.20 = 1/5) Certification
    // -------------------------------------------------------------------------
    {
        // Planted invalid game rejections:
        BimatrixGame emptyGame{};
        const NashApproxResult emptyRes = SolveBimatrixNashApprox(emptyGame);

        BimatrixGame nanGame{};
        nanGame.rows      = 2u;
        nanGame.cols      = 2u;
        nanGame.rowPayoff = {0.5f, std::nanf(""), 0.2f, 0.8f};
        nanGame.colPayoff = {0.5f, 0.5f, 0.5f, 0.5f};
        const NashApproxResult nanRes = SolveBimatrixNashApprox(nanGame);

        BimatrixGame outRangeGame{};
        outRangeGame.rows      = 2u;
        outRangeGame.cols      = 2u;
        outRangeGame.rowPayoff = {1.5f, 0.2f, 0.3f, 0.4f};
        outRangeGame.colPayoff = {0.1f, 0.2f, 0.3f, 0.4f};
        const NashApproxResult outRes = SolveBimatrixNashApprox(outRangeGame);

        // Build 64 deterministic test games:
        // 0: Rock-Paper-Scissors (3x3 zero-sum)
        // 1: Matching Pennies (2x2 zero-sum)
        // 2: 6x6 Asymmetric Combat Playbook
        // 3: 5x5 Faction Diplomacy Game
        // 4..63: 60 diverse random normalized bimatrix games (sizes 2x2 up to 8x8)
        std::vector<BimatrixGame> games;
        games.reserve(64u);

        // 0: Rock-Paper-Scissors
        {
            BimatrixGame rps{};
            rps.rows      = 3u;
            rps.cols      = 3u;
            rps.rowPayoff = {0.5f, 0.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.5f};
            rps.colPayoff = {0.5f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f, 0.0f, 0.5f};
            games.push_back(rps);
        }
        // 1: Matching Pennies
        {
            BimatrixGame mp{};
            mp.rows      = 2u;
            mp.cols      = 2u;
            mp.rowPayoff = {1.0f, 0.0f, 0.0f, 1.0f};
            mp.colPayoff = {0.0f, 1.0f, 1.0f, 0.0f};
            games.push_back(mp);
        }
        // 2 & 3: RAW Combat Playbook & Faction Diplomacy
        games.push_back(BuildCombatPlaybookGame());
        games.push_back(BuildFactionDiplomacyGame());

        uint32_t rng = 620062u;
        for (uint32_t k = 4u; k < 64u; ++k) {
            BimatrixGame g{};
            g.rows = 2u + (TestRng(rng) % 7u); // 2..8
            g.cols = 2u + (TestRng(rng) % 7u); // 2..8
            const size_t n = static_cast<size_t>(g.rows) * g.cols;
            g.rowPayoff.resize(n);
            g.colPayoff.resize(n);
            for (size_t i = 0u; i < n; ++i) {
                g.rowPayoff[i] = TestUniform01(rng);
                g.colPayoff[i] = TestUniform01(rng);
            }
            games.push_back(std::move(g));
        }

        uint32_t certifiedCount = 0u;
        float    worstRegret    = 0.0f;
        float    combatRegret   = 1.0f;
        float    diploRegret    = 1.0f;
        bool     allSimplexOk   = true;

        for (size_t idx = 0u; idx < games.size(); ++idx) {
            const NashApproxResult r = SolveBimatrixNashApprox(games[idx]);
            if (r.valid && r.certifiedWithinFifth && r.maxRegret <= kMaxCertifiedNashRegret + 1e-5f) {
                ++certifiedCount;
            }
            worstRegret = std::max(worstRegret, r.maxRegret);
            if (idx == 2u) combatRegret = r.maxRegret;
            if (idx == 3u) diploRegret  = r.maxRegret;

            float sx = 0.0f, sy = 0.0f;
            for (float v : r.rowStrategy) {
                if (!std::isfinite(v) || v < -1e-6f) allSimplexOk = false;
                sx += v;
            }
            for (float v : r.colStrategy) {
                if (!std::isfinite(v) || v < -1e-6f) allSimplexOk = false;
                sy += v;
            }
            if (std::fabs(sx - 1.0f) > 1e-3f || std::fabs(sy - 1.0f) > 1e-3f) {
                allSimplexOk = false;
            }
        }

        std::printf("  [64-Game Nash <= 0.20] certified=%u/64 worstRegret=%.5f combat6x6=%.5f diplo5x5=%.5f\n",
                    certifiedCount, worstRegret, combatRegret, diploRegret);

        const bool ok = !emptyRes.valid && !nanRes.valid && !outRes.valid &&
                        (certifiedCount == 64u) && (worstRegret <= 0.20001f) && allSimplexOk;
        check(ok, "game_theory_64_bimatrix_nash_regret_certified_within_fifth");
    }

    // -------------------------------------------------------------------------
    // Test 2: Online Adaptive Fictitious Play Commander & Empirical Duality-Gap Decay
    // -------------------------------------------------------------------------
    {
        const BimatrixGame combat = BuildCombatPlaybookGame();

        // 2A. Player spams PowerAtk (col=1): AI commander must smoothly shift onto Parry (row=3) / Flank (row=5)!
        AdaptiveFictitiousPlayTracker commander(combat, 0.25f, 0.85f);
        const std::vector<float> baseMix = commander.RecommendMixedStrategy();
        const float baseParryProb        = baseMix[static_cast<size_t>(CombatTactic::Parry)];

        for (int round = 0; round < 18; ++round) {
            commander.ObserveOpponentAction(static_cast<uint32_t>(CombatTactic::PowerAtk),
                                            static_cast<uint32_t>(CombatTactic::LightAtk));
        }
        const std::vector<float> adaptedMix = commander.RecommendMixedStrategy();
        const float adaptedParryProb        = adaptedMix[static_cast<size_t>(CombatTactic::Parry)];
        const float adaptedFlankProb        = adaptedMix[static_cast<size_t>(CombatTactic::Flank)];

        // Expected payoff against a pure PowerAtk spammer must rise sharply:
        float basePayVsSpam = 0.0f, adaptedPayVsSpam = 0.0f;
        for (uint32_t i = 0u; i < combat.rows; ++i) {
            const float payVsPower = combat.RowAt(i, static_cast<uint32_t>(CombatTactic::PowerAtk));
            basePayVsSpam    += baseMix[i] * payVsPower;
            adaptedPayVsSpam += adaptedMix[i] * payVsPower;
        }

        // 2B. Two-sided Lexicographic Fictitious Play self-play on RPS must show clear duality-gap decay!
        BimatrixGame rps{};
        rps.rows      = 3u;
        rps.cols      = 3u;
        rps.rowPayoff = {0.5f, 0.0f, 1.0f, 1.0f, 0.5f, 0.0f, 0.0f, 1.0f, 0.5f};
        rps.colPayoff = {0.5f, 1.0f, 0.0f, 0.0f, 0.5f, 1.0f, 1.0f, 0.0f, 0.5f};

        AdaptiveFictitiousPlayTracker fp(rps);
        float gapEarly = 0.0f;
        float gapLate  = 0.0f;
        for (int r = 1; r <= 320; ++r) {
            const float g = fp.StepSelfPlayRound();
            if (r == 8)   gapEarly = g;
            if (r == 320) gapLate  = g;
        }

        // Verify deterministic lexicographic tie-breaking on an all-equal payoff distribution:
        const std::vector<float> uniformCol = {1.0f / 3.0f, 1.0f / 3.0f, 1.0f / 3.0f};
        const uint32_t lexBr = fp.ComputeLexicographicRowBestResponse(uniformCol);

        std::printf("  [Fictitious Play] parryProb %.2f->%.2f (parry+flank=%.2f) payVsSpam %.2f->%.2f | "
                    "selfPlayGap(8->320) %.3f->%.3f lexTie=%u\n",
                    baseParryProb, adaptedParryProb, adaptedParryProb + adaptedFlankProb,
                    basePayVsSpam, adaptedPayVsSpam, gapEarly, gapLate, lexBr);

        const bool spamOk = (adaptedParryProb > baseParryProb + 0.15f) &&
                            (adaptedParryProb + adaptedFlankProb > 0.65f) &&
                            (adaptedPayVsSpam > basePayVsSpam + 0.12f);
        const bool fpOk   = (gapLate < 0.08f) && (gapLate < gapEarly) && (lexBr == 0u);
        check(spamOk && fpOk, "game_theory_adaptive_fictitious_play_and_duality_gap_decay");
    }

    // -------------------------------------------------------------------------
    // Test 3: Subadditive Prophet-Inequality Combinatorial Auction & Settlement Coupling
    // -------------------------------------------------------------------------
    {
        // 6 independent sequential merchant/caravan buyer arrival priors:
        const std::vector<AuctionBuyerPrior> priors = {
            {120.0f, 260.0f},
            {150.0f, 420.0f},
            {90.0f,  580.0f},
            {200.0f, 360.0f},
            {110.0f, 520.0f},
            {140.0f, 310.0f},
        };

        // Verify strict subadditivity: v(A U B) <= v(A) + v(B)
        const float vA  = EvaluateSubadditiveBundleValue({200.0f}, 0.75f);
        const float vB  = EvaluateSubadditiveBundleValue({300.0f}, 0.75f);
        const float vAB = EvaluateSubadditiveBundleValue({200.0f, 300.0f}, 0.75f);
        const bool  subadditiveOk = (vAB < vA + vB) && (vAB > std::max(vA, vB));

        // Verify RawSettlementSim scarcity shifts reservation threshold upward:
        RawSettlementSim settle(6201u);
        HoldState* wh = settle.GetHoldMutable(HoldId::Whiterun);
        const ProphetAuctionDecision decBase =
            EvaluateProphetAuctionBid(priors, 230.0f, HoldId::Whiterun, CommodityKind::SoulGems, &settle);
        if (wh) {
            wh->price[static_cast<size_t>(CommodityKind::SoulGems)] *= 1.35f; // Scarcity spike!
        }
        const ProphetAuctionDecision decScarce =
            EvaluateProphetAuctionBid(priors, 230.0f, HoldId::Whiterun, CommodityKind::SoulGems, &settle);

        // Verify planted invalid bid rejection:
        const ProphetAuctionDecision decBad =
            EvaluateProphetAuctionBid(priors, -50.0f, HoldId::Whiterun, CommodityKind::SoulGems, &settle);

        // Run 1024 deterministic trials verifying competitive ratio >= 0.50 (Prophet Inequality!)
        const ProphetTrialSummary sim = SimulateProphetAuctionTrials(priors, 1024u, 202662u);

        std::printf("  [Prophet Auction] E[OPT]=%.1fg baseTau=%.1fg scarceTau=%.1fg | "
                    "online=%.1fg opt=%.1fg compRatio=%.3f (>=0.50) subBundleRatio=%.3f\n",
                    decBase.expectedHindsightOpt, decBase.reservationThreshold, decScarce.reservationThreshold,
                    sim.meanOnlineRevenue, sim.meanHindsightOpt, sim.competitiveRatio, sim.subadditiveBundleRatio);

        const bool ok = subadditiveOk && decBase.valid && decScarce.valid && !decBad.valid &&
                        (decScarce.reservationThreshold > decBase.reservationThreshold + 20.0f) &&
                        sim.valid && (sim.competitiveRatio >= 0.50f) &&
                        (sim.subadditiveBundleRatio >= 0.55f);
        check(ok, "game_theory_subadditive_prophet_auction_and_competitive_ratio");
    }

    return failures;
}

}  // namespace raw::worldgen

int run_game_theory_ai_tests() {
    return raw::worldgen::RunGameTheoryAiTests(nullptr);
}

#ifdef RAW_GAME_THEORY_AI_TEST_STANDALONE
int main() {
    const int failures = run_game_theory_ai_tests();
    std::printf("RawGameTheoryAi standalone test summary: %s (%d failures)\n",
                failures == 0 ? "ALL PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
#endif
