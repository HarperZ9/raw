// rt_magic_physics.cpp: Wave 47 Binary-Verified 1.7.104.0 Elemental Magic Physics, Dual-Cast Scaling
// & Weather/Water Synergy + Aerodynamic Spell Ballistics.
// Original work, MIT.
#include "rt_magic_physics.h"
#include "rt_image.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_callgraph.h"
#include "rt_live_config.h"
#include "rt_version.h"
#include <windows.h>
#endif

namespace raw::rt {
namespace {

// Every single GMST RVA, on-disk default float, and .text reader verified directly against SkyrimSE.exe 1.7.104.0:
//   - Dual-Cast Scaling (0x422180..0x4221D4):
//       fMagicDualCastingCostMult          (0x20ABBE0, def=1.50 [ESM 2.8], reader 0x4221C0) -> 2.15
//       fMagicDualCastingTimeMult          (0x20ABB80, def=1.50 [ESM 1.0], reader 0x422180) -> 1.00
//       fMagicDualCastingEffectivenessBase (0x20ABB98, def=2.00 [ESM 2.2], reader 0x4221AC) -> 2.35
//       fMagicDualCastingEffectivenessMult (0x20ABBB0, def=0.00,           reader 0x4221A0) -> 0.00
//   - Spell Range & Caster Mastery (0x153608..0x153656, 0x421DF0..0x421DF8, 0x4220C1, 0x8B41DF):
//       fMagicBallMaximumDistance            (0x2076B50, def=2000.0, reader 0x153608) -> 3600.0
//       fMagicBoltMaximumDistance            (0x2076B80, def=2000.0, reader 0x153656) -> 4200.0
//       fMagicSprayMaximumDistance           (0x2076BB0, def=1000.0, reader 0x15363C) -> 1250.0
//       fMagicCasterPCSkillCostBase          (0x20AAD10, def=0.005,  reader 0x421DF0) -> 0.005
//       fMagicPCSkillCostScale               (0x20AB808, def=0.50,   reader 0x421DF8) -> 0.55
//       fMagicWardPowerMaxBase               (0x20ABB38, def=0.50,   reader 0x4220C1) -> 0.65
//       fCombatMagicConcentrationAimVariance (0x20A2B10, def=0.00,   reader 0x8B41DF) -> 0.15
//   - Magicka Regen, Casting Tempo & Telekinesis Physics (0x700209, 0x7B419E, 0x752FA4):
//       fMagickaRegenDelayMax                (0x20A12F8, def=5.00,   reader 0x700209) -> 3.20
//       fMagicPostDrawCastDelay              (0x20AB9A0, def=1.50,   reader 0x7B419E) -> 0.95
//       fMagicTelekinesisComplexMaxForce     (0x20AB2E0, def=1000.0, reader 0x752FA4) -> 1450.0
//   - Legacy 0-Reader GMST (verified on disk, cleanly skipped!):
//       fMagicProjectileMaxDistance          (0x20AAE60, def=10000.0, 0 .text readers)
constexpr MagicBalanceGmstSpec kMagicSpecs[] = {
    {"fMagicDualCastingCostMult",            0x20ABBE0u, 1.50f,    2.15f,   true,  "DualCast"},
    {"fMagicDualCastingTimeMult",            0x20ABB80u, 1.50f,    1.00f,   true,  "DualCast"},
    {"fMagicDualCastingEffectivenessBase",   0x20ABB98u, 2.00f,    2.35f,   true,  "DualCast"},
    {"fMagicDualCastingEffectivenessMult",   0x20ABBB0u, 0.00f,    0.00f,   true,  "DualCast"},
    {"fMagicBallMaximumDistance",            0x2076B50u, 2000.0f,  3600.0f, true,  "Range"},
    {"fMagicBoltMaximumDistance",            0x2076B80u, 2000.0f,  4200.0f, true,  "Range"},
    {"fMagicSprayMaximumDistance",           0x2076BB0u, 1000.0f,  1250.0f, true,  "Range"},
    {"fMagicCasterPCSkillCostBase",          0x20AAD10u, 0.005f,   0.005f,  true,  "DualCast"},
    {"fMagicPCSkillCostScale",               0x20AB808u, 0.50f,    0.55f,   true,  "DualCast"},
    {"fMagicWardPowerMaxBase",               0x20ABB38u, 0.50f,    0.65f,   true,  "Physics"},
    {"fCombatMagicConcentrationAimVariance", 0x20A2B10u, 0.00f,    0.15f,   true,  "Physics"},
    {"fMagickaRegenDelayMax",                0x20A12F8u, 5.00f,    3.20f,   true,  "Regen"},
    {"fMagicPostDrawCastDelay",              0x20AB9A0u, 1.50f,    0.95f,   true,  "Regen"},
    {"fMagicTelekinesisComplexMaxForce",     0x20AB2E0u, 1000.0f,  1450.0f, true,  "Physics"},
    {"fMagicProjectileMaxDistance",          0x20AAE60u, 10000.0f, 10000.0f,false, "Range"},
};

constexpr size_t kMagicSpecCount = sizeof(kMagicSpecs) / sizeof(kMagicSpecs[0]);

std::mutex           g_magicStateMu;
RawMagicPhysicsState g_magicState{};

}  // namespace

const MagicBalanceGmstSpec* MagicBalanceTable(size_t* outCount) {
    if (outCount) *outCount = kMagicSpecCount;
    return kMagicSpecs;
}

bool VerifyMagicBalanceGmstTable(const Image& img, uint32_t* outVerifiedCount) {
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
    for (size_t i = 0; i < kMagicSpecCount; ++i) {
        const auto& spec = kMagicSpecs[i];
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

        const float tol = std::max(1.0e-4f, std::fabs(spec.exeDefault) * 1.0e-4f);
        if (!std::isfinite(val) || std::fabs(val - spec.exeDefault) > tol) return false;

        ++verified;
    }

    if (outVerifiedCount) *outVerifiedCount = verified;
    return verified == kMagicSpecCount;
}

ElementalSynergyState EvaluateElementalSynergy(const MagicEnvInput& in) {
    ElementalSynergyState out{};
    const float tempC  = std::isfinite(in.surfaceTempC)    ? std::clamp(in.surfaceTempC, -40.0f, 55.0f) : 10.0f;
    const float rain   = std::isfinite(in.rainIntensity01) ? std::clamp(in.rainIntensity01, 0.0f, 1.0f) : 0.0f;
    const float snow   = std::isfinite(in.snowIntensity01) ? std::clamp(in.snowIntensity01, 0.0f, 1.0f) : 0.0f;
    const float wetRaw = std::isfinite(in.wetness01)       ? std::clamp(in.wetness01, 0.0f, 1.0f)       : 0.0f;
    const float storm  = std::isfinite(in.thunderstorm01)  ? std::clamp(in.thunderstorm01, 0.0f, 1.0f)  : 0.0f;
    const float windB  = std::isfinite(in.windBeaufort)    ? std::clamp(in.windBeaufort, 0.0f, 12.0f)   : 2.0f;

    // Effective moisture combines submersion, wetness, and active rainfall:
    const float effWet = in.submerged ? 1.0f : std::clamp(std::max(wetRaw, rain * 0.88f), 0.0f, 1.0f);
    const float dry01  = 1.0f - effWet;
    const float subZero01 = std::clamp((-tempC) / 20.0f, 0.0f, 1.0f);
    const float aridWarm01 = std::clamp((tempC - 18.0f) / 22.0f, 0.0f, 1.0f);
    const float wind01 = std::clamp((windB - 2.0f) / 8.0f, 0.0f, 1.0f);

    // 1. Fire Synergy:
    //    - Dry arid heat or crisp dry sub-zero oxygen-dense air boosts combustion up to +18% (1.18x)
    //    - Heavy rain / wetness / submersion damps fire cleanly down to -22% (0.78x) and triggers steam burst!
    const float dryCombustBoost = dry01 * (0.06f + 0.10f * std::max(aridWarm01, subZero01) + 0.02f * wind01);
    const float moistureDamp    = effWet * 0.22f + (in.submerged ? 0.04f : 0.0f);
    out.fireDamageMult  = std::clamp(1.0f + dryCombustBoost - moistureDamp, 0.78f, 1.18f);
    out.fireRadiusMult  = std::clamp(1.0f + dry01 * wind01 * 0.10f - effWet * 0.15f, 0.85f, 1.12f);
    out.snowMeltImpulse = std::clamp((snow * 0.70f + subZero01 * 0.45f) * (1.0f - (in.submerged ? 0.6f : 0.0f)), 0.0f, 1.0f);
    out.steamBurstFlag  = (effWet >= 0.40f || in.submerged) ? 1u : 0u;

    // 2. Frost Synergy:
    //    - Wet / rain-soaked / submerged targets suffer flash-freeze brittle amplification (+35% dmg, +25% stagger!)
    //    - Sub-zero blizzards boost frost reach (+12%) and add chilly damage bonus (+8%)
    const float blizzard01 = std::clamp(snow * (0.55f + 0.45f * subZero01), 0.0f, 1.0f);
    out.frostDamageMult  = std::clamp(1.0f + effWet * 0.35f + blizzard01 * 0.10f, 1.0f, 1.45f);
    out.frostStaggerMult = std::clamp(1.0f + effWet * 0.25f, 1.0f, 1.25f);
    out.frostReachMult   = std::clamp(1.0f + blizzard01 * 0.12f, 1.0f, 1.12f);

    // 3. Shock Synergy:
    //    - Wet / rain / submerged targets trigger conductive water-arc amplification (+40% dmg, +35% arc range!)
    //    - Active thunderstorms supercharge shock (+15% dmg, +10% arc range)
    out.shockDamageMult     = std::clamp(1.0f + effWet * 0.40f + storm * 0.15f, 1.0f, 1.55f);
    out.shockArcRangeMult   = std::clamp(1.0f + effWet * 0.35f + storm * 0.10f, 1.0f, 1.45f);
    out.waterArcConductFlag = (effWet >= 0.35f || in.submerged) ? 1u : 0u;
    return out;
}

SpellBallisticsResult EvaluateSpellBallistics(SpellProjectileKind kind,
                                              float flightTimeSec,
                                              const float launchDir[3],
                                              const float windVecXY[2]) {
    SpellBallisticsResult out{};
    const float t = std::isfinite(flightTimeSec) ? std::clamp(flightTimeSec, 0.0f, 10.0f) : 0.0f;

    float dx = 0.0f, dy = 1.0f, dz = 0.0f;
    if (launchDir) {
        const float lx = std::isfinite(launchDir[0]) ? launchDir[0] : 0.0f;
        const float ly = std::isfinite(launchDir[1]) ? launchDir[1] : 1.0f;
        const float lz = std::isfinite(launchDir[2]) ? launchDir[2] : 0.0f;
        const float len = std::sqrt(lx * lx + ly * ly + lz * lz);
        if (len > 1.0e-4f) { dx = lx / len; dy = ly / len; dz = lz / len; }
    }
    const float wx = (windVecXY && std::isfinite(windVecXY[0])) ? std::clamp(windVecXY[0], -1200.0f, 1200.0f) : 0.0f;
    const float wy = (windVecXY && std::isfinite(windVecXY[1])) ? std::clamp(windVecXY[1], -1200.0f, 1200.0f) : 0.0f;

    float speed = 3600.0f;
    float gravU = 160.0f;
    float windCoeff = 0.35f;

    if (kind == SpellProjectileKind::IceSpike) {
        speed     = 3200.0f;
        gravU     = 290.0f;  // denser ice mass drops slightly more, resists crosswind better
        windCoeff = 0.14f;
    } else if (kind == SpellProjectileKind::LightningBolt) {
        speed     = 28000.0f;  // near-instant electrical arc
        gravU     = 0.0f;      // strictly zero gravitational drop
        windCoeff = 0.0f;      // strictly zero crosswind deflection
    }

    out.speedUnitsPerSec = speed;
    out.gravityDropU     = 0.5f * gravU * t * t;
    out.windDriftXY[0]   = 0.5f * wx * windCoeff * t * t;
    out.windDriftXY[1]   = 0.5f * wy * windCoeff * t * t;

    out.displacement[0]  = dx * speed * t + out.windDriftXY[0];
    out.displacement[1]  = dy * speed * t + out.windDriftXY[1];
    out.displacement[2]  = dz * speed * t - out.gravityDropU;
    return out;
}

void MagicPhysicsUpdateEnvironment(const MagicEnvInput& env) {
    const ElementalSynergyState syn = EvaluateElementalSynergy(env);
    std::lock_guard<std::mutex> lk(g_magicStateMu);
    g_magicState.synergy        = syn;
    g_magicState.steamBurstFlag = syn.steamBurstFlag;
    g_magicState.waterArcFlag   = syn.waterArcConductFlag;
}

#ifndef RAW_SIM_PURE_TEST
namespace {

struct SavedMagicEntry {
    float* ptr = nullptr;
    float  savedValue = 0.0f;
};

std::mutex g_magicGmstMu;
std::vector<SavedMagicEntry> g_savedMagic;
bool g_magicApplied = false;

static bool SehReadMagicFloat(const float* p, float* out) noexcept {
    if (!p || !out) return false;
    __try {
        *out = *p;
        return std::isfinite(*out);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteMagicFloat(float* p, float v) noexcept {
    if (!p || !std::isfinite(v)) return false;
    __try {
        *p = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string InitMagicPhysics(const Image& img, const std::vector<RefTarget>& refs) {
    if (!AreBinaryOffsetsVerified()) {
        return "[MagicPhysics] REFUSED (fail-closed: startup Zydis binary verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[MagicPhysics] skipped (non-CE1 or online profile)";
    }

    // Initialize neutral baseline synergy state
    MagicPhysicsUpdateEnvironment(MagicEnvInput{});

    if (!GetLiveConfig().nonSpongyDifficulty) {
        return "[MagicPhysics] GMST overrides skipped (NonSpongyDifficulty=0 in raw_engine.ini)";
    }

    std::lock_guard<std::mutex> lk(g_magicGmstMu);
    if (g_magicApplied) {
        return "[MagicPhysics] already active";
    }

    size_t applied = 0u;
    size_t skippedDead = 0u;
    g_savedMagic.clear();

    for (size_t i = 0; i < kMagicSpecCount; ++i) {
        const auto& spec = kMagicSpecs[i];
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
        if (!SehReadMagicFloat(field, &orig)) continue;
        if (SehWriteMagicFloat(field, spec.balancedValue)) {
            g_savedMagic.push_back({field, orig});
            ++applied;
        }
    }
    g_magicApplied = (applied > 0u);

    {
        std::lock_guard<std::mutex> slk(g_magicStateMu);
        g_magicState.gmstsVerified     = 1u;
        g_magicState.gmstsAppliedCount = static_cast<uint8_t>(std::min<size_t>(applied, 255u));
    }

    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[MagicPhysics] applied %zu binary-verified dual-cast/range/regen/telekinesis GMSTs (%zu dead 0-reader skipped)",
                  applied, skippedDead);
    return buf;
}

void ShutdownMagicPhysics() {
    std::lock_guard<std::mutex> lk(g_magicGmstMu);
    if (!g_magicApplied) return;
    for (const auto& e : g_savedMagic) {
        SehWriteMagicFloat(e.ptr, e.savedValue);
    }
    g_savedMagic.clear();
    g_magicApplied = false;
    {
        std::lock_guard<std::mutex> slk(g_magicStateMu);
        g_magicState.gmstsAppliedCount = 0u;
    }
}
#endif

}  // namespace raw::rt

extern "C" __declspec(dllexport) int RAW_GetMagicPhysicsState(raw::rt::RawMagicPhysicsState* out) {
    if (!out) return 0;
    std::lock_guard<std::mutex> lk(raw::rt::g_magicStateMu);
    *out = raw::rt::g_magicState;
    return 1;
}
