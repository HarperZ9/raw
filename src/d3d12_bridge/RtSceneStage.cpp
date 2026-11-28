// RtScene's instance staging (RtIncremental): each draw's TLAS instance is written at the end of SetDraws,
// while its mesh is still in the CPU caches from the lookup, and PlaceStaged copies them in draw order into
// the upload heap, finishing the draws whose mesh got its BLAS this frame. The same instances in the same
// order as PlaceInto. Original work, MIT.
#include "RtScene.h"
#include "RtSceneDetail.h"

#include <array>
#include <cstring>
#include <unordered_set>

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
    // Track which mesh indices are currently drawn at multiple/moving positions so player-attached
    // rigid meshes (helmets, swords, shields, torches, quivers) NEVER leave 360-frame phantom DXR shadow ribbons!
    static std::unordered_map<uint32_t, std::array<double, 4>> s_lastMeshPos;   // [ax, ay, az, lastFrame]
    static std::unordered_set<uint32_t> s_dynamicMeshes;
    if (m_frame <= 2) { s_lastMeshPos.clear(); s_dynamicMeshes.clear(); }

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
        // Wave 31 Stable Off-Screen Static TLAS Persistence (fixes exterior shadow popping AND indoor window-light flicker!):
        // 1) Never persist actor/skinned draws or meshes detected moving within 1200u (s_dynamicMeshes -> 0 player shadow trails!).
        // 2) Allow verified stationary static architecture/walls/pillars/trees (stableFrames >= 4) to persist down to 140u
        //    for 600 frames (~4-10 sec) so indoor room walls (<650u) and nearby trees NEVER pop out of the TLAS when turning!
        const float rx = x.Transform[0][3], ry = x.Transform[1][3], rz = x.Transform[2][3];
        const float camDistSq = rx * rx + ry * ry + rz * rz;
        const double ax = double(rx) + m_camPos[0];
        const double ay = double(ry) + m_camPos[1];
        const double az = double(rz) + m_camPos[2];
        if (camDistSq < 1200.0f * 1200.0f) {
            auto prev = s_lastMeshPos.find(mi);
            if (prev != s_lastMeshPos.end() && m_frame > uint64_t(prev->second[3]) && m_frame <= uint64_t(prev->second[3]) + 6) {
                const double dx = ax - prev->second[0], dy = ay - prev->second[1], dz = az - prev->second[2];
                const double d2 = dx * dx + dy * dy + dz * dz;
                if (d2 > 0.25 && d2 < 90000.0) {
                    s_dynamicMeshes.insert(mi);
                }
            }
            s_lastMeshPos[mi] = {ax, ay, az, double(m_frame)};
        }
        const bool isAlphaDraw = (x.Flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) == 0;
        const float minPersistDistSq = isAlphaDraw ? (260.0f * 260.0f) : (140.0f * 140.0f);
        if (!m_draws[di].lodLand && !m_draws[di].actor && !m_draws[di].skinned &&
            camDistSq >= minPersistDistSq && s_dynamicMeshes.find(mi) == s_dynamicMeshes.end() &&
            m_persistInst.size() < 24000) {
            uint64_t h = uint64_t(mi) * 0x9E3779B97F4A7C15ull;
            h ^= uint64_t(int64_t(ax * 0.25)) * 0xBF58476D1CE4E5B9ull;
            h ^= uint64_t(int64_t(ay * 0.25)) * 0x94D049BB133111EBull;
            h ^= uint64_t(int64_t(az * 0.25)) * 0x2545F4914F6CDD1Dull;
            PersistInst& pi = m_persistInst[h];
            if (pi.lastSeen + 1 == m_frame && pi.meshIdx == mi) {
                if (pi.stableFrames < 60000) ++pi.stableFrames;
            } else if (pi.lastSeen != m_frame) {
                pi.stableFrames = 1;
            }
            pi.desc = x;
            pi.absT[0] = ax; pi.absT[1] = ay; pi.absT[2] = az;
            pi.tris = m_stageTris[di];
            pi.meshIdx = mi;
            pi.lastSeen = m_frame;
        }
    }
    // Re-emit verified stationary (stableFrames >= 4) off-screen static instances in camera-relative space:
    for (auto it = m_persistInst.begin(); it != m_persistInst.end(); ) {
        const PersistInst& pi = it->second;
        const bool isAlpha = (pi.desc.Flags & D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE) == 0;
        const uint64_t ttl = isAlpha ? 180 : 600;
        const float relX = float(pi.absT[0] - m_camPos[0]);
        const float relY = float(pi.absT[1] - m_camPos[1]);
        const float relZ = float(pi.absT[2] - m_camPos[2]);
        const float relDistSq = relX * relX + relY * relY + relZ * relZ;
        const float minKeepSq = isAlpha ? (260.0f * 260.0f) : (140.0f * 140.0f);
        const bool isDyn = s_dynamicMeshes.find(pi.meshIdx) != s_dynamicMeshes.end();
        if (isDyn || m_frame > pi.lastSeen + ttl || (pi.lastSeen < m_frame && (relDistSq < minKeepSq || pi.stableFrames < 4))) {
            it = m_persistInst.erase(it);
            continue;
        }
        if (pi.lastSeen < m_frame && pi.stableFrames >= 4 && n < kMaxInstances) {
            D3D12_RAYTRACING_INSTANCE_DESC x = pi.desc;
            x.Transform[0][3] = relX;
            x.Transform[1][3] = relY;
            x.Transform[2][3] = relZ;
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
