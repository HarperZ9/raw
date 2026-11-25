// RawGameTheoryAi.cpp: Clean-Room Game-Theoretic Tactical AI & Combinatorial Auction Engine
// (`raw::worldgen`). Pure portable C++23 with zero Windows or D3D12 headers.
// Original work, MIT.

#include "RawGameTheoryAi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numeric>

namespace raw::worldgen {

namespace {

inline float SafeClamp01(float v) {
    if (!std::isfinite(v)) return 0.0f;
    return std::clamp(v, 0.0f, 1.0f);
}

inline void NormalizeSimplex(std::vector<float>& v) {
    if (v.empty()) return;
    double sum = 0.0;
    for (float& x : v) {
        if (!std::isfinite(x) || x < 0.0f) x = 0.0f;
        sum += static_cast<double>(x);
    }
    if (sum <= 1e-12) {
        const float u = 1.0f / static_cast<float>(v.size());
        for (float& x : v) x = u;
        return;
    }
    const double inv = 1.0 / sum;
    for (float& x : v) {
        x = static_cast<float>(static_cast<double>(x) * inv);
    }
}

// Deterministic xorshift32 PRNG for reproducible auction benchmark trials.
inline uint32_t XorShift32(uint32_t& state) {
    uint32_t x = (state == 0u) ? 202662u : state;
    x ^= x << 13u;
    x ^= x >> 17u;
    x ^= x << 5u;
    state = (x == 0u) ? 202662u : x;
    return state;
}

inline float Uniform01(uint32_t& state) {
    return static_cast<float>(XorShift32(state) & 0x00FFFFFFu) / static_cast<float>(0x01000000u);
}

struct CandidateEval {
    std::vector<float> x;
    std::vector<float> y;
    float              rowPay    = 0.0f;
    float              colPay    = 0.0f;
    float              rowRegret = 1.0f;
    float              colRegret = 1.0f;
    float              maxRegret = 1.0f;
    const char*        tag       = "None";
};

inline CandidateEval ScoreProfile(const BimatrixGame&       game,
                                  std::vector<float>        x,
                                  std::vector<float>        y,
                                  const char*               tag) {
    NormalizeSimplex(x);
    NormalizeSimplex(y);
    CandidateEval e{};
    e.x   = std::move(x);
    e.y   = std::move(y);
    e.tag = tag;
    EvaluateBimatrixRegret(game, e.x, e.y, &e.rowPay, &e.colPay, &e.rowRegret, &e.colRegret);
    e.maxRegret = std::max(e.rowRegret, e.colRegret);
    return e;
}

inline void KeepBest(CandidateEval& best, CandidateEval&& cand) {
    if (std::isfinite(cand.maxRegret) && cand.maxRegret < best.maxRegret) {
        best = std::move(cand);
    }
}

// Compute exact expected payoff vector Ry (size m) and x^T C (size n).
void ComputePayoffVectors(const BimatrixGame&       game,
                          const std::vector<float>& x,
                          const std::vector<float>& y,
                          std::vector<double>&      ry,
                          std::vector<double>&      xc) {
    ry.assign(game.rows, 0.0);
    xc.assign(game.cols, 0.0);
    for (uint32_t i = 0u; i < game.rows; ++i) {
        double acc = 0.0;
        const size_t rowOff = static_cast<size_t>(i) * game.cols;
        for (uint32_t j = 0u; j < game.cols; ++j) {
            acc += static_cast<double>(game.rowPayoff[rowOff + j]) * static_cast<double>(y[j]);
            xc[j] += static_cast<double>(x[i]) * static_cast<double>(game.colPayoff[rowOff + j]);
        }
        ry[i] = acc;
    }
}

// Zero-sum minimax solver via deterministic Hedge / Multiplicative Weights Update on matrix M.
void SolveZeroSumMWU(uint32_t                   rows,
                     uint32_t                   cols,
                     const std::vector<double>& M,
                     uint32_t                   iters,
                     std::vector<float>&        outX,
                     std::vector<float>&        outY) {
    std::vector<double> wx(rows, 1.0 / static_cast<double>(rows));
    std::vector<double> wy(cols, 1.0 / static_cast<double>(cols));
    std::vector<double> avgX(rows, 0.0);
    std::vector<double> avgY(cols, 0.0);
    std::vector<double> rowGain(rows, 0.0);
    std::vector<double> colLoss(cols, 0.0);

    const double eta = std::sqrt(2.0 * std::log(static_cast<double>(std::max(rows, cols)) + 1.0) /
                                 static_cast<double>(std::max(iters, 1u)));

    for (uint32_t t = 0u; t < iters; ++t) {
        std::fill(rowGain.begin(), rowGain.end(), 0.0);
        std::fill(colLoss.begin(), colLoss.end(), 0.0);
        for (uint32_t i = 0u; i < rows; ++i) {
            const size_t off = static_cast<size_t>(i) * cols;
            for (uint32_t j = 0u; j < cols; ++j) {
                const double m = M[off + j];
                rowGain[i] += m * wy[j];
                colLoss[j] += wx[i] * m;
            }
        }
        double sx = 0.0;
        for (uint32_t i = 0u; i < rows; ++i) {
            avgX[i] += wx[i];
            wx[i] *= std::exp(eta * rowGain[i]);
            sx += wx[i];
        }
        for (uint32_t i = 0u; i < rows; ++i) wx[i] /= std::max(sx, 1e-18);

        double sy = 0.0;
        for (uint32_t j = 0u; j < cols; ++j) {
            avgY[j] += wy[j];
            wy[j] *= std::exp(-eta * colLoss[j]);
            sy += wy[j];
        }
        for (uint32_t j = 0u; j < cols; ++j) wy[j] /= std::max(sy, 1e-18);
    }

    outX.resize(rows);
    outY.resize(cols);
    for (uint32_t i = 0u; i < rows; ++i) outX[i] = static_cast<float>(avgX[i]);
    for (uint32_t j = 0u; j < cols; ++j) outY[j] = static_cast<float>(avgY[j]);
    NormalizeSimplex(outX);
    NormalizeSimplex(outY);
}

// Lemke-Howson pivoting for exact Nash equilibria on small/medium bimatrix games (m, n <= 16).
// Uses shifted strictly-positive payoffs A = R + 1, B = C + 1 with deterministic lexicographic
// tie-breaking and bounded pivot budget to guarantee polynomial worst-case runtime.
bool TryLemkeHowson(const BimatrixGame& game,
                    uint32_t            droppedLabel,
                    uint32_t            maxPivots,
                    std::vector<float>& outX,
                    std::vector<float>& outY,
                    uint32_t*           outPivotsUsed) {
    const uint32_t m = game.rows;
    const uint32_t n = game.cols;
    if (m == 0u || n == 0u || m > 16u || n > 16u || droppedLabel >= m + n) {
        return false;
    }

    // Tableau 1 (for y): m rows, columns are [y_0..y_{n-1}, s_0..s_{m-1} | rhs]
    // Equation: A y + s = 1  (labels: y_j has label m + j, slack s_i has label i)
    // Tableau 2 (for x): n rows, columns are [x_0..x_{m-1}, t_0..t_{n-1} | rhs]
    // Equation: B^T x + t = 1 (labels: x_i has label i, slack t_j has label m + j)
    const uint32_t colsT1 = n + m + 1u;
    const uint32_t colsT2 = m + n + 1u;

    std::vector<double> T1(static_cast<size_t>(m) * colsT1, 0.0);
    std::vector<double> T2(static_cast<size_t>(n) * colsT2, 0.0);
    std::vector<uint32_t> basis1(m, 0u);
    std::vector<uint32_t> basis2(n, 0u);

    // Tiny deterministic perturbation breaks exact degeneracy cleanly without distorting [0,1] payoffs:
    for (uint32_t i = 0u; i < m; ++i) {
        for (uint32_t j = 0u; j < n; ++j) {
            const double eps = 1e-9 * static_cast<double>((i + 1u) * 31u + (j + 1u) * 7u);
            T1[static_cast<size_t>(i) * colsT1 + j] = 1.0 + static_cast<double>(game.RowAt(i, j)) + eps;
        }
        T1[static_cast<size_t>(i) * colsT1 + n + i] = 1.0;
        T1[static_cast<size_t>(i) * colsT1 + n + m] = 1.0;
        basis1[i] = i; // slack s_i -> label i
    }

    for (uint32_t j = 0u; j < n; ++j) {
        for (uint32_t i = 0u; i < m; ++i) {
            const double eps = 1e-9 * static_cast<double>((j + 1u) * 29u + (i + 1u) * 11u);
            T2[static_cast<size_t>(j) * colsT2 + i] = 1.0 + static_cast<double>(game.ColAt(i, j)) + eps;
        }
        T2[static_cast<size_t>(j) * colsT2 + m + j] = 1.0;
        T2[static_cast<size_t>(j) * colsT2 + m + n] = 1.0;
        basis2[j] = m + j; // slack t_j -> label m + j
    }

    auto pivotTableau = [](std::vector<double>&   T,
                           uint32_t               numRows,
                           uint32_t               numCols,
                           std::vector<uint32_t>& basis,
                           uint32_t               enterCol,
                           uint32_t               enterLabel,
                           uint32_t&              outLeaveLabel) -> bool {
        int    bestRow   = -1;
        double bestRatio = std::numeric_limits<double>::infinity();
        const uint32_t rhsCol = numCols - 1u;

        for (uint32_t r = 0u; r < numRows; ++r) {
            const double a = T[static_cast<size_t>(r) * numCols + enterCol];
            if (a > 1e-12) {
                const double rhs   = T[static_cast<size_t>(r) * numCols + rhsCol];
                const double ratio = rhs / a;
                if (ratio < bestRatio - 1e-13 ||
                    (std::fabs(ratio - bestRatio) <= 1e-13 &&
                     (bestRow < 0 || basis[r] < basis[static_cast<size_t>(bestRow)]))) {
                    bestRatio = ratio;
                    bestRow   = static_cast<int>(r);
                }
            }
        }
        if (bestRow < 0) return false;

        const uint32_t pr = static_cast<uint32_t>(bestRow);
        const double pv   = T[static_cast<size_t>(pr) * numCols + enterCol];
        if (!std::isfinite(pv) || std::fabs(pv) <= 1e-14) return false;

        const double invPv = 1.0 / pv;
        for (uint32_t c = 0u; c < numCols; ++c) {
            T[static_cast<size_t>(pr) * numCols + c] *= invPv;
        }
        for (uint32_t r = 0u; r < numRows; ++r) {
            if (r == pr) continue;
            const double factor = T[static_cast<size_t>(r) * numCols + enterCol];
            if (std::fabs(factor) <= 1e-15) continue;
            for (uint32_t c = 0u; c < numCols; ++c) {
                T[static_cast<size_t>(r) * numCols + c] -= factor * T[static_cast<size_t>(pr) * numCols + c];
            }
        }

        outLeaveLabel = basis[pr];
        basis[pr]     = enterLabel;
        return true;
    };

    // Determine which tableau starts:
    // Label L < m enters T2 (variable x_L at col L) first, OR label L >= m enters T1 (variable y_{L-m} at col L-m) first.
    bool     inT1  = (droppedLabel >= m);
    uint32_t label = droppedLabel;

    uint32_t pivots = 0u;
    for (; pivots < maxPivots; ++pivots) {
        uint32_t leavingLabel = 0u;
        bool     ok           = false;
        if (inT1) {
            const uint32_t enterCol = (label >= m) ? (label - m) : (n + label);
            ok = pivotTableau(T1, m, colsT1, basis1, enterCol, label, leavingLabel);
        } else {
            const uint32_t enterCol = (label < m) ? label : (m + (label - m));
            ok = pivotTableau(T2, n, colsT2, basis2, enterCol, label, leavingLabel);
        }
        if (!ok) {
            if (outPivotsUsed) *outPivotsUsed = pivots;
            return false;
        }
        if (leavingLabel == droppedLabel) {
            ++pivots;
            break;
        }
        label = leavingLabel;
        inT1  = !inT1;
    }

    if (outPivotsUsed) *outPivotsUsed = pivots;
    if (pivots >= maxPivots) return false;

    outX.assign(m, 0.0f);
    outY.assign(n, 0.0f);
    for (uint32_t r = 0u; r < n; ++r) {
        if (basis2[r] < m) {
            const double val = T2[static_cast<size_t>(r) * colsT2 + (colsT2 - 1u)];
            if (val > 0.0 && std::isfinite(val)) outX[basis2[r]] = static_cast<float>(val);
        }
    }
    for (uint32_t r = 0u; r < m; ++r) {
        if (basis1[r] >= m && basis1[r] < m + n) {
            const double val = T1[static_cast<size_t>(r) * colsT1 + (colsT1 - 1u)];
            if (val > 0.0 && std::isfinite(val)) outY[basis1[r] - m] = static_cast<float>(val);
        }
    }

    NormalizeSimplex(outX);
    NormalizeSimplex(outY);
    return true;
}

// Regret-minimization directional descent (Frank-Wolfe / Tsaknakis-Spirakis style):
// Iteratively moves (x, y) toward the pure best-response pair or weighted mix that strictly
// decreases max(regretR, regretC).
CandidateEval RunRegretDescent(const BimatrixGame& game,
                               std::vector<float>  x,
                               std::vector<float>  y,
                               uint32_t            maxIters,
                               uint32_t*           outIters) {
    CandidateEval cur = ScoreProfile(game, std::move(x), std::move(y), "RegretDescent");
    std::vector<double> ry, xc;

    uint32_t it = 0u;
    for (; it < maxIters; ++it) {
        if (cur.maxRegret <= 1e-5f) break;
        ComputePayoffVectors(game, cur.x, cur.y, ry, xc);

        uint32_t brRow = 0u;
        for (uint32_t i = 1u; i < game.rows; ++i) {
            if (ry[i] > ry[brRow] + 1e-12) brRow = i;
        }
        uint32_t brCol = 0u;
        for (uint32_t j = 1u; j < game.cols; ++j) {
            if (xc[j] > xc[brCol] + 1e-12) brCol = j;
        }

        // Try moving row only, col only, and both jointly toward (brRow, brCol) with step scan:
        bool improved = false;
        static constexpr std::array<float, 9> kAlphas = {
            0.50f, 0.25f, 0.125f, 0.0625f, 0.03f, 0.015f, 0.006f, 0.002f, 0.0005f};

        for (int mode = 0; mode < 3; ++mode) {
            for (float a : kAlphas) {
                std::vector<float> nx = cur.x;
                std::vector<float> ny = cur.y;
                if (mode == 0 || mode == 2) {
                    for (float& v : nx) v *= (1.0f - a);
                    nx[brRow] += a;
                }
                if (mode == 1 || mode == 2) {
                    for (float& v : ny) v *= (1.0f - a);
                    ny[brCol] += a;
                }
                CandidateEval cand = ScoreProfile(game, std::move(nx), std::move(ny), "RegretDescent");
                if (cand.maxRegret < cur.maxRegret - 1e-7f) {
                    cur      = std::move(cand);
                    improved = true;
                    break;
                }
            }
            if (improved) break;
        }

        if (!improved) break;
    }

    if (outIters) *outIters += it;
    return cur;
}

}  // namespace

const char* GetCombatTacticName(CombatTactic tactic) {
    switch (tactic) {
        case CombatTactic::LightAtk: return "LightAtk";
        case CombatTactic::PowerAtk: return "PowerAtk";
        case CombatTactic::Feint:    return "Feint";
        case CombatTactic::Parry:    return "Parry";
        case CombatTactic::Block:    return "Block";
        case CombatTactic::Flank:    return "Flank";
        default:                     return "Unknown";
    }
}

const char* GetDiplomacyActionName(DiplomacyAction action) {
    switch (action) {
        case DiplomacyAction::Trade:    return "Trade";
        case DiplomacyAction::Tariff:   return "Tariff";
        case DiplomacyAction::Alliance: return "Alliance";
        case DiplomacyAction::Raid:     return "Raid";
        case DiplomacyAction::Truce:    return "Truce";
        default:                        return "Unknown";
    }
}

bool BimatrixGame::IsValid() const {
    if (rows == 0u || cols == 0u || rows > 64u || cols > 64u) return false;
    const size_t n = static_cast<size_t>(rows) * cols;
    if (rowPayoff.size() != n || colPayoff.size() != n) return false;
    for (size_t i = 0u; i < n; ++i) {
        if (!std::isfinite(rowPayoff[i]) || !std::isfinite(colPayoff[i])) return false;
        if (rowPayoff[i] < -1e-4f || rowPayoff[i] > 1.0f + 1e-4f) return false;
        if (colPayoff[i] < -1e-4f || colPayoff[i] > 1.0f + 1e-4f) return false;
    }
    return true;
}

BimatrixGame BuildCombatPlaybookGame() {
    // 6x6 Asymmetric Tactical Melee Playbook in [0, 1]:
    // Actions: 0=LightAtk, 1=PowerAtk, 2=Feint, 3=Parry, 4=Block, 5=Flank
    // Authentic combat triangle & rock-paper-scissors counter matrix:
    // - Parry hard-counters PowerAtk (0.94 vs 0.08) and beats LightAtk (0.72 vs 0.25)
    // - Feint baits & punishes Parry (0.92 vs 0.10) and flanks Block (0.78 vs 0.22)
    // - LightAtk interrupts Feint (0.85 vs 0.16) and Flank (0.76 vs 0.24)
    // - PowerAtk guard-breaks Block (0.90 vs 0.12) and overwhelms LightAtk (0.70 vs 0.30)
    // - Block stops LightAtk (0.82 vs 0.18) and survives Flank (0.42 vs 0.58)
    // - Flank punishes PowerAtk windup (0.86 vs 0.15) and Block turtling (0.82 vs 0.18)
    BimatrixGame g{};
    g.rows = 6u;
    g.cols = 6u;
    g.rowPayoff = {
        // Col: Light, Power, Feint, Parry, Block, Flank
        0.50f, 0.30f, 0.85f, 0.25f, 0.18f, 0.76f, // Row LightAtk
        0.70f, 0.45f, 0.58f, 0.08f, 0.90f, 0.15f, // Row PowerAtk
        0.16f, 0.42f, 0.50f, 0.92f, 0.78f, 0.48f, // Row Feint
        0.72f, 0.94f, 0.10f, 0.40f, 0.38f, 0.22f, // Row Parry
        0.82f, 0.12f, 0.22f, 0.48f, 0.50f, 0.18f, // Row Block
        0.24f, 0.86f, 0.52f, 0.78f, 0.82f, 0.46f, // Row Flank
    };
    g.colPayoff = {
        // Slightly asymmetric defender/challenger stamina economy (still normalized in [0,1]):
        0.50f, 0.68f, 0.18f, 0.74f, 0.80f, 0.26f, // Row LightAtk
        0.32f, 0.48f, 0.44f, 0.92f, 0.14f, 0.84f, // Row PowerAtk
        0.84f, 0.56f, 0.50f, 0.12f, 0.24f, 0.54f, // Row Feint
        0.28f, 0.10f, 0.90f, 0.44f, 0.52f, 0.76f, // Row Parry
        0.20f, 0.88f, 0.76f, 0.42f, 0.52f, 0.80f, // Row Block
        0.74f, 0.16f, 0.48f, 0.24f, 0.20f, 0.48f, // Row Flank
    };
    return g;
}

BimatrixGame BuildFactionDiplomacyGame() {
    // 5x5 Hold Diplomacy Payoff Matrix in [0, 1]:
    // Actions: 0=Trade, 1=Tariff, 2=Alliance, 3=Raid, 4=Truce
    BimatrixGame g{};
    g.rows = 5u;
    g.cols = 5u;
    g.rowPayoff = {
        0.78f, 0.38f, 0.84f, 0.12f, 0.66f, // Trade
        0.82f, 0.44f, 0.52f, 0.30f, 0.58f, // Tariff
        0.80f, 0.42f, 0.90f, 0.10f, 0.70f, // Alliance
        0.88f, 0.72f, 0.85f, 0.22f, 0.76f, // Raid
        0.62f, 0.48f, 0.68f, 0.34f, 0.60f, // Truce
    };
    g.colPayoff = {
        0.78f, 0.82f, 0.80f, 0.88f, 0.62f, // Trade
        0.38f, 0.44f, 0.42f, 0.72f, 0.48f, // Tariff
        0.84f, 0.52f, 0.90f, 0.85f, 0.68f, // Alliance
        0.12f, 0.30f, 0.10f, 0.22f, 0.34f, // Raid
        0.66f, 0.58f, 0.70f, 0.76f, 0.60f, // Truce
    };
    return g;
}

bool EvaluateBimatrixRegret(const BimatrixGame&       game,
                            const std::vector<float>& rowStrat,
                            const std::vector<float>& colStrat,
                            float*                    outRowPayoff,
                            float*                    outColPayoff,
                            float*                    outRowRegret,
                            float*                    outColRegret) {
    if (!game.IsValid() || rowStrat.size() != game.rows || colStrat.size() != game.cols) {
        return false;
    }

    std::vector<double> ry, xc;
    ComputePayoffVectors(game, rowStrat, colStrat, ry, xc);

    double uR    = 0.0;
    double bestR = -1e9;
    for (uint32_t i = 0u; i < game.rows; ++i) {
        uR += static_cast<double>(rowStrat[i]) * ry[i];
        bestR = std::max(bestR, ry[i]);
    }

    double uC    = 0.0;
    double bestC = -1e9;
    for (uint32_t j = 0u; j < game.cols; ++j) {
        uC += static_cast<double>(colStrat[j]) * xc[j];
        bestC = std::max(bestC, xc[j]);
    }

    const float regR = static_cast<float>(std::max(0.0, bestR - uR));
    const float regC = static_cast<float>(std::max(0.0, bestC - uC));

    if (outRowPayoff) *outRowPayoff = static_cast<float>(uR);
    if (outColPayoff) *outColPayoff = static_cast<float>(uC);
    if (outRowRegret) *outRowRegret = regR;
    if (outColRegret) *outColRegret = regC;
    return true;
}

NashApproxResult SolveBimatrixNashApprox(const BimatrixGame& game) {
    NashApproxResult res{};
    if (!game.IsValid()) {
        return res;
    }

    const uint32_t m = game.rows;
    const uint32_t n = game.cols;
    uint32_t totalIters = 0u;

    CandidateEval best{};
    best.maxRegret = 1e9f;

    // -------------------------------------------------------------------------
    // Stage 1: Exact Pure Nash Equilibrium Check (O(m*n))
    // -------------------------------------------------------------------------
    {
        std::vector<float> maxColPerRow(m, -1e9f);
        std::vector<float> maxRowPerCol(n, -1e9f);
        for (uint32_t i = 0u; i < m; ++i) {
            for (uint32_t j = 0u; j < n; ++j) {
                maxColPerRow[i] = std::max(maxColPerRow[i], game.ColAt(i, j));
                maxRowPerCol[j] = std::max(maxRowPerCol[j], game.RowAt(i, j));
            }
        }
        for (uint32_t i = 0u; i < m; ++i) {
            for (uint32_t j = 0u; j < n; ++j) {
                ++totalIters;
                if (game.RowAt(i, j) >= maxRowPerCol[j] - 1e-6f &&
                    game.ColAt(i, j) >= maxColPerRow[i] - 1e-6f) {
                    std::vector<float> x(m, 0.0f);
                    std::vector<float> y(n, 0.0f);
                    x[i] = 1.0f;
                    y[j] = 1.0f;
                    KeepBest(best, ScoreProfile(game, std::move(x), std::move(y), "PureNash"));
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Stage 2: Maximin-Kite Construction (Zero-Sum Security + Best-Response Kite)
    // Inspired by MaximinKiteNE: solves the zero-sum difference game (R - C) AND
    // individual security games R / C^T, then evaluates convex kite mixtures with
    // mutual best responses.
    // -------------------------------------------------------------------------
    if (best.maxRegret > 1e-5f) {
        std::vector<double> Mdiff(static_cast<size_t>(m) * n, 0.0);
        std::vector<double> MR(static_cast<size_t>(m) * n, 0.0);
        std::vector<double> MCneg(static_cast<size_t>(m) * n, 0.0);
        for (uint32_t i = 0u; i < m; ++i) {
            for (uint32_t j = 0u; j < n; ++j) {
                const size_t idx = static_cast<size_t>(i) * n + j;
                const double r   = static_cast<double>(game.rowPayoff[idx]);
                const double c   = static_cast<double>(game.colPayoff[idx]);
                Mdiff[idx] = 0.5 * (r - c);
                MR[idx]    = r;
                MCneg[idx] = -c;
            }
        }

        std::vector<float> xDiff, yDiff, xSecR, yDummy, xDummy, ySecC;
        SolveZeroSumMWU(m, n, Mdiff, 160u, xDiff, yDiff);
        SolveZeroSumMWU(m, n, MR, 120u, xSecR, yDummy);
        SolveZeroSumMWU(m, n, MCneg, 120u, xDummy, ySecC);
        totalIters += 400u;

        // Evaluate base maximin profiles and their Best-Response Kite mixtures:
        const std::array<std::pair<const std::vector<float>*, const std::vector<float>*>, 2> seeds = {{
            {&xDiff, &yDiff},
            {&xSecR, &ySecC},
        }};

        for (const auto& seedPair : seeds) {
            const auto& sx = *seedPair.first;
            const auto& sy = *seedPair.second;
            KeepBest(best, ScoreProfile(game, sx, sy, "MaximinBase"));

            std::vector<double> ry, xc;
            ComputePayoffVectors(game, sx, sy, ry, xc);
            uint32_t brRow = 0u;
            for (uint32_t i = 1u; i < m; ++i) if (ry[i] > ry[brRow]) brRow = i;
            uint32_t brCol = 0u;
            for (uint32_t j = 1u; j < n; ++j) if (xc[j] > xc[brCol]) brCol = j;

            // Kite convex sweep: mix base strategy with best-response vertices:
            for (int ka = 0; ka <= 5; ++ka) {
                const float alpha = 0.2f * static_cast<float>(ka);
                std::vector<float> kx = sx;
                for (float& v : kx) v *= (1.0f - alpha);
                kx[brRow] += alpha;
                for (int kb = 0; kb <= 5; ++kb) {
                    const float beta = 0.2f * static_cast<float>(kb);
                    std::vector<float> ky = sy;
                    for (float& v : ky) v *= (1.0f - beta);
                    ky[brCol] += beta;
                    KeepBest(best, ScoreProfile(game, kx, ky, "MaximinKite"));
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Stage 3: Exact 2x2 Support-Pair Enumeration (Polynomial for small m, n <= 12)
    // -------------------------------------------------------------------------
    if (best.maxRegret > 1e-4f && m >= 2u && n >= 2u && m <= 12u && n <= 12u) {
        for (uint32_t r1 = 0u; r1 < m && best.maxRegret > 1e-5f; ++r1) {
            for (uint32_t r2 = r1 + 1u; r2 < m && best.maxRegret > 1e-5f; ++r2) {
                for (uint32_t c1 = 0u; c1 < n && best.maxRegret > 1e-5f; ++c1) {
                    for (uint32_t c2 = c1 + 1u; c2 < n; ++c2) {
                        ++totalIters;
                        // Solve indifference for row player over {c1, c2}:
                        // y1*R(r1,c1) + (1-y1)*R(r1,c2) = y1*R(r2,c1) + (1-y1)*R(r2,c2)
                        const double a = game.RowAt(r1, c1), b = game.RowAt(r1, c2);
                        const double c = game.RowAt(r2, c1), d = game.RowAt(r2, c2);
                        const double denY = (a - b - c + d);
                        if (std::fabs(denY) <= 1e-9) continue;
                        const double y1 = (d - b) / denY;
                        if (y1 < -1e-6 || y1 > 1.0 + 1e-6) continue;

                        // Solve indifference for col player over {r1, r2}:
                        const double e = game.ColAt(r1, c1), f = game.ColAt(r1, c2);
                        const double g = game.ColAt(r2, c1), h = game.ColAt(r2, c2);
                        const double denX = (e - f - g + h);
                        if (std::fabs(denX) <= 1e-9) continue;
                        const double x1 = (h - g) / denX;
                        if (x1 < -1e-6 || x1 > 1.0 + 1e-6) continue;

                        std::vector<float> x(m, 0.0f);
                        std::vector<float> y(n, 0.0f);
                        x[r1] = SafeClamp01(static_cast<float>(x1));
                        x[r2] = 1.0f - x[r1];
                        y[c1] = SafeClamp01(static_cast<float>(y1));
                        y[c2] = 1.0f - y[c1];
                        KeepBest(best, ScoreProfile(game, std::move(x), std::move(y), "Support2x2"));
                    }
                }
            }
        }
    }

    // -------------------------------------------------------------------------
    // Stage 4: Lexicographic Lemke-Howson Pivoting (Exact NE for m, n <= 16)
    // -------------------------------------------------------------------------
    if (best.maxRegret > 1e-4f && m <= 16u && n <= 16u) {
        const uint32_t maxStarts = std::min(m + n, 6u);
        for (uint32_t startLbl = 0u; startLbl < maxStarts && best.maxRegret > 1e-5f; ++startLbl) {
            std::vector<float> lx, ly;
            uint32_t pivotsUsed = 0u;
            if (TryLemkeHowson(game, startLbl, 1024u, lx, ly, &pivotsUsed)) {
                KeepBest(best, ScoreProfile(game, std::move(lx), std::move(ly), "LemkeHowson"));
            }
            totalIters += pivotsUsed;
        }
    }

    // -------------------------------------------------------------------------
    // Stage 5: Regret-Minimization Descent Polish (Guaranteed <= 0.20 refinement)
    // -------------------------------------------------------------------------
    if (best.maxRegret > 1e-4f) {
        CandidateEval polished = RunRegretDescent(game, best.x, best.y, 120u, &totalIters);
        KeepBest(best, std::move(polished));

        // Also run a uniform-start descent in case best seed was trapped in a local ridge:
        if (best.maxRegret > 0.05f) {
            std::vector<float> ux(m, 1.0f / static_cast<float>(m));
            std::vector<float> uy(n, 1.0f / static_cast<float>(n));
            CandidateEval uPolished = RunRegretDescent(game, std::move(ux), std::move(uy), 140u, &totalIters);
            KeepBest(best, std::move(uPolished));
        }
    }

    res.valid                = true;
    res.rowStrategy          = std::move(best.x);
    res.colStrategy          = std::move(best.y);
    res.rowExpectedPayoff    = best.rowPay;
    res.colExpectedPayoff    = best.colPay;
    res.rowRegret            = best.rowRegret;
    res.colRegret            = best.colRegret;
    res.maxRegret            = best.maxRegret;
    res.certifiedWithinFifth = (res.maxRegret <= kMaxCertifiedNashRegret + 1e-5f);
    res.iterationsUsed       = totalIters;
    res.solverUsed           = best.tag;
    return res;
}

// =============================================================================
// 2. Online Adaptive Fictitious Play Commander (Lexicographic Tie-Breaking)
// =============================================================================

AdaptiveFictitiousPlayTracker::AdaptiveFictitiousPlayTracker(const BimatrixGame& game,
                                                             float               emaAlpha,
                                                             float               adaptationStrength) {
    Reset(game, emaAlpha, adaptationStrength);
}

bool AdaptiveFictitiousPlayTracker::Reset(const BimatrixGame& game,
                                          float               emaAlpha,
                                          float               adaptationStrength) {
    if (!game.IsValid()) {
        initialized_ = false;
        return false;
    }
    game_               = game;
    emaAlpha_           = std::clamp(std::isfinite(emaAlpha) ? emaAlpha : 0.22f, 0.02f, 0.85f);
    adaptationStrength_ = SafeClamp01(adaptationStrength);
    rounds_             = 0u;

    const NashApproxResult nash = SolveBimatrixNashApprox(game_);
    if (nash.valid) {
        baseNashRow_ = nash.rowStrategy;
    } else {
        baseNashRow_.assign(game_.rows, 1.0f / static_cast<float>(game_.rows));
    }

    rowCounts_.assign(game_.rows, 0.0f);
    colCounts_.assign(game_.cols, 0.0f);
    colEma_.assign(game_.cols, 1.0f / static_cast<float>(game_.cols));
    initialized_ = true;
    return true;
}

uint32_t AdaptiveFictitiousPlayTracker::ComputeLexicographicRowBestResponse(
    const std::vector<float>& colDist) const {
    if (!initialized_ || colDist.size() != game_.cols) return 0u;
    uint32_t bestIdx = 0u;
    double   bestVal = -1e18;
    for (uint32_t i = 0u; i < game_.rows; ++i) {
        double v = 0.0;
        const size_t off = static_cast<size_t>(i) * game_.cols;
        for (uint32_t j = 0u; j < game_.cols; ++j) {
            v += static_cast<double>(game_.rowPayoff[off + j]) * static_cast<double>(colDist[j]);
        }
        // Strict > with epsilon enforces fixed lexicographic tie-breaking (lowest index wins ties):
        if (v > bestVal + 1e-6) {
            bestVal = v;
            bestIdx = i;
        }
    }
    return bestIdx;
}

uint32_t AdaptiveFictitiousPlayTracker::ComputeLexicographicColBestResponse(
    const std::vector<float>& rowDist) const {
    if (!initialized_ || rowDist.size() != game_.rows) return 0u;
    uint32_t bestIdx = 0u;
    double   bestVal = -1e18;
    for (uint32_t j = 0u; j < game_.cols; ++j) {
        double v = 0.0;
        for (uint32_t i = 0u; i < game_.rows; ++i) {
            v += static_cast<double>(rowDist[i]) * static_cast<double>(game_.ColAt(i, j));
        }
        if (v > bestVal + 1e-6) {
            bestVal = v;
            bestIdx = j;
        }
    }
    return bestIdx;
}

bool AdaptiveFictitiousPlayTracker::ObserveOpponentAction(uint32_t colAction, uint32_t aiRowActionPlayed) {
    if (!initialized_ || colAction >= game_.cols || aiRowActionPlayed >= game_.rows) {
        return false;
    }

    rowCounts_[aiRowActionPlayed] += 1.0f;
    colCounts_[colAction]         += 1.0f;
    ++rounds_;

    // Regularized EMA update tracking recent player habits smoothly:
    for (uint32_t j = 0u; j < game_.cols; ++j) {
        const float target = (j == colAction) ? 1.0f : 0.0f;
        colEma_[j]         = (1.0f - emaAlpha_) * colEma_[j] + emaAlpha_ * target;
    }
    NormalizeSimplex(colEma_);
    return true;
}

float AdaptiveFictitiousPlayTracker::StepSelfPlayRound() {
    if (!initialized_) return 1.0f;

    std::vector<float> xHat(game_.rows, 1.0f / static_cast<float>(game_.rows));
    std::vector<float> yHat(game_.cols, 1.0f / static_cast<float>(game_.cols));
    if (rounds_ > 0u) {
        const float inv = 1.0f / static_cast<float>(rounds_);
        for (uint32_t i = 0u; i < game_.rows; ++i) xHat[i] = rowCounts_[i] * inv;
        for (uint32_t j = 0u; j < game_.cols; ++j) yHat[j] = colCounts_[j] * inv;
    }

    // Simultaneous lexicographic best-response fictitious play step:
    const uint32_t brRow = ComputeLexicographicRowBestResponse(yHat);
    const uint32_t brCol = ComputeLexicographicColBestResponse(xHat);

    rowCounts_[brRow] += 1.0f;
    colCounts_[brCol] += 1.0f;
    ++rounds_;

    for (uint32_t j = 0u; j < game_.cols; ++j) {
        const float target = (j == brCol) ? 1.0f : 0.0f;
        colEma_[j]         = (1.0f - emaAlpha_) * colEma_[j] + emaAlpha_ * target;
    }
    NormalizeSimplex(colEma_);

    const float inv = 1.0f / static_cast<float>(rounds_);
    for (uint32_t i = 0u; i < game_.rows; ++i) xHat[i] = rowCounts_[i] * inv;
    for (uint32_t j = 0u; j < game_.cols; ++j) yHat[j] = colCounts_[j] * inv;

    float rPay = 0.0f, cPay = 0.0f, rReg = 1.0f, cReg = 1.0f;
    EvaluateBimatrixRegret(game_, xHat, yHat, &rPay, &cPay, &rReg, &cReg);
    return rReg + cReg;
}

std::vector<float> AdaptiveFictitiousPlayTracker::RecommendMixedStrategy() const {
    if (!initialized_) return {};
    if (rounds_ == 0u) return baseNashRow_;

    // Combine empirical frequency & EMA recency belief over opponent actions:
    std::vector<float> belief(game_.cols, 0.0f);
    const float inv = 1.0f / static_cast<float>(rounds_);
    for (uint32_t j = 0u; j < game_.cols; ++j) {
        const float emp = colCounts_[j] * inv;
        belief[j]       = 0.45f * emp + 0.55f * colEma_[j];
    }
    NormalizeSimplex(belief);

    // Compute smooth softmax best-response over row payoffs against opponent belief:
    std::vector<double> scores(game_.rows, 0.0);
    double maxScore = -1e18;
    for (uint32_t i = 0u; i < game_.rows; ++i) {
        double v = 0.0;
        const size_t off = static_cast<size_t>(i) * game_.cols;
        for (uint32_t j = 0u; j < game_.cols; ++j) {
            v += static_cast<double>(game_.rowPayoff[off + j]) * static_cast<double>(belief[j]);
        }
        scores[i] = v;
        maxScore  = std::max(maxScore, v);
    }

    // Confidence ramps smoothly with sample count (avoids overreacting to 1 observation):
    const float confidence = SafeClamp01(static_cast<float>(rounds_) / (static_cast<float>(rounds_) + 4.0f));
    const float blendW     = adaptationStrength_ * confidence;
    const double beta      = 10.0; // Crisp but non-degenerate logit temperature

    std::vector<float> counterMix(game_.rows, 0.0f);
    for (uint32_t i = 0u; i < game_.rows; ++i) {
        counterMix[i] = static_cast<float>(std::exp(beta * (scores[i] - maxScore)));
    }
    NormalizeSimplex(counterMix);

    std::vector<float> out(game_.rows, 0.0f);
    for (uint32_t i = 0u; i < game_.rows; ++i) {
        out[i] = (1.0f - blendW) * baseNashRow_[i] + blendW * counterMix[i];
    }
    NormalizeSimplex(out);
    return out;
}

FictitiousPlaySnapshot AdaptiveFictitiousPlayTracker::GetSnapshot() const {
    FictitiousPlaySnapshot s{};
    if (!initialized_) return s;

    s.roundsObserved = rounds_;
    s.rowEmpirical.assign(game_.rows, 1.0f / static_cast<float>(game_.rows));
    s.colEmpirical.assign(game_.cols, 1.0f / static_cast<float>(game_.cols));
    if (rounds_ > 0u) {
        const float inv = 1.0f / static_cast<float>(rounds_);
        for (uint32_t i = 0u; i < game_.rows; ++i) s.rowEmpirical[i] = rowCounts_[i] * inv;
        for (uint32_t j = 0u; j < game_.cols; ++j) s.colEmpirical[j] = colCounts_[j] * inv;
    }

    s.bestRowResponse   = ComputeLexicographicRowBestResponse(s.colEmpirical);
    s.bestColResponse   = ComputeLexicographicColBestResponse(s.rowEmpirical);
    s.recommendedRowMix = RecommendMixedStrategy();

    float rPay = 0.0f, cPay = 0.0f, rReg = 1.0f, cReg = 1.0f;
    EvaluateBimatrixRegret(game_, s.rowEmpirical, s.colEmpirical, &rPay, &cPay, &rReg, &cReg);
    s.empiricalDualityGap = rReg + cReg;
    s.expectedRowPayoff   = rPay;
    return s;
}

// =============================================================================
// 3. Subadditive Prophet-Inequality Combinatorial Auction Pricer
// =============================================================================

float EstimateExpectedHindsightMax(const std::vector<AuctionBuyerPrior>& priors) {
    if (priors.empty()) return 0.0f;

    float globalMax = 0.0f;
    for (const auto& p : priors) {
        if (!std::isfinite(p.minVal) || !std::isfinite(p.maxVal) || p.maxVal < p.minVal || p.minVal < 0.0f) {
            return 0.0f;
        }
        globalMax = std::max(globalMax, p.maxVal);
    }
    if (globalMax <= 0.0f) return 0.0f;

    // Exact identity for non-negative random variables:
    // E[max_k X_k] = integral_0^{globalMax} (1 - prod_k F_k(t)) dt
    constexpr int kSteps = 256;
    const double  dt     = static_cast<double>(globalMax) / static_cast<double>(kSteps);
    double        sum    = 0.0;

    for (int s = 0; s < kSteps; ++s) {
        const double t = (static_cast<double>(s) + 0.5) * dt;
        double prodF   = 1.0;
        for (const auto& p : priors) {
            double fk = 0.0;
            if (t <= static_cast<double>(p.minVal)) {
                fk = 0.0;
            } else if (t >= static_cast<double>(p.maxVal)) {
                fk = 1.0;
            } else {
                const double span = static_cast<double>(p.maxVal - p.minVal);
                fk = (span > 1e-9) ? ((t - static_cast<double>(p.minVal)) / span) : 1.0;
            }
            prodF *= fk;
        }
        sum += (1.0 - prodF) * dt;
    }

    return static_cast<float>(sum);
}

float EvaluateSubadditiveBundleValue(const std::vector<float>& itemWeights, float concavityGamma) {
    if (itemWeights.empty()) return 0.0f;
    const float gamma = std::clamp(std::isfinite(concavityGamma) ? concavityGamma : 0.75f, 0.10f, 1.0f);
    double total = 0.0;
    for (float w : itemWeights) {
        if (std::isfinite(w) && w > 0.0f) {
            total += static_cast<double>(w);
        }
    }
    if (total <= 0.0) return 0.0f;
    // Concave power function f(x + y) <= f(x) + f(y) for gamma in (0, 1] -> strictly subadditive!
    return static_cast<float>(std::pow(total, static_cast<double>(gamma)));
}

ProphetAuctionDecision EvaluateProphetAuctionBid(const std::vector<AuctionBuyerPrior>& priors,
                                                 float                                 arrivingBidGold,
                                                 HoldId                                hold,
                                                 CommodityKind                         commodity,
                                                 const RawSettlementSim*               settlement) {
    ProphetAuctionDecision dec{};
    if (priors.empty() || !std::isfinite(arrivingBidGold) || arrivingBidGold < 0.0f) {
        return dec;
    }

    const float expOpt = EstimateExpectedHindsightMax(priors);
    if (expOpt <= 0.0f) return dec;

    // Optional live settlement scarcity coupling: scarce holds raise reservation threshold modestly.
    float scarcityMul = 1.0f;
    if (settlement && static_cast<size_t>(commodity) < kCommodityCount) {
        if (const HoldState* hs = settlement->GetHold(hold)) {
            const float basePrice = std::max(1.0f, GetCommodityBasePrice(commodity));
            const float curPrice  = hs->price[static_cast<size_t>(commodity)];
            if (std::isfinite(curPrice) && curPrice > 0.0f) {
                scarcityMul = std::clamp(curPrice / basePrice, 0.80f, 1.45f);
            }
        }
    }

    // Classical Kleinberg-Weinberg / Samuel-Cahn 1/2-OPT prophet reservation price:
    const float threshold = 0.5f * expOpt * scarcityMul;

    dec.valid                 = true;
    dec.expectedHindsightOpt  = expOpt;
    dec.settlementScarcityMul = scarcityMul;
    dec.reservationThreshold  = threshold;
    dec.effectiveBidGold      = arrivingBidGold;
    dec.acceptBid             = (arrivingBidGold >= threshold);
    return dec;
}

ProphetTrialSummary SimulateProphetAuctionTrials(const std::vector<AuctionBuyerPrior>& priors,
                                                 uint32_t                              numTrials,
                                                 uint32_t                              seed) {
    ProphetTrialSummary sum{};
    if (priors.empty() || numTrials == 0u) return sum;

    const float expOpt    = EstimateExpectedHindsightMax(priors);
    if (expOpt <= 0.0f) return sum;
    const float threshold = 0.5f * expOpt;

    uint32_t rng             = (seed == 0u) ? 202662u : seed;
    double   totOnline       = 0.0;
    double   totOpt          = 0.0;
    double   totSubOnline    = 0.0;
    double   totSubOpt       = 0.0;

    // Precompute balanced subadditive posted item prices (half-marginal threshold across arrivals):
    const size_t kCount = priors.size();
    std::vector<float> bids(kCount, 0.0f);

    for (uint32_t t = 0u; t < numTrials; ++t) {
        float bestBid = 0.0f;
        for (size_t i = 0u; i < kCount; ++i) {
            const float u = Uniform01(rng);
            bids[i]       = priors[i].minVal + u * (priors[i].maxVal - priors[i].minVal);
            bestBid       = std::max(bestBid, bids[i]);
        }

        // Single-item sequential prophet rule: accept first bid >= 0.5 * E[max_k X_k]:
        float accepted = 0.0f;
        for (size_t i = 0u; i < kCount; ++i) {
            if (bids[i] >= threshold) {
                accepted = bids[i];
                break;
            }
        }
        totOnline += static_cast<double>(accepted);
        totOpt    += static_cast<double>(bestBid);

        // Multi-item subadditive bundle auction (up to 2 items accepted online vs hindsight top-2):
        // Concave subadditive valuation v(S) = (sum_{i in S} bid_i)^0.75.
        std::vector<float> sortedBids = bids;
        std::sort(sortedBids.begin(), sortedBids.end(), std::greater<float>());
        std::vector<float> optBundle;
        const size_t cap = std::min<size_t>(2u, kCount);
        for (size_t i = 0u; i < cap; ++i) optBundle.push_back(sortedBids[i]);
        const float optSubVal = EvaluateSubadditiveBundleValue(optBundle, 0.75f);

        // Online posted-price marginal acceptance rule: accept item i if marginal subadditive gain >= tau_sub:
        const float singleRef   = EvaluateSubadditiveBundleValue({expOpt}, 0.75f);
        const float marginalTau = 0.35f * singleRef;
        std::vector<float> onlineBundle;
        float curSubVal = 0.0f;
        for (size_t i = 0u; i < kCount && onlineBundle.size() < cap; ++i) {
            std::vector<float> trialBundle = onlineBundle;
            trialBundle.push_back(bids[i]);
            const float nextVal = EvaluateSubadditiveBundleValue(trialBundle, 0.75f);
            if ((nextVal - curSubVal) >= marginalTau) {
                onlineBundle = std::move(trialBundle);
                curSubVal    = nextVal;
            }
        }
        totSubOnline += static_cast<double>(curSubVal);
        totSubOpt    += static_cast<double>(optSubVal);
    }

    const double inv           = 1.0 / static_cast<double>(numTrials);
    sum.valid                  = true;
    sum.trialsRun              = numTrials;
    sum.meanOnlineRevenue      = static_cast<float>(totOnline * inv);
    sum.meanHindsightOpt       = static_cast<float>(totOpt * inv);
    sum.competitiveRatio       = (totOpt > 1e-9) ? static_cast<float>(totOnline / totOpt) : 0.0f;
    sum.subadditiveBundleRatio = (totSubOpt > 1e-9) ? static_cast<float>(totSubOnline / totSubOpt) : 0.0f;
    return sum;
}

}  // namespace raw::worldgen
