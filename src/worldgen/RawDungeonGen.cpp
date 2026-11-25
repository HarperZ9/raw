// RawDungeonGen.cpp: Procedural 3D Nordic Barrow / Glacial Ice-Cavern / Basalt Dwemer Ruin Dungeon
// Generator (`raw::worldgen`). Original work, MIT. Zero Windows / D3D12 headers.

#include "RawDungeonGen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <queue>

namespace raw::worldgen {
namespace {

constexpr float kPi = 3.14159265358979323846f;

struct Rng32 {
    uint32_t state = 0x9E3779B9u;
    explicit Rng32(uint32_t seed) : state(seed != 0u ? seed : 0x9E3779B9u) {}
    uint32_t Next() noexcept {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
    int32_t RangeInt(int32_t lo, int32_t hi) noexcept {
        if (hi <= lo) return lo;
        return lo + static_cast<int32_t>(Next() % static_cast<uint32_t>(hi - lo + 1));
    }
    float Next01() noexcept {
        return static_cast<float>(Next() & 0x00FFFFFFu) / 16777216.0f;
    }
};

struct ArchetypePalette {
    TerrainMaterialId primaryMat;
    float             floorSplat0[4];
    float             floorSplat1[4];
    float             wallSplat0[4];
    float             wallSplat1[4];
    float             vaultSplat0[4];
    float             vaultSplat1[4];
    float             sconceRgb[3];
    float             brazierRgb[3];
    float             altarRgb[3];
    float             biomeIdFloat;
};

ArchetypePalette GetPalette(DungeonArchetype arch) {
    switch (arch) {
        case DungeonArchetype::GlacialIceCavern:
            return {
                TerrainMaterialId::GlacialIce,
                {0.10f, 0.45f, 0.45f, 0.00f}, {0.00f, 0.00f, 0.00f, 0.00f}, // Snow + GlacialIce floor
                {0.15f, 0.10f, 0.75f, 0.00f}, {0.00f, 0.00f, 0.00f, 0.00f}, // GlacialIce wall
                {0.05f, 0.15f, 0.70f, 0.00f}, {0.00f, 0.00f, 0.00f, 0.10f}, // Ice + Crystal vault
                {0.45f, 0.80f, 1.00f},
                {1.00f, 0.62f, 0.28f},
                {0.35f, 0.85f, 1.00f},
                0.0f
            };
        case DungeonArchetype::BasaltDwemerRuin:
            return {
                TerrainMaterialId::BasaltColumn,
                {0.25f, 0.00f, 0.00f, 0.75f}, {0.00f, 0.00f, 0.00f, 0.00f}, // Basalt + Rock floor
                {0.15f, 0.00f, 0.00f, 0.85f}, {0.00f, 0.00f, 0.00f, 0.00f}, // Hex basalt walls
                {0.20f, 0.00f, 0.00f, 0.75f}, {0.05f, 0.00f, 0.00f, 0.00f}, // Basalt + faint magma glow trim
                {1.00f, 0.72f, 0.32f},
                {1.00f, 0.50f, 0.15f},
                {0.95f, 0.78f, 0.25f},
                3.0f
            };
        case DungeonArchetype::NordicBarrow:
        default:
            return {
                TerrainMaterialId::RockGranite,
                {0.82f, 0.00f, 0.00f, 0.00f}, {0.00f, 0.18f, 0.00f, 0.00f}, // Granite + subtle moss floor
                {0.90f, 0.00f, 0.00f, 0.00f}, {0.00f, 0.10f, 0.00f, 0.00f}, // Carved Nordic stone walls
                {0.95f, 0.00f, 0.00f, 0.00f}, {0.00f, 0.05f, 0.00f, 0.00f}, // Granite vaulted arch
                {1.00f, 0.64f, 0.26f},
                {1.00f, 0.52f, 0.18f},
                {0.42f, 0.76f, 1.00f},
                2.0f
            };
    }
}

struct RoomRectTile {
    int32_t x0 = 0, y0 = 0;
    int32_t w  = 4, h  = 4;
    bool    octagonal = false;
    float   floorZ    = 0.0f;
    float   ceilH     = 384.0f;
    DungeonRoomRole role = DungeonRoomRole::Hall;

    int32_t CenterX() const noexcept { return x0 + w / 2; }
    int32_t CenterY() const noexcept { return y0 + h / 2; }
};

struct TileInfo {
    bool    occupied  = false;
    bool    isRoom    = false;
    int32_t roomId    = -1;
    float   floorZ    = 0.0f;
    float   ceilH     = 320.0f;
};

struct MeshBuilder {
    std::vector<TerrainVertex> verts;
    std::vector<uint32_t>      indices;

    TerrainVertex MakeVert(Vec3 p, Vec3 n, Vec2 uv,
                           const float s0[4], const float s1[4],
                           float curvature, float wetness, float emissive, float biomeId) const {
        const Vec3 un = Normalize(n);
        TerrainVertex v{};
        v.pos[0]    = p.x; v.pos[1]    = p.y; v.pos[2]    = p.z;
        v.normal[0] = un.x; v.normal[1] = un.y; v.normal[2] = un.z;
        v.uv[0]     = uv.x; v.uv[1]     = uv.y;
        float sum = s0[0] + s0[1] + s0[2] + s0[3] + s1[0] + s1[1] + s1[2] + s1[3];
        const float inv = (sum > 1.0e-5f) ? (1.0f / sum) : 1.0f;
        for (int i = 0; i < 4; ++i) {
            v.splat0[i] = s0[i] * inv;
            v.splat1[i] = s1[i] * inv;
        }
        v.meta[0] = curvature;
        v.meta[1] = wetness;
        v.meta[2] = emissive;
        v.meta[3] = biomeId;
        return v;
    }

    void EmitTri(const TerrainVertex& a, const TerrainVertex& b, const TerrainVertex& c) {
        const Vec3 p0{a.pos[0], a.pos[1], a.pos[2]};
        const Vec3 p1{b.pos[0], b.pos[1], b.pos[2]};
        const Vec3 p2{c.pos[0], c.pos[1], c.pos[2]};
        const Vec3 cr = Cross(p1 - p0, p2 - p0);
        if (Dot(cr, cr) < 1.0e-6f) {
            return; // Guard strictly against degenerate triangles
        }
        const uint32_t base = static_cast<uint32_t>(verts.size());
        verts.push_back(a);
        verts.push_back(b);
        verts.push_back(c);
        indices.push_back(base + 0u);
        indices.push_back(base + 1u);
        indices.push_back(base + 2u);
    }

    void EmitQuad(Vec3 p0, Vec3 p1, Vec3 p2, Vec3 p3,
                  const float s0[4], const float s1[4],
                  float curvature, float wetness, float emissive, float biomeId) {
        Vec3 nA = Cross(p1 - p0, p2 - p0);
        Vec3 nB = Cross(p2 - p0, p3 - p0);
        if (Dot(nA, nA) > 1.0e-6f) {
            const Vec3 un = Normalize(nA);
            EmitTri(MakeVert(p0, un, {0.0f, 0.0f}, s0, s1, curvature, wetness, emissive, biomeId),
                    MakeVert(p1, un, {1.0f, 0.0f}, s0, s1, curvature, wetness, emissive, biomeId),
                    MakeVert(p2, un, {1.0f, 1.0f}, s0, s1, curvature, wetness, emissive, biomeId));
        }
        if (Dot(nB, nB) > 1.0e-6f) {
            const Vec3 un = Normalize(nB);
            EmitTri(MakeVert(p0, un, {0.0f, 0.0f}, s0, s1, curvature, wetness, emissive, biomeId),
                    MakeVert(p2, un, {1.0f, 1.0f}, s0, s1, curvature, wetness, emissive, biomeId),
                    MakeVert(p3, un, {0.0f, 1.0f}, s0, s1, curvature, wetness, emissive, biomeId));
        }
    }

    void EmitOctagonalPillar(Vec3 baseCenter, float radius, float height,
                             const float s0[4], const float s1[4], float biomeId) {
        constexpr int kSides = 8;
        std::array<Vec3, kSides> bot{};
        std::array<Vec3, kSides> top{};
        for (int i = 0; i < kSides; ++i) {
            const float ang = (static_cast<float>(i) / static_cast<float>(kSides)) * 2.0f * kPi;
            const float ox  = std::cos(ang) * radius;
            const float oy  = std::sin(ang) * radius;
            bot[i] = {baseCenter.x + ox, baseCenter.y + oy, baseCenter.z};
            top[i] = {baseCenter.x + ox, baseCenter.y + oy, baseCenter.z + height};
        }
        for (int i = 0; i < kSides; ++i) {
            const int j = (i + 1) % kSides;
            EmitQuad(bot[i], bot[j], top[j], top[i], s0, s1, 0.15f, 0.1f, 0.0f, biomeId);
        }
        // Cap top
        const Vec3 capCenter{baseCenter.x, baseCenter.y, baseCenter.z + height};
        const TerrainVertex vc = MakeVert(capCenter, {0, 0, 1}, {0.5f, 0.5f}, s0, s1, 0.0f, 0.0f, 0.0f, biomeId);
        for (int i = 0; i < kSides; ++i) {
            const int j = (i + 1) % kSides;
            EmitTri(vc,
                    MakeVert(top[i], {0, 0, 1}, {0.0f, 0.0f}, s0, s1, 0.0f, 0.0f, 0.0f, biomeId),
                    MakeVert(top[j], {0, 0, 1}, {1.0f, 0.0f}, s0, s1, 0.0f, 0.0f, 0.0f, biomeId));
        }
    }
};

}  // namespace

const char* GetDungeonArchetypeName(DungeonArchetype arch) {
    switch (arch) {
        case DungeonArchetype::NordicBarrow:     return "NordicBarrow";
        case DungeonArchetype::GlacialIceCavern: return "GlacialIceCavern";
        case DungeonArchetype::BasaltDwemerRuin: return "BasaltDwemerRuin";
        default:                                 return "UnknownArchetype";
    }
}

const char* GetDungeonRoomRoleName(DungeonRoomRole role) {
    switch (role) {
        case DungeonRoomRole::Entrance:    return "Entrance";
        case DungeonRoomRole::Hall:        return "Hall";
        case DungeonRoomRole::Crypt:       return "Crypt";
        case DungeonRoomRole::Treasury:    return "Treasury";
        case DungeonRoomRole::BossChamber: return "BossChamber";
        default:                           return "UnknownRole";
    }
}

uint32_t RawDungeonGen::ComputeBfsReachableRooms(const GeneratedDungeon& dungeon) {
    const size_t n = dungeon.rooms.size();
    if (n == 0u) return 0u;

    std::vector<std::vector<uint32_t>> adj(n);
    for (const DungeonCorridor& c : dungeon.corridors) {
        if (c.roomA < n && c.roomB < n) {
            adj[c.roomA].push_back(c.roomB);
            adj[c.roomB].push_back(c.roomA);
        }
    }

    std::vector<uint8_t> visited(n, 0u);
    std::queue<uint32_t> q;
    visited[0] = 1u;
    q.push(0u);
    uint32_t count = 0u;

    while (!q.empty()) {
        const uint32_t u = q.front();
        q.pop();
        ++count;
        for (uint32_t v : adj[u]) {
            if (!visited[v]) {
                visited[v] = 1u;
                q.push(v);
            }
        }
    }
    return count;
}

bool RawDungeonGen::GenerateDungeon(const DungeonGenConfig& rawCfg, GeneratedDungeon& outDungeon) {
    outDungeon = GeneratedDungeon{};

    DungeonGenConfig cfg = rawCfg;
    cfg.roomCount      = std::clamp(cfg.roomCount, 6u, 14u);
    cfg.tileSizeU      = Clamp(std::isfinite(cfg.tileSizeU) ? cfg.tileSizeU : 128.0f, 64.0f, 256.0f);
    cfg.ceilingHeightU = Clamp(std::isfinite(cfg.ceilingHeightU) ? cfg.ceilingHeightU : 384.0f, 280.0f, 640.0f);
    cfg.maxMeshletTris = std::clamp(cfg.maxMeshletTris, 32u, 126u);

    const uint32_t archMix = static_cast<uint32_t>(cfg.archetype) * 0x9E3779B1u;
    Rng32 rng(cfg.seed ^ archMix);
    const ArchetypePalette pal = GetPalette(cfg.archetype);

    constexpr int32_t kGridW = 44;
    constexpr int32_t kGridH = 44;
    constexpr int32_t kMargin = 3;

    // -------------------------------------------------------------------------
    // 1. Place `roomCount` non-overlapping rectangular & octagonal chambers
    // -------------------------------------------------------------------------
    std::vector<RoomRectTile> rects;
    rects.reserve(cfg.roomCount);

    // Room 0 = Entrance near southwest corner, floorZ = 0:
    {
        RoomRectTile ent{};
        ent.x0        = 4;
        ent.y0        = 4;
        ent.w         = 5;
        ent.h         = 5;
        ent.octagonal = false;
        ent.floorZ    = 0.0f;
        ent.ceilH     = cfg.ceilingHeightU;
        ent.role      = DungeonRoomRole::Entrance;
        rects.push_back(ent);
    }

    auto overlapsAny = [&](int32_t x0, int32_t y0, int32_t w, int32_t h, int32_t pad) {
        for (const RoomRectTile& r : rects) {
            if (x0 - pad < r.x0 + r.w && x0 + w + pad > r.x0 &&
                y0 - pad < r.y0 + r.h && y0 + h + pad > r.y0) {
                return true;
            }
        }
        return false;
    };

    for (uint32_t rIdx = 1u; rIdx < cfg.roomCount; ++rIdx) {
        bool placed = false;
        for (int attempt = 0; attempt < 180; ++attempt) {
            const int32_t w  = rng.RangeInt(4, 7);
            const int32_t h  = rng.RangeInt(4, 7);
            const int32_t x0 = rng.RangeInt(kMargin, kGridW - kMargin - w);
            const int32_t y0 = rng.RangeInt(kMargin, kGridH - kMargin - h);
            if (!overlapsAny(x0, y0, w, h, 2)) {
                RoomRectTile rt{};
                rt.x0        = x0;
                rt.y0        = y0;
                rt.w         = w;
                rt.h         = h;
                rt.octagonal = (w >= 5 && h >= 5 && (rng.Next() % 3u == 0u));
                // Keep chamber floors in [-32u .. +80u] so ramps stay gentle (<= 16 deg) & interior nav ray is clear!
                rt.floorZ    = static_cast<float>(rng.RangeInt(-2, 5)) * 16.0f;
                rt.ceilH     = cfg.ceilingHeightU;
                rects.push_back(rt);
                placed = true;
                break;
            }
        }
        if (!placed) {
            // Deterministic fallback scan with 1-tile spacing so roomCount is guaranteed:
            for (int32_t y0 = kMargin; y0 <= kGridH - kMargin - 4 && !placed; y0 += 2) {
                for (int32_t x0 = kMargin; x0 <= kGridW - kMargin - 4 && !placed; x0 += 2) {
                    if (!overlapsAny(x0, y0, 4, 4, 1)) {
                        RoomRectTile rt{};
                        rt.x0     = x0;
                        rt.y0     = y0;
                        rt.w      = 4;
                        rt.h      = 4;
                        rt.floorZ = static_cast<float>((rIdx % 4u)) * 16.0f;
                        rt.ceilH  = cfg.ceilingHeightU;
                        rects.push_back(rt);
                        placed = true;
                    }
                }
            }
        }
    }

    // Assign roles: farthest room from Entrance (Room 0) becomes BossChamber!
    size_t bossIdx = rects.size() - 1u;
    float  maxDist2 = -1.0f;
    for (size_t i = 1u; i < rects.size(); ++i) {
        const float dx = static_cast<float>(rects[i].CenterX() - rects[0].CenterX());
        const float dy = static_cast<float>(rects[i].CenterY() - rects[0].CenterY());
        const float d2 = dx * dx + dy * dy;
        if (d2 > maxDist2) {
            maxDist2 = d2;
            bossIdx  = i;
        }
    }
    for (size_t i = 1u; i < rects.size(); ++i) {
        if (i == bossIdx) {
            rects[i].role      = DungeonRoomRole::BossChamber;
            rects[i].octagonal = (rects[i].w >= 5 && rects[i].h >= 5);
            rects[i].ceilH     = cfg.ceilingHeightU + 64.0f; // Grand boss vault!
        } else if (i % 3u == 1u) {
            rects[i].role = DungeonRoomRole::Hall;
        } else if (i % 3u == 2u) {
            rects[i].role = DungeonRoomRole::Crypt;
        } else {
            rects[i].role = DungeonRoomRole::Treasury;
        }
    }

    // -------------------------------------------------------------------------
    // 2. Connect all rooms via Prim's Minimum Spanning Tree + 1..2 Cycle Loops
    // -------------------------------------------------------------------------
    const size_t nRooms = rects.size();
    std::vector<uint8_t> inMst(nRooms, 0u);
    inMst[0] = 1u;

    auto roomDistTiles = [&](size_t a, size_t b) -> float {
        const float dx = static_cast<float>(rects[a].CenterX() - rects[b].CenterX());
        const float dy = static_cast<float>(rects[a].CenterY() - rects[b].CenterY());
        return std::sqrt(dx * dx + dy * dy);
    };

    std::vector<DungeonCorridor> corridors;
    std::vector<std::vector<uint8_t>> edgeExists(nRooms, std::vector<uint8_t>(nRooms, 0u));

    for (size_t step = 1u; step < nRooms; ++step) {
        size_t bestU = 0u, bestV = 0u;
        float  bestD = 1.0e20f;
        for (size_t u = 0u; u < nRooms; ++u) {
            if (!inMst[u]) continue;
            for (size_t v = 0u; v < nRooms; ++v) {
                if (inMst[v]) continue;
                const float d = roomDistTiles(u, v);
                if (d < bestD) {
                    bestD = d;
                    bestU = u;
                    bestV = v;
                }
            }
        }
        inMst[bestV] = 1u;
        edgeExists[bestU][bestV] = edgeExists[bestV][bestU] = 1u;
        corridors.push_back({static_cast<uint32_t>(bestU), static_cast<uint32_t>(bestV), false,
                             bestD * cfg.tileSizeU});
    }

    // Add up to 2 short extra cycle-loop corridors for classic Skyrim dungeon loop-backs:
    for (int extra = 0; extra < 2; ++extra) {
        size_t bestU = 0u, bestV = 0u;
        float  bestD = 1.0e20f;
        bool   found = false;
        for (size_t u = 0u; u < nRooms; ++u) {
            for (size_t v = u + 1u; v < nRooms; ++v) {
                if (edgeExists[u][v]) continue;
                const float d = roomDistTiles(u, v);
                if (d < bestD) {
                    bestD = d;
                    bestU = u;
                    bestV = v;
                    found = true;
                }
            }
        }
        if (found) {
            edgeExists[bestU][bestV] = edgeExists[bestV][bestU] = 1u;
            corridors.push_back({static_cast<uint32_t>(bestU), static_cast<uint32_t>(bestV), true,
                                 bestD * cfg.tileSizeU});
        }
    }

    // -------------------------------------------------------------------------
    // 3. Rasterize chambers & 2-tile-wide sloped L-corridors onto tile grid
    // -------------------------------------------------------------------------
    std::vector<TileInfo> grid(static_cast<size_t>(kGridW * kGridH));
    auto tileAt = [&](int32_t x, int32_t y) -> TileInfo& {
        return grid[static_cast<size_t>(y) * kGridW + static_cast<size_t>(x)];
    };

    // Carve chambers first:
    for (size_t i = 0u; i < nRooms; ++i) {
        const RoomRectTile& r = rects[i];
        for (int32_t y = r.y0; y < r.y0 + r.h; ++y) {
            for (int32_t x = r.x0; x < r.x0 + r.w; ++x) {
                if (r.octagonal) {
                    const bool leftEdge   = (x == r.x0);
                    const bool rightEdge  = (x == r.x0 + r.w - 1);
                    const bool bottomEdge = (y == r.y0);
                    const bool topEdge    = (y == r.y0 + r.h - 1);
                    if ((leftEdge || rightEdge) && (bottomEdge || topEdge)) {
                        continue; // Chamfered octagonal room corner!
                    }
                }
                TileInfo& ti = tileAt(x, y);
                ti.occupied = true;
                ti.isRoom   = true;
                ti.roomId   = static_cast<int32_t>(i);
                ti.floorZ   = r.floorZ;
                ti.ceilH    = r.ceilH;
            }
        }
    }

    // Carve 2-tile-wide (256u) smooth-ramp L-corridors (guaranteed clear walkable passage!):
    for (const DungeonCorridor& c : corridors) {
        const RoomRectTile& rA = rects[c.roomA];
        const RoomRectTile& rB = rects[c.roomB];
        const int32_t x0 = rA.CenterX(), y0 = rA.CenterY();
        const int32_t x1 = rB.CenterX(), y1 = rB.CenterY();
        const int32_t totalSteps = std::max(1, std::abs(x1 - x0) + std::abs(y1 - y0));

        auto carveBrush = [&](int32_t cx, int32_t cy, float zVal) {
            for (int32_t oy = 0; oy <= 1; ++oy) {
                for (int32_t ox = 0; ox <= 1; ++ox) {
                    const int32_t tx = std::clamp(cx + ox, 1, kGridW - 2);
                    const int32_t ty = std::clamp(cy + oy, 1, kGridH - 2);
                    TileInfo& ti = tileAt(tx, ty);
                    if (!ti.isRoom) {
                        ti.occupied = true;
                        ti.floorZ   = zVal;
                        ti.ceilH    = std::max(304.0f, cfg.ceilingHeightU - 64.0f);
                    }
                }
            }
        };

        int32_t step = 0;
        const int32_t dx = (x1 >= x0) ? 1 : -1;
        for (int32_t x = x0; x != x1; x += dx, ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(totalSteps);
            carveBrush(x, y0, Lerp(rA.floorZ, rB.floorZ, t));
        }
        const int32_t dy = (y1 >= y0) ? 1 : -1;
        for (int32_t y = y0; y != y1; y += dy, ++step) {
            const float t = static_cast<float>(step) / static_cast<float>(totalSteps);
            carveBrush(x1, y, Lerp(rA.floorZ, rB.floorZ, t));
        }
        carveBrush(x1, y1, rB.floorZ);
    }

    // Compute C0-continuous shared vertex heights at all (kGridW+1) x (kGridH+1) tile corners
    // so room floors and corridor ramps stitch together with ZERO vertical cracks or steps!
    const int32_t kVertW = kGridW + 1;
    const int32_t kVertH = kGridH + 1;
    std::vector<float> cornerFloorZ(static_cast<size_t>(kVertW * kVertH), 0.0f);
    std::vector<float> cornerCeilZ(static_cast<size_t>(kVertW * kVertH), cfg.ceilingHeightU);

    float globalMaxFloorZ = -1.0e20f;
    float globalMinCeilZ  =  1.0e20f;

    for (int32_t vy = 0; vy < kVertH; ++vy) {
        for (int32_t vx = 0; vx < kVertW; ++vx) {
            float sumF = 0.0f, sumC = 0.0f;
            int   cnt  = 0;
            for (int32_t oy = -1; oy <= 0; ++oy) {
                for (int32_t ox = -1; ox <= 0; ++ox) {
                    const int32_t tx = vx + ox;
                    const int32_t ty = vy + oy;
                    if (tx >= 0 && ty >= 0 && tx < kGridW && ty < kGridH) {
                        const TileInfo& ti = tileAt(tx, ty);
                        if (ti.occupied) {
                            sumF += ti.floorZ;
                            sumC += ti.floorZ + ti.ceilH;
                            ++cnt;
                        }
                    }
                }
            }
            if (cnt > 0) {
                const float fz = sumF / static_cast<float>(cnt);
                const float cz = sumC / static_cast<float>(cnt);
                cornerFloorZ[static_cast<size_t>(vy) * kVertW + static_cast<size_t>(vx)] = fz;
                cornerCeilZ[static_cast<size_t>(vy) * kVertW + static_cast<size_t>(vx)]  = cz;
                globalMaxFloorZ = std::max(globalMaxFloorZ, fz);
                globalMinCeilZ  = std::min(globalMinCeilZ, cz);
            }
        }
    }

    auto vFloor = [&](int32_t vx, int32_t vy) -> float {
        return cornerFloorZ[static_cast<size_t>(vy) * kVertW + static_cast<size_t>(vx)];
    };
    auto vCeil = [&](int32_t vx, int32_t vy) -> float {
        return cornerCeilZ[static_cast<size_t>(vy) * kVertW + static_cast<size_t>(vx)];
    };

    // -------------------------------------------------------------------------
    // 4. Emit Watertight 3D Floors, Boundary Walls, Vaulted Arched Ceilings & Pillars
    // -------------------------------------------------------------------------
    MeshBuilder mb;
    const float S = cfg.tileSizeU;

    for (int32_t y = 0; y < kGridH; ++y) {
        for (int32_t x = 0; x < kGridW; ++x) {
            const TileInfo& ti = tileAt(x, y);
            if (!ti.occupied) continue;

            const float x0 = static_cast<float>(x) * S;
            const float x1 = x0 + S;
            const float y0 = static_cast<float>(y) * S;
            const float y1 = y0 + S;

            const Vec3 f00{x0, y0, vFloor(x,     y)};
            const Vec3 f10{x1, y0, vFloor(x + 1, y)};
            const Vec3 f11{x1, y1, vFloor(x + 1, y + 1)};
            const Vec3 f01{x0, y1, vFloor(x,     y + 1)};

            // 4a. Watertight Floor Quad (+Z upward normal):
            mb.EmitQuad(f00, f10, f11, f01, pal.floorSplat0, pal.floorSplat1,
                        0.0f, (cfg.archetype == DungeonArchetype::GlacialIceCavern ? 0.35f : 0.15f),
                        0.0f, pal.biomeIdFloat);

            // 4b. 3-Segment Vaulted Arched Ceiling (inward -Z normal, arching +32u at center crown!):
            const float archRise = ti.isRoom ? 38.0f : 24.0f;
            const Vec3 c00{x0, y0, vCeil(x,     y)};
            const Vec3 c10{x1, y0, vCeil(x + 1, y)};
            const Vec3 c11{x1, y1, vCeil(x + 1, y + 1)};
            const Vec3 c01{x0, y1, vCeil(x,     y + 1)};

            const Vec3 mL{x0 + S * 0.25f, y0, Lerp(c00.z, c10.z, 0.25f) + archRise * 0.75f};
            const Vec3 mR{x0 + S * 0.75f, y0, Lerp(c00.z, c10.z, 0.75f) + archRise * 0.75f};
            const Vec3 mLT{x0 + S * 0.25f, y1, Lerp(c01.z, c11.z, 0.25f) + archRise * 0.75f};
            const Vec3 mRT{x0 + S * 0.75f, y1, Lerp(c01.z, c11.z, 0.75f) + archRise * 0.75f};

            // Winding reversed so ceiling normals point downward (-Z) into the chamber:
            mb.EmitQuad(c00, c01, mLT, mL,  pal.vaultSplat0, pal.vaultSplat1, -0.25f, 0.1f, 0.0f, pal.biomeIdFloat);
            mb.EmitQuad(mL,  mLT, mRT, mR,  pal.vaultSplat0, pal.vaultSplat1, -0.35f, 0.1f, 0.0f, pal.biomeIdFloat);
            mb.EmitQuad(mR,  mRT, c11, c10, pal.vaultSplat0, pal.vaultSplat1, -0.25f, 0.1f, 0.0f, pal.biomeIdFloat);

            // 4c. Boundary Walls ONLY on edges adjacent to unoccupied rock (zero blocked doorways!):
            auto isOcc = [&](int32_t nx, int32_t ny) -> bool {
                return (nx >= 0 && ny >= 0 && nx < kGridW && ny < kGridH) && tileAt(nx, ny).occupied;
            };
            // South wall (-Y boundary, normal +Y):
            if (!isOcc(x, y - 1)) {
                mb.EmitQuad(f00, c00, c10, f10, pal.wallSplat0, pal.wallSplat1, 0.1f, 0.1f, 0.0f, pal.biomeIdFloat);
            }
            // North wall (+Y boundary, normal -Y):
            if (!isOcc(x, y + 1)) {
                mb.EmitQuad(f11, c11, c01, f01, pal.wallSplat0, pal.wallSplat1, 0.1f, 0.1f, 0.0f, pal.biomeIdFloat);
            }
            // West wall (-X boundary, normal +X):
            if (!isOcc(x - 1, y)) {
                mb.EmitQuad(f01, c01, c00, f00, pal.wallSplat0, pal.wallSplat1, 0.1f, 0.1f, 0.0f, pal.biomeIdFloat);
            }
            // East wall (+X boundary, normal -X):
            if (!isOcc(x + 1, y)) {
                mb.EmitQuad(f10, c10, c11, f11, pal.wallSplat0, pal.wallSplat1, 0.1f, 0.1f, 0.0f, pal.biomeIdFloat);
            }
        }
    }

    // -------------------------------------------------------------------------
    // 5. Populate Room Metadata, Architectural Pillars, Boss Altar & Light Anchors
    // -------------------------------------------------------------------------
    outDungeon.archetype = cfg.archetype;
    outDungeon.seed      = cfg.seed;
    outDungeon.corridors = corridors;
    outDungeon.rooms.reserve(nRooms);

    for (size_t i = 0u; i < nRooms; ++i) {
        const RoomRectTile& r = rects[i];
        DungeonRoom dr{};
        dr.id        = static_cast<uint32_t>(i);
        dr.role      = r.role;
        dr.octagonal = r.octagonal;
        dr.floorZ    = r.floorZ;
        dr.ceilingZ  = r.floorZ + r.ceilH;
        dr.boundsMin = {static_cast<float>(r.x0) * S, static_cast<float>(r.y0) * S, r.floorZ};
        dr.boundsMax = {static_cast<float>(r.x0 + r.w) * S, static_cast<float>(r.y0 + r.h) * S, dr.ceilingZ};
        dr.center    = {(static_cast<float>(r.CenterX()) + 0.5f) * S,
                        (static_cast<float>(r.CenterY()) + 0.5f) * S,
                        r.floorZ};
        outDungeon.rooms.push_back(dr);

        // Place architectural corner stone pillars inside large rooms (never blocking central walkways!):
        if (r.w >= 5 && r.h >= 5) {
            const float inset = S * 1.15f;
            const std::array<Vec3, 4> pillarCoords = {{
                {dr.boundsMin.x + inset, dr.boundsMin.y + inset, r.floorZ},
                {dr.boundsMax.x - inset, dr.boundsMin.y + inset, r.floorZ},
                {dr.boundsMin.x + inset, dr.boundsMax.y - inset, r.floorZ},
                {dr.boundsMax.x - inset, dr.boundsMax.y - inset, r.floorZ},
            }};
            for (const Vec3& pc : pillarCoords) {
                mb.EmitOctagonalPillar(pc, 22.0f, r.ceilH, pal.wallSplat0, pal.wallSplat1, pal.biomeIdFloat);
            }
        }

        // Place atmospheric light anchors in every room:
        DungeonLightAnchor la{};
        la.roomId = dr.id;
        if (r.role == DungeonRoomRole::BossChamber) {
            la.kind      = DungeonLightKind::BossAltarCrystal;
            la.pos[0]    = dr.center.x;
            la.pos[1]    = dr.center.y;
            la.pos[2]    = r.floorZ + 120.0f;
            la.color[0]  = pal.altarRgb[0];
            la.color[1]  = pal.altarRgb[1];
            la.color[2]  = pal.altarRgb[2];
            la.radiusU   = 680.0f;
            la.intensity = 1.65f;
        } else if (r.role == DungeonRoomRole::Hall || r.role == DungeonRoomRole::Treasury) {
            la.kind      = DungeonLightKind::FloorBrazier;
            la.pos[0]    = dr.center.x;
            la.pos[1]    = dr.center.y;
            la.pos[2]    = r.floorZ + 72.0f;
            la.color[0]  = pal.brazierRgb[0];
            la.color[1]  = pal.brazierRgb[1];
            la.color[2]  = pal.brazierRgb[2];
            la.radiusU   = 520.0f;
            la.intensity = 1.25f;
        } else {
            la.kind      = DungeonLightKind::WallSconce;
            la.pos[0]    = dr.boundsMin.x + 28.0f;
            la.pos[1]    = dr.center.y;
            la.pos[2]    = r.floorZ + 145.0f;
            la.color[0]  = pal.sconceRgb[0];
            la.color[1]  = pal.sconceRgb[1];
            la.color[2]  = pal.sconceRgb[2];
            la.radiusU   = 420.0f;
            la.intensity = 1.0f;
        }
        outDungeon.lights.push_back(la);
    }

    // Entrance & Boss positions (guaranteed open chamber centers!):
    outDungeon.entrancePos[0] = outDungeon.rooms[0].center.x;
    outDungeon.entrancePos[1] = outDungeon.rooms[0].center.y;
    outDungeon.entrancePos[2] = outDungeon.rooms[0].center.z;

    outDungeon.bossPos[0] = outDungeon.rooms[bossIdx].center.x;
    outDungeon.bossPos[1] = outDungeon.rooms[bossIdx].center.y;
    outDungeon.bossPos[2] = outDungeon.rooms[bossIdx].center.z;

    outDungeon.bfsReachableRooms = ComputeBfsReachableRooms(outDungeon);

    // Recommended NavMesh interior raycast start height: strictly above all floors AND below all ceilings!
    outDungeon.recommendedNavRayStartZ = 0.5f * (globalMaxFloorZ + globalMinCeilZ);

    // -------------------------------------------------------------------------
    // 6. Pack into Hardware-Validated 64v/126t GPU Meshlets (`ProceduralMesh`)
    // -------------------------------------------------------------------------
    ProceduralMesh mesh{};
    mesh.name            = std::string("RawDungeon_") + GetDungeonArchetypeName(cfg.archetype);
    mesh.primaryMaterial = pal.primaryMat;
    mesh.lodLevel        = 0u;

    BuildMeshlets(mb.verts, mb.indices, 64u, cfg.maxMeshletTris,
                  &mesh.vertices, &mesh.indices, &mesh.meshlets);

    Vec3 bmin{ 1.0e20f,  1.0e20f,  1.0e20f};
    Vec3 bmax{-1.0e20f, -1.0e20f, -1.0e20f};
    for (const TerrainVertex& v : mesh.vertices) {
        bmin.x = std::min(bmin.x, v.pos[0]);
        bmin.y = std::min(bmin.y, v.pos[1]);
        bmin.z = std::min(bmin.z, v.pos[2]);
        bmax.x = std::max(bmax.x, v.pos[0]);
        bmax.y = std::max(bmax.y, v.pos[1]);
        bmax.z = std::max(bmax.z, v.pos[2]);
    }
    mesh.aabbMin              = bmin;
    mesh.aabbMax              = bmax;
    outDungeon.worldBoundsMin = bmin;
    outDungeon.worldBoundsMax = bmax;
    outDungeon.mesh           = std::move(mesh);

    return !outDungeon.mesh.meshlets.empty() &&
           outDungeon.bfsReachableRooms == static_cast<uint32_t>(outDungeon.rooms.size());
}

}  // namespace raw::worldgen
