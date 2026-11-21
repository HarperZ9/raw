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
    // Distant land is tested hit by hit (RtShadow.hlsl, lodHidden): not opaque, and marked in the ID's top bit.
    x.InstanceID = m.alphaRecord | (d.lodLand ? kLodLandBit : 0u); x.InstanceMask = 0xFF; x.InstanceContributionToHitGroupIndex = 0;
    x.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE |
              (d.lodLand ? D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_NON_OPAQUE :   // its BLAS geometry is opaque
               m.alpha ? D3D12_RAYTRACING_INSTANCE_FLAG_NONE : D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE);
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
// Also retains recently-seen static world instances in world-absolute coordinates when Skyrim's frustum
// culler drops them off-screen so RT shadows, reflections, and GI do not pop when turning the camera.
uint32_t RtScene::PlaceStaged(D3D12_RAYTRACING_INSTANCE_DESC* inst, uint32_t slot, uint32_t& alphaN, uint64_t& sceneTris)
{
    uint32_t n = 0;
    const size_t count = m_stageState.size() < m_draws.size() ? m_stageState.size() : m_draws.size();
    for (size_t di = 0; di < count && n < kMaxInstances; ++di) {
        const uint8_t st = m_stageState[di];
        if (!st) continue;
        const uint32_t mi = m_drawMesh[di];
        if (st == 2 && !Instance(mi, m_draws[di], m_stage[di], m_stageTris[di])) continue;
        const D3D12_RAYTRACING_INSTANCE_DESC& x = m_stage[di];
        std::memcpy(inst + n++, &x, sizeof x);
        alphaN += (x.Flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) == 0;
        sceneTris += m_stageTris[di];
        // Cache static world-space instances (non-LOD-land) so off-screen buildings/trees stay in the TLAS.
        if (!m_draws[di].lodLand && m_persistInst.size() < 24000) {
            const double ax = double(x.Transform[0][3]) + m_camPos[0];
            const double ay = double(x.Transform[1][3]) + m_camPos[1];
            const double az = double(x.Transform[2][3]) + m_camPos[2];
            uint64_t h = uint64_t(mi) * 0x9E3779B97F4A7C15ull;
            h ^= uint64_t(int64_t(ax * 0.25)) * 0xBF58476D1CE4E5B9ull;
            h ^= uint64_t(int64_t(ay * 0.25)) * 0x94D049BB133111EBull;
            h ^= uint64_t(int64_t(az * 0.25)) * 0x2545F4914F6CDD1Dull;
            PersistInst& pi = m_persistInst[h];
            pi.desc = x;
            pi.absT[0] = ax; pi.absT[1] = ay; pi.absT[2] = az;
            pi.tris = m_stageTris[di];
            pi.lastSeen = m_frame;
        }
    }
    // Re-emit recently seen frustum-culled static instances (up to 360 frames TTL) in current camera-relative space.
    for (auto it = m_persistInst.begin(); it != m_persistInst.end(); ) {
        const PersistInst& pi = it->second;
        if (m_frame > pi.lastSeen + 360) {
            it = m_persistInst.erase(it);
            continue;
        }
        if (pi.lastSeen < m_frame && n < kMaxInstances) {
            D3D12_RAYTRACING_INSTANCE_DESC x = pi.desc;
            x.Transform[0][3] = float(pi.absT[0] - m_camPos[0]);
            x.Transform[1][3] = float(pi.absT[1] - m_camPos[1]);
            x.Transform[2][3] = float(pi.absT[2] - m_camPos[2]);
            std::memcpy(inst + n++, &x, sizeof x);
            alphaN += (x.Flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) == 0;
            sceneTris += pi.tris;
        }
        ++it;
    }
    if (m_skinReady && m_tune.skin && n < kMaxInstances) n += m_skin.Place(inst + n, kMaxInstances - n, slot, sceneTris);
    return n;
}

} // namespace raw::bridge
