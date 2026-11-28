// rt_movement_combat.cpp: Wave 43 Live Creation Engine 1.7.104.0 Tactile Directional Dodge-Step
// with 260ms i-Frame Evasion + Smooth Critically-Damped Combat/Sprint Camera FOV Breath.
// Original work, MIT.
#include "rt_movement_combat.h"
#include "rt_image.h"
#include "rt_live_config.h"

#include <Zydis/Zydis.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

#ifndef RAW_SIM_PURE_TEST
#include "rt_version.h"
#include "rt_view.h"
#include <windows.h>
#endif

namespace raw::rt {

// =================================================================================================
// 1. Pure Deterministic Directional Dodge-Step & 260ms Wall-Clock i-Frame Controller
// =================================================================================================
bool IsDodgeEvadingAt(const DodgeControllerState& st, uint64_t nowMs) {
    return (st.iFrameEndMs > st.dodgeStartMs) && (nowMs >= st.dodgeStartMs) && (nowMs < st.iFrameEndMs);
}

bool IsDodgeEvasionFeelActive(const DodgeControllerState& st, uint64_t nowMs) {
    return (st.evasionFeelEndMs > 0ull) && (nowMs < st.evasionFeelEndMs);
}

DodgeTriggerResult StepDodgeController(DodgeControllerState& st, const DodgeTriggerInput& in) {
    DodgeTriggerResult res{};
    const LiveEngineConfig cfg = GetLiveConfig();

    if (in.menuPaused || !cfg.dodgeIFramesEnabled) {
        st.evadingNow = false;
        return res;
    }

    st.evadingNow = IsDodgeEvadingAt(st, in.nowMs);

    // Detect either crisp Alt rising edge OR quick WASD double-tap within kDodgeDoubleTapWindowMs (260 ms):
    bool wantDodge = false;
    DodgeDirection dir = DodgeDirection::None;

    if (in.altEdge) {
        wantDodge = true;
        if (in.wasdHeld[2])      dir = DodgeDirection::Left;
        else if (in.wasdHeld[3]) dir = DodgeDirection::Right;
        else if (in.wasdHeld[1]) dir = DodgeDirection::Back;
        else if (in.wasdHeld[0]) dir = DodgeDirection::Forward;
        else                     dir = DodgeDirection::Back;
    }

    static constexpr DodgeDirection kWasdDir[4] = {
        DodgeDirection::Forward, DodgeDirection::Back, DodgeDirection::Left, DodgeDirection::Right
    };
    for (int k = 0; k < 4; ++k) {
        if (!in.wasdEdge[k]) continue;
        const uint64_t prev = st.lastWasdTapMs[k];
        if (!wantDodge && prev > 0ull && in.nowMs >= prev && (in.nowMs - prev) <= kDodgeDoubleTapWindowMs) {
            wantDodge = true;
            dir = kWasdDir[k];
            st.lastWasdTapMs[k] = 0ull;  // consume double-tap pair cleanly
        } else {
            st.lastWasdTapMs[k] = in.nowMs;
        }
    }

    if (!wantDodge) {
        return res;
    }

    if (in.nowMs < st.cooldownEndMs) {
        res.rejectedCooldown = true;
        ++st.dodgesRejectedCooldown;
        return res;
    }

    const float stamCost = cfg.dodgeStaminaCost;
    const float stam = std::isfinite(in.currentStamina) ? in.currentStamina : 0.0f;
    if (stam < stamCost) {
        res.rejectedStamina = true;
        ++st.dodgesRejectedStamina;
        return res;
    }

    // Engage configured invulnerability window (default 260 ms) & 550 ms cooldown:
    const uint64_t iFrameMs = static_cast<uint64_t>(std::lround(cfg.dodgeIFrameMs));
    st.dodgeStartMs  = in.nowMs;
    st.iFrameEndMs   = in.nowMs + iFrameMs;
    st.cooldownEndMs = in.nowMs + kDodgeCooldownMs;
    st.activeDir     = dir;
    st.evadingNow    = true;
    ++st.dodgesTriggered;

    res.triggered   = true;
    res.staminaCost = stamCost;
    res.direction   = dir;
    return res;
}

bool TryEvadeIncomingHit(DodgeControllerState& st, uint64_t nowMs,
                         float* inOutTotalDamage, float* inOutPhysicalDamage, float* inOutPercentBlocked) {
    if (!IsDodgeEvadingAt(st, nowMs)) {
        return false;
    }
    if (inOutTotalDamage)    *inOutTotalDamage    = 0.0f;
    if (inOutPhysicalDamage) *inOutPhysicalDamage = 0.0f;
    if (inOutPercentBlocked) *inOutPercentBlocked = 1.0f;
    st.evasionFeelEndMs = nowMs + kDodgeEvasionFeelMs;
    ++st.evadedHits;
    return true;
}

// =================================================================================================
// 2. Pure Deterministic Critically-Damped Combat / Sprint Camera FOV Breath Spring
// =================================================================================================
FovStepOutput StepFovBreathSpring(FovSpringState& st, const FovStepInput& in) {
    FovStepOutput out{};
    const bool validRead = std::isfinite(in.liveReadFovDeg) &&
                           in.liveReadFovDeg >= 40.0f && in.liveReadFovDeg <= 135.0f;
    const float clampedRead = validRead ? in.liveReadFovDeg : st.baselineFovDeg;

    if (!st.initialized) {
        st.baselineFovDeg    = clampedRead;
        st.lastWrittenFovDeg = clampedRead;
        st.currentOffsetDeg  = 0.0f;
        st.velocityDegPerSec = 0.0f;
        st.initialized       = true;
        out.baselineUpdated  = true;
    } else if (validRead) {
        // Detect external FOV changes (console `fov 90`, INI, cutscene, scope zoom) by comparing
        // live engine worldFOV against the exact value we last wrote — NEVER clobber user FOV!
        const float expected = st.hasWrittenLive ? st.lastWrittenFovDeg : st.baselineFovDeg;
        if (std::fabs(clampedRead - expected) > 0.25f) {
            st.baselineFovDeg    = std::clamp(clampedRead, 40.0f, 130.0f);
            st.lastWrittenFovDeg = st.baselineFovDeg;
            st.currentOffsetDeg  = 0.0f;
            st.velocityDegPerSec = 0.0f;
            st.hasWrittenLive    = false;
            ++st.baselineResyncCount;
            out.baselineUpdated  = true;
        }
    }

    const LiveEngineConfig cfg = GetLiveConfig();
    if (in.menuPaused || !cfg.fovBreathEnabled || cfg.fovBreathScale <= 0.001f) {
        const bool wasOffset = st.hasWrittenLive && (std::fabs(st.lastWrittenFovDeg - st.baselineFovDeg) > 1.0e-3f);
        st.currentOffsetDeg  = 0.0f;
        st.velocityDegPerSec = 0.0f;
        st.lastWrittenFovDeg = st.baselineFovDeg;
        st.hasWrittenLive    = false;
        out.targetOffsetDeg    = 0.0f;
        out.appliedOffsetDeg   = 0.0f;
        out.desiredWorldFovDeg = st.baselineFovDeg;
        out.needWrite          = wasOffset;
        return out;
    }

    float target = 0.0f;
    if (in.sprinting) {
        target = kFovSprintOffsetDeg;
    } else if (in.dodging || in.inCombatOrWeaponDrawn) {
        target = kFovDodgeCombatOffsetDeg;
    }
    if (in.evasionFeelActive) {
        target = std::min(target + kFovEvasionPulseDeg, 5.2f);
    }
    target = std::clamp(target * cfg.fovBreathScale, 0.0f, 12.5f);

    // Exact closed-form critically-damped harmonic spring (zeta = 1.0, unconditionally stable & zero overshoot):
    const float dt = std::clamp(std::isfinite(in.dtSec) ? in.dtSec : (1.0f / 60.0f), 1.0e-4f, 0.05f);
    const float w  = kFovSpringOmega;
    const float x0 = st.currentOffsetDeg - target;
    const float v0 = st.velocityDegPerSec;
    const float e  = std::exp(-w * dt);
    const float c  = v0 + w * x0;
    const float x1 = (x0 + c * dt) * e;
    const float v1 = (v0 - w * c * dt) * e;

    st.currentOffsetDeg  = std::clamp(target + x1, -0.5f, 13.0f);
    st.velocityDegPerSec = v1;
    if (std::fabs(st.currentOffsetDeg - target) < 0.005f && std::fabs(st.velocityDegPerSec) < 0.02f) {
        st.currentOffsetDeg  = target;
        st.velocityDegPerSec = 0.0f;
    }

    const float desired = std::clamp(st.baselineFovDeg + st.currentOffsetDeg, 40.0f, 135.0f);
    const bool needWrite = (std::fabs(st.currentOffsetDeg) > 1.0e-3f) ||
                           (st.hasWrittenLive && std::fabs(desired - st.lastWrittenFovDeg) > 1.0e-3f);
    if (needWrite) {
        st.lastWrittenFovDeg = desired;
        st.hasWrittenLive    = (std::fabs(st.currentOffsetDeg) > 1.0e-3f);
    }

    out.targetOffsetDeg    = target;
    out.appliedOffsetDeg   = st.currentOffsetDeg;
    out.desiredWorldFovDeg = desired;
    out.needWrite          = needWrite;
    return out;
}

// =================================================================================================
// 3. Zydis Binary Verification of PlayerCamera::Ctor (0x8F79F0 -> Singleton, +0x13C, +0x140)
// =================================================================================================
bool VerifyPlayerCameraFovCodeSite(const Image& img, uint32_t fnCtorRva,
                                   uint32_t expectedSingletonRva,
                                   uint32_t expectedWorldFovOff,
                                   uint32_t expectedFirstPersonFovOff) {
    if (!img.base || fnCtorRva == 0u || expectedSingletonRva == 0u ||
        expectedWorldFovOff == 0u || expectedFirstPersonFovOff == 0u) {
        return false;
    }
    if (fnCtorRva < img.textBegin || fnCtorRva >= img.textEnd) {
        return false;
    }

    ZydisDecoder dec;
    if (!ZYAN_SUCCESS(ZydisDecoderInit(&dec, ZYDIS_MACHINE_MODE_LONG_64, ZYDIS_STACK_WIDTH_64))) {
        return false;
    }

    bool sawSingletonStore = false;
    bool sawWorldFovStore  = false;
    bool sawFirstFovStore  = false;

    constexpr uint32_t kCtorDecodeWindowBytes = 0x160u;
    static_assert(kCtorDecodeWindowBytes >= 256u, "PlayerCamera ctor Zydis decode window guard");
    uint32_t cur = fnCtorRva;
    const uint32_t maxEnd = std::min(img.textEnd, fnCtorRva + kCtorDecodeWindowBytes);
    for (uint32_t insnCount = 0; insnCount < 96u && cur < maxEnd; ++insnCount) {
        ZydisDecodedInstruction insn;
        ZydisDecodedOperand ops[ZYDIS_MAX_OPERAND_COUNT];
        if (!ZYAN_SUCCESS(ZydisDecoderDecodeFull(&dec, img.base + cur, maxEnd - cur, &insn, ops))) {
            break;
        }
        if (insn.mnemonic == ZYDIS_MNEMONIC_INT3) break;

        for (uint8_t i = 0; i < insn.operand_count_visible; ++i) {
            const auto& op = ops[i];
            if (op.type != ZYDIS_OPERAND_TYPE_MEMORY) continue;

            // Check RIP-relative store to PlayerCamera::Singleton (e.g. mov [rip+disp], rax)
            if (op.mem.base == ZYDIS_REGISTER_RIP && op.mem.disp.has_displacement && i == 0u) {
                const int64_t targetRva = static_cast<int64_t>(cur) + static_cast<int64_t>(insn.length) + op.mem.disp.value;
                if (targetRva == static_cast<int64_t>(expectedSingletonRva)) {
                    sawSingletonStore = true;
                }
            }
            // Check 32-bit float MOVSS store to [reg + worldFOV] and [reg + firstPersonFOV]
            if (insn.mnemonic == ZYDIS_MNEMONIC_MOVSS && i == 0u &&
                op.mem.base != ZYDIS_REGISTER_RIP && op.mem.base != ZYDIS_REGISTER_RSP &&
                op.mem.disp.has_displacement && op.size == 32u) {
                if (op.mem.disp.value == static_cast<int64_t>(expectedWorldFovOff)) {
                    sawWorldFovStore = true;
                } else if (op.mem.disp.value == static_cast<int64_t>(expectedFirstPersonFovOff)) {
                    sawFirstFovStore = true;
                }
            }
        }
        cur += insn.length;
        if (insn.mnemonic == ZYDIS_MNEMONIC_RET) break;
    }
    return sawSingletonStore && sawWorldFovStore && sawFirstFovStore;
}

#ifndef RAW_SIM_PURE_TEST
// =================================================================================================
// 4. Live SkyrimSE 1.7.104.0 Runtime (SEH-Guarded, Fail-Closed, Zero-SKSE)
// =================================================================================================
namespace {

using GetActorValueFn  = float (*)(void* avOwner, int32_t av);
using RestoreAvThunkFn = void (*)(void* avOwner, int32_t mod, int32_t av, float val);

constexpr uint32_t  kPlayerCameraWorldFovOffset = 0x13Cu;
constexpr uintptr_t kActorValueOwnerOffset      = 0xB8u;
constexpr uintptr_t kActorState1Offset          = 0xC8u;
constexpr uintptr_t kActorState2Offset          = 0xCCu;
static_assert(kActorState2Offset == kActorState1Offset + 4u && kActorState1Offset > kActorValueOwnerOffset,
              "SkyrimSE 1.7.104.0 ActorValueOwner/ActorState layout guard");

struct LiveMovementRuntime {
    const uint8_t*       base = nullptr;
    uint32_t             rvaPlayerCamera = 0;
    uint32_t             rvaPlayer = 0;
    uint32_t             offWorldFov = kPlayerCameraWorldFovOffset;
    GetActorValueFn      fnGetAv = nullptr;
    RestoreAvThunkFn     fnRestoreAv = nullptr;

    std::atomic<bool>    verified{false};
    std::atomic<bool>    evadingAtomic{false};
    std::atomic<bool>    sprintingAtomic{false};
    std::atomic<bool>    weaponDrawnAtomic{false};

    std::mutex           mu;
    DodgeControllerState dodge{};
    FovSpringState       fov{};
    uint64_t             lastTickUs = 0;
    bool                 prevAltDown = false;
    bool                 prevWasdDown[4] = {};
} g_mc;

static uint64_t NowMonoMs() {
    LARGE_INTEGER f{}, c{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return f.QuadPart > 0 ? uint64_t((c.QuadPart * 1000ll) / f.QuadPart) : GetTickCount64();
}

static uint64_t NowMonoUs() {
    LARGE_INTEGER f{}, c{};
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return f.QuadPart > 0 ? uint64_t((c.QuadPart * 1000000ll) / f.QuadPart) : (GetTickCount64() * 1000ull);
}

static bool IsCurrentProcessForeground() {
    HWND fg = GetForegroundWindow();
    if (!fg) return false;
    DWORD pid = 0;
    GetWindowThreadProcessId(fg, &pid);
    return pid == GetCurrentProcessId();
}

static bool SehReadPlayerMovementState(float* outStamina, bool* outSprinting, bool* outWeaponDrawn) {
    if (!g_mc.base || !g_mc.rvaPlayer) return false;
    __try {
        uint8_t* player = *reinterpret_cast<uint8_t* const*>(g_mc.base + g_mc.rvaPlayer);
        if (!player || reinterpret_cast<uintptr_t>(player) < 0x10000ull) return false;

        if (outStamina && g_mc.fnGetAv) {
            const float st = g_mc.fnGetAv(player + kActorValueOwnerOffset, 26 /*kStamina*/);
            *outStamina = std::isfinite(st) ? st : 0.0f;
        }
        // Binary-verified SkyrimSE 1.7.104.0 (0x6B5520 -> 0x6E1530):
        // ActorState::actorState1 at player+0xC8: bit 8 (0x100) == Sprinting
        // ActorState::actorState2 at player+0xCC: bits 5..7 == weaponState (1..5 = weapon drawn/combat-ready)
        const uint32_t state1 = *reinterpret_cast<const uint32_t*>(player + kActorState1Offset);
        const uint32_t state2 = *reinterpret_cast<const uint32_t*>(player + kActorState2Offset);
        if (outSprinting)   *outSprinting   = ((state1 & 0x0100u) != 0u);
        if (outWeaponDrawn) *outWeaponDrawn = (((state2 >> 5) & 7u) != 0u);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static void SehDeductPlayerStamina(float amount) {
    if (!g_mc.base || !g_mc.rvaPlayer || !g_mc.fnRestoreAv || !(amount > 0.0f)) return;
    __try {
        uint8_t* player = *reinterpret_cast<uint8_t* const*>(g_mc.base + g_mc.rvaPlayer);
        if (!player || reinterpret_cast<uintptr_t>(player) < 0x10000ull) return;
        // ActorValueOwner::RestoreActorValueThunk (0x6C51F0): (avOwner=player+0xB8, mod=2 kDamage, av=26 kStamina, val=-amount)
        g_mc.fnRestoreAv(player + kActorValueOwnerOffset, 2, 26, -amount);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
    }
}

static bool SehReadWorldFov(float* outFov) {
    if (!g_mc.base || !g_mc.rvaPlayerCamera || !outFov) return false;
    __try {
        const uint8_t* cam = *reinterpret_cast<const uint8_t* const*>(g_mc.base + g_mc.rvaPlayerCamera);
        if (!cam || reinterpret_cast<uintptr_t>(cam) < 0x10000ull) return false;
        const float v = *reinterpret_cast<const float*>(cam + g_mc.offWorldFov);
        if (!std::isfinite(v) || v < 40.0f || v > 135.0f) return false;
        *outFov = v;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

static bool SehWriteWorldFov(float newFov) {
    if (!g_mc.base || !g_mc.rvaPlayerCamera || !std::isfinite(newFov) || newFov < 40.0f || newFov > 135.0f) {
        return false;
    }
    __try {
        uint8_t* cam = *reinterpret_cast<uint8_t* const*>(g_mc.base + g_mc.rvaPlayerCamera);
        if (!cam || reinterpret_cast<uintptr_t>(cam) < 0x10000ull) return false;
        *reinterpret_cast<float*>(cam + g_mc.offWorldFov) = newFov;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

}  // namespace

std::string MovementCombatInit(const Image& img) {
    g_mc.verified.store(false, std::memory_order_release);
    g_mc.evadingAtomic.store(false, std::memory_order_release);
    if (!AreBinaryOffsetsVerified()) {
        return "[MovementCombat] REFUSED (fail-closed: startup Zydis binary offset verification did not pass)";
    }
    if (ActiveProfile().isMultiplayerOnline || ActiveProfile().gen != raw::versions::EngineGen::CE1_DX11) {
        return "[MovementCombat] skipped (non-CE1 or online profile)";
    }

    const uint32_t rvaCamCtor     = Addr("fn:PlayerCamera::Ctor");
    const uint32_t rvaCamSing     = Addr("PlayerCamera::Singleton");
    const uint32_t offWorldFov    = Off("off:PlayerCamera::worldFOV");
    const uint32_t offFirstFov    = Off("off:PlayerCamera::firstPersonFOV");
    const uint32_t rvaPlayer      = Addr("PlayerCharacter::Singleton");
    const uint32_t rvaGetAv       = Addr("fn:ActorValueOwner::GetActorValue");
    const uint32_t rvaRestoreAv   = Addr("fn:ActorValueOwner::RestoreActorValueThunk");

    const bool fovSiteOk = VerifyPlayerCameraFovCodeSite(img, rvaCamCtor, rvaCamSing, offWorldFov, offFirstFov);
    if (!fovSiteOk) {
        return "[MovementCombat] REFUSED (fail-closed: PlayerCamera::Ctor Zydis verification failed)";
    }

    {
        std::lock_guard<std::mutex> lk(g_mc.mu);
        g_mc.base            = img.base;
        g_mc.rvaPlayerCamera = rvaCamSing;
        g_mc.rvaPlayer       = rvaPlayer;
        g_mc.offWorldFov     = offWorldFov;
        g_mc.fnGetAv         = rvaGetAv ? reinterpret_cast<GetActorValueFn>(const_cast<uint8_t*>(img.base) + rvaGetAv) : nullptr;
        g_mc.fnRestoreAv     = rvaRestoreAv ? reinterpret_cast<RestoreAvThunkFn>(const_cast<uint8_t*>(img.base) + rvaRestoreAv) : nullptr;
        g_mc.dodge           = {};
        g_mc.fov             = {};
        g_mc.lastTickUs      = 0;
        g_mc.prevAltDown     = false;
        for (int i = 0; i < 4; ++i) g_mc.prevWasdDown[i] = false;
    }

    g_mc.verified.store(true, std::memory_order_release);
    char buf[224];
    std::snprintf(buf, sizeof buf,
                  "[MovementCombat] Zydis-verified ACTIVE (PlayerCamera=0x%X worldFOV=+0x%X firstFOV=+0x%X dodge=260ms/18stam fovSpring=+4.2/+2.6)",
                  rvaCamSing, offWorldFov, offFirstFov);
    return buf;
}

void MovementCombatOnTick() {
    if (!g_mc.verified.load(std::memory_order_acquire)) return;

    EngineView ev{};
    RAW_EngineView(&ev);
    const bool viewReady = ((ev.ok & 2u) != 0u);
    const bool menuPaused = (!viewReady) || (ev.menuPaused != 0u) || (ev.camStateId == 3u /*tfc free cam*/);

    float stamina = 100.0f;
    bool sprinting = false;
    bool weaponDrawn = false;
    if (!menuPaused) {
        SehReadPlayerMovementState(&stamina, &sprinting, &weaponDrawn);
    }

    float liveFov = 80.0f;
    const bool hasFov = SehReadWorldFov(&liveFov);

    const uint64_t nowMs = NowMonoMs();
    const uint64_t nowUs = NowMonoUs();
    const bool fgWindow = (!menuPaused) && IsCurrentProcessForeground();

    bool altDown = false;
    bool wasdDown[4] = {};
    if (fgWindow) {
        altDown     = (GetAsyncKeyState(VK_LMENU) & 0x8000) != 0;
        wasdDown[0] = (GetAsyncKeyState('W') & 0x8000) != 0;
        wasdDown[1] = (GetAsyncKeyState('S') & 0x8000) != 0;
        wasdDown[2] = (GetAsyncKeyState('A') & 0x8000) != 0;
        wasdDown[3] = (GetAsyncKeyState('D') & 0x8000) != 0;
    }

    DodgeTriggerResult dodgeRes{};
    FovStepOutput fovOut{};
    bool evadingNow = false;

    {
        std::lock_guard<std::mutex> lk(g_mc.mu);
        float dtSec = 1.0f / 60.0f;
        if (g_mc.lastTickUs > 0 && nowUs > g_mc.lastTickUs) {
            dtSec = std::clamp(static_cast<float>(static_cast<double>(nowUs - g_mc.lastTickUs) * 1.0e-6), 1.0e-4f, 0.05f);
        }
        g_mc.lastTickUs = nowUs;

        DodgeTriggerInput dIn{};
        dIn.nowMs          = nowMs;
        dIn.currentStamina = stamina;
        dIn.menuPaused     = menuPaused;
        dIn.altEdge        = altDown && !g_mc.prevAltDown;
        for (int k = 0; k < 4; ++k) {
            dIn.wasdEdge[k] = wasdDown[k] && !g_mc.prevWasdDown[k];
            dIn.wasdHeld[k] = wasdDown[k];
            g_mc.prevWasdDown[k] = wasdDown[k];
        }
        g_mc.prevAltDown = altDown;

        dodgeRes = StepDodgeController(g_mc.dodge, dIn);
        evadingNow = g_mc.dodge.evadingNow;

        if (hasFov) {
            FovStepInput fIn{};
            fIn.liveReadFovDeg        = liveFov;
            fIn.dtSec                 = dtSec;
            fIn.sprinting             = sprinting;
            fIn.dodging               = evadingNow;
            fIn.inCombatOrWeaponDrawn = weaponDrawn;
            fIn.evasionFeelActive     = IsDodgeEvasionFeelActive(g_mc.dodge, nowMs);
            fIn.menuPaused            = menuPaused;
            fovOut = StepFovBreathSpring(g_mc.fov, fIn);
        }
    }

    g_mc.evadingAtomic.store(evadingNow, std::memory_order_release);
    g_mc.sprintingAtomic.store(sprinting, std::memory_order_relaxed);
    g_mc.weaponDrawnAtomic.store(weaponDrawn, std::memory_order_relaxed);

    if (dodgeRes.triggered && dodgeRes.staminaCost > 0.0f) {
        SehDeductPlayerStamina(dodgeRes.staminaCost);
    }
    if (hasFov && fovOut.needWrite) {
        SehWriteWorldFov(fovOut.desiredWorldFovDeg);
    }
}

void MovementCombatShutdown() {
    const bool wasVerified = g_mc.verified.exchange(false, std::memory_order_acq_rel);
    g_mc.evadingAtomic.store(false, std::memory_order_release);
    if (!wasVerified) return;

    float restoreFov = 0.0f;
    bool needRestore = false;
    {
        std::lock_guard<std::mutex> lk(g_mc.mu);
        if (g_mc.fov.initialized && g_mc.fov.hasWrittenLive) {
            restoreFov = g_mc.fov.baselineFovDeg;
            needRestore = true;
        }
        g_mc.fov.hasWrittenLive    = false;
        g_mc.fov.currentOffsetDeg  = 0.0f;
        g_mc.fov.velocityDegPerSec = 0.0f;
        g_mc.dodge.evadingNow      = false;
    }
    if (needRestore) {
        SehWriteWorldFov(restoreFov);
    }
}

bool MovementCombatIsPlayerEvading() {
    if (!g_mc.verified.load(std::memory_order_acquire)) return false;
    if (!g_mc.evadingAtomic.load(std::memory_order_acquire)) return false;
    const uint64_t nowMs = NowMonoMs();
    std::lock_guard<std::mutex> lk(g_mc.mu);
    const bool active = IsDodgeEvadingAt(g_mc.dodge, nowMs);
    g_mc.dodge.evadingNow = active;
    if (!active) g_mc.evadingAtomic.store(false, std::memory_order_release);
    return active;
}

bool MovementCombatTryEvadePlayerHit(float* inOutTotalDmg, float* inOutPhysDmg, float* inOutPctBlocked) {
    if (!g_mc.verified.load(std::memory_order_acquire)) return false;
    const uint64_t nowMs = NowMonoMs();
    std::lock_guard<std::mutex> lk(g_mc.mu);
    return TryEvadeIncomingHit(g_mc.dodge, nowMs, inOutTotalDmg, inOutPhysDmg, inOutPctBlocked);
}

bool MovementCombatGetTelemetry(MovementCombatTelemetry* out) {
    if (!out) return false;
    std::lock_guard<std::mutex> lk(g_mc.mu);
    out->verified           = g_mc.verified.load(std::memory_order_relaxed) ? 1u : 0u;
    out->evadingNow         = g_mc.evadingAtomic.load(std::memory_order_relaxed) ? 1u : 0u;
    out->sprintingNow       = g_mc.sprintingAtomic.load(std::memory_order_relaxed) ? 1u : 0u;
    out->weaponDrawnNow     = g_mc.weaponDrawnAtomic.load(std::memory_order_relaxed) ? 1u : 0u;
    out->dodgesTriggered    = g_mc.dodge.dodgesTriggered;
    out->dodgesRejectedStam = g_mc.dodge.dodgesRejectedStamina;
    out->evadedHits         = g_mc.dodge.evadedHits;
    out->fovBaselineResyncs = g_mc.fov.baselineResyncCount;
    out->baselineFovDeg     = g_mc.fov.baselineFovDeg;
    out->appliedFovOffset   = g_mc.fov.currentOffsetDeg;
    return true;
}
#endif

}  // namespace raw::rt
