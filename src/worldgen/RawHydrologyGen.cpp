// RawHydrologyGen.cpp: Real-Time Hydraulic & Thermal Terrain Erosion + D8 Watershed Hydrology Engine.
// Original work, MIT.

#include "RawHydrologyGen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace raw::worldgen {
namespace {

constexpr float kPiF    = 3.14159265358979323846f;
constexpr float kTwoPiF = 6.28318530717958647692f;
constexpr float kSqrt2F = 1.41421356237309504880f;

// 8-connected Moore neighborhood (D8 directions: E, NE, N, NW, W, SW, S, SE)
constexpr int   kDx[8]   = { 1,  1,  0, -1, -1, -1,  0,  1 };
constexpr int   kDy[8]   = { 0,  1,  1,  1,  0, -1, -1, -1 };
constexpr float kDist[8] = { 1.0f, kSqrt2F, 1.0f, kSqrt2F, 1.0f, kSqrt2F, 1.0f, kSqrt2F };

struct SplitMix32 {
    uint32_t state = 0x9E3779B9u;
    explicit SplitMix32(uint32_t seed) : state(seed ? seed : 0x9E3779B9u) {}

    uint32_t NextU32() {
        uint32_t z = (state += 0x9E3779B9u);
        z = (z ^ (z >> 16u)) * 0x85EBCA6Bu;
        z = (z ^ (z >> 13u)) * 0xC2B2AE35u;
        return z ^ (z >> 16u);
    }

    float Next01() {
        return float(NextU32() & 0x00FFFFFFu) * (1.0f / 16777216.0f);
    }
};

struct BrushOffset {
    int   dx = 0;
    int   dy = 0;
    float w  = 0.0f;
};

std::vector<BrushOffset> BuildGaussianBrush(int radius) {
    const int r = std::clamp(radius, 1, 6);
    const float sigma = std::max(0.75f, float(r) * 0.55f);
    const float inv2Sigma2 = 1.0f / (2.0f * sigma * sigma);

    std::vector<BrushOffset> brush;
    brush.reserve(size_t((2 * r + 1) * (2 * r + 1)));
    float sumW = 0.0f;
    for (int dy = -r; dy <= r; ++dy) {
        for (int dx = -r; dx <= r; ++dx) {
            const float d2 = float(dx * dx + dy * dy);
            if (d2 > float(r * r) + 0.25f) continue;
            const float w = std::exp(-d2 * inv2Sigma2);
            brush.push_back({dx, dy, w});
            sumW += w;
        }
    }
    if (sumW > 1.0e-7f) {
        const float inv = 1.0f / sumW;
        for (auto& b : brush) b.w *= inv;
    }
    return brush;
}

float SampleHeightAndGradBilinear(const std::vector<float>& heights,
                                  uint32_t w,
                                  uint32_t h,
                                  float fx,
                                  float fy,
                                  float cellSizeU,
                                  float* outGx,
                                  float* outGy) {
    const float cx = Clamp(fx, 0.0f, float(w - 2u) + 0.9999f);
    const float cy = Clamp(fy, 0.0f, float(h - 2u) + 0.9999f);
    const uint32_t ix = static_cast<uint32_t>(cx);
    const uint32_t iy = static_cast<uint32_t>(cy);
    const float u = cx - float(ix);
    const float v = cy - float(iy);

    const size_t row0 = size_t(iy) * size_t(w) + size_t(ix);
    const size_t row1 = row0 + size_t(w);
    const float h00 = heights[row0];
    const float h10 = heights[row0 + 1u];
    const float h01 = heights[row1];
    const float h11 = heights[row1 + 1u];

    const float invCell = 1.0f / std::max(cellSizeU, 1.0e-4f);
    if (outGx) *outGx = ((h10 - h00) * (1.0f - v) + (h11 - h01) * v) * invCell;
    if (outGy) *outGy = ((h01 - h00) * (1.0f - u) + (h11 - h10) * u) * invCell;

    const float a = h00 + (h10 - h00) * u;
    const float b = h01 + (h11 - h01) * u;
    return a + (b - a) * v;
}

float ComputeMaxGridSlope(const std::vector<float>& heights,
                          uint32_t width,
                          uint32_t height,
                          float cellSizeU,
                          bool use8Neighbors) {
    const int stepK = use8Neighbors ? 1 : 2;
    const float cs  = std::max(cellSizeU, 1.0e-4f);
    float maxSlope  = 0.0f;

    for (uint32_t y = 0u; y < height; ++y) {
        for (uint32_t x = 0u; x < width; ++x) {
            const float hc = heights[size_t(y) * width + x];
            for (int k = 0; k < 8; k += stepK) {
                const int nx = int(x) + kDx[k];
                const int ny = int(y) + kDy[k];
                if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height)) continue;
                const float diff  = std::fabs(hc - heights[size_t(ny) * width + size_t(nx)]);
                const float slope = diff / (cs * kDist[k]);
                if (slope > maxSlope) maxSlope = slope;
            }
        }
    }
    return maxSlope;
}

void DefaultTerrainGenHydrologyHook(TerrainChunkData& chunk, const TerrainConfig& cfg) {
    HydrologyChunkConfig hcfg{};
    const float cellSize = chunk.chunkSizeU / float(std::max(1u, chunk.gridN));
    hcfg.enableHydraulic = true;
    hcfg.enableThermal   = true;
    hcfg.enableWatershed = true;

    hcfg.hydraulic.seed         = Hash2I(chunk.chunkX, chunk.chunkY, cfg.seed ^ 0x48594452u);
    hcfg.hydraulic.dropletCount = std::max(128u, 768u >> chunk.lodLevel);
    hcfg.hydraulic.maxLifetime  = 48u;
    hcfg.hydraulic.cellSizeU    = cellSize;
    hcfg.hydraulic.erosionRadius = 2;

    hcfg.thermal.iterations     = std::max(4u, cfg.thermalIterations);
    hcfg.thermal.cellSizeU      = cellSize;
    hcfg.thermal.talusAngleDeg  = std::atan(std::max(0.25f, cfg.talusAngleTan)) * (180.0f / kPiF);

    hcfg.watershed.cellSizeU          = cellSize;
    hcfg.watershed.originX            = chunk.originX;
    hcfg.watershed.originY            = chunk.originY;
    hcfg.watershed.riverFlowThreshold = std::max(8.0f, float(chunk.gridN) * 0.28f);
    hcfg.watershed.riverCarveDepthU   = cellSize * 0.35f;
    hcfg.watershed.preserveBorderEdge = true;

    ApplyHydrologyToTerrainChunk(chunk, hcfg, nullptr);
}

}  // namespace

HydraulicErosionStats ApplyHydraulicErosion(std::vector<float>& heights,
                                            uint32_t width,
                                            uint32_t height,
                                            const HydraulicErosionConfig& cfg,
                                            std::vector<float>* outErosionMap,
                                            std::vector<float>* outDepositMap) {
    HydraulicErosionStats st{};
    const size_t count = size_t(width) * size_t(height);
    if (width < 4u || height < 4u || heights.size() != count) {
        st.allFinite = false;
        return st;
    }

    if (outErosionMap) outErosionMap->assign(count, 0.0f);
    if (outDepositMap) outDepositMap->assign(count, 0.0f);

    double sumBefore = 0.0;
    for (float v : heights) {
        if (!std::isfinite(v)) { st.allFinite = false; return st; }
        sumBefore += double(v);
    }

    const float cellSize = std::max(cfg.cellSizeU, 1.0e-4f);
    const float inertia  = Clamp(cfg.inertia, 0.0f, 0.95f);
    const float minSlope = std::max(cfg.minSlope, 1.0e-4f);
    const float capFac   = std::max(cfg.sedimentCapacityFactor, 0.05f);
    const float erodeSpd = Clamp(cfg.erodeSpeed, 0.01f, 1.0f);
    const float depSpd   = Clamp(cfg.depositSpeed, 0.01f, 1.0f);
    const float evapSpd  = Clamp(cfg.evaporateSpeed, 0.001f, 0.5f);
    const float grav     = std::max(cfg.gravity, 0.1f);
    const float maxCut   = std::max(cfg.maxDeltaPerStep, 0.01f) * cellSize;
    const uint32_t maxLife = std::clamp(cfg.maxLifetime, 1u, 512u);

    const std::vector<BrushOffset> brush = BuildGaussianBrush(cfg.erosionRadius);
    SplitMix32 rng(cfg.seed);

    for (uint32_t d = 0u; d < cfg.dropletCount; ++d) {
        float px = 1.0f + rng.Next01() * float(width - 3u);
        float py = 1.0f + rng.Next01() * float(height - 3u);
        float dx = 0.0f;
        float dy = 0.0f;
        float speed    = std::max(cfg.initialSpeed, 0.05f);
        float water    = std::max(cfg.initialWater, 0.05f);
        float sediment = 0.0f;

        ++st.dropletsSimulated;

        for (uint32_t step = 0u; step < maxLife; ++step) {
            const int ix = int(px);
            const int iy = int(py);
            if (ix < 0 || iy < 0 || ix >= int(width) - 1 || iy >= int(height) - 1) break;

            const float u = px - float(ix);
            const float v = py - float(iy);

            float gx = 0.0f, gy = 0.0f;
            const float hOld = SampleHeightAndGradBilinear(heights, width, height, px, py, cellSize, &gx, &gy);

            // Momentum-blended steepest descent direction
            dx = dx * inertia - gx * (1.0f - inertia);
            dy = dy * inertia - gy * (1.0f - inertia);
            const float dLen = std::hypot(dx, dy);
            if (dLen < 1.0e-6f) {
                const float ang = rng.Next01() * kTwoPiF;
                dx = std::cos(ang);
                dy = std::sin(ang);
            } else {
                dx /= dLen;
                dy /= dLen;
            }

            const float nx = px + dx;
            const float ny = py + dy;
            if (nx < 0.0f || ny < 0.0f || nx >= float(width - 1u) || ny >= float(height - 1u)) {
                break;
            }

            const float hNew = SampleHeightAndGradBilinear(heights, width, height, nx, ny, cellSize, nullptr, nullptr);
            const float dH   = hNew - hOld;

            // Stream-power sediment carrying capacity
            const float effSlope = std::max(-dH / cellSize, minSlope);
            const float capacity = effSlope * speed * water * capFac * cellSize;

            if (sediment > capacity || dH > 0.0f) {
                const float deposit = (dH > 0.0f) ? std::min(dH, sediment) : (sediment - capacity) * depSpd;
                if (deposit > 0.0f) {
                    sediment -= deposit;
                    const size_t i00 = size_t(iy) * width + size_t(ix);
                    const size_t i10 = i00 + 1u;
                    const size_t i01 = i00 + width;
                    const size_t i11 = i01 + 1u;
                    const float w00  = (1.0f - u) * (1.0f - v);
                    const float w10  = u * (1.0f - v);
                    const float w01  = (1.0f - u) * v;
                    const float w11  = u * v;

                    heights[i00] += deposit * w00;
                    heights[i10] += deposit * w10;
                    heights[i01] += deposit * w01;
                    heights[i11] += deposit * w11;

                    if (outDepositMap) {
                        (*outDepositMap)[i00] += deposit * w00;
                        (*outDepositMap)[i10] += deposit * w10;
                        (*outDepositMap)[i01] += deposit * w01;
                        (*outDepositMap)[i11] += deposit * w11;
                    }
                    st.totalDepositedVolume += double(deposit);
                    st.maxDepositHeight = std::max(st.maxDepositHeight, deposit);
                }
            } else {
                const float rawErode = std::min((capacity - sediment) * erodeSpd, -dH * 0.90f);
                const float erodeAmt = std::min(rawErode, maxCut);
                if (erodeAmt > 0.0f) {
                    // Renormalize Gaussian weights over in-bounds cells for exact smooth footprint
                    float inBoundsWeight = 0.0f;
                    for (const BrushOffset& bo : brush) {
                        const int bx = ix + bo.dx;
                        const int by = iy + bo.dy;
                        if (bx >= 0 && by >= 0 && bx < int(width) && by < int(height)) {
                            inBoundsWeight += bo.w;
                        }
                    }
                    if (inBoundsWeight > 1.0e-6f) {
                        const float invW = 1.0f / inBoundsWeight;
                        for (const BrushOffset& bo : brush) {
                            const int bx = ix + bo.dx;
                            const int by = iy + bo.dy;
                            if (bx >= 0 && by >= 0 && bx < int(width) && by < int(height)) {
                                const float delta = erodeAmt * (bo.w * invW);
                                const size_t bi = size_t(by) * width + size_t(bx);
                                heights[bi] -= delta;
                                if (outErosionMap) (*outErosionMap)[bi] += delta;
                            }
                        }
                        sediment += erodeAmt;
                        st.totalErodedVolume += double(erodeAmt);
                        st.maxErosionDepth = std::max(st.maxErosionDepth, erodeAmt);
                    }
                }
            }

            // Update droplet velocity & exponential evaporation
            const float accel = std::max(-dH, -cellSize) * (grav / cellSize);
            speed = std::clamp(std::sqrt(std::max(speed * speed + accel, 0.05f)), 0.05f, 20.0f);
            water *= (1.0f - evapSpd);
            px = nx;
            py = ny;
            ++st.totalSteps;
            if (water < 0.01f) break;
        }
    }

    double sumAfter = 0.0;
    for (float v : heights) {
        if (!std::isfinite(v)) { st.allFinite = false; break; }
        sumAfter += double(v);
    }
    st.netHeightChange = sumAfter - sumBefore;
    return st;
}

ThermalErosionStats ApplyThermalErosion(std::vector<float>& heights,
                                        uint32_t width,
                                        uint32_t height,
                                        const ThermalErosionConfig& cfg) {
    ThermalErosionStats st{};
    const size_t count = size_t(width) * size_t(height);
    if (width < 3u || height < 3u || heights.size() != count) {
        st.allFinite = false;
        return st;
    }

    double sumBefore = 0.0;
    for (float v : heights) {
        if (!std::isfinite(v)) { st.allFinite = false; return st; }
        sumBefore += double(v);
    }

    const float cellSize = std::max(cfg.cellSizeU, 1.0e-4f);
    const float angleRad = Clamp(cfg.talusAngleDeg, 5.0f, 80.0f) * (kPiF / 180.0f);
    const float talusTan = std::tan(angleRad);
    const float rate     = Clamp(cfg.transferRate, 0.02f, 0.50f);
    const int   stepK    = cfg.use8Neighbors ? 1 : 2;

    st.maxSlopeBefore = ComputeMaxGridSlope(heights, width, height, cellSize, cfg.use8Neighbors);

    std::vector<float> delta(count, 0.0f);
    for (uint32_t it = 0u; it < cfg.iterations; ++it) {
        std::fill(delta.begin(), delta.end(), 0.0f);
        double iterMoved = 0.0;

        for (uint32_t y = 0u; y < height; ++y) {
            for (uint32_t x = 0u; x < width; ++x) {
                const size_t c = size_t(y) * width + x;
                const float hc = heights[c];

                float excess[8] = {};
                float maxExcess = 0.0f;
                float sumExcess = 0.0f;

                for (int k = 0; k < 8; k += stepK) {
                    const int nx = int(x) + kDx[k];
                    const int ny = int(y) + kDy[k];
                    if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height)) continue;
                    const size_t nb     = size_t(ny) * width + size_t(nx);
                    const float maxDiff = talusTan * cellSize * kDist[k];
                    const float diff    = hc - heights[nb];
                    if (diff > maxDiff) {
                        const float ex = diff - maxDiff;
                        excess[k] = ex;
                        sumExcess += ex;
                        if (ex > maxExcess) maxExcess = ex;
                    }
                }

                if (sumExcess > 1.0e-6f) {
                    const float move = rate * maxExcess;
                    delta[c] -= move;
                    const float invSum = move / sumExcess;
                    for (int k = 0; k < 8; k += stepK) {
                        if (excess[k] > 0.0f) {
                            const size_t nb = size_t(int(y) + kDy[k]) * width + size_t(int(x) + kDx[k]);
                            delta[nb] += excess[k] * invSum;
                        }
                    }
                    iterMoved += double(move);
                }
            }
        }

        for (size_t i = 0; i < count; ++i) {
            heights[i] += delta[i];
        }
        st.totalMassMoved += iterMoved;
        ++st.iterationsRun;
        if (iterMoved <= 1.0e-7) break;
    }

    double sumAfter = 0.0;
    for (float v : heights) {
        if (!std::isfinite(v)) { st.allFinite = false; break; }
        sumAfter += double(v);
    }
    st.netHeightChange = sumAfter - sumBefore;
    st.maxSlopeAfter   = ComputeMaxGridSlope(heights, width, height, cellSize, cfg.use8Neighbors);
    return st;
}

WatershedResult ComputeWatershedHydrology(std::vector<float>& heights,
                                          uint32_t width,
                                          uint32_t height,
                                          const WatershedConfig& cfg) {
    WatershedResult res{};
    const size_t count = size_t(width) * size_t(height);
    if (width < 3u || height < 3u || heights.size() != count) {
        return res;
    }

    res.width  = width;
    res.height = height;
    res.d8Dir.assign(count, int8_t(-1));
    res.flowAccum.assign(count, 1.0f);
    res.riverMask01.assign(count, 0.0f);
    res.riverDepthU.assign(count, 0.0f);
    res.wetness01.assign(count, 0.0f);

    const float cellSize = std::max(cfg.cellSizeU, 1.0e-4f);

    // 1. Compute D8 steepest-descent drainage directions (strictly downhill -> guaranteed DAG)
    std::vector<uint32_t> inDegree(count, 0u);
    std::vector<int32_t>  downIdx(count, -1);

    for (uint32_t y = 0u; y < height; ++y) {
        for (uint32_t x = 0u; x < width; ++x) {
            const size_t c = size_t(y) * width + x;
            const float hc = heights[c];
            if (!std::isfinite(hc)) return res;

            float  bestSlope = 0.0f;
            int8_t bestDir   = -1;
            int32_t bestNb   = -1;

            for (int k = 0; k < 8; ++k) {
                const int nx = int(x) + kDx[k];
                const int ny = int(y) + kDy[k];
                if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height)) continue;
                const size_t nb   = size_t(ny) * width + size_t(nx);
                const float drop  = hc - heights[nb];
                if (drop > 0.0f) {
                    const float slope = drop / (cellSize * kDist[k]);
                    if (slope > bestSlope) {
                        bestSlope = slope;
                        bestDir   = static_cast<int8_t>(k);
                        bestNb    = static_cast<int32_t>(nb);
                    }
                }
            }

            res.d8Dir[c] = bestDir;
            downIdx[c]   = bestNb;
            if (bestNb >= 0) {
                ++inDegree[size_t(bestNb)];
            }
        }
    }

    // 2. Topological upstream drainage accumulation via Kahn's algorithm (O(N))
    std::queue<uint32_t> q;
    for (size_t i = 0; i < count; ++i) {
        if (inDegree[i] == 0u) {
            q.push(static_cast<uint32_t>(i));
        }
    }

    while (!q.empty()) {
        const uint32_t c = q.front();
        q.pop();
        res.maxFlowAccum = std::max(res.maxFlowAccum, res.flowAccum[c]);
        const int32_t nb = downIdx[c];
        if (nb >= 0) {
            res.flowAccum[size_t(nb)] += res.flowAccum[c];
            if (--inDegree[size_t(nb)] == 0u) {
                q.push(static_cast<uint32_t>(nb));
            }
        }
    }

    // 3. Detect steep high-flow escarpment drops as waterfalls (prior to valley carving)
    const float wfMinSlope = std::max(0.10f, cfg.waterfallMinSlopeTan);
    const float wfMinFlow  = std::max(2.0f, cfg.waterfallMinFlow);

    for (uint32_t y = 0u; y < height; ++y) {
        for (uint32_t x = 0u; x < width; ++x) {
            const size_t c = size_t(y) * width + x;
            const int8_t dir = res.d8Dir[c];
            if (dir < 0) continue;
            const float flow = res.flowAccum[c];
            if (flow < wfMinFlow) continue;

            const size_t nb  = static_cast<size_t>(downIdx[c]);
            const float drop = heights[c] - heights[nb];
            const float dist = cellSize * kDist[dir];
            const float slopeTan = drop / dist;
            if (drop > 0.0f && slopeTan >= wfMinSlope) {
                WaterfallPoint wf{};
                wf.gridX       = x;
                wf.gridY       = y;
                wf.worldPos    = {cfg.originX + float(x) * cellSize, cfg.originY + float(y) * cellSize, heights[c]};
                wf.flowDir     = Normalize(Vec2{float(kDx[dir]), float(kDy[dir])});
                wf.dropHeightU = drop;
                wf.slopeTan    = slopeTan;
                wf.flowAccum   = flow;
                res.waterfalls.push_back(wf);
            }
        }
    }

    std::sort(res.waterfalls.begin(), res.waterfalls.end(), [](const WaterfallPoint& a, const WaterfallPoint& b) {
        const float pa = a.flowAccum * a.dropHeightU;
        const float pb = b.flowAccum * b.dropHeightU;
        if (std::fabs(pa - pb) > 1.0e-5f) return pa > pb;
        return (a.gridY != b.gridY) ? (a.gridY < b.gridY) : (a.gridX < b.gridX);
    });

    // 4. River channel intensity mask & smooth riverbank wetness dilation
    const float flowLo = std::max(2.0f, cfg.riverFlowThreshold * 0.65f);
    const float flowHi = std::max(flowLo + 4.0f, cfg.riverFlowThreshold * 3.2f);
    const float maxCarveDepth = (cfg.riverCarveDepthU > 0.0f) ? cfg.riverCarveDepthU : (cellSize * 0.55f);

    for (size_t i = 0; i < count; ++i) {
        const float mask = Smoothstep(flowLo, flowHi, res.flowAccum[i]);
        res.riverMask01[i] = mask;
        if (res.flowAccum[i] >= cfg.riverFlowThreshold) {
            ++res.riverCellCount;
        }
    }

    // Dilate riverbank wetness within 2 cells around active channels
    for (uint32_t y = 0u; y < height; ++y) {
        for (uint32_t x = 0u; x < width; ++x) {
            float wet = res.riverMask01[size_t(y) * width + x];
            for (int dy = -2; dy <= 2; ++dy) {
                for (int dx = -2; dx <= 2; ++dx) {
                    const int nx = int(x) + dx;
                    const int ny = int(y) + dy;
                    if (nx < 0 || ny < 0 || nx >= int(width) || ny >= int(height)) continue;
                    const float d = std::hypot(float(dx), float(dy));
                    if (d <= 2.25f) {
                        const float bankW = res.riverMask01[size_t(ny) * width + size_t(nx)] * std::max(0.0f, 1.0f - d * 0.38f);
                        wet = std::max(wet, bankW);
                    }
                }
            }
            res.wetness01[size_t(y) * width + x] = Clamp01(wet);
        }
    }

    // 5. Continuous downhill river valley carving (smooth channel + gentle bank taper)
    if (cfg.carveRiverValleys && maxCarveDepth > 0.0f) {
        std::vector<float> carveField(count, 0.0f);
        for (uint32_t y = 0u; y < height; ++y) {
            const float edgeY = std::min(float(y), float(height - 1u - y));
            const float fadeY = cfg.preserveBorderEdge ? Smoothstep(0.0f, 2.0f, edgeY) : 1.0f;
            for (uint32_t x = 0u; x < width; ++x) {
                const float edgeX = std::min(float(x), float(width - 1u - x));
                const float fade  = fadeY * (cfg.preserveBorderEdge ? Smoothstep(0.0f, 2.0f, edgeX) : 1.0f);
                const size_t idx  = size_t(y) * width + x;
                const float rMask = res.riverMask01[idx];
                const float bankM = res.wetness01[idx];
                const float depth = maxCarveDepth * (0.78f * std::pow(rMask, 0.85f) + 0.22f * bankM * bankM) * fade;
                carveField[idx]   = depth;
            }
        }

        for (size_t i = 0; i < count; ++i) {
            heights[i] -= carveField[i];
            res.riverDepthU[i] = carveField[i];
        }
    }

    res.ok = true;
    return res;
}

bool ApplyHydrologyToTerrainChunk(TerrainChunkData& chunk,
                                  const HydrologyChunkConfig& cfg,
                                  WatershedResult* outWatershed) {
    const uint32_t S = chunk.stride;
    const size_t count = size_t(S) * size_t(S);
    if (S < 5u || chunk.heights.size() != count) {
        return false;
    }

    const float cellStep = chunk.chunkSizeU / float(std::max(1u, chunk.gridN));
    const std::vector<float> origHeights = chunk.heights;
    std::vector<float> workH = chunk.heights;
    std::vector<float> erosionMap;

    if (cfg.enableHydraulic && cfg.hydraulic.dropletCount > 0u) {
        HydraulicErosionConfig hc = cfg.hydraulic;
        if (hc.cellSizeU <= 1.0e-4f) hc.cellSizeU = cellStep;
        ApplyHydraulicErosion(workH, S, S, hc, &erosionMap, nullptr);
    }

    if (cfg.enableThermal && cfg.thermal.iterations > 0u) {
        ThermalErosionConfig tc = cfg.thermal;
        if (tc.cellSizeU <= 1.0e-4f) tc.cellSizeU = cellStep;
        ApplyThermalErosion(workH, S, S, tc);
    }

    WatershedResult ws{};
    if (cfg.enableWatershed) {
        WatershedConfig wc = cfg.watershed;
        if (wc.cellSizeU <= 1.0e-4f) wc.cellSizeU = cellStep;
        wc.originX            = chunk.originX;
        wc.originY            = chunk.originY;
        wc.preserveBorderEdge = true;
        ws = ComputeWatershedHydrology(workH, S, S, wc);
    }

    // Blend modified heights back with 2-cell border taper so adjacent chunk seams stay bit-exact watertight!
    float minZ = std::numeric_limits<float>::max();
    float maxZ = std::numeric_limits<float>::lowest();
    for (uint32_t iy = 0u; iy < S; ++iy) {
        const float fadeY = Smoothstep(0.0f, 2.0f, std::min(float(iy), float(S - 1u - iy)));
        for (uint32_t ix = 0u; ix < S; ++ix) {
            const float fade = fadeY * Smoothstep(0.0f, 2.0f, std::min(float(ix), float(S - 1u - ix)));
            const size_t vi  = size_t(iy) * S + ix;
            const float bMask = (vi < chunk.borderBlend.size()) ? chunk.borderBlend[vi] : 1.0f;
            const float hNew  = Lerp(origHeights[vi], workH[vi], fade * bMask);
            chunk.heights[vi] = hNew;
            minZ = std::min(minZ, hNew);
            maxZ = std::max(maxZ, hNew);
        }
    }
    chunk.minZ = minZ;
    chunk.maxZ = maxZ;

    // Recompute exact central-difference normals & mean curvature on interior vertices (keep border normals clean)
    const float invStep  = 1.0f / std::max(cellStep, 1.0e-4f);
    const float inv2Step = 0.5f * invStep;
    const float invStep2 = invStep * invStep;

    if (chunk.normals.size() != count)    chunk.normals.assign(count, {0.0f, 0.0f, 1.0f});
    if (chunk.curvatures.size() != count) chunk.curvatures.assign(count, 0.0f);
    if (chunk.flow.size() != count)       chunk.flow.assign(count, 0.0f);
    if (chunk.erosion.size() != count)    chunk.erosion.assign(count, 0.0f);
    if (chunk.wetness.size() != count)    chunk.wetness.assign(count, 0.0f);
    if (chunk.riverDepth.size() != count) chunk.riverDepth.assign(count, 0.0f);
    if (chunk.splat0.size() != count)     chunk.splat0.assign(count, {1.0f, 0.0f, 0.0f, 0.0f});
    if (chunk.splat1.size() != count)     chunk.splat1.assign(count, {0.0f, 0.0f, 0.0f, 0.0f});

    const float bankStrength = Clamp01(cfg.bankSplatStrength);
    for (uint32_t iy = 0u; iy < S; ++iy) {
        for (uint32_t ix = 0u; ix < S; ++ix) {
            const size_t vi = size_t(iy) * S + ix;
            const uint32_t xL = (ix > 0u) ? (ix - 1u) : ix;
            const uint32_t xR = (ix + 1u < S) ? (ix + 1u) : ix;
            const uint32_t yD = (iy > 0u) ? (iy - 1u) : iy;
            const uint32_t yU = (iy + 1u < S) ? (iy + 1u) : iy;

            const float hC = chunk.heights[vi];
            const float hL = chunk.heights[size_t(iy) * S + xL];
            const float hR = chunk.heights[size_t(iy) * S + xR];
            const float hD = chunk.heights[size_t(yD) * S + ix];
            const float hU = chunk.heights[size_t(yU) * S + ix];

            const float dhdx = (xR != xL) ? ((hR - hL) * ((xR - xL == 2u) ? inv2Step : invStep)) : 0.0f;
            const float dhdy = (yU != yD) ? ((hU - hD) * ((yU - yD == 2u) ? inv2Step : invStep)) : 0.0f;
            if (ix > 0u && ix + 1u < S && iy > 0u && iy + 1u < S) {
                chunk.normals[vi] = Normalize({-dhdx, -dhdy, 1.0f});
                const float lap   = (hL + hR + hD + hU - 4.0f * hC) * invStep2;
                chunk.curvatures[vi] = Clamp(-lap * 140.0f, -1.0f, 1.0f);
            }

            if (!erosionMap.empty()) {
                chunk.erosion[vi] = Clamp01(std::max(chunk.erosion[vi], erosionMap[vi] / std::max(cellStep * 0.25f, 1.0f)));
            }

            if (ws.ok) {
                const float normFlow = (ws.maxFlowAccum > 1.0f)
                                         ? Clamp01(std::log2(ws.flowAccum[vi]) / std::log2(ws.maxFlowAccum + 1.0f))
                                         : 0.0f;
                chunk.flow[vi]       = Clamp01(std::max(chunk.flow[vi], normFlow));
                chunk.riverDepth[vi] = std::max(chunk.riverDepth[vi], ws.riverDepthU[vi]);
                chunk.wetness[vi]    = Clamp01(std::max(chunk.wetness[vi], ws.wetness01[vi]));

                // Blend splat1.z (RiverPebbleSand) along carved riverbanks while keeping 8-channel sum == 1.0!
                const float pebbleAdd = Clamp01(ws.wetness01[vi] * bankStrength);
                if (pebbleAdd > 1.0e-4f) {
                    const float keep = 1.0f - pebbleAdd;
                    chunk.splat0[vi].x *= keep;
                    chunk.splat0[vi].y *= keep;
                    chunk.splat0[vi].z *= keep;
                    chunk.splat0[vi].w *= keep;
                    chunk.splat1[vi].x *= keep;
                    chunk.splat1[vi].y *= keep;
                    chunk.splat1[vi].z = chunk.splat1[vi].z * keep + pebbleAdd;
                    chunk.splat1[vi].w *= keep;

                    // Renormalize strictly to 1.0f for ValidateProceduralMeshStrict compliance
                    const float sum = chunk.splat0[vi].x + chunk.splat0[vi].y + chunk.splat0[vi].z + chunk.splat0[vi].w +
                                      chunk.splat1[vi].x + chunk.splat1[vi].y + chunk.splat1[vi].z + chunk.splat1[vi].w;
                    if (sum > 1.0e-6f) {
                        const float inv = 1.0f / sum;
                        chunk.splat0[vi].x *= inv; chunk.splat0[vi].y *= inv; chunk.splat0[vi].z *= inv; chunk.splat0[vi].w *= inv;
                        chunk.splat1[vi].x *= inv; chunk.splat1[vi].y *= inv; chunk.splat1[vi].z *= inv; chunk.splat1[vi].w *= inv;
                    }
                }
            }
        }
    }

    if (outWatershed) *outWatershed = std::move(ws);
    return true;
}

void SetTerrainGenHydrologyErosionEnabled(bool enabled) noexcept {
    RegisterTerrainGenHydrologyHook(&DefaultTerrainGenHydrologyHook, enabled);
}

bool IsTerrainGenHydrologyErosionEnabled() noexcept {
    return QueryTerrainGenHydrologyHookEnabled();
}

}  // namespace raw::worldgen
