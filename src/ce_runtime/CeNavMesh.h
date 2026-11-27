#pragma once
// CeNavMesh.h: Clean-Room Creation Engine NAVM Parser & Dynamic Obstacle-Aware 3D Navigation Graph (Zero SKSE).
// Parses real Skyrim SE .esm NAVM (NVNM subrecord) triangle meshes directly without touching Bethesda's runtime
// NAVM records, and adds:
//   - Dynamic runtime obstacle spheres (snowdrifts, fallen logs, actors, hazards)
//   - Snow-depth & water-wading traversal cost modulation
//   - Off-mesh climb / vault / jump links
//   - A* search + 2D/3D Simple Stupid Funnel Algorithm (SSFA) smooth waypoint string-pulling
// Original work, MIT.

#include <cstdint>
#include <string>
#include <vector>

namespace raw::ce {

struct NavVec3 {
    float x = 0.0f, y = 0.0f, z = 0.0f;
};

struct NavTri {
    uint16_t v[3]        = {0, 0, 0};
    int16_t  neighbor[3] = {-1, -1, -1};  // adjacent triangle index per edge (0-1, 1-2, 2-0), -1 = boundary
    uint16_t flags       = 0;             // bit 0..2 edge links, bit 9 water, bit 10 preferred, etc.
};

struct NavOffMeshLink {
    uint32_t fromTri = 0;
    uint32_t toTri   = 0;
    NavVec3  startPos{};
    NavVec3  endPos{};
    float    costMult = 1.35f;   // climb/vault/jump link penalty
    uint8_t  kind     = 0;       // 0=jump, 1=vault, 2=climb
};

struct NavDynamicObstacle {
    uint32_t id = 0;
    NavVec3  center{};
    float    radius = 64.0f;
    float    extraCost = 1000.0f;  // >= 1000 blocks triangle, smaller values softly penalize (e.g. deep snow)
};

struct CeNavMeshData {
    uint32_t formId        = 0;
    uint32_t nvnmVersion   = 0;
    uint32_t worldspaceId  = 0;
    uint32_t cellId        = 0;
    std::vector<NavVec3> vertices;
    std::vector<NavTri>  triangles;
    std::vector<NavOffMeshLink> offMeshLinks;
};

struct NavPathResult {
    bool                 found = false;
    float                totalCost = 0.0f;
    uint32_t             nodesExpanded = 0;
    std::vector<uint32_t> triCorridor;
    std::vector<NavVec3>  waypoints;   // funnel-smoothed world-space steering points
};

// Parses a single decompressed NAVM record payload (subrecords containing NVNM).
bool ParseNavmRecordPayload(uint32_t formId, const uint8_t* payload, size_t payloadSize, CeNavMeshData& out);

// Streams a real .esm/.esp file and extracts up to maxMeshes valid NAVM records directly (clean-room, zero SKSE).
bool ExtractNavMeshesFromPlugin(const std::string& esmPath, uint32_t maxMeshes,
                                std::vector<CeNavMeshData>& outMeshes, std::string* outErr = nullptr);

// Dynamic runtime navigation engine with A*, SSFA funnel string-pulling, obstacle avoidance, and snow cost.
class RawNavGraph {
public:
    void Load(CeNavMeshData mesh);
    const CeNavMeshData& Data() const { return m_mesh; }
    uint32_t TriangleCount() const { return static_cast<uint32_t>(m_mesh.triangles.size()); }

    void ClearDynamicObstacles() { m_obstacles.clear(); }
    void AddDynamicObstacle(const NavDynamicObstacle& obs) { m_obstacles.push_back(obs); }
    void AddOffMeshLink(const NavOffMeshLink& link) { m_mesh.offMeshLinks.push_back(link); }
    void SetGlobalSnowDepth(float snowDepthUnits) { m_snowDepthU = snowDepthUnits; }

    // Finds the nearest navmesh triangle to world position p (returns -1 if empty).
    int32_t FindNearestTriangle(NavVec3 p) const;

    // Computes an obstacle-aware, snow-weighted A* corridor and funnel-smoothed waypoint path.
    NavPathResult FindPath(NavVec3 start, NavVec3 goal) const;

private:
    NavVec3 TriCentroid(uint32_t triIdx) const;
    float   TriExtraCost(uint32_t triIdx) const;
    void    SmoothFunnel(NavVec3 start, NavVec3 goal, const std::vector<uint32_t>& corridor,
                         std::vector<NavVec3>& outWaypoints) const;

    CeNavMeshData                   m_mesh;
    std::vector<NavDynamicObstacle> m_obstacles;
    float                           m_snowDepthU = 0.0f;
};

// ============================================================================
// Wave 84: Hierarchical HPA* Multi-Cell Portal Graph + O(1) Spatial Hash + Dynamic Tile Carving
// ============================================================================
struct HpaPortalEdge {
    uint32_t fromCluster = 0u;
    uint32_t toCluster   = 0u;
    NavVec3  portalPos{};
    float    cost        = 64.0f;
    bool     enabled     = true;
};

struct HpaDynamicCarve {
    uint32_t id      = 0u;
    NavVec3  center{};
    float    radiusM = 4.0f;
};

struct HpaPathResult {
    bool                  found         = false;
    float                 totalCost     = 0.0f;
    uint32_t              clustersVisited = 0u;
    std::vector<uint32_t> clusterPath;
    std::vector<NavVec3>  waypoints;
};

class HpaClusterNavGraph {
public:
    static constexpr float kClusterSizeM = 64.0f;

    // Builds an nx x ny grid of 64 m world clusters with stitched border portals.
    void BuildGrid(uint32_t gridX, uint32_t gridY);

    // Removes/disables a cross-cell portal (used for broken-bridge gameplay or Planted Control 84-B).
    void SetPortalEnabled(uint32_t fromCluster, uint32_t toCluster, bool enabled);

    // Runtime dynamic obstacle carving (falling tree, barricade, brazier, spell wall).
    void ClearCarvedObstacles();
    void CarveDynamicObstacle(const HpaDynamicCarve& obs);

    // O(1) spatial hash cluster lookup.
    [[nodiscard]] int32_t FindClusterAt(float worldXM, float worldYM) const noexcept;

    // Hierarchical multi-cell HPA* path search avoiding dynamically carved blocked clusters/portals.
    [[nodiscard]] HpaPathResult FindMultiCellPath(NavVec3 startM, NavVec3 goalM) const;

    [[nodiscard]] uint32_t ClusterCount() const noexcept { return m_gridX * m_gridY; }

private:
    bool IsPortalBlockedByCarve(const HpaPortalEdge& edge) const noexcept;

    uint32_t                     m_gridX = 0u;
    uint32_t                     m_gridY = 0u;
    std::vector<HpaPortalEdge>   m_portals;
    std::vector<HpaDynamicCarve> m_carves;
};

}  // namespace raw::ce
