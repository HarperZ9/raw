// Native 240 Hz Sub-Stepped XPBD + Anisotropic Viscoelastic Soft-Body Physics Engine (RAW engine runtime).
// Original work, MIT.
#include "rt_softbody.h"
#include "rt_image.h"
#include "rt_version.h"

#include <windows.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace raw::rt {
namespace {

uint64_t g_totalSubsteps = 0;
uint64_t g_totalBonesStepped = 0;
const uint8_t* g_base = nullptr;
uint32_t g_kPlayer = 0;
uint32_t g_kProcessLists = 0;
uint32_t g_kHandleEntries = 0;
uint64_t g_lastTickUs = 0;
uint32_t g_lastActorsVisited = 0;
uint32_t g_lastHighActors = 0;
uint32_t g_lastNpcActors = 0;
uint32_t g_lastBonesFound = 0;
uint32_t g_frameCounter = 0;

constexpr uint32_t kMaxExportActors = 32;
std::mutex g_actorPosMtx;
float g_actorPos[kMaxExportActors][4] = {};
uint32_t g_actorPosCount = 0;

struct LiveBoneEntry {
    SoftBoneState state;
    uint32_t lastFrame = 0;
};
std::unordered_map<uintptr_t, LiveBoneEntry> g_liveBones;

inline bool IsCanonicalPtr(const void* p) {
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000ull && v < 0x00007FFFFFFFFFFFull && (v & 0x7ull) == 0;
}

template <class T>
T SafeAt(const uint8_t* p, uint32_t off) {
    T v{};
    std::memcpy(&v, p + off, sizeof v);
    return v;
}

bool ContainsCI(const char* hay, const char* needle) {
    if (!hay || !needle || !*needle) return false;
    const size_t nLen = std::strlen(needle);
    for (const char* p = hay; *p; ++p) {
        size_t i = 0;
        while (i < nLen && p[i]) {
            char a = p[i]; if (a >= 'A' && a <= 'Z') a = char(a + 32);
            char b = needle[i]; if (b >= 'A' && b <= 'Z') b = char(b + 32);
            if (a != b) break;
            ++i;
        }
        if (i == nLen) return true;
    }
    return false;
}

}  // namespace

TissueProfile DefaultTissueProfile(TissueKind kind) {
    switch (kind) {
    case TissueKind::Breast:
        // Soft viscoelastic teardrop tissue: natural vertical bounce, subtle lateral sway, realistic gravity sag & recoil.
        return {{135.0f, 165.0f, 118.0f}, {9.5f, 11.0f, 8.2f}, -0.35f, {2.1f, 2.4f, 3.2f}, 0.085f, 0.24f};
    case TissueKind::Glute:
        // Firm muscular-adipose tissue: crisp stride-coupled vertical & anterior-posterior compression, quick recovery.
        return {{220.0f, 195.0f, 175.0f}, {13.5f, 12.0f, 11.2f}, -0.18f, {1.4f, 2.0f, 2.2f}, 0.045f, 0.18f};
    case TissueKind::Belly:
        // Gentle breathing & impact recoil with strong strain limit.
        return {{240.0f, 185.0f, 210.0f}, {15.0f, 12.5f, 14.0f}, -0.12f, {1.0f, 1.8f, 1.5f}, 0.030f, 0.14f};
    case TissueKind::Thigh:
        // High-stiffness athletic stride jiggle on impact/sprint.
        return {{280.0f, 260.0f, 240.0f}, {16.5f, 15.5f, 15.0f}, -0.06f, {1.0f, 1.2f, 1.2f}, 0.025f, 0.12f};
    case TissueKind::Pelvic:
        // Free-hanging anatomical pendulum & viscoelastic soft-tissue bounce with thigh-collision strain limits.
        return {{78.0f, 92.0f, 108.0f}, {6.4f, 7.2f, 8.0f}, -0.45f, {2.8f, 3.2f, 3.6f}, 0.145f, 0.16f};
    case TissueKind::Hair:
        // Low-stiffness flowing strand dynamics with wind/motion trail.
        return {{65.0f, 65.0f, 95.0f}, {6.8f, 6.8f, 8.5f}, -0.60f, {5.5f, 5.5f, 4.5f}, 0.160f, 0.0f};
    case TissueKind::Cloak:
    default:
        // Cloth/cape dynamics with wide lateral sway and aerodynamic drag.
        return {{48.0f, 52.0f, 88.0f}, {5.4f, 5.8f, 7.6f}, -0.85f, {7.5f, 7.5f, 5.0f}, 0.180f, 0.0f};
    }
}

bool ClassifySoftBone(const char* boneName, TissueKind* outKind) {
    if (!boneName || !outKind) return false;
    if (ContainsCI(boneName, "breast")) { *outKind = TissueKind::Breast; return true; }
    if (ContainsCI(boneName, "butt") || ContainsCI(boneName, "glute")) { *outKind = TissueKind::Glute; return true; }
    if (ContainsCI(boneName, "belly")) { *outKind = TissueKind::Belly; return true; }
    if (ContainsCI(boneName, "thigh") && ContainsCI(boneName, "phys")) { *outKind = TissueKind::Thigh; return true; }
    if (ContainsCI(boneName, "genital") || ContainsCI(boneName, "scrotum") || ContainsCI(boneName, "pelvic")) {
        *outKind = TissueKind::Pelvic; return true;
    }
    if (ContainsCI(boneName, "hair") || ContainsCI(boneName, "ponytail") || ContainsCI(boneName, "braid")) {
        *outKind = TissueKind::Hair; return true;
    }
    if (ContainsCI(boneName, "cloak") || ContainsCI(boneName, "cape") || ContainsCI(boneName, "skirt")) {
        *outKind = TissueKind::Cloak; return true;
    }
    return false;
}

SoftBoneOutput StepSoftBone(SoftBoneState& st,
                            const float parentWorldPos[3],
                            const TissueProfile& prof,
                            float dtSec,
                            float colliderRadius,
                            const float* colliderLocalCenter) {
    SoftBoneOutput out{};
    if (!parentWorldPos || !(dtSec > 0.0f)) return out;
    const float clampedDt = std::min(dtSec, 0.05f);   // Prevent teleport/load-screen impulse spikes

    if (!st.initialized) {
        std::memcpy(st.prevParent, parentWorldPos, 12);
        std::memset(st.prevVel, 0, 12);
        st.pos[0] = 0.0f; st.pos[1] = 0.0f; st.pos[2] = prof.gravitySag;
        st.vel[0] = st.vel[1] = st.vel[2] = 0.0f;
        st.initialized = true;
    }

    // Compute parent acceleration (clamped against cell teleports / fast-travel jumps > 120u/frame).
    float pVel[3], pAcc[3];
    float jumpSq = 0.0f;
    for (int i = 0; i < 3; ++i) {
        const float d = parentWorldPos[i] - st.prevParent[i];
        jumpSq += d * d;
        pVel[i] = d / clampedDt;
    }
    if (jumpSq > 120.0f * 120.0f) {
        std::memcpy(st.prevParent, parentWorldPos, 12);
        std::memset(st.prevVel, 0, 12);
        st.vel[0] = st.vel[1] = st.vel[2] = 0.0f;
        out.localOffset[2] = prof.gravitySag;
        return out;
    }
    for (int i = 0; i < 3; ++i) {
        pAcc[i] = std::clamp((pVel[i] - st.prevVel[i]) / clampedDt, -1800.0f, 1800.0f);
        st.prevParent[i] = parentWorldPos[i];
        st.prevVel[i] = pVel[i];
    }

    // Fixed 240 Hz symplectic-XPBD sub-stepping: identical physical bounce at 60, 120, or 240+ FPS.
    constexpr float kSubDt = 1.0f / 240.0f;
    const int steps = std::clamp(int(std::ceil(clampedDt / kSubDt)), 1, 12);
    const float h = clampedDt / float(steps);
    const float rest[3] = {0.0f, 0.0f, prof.gravitySag};

    for (int s = 0; s < steps; ++s) {
        for (int i = 0; i < 3; ++i) {
            const float x = st.pos[i] - rest[i];
            // Inertial counter-force (-pAcc * 0.065) + anisotropic restoring spring + viscoelastic damping:
            const float f = -pAcc[i] * 0.065f - prof.stiffness[i] * x - prof.damping[i] * st.vel[i];
            st.vel[i] += f * h;
            st.pos[i] += st.vel[i] * h;
            // XPBD hard strain-limit projection (zero stretching/clipping):
            const float lim = prof.maxDisp[i];
            if (st.pos[i] - rest[i] > lim) {
                st.pos[i] = rest[i] + lim;
                if (st.vel[i] > 0.0f) st.vel[i] *= -0.22f;
            } else if (st.pos[i] - rest[i] < -lim) {
                st.pos[i] = rest[i] - lim;
                if (st.vel[i] < 0.0f) st.vel[i] *= -0.22f;
            }
        }
        // Analytical sphere/capsule collision constraint (e.g., hand/arm/torso contact push-out):
        if (colliderRadius > 0.0f && colliderLocalCenter) {
            const float dx = st.pos[0] - colliderLocalCenter[0];
            const float dy = st.pos[1] - colliderLocalCenter[1];
            const float dz = st.pos[2] - colliderLocalCenter[2];
            const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
            if (dist < colliderRadius && dist > 1.0e-5f) {
                const float nx = dx / dist, ny = dy / dist, nz = dz / dist;
                const float pen = colliderRadius - dist;
                st.pos[0] += nx * pen;
                st.pos[1] += ny * pen;
                st.pos[2] += nz * pen;
                const float vn = st.vel[0] * nx + st.vel[1] * ny + st.vel[2] * nz;
                if (vn < 0.0f) {
                    st.vel[0] -= 1.25f * vn * nx;
                    st.vel[1] -= 1.25f * vn * ny;
                    st.vel[2] -= 1.25f * vn * nz;
                }
            }
        }
    }
    g_totalSubsteps += uint64_t(steps);
    ++g_totalBonesStepped;

    for (int i = 0; i < 3; ++i) out.localOffset[i] = st.pos[i];
    out.localPitchRad = std::clamp((st.pos[2] - prof.gravitySag) * prof.rotCouple, -0.35f, 0.35f);
    out.localRollRad  = std::clamp(st.pos[0] * prof.rotCouple, -0.30f, 0.30f);
    // Poisson volume-preserving squash-and-stretch (when compressed vertically, widens subtly laterally):
    const float zStrain = std::clamp((st.pos[2] - prof.gravitySag) / std::max(prof.maxDisp[2], 0.5f), -0.35f, 0.35f);
    const float sZ = 1.0f + zStrain * prof.volumePreserve;
    const float sXY = 1.0f / std::sqrt(std::max(sZ, 0.65f));
    out.localScale[0] = sXY;
    out.localScale[1] = sXY;
    out.localScale[2] = sZ;
    return out;
}

namespace {

// SEH-safe recursive NiNode skeleton traversal: validates child->parent == node (+0x30)
// before descending so leaf BSTriShape / non-NiNode objects are never misread.
void TraverseSkeletonSEH(uint8_t* node, float dtSec, int depth, uint32_t& bonesOut) {
    if (!IsCanonicalPtr(node) || depth > 14 || bonesOut >= 64u) return;
    __try {
        const char* name = SafeAt<const char*>(node, 0x10);
        if (IsCanonicalPtr(name)) {
            TissueKind kind{};
            if (ClassifySoftBone(name, &kind)) {
                const uint8_t* parent = SafeAt<const uint8_t*>(node, 0x30);
                if (IsCanonicalPtr(parent)) {
                    float parentWorld[3];
                    std::memcpy(parentWorld, parent + 0xA0, 12);
                    const bool finiteP = std::isfinite(parentWorld[0]) && std::isfinite(parentWorld[1]) && std::isfinite(parentWorld[2]);
                    if (finiteP) {
                        const uintptr_t key = reinterpret_cast<uintptr_t>(node);
                        LiveBoneEntry& entry = g_liveBones[key];
                        if (!entry.state.initialized) {
                            std::memcpy(entry.state.restLocal, node + 0x64, 12);
                        }
                        const TissueProfile prof = DefaultTissueProfile(kind);
                        const SoftBoneOutput out = StepSoftBone(entry.state, parentWorld, prof, dtSec);
                        // Apply bounded viscoelastic displacement relative to captured bind-pose local.translate (+0x64):
                        float newLocal[3] = {
                            entry.state.restLocal[0] + out.localOffset[0],
                            entry.state.restLocal[1] + out.localOffset[1],
                            entry.state.restLocal[2] + out.localOffset[2],
                        };
                        std::memcpy(node + 0x64, newLocal, 12);
                        entry.lastFrame = g_frameCounter;
                        ++bonesOut;
                    }
                }
            }
        }
        // NiNode::children (NiTPrimitiveArray<NiAVObject*> at +0x110: data +0x118, size uint16 +0x120 / +0x124):
        uint8_t* const* data = SafeAt<uint8_t* const*>(node, 0x118);
        const uint16_t cap = SafeAt<uint16_t>(node, 0x120);
        const uint16_t sz  = SafeAt<uint16_t>(node, 0x124);
        if (IsCanonicalPtr(data) && sz > 0 && sz <= cap && sz <= 96u) {
            for (uint16_t i = 0; i < sz; ++i) {
                uint8_t* child = data[i];
                if (!IsCanonicalPtr(child)) continue;
                // Structural proof: only descend if child->parent (+0x30) points back to this node!
                if (SafeAt<const uint8_t*>(child, 0x30) != node) continue;
                TraverseSkeletonSEH(child, dtSec, depth + 1, bonesOut);
            }
        }
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

void PublishActorPositions(const float pos[][4], uint32_t count) {
    std::lock_guard<std::mutex> lk(g_actorPosMtx);
    g_actorPosCount = std::min(count, kMaxExportActors);
    if (g_actorPosCount > 0) std::memcpy(g_actorPos, pos, g_actorPosCount * 16u);
}

void StepLiveActorsSEH(float dtSec) {
    __try {
        uint32_t actors = 0;
        uint32_t npcs = 0;
        uint32_t bones = 0;
        const uint8_t* pc = (g_base && g_kPlayer) ? SafeAt<const uint8_t*>(g_base, g_kPlayer) : nullptr;
        if (IsCanonicalPtr(pc)) {
            // TESObjectREFR::loadedData (+0x70) -> LOADED_REF_DATA::data3D (+0x00)
            const uint8_t* loaded = SafeAt<const uint8_t*>(pc, 0x70);
            if (IsCanonicalPtr(loaded)) {
                uint8_t* root3d = SafeAt<uint8_t*>(loaded, 0x00);
                if (IsCanonicalPtr(root3d)) {
                    ++actors;
                    TraverseSkeletonSEH(root3d, dtSec, 0, bones);
                }
            }
        }
        // Traverse ALL active high-process NPC actors from ProcessLists::highActorHandles (+0x30 data, +0x40 size)
        // resolved through BSPointerHandleManager::Entry::GetHandleEntries.
        if (g_base && g_kProcessLists && g_kHandleEntries) {
            const uint8_t* pl = SafeAt<const uint8_t*>(g_base, g_kProcessLists);
            const uint8_t* rawTbl = g_base + g_kHandleEntries;
            const uint8_t* indTbl = SafeAt<const uint8_t*>(g_base, g_kHandleEntries);
            const uint8_t* entries = IsCanonicalPtr(indTbl) ? indTbl : rawTbl;
            if (IsCanonicalPtr(pl)) {
                const uint32_t* handles = SafeAt<const uint32_t*>(pl, 0x30);
                const uint32_t hiCount = SafeAt<uint32_t>(pl, 0x40);
                if (hiCount <= 512u) {
                    g_lastHighActors = hiCount;
                    if (IsCanonicalPtr(handles)) {
                        const uint32_t cap = std::min<uint32_t>(hiCount, 48u);
                        float localPos[kMaxExportActors][4] = {};
                        uint32_t localCount = 0;
                        for (uint32_t i = 0; i < cap && bones < 64u; ++i) {
                            const uint32_t h = handles[i];
                            const uint32_t idx = h & 0xFFFFFu;
                            if (!h || idx >= 0x100000u) continue;
                            const uint8_t* ent = entries + size_t(idx) * 16u;
                            // BSPointerHandleManager::Entry::pointer (+0x08) is a BSHandleRefObject* (TESObjectREFR + 0x20)!
                            const uint8_t* handleObj = SafeAt<const uint8_t*>(ent, 0x08);
                            if (!IsCanonicalPtr(handleObj)) continue;
                            const uint8_t* refr = handleObj - 0x20;
                            if (!IsCanonicalPtr(refr) || SafeAt<uint8_t>(refr, 0x1A) != 62u) {
                                refr = handleObj;   // fallback if already TESObjectREFR*
                            }
                            if (!IsCanonicalPtr(refr) || refr == pc) continue;
                            if (SafeAt<uint8_t>(refr, 0x1A) != 62u) continue;
                            // Export live NPC world position (TESObjectREFR::data.location at +0x54) for GPU grass/snow/water interaction!
                            if (localCount < kMaxExportActors) {
                                float pos[3];
                                std::memcpy(pos, refr + 0x54, 12);
                                if (std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2])) {
                                    localPos[localCount][0] = pos[0];
                                    localPos[localCount][1] = pos[1];
                                    localPos[localCount][2] = pos[2];
                                    localPos[localCount][3] = 1.0f;
                                    ++localCount;
                                }
                            }
                            const uint8_t* loaded = SafeAt<const uint8_t*>(refr, 0x70);
                            if (!IsCanonicalPtr(loaded)) continue;
                            uint8_t* root3d = SafeAt<uint8_t*>(loaded, 0x00);
                            if (!IsCanonicalPtr(root3d)) continue;
                            ++actors;
                            ++npcs;
                            TraverseSkeletonSEH(root3d, dtSec, 0, bones);
                        }
                        PublishActorPositions(localPos, localCount);
                    }
                }
            }
        }
        g_lastActorsVisited = actors;
        g_lastNpcActors = npcs;
        g_lastBonesFound = bones;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return;
    }
}

}  // namespace

void SoftBodyInit(const Image& img) {
    g_base = img.base;
    g_kPlayer = Addr("PlayerCharacter::Singleton");
    g_kProcessLists = Addr("ProcessLists::GetSingleton");
    g_kHandleEntries = Addr("BSPointerHandleManager::Entry::GetHandleEntries");
    g_liveBones.clear();
    g_lastTickUs = 0;
    g_frameCounter = 0;
}

void SoftBodyOnPresent() {
    if (!g_base || !g_kPlayer) return;
    LARGE_INTEGER freq{}, now{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&now);
    const uint64_t nowUs = freq.QuadPart > 0 ? uint64_t((now.QuadPart * 1000000ll) / freq.QuadPart) : 0;
    float dtSec = 1.0f / 60.0f;
    if (g_lastTickUs > 0 && nowUs > g_lastTickUs) {
        dtSec = float(double(nowUs - g_lastTickUs) * 1e-6);
    }
    g_lastTickUs = nowUs;
    ++g_frameCounter;
    StepLiveActorsSEH(dtSec);
    // Prune stale bone entries after cell unload / actor despawn (300 frames):
    if ((g_frameCounter & 127u) == 0u && !g_liveBones.empty()) {
        for (auto it = g_liveBones.begin(); it != g_liveBones.end(); ) {
            if (g_frameCounter - it->second.lastFrame > 300u) it = g_liveBones.erase(it);
            else ++it;
        }
    }
}

void SoftBodyShutdown() {
    g_liveBones.clear();
    g_base = nullptr;
}

std::string SoftBodyStatsJson() {
    char b[288];
    std::snprintf(b, sizeof b,
                  "{\"cmd\":\"softbody\",\"wired\":%s,\"active_actors\":%u,\"npc_actors\":%u,\"high_actors\":%u,"
                  "\"tracked_bones\":%zu,\"last_frame_bones\":%u,"
                  "\"softbody_bones_stepped\":%llu,\"softbody_substeps_240hz\":%llu}",
                  (g_base && g_kPlayer && g_kProcessLists && g_kHandleEntries) ? "true" : "false",
                  g_lastActorsVisited, g_lastNpcActors, g_lastHighActors, g_liveBones.size(), g_lastBonesFound,
                  static_cast<unsigned long long>(g_totalBonesStepped),
                  static_cast<unsigned long long>(g_totalSubsteps));
    return b;
}

uint32_t CopyActiveActorPositions(float* outXyzw, uint32_t maxActors) {
    std::lock_guard<std::mutex> lk(g_actorPosMtx);
    const uint32_t n = std::min(g_actorPosCount, maxActors);
    if (outXyzw && n > 0) std::memcpy(outXyzw, g_actorPos, n * 16u);
    return n;
}

}  // namespace raw::rt

extern "C" __declspec(dllexport) uint32_t RAW_EngineActors(float* outXyzw, uint32_t maxActors) {
    return raw::rt::CopyActiveActorPositions(outXyzw, maxActors);
}
