//=============================================================================
//  NavMeshVisualizer.cpp — In-world navmesh wireframe overlay
//
//  Reads NavMesh geometry from loaded cells via CommonLibSSE, caches the
//  vertex/triangle data, and submits wireframe lines to DebugRenderer each
//  frame.  SEH protection wraps all navmesh pointer access to survive cell
//  transitions where pointers may become stale.
//
//  Author: Zain Dana Harper
//=============================================================================

#include "NavMeshVisualizer.h"
#include "DebugRenderer.h"

#include <RE/Skyrim.h>
#include <SKSE/SKSE.h>

#include <cmath>

namespace SB
{

// ---- Color constants (ABGR byte order — matches DebugRenderer) ----------

static constexpr uint32_t kColorWalkable     = 0xFF00FF00;  // Green
static constexpr uint32_t kColorPreferred    = 0xFF00FFFF;  // Yellow
static constexpr uint32_t kColorWater        = 0xFFFF0000;  // Blue
static constexpr uint32_t kColorDeleted      = 0xFF0000FF;  // Red
static constexpr uint32_t kColorNoLarge      = 0xFF0099FF;  // Orange
static constexpr uint32_t kColorEdgeLink     = 0xFFFFFF00;  // Cyan
static constexpr uint32_t kColorCover        = 0xFFFF00FF;  // Magenta
static constexpr uint32_t kColorDoorPortal   = 0xFFFFFFFF;  // White
static constexpr uint32_t kColorUniform      = 0xFF00FF00;  // Green (when flag coloring is off)

// ---- Triangle flag bits (from BSNavmeshTriangle::TriangleFlag) ----------

static constexpr uint16_t kFlagEdge0Link      = 1 << 0;
static constexpr uint16_t kFlagEdge1Link      = 1 << 1;
static constexpr uint16_t kFlagEdge2Link      = 1 << 2;
static constexpr uint16_t kFlagDeleted        = 1 << 3;
static constexpr uint16_t kFlagNoLarge        = 1 << 4;
static constexpr uint16_t kFlagOverlapping    = 1 << 5;
static constexpr uint16_t kFlagPreferred      = 1 << 6;

// =========================================================================
//  Singleton
// =========================================================================

NavMeshVisualizer& NavMeshVisualizer::Get()
{
    static NavMeshVisualizer instance;
    return instance;
}

// =========================================================================
//  Public API
// =========================================================================

void NavMeshVisualizer::SetEnabled(bool enabled)    { m_enabled = enabled; }
bool NavMeshVisualizer::IsEnabled() const           { return m_enabled; }

void NavMeshVisualizer::SetDrawDistance(float units) { m_drawDistance = units; }
void NavMeshVisualizer::SetShowCover(bool show)      { m_showCover = show; }
void NavMeshVisualizer::SetShowPortals(bool show)    { m_showPortals = show; }
void NavMeshVisualizer::SetShowEdgeLinks(bool show)  { m_showEdgeLinks = show; }
void NavMeshVisualizer::SetShowTriangleFlags(bool show) { m_showTriFlags = show; }

uint32_t NavMeshVisualizer::GetVisibleTriangles() const  { return m_visibleTriangles; }
uint32_t NavMeshVisualizer::GetVisibleNavMeshes() const  { return m_visibleNavMeshes; }

// =========================================================================
//  Per-frame update
// =========================================================================

void NavMeshVisualizer::Update()
{
    if (!m_enabled)
        return;

    static uint32_t s_logCount = 0;
    if (s_logCount++ < 30) {
        SKSE::log::info("NavMeshViz: Update() running, cache={} tris, DebugRenderer init={}",
            [this]() -> uint32_t { uint32_t n=0; for(auto& c:m_cache) n+=c.triangles.size(); return n; }(),
            DebugRenderer::Get().IsInitialized());
    }

    // Snapshot player position and detect cell changes
    __try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto pos = player->GetPosition();
        m_playerPos[0] = pos.x;
        m_playerPos[1] = pos.y;
        m_playerPos[2] = pos.z;

        auto* cell = player->GetParentCell();
        if (!cell) return;

        // Rebuild cache if cell changed
        uint32_t cellFormID = cell->GetFormID();
        if (cell != m_lastCell || cellFormID != m_lastCellFormID) {
            m_lastCell       = cell;
            m_lastCellFormID = cellFormID;
            RebuildCache();
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Player/cell access failed during transition — skip this frame
        return;
    }

    // Submit cached geometry to DebugRenderer (distance-culled)
    SubmitToRenderer();
}

// =========================================================================
//  Cache rebuild — reads all navmeshes from loaded cells
// =========================================================================

void NavMeshVisualizer::RebuildCache()
{
    m_cache.clear();

    __try {
        auto* player = RE::PlayerCharacter::GetSingleton();
        if (!player) return;

        auto* cell = player->GetParentCell();
        if (!cell) return;

        if (cell->IsInteriorCell()) {
            // Interior: single cell
            CacheNavMeshFromCell(cell);
        } else {
            // Exterior: iterate the loaded grid
            auto* tes = RE::TES::GetSingleton();
            if (tes && tes->gridCells) {
                auto* grid = tes->gridCells;
                uint32_t len = grid->length;
                for (uint32_t x = 0; x < len; ++x) {
                    for (uint32_t y = 0; y < len; ++y) {
                        __try {
                            auto* gridCell = grid->GetCell(x, y);
                            if (gridCell) {
                                CacheNavMeshFromCell(gridCell);
                            }
                        }
                        __except (EXCEPTION_EXECUTE_HANDLER) {
                            // Skip invalid grid cell
                        }
                    }
                }
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        SKSE::log::warn("NavMeshVisualizer: SEH during cache rebuild — partial data may be used");
    }
}

// =========================================================================
//  Cache navmeshes from a single cell
// =========================================================================

void NavMeshVisualizer::CacheNavMeshFromCell(void* cellPtr)
{
    auto* cell = static_cast<RE::TESObjectCELL*>(cellPtr);
    if (!cell) return;

    __try {
        auto& rtData = cell->GetRuntimeData();
        auto* navArr = rtData.navMeshes;
        if (!navArr) return;

        auto& meshes = navArr->navMeshes;
        for (uint32_t i = 0; i < meshes.size(); ++i) {
            __try {
                auto& smartPtr = meshes[i];
                if (smartPtr) {
                    CacheOneNavMesh(smartPtr.get());
                }
            }
            __except (EXCEPTION_EXECUTE_HANDLER) {
                // Skip corrupted navmesh entry
            }
        }
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        // Cell runtime data or navmesh array was invalid
    }
}

// =========================================================================
//  Cache a single NavMesh's geometry
// =========================================================================

void NavMeshVisualizer::CacheOneNavMesh(void* navmeshPtr)
{
    // NOTE: No __try/__except in this function — it contains C++ objects with
    // destructors (vectors inside CachedNavMesh).  The caller (CacheNavMeshFromCell)
    // already wraps each call in SEH, so access violations are caught there.
    auto* navmesh = static_cast<RE::NavMesh*>(navmeshPtr);
    if (!navmesh) return;

    CachedNavMesh cached;

    // --- Vertices ---
    auto& verts = navmesh->vertices;
    cached.vertices.resize(verts.size());
    for (uint32_t i = 0; i < verts.size(); ++i) {
        cached.vertices[i].x = verts[i].location.x;
        cached.vertices[i].y = verts[i].location.y;
        cached.vertices[i].z = verts[i].location.z;
    }

    // --- Triangles ---
    auto& tris = navmesh->triangles;
    cached.triangles.reserve(tris.size());
    for (uint32_t i = 0; i < tris.size(); ++i) {
        uint16_t flags = static_cast<uint16_t>(tris[i].triangleFlags.get());

        // Skip deleted triangles
        if (flags & kFlagDeleted)
            continue;

        CachedTriangle ct;
        ct.v0    = tris[i].vertices[0];
        ct.v1    = tris[i].vertices[1];
        ct.v2    = tris[i].vertices[2];
        ct.color = GetTriangleColor(flags);

        // Bounds check vertex indices
        if (ct.v0 < cached.vertices.size() &&
            ct.v1 < cached.vertices.size() &&
            ct.v2 < cached.vertices.size())
        {
            cached.triangles.push_back(ct);
        }
    }

    // --- Cover edges ---
    auto& covers = navmesh->coverArray;
    cached.coverEdges.reserve(covers.size());
    for (uint32_t i = 0; i < covers.size(); ++i) {
        CachedCoverEdge ce;
        ce.v0 = covers[i].vertices[0];
        ce.v1 = covers[i].vertices[1];
        if (ce.v0 < cached.vertices.size() &&
            ce.v1 < cached.vertices.size())
        {
            cached.coverEdges.push_back(ce);
        }
    }

    // --- Door portals ---
    auto& portals = navmesh->doorPortals;
    for (uint32_t i = 0; i < portals.size(); ++i) {
        uint16_t triIdx = portals[i].owningTriangleIndex;
        if (triIdx < tris.size()) {
            auto& tri = tris[triIdx];
            if (tri.vertices[0] < cached.vertices.size() &&
                tri.vertices[1] < cached.vertices.size() &&
                tri.vertices[2] < cached.vertices.size())
            {
                auto& a = cached.vertices[tri.vertices[0]];
                auto& b = cached.vertices[tri.vertices[1]];
                auto& c = cached.vertices[tri.vertices[2]];

                CachedPortal cp;
                float cx = (a.x + b.x + c.x) / 3.0f;
                float cy = (a.y + b.y + c.y) / 3.0f;
                float cz = (a.z + b.z + c.z) / 3.0f;
                cp.p0[0] = cx; cp.p0[1] = cy; cp.p0[2] = cz;
                cp.p1[0] = cx; cp.p1[1] = cy; cp.p1[2] = cz + 50.0f;
                cached.doorPortals.push_back(cp);
            }
        }
    }

    // --- Edge links (cross-navmesh connections) ---
    auto& extraEdges = navmesh->extraEdgeInfo;
    for (uint32_t i = 0; i < extraEdges.size(); ++i) {
        auto& ee = extraEdges[i];
        auto eeType = static_cast<uint32_t>(ee.type.get());
        if (eeType == 0) {  // Portal type
            auto& portal = ee.portal;
            uint16_t triIdx = portal.triangle;
            if (triIdx < tris.size()) {
                auto& tri = tris[triIdx];
                if (tri.vertices[0] < cached.vertices.size() &&
                    tri.vertices[1] < cached.vertices.size() &&
                    tri.vertices[2] < cached.vertices.size())
                {
                    auto& va = cached.vertices[tri.vertices[0]];
                    auto& vb = cached.vertices[tri.vertices[1]];
                    auto& vc = cached.vertices[tri.vertices[2]];

                    CachedEdgeLink el;
                    el.from[0] = (va.x + vb.x + vc.x) / 3.0f;
                    el.from[1] = (va.y + vb.y + vc.y) / 3.0f;
                    el.from[2] = (va.z + vb.z + vc.z) / 3.0f;
                    el.to[0] = el.from[0];
                    el.to[1] = el.from[1];
                    el.to[2] = el.from[2] + 30.0f;
                    cached.edgeLinks.push_back(el);
                }
            }
        }
    }

    if (!cached.triangles.empty()) {
        m_cache.push_back(std::move(cached));
    }
}

// =========================================================================
//  Triangle flag -> color mapping
// =========================================================================

uint32_t NavMeshVisualizer::GetTriangleColor(uint16_t flags) const
{
    if (!m_showTriFlags)
        return kColorUniform;

    // Priority order: deleted > preferred > no-large > default walkable
    // (Deleted triangles are already filtered out during caching, but
    //  we keep the check here for safety.)
    if (flags & kFlagDeleted)
        return kColorDeleted;
    if (flags & kFlagPreferred)
        return kColorPreferred;
    if (flags & kFlagNoLarge)
        return kColorNoLarge;

    return kColorWalkable;
}

// =========================================================================
//  Submit cached geometry to DebugRenderer (with distance culling)
// =========================================================================

void NavMeshVisualizer::SubmitToRenderer()
{
    auto& dr = DebugRenderer::Get();
    if (!dr.IsInitialized() || !dr.IsEnabled()) {
        static uint32_t s_skipLog = 0;
        if (s_skipLog++ < 5)
            SKSE::log::warn("NavMeshViz: SubmitToRenderer skipped — init={} enabled={}",
                dr.IsInitialized(), dr.IsEnabled());
        return;
    }

    float distSq = m_drawDistance * m_drawDistance;
    uint32_t triCount  = 0;
    uint32_t meshCount = 0;

    for (auto& mesh : m_cache) {
        bool meshVisible = false;

        // --- Triangles ---
        for (auto& tri : mesh.triangles) {
            auto& v0 = mesh.vertices[tri.v0];
            auto& v1 = mesh.vertices[tri.v1];
            auto& v2 = mesh.vertices[tri.v2];

            // Centroid for distance culling
            float cx = (v0.x + v1.x + v2.x) / 3.0f;
            float cy = (v0.y + v1.y + v2.y) / 3.0f;
            float cz = (v0.z + v1.z + v2.z) / 3.0f;

            float dx = cx - m_playerPos[0];
            float dy = cy - m_playerPos[1];
            float dz = cz - m_playerPos[2];
            float d2 = dx * dx + dy * dy + dz * dz;

            if (d2 > distSq)
                continue;

            float p0[3] = { v0.x, v0.y, v0.z };
            float p1[3] = { v1.x, v1.y, v1.z };
            float p2[3] = { v2.x, v2.y, v2.z };

            dr.DrawTriangle(p0, p1, p2, tri.color);
            ++triCount;
            meshVisible = true;
        }

        // --- Cover edges ---
        if (m_showCover) {
            for (auto& ce : mesh.coverEdges) {
                auto& v0 = mesh.vertices[ce.v0];
                auto& v1 = mesh.vertices[ce.v1];

                // Midpoint distance check
                float mx = (v0.x + v1.x) * 0.5f;
                float my = (v0.y + v1.y) * 0.5f;
                float mz = (v0.z + v1.z) * 0.5f;

                float dx = mx - m_playerPos[0];
                float dy = my - m_playerPos[1];
                float dz = mz - m_playerPos[2];
                if (dx * dx + dy * dy + dz * dz > distSq)
                    continue;

                // Draw cover edge raised slightly above the navmesh surface
                float p0[3] = { v0.x, v0.y, v0.z + 10.0f };
                float p1[3] = { v1.x, v1.y, v1.z + 10.0f };
                dr.DrawLine(p0, p1, kColorCover);
                meshVisible = true;
            }
        }

        // --- Door portals ---
        if (m_showPortals) {
            for (auto& portal : mesh.doorPortals) {
                float dx = portal.p0[0] - m_playerPos[0];
                float dy = portal.p0[1] - m_playerPos[1];
                float dz = portal.p0[2] - m_playerPos[2];
                if (dx * dx + dy * dy + dz * dz > distSq)
                    continue;

                dr.DrawLine(portal.p0, portal.p1, kColorDoorPortal);
                meshVisible = true;
            }
        }

        // --- Edge links ---
        if (m_showEdgeLinks) {
            for (auto& link : mesh.edgeLinks) {
                float dx = link.from[0] - m_playerPos[0];
                float dy = link.from[1] - m_playerPos[1];
                float dz = link.from[2] - m_playerPos[2];
                if (dx * dx + dy * dy + dz * dz > distSq)
                    continue;

                dr.DrawLine(link.from, link.to, kColorEdgeLink);
                meshVisible = true;
            }
        }

        if (meshVisible)
            ++meshCount;
    }

    m_visibleTriangles = triCount;
    m_visibleNavMeshes = meshCount;
}

} // namespace SB
