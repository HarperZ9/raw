#pragma once
// Native 240 Hz Sub-Stepped XPBD + Anisotropic Viscoelastic Soft-Body & Live Skeleton Physics Engine.
// Replaces legacy Euler CBPC and heavy CPU Bullet HDT-SMP bone physics with a zero-allocation,
// framerate-independent C++23 solver hooked strictly post-Havok / pre-Main::Draw (0x6565B0)
// with immediate NiNode::UpdateDownwardPass (0xEE24D0) world-transform propagation into NiSkinInstance.
// Original work, MIT.
#include <cstdint>
#include <string>

namespace raw::rt {
struct Image;

// Machine-verified SkyrimSE.exe 1.7.104.0 (TS=0x6A8C7046) RVAs & ABI struct offsets.
namespace ce17104 {
inline constexpr uint32_t kRvaMainDraw                 = 0x6565B0u;
inline constexpr uint32_t kRvaNiNodeUpdateDownwardPass = 0xEE24D0u;
inline constexpr uint32_t kRvaNiAvObjUpdateWorldData   = 0xEEBF40u;
inline constexpr uint32_t kRvaNiNodeGetObjectByName    = 0xEE23A0u;
inline constexpr uint32_t kRvaRefrGet3D2               = 0x2EBCE0u;
inline constexpr uint32_t kRvaPlayerGet3D2             = 0x747AF0u;
inline constexpr uint32_t kRvaPlayerSingleton          = 0x3230778u;
inline constexpr uint32_t kRvaProcessListsSingleton    = 0x219E570u;
inline constexpr uint32_t kRvaHandleEntriesInline      = 0x21A4280u;
inline constexpr uint32_t kRvaFrameDeltaTime           = 0x3275608u;

inline constexpr uint32_t kOffRefrFormType             = 0x1Au;
inline constexpr uint32_t kOffRefrLocation             = 0x54u;
inline constexpr uint32_t kOffRefrLoadedData           = 0x68u;  // Verified @ 0x2EBD08 (NOT 0x70 ExtraDataList!)
inline constexpr uint32_t kOffLoadedData3D             = 0x68u;  // Verified @ 0x2EBD11 (NOT 0x00!)
inline constexpr uint32_t kOffPlayerFirstPerson3D      = 0x900u; // Verified @ 0x747B06 / 0x747B74

inline constexpr uint32_t kOffNiName                   = 0x10u;  // Verified @ 0xEE0A45
inline constexpr uint32_t kOffNiParent                 = 0x30u;  // Verified @ 0xEEBFD6
inline constexpr uint32_t kOffNiCollisionObject        = 0x40u;  // Verified @ 0xEEBFBF — NEVER WRITE!
inline constexpr uint32_t kOffNiLocalRotate            = 0x48u;  // Verified @ 0xEEC039 / 0xEEC087 (36 bytes)
inline constexpr uint32_t kOffNiLocalTranslate         = 0x6Cu;  // Verified @ 0xEEC0A3 (12 bytes)
inline constexpr uint32_t kOffNiLocalScale             = 0x78u;  // Verified @ 0xEEC0BE (4 bytes)
inline constexpr uint32_t kOffNiWorldRotate            = 0x7Cu;  // Verified @ 0xEEBF6A (36 bytes)
inline constexpr uint32_t kOffNiWorldTranslate         = 0xA0u;  // Verified @ 0xEEBF8F (12 bytes)
inline constexpr uint32_t kOffNiWorldScale             = 0xACu;  // Verified @ 0xEEBFB3 (4 bytes)
// Wave 82 ABI layout guards (SkyrimSE 1.7.104.0 verified NiAVObject / TESObjectREFR offsets):
static_assert(kOffNiLocalTranslate == kOffNiLocalRotate + 36u, "NiAVObject local translate follows 3x3 rotate");
static_assert(kOffNiLocalScale == kOffNiLocalTranslate + 12u, "NiAVObject local scale follows translate");
static_assert(kOffNiWorldTranslate == kOffNiWorldRotate + 36u, "NiAVObject world translate follows 3x3 rotate");
static_assert(kOffNiWorldScale == kOffNiWorldTranslate + 12u, "NiAVObject world scale follows translate");
static_assert(kOffRefrLoadedData == 0x68u && kOffLoadedData3D == 0x68u, "TESObjectREFR loaded3D ABI lock");
inline constexpr uint32_t kOffNiFlags                  = 0xF4u;  // Verified @ 0xEE0E56
inline constexpr uint32_t kOffNiNodeChildrenData       = 0x118u; // Verified @ 0xEE23D0 / 0xEE2570
inline constexpr uint32_t kOffNiNodeChildrenCap        = 0x120u;
inline constexpr uint32_t kOffNiNodeChildrenFreeSize   = 0x122u; // Verified @ 0xEE23C1 / 0xEE2536 (high-water slot bound!)
inline constexpr uint32_t kOffNiNodeChildrenSize       = 0x124u;

inline constexpr uint32_t kOffProcessListsHighData     = 0x30u;  // Verified in ProcessLists::Dtor 0x77A69C..0x77A72B
inline constexpr uint32_t kOffProcessListsHighSize     = 0x40u;
inline constexpr uint32_t kHandleIndexMask             = 0x000FFFFFu;
inline constexpr uint32_t kHandleAgeMask               = 0x03F00000u;
inline constexpr uint32_t kHandleInUseBit              = 0x04000000u; // Verified @ 0x176512
inline constexpr uint32_t kHandleRefObjOffset          = 0x20u;       // BSHandleRefObject* = refr + 0x20
}  // namespace ce17104

enum class TissueKind : uint8_t {
    Breast     = 0,
    Glute      = 1,
    Belly      = 2,
    Thigh      = 3,
    Pelvic     = 4,   // Multi-axis anatomical pendulum/scrotal viscoelastic dynamics
    Hair       = 5,
    Cloak      = 6,
    WeaponGear = 7,   // Vanilla scabbard/quiver/weapon/satchel secondary inertia swing
};

struct TissueProfile {
    float stiffness[3];     // Anisotropic spring stiffness (X lateral, Y anterior-posterior, Z vertical)
    float damping[3];       // Viscoelastic damping ratios per axis
    float gravitySag;       // Rest-pose gravity sag offset (game units, -Z)
    float maxDisp[3];       // XPBD hard strain limit per axis (prevents clipping/stretching at any FPS)
    float rotCouple;        // Rotational inertia coupling (radians per unit displacement)
    float volumePreserve;   // Squash-and-stretch Poisson volume preservation ratio (0..0.45)
};

struct SoftBoneState {
    float pos[3] = {};          // Current local displacement (game units)
    float vel[3] = {};          // Current local velocity (game units/sec)
    float prevParent[3] = {};   // Previous parent world position (for acceleration forcing)
    float prevVel[3] = {};      // Previous parent world velocity
    float restLocal[3] = {};    // Captured Havok/bind-pose NiAVObject::local.translate (+0x6C)
    float restRotate[9] = {};   // Captured Havok/bind-pose NiAVObject::local.rotate 3x3 (+0x48)
    float lastWrittenLocal[3] = {};   // Last written local.translate (+0x6C) for Havok-overwrite detection
    float lastWrittenRotate[9] = {};  // Last written local.rotate (+0x48) for Havok-overwrite detection
    bool  hasRestRotate = false;
    bool  hasLastWritten = false;
    bool  initialized = false;
};

struct SoftBoneOutput {
    float localOffset[3] = {};  // Local translation delta to apply to NiAVObject::local.translate (+0x6C)
    float localPitchRad = 0.0f; // Local X pitch rotation delta (cone-clamped)
    float localRollRad  = 0.0f; // Local Y roll rotation delta (cone-clamped)
    float localScale[3] = {1.0f, 1.0f, 1.0f}; // Poisson volume-preserving squash/stretch
};

struct SkeletonPhysicsStats {
    bool     mainDrawHookActive = false;
    uint32_t activeActors       = 0;
    uint32_t npcActors          = 0;
    uint32_t highActors         = 0;
    uint32_t lastFrameIkBones   = 0;
    uint32_t lastFrameXpbdBones = 0;
    uint32_t lastFrameUpdated   = 0;
    uint64_t totalHooksRun      = 0;
    uint64_t totalBonesStepped  = 0;
    uint64_t totalSubsteps240Hz = 0;
};

// Returns calibrated 2027 viscoelastic tissue / cloth / hair / gear parameters.
TissueProfile DefaultTissueProfile(TissueKind kind);

// Advances one soft-body bone by dtSec using fixed 240 Hz sub-steps with XPBD strain limiting & capsule clamping.
SoftBoneOutput StepSoftBone(SoftBoneState& state,
                            const float parentWorldPos[3],
                            const TissueProfile& prof,
                            float dtSec,
                            float colliderRadius = 0.0f,
                            const float* colliderLocalCenter = nullptr);

// Classifies a Skyrim skeleton bone name (vanilla / XPMSSE / 3BA / UBE / HDT-SMP) into a TissueKind.
bool ClassifySoftBone(const char* boneName, TissueKind* outKind);

// Verified SEH-safe ABI helpers (used live in Main::Draw detour and directly verified in unit tests):
void* ResolveRefr3DRoot(const void* refr, bool isPlayerFirstPerson = false);
void* ResolveHandleEntryRefr(const void* handleEntriesBase, uint32_t handle, uint32_t maxEntries = 0x100000u);

// Zydis-verifies Main::Draw (0x6565B0), NiNode::UpdateDownwardPass (0xEE24D0), and NiAVObject::UpdateWorldData (0xEEBF40).
bool VerifySkeletonHookPrologues(const uint8_t* moduleBase, uint32_t sizeOfImage, std::string* outReport = nullptr);

// Steps IK + XPBD across one actor's NiNode hierarchy using exact +0x48/+0x6C/+0x7C/+0xA0/+0x122 offsets,
// and immediately propagates local -> world transforms down modified subtrees.
uint32_t StepSkeletonSubtree(void* rootNode,
                             uintptr_t actorKey,
                             float dtSec,
                             float timeSec,
                             uint32_t* outIkBones = nullptr,
                             uint32_t* outXpbdBones = nullptr,
                             uint32_t* outUpdatedNodes = nullptr,
                             bool callEngineDownwardPass = false);

// Wave 39 Post-Havok / Pre-Skinning Main::Draw (0x6565B0) Hook API:
bool SkeletonPhysicsInstallHooks(uintptr_t moduleBase);
void SkeletonPhysicsShutdown();
void SkeletonPhysicsSetWind(float wx, float wy, float wz, float gust01);
SkeletonPhysicsStats GetSkeletonPhysicsStats();

// Backwards-compatible lifecycle entry points (SoftBodyOnPresent is a no-op once Main::Draw hook is active):
void SoftBodyInit(const Image& img);
void SoftBodyOnPresent();
void SoftBodyShutdown();
std::string SoftBodyStatsJson();

// Copies up to maxActors live high-process NPC world positions (x, y, z, 1.0f) for GPU world-field interaction.
uint32_t CopyActiveActorPositions(float* outXyzw, uint32_t maxActors);

}  // namespace raw::rt
