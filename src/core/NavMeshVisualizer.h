#pragma once
//=============================================================================
//  NavMeshVisualizer.h — In-world navmesh wireframe overlay
//
//  Reads Skyrim's NavMesh data via CommonLibSSE and submits wireframe
//  triangles to the DebugRenderer for real-time 3D visualization.
//
//  Features:
//    - Walkable surface wireframe with flag-based coloring
//    - Cover edge, door portal, and cross-mesh link visualization
//    - Draw-distance culling from player position
//    - Cell-change caching to avoid redundant navmesh reads
//    - SEH protection around all navmesh data access
//
//  Usage:
//    NavMeshVisualizer::Get().SetEnabled(true);
//    NavMeshVisualizer::Get().Update();  // call once per frame
//
//  Author: Zain Dana Harper
//=============================================================================

#include <cstdint>
#include <vector>

namespace SB
{

class NavMeshVisualizer
{
public:
    static NavMeshVisualizer& Get();

    void SetEnabled(bool enabled);
    bool IsEnabled() const;

    // Call once per frame during DoFrameUpdate
    void Update();

    // Settings
    void SetDrawDistance(float units);     // Default: 4096 (game units from player)
    void SetShowCover(bool show);         // Show cover edges
    void SetShowPortals(bool show);       // Show door portals
    void SetShowEdgeLinks(bool show);     // Show cross-navmesh connections
    void SetShowTriangleFlags(bool show); // Color triangles by flags (vs uniform green)

    // Stats for debug GUI
    uint32_t GetVisibleTriangles() const;
    uint32_t GetVisibleNavMeshes() const;

private:
    NavMeshVisualizer() = default;

    // ---- Internal types (opaque to callers) ---------------------------------

    struct CachedVertex
    {
        float x, y, z;
    };

    struct CachedTriangle
    {
        uint16_t v0, v1, v2;
        uint32_t color;        // ABGR packed
    };

    struct CachedCoverEdge
    {
        uint16_t v0, v1;
    };

    struct CachedPortal
    {
        float p0[3];
        float p1[3];
    };

    struct CachedEdgeLink
    {
        float from[3];   // Midpoint of the linked edge on this mesh
        float to[3];     // Centroid of the target triangle (approximate)
    };

    struct CachedNavMesh
    {
        std::vector<CachedVertex>   vertices;
        std::vector<CachedTriangle> triangles;
        std::vector<CachedCoverEdge> coverEdges;
        std::vector<CachedPortal>   doorPortals;
        std::vector<CachedEdgeLink> edgeLinks;
    };

    // ---- Internal methods ---------------------------------------------------

    void RebuildCache();
    void CacheNavMeshFromCell(void* cell);   // RE::TESObjectCELL*
    void CacheOneNavMesh(void* navmesh);     // RE::NavMesh*
    uint32_t GetTriangleColor(uint16_t triangleFlags) const;
    void SubmitToRenderer();

    // ---- State --------------------------------------------------------------

    bool     m_enabled         = false;
    float    m_drawDistance     = 4096.0f;
    bool     m_showCover       = true;
    bool     m_showPortals     = true;
    bool     m_showEdgeLinks   = true;
    bool     m_showTriFlags    = true;

    // Cache invalidation
    void*    m_lastCell        = nullptr;   // RE::TESObjectCELL* — tracked for change detection
    uint32_t m_lastCellFormID  = 0;

    // Cached geometry
    std::vector<CachedNavMesh> m_cache;

    // Per-frame stats
    uint32_t m_visibleTriangles  = 0;
    uint32_t m_visibleNavMeshes  = 0;

    // Player position snapshot (updated each frame)
    float m_playerPos[3] = {};
};

} // namespace SB
