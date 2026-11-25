// test_skeleton_physics.cpp: Machine-verified unit & real-binary Zydis prologue verification suite
// for Wave 39 live skeleton IK, hair, cloth, weapon-gear & 240 Hz XPBD soft-body physics.
// Export: int raw::rt::RunSkeletonPhysicsTests(void (*checkFn)(bool, const char*) = nullptr)
// Original work, MIT.

#include "../src/engine_rt/rt_skeleton_ik.h"
#include "../src/engine_rt/rt_softbody.h"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace raw::rt {
namespace {

// Exact synthetic SkyrimSE 1.7.104 NiNode buffer matching verified binary offsets:
//   +0x10 name, +0x30 parent, +0x40 collisionObject (GUARDED SENTINEL!),
//   +0x48 local.rotate (36B), +0x6C local.translate (12B), +0x78 local.scale (4B),
//   +0x7C world.rotate (36B), +0xA0 world.translate (12B), +0xAC world.scale (4B),
//   +0xF4 flags, +0x118 children.data, +0x120 children.cap, +0x122 children.freeSize, +0x124 children.size.
struct alignas(16) FakeNiNode {
    uint8_t bytes[0x140]{};

    FakeNiNode(const char* name, FakeNiNode* parent, float lx, float ly, float lz) {
        std::memset(bytes, 0, sizeof(bytes));
        std::memcpy(bytes + ce17104::kOffNiName, &name, 8);
        void* pPtr = parent ? parent->bytes : nullptr;
        std::memcpy(bytes + ce17104::kOffNiParent, &pPtr, 8);
        // Poison sentinel at +0x40 (NiCollisionObject*) — MUST NEVER BE MODIFIED!
        const uint64_t sentinel = 0xDEADBEEFCAFEBABEull;
        std::memcpy(bytes + ce17104::kOffNiCollisionObject, &sentinel, 8);

        const float eye[9] = {1, 0, 0,  0, 1, 0,  0, 0, 1};
        const float tr[3]  = {lx, ly, lz};
        const float sc     = 1.0f;
        std::memcpy(bytes + ce17104::kOffNiLocalRotate, eye, 36);
        std::memcpy(bytes + ce17104::kOffNiLocalTranslate, tr, 12);
        std::memcpy(bytes + ce17104::kOffNiLocalScale, &sc, 4);
        std::memcpy(bytes + ce17104::kOffNiWorldRotate, eye, 36);
        std::memcpy(bytes + ce17104::kOffNiWorldTranslate, tr, 12);
        std::memcpy(bytes + ce17104::kOffNiWorldScale, &sc, 4);
    }

    void SetChildren(void** arr, uint16_t cap, uint16_t freeSize, uint16_t occupiedSize) {
        std::memcpy(bytes + ce17104::kOffNiNodeChildrenData, &arr, 8);
        std::memcpy(bytes + ce17104::kOffNiNodeChildrenCap, &cap, 2);
        std::memcpy(bytes + ce17104::kOffNiNodeChildrenFreeSize, &freeSize, 2);
        std::memcpy(bytes + ce17104::kOffNiNodeChildrenSize, &occupiedSize, 2);
    }

    uint64_t CollisionSentinel() const {
        uint64_t v = 0;
        std::memcpy(&v, bytes + ce17104::kOffNiCollisionObject, 8);
        return v;
    }

    void GetLocalT(float out[3]) const { std::memcpy(out, bytes + ce17104::kOffNiLocalTranslate, 12); }
    void GetWorldT(float out[3]) const { std::memcpy(out, bytes + ce17104::kOffNiWorldTranslate, 12); }
    void GetLocalR(float out[9]) const { std::memcpy(out, bytes + ce17104::kOffNiLocalRotate, 36); }
};

bool MapSkyrimPeForPrologueTest(const char* path, std::vector<uint8_t>* outImg) {
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f) return false;
    const size_t sz = static_cast<size_t>(f.tellg());
    if (sz < 0x1000) return false;
    f.seekg(0);
    std::vector<uint8_t> raw(sz);
    f.read(reinterpret_cast<char*>(raw.data()), static_cast<std::streamsize>(sz));

    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(raw.data());
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(raw.data() + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;

    const uint32_t imgSz = nt->OptionalHeader.SizeOfImage;
    outImg->assign(imgSz, 0u);
    std::memcpy(outImg->data(), raw.data(), nt->OptionalHeader.SizeOfHeaders);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (sec->SizeOfRawData == 0 || sec->VirtualAddress >= imgSz) continue;
        const size_t copyLen = (std::min)(static_cast<size_t>(sec->SizeOfRawData),
                                          static_cast<size_t>(imgSz - sec->VirtualAddress));
        std::memcpy(outImg->data() + sec->VirtualAddress, raw.data() + sec->PointerToRawData, copyLen);
    }
    return true;
}

}  // namespace

int RunSkeletonPhysicsTests(void (*checkFn)(bool, const char*)) {
    int localFail = 0;
    auto check = [&](bool cond, const char* name) {
        if (checkFn) {
            checkFn(cond, name);
        } else {
            std::printf("%s %s\n", cond ? "PASS" : "FAIL", name);
        }
        if (!cond) ++localFail;
    };

    // 1. TESObjectREFR::Get3D2 (+0x68 -> +0x68) & PlayerCharacter 1st-person (+0x900) verification
    {
        alignas(16) uint8_t fakeRefr[0x980]{};
        alignas(16) uint8_t fakeLoaded[0x100]{};
        alignas(16) uint8_t fake3D[0x40]{};
        alignas(16) uint8_t fake1stP[0x40]{};
        alignas(16) uint8_t poisonExtraList[0x40]{};

        void* pLoaded = fakeLoaded;
        void* pPoison = poisonExtraList;
        void* p3D     = fake3D;
        void* p1stP   = fake1stP;
        std::memcpy(fakeRefr + ce17104::kOffRefrLoadedData, &pLoaded, 8); // +0x68
        std::memcpy(fakeRefr + 0x70u, &pPoison, 8);                       // +0x70 ExtraDataList (old buggy offset!)
        std::memcpy(fakeLoaded + ce17104::kOffLoadedData3D, &p3D, 8);     // +0x68 data3D
        std::memcpy(fakeRefr + ce17104::kOffPlayerFirstPerson3D, &p1stP, 8); // +0x900

        void* got3P = ResolveRefr3DRoot(fakeRefr, false);
        void* got1P = ResolveRefr3DRoot(fakeRefr, true);
        void* gotNull = ResolveRefr3DRoot(reinterpret_cast<void*>(0x1000u), false); // SEH unmapped test!
        check(got3P == fake3D, "skel_refr_get3d_exact_0x68_to_0x68");
        check(got1P == fake1stP, "skel_player_get3d_firstperson_0x900");
        check(gotNull == nullptr, "skel_refr_get3d_seh_unmapped_safe");
    }

    // 2. BSPointerHandleManager::Entry (0x21A4280) inUse & age validation
    {
        alignas(16) uint8_t fakeRefrObj[0x80]{};
        uint8_t* handleRefObj = fakeRefrObj + ce17104::kHandleRefObjOffset; // +0x20
        alignas(16) uint8_t fakeHandleTable[64 * 16]{};

        const uint32_t idx = 7u;
        const uint32_t age = 0x00500000u;
        const uint32_t validHandle = idx | age;
        const uint32_t validBits   = ce17104::kHandleInUseBit | age | idx;
        std::memcpy(fakeHandleTable + idx * 16u + 0u, &validBits, 4);
        std::memcpy(fakeHandleTable + idx * 16u + 8u, &handleRefObj, 8);

        void* resolved = ResolveHandleEntryRefr(fakeHandleTable, validHandle, 64u);
        check(resolved == fakeRefrObj, "skel_handle_entry_valid_inuse_age");

        // Reject wrong age
        const uint32_t badAgeHandle = idx | 0x00600000u;
        check(ResolveHandleEntryRefr(fakeHandleTable, badAgeHandle, 64u) == nullptr, "skel_handle_entry_reject_bad_age");

        // Reject cleared inUse bit 26
        const uint32_t freeBits = age | idx;
        std::memcpy(fakeHandleTable + idx * 16u + 0u, &freeBits, 4);
        check(ResolveHandleEntryRefr(fakeHandleTable, validHandle, 64u) == nullptr, "skel_handle_entry_reject_not_inuse");

        // Reject out-of-range index
        check(ResolveHandleEntryRefr(fakeHandleTable, 999u | age, 64u) == nullptr, "skel_handle_entry_reject_oob");
    }

    // 3. Synthetic NiNode hierarchy with NULL holes in children (+0x122 freeSize > +0x124 size),
    //    exact +0x48/+0x6C/+0x7C/+0xA0 updates, AND +0x40 NiCollisionObject* UNTOUCHED!
    {
        SkeletonPhysicsShutdown();
        SkeletonPhysicsSetWind(95.0f, 35.0f, 5.0f, 0.8f);

        FakeNiNode root("NPC Root [Root]", nullptr, 100.0f, 200.0f, 0.0f);
        FakeNiNode pelvis("NPC Pelvis [Pelv]", &root, 0.0f, 0.0f, 68.0f);
        FakeNiNode spine2("NPC Spine2 [Spn2]", &pelvis, 0.0f, 0.0f, 22.0f);
        FakeNiNode head("NPC Head [Head]", &spine2, 0.0f, 0.0f, 18.0f);
        FakeNiNode lFoot("NPC L Foot [Lft ]", &pelvis, -10.0f, 0.0f, -68.0f);
        FakeNiNode rFoot("NPC R Foot [Rft ]", &pelvis, 10.0f, 0.0f, -62.0f);
        FakeNiNode hair("NPC HairPonytail01", &head, 0.0f, -6.0f, 4.0f);
        FakeNiNode cloak("HDT CloakBack01", &spine2, 0.0f, -8.0f, -4.0f);
        FakeNiNode breast("NPC L Breast", &spine2, -5.0f, 6.0f, -2.0f);
        FakeNiNode quiver("QUIVER", &spine2, 4.0f, -7.0f, 2.0f);

        // Deliberately insert NULL holes so freeSize (+0x122) = 7 while occupied size (+0x124) = 4!
        // Under the old buggy +0x124 loop, cloak/breast/quiver at slots 4..6 were COMPLETELY MISSED!
        void* rootKids[2]   = {pelvis.bytes, nullptr};
        void* pelvisKids[4] = {spine2.bytes, nullptr, lFoot.bytes, rFoot.bytes};
        void* spineKids[8]  = {head.bytes, nullptr, nullptr, nullptr, cloak.bytes, breast.bytes, quiver.bytes, nullptr};
        void* headKids[3]   = {nullptr, nullptr, hair.bytes};

        root.SetChildren(rootKids, 2, 2, 1);
        pelvis.SetChildren(pelvisKids, 4, 4, 3);
        spine2.SetChildren(spineKids, 8, 7, 4);
        head.SetChildren(headKids, 3, 3, 1);

        // Step 20 frames with gentle root motion so XPBD & IK both activate visibly
        uint32_t ikCount = 0, xpbdCount = 0, updatedNodes = 0;
        for (int f = 0; f < 20; ++f) {
            const float rootT[3] = {100.0f + float(f) * 4.5f, 200.0f + std::sin(float(f) * 0.3f) * 6.0f, 0.0f};
            std::memcpy(root.bytes + ce17104::kOffNiLocalTranslate, rootT, 12);
            std::memcpy(root.bytes + ce17104::kOffNiWorldTranslate, rootT, 12);
            StepSkeletonSubtree(root.bytes, 0x12345u, 1.0f / 60.0f, float(f) / 60.0f,
                                &ikCount, &xpbdCount, &updatedNodes, false);
        }

        check(ikCount == 5u, "skel_ik_hits_all_5_humanoid_bones_across_null_holes");
        check(xpbdCount == 4u, "skel_xpbd_hits_hair_cloak_breast_quiver_beyond_size_offset");
        check(updatedNodes >= 10u, "skel_world_propagation_updated_full_subtree");

        // Verify +0x40 NiCollisionObject* pointer sentinel is 100% UNTOUCHED on EVERY SINGLE NODE!
        const bool allSentinelsIntact =
            root.CollisionSentinel()   == 0xDEADBEEFCAFEBABEull &&
            pelvis.CollisionSentinel() == 0xDEADBEEFCAFEBABEull &&
            spine2.CollisionSentinel() == 0xDEADBEEFCAFEBABEull &&
            head.CollisionSentinel()   == 0xDEADBEEFCAFEBABEull &&
            hair.CollisionSentinel()   == 0xDEADBEEFCAFEBABEull &&
            cloak.CollisionSentinel()  == 0xDEADBEEFCAFEBABEull &&
            breast.CollisionSentinel() == 0xDEADBEEFCAFEBABEull &&
            quiver.CollisionSentinel() == 0xDEADBEEFCAFEBABEull;
        check(allSentinelsIntact, "skel_collision_object_0x40_never_clobbered");

        // Verify world.translate (+0xA0) was actually propagated from updated local.translate (+0x6C)
        float cloakWorld[3]{}, hairWorld[3]{};
        cloak.GetWorldT(cloakWorld);
        hair.GetWorldT(hairWorld);
        check(std::isfinite(cloakWorld[0]) && std::isfinite(cloakWorld[1]) && std::isfinite(cloakWorld[2]) &&
              cloakWorld[0] > 140.0f, "skel_cloak_world_translate_0xA0_propagated");
        check(std::isfinite(hairWorld[0]) && std::isfinite(hairWorld[1]) && std::isfinite(hairWorld[2]) &&
              hairWorld[2] > 90.0f, "skel_hair_world_translate_0xA0_propagated");
    }

    // 4. 240 Hz XPBD 600-frame stability, wind response, strain limits & capsule push-out
    {
        SkeletonPhysicsSetWind(120.0f, 0.0f, 0.0f, 1.0f);
        const TissueKind kinds[] = {
            TissueKind::Breast, TissueKind::Glute, TissueKind::Pelvic,
            TissueKind::Hair, TissueKind::Cloak, TissueKind::WeaponGear
        };
        bool allFiniteAndBounded = true;
        float hairWindDispX = 0.0f;
        for (TissueKind k : kinds) {
            SoftBoneState st{};
            const TissueProfile prof = DefaultTissueProfile(k);
            const float colC[3] = {0.0f, 0.0f, 0.0f};
            const float colR = (k == TissueKind::Hair) ? 2.5f : 0.0f;
            for (int f = 0; f < 600; ++f) {
                const float pPos[3] = {std::sin(float(f) * 0.12f) * 18.0f, std::cos(float(f) * 0.09f) * 12.0f, std::sin(float(f) * 0.25f) * 4.0f};
                const SoftBoneOutput o = StepSoftBone(st, pPos, prof, 1.0f / 60.0f, colR, colR > 0.0f ? colC : nullptr);
                for (int a = 0; a < 3; ++a) {
                    if (!std::isfinite(o.localOffset[a]) || std::fabs(o.localOffset[a]) > prof.maxDisp[a] + std::fabs(prof.gravitySag) + 3.0f) {
                        allFiniteAndBounded = false;
                    }
                }
                if (k == TissueKind::Hair && f == 599) {
                    hairWindDispX = o.localOffset[0];
                }
            }
        }
        check(allFiniteAndBounded, "skel_xpbd_600frames_zero_nan_and_bounded");
        check(hairWindDispX > 0.25f, "skel_xpbd_hair_responds_to_wind_forcing");
    }

    // 5. Analytical 2-Bone IK & Foot Planting verification
    {
        const TwoBoneIkResult ik = SolveTwoBoneIK({0, 0, 80}, {0, 10, 10}, {0, 1, 0}, 40.0f, 40.0f);
        check(ik.solved && ik.midPos.y > 0.0f && ik.reachRatio > 0.85f && ik.reachRatio <= 1.0f,
              "skel_ik_two_bone_reach_and_pole");

        FootPlantInput fp{};
        fp.pelvisBaseZ = 68.0f;
        fp.leftGroundZ = 76.0f;
        fp.rightGroundZ = 64.0f;
        fp.snowDepthU = 15.0f;
        const FootPlantOutput fpo = ComputeFootPlanting(fp);
        check(fpo.pelvisOffsetZ < 0.0f && fpo.leftFootLiftZ > fpo.rightFootLiftZ && fpo.strideDampMult < 0.9f,
              "skel_ik_foot_planting_stairs_and_snow_sink");
    }

    // 6. Real-Binary Zydis Prologue Verification against SkyrimSE.exe 1.7.104.0 (if installed on host)
    {
        const char* skyrimPath = "H:\\SteamLibrary\\steamapps\\common\\Skyrim Special Edition\\SkyrimSE.exe";
        if (std::filesystem::exists(skyrimPath)) {
            std::vector<uint8_t> mapped;
            const bool mappedOk = MapSkyrimPeForPrologueTest(skyrimPath, &mapped);
            std::string rep;
            const bool prologuesOk = mappedOk && VerifySkeletonHookPrologues(mapped.data(), static_cast<uint32_t>(mapped.size()), &rep);
            check(prologuesOk, "skel_real_binary_zydis_prologues_1_7_104");
        } else {
            // Synthetic clean 18-byte prologue check when running on CI without SkyrimSE.exe
            std::vector<uint8_t> synth(ce17104::kRvaNiAvObjUpdateWorldData + 128u, 0x90u);
            // Main::Draw exact 1.7.104 prologue: push rbx; push rbp; push rsi; push rdi; push r12; push r14; push r15; sub rsp, 0x160
            const uint8_t mainDrawBytes[] = {0x40, 0x53, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0x60, 0x01, 0x00, 0x00};
            std::memcpy(synth.data() + ce17104::kRvaMainDraw, mainDrawBytes, sizeof(mainDrawBytes));
            std::string rep;
            check(VerifySkeletonHookPrologues(synth.data(), static_cast<uint32_t>(synth.size()), &rep),
                  "skel_synthetic_zydis_prologues_verified");
        }
    }

    SkeletonPhysicsShutdown();
    return localFail;
}

}  // namespace raw::rt

#ifdef RAW_SKELETON_PHYSICS_STANDALONE_MAIN
int main() {
    const int fails = raw::rt::RunSkeletonPhysicsTests(nullptr);
    std::printf("SkeletonPhysicsTests completed: %s (failures=%d)\n", fails == 0 ? "PASS" : "FAIL", fails);
    return fails == 0 ? 0 : 1;
}
#endif
