// rt_physics_camera_gmst.cpp: Wave 51 Binary-Verified 1.7.104.0 Havok Ragdoll Impulse, Shield Block,
// Combat Regen, Bash & Mounted Camera GMSTs.
// Original work, MIT.
#include "rt_physics_camera_gmst.h"
#include "rt_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_callgraph.h"
#include "rt_version.h"
#include <windows.h>
#endif

namespace raw::rt {
namespace {

// Every single GMST RVA, on-disk float default, and .text reader verified directly against SkyrimSE.exe 1.7.104.0:
//   - Havok Ragdoll Death Force & Clutter Physics (fn:Combat::ComputeDeathForce 0x41FC60, fn:Havok::ComputeGameplayImpulse 0x4047DE):
//       fDeathForceForceMin         (0x20A7500, def=35.0,   readers 0x41FC9C, 0x677B98) -> 20.0
//       fDeathForceForceMax         (0x20A7518, def=85.0,   reader  0x41FCA4)           -> 48.0
//       fDeathForceDamageMax        (0x20A7548, def=50.0,   reader  0x41FC94)           -> 75.0
//       fDeathForceRangedForceMin   (0x20A7560, def=20.0,   reader  0x41FC7A)           -> 10.0
//       fDeathForceRangedForceMax   (0x20A7578, def=40.0,   reader  0x41FC82)           -> 22.0
//       fDeathForceSpellImpactMult  (0x20A75C0, def=2.0,    readers 0x6AB3CF, 0x80122D) -> 1.35
//       fGameplayImpulseScale       (0x20A9E40, def=150.0,  reader  0x404883)           -> 115.0
//       fPhysicsDamageSpeedMin      (0x20A6B88, def=500.0,  readers 0x3FBF23, 0x3FC07A) -> 650.0
//       fPhysicsDamage1Damage       (0x20A6B10, def=1.0,    reader  0x405330)           -> 0.75
//   - Shield/Weapon Block & Bash (0x41F538, fn:Combat::ComputeBlockStamina 0x41F850, fn:Combat::ComputeBashScale 0x41F0A0):
//       fBlockWeaponBase            (0x20A7E48, def=0.05,   reader  0x41F538)           -> 0.32
//       fBlockWeaponScaling         (0x20A7E60, def=0.05,   reader  0x41F52C)           -> 0.06
//       fBlockPowerAttackMult       (0x20A7E78, def=0.75,   reader  0x41F547)           -> 0.68
//       fStaminaBlockDmgMult        (0x20A7B78, def=0.25,   reader  0x41F86A)           -> 0.22
//       fShieldBashPCMax            (0x20A5EC8, def=0.5,    reader  0x41F101)           -> 0.65
//       fWeaponBashPCMax            (0x20A5F28, def=0.5,    reader  0x41F184)           -> 0.55
//   - Combat Regen & Mounted Camera (fn:Actor::ComputeCombatRegenMult 0x6C4989, fn:Actor::ComputeDamagedRegenDelay 0x6C54C0):
//       fCombatMagickaRegenRateMult (0x20A1328, def=0.75,   reader  0x6C49DD)           -> 0.62
//       fCombatStaminaRegenRateMult (0x20A1340, def=0.75,   reader  0x6C49C9)           -> 0.82
//       fDamagedHealthRegenDelay    (0x20A96C0, def=1.0,    reader  0x6C554C)           -> 2.25
//       fDamagedStaminaRegenDelay   (0x20A96F0, def=1.0,    reader  0x6C5521)           -> 0.75
//       fMountedMaxLookingDown      (0x20A0458, def=40.0,   readers 0x67CEE9, 0x692F2D) -> 58.0
//   - Dead Legacy 0-Reader GMSTs (confirmed 0 .text readers via exhaustive ModRM scan, strictly never written!):
//       fBlockMax                   (0x20A71A0, def=0.75,    0 .text readers - clamped by fPlayerMaxResistance!)
//       fBlockSkillBase             (0x20A7170, def=0.0,     0 .text readers)
//       fBlockAmountWeaponMult      (0x20A71B8, def=0.5,     0 .text readers)
//       fArrowFakeMass              (0x20A6408, def=0.25,    0 .text readers)
//       fCombatHealthRegenRateMult  (0x20A1310, def=0.75,    0 .text readers)
//       fHorseMountOffsetX          (0x20A8B08, def=-63.479, 0 .text readers)
constexpr PhysicsCameraGmstSpec kPhysicsCameraSpecs[] = {
    {"fDeathForceForceMin",         0x20A7500u, 35.0f,    20.0f,    true,  "Ragdoll"},
    {"fDeathForceForceMax",         0x20A7518u, 85.0f,    48.0f,    true,  "Ragdoll"},
    {"fDeathForceDamageMax",        0x20A7548u, 50.0f,    75.0f,    true,  "Ragdoll"},
    {"fDeathForceRangedForceMin",   0x20A7560u, 20.0f,    10.0f,    true,  "Ragdoll"},
    {"fDeathForceRangedForceMax",   0x20A7578u, 40.0f,    22.0f,    true,  "Ragdoll"},
    {"fDeathForceSpellImpactMult",  0x20A75C0u, 2.0f,     1.35f,    true,  "Ragdoll"},
    {"fGameplayImpulseScale",       0x20A9E40u, 150.0f,   115.0f,   true,  "Physics"},
    {"fPhysicsDamageSpeedMin",      0x20A6B88u, 500.0f,   650.0f,   true,  "Physics"},
    {"fPhysicsDamage1Damage",       0x20A6B10u, 1.0f,     0.75f,    true,  "Physics"},
    {"fBlockWeaponBase",            0x20A7E48u, 0.05f,    0.32f,    true,  "Block"},
    {"fBlockWeaponScaling",         0x20A7E60u, 0.05f,    0.06f,    true,  "Block"},
    {"fBlockPowerAttackMult",       0x20A7E78u, 0.75f,    0.68f,    true,  "Block"},
    {"fStaminaBlockDmgMult",        0x20A7B78u, 0.25f,    0.22f,    true,  "Block"},
    {"fShieldBashPCMax",            0x20A5EC8u, 0.5f,     0.65f,    true,  "Bash"},
    {"fWeaponBashPCMax",            0x20A5F28u, 0.5f,     0.55f,    true,  "Bash"},
    {"fCombatMagickaRegenRateMult", 0x20A1328u, 0.75f,    0.62f,    true,  "Regen"},
    {"fCombatStaminaRegenRateMult", 0x20A1340u, 0.75f,    0.82f,    true,  "Regen"},
    {"fDamagedHealthRegenDelay",    0x20A96C0u, 1.0f,     2.25f,    true,  "Regen"},
    {"fDamagedStaminaRegenDelay",   0x20A96F0u, 1.0f,     0.75f,    true,  "Regen"},
    {"fMountedMaxLookingDown",      0x20A0458u, 40.0f,    58.0f,    true,  "Camera"},
    // Dead 0-reader legacy GMSTs (verified on disk, strictly never written):
    {"fBlockMax",                   0x20A71A0u, 0.75f,    0.75f,    false, "Block"},
    {"fBlockSkillBase",             0x20A7170u, 0.0f,     0.0f,     false, "Block"},
    {"fBlockAmountWeaponMult",      0x20A71B8u, 0.5f,     0.5f,     false, "Block"},
    {"fArrowFakeMass",              0x20A6408u, 0.25f,    0.25f,    false, "Ragdoll"},
    {"fCombatHealthRegenRateMult",  0x20A1310u, 0.75f,    0.75f,    false, "Regen"},
    {"fHorseMountOffsetX",          0x20A8B08u, -63.479f, -63.479f, false, "Camera"},
};

constexpr size_t kPhysicsCameraSpecCount = sizeof(kPhysicsCameraSpecs) / sizeof(kPhysicsCameraSpecs[0]);

std::mutex            g_pcStateMu;
RawPhysicsCameraState g_pcState{};

}  // namespace

const PhysicsCameraGmstSpec* PhysicsCameraGmstTable(size_t* outCount) {
    if (outCount) *outCount = kPhysicsCameraSpecCount;
    return kPhysicsCameraSpecs;
}

bool VerifyPhysicsCameraGmstTable(const Image& img, uint32_t* outVerifiedCount) {
    if (outVerifiedCount) *outVerifiedCount = 0u;
    if (!img.base || img.sizeOfImage < 0x20B0000u) return false;

    uint64_t imageBase = reinterpret_cast<uint64_t>(img.base);
    if (img.base[0] == 'M' && img.base[1] == 'Z') {
        uint32_t lfanew = 0u;
        std::memcpy(&lfanew, img.base + 0x3Cu, 4u);
        if (lfanew + 0x40u < img.sizeOfImage) {
            uint64_t optBase = 0ull;
            std::memcpy(&optBase, img.base + lfanew + 0x30u, 8u);
            if (optBase >= 0x100000000ull) imageBase = optBase;
        }
    }

    uint32_t verified = 0u;
    for (size_t i = 0; i < kPhysicsCameraSpecCount; ++i) {
        const auto& spec = kPhysicsCameraSpecs[i];
        if (spec.expectedRva + 24u > img.sizeOfImage) return false;

        const uint8_t* obj = img.base + spec.expectedRva;
        float val = 0.0f;
        uint64_t nameVa = 0ull;
        std::memcpy(&val, obj + 8u, 4u);
        std::memcpy(&nameVa, obj + 16u, 8u);

        uint64_t nameRva = 0ull;
        if (nameVa >= imageBase && (nameVa - imageBase) < img.sizeOfImage) {
            nameRva = nameVa - imageBase;
        } else if (nameVa >= reinterpret_cast<uint64_t>(img.base) &&
                   (nameVa - reinterpret_cast<uint64_t>(img.base)) < img.sizeOfImage) {
            nameRva = nameVa - reinterpret_cast<uint64_t>(img.base);
        } else {
            return false;
        }

        const size_t nameLen = std::strlen(spec.name);
        if (nameRva + nameLen + 1u > img.sizeOfImage) return false;
        if (std::memcmp(img.base + nameRva, spec.name, nameLen + 1u) != 0) return false;

        const float tol = std::max(1.0e-3f, std::fabs(spec.exeDefault) * 1.0e-4f);
        if (!std::isfinite(val) || std::fabs(val - spec.exeDefault) > tol) return false;

        ++verified;
    }

    if (outVerifiedCount) *outVerifiedCount = verified;
    return verified == kPhysicsCameraSpecCount;
}

RagdollImpulseResult EvaluateRagdollDeathImpulse(const RagdollImpulseInput& in) {
    RagdollImpulseResult out{};
    const float massKg   = std::isfinite(in.projectileMassKg) ? std::max(in.projectileMassKg, 0.0f) : 0.0f;
    const float speedMps = std::isfinite(in.impactSpeedMps)   ? std::max(in.impactSpeedMps,   0.0f) : 0.0f;
    const float dmg      = std::isfinite(in.damageAmount)     ? std::max(in.damageAmount,     0.0f) : 0.0f;
    const float bodyKg   = std::isfinite(in.targetMassKg)     ? std::clamp(in.targetMassKg, 20.0f, 500.0f) : 75.0f;

    const float p = massKg * speedMps;
    out.rawMomentumNs = p;

    // Grounded death-impulse ceilings match our binary-verified fDeathForceRangedForceMax (22) & fDeathForceForceMax (48):
    const float ceilingN = in.isRanged ? 22.0f : 48.0f;
    // Combine true physical momentum transfer with modest damage-weighted knockback:
    const float dmgImpulse = std::min(dmg * (in.isRanged ? 0.08f : 0.22f), ceilingN * 0.45f);
    const float rawImpulse = p + dmgImpulse;

    // Smooth hyperbolic-tangent kinetic cap: linear at normal energies, strictly < ceilingN even under 10,000-damage hits!
    const float capped = ceilingN * std::tanh(rawImpulse / std::max(ceilingN, 1.0e-4f));
    out.cappedImpulseN    = std::isfinite(capped) ? std::clamp(capped, 0.0f, ceilingN) : 0.0f;
    out.launchVelocityMps = out.cappedImpulseN / bodyKg;
    out.wasClamped        = (rawImpulse > ceilingN * 0.65f);
    return out;
}

BlockMitigationResult EvaluateBlockMitigation(const BlockMitigationInput& in) {
    BlockMitigationResult out{};
    const float skill  = std::isfinite(in.blockSkill0_100) ? std::clamp(in.blockSkill0_100, 0.0f, 100.0f) : 15.0f;
    const float armor  = std::isfinite(in.shieldArmorRating) ? std::clamp(in.shieldArmorRating, 0.0f, 200.0f) : 0.0f;
    const float cap    = std::isfinite(in.maxCapFrac) ? std::clamp(in.maxCapFrac, 0.20f, 0.85f) : 0.85f;
    const float skill01 = skill / 100.0f;

    float baseMit = 0.0f;
    switch (in.gearType) {
        case BlockGearType::BareHand:
            baseMit = 0.14f + 0.16f * skill01;                          // 14% .. 30%
            break;
        case BlockGearType::Weapon:
            baseMit = 0.32f + 0.26f * skill01;                          // 32% .. 58% (matches fBlockWeaponBase=0.32)
            break;
        case BlockGearType::Shield:
            baseMit = 0.46f + 0.24f * skill01 + 0.0010f * armor;        // 46% .. 85%
            break;
    }

    if (in.isTimedParry) {
        baseMit += 0.15f;
    }
    if (in.isPowerAttack) {
        baseMit *= 0.68f;  // matches verified fBlockPowerAttackMult=0.68
    }

    out.mitigationFrac  = std::clamp(baseMit, 0.0f, cap);
    out.damageTakenMult = std::clamp(1.0f - out.mitigationFrac, 1.0f - cap, 1.0f);
    out.staminaCostMult = (in.isTimedParry ? 0.55f : 1.0f) * (in.isPowerAttack ? 1.45f : 1.0f);
    return out;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedPhysCamEntry {
    float* ptr        = nullptr;
    float  savedValue = 0.0f;
};

std::mutex                     g_pcGmstMu;
std::vector<SavedPhysCamEntry> g_savedPc;
bool                           g_pcApplied = false;

static bool SehReadPcFloat(const float* p, float* out) noexcept {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWritePcFloat(float* p, float v) noexcept {
    if (!p || !std::isfinite(v)) return false;
    __try {
        *p = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitPhysicsCameraGmst(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[PhysicsCameraGMST] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[PhysicsCameraGMST] skipped (non-CE1 or online profile)";
    }

    std::lock_guard<std::mutex> lk(g_pcGmstMu);
    if (g_pcApplied) {
        return "[PhysicsCameraGMST] already active";
    }

    size_t applied     = 0u;
    size_t skippedDead = 0u;
    g_savedPc.clear();

    for (size_t i = 0; i < kPhysicsCameraSpecCount; ++i) {
        const auto& spec = kPhysicsCameraSpecs[i];
        if (!spec.hasLiveReader) {
            ++skippedDead;
            continue;
        }
        uint32_t rva = 0u;
        for (const auto& r : refs) {
            if (r.setting && r.name == spec.name) {
                rva = r.rva;
                break;
            }
        }
        if (rva == 0u) continue;
        if (TableVersion() == "1.7.104.0" && rva != spec.expectedRva) continue;

        auto* field = reinterpret_cast<float*>(const_cast<uint8_t*>(img.base) + rva + 8u);
        float orig = 0.0f;
        if (!SehReadPcFloat(field, &orig)) continue;
        if (SehWritePcFloat(field, spec.balancedValue)) {
            g_savedPc.push_back({field, orig});
            ++applied;
        }
    }
    g_pcApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_pcStateMu);
        g_pcState.gmstsVerified       = 1u;
        g_pcState.gmstsAppliedCount   = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
        g_pcState.deadLegacySkipped   = static_cast<uint8_t>(std::min<size_t>(skippedDead, 255u));
        g_pcState.physicsCameraActive = g_pcApplied ? 1u : 0u;
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[PhysicsCameraGMST] applied %zu binary-verified ragdoll/block/bash/regen/camera GMSTs (%zu dead 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownPhysicsCameraGmst() {
    std::lock_guard<std::mutex> lk(g_pcGmstMu);
    if (!g_pcApplied) return;
    for (const auto& e : g_savedPc) {
        SehWritePcFloat(e.ptr, e.savedValue);
    }
    g_savedPc.clear();
    g_pcApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_pcStateMu);
        g_pcState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetPhysicsCameraState(raw::rt::RawPhysicsCameraState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_pcStateMu);
    *out = raw::rt::g_pcState;
    return 1;
}
