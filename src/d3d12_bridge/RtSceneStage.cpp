// RtScene's instance staging (RtIncremental): each draw's TLAS instance is written at the end of SetDraws,
// while its mesh is still in the CPU caches from the lookup, and PlaceStaged copies them in draw order into
// the upload heap, finishing the draws whose mesh got its BLAS this frame. The same instances in the same
// order as PlaceInto. Original work, MIT.
#include "RtScene.h"
#include "RtSceneDetail.h"

#include <cstring>

namespace raw::bridge
{
using namespace rtdetail;

// The instance of a draw whose mesh is mi, into x; false when the mesh has no BLAS or stays out.
bool RtScene::Instance(uint32_t mi, const RtDraw& d, D3D12_RAYTRACING_INSTANCE_DESC& x, uint32_t& tris) const
{
    if (mi == ~0u || mi >= m_meshes.size() || !m_meshes[mi].built) return false;
    const Mesh& m = m_meshes[mi];
    if (m.alpha && !m_tune.alpha) return false;   // RtAlpha=0 live: the alpha-tested meshes leave the scene
    std::memcpy(x.Transform, d.world, sizeof x.Transform);
    x.InstanceID = m.alphaRecord; x.InstanceMask = 0xFF; x.InstanceContributionToHitGroupIndex = 0;
    x.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE |
              (m.alpha ? D3D12_RAYTRACING_INSTANCE_FLAG_NONE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE);
    x.AccelerationStructure = m_blasHeap->GetGPUVirtualAddress() + m.blas;
    tris = m.tri.IndexCount / 3;
    return true;
}

// At the end of SetDraws, while each draw's mesh is still in the CPU caches from the lookup: the instances of
// draws whose mesh already has a BLAS. Those built this frame (state 2) are finished in PlaceStaged.
void RtScene::StageDraws()
{
    const size_t n = m_draws.size();
    m_stage.resize(n);
    m_stageState.assign(n, 0);
    m_stageTris.resize(n);
    for (size_t di = 0; di < n; ++di) {
        const uint32_t mi = m_drawMesh[di];
        if (mi == ~0u || mi >= m_meshes.size() || m_meshes[mi].failed) continue;
        m_stageState[di] = Instance(mi, m_draws[di], m_stage[di], m_stageTris[di]) ? 1 : 2;
    }
}

// The staged instances in draw order, as PlaceInto writes them, each copied whole into the upload heap.
uint32_t RtScene::PlaceStaged(D3D12_RAYTRACING_INSTANCE_DESC* inst, uint32_t slot, uint32_t& alphaN, uint64_t& sceneTris)
{
    uint32_t n = 0;
    const size_t count = m_stageState.size() < m_draws.size() ? m_stageState.size() : m_draws.size();
    for (size_t di = 0; di < count && n < kMaxInstances; ++di) {
        const uint8_t st = m_stageState[di];
        if (!st) continue;
        if (st == 2 && !Instance(m_drawMesh[di], m_draws[di], m_stage[di], m_stageTris[di])) continue;
        const D3D12_RAYTRACING_INSTANCE_DESC& x = m_stage[di];
        std::memcpy(inst + n++, &x, sizeof x);
        alphaN += (x.Flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) == 0;
        sceneTris += m_stageTris[di];
    }
    if (m_skinReady && m_tune.skin && n < kMaxInstances) n += m_skin.Place(inst + n, kMaxInstances - n, slot, sceneTris);
    return n;
}

} // namespace raw::bridge
