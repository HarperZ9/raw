// RawNavMeshAi.cpp: Clean-Room 3D NavMesh Baker, Octile A* Funnel Pathfinder & Tactical Squad Flanking AI.
// Original work, MIT. Zero Windows / D3D12 / Recast / Havok dependencies.

#include "RawNavMeshAi.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <queue>

namespace raw::worldgen {
namespace {

constexpr float kPi       = 3.14159265358979323846f;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kSqrt2    = 1.4142135623730950488f;
constexpr float kInfCost  = 1.0e30f;

// 8-way neighbor offsets matching passableNeighborMask bits 0..7:
// 0:E, 1:NE, 2:N, 3:NW, 4:W, 5:SW, 6:S, 7:SE
constexpr int32_t kDirX[8] = { 1,  1,  0, -1, -1, -1,  0,  1};
constexpr int32_t kDirY[8] = { 0,  1,  1,  1,  0, -1, -1, -1};
constexpr float   kDirDist[8] = {1.0f, kSqrt2, 1.0f, kSqrt2, 1.0f, kSqrt2, 1.0f, kSqrt2};

inline bool IsFiniteVec3(Vec3 v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

inline Vec2 NormalizeXY(Vec3 v) noexcept {
    const float len = std::sqrt(v.x * v.x + v.y * v.y);
    return (len > 1.0e-5f) ? Vec2{v.x / len, v.y / len} : Vec2{1.0f, 0.0f};
}

inline float DistXY(Vec3 a, Vec3 b) noexcept {
    const float dx = a.x - b.x;
    const float dy = a.y - b.y;
    return std::sqrt(dx * dx + dy * dy);
}

bool HasHorizontalLineOfSight(const RawWorldPhysics& physics, Vec3 a, Vec3 b, float eyeOffsetZ) {
    const Vec3 p0{a.x, a.y, a.z + eyeOffsetZ};
    const Vec3 p1{b.x, b.y, b.z + eyeOffsetZ};
    const Vec3 delta = p1 - p0;
    const float dist = Length(delta);
    if (dist < 1.0e-3f) {
        return true;
    }
    const RayHit hit = physics.RayCast(p0, delta / dist, dist - 2.0f);
    return !hit.hit;
}

}  // namespace

// ============================================================================
// RawNavMesh Implementation
// ============================================================================

void RawNavMesh::Clear() {
    gridW_ = 0u;
    gridH_ = 0u;
    cells_.clear();
    stats_ = {};
}

bool RawNavMesh::WorldToGrid(float worldX, float worldY, int32_t* outGx, int32_t* outGy) const {
    if (!IsBaked() || !std::isfinite(worldX) || !std::isfinite(worldY)) {
        return false;
    }
    const int32_t gx = static_cast<int32_t>(std::floor((worldX - cfg_.originX) / cfg_.cellSizeU));
    const int32_t gy = static_cast<int32_t>(std::floor((worldY - cfg_.originY) / cfg_.cellSizeU));
    if (gx < 0 || gy < 0 || gx >= static_cast<int32_t>(gridW_) || gy >= static_cast<int32_t>(gridH_)) {
        return false;
    }
    if (outGx) *outGx = gx;
    if (outGy) *outGy = gy;
    return true;
}

Vec3 RawNavMesh::GridToWorldCenter(int32_t gx, int32_t gy) const {
    const float cx = cfg_.originX + (static_cast<float>(gx) + 0.5f) * cfg_.cellSizeU;
    const float cy = cfg_.originY + (static_cast<float>(gy) + 0.5f) * cfg_.cellSizeU;
    float cz = 0.0f;
    if (const NavCell* c = GetCell(gx, gy)) {
        cz = c->heightZ;
    }
    return {cx, cy, cz};
}

const NavCell* RawNavMesh::GetCell(int32_t gx, int32_t gy) const {
    if (gx < 0 || gy < 0 || gx >= static_cast<int32_t>(gridW_) || gy >= static_cast<int32_t>(gridH_)) {
        return nullptr;
    }
    return &cells_[static_cast<size_t>(gy) * gridW_ + static_cast<size_t>(gx)];
}

bool RawNavMesh::BakeFromPhysics(const RawWorldPhysics& physics, const NavBakeConfig& cfg) {
    Clear();
    if (!std::isfinite(cfg.sizeX) || !std::isfinite(cfg.sizeY) || !std::isfinite(cfg.cellSizeU) ||
        cfg.sizeX <= 1.0f || cfg.sizeY <= 1.0f || cfg.cellSizeU < 8.0f) {
        return false;
    }

    const auto t0 = std::chrono::steady_clock::now();

    cfg_   = cfg;
    gridW_ = std::max(1u, static_cast<uint32_t>(std::ceil(cfg.sizeX / cfg.cellSizeU)));
    gridH_ = std::max(1u, static_cast<uint32_t>(std::ceil(cfg.sizeY / cfg.cellSizeU)));
    cells_.assign(static_cast<size_t>(gridW_) * static_cast<size_t>(gridH_), NavCell{});

    const float cosMaxSlope   = std::cos(Clamp(cfg.maxSlopeDeg, 5.0f, 85.0f) * kDegToRad);
    const float cosSteepSlope = std::cos(Clamp(cfg.steepSlopeDeg, 1.0f, cfg.maxSlopeDeg) * kDegToRad);

    // Pass 1: Sample elevation, surface normal, water depth, and overhead clearance for every cell.
    for (uint32_t gy = 0u; gy < gridH_; ++gy) {
        const float cy = cfg_.originY + (static_cast<float>(gy) + 0.5f) * cfg_.cellSizeU;
        for (uint32_t gx = 0u; gx < gridW_; ++gx) {
            const float cx = cfg_.originX + (static_cast<float>(gx) + 0.5f) * cfg_.cellSizeU;
            NavCell& cell  = cells_[static_cast<size_t>(gy) * gridW_ + gx];

            const RayHit hit = physics.RayCast({cx, cy, cfg_.rayStartZ}, {0.0f, 0.0f, -1.0f}, cfg_.rayMaxDist);
            float waterZ = physics.DefaultWaterLevel();
            float terrainZ = 0.0f;
            Vec3  terrainN{0.0f, 0.0f, 1.0f};
            const bool hasTerrain = physics.SampleTerrainHeight(cx, cy, &terrainZ, &terrainN, &waterZ, nullptr);

            float surfaceZ = 0.0f;
            Vec3  surfaceN{0.0f, 0.0f, 1.0f};
            if (hit.hit) {
                surfaceZ = hit.point[2];
                surfaceN = Vec3{hit.normal[0], hit.normal[1], hit.normal[2]};
            } else if (hasTerrain) {
                surfaceZ = terrainZ;
                surfaceN = terrainN;
            } else {
                cell.tag           = NavCellTag::Blocked;
                cell.traversalCost = kInfCost;
                continue;
            }

            cell.heightZ = surfaceZ;
            cell.normal  = surfaceN;

            // 1. Walkable slope check:
            if (surfaceN.z < cosMaxSlope) {
                cell.tag           = NavCellTag::Blocked;
                cell.traversalCost = kInfCost;
                continue;
            }

            // 2. Agent headroom clearance check (>= 130u upward raycast):
            if (cfg_.agentHeadroomU > 1.0f) {
                const RayHit headHit = physics.RayCast({cx, cy, surfaceZ + 4.0f},
                                                       {0.0f, 0.0f, 1.0f},
                                                       cfg_.agentHeadroomU);
                if (headHit.hit) {
                    cell.tag           = NavCellTag::Blocked;
                    cell.traversalCost = kInfCost;
                    continue;
                }
            }

            // 3. Water depth classification:
            const float waterDepth = waterZ - surfaceZ;
            if (waterDepth > cfg_.maxShallowWaterDepthU) {
                cell.tag           = NavCellTag::Blocked;
                cell.traversalCost = kInfCost;
                continue;
            }
            if (waterDepth > 2.0f) {
                cell.tag           = NavCellTag::ShallowWater;
                cell.traversalCost = 2.4f;
            } else if (surfaceN.z < cosSteepSlope) {
                cell.tag           = NavCellTag::SteepSlope;
                cell.traversalCost = 1.8f;
            } else {
                cell.tag           = NavCellTag::Walkable;
                cell.traversalCost = 1.0f;
            }
        }
    }

    // Pass 2: Precompute cardinal passability (step height continuity + thin-wall edge clearance).
    // Checking symmetric E (bit 0) and N (bit 2) pairs halves raycasts!
    auto linkCardinal = [&](uint32_t xA, uint32_t yA, uint8_t bitAB,
                            uint32_t xB, uint32_t yB, uint8_t bitBA) {
        NavCell& a = cells_[static_cast<size_t>(yA) * gridW_ + xA];
        NavCell& b = cells_[static_cast<size_t>(yB) * gridW_ + xB];
        if (a.tag == NavCellTag::Blocked || b.tag == NavCellTag::Blocked) {
            return;
        }
        if (std::abs(a.heightZ - b.heightZ) > cfg_.maxStepHeightU) {
            return;
        }
        if (cfg_.checkHorizontalWalls) {
            const Vec3 pA = GridToWorldCenter(static_cast<int32_t>(xA), static_cast<int32_t>(yA));
            const Vec3 pB = GridToWorldCenter(static_cast<int32_t>(xB), static_cast<int32_t>(yB));
            const float baseZ = std::max(a.heightZ, b.heightZ);
            const Vec3 segA{pA.x, pA.y, baseZ + 48.0f};
            const Vec3 segB{pB.x, pB.y, baseZ + 48.0f};
            const Vec3 delta = segB - segA;
            const float dist = Length(delta);
            if (dist > 1.0e-3f) {
                const RayHit wallHit = physics.RayCast(segA, delta / dist, dist);
                if (wallHit.hit) {
                    return;
                }
            }
        }
        a.passableNeighborMask |= static_cast<uint8_t>(1u << bitAB);
        b.passableNeighborMask |= static_cast<uint8_t>(1u << bitBA);
    };

    for (uint32_t gy = 0u; gy < gridH_; ++gy) {
        for (uint32_t gx = 0u; gx < gridW_; ++gx) {
            if (gx + 1u < gridW_) linkCardinal(gx, gy, 0u, gx + 1u, gy, 4u); // E <-> W
            if (gy + 1u < gridH_) linkCardinal(gx, gy, 2u, gx, gy + 1u, 6u); // N <-> S
        }
    }

    // Pass 3: Precompute diagonal passability with STRICT corner-cutting prevention:
    // A diagonal move (gx,gy) -> (gx+dx, gy+dy) is valid ONLY if BOTH flanking cardinal moves are open
    // from (gx,gy) AND from the flanking cells into (gx+dx, gy+dy), plus height step continuity!
    auto linkDiagonal = [&](uint32_t gx, uint32_t gy, uint8_t diagBit,
                            uint8_t cardBit1, int32_t dx1, int32_t dy1,
                            uint8_t cardBit2, int32_t dx2, int32_t dy2) {
        const int32_t nx = static_cast<int32_t>(gx) + dx1 + dx2;
        const int32_t ny = static_cast<int32_t>(gy) + dy1 + dy2;
        if (nx < 0 || ny < 0 || nx >= static_cast<int32_t>(gridW_) || ny >= static_cast<int32_t>(gridH_)) {
            return;
        }
        NavCell& self = cells_[static_cast<size_t>(gy) * gridW_ + gx];
        if (self.tag == NavCellTag::Blocked) return;

        // Both cardinal steps from self must be passable:
        if ((self.passableNeighborMask & (1u << cardBit1)) == 0u ||
            (self.passableNeighborMask & (1u << cardBit2)) == 0u) {
            return;
        }
        const NavCell& side1 = cells_[static_cast<size_t>(static_cast<int32_t>(gy) + dy1) * gridW_ +
                                      static_cast<size_t>(static_cast<int32_t>(gx) + dx1)];
        const NavCell& side2 = cells_[static_cast<size_t>(static_cast<int32_t>(gy) + dy2) * gridW_ +
                                      static_cast<size_t>(static_cast<int32_t>(gx) + dx2)];
        // Both flanking cells must connect into the diagonal destination:
        if ((side1.passableNeighborMask & (1u << cardBit2)) == 0u ||
            (side2.passableNeighborMask & (1u << cardBit1)) == 0u) {
            return;
        }
        const NavCell& dest = cells_[static_cast<size_t>(ny) * gridW_ + static_cast<size_t>(nx)];
        if (dest.tag == NavCellTag::Blocked ||
            std::abs(dest.heightZ - self.heightZ) > cfg_.maxStepHeightU) {
            return;
        }
        self.passableNeighborMask |= static_cast<uint8_t>(1u << diagBit);
    };

    for (uint32_t gy = 0u; gy < gridH_; ++gy) {
        for (uint32_t gx = 0u; gx < gridW_; ++gx) {
            linkDiagonal(gx, gy, 1u, 0u,  1,  0, 2u,  0,  1); // NE via E & N
            linkDiagonal(gx, gy, 3u, 4u, -1,  0, 2u,  0,  1); // NW via W & N
            linkDiagonal(gx, gy, 5u, 4u, -1,  0, 6u,  0, -1); // SW via W & S
            linkDiagonal(gx, gy, 7u, 0u,  1,  0, 6u,  0, -1); // SE via E & S
        }
    }

    // Gather bake stats:
    stats_ = {};
    stats_.gridW      = gridW_;
    stats_.gridH      = gridH_;
    stats_.totalCells = gridW_ * gridH_;
    for (const NavCell& c : cells_) {
        switch (c.tag) {
            case NavCellTag::Walkable:     ++stats_.walkableCells; break;
            case NavCellTag::SteepSlope:   ++stats_.steepCells; break;
            case NavCellTag::ShallowWater: ++stats_.shallowWaterCells; break;
            default:                       ++stats_.blockedCells; break;
        }
        uint8_t m = c.passableNeighborMask;
        while (m != 0u) {
            stats_.totalLinks += (m & 1u);
            m >>= 1u;
        }
    }

    const auto t1 = std::chrono::steady_clock::now();
    stats_.bakeTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return true;
}

bool RawNavMesh::FindNearestWalkableCell(Vec3 worldPos, int32_t searchRadiusCells,
                                         int32_t* outGx, int32_t* outGy) const {
    if (!IsBaked() || !IsFiniteVec3(worldPos)) {
        return false;
    }
    const int32_t cx = static_cast<int32_t>(std::floor((worldPos.x - cfg_.originX) / cfg_.cellSizeU));
    const int32_t cy = static_cast<int32_t>(std::floor((worldPos.y - cfg_.originY) / cfg_.cellSizeU));

    bool found    = false;
    float bestScore = kInfCost;
    int32_t bestX = 0, bestY = 0;

    const int32_t rad = std::max(0, searchRadiusCells);
    for (int32_t dy = -rad; dy <= rad; ++dy) {
        for (int32_t dx = -rad; dx <= rad; ++dx) {
            const int32_t gx = cx + dx;
            const int32_t gy = cy + dy;
            const NavCell* cell = GetCell(gx, gy);
            if (!cell || cell->tag == NavCellTag::Blocked) {
                continue;
            }
            const Vec3 cellPos = GridToWorldCenter(gx, gy);
            const float dxy    = DistXY(cellPos, worldPos);
            const float dz     = std::abs(cellPos.z - worldPos.z);
            // Strongly prefer cells at the matching elevation (so ground queries never snap onto roofs/wall tops):
            const float score  = dxy + 2.5f * dz;
            if (score < bestScore) {
                bestScore = score;
                bestX     = gx;
                bestY     = gy;
                found     = true;
            }
        }
    }
    if (found) {
        if (outGx) *outGx = bestX;
        if (outGy) *outGy = bestY;
    }
    return found;
}

bool RawNavMesh::IsSegmentClearOnGrid(int32_t x0, int32_t y0, int32_t x1, int32_t y1,
                                      const RawWorldPhysics* physicsOpt) const {
    const int32_t dx = x1 - x0;
    const int32_t dy = y1 - y0;
    const int32_t steps = std::max(std::abs(dx), std::abs(dy)) * 2;
    if (steps <= 0) {
        return true;
    }

    const NavCell* startCell = GetCell(x0, y0);
    const NavCell* endCell   = GetCell(x1, y1);
    if (!startCell || !endCell || startCell->tag == NavCellTag::Blocked || endCell->tag == NavCellTag::Blocked) {
        return false;
    }

    // Supercover grid sampling: every touched cell + small margin must be walkable and height-continuous.
    int32_t prevGx = x0;
    int32_t prevGy = y0;
    float   prevZ  = startCell->heightZ;

    for (int32_t i = 1; i <= steps; ++i) {
        const float t  = static_cast<float>(i) / static_cast<float>(steps);
        const float fx = static_cast<float>(x0) + static_cast<float>(dx) * t;
        const float fy = static_cast<float>(y0) + static_cast<float>(dy) * t;
        const int32_t gx = static_cast<int32_t>(std::lround(fx));
        const int32_t gy = static_cast<int32_t>(std::lround(fy));

        const NavCell* c = GetCell(gx, gy);
        if (!c || c->tag == NavCellTag::Blocked) {
            return false;
        }
        // Prevent string-pull from cutting across steep/water penalties when clean walkable path exists:
        if (c->traversalCost > std::max(startCell->traversalCost, endCell->traversalCost) + 0.5f) {
            return false;
        }
        if (std::abs(c->heightZ - prevZ) > cfg_.maxStepHeightU) {
            return false;
        }

        // Prevent corner clipping when stepping diagonally across supercover samples:
        if (gx != prevGx && gy != prevGy) {
            const NavCell* cA = GetCell(gx, prevGy);
            const NavCell* cB = GetCell(prevGx, gy);
            if (!cA || !cB || cA->tag == NavCellTag::Blocked || cB->tag == NavCellTag::Blocked ||
                std::abs(cA->heightZ - prevZ) > cfg_.maxStepHeightU ||
                std::abs(cB->heightZ - prevZ) > cfg_.maxStepHeightU) {
                return false;
            }
        }
        prevGx = gx;
        prevGy = gy;
        prevZ  = c->heightZ;
    }

    // Physics ray verification along the smoothed chord at waist & chest height:
    if (physicsOpt) {
        const Vec3 p0 = GridToWorldCenter(x0, y0);
        const Vec3 p1 = GridToWorldCenter(x1, y1);
        if (!HasHorizontalLineOfSight(*physicsOpt, p0, p1, 48.0f) ||
            !HasHorizontalLineOfSight(*physicsOpt, p0, p1, 96.0f)) {
            return false;
        }
    }
    return true;
}

NavPathResult RawNavMesh::FindPath(Vec3 startWorld, Vec3 goalWorld,
                                   const RawWorldPhysics* physicsOpt,
                                   uint32_t maxExpandedNodes) const {
    NavPathResult result{};
    if (!IsBaked() || !IsFiniteVec3(startWorld) || !IsFiniteVec3(goalWorld) || maxExpandedNodes == 0u) {
        return result;
    }

    const auto t0 = std::chrono::steady_clock::now();

    int32_t startGx = 0, startGy = 0;
    int32_t goalGx  = 0, goalGy  = 0;
    if (!FindNearestWalkableCell(startWorld, 2, &startGx, &startGy) ||
        !FindNearestWalkableCell(goalWorld, 2, &goalGx, &goalGy)) {
        const auto t1 = std::chrono::steady_clock::now();
        result.searchTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return result;
    }

    const uint32_t totalCells = gridW_ * gridH_;
    const uint32_t startIdx   = static_cast<uint32_t>(startGy) * gridW_ + static_cast<uint32_t>(startGx);
    const uint32_t goalIdx    = static_cast<uint32_t>(goalGy)  * gridW_ + static_cast<uint32_t>(goalGx);

    if (startIdx == goalIdx) {
        result.found        = true;
        result.rawGridNodes = 1u;
        result.waypoints.push_back({GridToWorldCenter(startGx, startGy), cells_[startIdx].tag});
        const auto t1 = std::chrono::steady_clock::now();
        result.searchTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return result;
    }

    std::vector<float>    gScore(totalCells, kInfCost);
    std::vector<uint32_t> parent(totalCells, std::numeric_limits<uint32_t>::max());
    std::vector<uint8_t>  closed(totalCells, 0u);

    auto heuristic = [&](int32_t gx, int32_t gy, float z) noexcept -> float {
        const float dx = static_cast<float>(std::abs(gx - goalGx));
        const float dy = static_cast<float>(std::abs(gy - goalGy));
        const float octileXY = (std::max(dx, dy) + (kSqrt2 - 1.0f) * std::min(dx, dy)) * cfg_.cellSizeU;
        const float dz = z - cells_[goalIdx].heightZ;
        return std::sqrt(octileXY * octileXY + dz * dz);
    };

    struct OpenNode {
        float    fScore = 0.0f;
        uint32_t idx    = 0u;
        bool operator>(const OpenNode& o) const noexcept { return fScore > o.fScore; }
    };

    std::priority_queue<OpenNode, std::vector<OpenNode>, std::greater<OpenNode>> open;
    gScore[startIdx] = 0.0f;
    open.push({heuristic(startGx, startGy, cells_[startIdx].heightZ), startIdx});

    bool reachedGoal = false;
    while (!open.empty() && result.expandedNodes < maxExpandedNodes) {
        const OpenNode cur = open.top();
        open.pop();

        if (closed[cur.idx]) {
            continue;
        }
        closed[cur.idx] = 1u;
        ++result.expandedNodes;

        if (cur.idx == goalIdx) {
            reachedGoal = true;
            break;
        }

        const int32_t gx = static_cast<int32_t>(cur.idx % gridW_);
        const int32_t gy = static_cast<int32_t>(cur.idx / gridW_);
        const NavCell& curCell = cells_[cur.idx];
        const uint8_t  mask    = curCell.passableNeighborMask;

        for (int dir = 0; dir < 8; ++dir) {
            if ((mask & (1u << dir)) == 0u) {
                continue;
            }
            const int32_t nx = gx + kDirX[dir];
            const int32_t ny = gy + kDirY[dir];
            const uint32_t nIdx = static_cast<uint32_t>(ny) * gridW_ + static_cast<uint32_t>(nx);
            if (closed[nIdx]) {
                continue;
            }
            const NavCell& nCell = cells_[nIdx];
            const float stepXY = kDirDist[dir] * cfg_.cellSizeU;
            const float stepZ  = nCell.heightZ - curCell.heightZ;
            const float step3D = std::sqrt(stepXY * stepXY + stepZ * stepZ);
            const float edgeCost = step3D * 0.5f * (curCell.traversalCost + nCell.traversalCost);
            const float tentativeG = gScore[cur.idx] + edgeCost;

            if (tentativeG < gScore[nIdx]) {
                gScore[nIdx] = tentativeG;
                parent[nIdx] = cur.idx;
                const float f = tentativeG + heuristic(nx, ny, nCell.heightZ);
                open.push({f, nIdx});
            }
        }
    }

    if (!reachedGoal) {
        const auto t1 = std::chrono::steady_clock::now();
        result.searchTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
        return result;
    }

    // Reconstruct raw cell chain (start -> goal):
    std::vector<uint32_t> rawChain;
    for (uint32_t cur = goalIdx; cur != std::numeric_limits<uint32_t>::max(); cur = parent[cur]) {
        rawChain.push_back(cur);
        if (cur == startIdx) break;
    }
    std::reverse(rawChain.begin(), rawChain.end());
    result.rawGridNodes = static_cast<uint32_t>(rawChain.size());

    // String-pull funnel smoothing (greedily advance to farthest line-of-sight clear node):
    std::vector<uint32_t> smoothed;
    smoothed.reserve(rawChain.size());
    size_t anchor = 0u;
    smoothed.push_back(rawChain[0]);

    while (anchor + 1u < rawChain.size()) {
        const int32_t ax = static_cast<int32_t>(rawChain[anchor] % gridW_);
        const int32_t ay = static_cast<int32_t>(rawChain[anchor] / gridW_);
        size_t farthest = anchor + 1u;
        // Scan backward from end so long straight corridors collapse in O(1) checks!
        for (size_t cand = rawChain.size() - 1u; cand > anchor + 1u; --cand) {
            const int32_t bx = static_cast<int32_t>(rawChain[cand] % gridW_);
            const int32_t by = static_cast<int32_t>(rawChain[cand] / gridW_);
            if (IsSegmentClearOnGrid(ax, ay, bx, by, physicsOpt)) {
                farthest = cand;
                break;
            }
        }
        smoothed.push_back(rawChain[farthest]);
        anchor = farthest;
    }

    result.found = true;
    result.waypoints.reserve(smoothed.size());
    for (size_t i = 0u; i < smoothed.size(); ++i) {
        const uint32_t idx = smoothed[i];
        const int32_t gx = static_cast<int32_t>(idx % gridW_);
        const int32_t gy = static_cast<int32_t>(idx / gridW_);
        NavWaypoint wp{};
        wp.position = GridToWorldCenter(gx, gy);
        wp.tag      = cells_[idx].tag;
        if (i > 0u) {
            result.totalLengthU += Length(wp.position - result.waypoints.back().position);
        }
        result.waypoints.push_back(wp);
    }

    const auto t1 = std::chrono::steady_clock::now();
    result.searchTimeMs = std::chrono::duration<double, std::milli>(t1 - t0).count();
    return result;
}

// ============================================================================
// Tactical Squad & Combat AI Steering
// ============================================================================

std::vector<TacticalSteeringOutput> StepSquadTactics(std::vector<SquadAgent>& agents,
                                                     const ThreatTarget& threat,
                                                     const RawWorldPhysics& physics,
                                                     const RawNavMesh* navMeshOpt,
                                                     float dt) {
    std::vector<TacticalSteeringOutput> outputs(agents.size());
    if (agents.empty()) {
        return outputs;
    }

    const Vec2 threatFwd = NormalizeXY(threat.facing);
    const float threatFacingRad = std::atan2(threatFwd.y, threatFwd.x);

    // Deterministic distinct flank sector angles around threat facing (+-65..115 deg first, then rear):
    constexpr std::array<float, 8> kFlankAnglesDeg = {
        70.0f, -70.0f, 112.0f, -112.0f, 148.0f, -148.0f, 88.0f, -88.0f
    };

    // Assign sequential flanker ranks so MeleeFlankers fan out into distinct angular sectors:
    std::vector<uint32_t> flankRank(agents.size(), 0u);
    uint32_t nextFlankSlot = 0u;
    for (size_t i = 0u; i < agents.size(); ++i) {
        const bool retreating = (agents[i].archetype == TacticalArchetype::RetreatingMorale) ||
                                (agents[i].hpRatio < 0.25f);
        if (!retreating && agents[i].archetype == TacticalArchetype::MeleeFlanker) {
            flankRank[i] = nextFlankSlot++;
        }
    }

    for (size_t i = 0u; i < agents.size(); ++i) {
        const SquadAgent& a = agents[i];
        TacticalSteeringOutput& out = outputs[i];
        out.agentId = a.id;

        const bool lowMorale = (a.archetype == TacticalArchetype::RetreatingMorale) || (a.hpRatio < 0.25f);
        out.effectiveArchetype = lowMorale ? TacticalArchetype::RetreatingMorale : a.archetype;

        const Vec2 toThreatXY{threat.position.x - a.position.x, threat.position.y - a.position.y};
        const float distThreat = Length(toThreatXY);
        const Vec2 dirToThreat = (distThreat > 1.0e-4f) ? (toThreatXY * (1.0f / distThreat)) : Vec2{1.0f, 0.0f};
        const Vec2 rightPerp{-dirToThreat.y, dirToThreat.x};

        out.hasLineOfSight = HasHorizontalLineOfSight(physics, a.position, threat.position, 96.0f);

        Vec2 tacticMoveXY{0.0f, 0.0f};
        float tacticSpeed = a.moveSpeedU;
        out.desiredPosition = a.position;

        if (out.effectiveArchetype == TacticalArchetype::RetreatingMorale) {
            // Disengage directly away from threat; veer around obstacles if escape path is blocked:
            Vec2 away{-dirToThreat.x, -dirToThreat.y};
            const float baseAng = std::atan2(away.y, away.x);
            constexpr float kTryOffsetsDeg[5] = {0.0f, 35.0f, -35.0f, 70.0f, -70.0f};
            Vec2 bestAway = away;
            for (float offDeg : kTryOffsetsDeg) {
                const float ang = baseAng + offDeg * kDegToRad;
                const Vec2 cand{std::cos(ang), std::sin(ang)};
                const Vec3 probeEnd{a.position.x + cand.x * 200.0f, a.position.y + cand.y * 200.0f, a.position.z};
                if (HasHorizontalLineOfSight(physics, a.position, probeEnd, 64.0f)) {
                    bestAway = cand;
                    break;
                }
            }
            tacticMoveXY        = bestAway;
            out.desiredPosition = Vec3{a.position.x + bestAway.x * 600.0f,
                                       a.position.y + bestAway.y * 600.0f,
                                       a.position.z};
            tacticSpeed         = a.moveSpeedU * 1.1f;
        } else if (out.effectiveArchetype == TacticalArchetype::MeleeFlanker) {
            const float angleDeg = kFlankAnglesDeg[flankRank[i] % kFlankAnglesDeg.size()];
            out.assignedFlankAngleDeg = angleDeg;
            const float worldAng = threatFacingRad + angleDeg * kDegToRad;
            const float ringR    = std::max(60.0f, a.flankRingRadiusU);
            Vec3 flankGoal{
                threat.position.x + std::cos(worldAng) * ringR,
                threat.position.y + std::sin(worldAng) * ringR,
                threat.position.z
            };
            float goalZ = flankGoal.z;
            if (physics.SampleTerrainHeight(flankGoal.x, flankGoal.y, &goalZ)) {
                flankGoal.z = goalZ;
            }
            out.desiredPosition = flankGoal;

            Vec3 steerTarget = flankGoal;
            if (navMeshOpt && navMeshOpt->IsBaked() && !HasHorizontalLineOfSight(physics, a.position, flankGoal, 64.0f)) {
                const NavPathResult path = navMeshOpt->FindPath(a.position, flankGoal, &physics, 4096u);
                if (path.found && path.waypoints.size() >= 2u) {
                    steerTarget = path.waypoints[1].position;
                }
            }
            const Vec2 toGoal{steerTarget.x - a.position.x, steerTarget.y - a.position.y};
            const float goalDist = Length(toGoal);
            if (goalDist > 16.0f) {
                tacticMoveXY = toGoal * (1.0f / goalDist);
                tacticSpeed  = a.moveSpeedU * Clamp01(goalDist / 80.0f + 0.35f);
            } else {
                tacticSpeed = 0.0f;
            }
        } else {
            // RangedArcher / Spellcaster: hold [preferredRangeMinU .. preferredRangeMaxU] with clear LOS!
            const float minR = std::max(120.0f, a.preferredRangeMinU);
            const float maxR = std::max(minR + 100.0f, a.preferredRangeMaxU);
            const float midR = 0.5f * (minR + maxR);

            if (!out.hasLineOfSight) {
                out.repositioningForLos = true;
                // Probe lateral strafe offsets to find a clear firing lane around blocking house/rock:
                constexpr float kStrafeDists[6] = {180.0f, -180.0f, 340.0f, -340.0f, 520.0f, -520.0f};
                bool foundLane = false;
                Vec3 lanePos = a.position;
                for (float sd : kStrafeDists) {
                    Vec3 cand{a.position.x + rightPerp.x * sd, a.position.y + rightPerp.y * sd, a.position.z};
                    float cz = cand.z;
                    if (physics.SampleTerrainHeight(cand.x, cand.y, &cz)) cand.z = cz;
                    if (HasHorizontalLineOfSight(physics, a.position, cand, 64.0f) &&
                        HasHorizontalLineOfSight(physics, cand, threat.position, 96.0f)) {
                        lanePos   = cand;
                        foundLane = true;
                        break;
                    }
                }
                if (!foundLane) {
                    const float side = ((a.id & 1u) == 0u) ? 1.0f : -1.0f;
                    lanePos = Vec3{a.position.x + rightPerp.x * (side * 260.0f),
                                   a.position.y + rightPerp.y * (side * 260.0f),
                                   a.position.z};
                }
                out.desiredPosition = lanePos;
                const Vec2 toLane{lanePos.x - a.position.x, lanePos.y - a.position.y};
                const float laneDist = Length(toLane);
                if (laneDist > 8.0f) {
                    tacticMoveXY = toLane * (1.0f / laneDist);
                }
            } else if (distThreat < minR) {
                // Too close: backpedal cleanly into standoff band
                tacticMoveXY        = Vec2{-dirToThreat.x, -dirToThreat.y};
                out.desiredPosition = Vec3{threat.position.x - dirToThreat.x * midR,
                                           threat.position.y - dirToThreat.y * midR,
                                           a.position.z};
                tacticSpeed         = a.moveSpeedU * 0.85f;
            } else if (distThreat > maxR) {
                // Too far: close into standoff band
                tacticMoveXY        = dirToThreat;
                out.desiredPosition = Vec3{threat.position.x - dirToThreat.x * midR,
                                           threat.position.y - dirToThreat.y * midR,
                                           a.position.z};
                tacticSpeed         = a.moveSpeedU * 0.95f;
            } else {
                // Inside ideal firing band with clear LOS: hold position
                tacticSpeed = 0.0f;
            }
        }

        // Local separation steering (prevents squad members from clumping/overlapping):
        Vec2 sepXY{0.0f, 0.0f};
        for (size_t j = 0u; j < agents.size(); ++j) {
            if (i == j) continue;
            const Vec2 diff{a.position.x - agents[j].position.x, a.position.y - agents[j].position.y};
            const float d = Length(diff);
            const float minSep = (a.personalRadiusU + agents[j].personalRadiusU) * 1.45f;
            if (d < minSep) {
                const float strength = (minSep - d) / std::max(1.0f, minSep);
                Vec2 push = (d > 1.0e-4f) ? (diff * (1.0f / d))
                                          : Vec2{std::cos(static_cast<float>(i) * 1.7f),
                                                 std::sin(static_cast<float>(i) * 1.7f)};
                sepXY = sepXY + push * (strength * 1.35f);
            }
        }

        Vec2 combined = tacticMoveXY * (tacticSpeed > 0.0f ? 1.0f : 0.0f) + sepXY;
        const float combLen = Length(combined);
        if (combLen > 1.0e-4f) {
            out.desiredMoveDir = Vec3{combined.x / combLen, combined.y / combLen, 0.0f};
            out.desiredSpeedU  = std::max(tacticSpeed, std::min(a.moveSpeedU, combLen * a.moveSpeedU * 0.6f));
        } else {
            out.desiredMoveDir = {};
            out.desiredSpeedU  = 0.0f;
        }
    }

    // Integrate positions cleanly over dt (with obstacle slide & terrain grounding):
    if (std::isfinite(dt) && dt > 0.0f) {
        const float clampedDt = std::min(dt, 0.25f);
        for (size_t i = 0u; i < agents.size(); ++i) {
            SquadAgent& a = agents[i];
            const TacticalSteeringOutput& out = outputs[i];
            const Vec3 stepVel{out.desiredMoveDir.x * out.desiredSpeedU,
                               out.desiredMoveDir.y * out.desiredSpeedU,
                               0.0f};
            Vec3 nextPos = a.position + stepVel * clampedDt;

            // Prevent walking straight through solid walls during tactical steering:
            const float stepLen = Length(stepVel) * clampedDt;
            if (stepLen > 1.0e-3f) {
                const Vec3 probeOrigin{a.position.x, a.position.y, a.position.z + 56.0f};
                const RayHit wall = physics.RayCast(probeOrigin, out.desiredMoveDir, stepLen + a.personalRadiusU);
                if (wall.hit && !wall.hitTerrain) {
                    const Vec2 wn{wall.normal[0], wall.normal[1]};
                    const float wnLen = Length(wn);
                    if (wnLen > 1.0e-3f) {
                        const Vec2 wnN = wn * (1.0f / wnLen);
                        const float vn = stepVel.x * wnN.x + stepVel.y * wnN.y;
                        if (vn < 0.0f) {
                            const Vec3 slideVel{stepVel.x - wnN.x * vn, stepVel.y - wnN.y * vn, 0.0f};
                            nextPos = a.position + slideVel * clampedDt;
                        }
                    }
                }
            }

            float groundZ = nextPos.z;
            if (physics.SampleTerrainHeight(nextPos.x, nextPos.y, &groundZ)) {
                nextPos.z = groundZ;
            }
            if (IsFiniteVec3(nextPos)) {
                a.velocity = (nextPos - a.position) * (1.0f / clampedDt);
                a.position = nextPos;
            }
            // Face toward threat unless retreating:
            if (out.effectiveArchetype == TacticalArchetype::RetreatingMorale && Length(out.desiredMoveDir) > 0.1f) {
                a.facing = out.desiredMoveDir;
            } else {
                const Vec2 toT = NormalizeXY(threat.position - a.position);
                a.facing = Vec3{toT.x, toT.y, 0.0f};
            }
        }
    }

    return outputs;
}

}  // namespace raw::worldgen
