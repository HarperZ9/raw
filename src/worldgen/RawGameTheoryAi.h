#pragma once
// RawGameTheoryAi.h: Clean-Room Game-Theoretic Tactical AI & Combinatorial Auction Engine
// (`raw::worldgen`). Pure portable C++23 with zero Windows or D3D12 headers.
// Synthesizes three core algorithmic game-theory mechanisms:
//   1. Bounded-Regret (<= 0.20 = 1/5) Bimatrix Nash Equilibrium Solver (`SolveBimatrixNashApprox`)
//      combining exact pure/2x2 support search, zero-sum maximin-kite convex mixtures, lexicographic
//      Lemke-Howson complementarity pivoting, and regret-minimization descent with exact worst-case
//      unilateral regret certification.
//   2. Online Adaptive Fictitious Play Commander (`AdaptiveFictitiousPlayTracker`) with fixed
//      lexicographic tie-breaking: tracks opponent empirical/recency frequencies, counters player
//      action spam smoothly, and achieves decaying empirical duality gap in self-play.
//   3. Subadditive Prophet-Inequality Combinatorial Auction Pricer (`EvaluateProphetAuctionBid` /
//      `RunProphetCaravanAuction`): computes online reservation thresholds and sequential acceptance
//      rules for `RawSettlementSim` rare-goods & bundle auctions with provable competitive ratio
//      against the hindsight optimum.
// Original work, MIT.

#include "RawSettlementSim.h"

#include <cstdint>
#include <vector>

namespace raw::worldgen {

inline constexpr float kMaxCertifiedNashRegret = 0.20f; // 1/5 approximate Nash regret ceiling

enum class CombatTactic : uint8_t {
    LightAtk = 0,
    PowerAtk = 1,
    Feint    = 2,
    Parry    = 3,
    Block    = 4,
    Flank    = 5,
    Count    = 6,
};

enum class DiplomacyAction : uint8_t {
    Trade    = 0,
    Tariff   = 1,
    Alliance = 2,
    Raid     = 3,
    Truce    = 4,
    Count    = 5,
};

const char* GetCombatTacticName(CombatTactic tactic);
const char* GetDiplomacyActionName(DiplomacyAction action);

// Normalized bimatrix game (R, C) in [0, 1]^(rows x cols) stored in row-major order.
struct BimatrixGame {
    uint32_t           rows = 0u;
    uint32_t           cols = 0u;
    std::vector<float> rowPayoff; // size = rows * cols
    std::vector<float> colPayoff; // size = rows * cols

    bool IsValid() const;
    float RowAt(uint32_t r, uint32_t c) const { return rowPayoff[static_cast<size_t>(r) * cols + c]; }
    float ColAt(uint32_t r, uint32_t c) const { return colPayoff[static_cast<size_t>(r) * cols + c]; }
};

struct NashApproxResult {
    bool               valid                = false;
    bool               certifiedWithinFifth = false; // maxRegret <= 0.20
    std::vector<float> rowStrategy;                  // Simplex probability vector (size = rows)
    std::vector<float> colStrategy;                  // Simplex probability vector (size = cols)
    float              rowExpectedPayoff    = 0.0f;
    float              colExpectedPayoff    = 0.0f;
    float              rowRegret            = 1.0f;
    float              colRegret            = 1.0f;
    float              maxRegret            = 1.0f;  // max(rowRegret, colRegret)
    uint32_t           iterationsUsed       = 0u;
    const char*        solverUsed           = "None";
};

// Built-in canonical tactical & diplomatic bimatrix games:
BimatrixGame BuildCombatPlaybookGame();    // 6x6 {LightAtk, PowerAtk, Feint, Parry, Block, Flank}
BimatrixGame BuildFactionDiplomacyGame();  // 5x5 {Trade, Tariff, Alliance, Raid, Truce}

// Exact unilateral regret evaluator for any strategy pair (rowStrat, colStrat) on game (R, C).
bool EvaluateBimatrixRegret(const BimatrixGame&       game,
                            const std::vector<float>& rowStrat,
                            const std::vector<float>& colStrat,
                            float*                    outRowPayoff,
                            float*                    outColPayoff,
                            float*                    outRowRegret,
                            float*                    outColRegret);

// Polynomial-time bounded-regret (<= 1/5 = 0.20) bimatrix approximate Nash equilibrium solver.
NashApproxResult SolveBimatrixNashApprox(const BimatrixGame& game);

// =============================================================================
// 2. Online Adaptive Fictitious Play Commander (Lexicographic Tie-Breaking)
// =============================================================================
struct FictitiousPlaySnapshot {
    uint32_t           roundsObserved      = 0u;
    uint32_t           bestRowResponse     = 0u;
    uint32_t           bestColResponse     = 0u;
    float              empiricalDualityGap = 1.0f; // regretR(xHat, yHat) + regretC(xHat, yHat)
    float              expectedRowPayoff   = 0.0f;
    std::vector<float> rowEmpirical;
    std::vector<float> colEmpirical;
    std::vector<float> recommendedRowMix;
};

class AdaptiveFictitiousPlayTracker {
public:
    AdaptiveFictitiousPlayTracker() = default;
    explicit AdaptiveFictitiousPlayTracker(const BimatrixGame& game,
                                           float               emaAlpha           = 0.22f,
                                           float               adaptationStrength = 0.80f);

    bool Reset(const BimatrixGame& game,
               float               emaAlpha           = 0.22f,
               float               adaptationStrength = 0.80f);

    // Records an observed opponent (column) action and updates adaptive counter-strategy.
    bool ObserveOpponentAction(uint32_t colAction, uint32_t aiRowActionPlayed = 0u);

    // Executes one deterministic two-sided fictitious play round with strict lexicographic tie-breaking.
    // Returns current empirical duality gap (regretR + regretC).
    float StepSelfPlayRound();

    // Returns the AI's current recommended smooth mixed strategy countering the opponent's history.
    std::vector<float> RecommendMixedStrategy() const;

    // Deterministic lexicographic best response to a given opponent column distribution.
    uint32_t ComputeLexicographicRowBestResponse(const std::vector<float>& colDist) const;
    uint32_t ComputeLexicographicColBestResponse(const std::vector<float>& rowDist) const;

    FictitiousPlaySnapshot GetSnapshot() const;
    const BimatrixGame&    Game() const noexcept { return game_; }

private:
    BimatrixGame       game_{};
    std::vector<float> baseNashRow_;
    std::vector<float> rowCounts_;
    std::vector<float> colCounts_;
    std::vector<float> colEma_;
    uint32_t           rounds_             = 0u;
    float              emaAlpha_           = 0.22f;
    float              adaptationStrength_ = 0.80f;
    bool               initialized_        = false;
};

// =============================================================================
// 3. Subadditive Prophet-Inequality Combinatorial Auction Pricer
// =============================================================================
struct AuctionBuyerPrior {
    float minVal = 100.0f; // Lower bound of buyer valuation distribution (gold)
    float maxVal = 300.0f; // Upper bound of buyer valuation distribution (gold)
};

struct ProphetAuctionDecision {
    bool  valid                 = false;
    bool  acceptBid             = false;
    float reservationThreshold  = 0.0f;
    float effectiveBidGold      = 0.0f;
    float expectedHindsightOpt  = 0.0f;
    float settlementScarcityMul = 1.0f;
};

// Computes expected hindsight maximum E[max_k X_k] for independent Uniform[minVal, maxVal] priors
// via exact Gauss-Legendre / midpoint numerical integration of the CDF survival function.
float EstimateExpectedHindsightMax(const std::vector<AuctionBuyerPrior>& priors);

// Subadditive bundle value function v(S) = (sum_{i in S} w_i)^gamma * scale (concave, 0 < gamma <= 1).
float EvaluateSubadditiveBundleValue(const std::vector<float>& itemWeights, float concavityGamma = 0.75f);

// Online prophet-inequality bid evaluator optionally coupled with RawSettlementSim scarcity.
ProphetAuctionDecision EvaluateProphetAuctionBid(const std::vector<AuctionBuyerPrior>& priors,
                                                 float                                 arrivingBidGold,
                                                 HoldId                                hold       = HoldId::Whiterun,
                                                 CommodityKind                         commodity  = CommodityKind::SoulGems,
                                                 const RawSettlementSim*               settlement = nullptr);

struct ProphetTrialSummary {
    bool     valid                    = false;
    uint32_t trialsRun                = 0u;
    float    meanOnlineRevenue        = 0.0f;
    float    meanHindsightOpt         = 0.0f;
    float    competitiveRatio         = 0.0f; // meanOnlineRevenue / meanHindsightOpt (provably >= 0.50!)
    float    subadditiveBundleRatio   = 0.0f; // Multi-item subadditive posted-price ratio vs hindsight
};

// Runs deterministic multi-trial benchmark proving competitive ratio against hindsight optimum.
ProphetTrialSummary SimulateProphetAuctionTrials(const std::vector<AuctionBuyerPrior>& priors,
                                                 uint32_t                              numTrials = 512u,
                                                 uint32_t                              seed      = 202662u);

}  // namespace raw::worldgen
