#pragma once
// RawDungeonGen.h: Procedural 3D Nordic Barrow / Glacial Ice-Cavern / Basalt Dwemer Ruin Dungeon
// Generator (`raw::worldgen`). Pure portable C++23 with zero Windows or D3D12 headers.
// Generates seeded multi-elevation vaulted 3D underground interiors:
//   - 3 Archetypes: `NordicBarrow`, `GlacialIceCavern`, `BasaltDwemerRuin`
//   - 6..14 non-overlapping rectangular & octagonal chambers (`Entrance`, `Hall`, `Crypt`, `Treasury`, `BossChamber`)
//   - Guaranteed Prim MST + cycle-loop corridors (100% BFS graph reachability) with C0-watertight sloped ramps
//   - Watertight floors, boundary walls, 3-segment vaulted arched ceilings, architectural pillars & boss altar
//     packed into DX12 AS+MS 64v/126t GPU meshlets (`ProceduralMesh`) passing `ValidateProceduralMeshStrict`!
//   - Placed `DungeonLightAnchor`s (wall sconces, floor braziers, boss altar crystal) + walkable `RawNavMesh` metadata.
// Original work, MIT.

#include "RawMeshPipeline.h"

#include <cstdint>
#include <string>
#include <vector>

namespace raw::worldgen {

enum class DungeonArchetype : uint8_t {
    NordicBarrow     = 0,
    GlacialIceCavern = 1,
    BasaltDwemerRuin = 2,
    Count            = 3,
};

enum class DungeonRoomRole : uint8_t {
    Entrance    = 0,
    Hall        = 1,
    Crypt       = 2,
    Treasury    = 3,
    BossChamber = 4,
};

enum class DungeonLightKind : uint8_t {
    WallSconce       = 0,
    FloorBrazier     = 1,
    BossAltarCrystal = 2,
};

struct DungeonGenConfig {
    DungeonArchetype archetype      = DungeonArchetype::NordicBarrow;
    uint32_t         seed           = 202656u;
    uint32_t         roomCount      = 9u;     // Clamped safely to [6..14]
    float            tileSizeU      = 128.0f; // Modular kit tile size (128u = ~1.83m)
    float            ceilingHeightU = 384.0f; // Vaulted chamber apex height above floor
    uint32_t         maxMeshletTris = 126u;   // Hardware DX12 AS+MS meshlet cap (<= 126)
};

struct DungeonRoom {
    uint32_t        id         = 0u;
    DungeonRoomRole role       = DungeonRoomRole::Hall;
    bool            octagonal  = false;
    Vec3            boundsMin{};
    Vec3            boundsMax{};
    Vec3            center{};
    float           floorZ     = 0.0f;
    float           ceilingZ   = 384.0f;
};

struct DungeonCorridor {
    uint32_t roomA   = 0u;
    uint32_t roomB   = 0u;
    bool     isLoop  = false; // True for extra cycle-loop shortcuts beyond spanning tree
    float    lengthU = 0.0f;
};

struct DungeonLightAnchor {
    DungeonLightKind kind      = DungeonLightKind::WallSconce;
    float            pos[3]    = {};
    float            color[3]  = {1.0f, 0.65f, 0.30f};
    float            radiusU   = 420.0f;
    float            intensity = 1.0f;
    uint32_t         roomId    = 0u;
};

struct GeneratedDungeon {
    DungeonArchetype                archetype               = DungeonArchetype::NordicBarrow;
    uint32_t                        seed                    = 0u;
    float                           entrancePos[3]          = {};
    float                           bossPos[3]              = {};
    Vec3                            worldBoundsMin{};
    Vec3                            worldBoundsMax{};
    float                           recommendedNavRayStartZ = 200.0f; // Strictly above all floors & below all ceilings
    uint32_t                        bfsReachableRooms       = 0u;
    std::vector<DungeonRoom>        rooms;
    std::vector<DungeonCorridor>    corridors;
    std::vector<DungeonLightAnchor> lights;
    ProceduralMesh                  mesh{};
};

const char* GetDungeonArchetypeName(DungeonArchetype arch);
const char* GetDungeonRoomRoleName(DungeonRoomRole role);

class RawDungeonGen {
public:
    // Deterministically generates a watertight 3D dungeon meshlet asset + graph + light anchors.
    static bool GenerateDungeon(const DungeonGenConfig& cfg, GeneratedDungeon& outDungeon);

    // Runs BFS from room 0 over `dungeon.corridors` and returns count of reachable rooms.
    static uint32_t ComputeBfsReachableRooms(const GeneratedDungeon& dungeon);
};

}  // namespace raw::worldgen
