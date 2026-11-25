// Native 240 Hz Sub-Stepped XPBD + Anisotropic Viscoelastic Soft-Body & Live Skeleton Physics Engine.
// Post-Havok / Pre-Skinning Main::Draw (0x6565B0) hook with exact SkyrimSE.exe 1.7.104.0 ABI offsets
// and immediate NiNode::UpdateDownwardPass (0xEE24D0) world-transform propagation into NiSkinInstance.
// Original work, MIT.
#include "rt_softbody.h"
#include "rt_live_config.h"
#include "rt_skeleton_ik.h"

#include <Zydis/Zydis.h>

#ifndef RAW_SIM_PURE_TEST
#include "rt_detour.h"
#include "rt_image.h"
#include "rt_version.h"
#include "rt_weather.h"
#endif

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

namespace raw::rt {
namespace {

using MainDrawFn = void(__fastcall*)(void* mainPtr);
struct NiUpdateData {
    float    time  = 0.0f;
    uint32_t flags = 0u;  // bit 0 = updateControllers (0 = preserve Havok pose), bit 1 = forcePrevWorldCopy (0 = keep motion vectors)
};
using NiNodeUpdateDownwardPassFn = void(__fastcall*)(void* niNode, NiUpdateData* data, uint32_t flags);

std::atomic<bool> g_hookActive{false};
std::atomic<bool> g_inHook{false};
MainDrawFn g_origMainDraw = nullptr;
NiNodeUpdateDownwardPassFn g_fnUpdateDownwardPass = nullptr;

const uint8_t* g_base = nullptr;
uint32_t g_kPlayer = 0;
uint32_t g_kProcessLists = 0;
uint32_t g_kHandleEntries = 0;
uint32_t g_kFrameDeltaTime = 0;
uint32_t g_kMainDraw = 0;
uint32_t g_kUpdateDownwardPass = 0;

uint64_t g_totalHooksRun = 0;
uint64_t g_totalSubsteps = 0;
uint64_t g_totalBonesStepped = 0;
uint64_t g_lastTickUs = 0;
double   g_accumTimeSec = 0.0;
uint32_t g_lastActorsVisited = 0;
uint32_t g_lastHighActors = 0;
uint32_t g_lastNpcActors = 0;
uint32_t g_lastIkBones = 0;
uint32_t g_lastXpbdBones = 0;
uint32_t g_lastUpdatedNodes = 0;
uint32_t g_frameCounter = 0;

// Thread-safe external wind override (set via SkeletonPhysicsSetWind or WeatherGetWind).
std::mutex g_windMtx;
bool  g_hasExternalWind = false;
float g_windVec[3] = {35.0f, 12.0f, 0.0f};
float g_windGust01 = 0.0f;

constexpr uint32_t kMaxExportActors = 32;
std::mutex g_actorPosMtx;
float g_actorPos[kMaxExportActors][4] = {};
uint32_t g_actorPosCount = 0;

struct LiveBoneEntry {
    SoftBoneState state;
    uint32_t lastFrame = 0;
};
struct LiveIkBoneBase {
    float baseLocalT[3]        = {};
    float baseLocalR[9]        = {};
    float lastWrittenLocalT[3] = {};
    float lastWrittenLocalR[9] = {};
    bool  hasLastWritten       = false;
    uint32_t lastFrame         = 0;
};
struct LiveActorIkEntry {
    ActorIkMotionState state;
    uint32_t lastFrame = 0;
};

std::mutex g_stateMtx;
std::unordered_map<uintptr_t, LiveBoneEntry>    g_liveBones;
std::unordered_map<uintptr_t, LiveIkBoneBase>   g_liveIkBoneBases;
std::unordered_map<uintptr_t, LiveActorIkEntry> g_liveActorIk;

inline bool FloatsNearEq(const float* a, const float* b, int n, float eps = 1.0e-3f) noexcept {
    for (int i = 0; i < n; ++i) {
        if (std::fabs(a[i] - b[i]) > eps) return false;
    }
    return true;
}

inline bool IsCanonicalPtr(const void* p) noexcept {
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000ull && v < 0x00007FFFFFFFFFFFull && (v & 0x7ull) == 0;
}

inline bool IsCanonicalStr(const char* p) noexcept {
    const uintptr_t v = reinterpret_cast<uintptr_t>(p);
    return v >= 0x10000ull && v < 0x00007FFFFFFFFFFFull;
}

bool ContainsCI(const char* hay, const char* needle) noexcept {
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

// ---------------------------------------------------------------------------
// Isolated POD-only SEH memory & engine-call helpers (MSVC C2712 clean)
// ---------------------------------------------------------------------------

bool SehReadBytes(const void* src, void* dst, size_t len) noexcept {
    if (!src || !dst || len == 0) return false;
    __try {
        std::memcpy(dst, src, len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SehWriteBytes(void* dst, const void* src, size_t len) noexcept {
    if (!dst || !src || len == 0) return false;
    __try {
        std::memcpy(dst, src, len);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

bool SehReadCString(const char* src, char* outBuf, size_t maxLen) noexcept {
    if (!IsCanonicalStr(src) || !outBuf || maxLen < 2) return false;
    __try {
        size_t i = 0;
        for (; i + 1 < maxLen; ++i) {
            const char ch = src[i];
            outBuf[i] = ch;
            if (ch == '\0') return i > 0;
            if (static_cast<unsigned char>(ch) < 0x20 || static_cast<unsigned char>(ch) > 0x7E) return false;
        }
        outBuf[maxLen - 1] = '\0';
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        outBuf[0] = '\0';
        return false;
    }
}

bool SehCallUpdateDownwardPass(NiNodeUpdateDownwardPassFn fn, void* node, NiUpdateData* data) noexcept {
    if (!fn || !IsCanonicalPtr(node) || !data) return false;
    __try {
        fn(node, data, 0u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Pure-C++ fallback world-transform propagation matching NiAVObject::UpdateWorldData (0xEEBF40):
//   world.rotate    = parent.world.rotate * local.rotate
//   world.translate = parent.world.translate + (parent.world.rotate * local.translate) * parent.world.scale
//   world.scale     = parent.world.scale * local.scale
void PropagateWorldTransformNode(uint8_t* node, int depth, uint32_t& updatedOut) noexcept {
    if (!IsCanonicalPtr(node) || depth > 18 || updatedOut >= 512u) return;

    uint8_t* parent = nullptr;
    float localR[9]{}, localT[3]{}, localS = 1.0f;
    uint32_t flags = 0u;
    if (!SehReadBytes(node + ce17104::kOffNiParent, &parent, 8) ||
        !SehReadBytes(node + ce17104::kOffNiLocalRotate, localR, 36) ||
        !SehReadBytes(node + ce17104::kOffNiLocalTranslate, localT, 12) ||
        !SehReadBytes(node + ce17104::kOffNiLocalScale, &localS, 4) ||
        !SehReadBytes(node + ce17104::kOffNiFlags, &flags, 4)) {
        return;
    }

    float worldR[9]{}, worldT[3]{}, worldS = localS;
    const bool ignoreParent = ((flags >> 9) & 1u) != 0u;
    if (IsCanonicalPtr(parent) && !ignoreParent) {
        float pR[9]{}, pT[3]{}, pS = 1.0f;
        if (SehReadBytes(parent + ce17104::kOffNiWorldRotate, pR, 36) &&
            SehReadBytes(parent + ce17104::kOffNiWorldTranslate, pT, 12) &&
            SehReadBytes(parent + ce17104::kOffNiWorldScale, &pS, 4)) {
            for (int r = 0; r < 3; ++r) {
                for (int c = 0; c < 3; ++c) {
                    worldR[r * 3 + c] =
                        pR[r * 3 + 0] * localR[0 * 3 + c] +
                        pR[r * 3 + 1] * localR[1 * 3 + c] +
                        pR[r * 3 + 2] * localR[2 * 3 + c];
                }
                worldT[r] = pT[r] + (pR[r * 3 + 0] * localT[0] +
                                     pR[r * 3 + 1] * localT[1] +
                                     pR[r * 3 + 2] * localT[2]) * pS;
            }
            worldS = pS * localS;
        } else {
            std::memcpy(worldR, localR, 36);
            std::memcpy(worldT, localT, 12);
        }
    } else {
        std::memcpy(worldR, localR, 36);
        std::memcpy(worldT, localT, 12);
    }

    if (std::isfinite(worldT[0]) && std::isfinite(worldT[1]) && std::isfinite(worldT[2]) && std::isfinite(worldS)) {
        SehWriteBytes(node + ce17104::kOffNiWorldRotate, worldR, 36);
        SehWriteBytes(node + ce17104::kOffNiWorldTranslate, worldT, 12);
        SehWriteBytes(node + ce17104::kOffNiWorldScale, &worldS, 4);
        ++updatedOut;
    }

    // Descend NiNode::children using exact freeSize (+0x122)!
    uint8_t* const* children = nullptr;
    uint16_t cap = 0, freeSize = 0;
    if (!SehReadBytes(node + ce17104::kOffNiNodeChildrenData, &children, 8) ||
        !SehReadBytes(node + ce17104::kOffNiNodeChildrenCap, &cap, 2) ||
        !SehReadBytes(node + ce17104::kOffNiNodeChildrenFreeSize, &freeSize, 2)) {
        return;
    }
    if (!IsCanonicalPtr(children) || freeSize == 0 || freeSize > cap || freeSize > 128u) return;

    for (uint16_t i = 0; i < freeSize; ++i) {
        uint8_t* child = nullptr;
        if (!SehReadBytes(children + i, &child, 8) || !IsCanonicalPtr(child)) continue;
        uint8_t* childParent = nullptr;
        if (!SehReadBytes(child + ce17104::kOffNiParent, &childParent, 8) || childParent != node) continue;
        PropagateWorldTransformNode(child, depth + 1, updatedOut);
    }
}

struct CollectedBone {
    uint8_t*         node     = nullptr;
    uint8_t*         parent   = nullptr;
    HumanoidBoneRole ikRole   = HumanoidBoneRole::None;
    TissueKind       softKind = TissueKind::Cloak;
    bool             isSoft   = false;
    bool             isIk     = false;
};

void CollectSkeletonBones(uint8_t* node,
                          int depth,
                          CollectedBone* outBones,
                          uint32_t& count,
                          uint32_t maxBones) noexcept {
    if (!IsCanonicalPtr(node) || depth > 16 || count >= maxBones) return;

    const char* namePtr = nullptr;
    char nameBuf[96]{};
    if (SehReadBytes(node + ce17104::kOffNiName, &namePtr, 8) &&
        SehReadCString(namePtr, nameBuf, sizeof(nameBuf))) {
        TissueKind softKind{};
        const bool isSoft = ClassifySoftBone(nameBuf, &softKind);
        const HumanoidBoneRole ikRole = ClassifyHumanoidBone(nameBuf);
        const bool isIk = (ikRole != HumanoidBoneRole::None);
        if (isSoft || isIk) {
            uint8_t* parent = nullptr;
            SehReadBytes(node + ce17104::kOffNiParent, &parent, 8);
            CollectedBone& cb = outBones[count++];
            cb.node     = node;
            cb.parent   = parent;
            cb.ikRole   = ikRole;
            cb.softKind = softKind;
            cb.isSoft   = isSoft;
            cb.isIk     = isIk;
        }
    }

    // Complete NiNode::children traversal using verified freeSize (+0x122) — NEVER +0x124 size!
    uint8_t* const* children = nullptr;
    uint16_t cap = 0, freeSize = 0;
    if (!SehReadBytes(node + ce17104::kOffNiNodeChildrenData, &children, 8) ||
        !SehReadBytes(node + ce17104::kOffNiNodeChildrenCap, &cap, 2) ||
        !SehReadBytes(node + ce17104::kOffNiNodeChildrenFreeSize, &freeSize, 2)) {
        return;
    }
    if (!IsCanonicalPtr(children) || freeSize == 0 || freeSize > cap || freeSize > 128u) return;

    for (uint16_t i = 0; i < freeSize && count < maxBones; ++i) {
        uint8_t* child = nullptr;
        if (!SehReadBytes(children + i, &child, 8) || !IsCanonicalPtr(child)) continue;
        uint8_t* childParent = nullptr;
        // Structural proof: child->parent (+0x30) MUST point back to this node!
        if (!SehReadBytes(child + ce17104::kOffNiParent, &childParent, 8) || childParent != node) continue;
        CollectSkeletonBones(child, depth + 1, outBones, count, maxBones);
    }
}

void GetActiveWind(float outVec[3], float& outGust01) {
    {
        std::lock_guard<std::mutex> lk(g_windMtx);
        if (g_hasExternalWind) {
            outVec[0] = g_windVec[0];
            outVec[1] = g_windVec[1];
            outVec[2] = g_windVec[2];
            outGust01 = g_windGust01;
            return;
        }
    }
    float wx = 35.0f, wy = 12.0f, wStorm = 0.0f;
#ifndef RAW_SIM_PURE_TEST
    WeatherGetWind(wx, wy, wStorm);
#endif
    outVec[0] = wx;
    outVec[1] = wy;
    outVec[2] = 0.0f;
    outGust01 = wStorm;
}

void PublishActorPositions(const float pos[][4], uint32_t count) {
    std::lock_guard<std::mutex> lk(g_actorPosMtx);
    g_actorPosCount = std::min(count, kMaxExportActors);
    if (g_actorPosCount > 0) std::memcpy(g_actorPos, pos, g_actorPosCount * 16u);
}

void LogDiagnosticLine(const char* line) {
    if (!line) return;
    OutputDebugStringA(line);
    OutputDebugStringA("\n");
#ifndef RAW_SIM_PURE_TEST
    char exePath[MAX_PATH]{};
    if (GetModuleFileNameA(nullptr, exePath, MAX_PATH) > 0) {
        std::string s(exePath);
        const size_t sl = s.rfind('\\');
        if (sl != std::string::npos) {
            const std::string logPath = s.substr(0, sl + 1) + "raw_engine_out\\engine_rt.log";
            FILE* f = nullptr;
            if (fopen_s(&f, logPath.c_str(), "a") == 0 && f) {
                std::fprintf(f, "%s\n", line);
                std::fclose(f);
            }
        }
    }
#endif
}

// Core per-frame pre-Main::Draw solver: steps PlayerCharacter + ProcessLists high actors.
void StepAllLiveActorsPreDraw() {
    if (!g_base || !g_kPlayer) return;

    // Compute accurate frame dt (prefer Skyrim's live frameDeltaTime global 0x3275608, fallback QPC).
    float dtSec = 1.0f / 60.0f;
    float gameDt = 0.0f;
    if (g_kFrameDeltaTime && SehReadBytes(g_base + g_kFrameDeltaTime, &gameDt, 4) &&
        std::isfinite(gameDt) && gameDt > 1.0e-4f && gameDt <= 0.1f) {
        dtSec = gameDt;
    } else {
        LARGE_INTEGER freq{}, now{};
        QueryPerformanceFrequency(&freq);
        QueryPerformanceCounter(&now);
        const uint64_t nowUs = freq.QuadPart > 0 ? uint64_t((now.QuadPart * 1000000ll) / freq.QuadPart) : 0;
        if (g_lastTickUs > 0 && nowUs > g_lastTickUs) {
            dtSec = std::clamp(float(double(nowUs - g_lastTickUs) * 1e-6), 1.0e-4f, 0.05f);
        }
        g_lastTickUs = nowUs;
    }
    g_accumTimeSec += double(dtSec);
    const float timeSec = static_cast<float>(g_accumTimeSec);
    ++g_frameCounter;
    ++g_totalHooksRun;

    uint32_t actors = 0;
    uint32_t npcs = 0;
    uint32_t totalIk = 0;
    uint32_t totalXpbd = 0;
    uint32_t totalUpdated = 0;

    // 1. PlayerCharacter (3rd-person root + 1st-person root if present)
    const uint8_t* pc = nullptr;
    if (SehReadBytes(g_base + g_kPlayer, &pc, 8) && IsCanonicalPtr(pc)) {
        void* root3d = ResolveRefr3DRoot(pc, false);
        if (IsCanonicalPtr(root3d)) {
            ++actors;
            uint32_t ik = 0, xp = 0, upd = 0;
            StepSkeletonSubtree(root3d, reinterpret_cast<uintptr_t>(pc), dtSec, timeSec, &ik, &xp, &upd, true);
            totalIk += ik;
            totalXpbd += xp;
            totalUpdated += upd;
        }
    }

    // 2. Active high-process NPCs from ProcessLists::highActorHandles (+0x30 data, +0x40 size)
    //    resolved via validated inline BSPointerHandleManager::Entry (0x21A4280).
    if (g_kProcessLists && g_kHandleEntries) {
        const uint8_t* pl = nullptr;
        if (SehReadBytes(g_base + g_kProcessLists, &pl, 8) && IsCanonicalPtr(pl)) {
            const uint32_t* handles = nullptr;
            uint32_t hiCount = 0;
            if (SehReadBytes(pl + ce17104::kOffProcessListsHighData, &handles, 8) &&
                SehReadBytes(pl + ce17104::kOffProcessListsHighSize, &hiCount, 4) &&
                IsCanonicalPtr(handles) && hiCount <= 512u) {
                g_lastHighActors = hiCount;
                const uint8_t* entriesBase = g_base + g_kHandleEntries;
                const uint32_t cap = std::min<uint32_t>(hiCount, 48u);
                float localPos[kMaxExportActors][4] = {};
                uint32_t localCount = 0;

                for (uint32_t i = 0; i < cap; ++i) {
                    uint32_t h = 0;
                    if (!SehReadBytes(handles + i, &h, 4) || h == 0u) continue;
                    const auto* refr = static_cast<const uint8_t*>(ResolveHandleEntryRefr(entriesBase, h));
                    if (!IsCanonicalPtr(refr) || refr == pc) continue;
                    uint8_t formType = 0;
                    if (!SehReadBytes(refr + ce17104::kOffRefrFormType, &formType, 1) || formType != 62u) continue;

                    if (localCount < kMaxExportActors) {
                        float pos[3]{};
                        if (SehReadBytes(refr + ce17104::kOffRefrLocation, pos, 12) &&
                            std::isfinite(pos[0]) && std::isfinite(pos[1]) && std::isfinite(pos[2])) {
                            localPos[localCount][0] = pos[0];
                            localPos[localCount][1] = pos[1];
                            localPos[localCount][2] = pos[2];
                            localPos[localCount][3] = 1.0f;
                            ++localCount;
                        }
                    }

                    void* root3d = ResolveRefr3DRoot(refr, false);
                    if (!IsCanonicalPtr(root3d)) continue;
                    ++actors;
                    ++npcs;
                    if (actors <= 24u) {
                        uint32_t ik = 0, xp = 0, upd = 0;
                        StepSkeletonSubtree(root3d, reinterpret_cast<uintptr_t>(refr), dtSec, timeSec, &ik, &xp, &upd, true);
                        totalIk += ik;
                        totalXpbd += xp;
                        totalUpdated += upd;
                    }
                }
                PublishActorPositions(localPos, localCount);
            }
        }
    }

    g_lastActorsVisited = actors;
    g_lastNpcActors     = npcs;
    g_lastIkBones       = totalIk;
    g_lastXpbdBones     = totalXpbd;
    g_lastUpdatedNodes  = totalUpdated;

    // Periodic pruning of despawned actors/bones (every 256 frames)
    if ((g_frameCounter & 255u) == 0u) {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        for (auto it = g_liveBones.begin(); it != g_liveBones.end(); ) {
            if (g_frameCounter - it->second.lastFrame > 300u) it = g_liveBones.erase(it);
            else ++it;
        }
        for (auto it = g_liveIkBoneBases.begin(); it != g_liveIkBoneBases.end(); ) {
            if (g_frameCounter - it->second.lastFrame > 300u) it = g_liveIkBoneBases.erase(it);
            else ++it;
        }
        for (auto it = g_liveActorIk.begin(); it != g_liveActorIk.end(); ) {
            if (g_frameCounter - it->second.lastFrame > 300u) it = g_liveActorIk.erase(it);
            else ++it;
        }
    }

    // Periodic proof-of-life telemetry in engine_rt.log (frame 1 + every 300 frames)
    if (g_totalHooksRun == 1u || (g_totalHooksRun % 300u) == 0u) {
        char buf[224];
        std::snprintf(buf, sizeof(buf),
                      "[SkeletonPhysics] Main::Draw hook active; actors=%u, ikBones=%u, xpbdChains=%u, updatedNodes=%u",
                      actors, totalIk, totalXpbd, totalUpdated);
        LogDiagnosticLine(buf);
    }
}

void __fastcall HookMainDraw(void* mainPtr) {
    if (g_hookActive.load(std::memory_order_acquire)) {
        bool expected = false;
        if (g_inHook.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
            StepAllLiveActorsPreDraw();
            g_inHook.store(false, std::memory_order_release);
        }
    }
    if (g_origMainDraw) {
        g_origMainDraw(mainPtr);
    }
}

}  // namespace

TissueProfile DefaultTissueProfile(TissueKind kind) {
    switch (kind) {
    case TissueKind::Breast:
        return {{135.0f, 165.0f, 118.0f}, {9.5f, 11.0f, 8.2f}, -0.35f, {2.1f, 2.4f, 3.2f}, 0.085f, 0.24f};
    case TissueKind::Glute:
        return {{220.0f, 195.0f, 175.0f}, {13.5f, 12.0f, 11.2f}, -0.18f, {1.4f, 2.0f, 2.2f}, 0.045f, 0.18f};
    case TissueKind::Belly:
        return {{240.0f, 185.0f, 210.0f}, {15.0f, 12.5f, 14.0f}, -0.12f, {1.0f, 1.8f, 1.5f}, 0.030f, 0.14f};
    case TissueKind::Thigh:
        return {{280.0f, 260.0f, 240.0f}, {16.5f, 15.5f, 15.0f}, -0.06f, {1.0f, 1.2f, 1.2f}, 0.025f, 0.12f};
    case TissueKind::Pelvic:
        return {{78.0f, 92.0f, 108.0f}, {6.4f, 7.2f, 8.0f}, -0.45f, {2.8f, 3.2f, 3.6f}, 0.145f, 0.16f};
    case TissueKind::Hair:
        return {{62.0f, 62.0f, 92.0f}, {6.6f, 6.6f, 8.2f}, -0.60f, {5.5f, 5.5f, 4.5f}, 0.165f, 0.0f};
    case TissueKind::WeaponGear:
        return {{190.0f, 190.0f, 220.0f}, {11.5f, 11.5f, 13.0f}, -0.15f, {2.0f, 2.0f, 1.6f}, 0.075f, 0.0f};
    case TissueKind::Cloak:
    default:
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
    if (ContainsCI(boneName, "hair") || ContainsCI(boneName, "ponytail") || ContainsCI(boneName, "braid") ||
        ContainsCI(boneName, "bangs") || ContainsCI(boneName, "strand") || ContainsCI(boneName, "hairlock") ||
        ContainsCI(boneName, "forelock") || ContainsCI(boneName, "tail") || ContainsCI(boneName, "hdt")) {
        *outKind = TissueKind::Hair; return true;
    }
    if (ContainsCI(boneName, "cloak") || ContainsCI(boneName, "cape") || ContainsCI(boneName, "skirt") ||
        ContainsCI(boneName, "robe") || ContainsCI(boneName, "dress") || ContainsCI(boneName, "sash") ||
        ContainsCI(boneName, "cloth") || ContainsCI(boneName, "tassel") || ContainsCI(boneName, "pouch") ||
        ContainsCI(boneName, "fur") || ContainsCI(boneName, "cowl")) {
        *outKind = TissueKind::Cloak; return true;
    }
    if (ContainsCI(boneName, "quiver") || ContainsCI(boneName, "scabbard") ||
        ContainsCI(boneName, "weaponsword") || ContainsCI(boneName, "weaponaxe") ||
        ContainsCI(boneName, "weaponmace") || ContainsCI(boneName, "weapondagger") ||
        ContainsCI(boneName, "weaponback") || ContainsCI(boneName, "weaponbow") ||
        ContainsCI(boneName, "shieldback")) {
        *outKind = TissueKind::WeaponGear; return true;
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
    if (!std::isfinite(parentWorldPos[0]) || !std::isfinite(parentWorldPos[1]) || !std::isfinite(parentWorldPos[2])) {
        return out;
    }
    const float clampedDt = std::clamp(dtSec, 1.0e-4f, 0.05f);

    if (!st.initialized) {
        std::memcpy(st.prevParent, parentWorldPos, 12);
        std::memset(st.prevVel, 0, 12);
        st.pos[0] = 0.0f; st.pos[1] = 0.0f; st.pos[2] = prof.gravitySag;
        st.vel[0] = st.vel[1] = st.vel[2] = 0.0f;
        st.initialized = true;
    }

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

    constexpr float kSubDt = 1.0f / 240.0f;
    const int steps = std::clamp(int(std::ceil(clampedDt / kSubDt)), 1, 12);
    const float h = clampedDt / float(steps);
    const float rest[3] = {0.0f, 0.0f, prof.gravitySag};

    // Aerodynamic wind & gust forcing for hair & cloaks (gravitySag <= -0.55)
    float windAcc[3] = {};
    if (prof.gravitySag <= -0.55f) {
        float wVec[3] = {};
        float wGust = 0.0f;
        GetActiveWind(wVec, wGust);
        const float wt = float(GetTickCount64() % 3600000ull) * 0.001f;
        const float ph = parentWorldPos[0] * 0.013f + parentWorldPos[1] * 0.011f;
        const float gust = std::clamp(
            1.0f + (0.45f + 0.55f * wGust) * std::sin(wt * 1.95f + ph) + 0.28f * std::cos(wt * 3.70f - ph * 1.3f),
            0.35f, 2.25f);
        const float windScale = GetLiveConfig().xpbdWindScale;
        const float dragScale = ((prof.gravitySag <= -0.80f) ? 0.42f : 0.28f) * windScale;
        windAcc[0] = std::clamp(wVec[0] * dragScale * gust, -220.0f, 220.0f);
        windAcc[1] = std::clamp(wVec[1] * dragScale * gust, -220.0f, 220.0f);
        windAcc[2] = std::clamp(wVec[2] * dragScale + std::sqrt(wVec[0] * wVec[0] + wVec[1] * wVec[1]) * dragScale * 0.20f * std::sin(wt * 2.85f + ph), -54.0f, 54.0f);
    }

    // Distance/tether sphere radius for XPBD radial strain limit
    const float maxTether = std::sqrt(prof.maxDisp[0] * prof.maxDisp[0] +
                                      prof.maxDisp[1] * prof.maxDisp[1] +
                                      prof.maxDisp[2] * prof.maxDisp[2]);

    for (int s = 0; s < steps; ++s) {
        for (int i = 0; i < 3; ++i) {
            const float x = st.pos[i] - rest[i];
            const float f = -pAcc[i] * 0.065f + windAcc[i] - prof.stiffness[i] * x - prof.damping[i] * st.vel[i];
            st.vel[i] += f * h;
            st.pos[i] += st.vel[i] * h;
            // Per-axis XPBD box strain projection
            const float lim = prof.maxDisp[i];
            if (st.pos[i] - rest[i] > lim) {
                st.pos[i] = rest[i] + lim;
                if (st.vel[i] > 0.0f) st.vel[i] *= -0.22f;
            } else if (st.pos[i] - rest[i] < -lim) {
                st.pos[i] = rest[i] - lim;
                if (st.vel[i] < 0.0f) st.vel[i] *= -0.22f;
            }
        }
        // Radial XPBD tether constraint (prevents diagonal stretch exceeding physical bone length)
        const float rx = st.pos[0] - rest[0];
        const float ry = st.pos[1] - rest[1];
        const float rz = st.pos[2] - rest[2];
        const float rLen = std::sqrt(rx * rx + ry * ry + rz * rz);
        if (rLen > maxTether && rLen > 1.0e-5f) {
            const float inv = maxTether / rLen;
            st.pos[0] = rest[0] + rx * inv;
            st.pos[1] = rest[1] + ry * inv;
            st.pos[2] = rest[2] + rz * inv;
        }
        // Analytical capsule/sphere push-out collision constraint (torso/head/shoulders)
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

    // NaN/Inf safety fuse
    for (int i = 0; i < 3; ++i) {
        if (!std::isfinite(st.pos[i]) || !std::isfinite(st.vel[i])) {
            st.pos[i] = rest[i];
            st.vel[i] = 0.0f;
        }
    }

    g_totalSubsteps += uint64_t(steps);
    ++g_totalBonesStepped;

    for (int i = 0; i < 3; ++i) out.localOffset[i] = st.pos[i];
    // Angular cone & twist clamping
    out.localPitchRad = std::clamp((st.pos[2] - prof.gravitySag) * prof.rotCouple, -0.35f, 0.35f);
    out.localRollRad  = std::clamp(st.pos[0] * prof.rotCouple, -0.30f, 0.30f);
    const float zStrain = std::clamp((st.pos[2] - prof.gravitySag) / std::max(prof.maxDisp[2], 0.5f), -0.35f, 0.35f);
    const float sZ = 1.0f + zStrain * prof.volumePreserve;
    const float sXY = 1.0f / std::sqrt(std::max(sZ, 0.65f));
    out.localScale[0] = sXY;
    out.localScale[1] = sXY;
    out.localScale[2] = sZ;
    return out;
}

void* ResolveRefr3DRoot(const void* refr, bool isPlayerFirstPerson) {
    if (!IsCanonicalPtr(refr)) return nullptr;
    const auto* base = static_cast<const uint8_t*>(refr);
    if (isPlayerFirstPerson) {
        void* fp3d = nullptr;
        if (SehReadBytes(base + ce17104::kOffPlayerFirstPerson3D, &fp3d, 8) && IsCanonicalPtr(fp3d)) {
            return fp3d;
        }
    }
    // Verified SkyrimSE 1.7.104 TESObjectREFR::Get3D2 (0x2EBCE0):
    //   mov rax, [rcx + 0x68]  (TESObjectREFR::loadedData — NEVER +0x70 ExtraDataList!)
    //   mov rax, [rax + 0x68]  (LOADED_REF_DATA::data3D — NEVER +0x00!)
    const uint8_t* loadedData = nullptr;
    if (!SehReadBytes(base + ce17104::kOffRefrLoadedData, &loadedData, 8) || !IsCanonicalPtr(loadedData)) {
        return nullptr;
    }
    void* data3d = nullptr;
    if (!SehReadBytes(loadedData + ce17104::kOffLoadedData3D, &data3d, 8) || !IsCanonicalPtr(data3d)) {
        return nullptr;
    }
    return data3d;
}

void* ResolveHandleEntryRefr(const void* handleEntriesBase, uint32_t handle, uint32_t maxEntries) {
    if (!IsCanonicalPtr(handleEntriesBase) || handle == 0u) return nullptr;
    const uint32_t idx = handle & ce17104::kHandleIndexMask;
    const uint32_t age = handle & ce17104::kHandleAgeMask;
    if (idx >= maxEntries) return nullptr;

    // Verified @ 0x1764F0..0x17652E: entry stride 16B, +0x00 bits (bit 26 inUse, bits 20..25 age), +0x08 BSHandleRefObject*
    const uint8_t* entry = static_cast<const uint8_t*>(handleEntriesBase) + static_cast<size_t>(idx) * 16u;
    uint32_t bits = 0u;
    const uint8_t* handleObj = nullptr;
    if (!SehReadBytes(entry, &bits, 4) || !SehReadBytes(entry + 8u, &handleObj, 8)) {
        return nullptr;
    }
    if ((bits & ce17104::kHandleInUseBit) == 0u) return nullptr;
    if ((bits & ce17104::kHandleAgeMask) != age) return nullptr;
    if (!IsCanonicalPtr(handleObj)) return nullptr;

    const uint8_t* refr = handleObj - ce17104::kHandleRefObjOffset;
    if (!IsCanonicalPtr(refr)) return nullptr;
    return const_cast<uint8_t*>(refr);
}

bool VerifySkeletonHookPrologues(const uint8_t* moduleBase, uint32_t sizeOfImage, std::string* outReport) {
    if (!moduleBase || sizeOfImage < ce17104::kRvaNiAvObjUpdateWorldData + 64u) {
        if (outReport) *outReport = "fail: null base or image too small";
        return false;
    }

    auto checkNonRelPrologue = [&](uint32_t rva, size_t minBytes, bool requireNoRel, const char* name) -> bool {
        if (rva + 64u > sizeOfImage) {
            if (outReport) *outReport = std::string("fail: ") + name + " out of image bounds";
            return false;
        }
        uint8_t buf[64]{};
        if (!SehReadBytes(moduleBase + rva, buf, sizeof(buf))) {
            if (outReport) *outReport = std::string("fail: SEH unreadable prologue for ") + name;
            return false;
        }
        ZydisDecoder dec;
        ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64);
        size_t pos = 0;
        while (pos < minBytes) {
            ZydisDecodedInstruction ins;
            ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
            if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, buf + pos, sizeof(buf) - pos, &ins, ops))) {
                if (outReport) *outReport = std::string("fail: Zydis decode error at ") + name;
                return false;
            }
            if (requireNoRel && (ins.attributes & ZYDIS_ATTRIB_IS_RELATIVE)) {
                if (outReport) *outReport = std::string("fail: relative operand inside detour prologue of ") + name;
                return false;
            }
            if (ins.meta.category == ZYDIS_CATEGORY_RET || ins.meta.category == ZYDIS_CATEGORY_INTERRUPT) {
                if (outReport) *outReport = std::string("fail: function too short at ") + name;
                return false;
            }
            pos += ins.length;
        }
        return true;
    };

    const bool okDraw = checkNonRelPrologue(ce17104::kRvaMainDraw, 14u, true, "Main::Draw(0x6565B0)");
    const bool okDown = okDraw && checkNonRelPrologue(ce17104::kRvaNiNodeUpdateDownwardPass, 14u, true, "NiNode::UpdateDownwardPass(0xEE24D0)");
    const bool okWld  = okDown && checkNonRelPrologue(ce17104::kRvaNiAvObjUpdateWorldData, 8u, false, "NiAVObject::UpdateWorldData(0xEEBF40)");
    if (okWld && outReport) {
        *outReport = "ok: Zydis verified Main::Draw(0x6565B0>=14B clean), NiNode::UpdateDownwardPass(0xEE24D0), NiAVObject::UpdateWorldData(0xEEBF40)";
    }
    return okWld;
}

uint32_t StepSkeletonSubtree(void* rootNode,
                             uintptr_t actorKey,
                             float dtSec,
                             float timeSec,
                             uint32_t* outIkBones,
                             uint32_t* outXpbdBones,
                             uint32_t* outUpdatedNodes,
                             bool callEngineDownwardPass) {
    if (outIkBones)      *outIkBones = 0;
    if (outXpbdBones)    *outXpbdBones = 0;
    if (outUpdatedNodes) *outUpdatedNodes = 0;
    if (!GetLiveConfig().skeletonXpbdEnabled) return 0;
    if (!IsCanonicalPtr(rootNode)) return 0;

    auto* root = static_cast<uint8_t*>(rootNode);
    CollectedBone bones[128]{};
    uint32_t boneCount = 0;
    CollectSkeletonBones(root, 0, bones, boneCount, 128u);
    if (boneCount == 0) return 0;

    // Extract root world position & forward direction + foot/pelvis Z for live humanoid IK
    float rootWorldT[3]{}, rootWorldR[9]{};
    bool hasRootWorld = SehReadBytes(root + ce17104::kOffNiWorldTranslate, rootWorldT, 12) &&
                        SehReadBytes(root + ce17104::kOffNiWorldRotate, rootWorldR, 36) &&
                        std::isfinite(rootWorldT[0]) && std::isfinite(rootWorldT[1]) && std::isfinite(rootWorldT[2]);
    if (!hasRootWorld) {
        rootWorldT[0] = rootWorldT[1] = rootWorldT[2] = 0.0f;
        rootWorldR[1] = 0.0f; rootWorldR[4] = 1.0f;
    }
    float rootFwdXY[2] = {rootWorldR[1], rootWorldR[4]};
    if (rootFwdXY[0] * rootFwdXY[0] + rootFwdXY[1] * rootFwdXY[1] < 1.0e-4f) {
        rootFwdXY[0] = 0.0f; rootFwdXY[1] = 1.0f;
    }

    float pelvisWorldZ = rootWorldT[2] + 68.0f;
    float leftFootZ    = rootWorldT[2];
    float rightFootZ   = rootWorldT[2];
    bool hasAnyIkBone  = false;
    for (uint32_t i = 0; i < boneCount; ++i) {
        if (!bones[i].isIk) continue;
        hasAnyIkBone = true;
        float wt[3]{};
        if (!SehReadBytes(bones[i].node + ce17104::kOffNiWorldTranslate, wt, 12) || !std::isfinite(wt[2])) continue;
        if (bones[i].ikRole == HumanoidBoneRole::Pelvis)    pelvisWorldZ = wt[2];
        if (bones[i].ikRole == HumanoidBoneRole::LeftFoot)  leftFootZ = wt[2];
        if (bones[i].ikRole == HumanoidBoneRole::RightFoot) rightFootZ = wt[2];
    }

    uint32_t ikModified = 0;
    uint32_t xpbdModified = 0;

    // 1. Live Humanoid IK Pass (additive onto live Havok local.rotate +0x48 / local.translate +0x6C)
    if (hasAnyIkBone) {
        ActorIkPoseDelta ikDelta{};
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            LiveActorIkEntry& ikEnt = g_liveActorIk[actorKey ? actorKey : reinterpret_cast<uintptr_t>(root)];
            ikDelta = StepActorHumanoidIk(ikEnt.state, rootWorldT, rootFwdXY,
                                          leftFootZ, rightFootZ, pelvisWorldZ,
                                          0.0f, dtSec, timeSec);
            ikEnt.lastFrame = g_frameCounter;
        }
        if (ikDelta.valid) {
            for (uint32_t i = 0; i < boneCount; ++i) {
                if (!bones[i].isIk) continue;
                uint8_t* bn = bones[i].node;
                float curR[9]{}, curT[3]{};
                if (!SehReadBytes(bn + ce17104::kOffNiLocalRotate, curR, 36) ||
                    !SehReadBytes(bn + ce17104::kOffNiLocalTranslate, curT, 12)) {
                    continue;
                }
                // Drift-free Havok overwrite detection: if Havok updated this bone this frame, use live Havok pose as base;
                // if frozen/un-animated (e.g. tfc pause or idle bone), reuse captured base so deltas NEVER accumulate!
                float locR[9]{}, locT[3]{};
                {
                    std::lock_guard<std::mutex> lk(g_stateMtx);
                    LiveIkBoneBase& kb = g_liveIkBoneBases[reinterpret_cast<uintptr_t>(bn)];
                    const bool unchangedT = kb.hasLastWritten && FloatsNearEq(curT, kb.lastWrittenLocalT, 3);
                    const bool unchangedR = kb.hasLastWritten && FloatsNearEq(curR, kb.lastWrittenLocalR, 9);
                    if (!unchangedT) std::memcpy(kb.baseLocalT, curT, 12);
                    if (!unchangedR) std::memcpy(kb.baseLocalR, curR, 36);
                    std::memcpy(locT, kb.baseLocalT, 12);
                    std::memcpy(locR, kb.baseLocalR, 36);
                    kb.lastFrame = g_frameCounter;
                }
                bool touched = false;
                switch (bones[i].ikRole) {
                case HumanoidBoneRole::Pelvis:
                    if (std::fabs(ikDelta.pelvisDeltaZ) > 1.0e-4f) {
                        locT[2] += ikDelta.pelvisDeltaZ;
                        touched = true;
                    }
                    break;
                case HumanoidBoneRole::Spine0:
                    ComposeAdditiveLocalRotation(locR, ikDelta.spinePitchRad[0], ikDelta.spineBankRollRad * 0.40f, ikDelta.spineYawRad[0]);
                    touched = true;
                    break;
                case HumanoidBoneRole::Spine1:
                    ComposeAdditiveLocalRotation(locR, ikDelta.spinePitchRad[1], ikDelta.spineBankRollRad * 0.35f, ikDelta.spineYawRad[1]);
                    touched = true;
                    break;
                case HumanoidBoneRole::Spine2:
                    ComposeAdditiveLocalRotation(locR, ikDelta.spinePitchRad[2] + ikDelta.chestBreathLiftZ * 0.045f,
                                                 ikDelta.spineBankRollRad * 0.25f, ikDelta.spineYawRad[2]);
                    locT[1] += ikDelta.chestBreathLiftZ * 0.35f;
                    touched = true;
                    break;
                case HumanoidBoneRole::Neck:
                    ComposeAdditiveLocalRotation(locR, ikDelta.spinePitchRad[2] * 0.6f, 0.0f, ikDelta.spineYawRad[2] * 0.6f);
                    touched = true;
                    break;
                case HumanoidBoneRole::Head:
                    ComposeAdditiveLocalRotation(locR, ikDelta.spinePitchRad[3], -ikDelta.spineBankRollRad * 0.35f, ikDelta.spineYawRad[3]);
                    touched = true;
                    break;
                case HumanoidBoneRole::LeftThigh:
                    ComposeAdditiveLocalRotation(locR, ikDelta.leftThighPitchRad, 0.0f, 0.0f);
                    touched = true;
                    break;
                case HumanoidBoneRole::LeftCalf:
                    ComposeAdditiveLocalRotation(locR, ikDelta.leftCalfPitchRad, 0.0f, 0.0f);
                    touched = true;
                    break;
                case HumanoidBoneRole::LeftFoot:
                    ComposeAdditiveLocalRotation(locR, ikDelta.leftAnklePitchRad, ikDelta.leftAnkleRollRad, 0.0f);
                    touched = true;
                    break;
                case HumanoidBoneRole::RightThigh:
                    ComposeAdditiveLocalRotation(locR, ikDelta.rightThighPitchRad, 0.0f, 0.0f);
                    touched = true;
                    break;
                case HumanoidBoneRole::RightCalf:
                    ComposeAdditiveLocalRotation(locR, ikDelta.rightCalfPitchRad, 0.0f, 0.0f);
                    touched = true;
                    break;
                case HumanoidBoneRole::RightFoot:
                    ComposeAdditiveLocalRotation(locR, ikDelta.rightAnklePitchRad, ikDelta.rightAnkleRollRad, 0.0f);
                    touched = true;
                    break;
                default:
                    break;
                }
                if (touched) {
                    // Write ONLY to verified +0x48 (local.rotate) and +0x6C (local.translate) — NEVER +0x40!
                    SehWriteBytes(bn + ce17104::kOffNiLocalRotate, locR, 36);
                    SehWriteBytes(bn + ce17104::kOffNiLocalTranslate, locT, 12);
                    {
                        std::lock_guard<std::mutex> lk(g_stateMtx);
                        LiveIkBoneBase& kb = g_liveIkBoneBases[reinterpret_cast<uintptr_t>(bn)];
                        std::memcpy(kb.lastWrittenLocalT, locT, 12);
                        std::memcpy(kb.lastWrittenLocalR, locR, 36);
                        kb.hasLastWritten = true;
                    }
                    ++ikModified;
                }
            }
        }
    }

    // 2. 240 Hz XPBD Soft-Body / Hair / Cloak / WeaponGear Pass (drift-free additive onto +0x48 & +0x6C)
    for (uint32_t i = 0; i < boneCount; ++i) {
        if (!bones[i].isSoft) continue;
        uint8_t* bn = bones[i].node;
        uint8_t* parent = bones[i].parent;
        if (!IsCanonicalPtr(parent)) continue;

        float parentWorld[3]{};
        // Verified NiAVObject::world.translate = +0xA0 (NOT +0x98!)
        if (!SehReadBytes(parent + ce17104::kOffNiWorldTranslate, parentWorld, 12)) continue;
        if (!std::isfinite(parentWorld[0]) || !std::isfinite(parentWorld[1]) || !std::isfinite(parentWorld[2])) continue;

        float curLocalT[3]{}, curLocalR[9]{};
        if (!SehReadBytes(bn + ce17104::kOffNiLocalTranslate, curLocalT, 12) ||
            !SehReadBytes(bn + ce17104::kOffNiLocalRotate, curLocalR, 36)) {
            continue;
        }

        const TissueProfile prof = DefaultTissueProfile(bones[i].softKind);
        SoftBoneOutput out{};
        float baseT[3]{}, baseR[9]{};
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            const uintptr_t key = reinterpret_cast<uintptr_t>(bn);
            LiveBoneEntry& entry = g_liveBones[key];
            const bool unchangedT = entry.state.hasLastWritten && FloatsNearEq(curLocalT, entry.state.lastWrittenLocal, 3);
            const bool unchangedR = entry.state.hasLastWritten && FloatsNearEq(curLocalR, entry.state.lastWrittenRotate, 9);
            if (!entry.state.initialized || !unchangedT) {
                std::memcpy(entry.state.restLocal, curLocalT, 12);
            }
            if (!entry.state.initialized || !unchangedR) {
                std::memcpy(entry.state.restRotate, curLocalR, 36);
                float rSum = 0.0f;
                for (int ri = 0; ri < 9; ++ri) rSum += curLocalR[ri] * curLocalR[ri];
                entry.state.hasRestRotate = std::isfinite(rSum) && rSum > 2.4f && rSum < 3.6f;
            }
            std::memcpy(baseT, entry.state.restLocal, 12);
            std::memcpy(baseR, entry.state.restRotate, 36);

            float effParentWorld[3] = {parentWorld[0], parentWorld[1], parentWorld[2]};
            const auto pIt = g_liveBones.find(reinterpret_cast<uintptr_t>(parent));
            if (pIt != g_liveBones.end() && pIt->second.state.initialized) {
                effParentWorld[0] += pIt->second.state.pos[0] * 0.65f;
                effParentWorld[1] += pIt->second.state.pos[1] * 0.65f;
                effParentWorld[2] += pIt->second.state.pos[2] * 0.65f;
            }

            float colCenter[3] = {0.0f, -2.5f, 0.0f};
            float colRad = 0.0f;
            if (bones[i].softKind == TissueKind::Hair)       { colCenter[1] = 2.8f;  colRad = 3.2f; }
            else if (bones[i].softKind == TissueKind::Cloak) { colCenter[1] = 4.2f;  colRad = 4.8f; }
            else if (bones[i].softKind == TissueKind::Breast){ colCenter[1] = -2.6f; colRad = 2.2f; }
            else if (bones[i].softKind == TissueKind::Glute) { colCenter[1] = 2.4f;  colRad = 2.0f; }

            out = StepSoftBone(entry.state, effParentWorld, prof, dtSec, colRad, colRad > 0.0f ? colCenter : nullptr);
            entry.lastFrame = g_frameCounter;
        }

        // Apply drift-free XPBD translation & rotation relative to live Havok / rest base (+0x6C / +0x48)
        float newT[3] = {
            baseT[0] + out.localOffset[0],
            baseT[1] + out.localOffset[1],
            baseT[2] + out.localOffset[2],
        };
        float newR[9]{};
        std::memcpy(newR, baseR, 36);
        ComposeAdditiveLocalRotation(newR, out.localPitchRad, out.localRollRad, 0.0f);

        SehWriteBytes(bn + ce17104::kOffNiLocalTranslate, newT, 12);
        SehWriteBytes(bn + ce17104::kOffNiLocalRotate, newR, 36);
        {
            std::lock_guard<std::mutex> lk(g_stateMtx);
            LiveBoneEntry& entry = g_liveBones[reinterpret_cast<uintptr_t>(bn)];
            std::memcpy(entry.state.lastWrittenLocal, newT, 12);
            std::memcpy(entry.state.lastWrittenRotate, newR, 36);
            entry.state.hasLastWritten = true;
        }
        ++xpbdModified;
    }

    // 3. Immediate World-Transform Propagation (local -> world +0x7C..+0xAF) BEFORE Main::Draw skins NiSkinInstance!
    uint32_t updatedNodes = 0;
    if (ikModified > 0 || xpbdModified > 0) {
        bool propagatedByEngine = false;
        if (callEngineDownwardPass && g_fnUpdateDownwardPass) {
            NiUpdateData upd{};
            upd.time  = 0.0f;
            upd.flags = 0u;  // Preserve Havok animations & previousWorld motion vectors
            propagatedByEngine = SehCallUpdateDownwardPass(g_fnUpdateDownwardPass, root, &upd);
            if (propagatedByEngine) {
                updatedNodes = boneCount;
            }
        }
        if (!propagatedByEngine) {
            PropagateWorldTransformNode(root, 0, updatedNodes);
        }
    }

    if (outIkBones)      *outIkBones = ikModified;
    if (outXpbdBones)    *outXpbdBones = xpbdModified;
    if (outUpdatedNodes) *outUpdatedNodes = updatedNodes;
    return ikModified + xpbdModified;
}

bool SkeletonPhysicsInstallHooks(uintptr_t moduleBase) {
    if (!moduleBase) return false;
    const auto* base = reinterpret_cast<const uint8_t*>(moduleBase);
    g_base = base;

    // Parse PE headers under SEH to obtain exact SizeOfImage and .text range
    IMAGE_DOS_HEADER dos{};
    if (!SehReadBytes(base, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE) return false;
    IMAGE_NT_HEADERS64 nt{};
    if (!SehReadBytes(base + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE) return false;
    const uint32_t sizeOfImage = nt.OptionalHeader.SizeOfImage;

    // Fail-closed Zydis verification of Main::Draw (0x6565B0) & NiNode::UpdateDownwardPass (0xEE24D0)
    std::string report;
    if (!VerifySkeletonHookPrologues(base, sizeOfImage, &report)) {
        LogDiagnosticLine(("[SkeletonPhysics] Hook installation REFUSED (fail-closed): " + report).c_str());
        return false;
    }

#ifndef RAW_SIM_PURE_TEST
    const uint32_t rvaMainDraw   = Addr("fn:Main::Draw") ? Addr("fn:Main::Draw") : ce17104::kRvaMainDraw;
    const uint32_t rvaDownward   = Addr("fn:NiNode::UpdateDownwardPass") ? Addr("fn:NiNode::UpdateDownwardPass") : ce17104::kRvaNiNodeUpdateDownwardPass;
    g_kPlayer         = Addr("PlayerCharacter::Singleton") ? Addr("PlayerCharacter::Singleton") : ce17104::kRvaPlayerSingleton;
    g_kProcessLists   = Addr("ProcessLists::GetSingleton") ? Addr("ProcessLists::GetSingleton") : ce17104::kRvaProcessListsSingleton;
    g_kHandleEntries  = Addr("BSPointerHandleManager::Entry::GetHandleEntries") ? Addr("BSPointerHandleManager::Entry::GetHandleEntries") : ce17104::kRvaHandleEntriesInline;
    g_kFrameDeltaTime = Addr("global:FrameDeltaTime") ? Addr("global:FrameDeltaTime") : ce17104::kRvaFrameDeltaTime;
    g_kMainDraw       = rvaMainDraw;
    g_kUpdateDownwardPass = rvaDownward;
    g_fnUpdateDownwardPass = reinterpret_cast<NiNodeUpdateDownwardPassFn>(const_cast<uint8_t*>(base + rvaDownward));

    if (g_hookActive.load(std::memory_order_acquire)) {
        return true;
    }

    Image img{};
    img.base = base;
    img.sizeOfImage = sizeOfImage;
    const IMAGE_SECTION_HEADER* sec = reinterpret_cast<const IMAGE_SECTION_HEADER*>(
        base + dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) + nt.FileHeader.SizeOfOptionalHeader);
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i, ++sec) {
        IMAGE_SECTION_HEADER sh{};
        if (SehReadBytes(sec, &sh, sizeof(sh)) && (sh.Characteristics & IMAGE_SCN_MEM_EXECUTE) && img.textEnd == 0) {
            img.textBegin = sh.VirtualAddress;
            img.textEnd = sh.VirtualAddress + sh.Misc.VirtualSize;
        }
    }

    void* orig = nullptr;
    const std::string res = InstallDetour(img, rvaMainDraw, reinterpret_cast<void*>(&HookMainDraw), &orig);
    if (res.rfind("ok", 0) != 0 || !orig) {
        LogDiagnosticLine(("[SkeletonPhysics] Main::Draw detour failed: " + res).c_str());
        return false;
    }
    g_origMainDraw = reinterpret_cast<MainDrawFn>(orig);
    g_hookActive.store(true, std::memory_order_release);
    LogDiagnosticLine(("[SkeletonPhysics] Main::Draw (0x6565B0) post-Havok pre-skinning hook INSTALLED (" + res + ")").c_str());
    return true;
#else
    g_kMainDraw = ce17104::kRvaMainDraw;
    g_kUpdateDownwardPass = ce17104::kRvaNiNodeUpdateDownwardPass;
    g_kPlayer = ce17104::kRvaPlayerSingleton;
    g_kProcessLists = ce17104::kRvaProcessListsSingleton;
    g_kHandleEntries = ce17104::kRvaHandleEntriesInline;
    g_kFrameDeltaTime = ce17104::kRvaFrameDeltaTime;
    return true;
#endif
}

void SkeletonPhysicsShutdown() {
    g_hookActive.store(false, std::memory_order_release);
    g_fnUpdateDownwardPass = nullptr;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        g_liveBones.clear();
        g_liveIkBoneBases.clear();
        g_liveActorIk.clear();
    }
    g_base = nullptr;
}

void SkeletonPhysicsSetWind(float wx, float wy, float wz, float gust01) {
    std::lock_guard<std::mutex> lk(g_windMtx);
    g_windVec[0] = std::isfinite(wx) ? wx : 0.0f;
    g_windVec[1] = std::isfinite(wy) ? wy : 0.0f;
    g_windVec[2] = std::isfinite(wz) ? wz : 0.0f;
    g_windGust01 = std::isfinite(gust01) ? std::clamp(gust01, 0.0f, 1.0f) : 0.0f;
    g_hasExternalWind = true;
}

SkeletonPhysicsStats GetSkeletonPhysicsStats() {
    SkeletonPhysicsStats st{};
    st.mainDrawHookActive = g_hookActive.load(std::memory_order_acquire);
    st.activeActors       = g_lastActorsVisited;
    st.npcActors          = g_lastNpcActors;
    st.highActors         = g_lastHighActors;
    st.lastFrameIkBones   = g_lastIkBones;
    st.lastFrameXpbdBones = g_lastXpbdBones;
    st.lastFrameUpdated   = g_lastUpdatedNodes;
    st.totalHooksRun      = g_totalHooksRun;
    st.totalBonesStepped  = g_totalBonesStepped;
    st.totalSubsteps240Hz = g_totalSubsteps;
    return st;
}

void SoftBodyInit(const Image& img) {
#ifndef RAW_SIM_PURE_TEST
    g_base = img.base;
    g_kPlayer = Addr("PlayerCharacter::Singleton") ? Addr("PlayerCharacter::Singleton") : ce17104::kRvaPlayerSingleton;
    g_kProcessLists = Addr("ProcessLists::GetSingleton") ? Addr("ProcessLists::GetSingleton") : ce17104::kRvaProcessListsSingleton;
    g_kHandleEntries = Addr("BSPointerHandleManager::Entry::GetHandleEntries") ? Addr("BSPointerHandleManager::Entry::GetHandleEntries") : ce17104::kRvaHandleEntriesInline;
    g_kFrameDeltaTime = Addr("global:FrameDeltaTime") ? Addr("global:FrameDeltaTime") : ce17104::kRvaFrameDeltaTime;
#else
    (void)img;
#endif
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        g_liveBones.clear();
        g_liveIkBoneBases.clear();
        g_liveActorIk.clear();
    }
    g_lastTickUs = 0;
    g_frameCounter = 0;
}

void SoftBodyOnPresent() {
    // Bone mutation is strictly executed ONCE per frame inside HookMainDraw (0x6565B0)
    // post-Havok and pre-skinning — NEVER during OnPresent() after rendering!
    if (g_hookActive.load(std::memory_order_acquire)) {
        return;
    }
}

void SoftBodyShutdown() {
    SkeletonPhysicsShutdown();
}

std::string SoftBodyStatsJson() {
    size_t trackedBones = 0;
    {
        std::lock_guard<std::mutex> lk(g_stateMtx);
        trackedBones = g_liveBones.size();
    }
    char b[384];
    std::snprintf(b, sizeof b,
                  "{\"cmd\":\"softbody\",\"wired\":%s,\"main_draw_hook\":%s,"
                  "\"active_actors\":%u,\"npc_actors\":%u,\"high_actors\":%u,"
                  "\"tracked_bones\":%zu,\"last_ik_bones\":%u,\"last_xpbd_bones\":%u,\"last_updated_nodes\":%u,"
                  "\"hooks_run\":%llu,\"softbody_bones_stepped\":%llu,\"softbody_substeps_240hz\":%llu}",
                  (g_base && g_kPlayer && g_kProcessLists && g_kHandleEntries) ? "true" : "false",
                  g_hookActive.load(std::memory_order_acquire) ? "true" : "false",
                  g_lastActorsVisited, g_lastNpcActors, g_lastHighActors,
                  trackedBones, g_lastIkBones, g_lastXpbdBones, g_lastUpdatedNodes,
                  static_cast<unsigned long long>(g_totalHooksRun),
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
