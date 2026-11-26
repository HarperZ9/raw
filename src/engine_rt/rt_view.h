#pragma once
// The view the ray-tracing and weather work need: camera, cell, time and the screen (RAW engine runtime).
// Original work, MIT.
//
// Read each present on the render thread under structured exception handling (offsets from CommonLibSSE-NG for
// 1.6.1170, each checked in game; see docs/ENGINEMAP.md, engine control 7):
//   BSGraphics::State 0x328CC20: screenWidth +0x24, screenHeight +0x28, frameBufferViewport +0x2C (two uint32: an
//     earlier read took them as floats and saw zeros), insideFrame +0x50, letterbox +0x51.
//   PlayerCamera* at 0x30FD7F8: cameraRoot +0x20 (its world transform: rotation +0x7C, translation +0xA0),
//     worldFOV +0x13C, firstPersonFOV +0x140.
//   PlayerCharacter* at 0x31874F8: position +0x54, parentCell +0x60; the cell's form id +0x14, flags +0x40 (bit 0
//     interior), worldspace +0x128 (its form id +0x14, form type 71).
//   Sky (Sky::GetSingleton 0x01C2640): currentGameHour +0x1B0. Calendar 0x20F8960: gameDaysPassed +0x28, a
//     TESGlobal whose value sits at +0x34.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <sstream>
#include <string>

#include "rt_image.h"

namespace raw::rt {

struct CameraPose {
    float pos[3]   = {};
    float pitchRad = 0.0f;
    float rollRad  = 0.0f;
    float yawRad   = 0.0f;
};

struct CameraSweepState {
    float    dYawRadPerFrame   = 0.0f;
    float    dPitchRadPerFrame = 0.0f;
    float    dFwdUnitsPerFrame = 0.0f;
    uint32_t framesRemaining   = 0u;
    uint32_t framesExecuted    = 0u;
    uint32_t framesApplied     = 0u;
    bool     active            = false;
};

inline constexpr float kCamTwoPi       = 6.28318530717958647692f;
inline constexpr float kCamMaxPitchRad = 1.25f;
inline constexpr float kCamMaxStepRad  = 0.50f;
inline constexpr float kCamMaxStepFwd  = 256.0f;
inline constexpr float kCamMaxCoordU   = 1.0e6f;
inline constexpr float kMaxGameHour    = 23.99f;

enum class ModalDismissPhase : uint8_t { Idle = 0, SendRight = 1, WaitReturn = 2 };
enum class ModalKeyAction    : uint8_t { None = 0, PostRight = 1, PostReturn = 2 };

struct ModalDismissState {
    ModalDismissPhase phase              = ModalDismissPhase::Idle;
    uint32_t          waitFrames         = 0u;
    uint32_t          cooldownFrames     = 0u;
    uint32_t          dismissedCount     = 0u;
    bool              autoDismissEnabled = true;  // defaults ON while cam_sweep is active
};

inline bool ComputeSetGameHour(float& outHour, float inHour) noexcept {
    if (!std::isfinite(inHour)) return false;
    outHour = std::clamp(inHour, 0.0f, kMaxGameHour);
    return true;
}

inline bool ParseSetTimeArg(const std::string& a, float& outHour) {
    std::istringstream ss(a);
    double h = 0.0;
    if (!(ss >> h) || !std::isfinite(h)) return false;
    return ComputeSetGameHour(outHour, static_cast<float>(h));
}

inline bool ParseAutoDismissArg(const std::string& a, bool& outEnabled) {
    if (a == "1" || a == "true" || a == "on") { outEnabled = true; return true; }
    if (a == "0" || a == "false" || a == "off") { outEnabled = false; return true; }
    return false;
}

inline void TriggerModalDismiss(ModalDismissState& st) noexcept {
    if (st.phase == ModalDismissPhase::Idle) {
        st.phase      = ModalDismissPhase::SendRight;
        st.waitFrames = 0u;
    }
}

// Non-blocking modal dismiss state machine: posts VK_RIGHT (select "No"), waits 2 present frames,
// then posts VK_RETURN (confirm) and enters a 20-frame cooldown.
inline ModalKeyAction StepModalDismissState(ModalDismissState& st, bool isPausedOrModal,
                                            bool autoTriggerAllowed) noexcept {
    if (st.cooldownFrames > 0u) --st.cooldownFrames;
    if (st.phase == ModalDismissPhase::Idle) {
        if (autoTriggerAllowed && st.autoDismissEnabled && isPausedOrModal && st.cooldownFrames == 0u) {
            st.phase      = ModalDismissPhase::SendRight;
            st.waitFrames = 0u;
        }
    }
    if (st.phase == ModalDismissPhase::SendRight) {
        st.phase      = ModalDismissPhase::WaitReturn;
        st.waitFrames = 2u;
        return ModalKeyAction::PostRight;
    }
    if (st.phase == ModalDismissPhase::WaitReturn) {
        if (st.waitFrames > 1u) {
            --st.waitFrames;
            return ModalKeyAction::None;
        }
        st.waitFrames     = 0u;
        st.phase          = ModalDismissPhase::Idle;
        st.cooldownFrames = 20u;
        ++st.dismissedCount;
        return ModalKeyAction::PostReturn;
    }
    return ModalKeyAction::None;
}

// Pure deterministic camera/player math helpers (shared by live raw_engine.dll and ce_runtime_check).
inline float WrapYawTwoPi(float yawRad) noexcept {
    if (!std::isfinite(yawRad)) return 0.0f;
    float y = std::fmod(yawRad, kCamTwoPi);
    if (!std::isfinite(y)) return 0.0f;
    if (y < 0.0f) y += kCamTwoPi;
    if (y >= kCamTwoPi) y -= kCamTwoPi;
    return (y < 0.0f || y >= kCamTwoPi) ? 0.0f : y;
}

inline float ClampPitchRad(float pitchRad) noexcept {
    if (!std::isfinite(pitchRad)) return 0.0f;
    return std::clamp(pitchRad, -kCamMaxPitchRad, kCamMaxPitchRad);
}

inline bool ComputeCameraSweepStep(CameraPose& ioPose, float dYawRad, float dPitchRad, float dFwdUnits) noexcept {
    if (!std::isfinite(dYawRad) || !std::isfinite(dPitchRad) || !std::isfinite(dFwdUnits)) return false;
    if (!std::isfinite(ioPose.pos[0]) || !std::isfinite(ioPose.pos[1]) || !std::isfinite(ioPose.pos[2]) ||
        !std::isfinite(ioPose.pitchRad) || !std::isfinite(ioPose.rollRad) || !std::isfinite(ioPose.yawRad)) {
        return false;
    }
    const float dy = std::clamp(dYawRad, -kCamMaxStepRad, kCamMaxStepRad);
    const float dp = std::clamp(dPitchRad, -kCamMaxStepRad, kCamMaxStepRad);
    const float df = std::clamp(dFwdUnits, -kCamMaxStepFwd, kCamMaxStepFwd);

    const float nextPitch = ClampPitchRad(ioPose.pitchRad + dp);
    const float nextYaw   = WrapYawTwoPi(ioPose.yawRad + dy);
    // Skyrim Z-up heading convention: yaw=0 faces +Y (North), yaw=+pi/2 faces +X (East).
    const float nextX = ioPose.pos[0] + std::sin(nextYaw) * df;
    const float nextY = ioPose.pos[1] + std::cos(nextYaw) * df;
    if (!std::isfinite(nextX) || !std::isfinite(nextY) ||
        std::fabs(nextX) > kCamMaxCoordU || std::fabs(nextY) > kCamMaxCoordU) {
        return false;
    }
    ioPose.pitchRad = nextPitch;
    ioPose.yawRad   = nextYaw;
    ioPose.pos[0]   = nextX;
    ioPose.pos[1]   = nextY;
    return true;
}

inline bool ComputeSetPlayerPose(CameraPose& outPose, float x, float y, float z,
                                 float pitchRad, float yawRad) noexcept {
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(pitchRad) || !std::isfinite(yawRad)) {
        return false;
    }
    if (std::fabs(x) > kCamMaxCoordU || std::fabs(y) > kCamMaxCoordU || std::fabs(z) > kCamMaxCoordU) {
        return false;
    }
    outPose.pos[0]   = x;
    outPose.pos[1]   = y;
    outPose.pos[2]   = z;
    outPose.pitchRad = ClampPitchRad(pitchRad);
    outPose.rollRad  = 0.0f;
    outPose.yawRad   = WrapYawTwoPi(yawRad);
    return true;
}

inline bool StartCameraSweepState(CameraSweepState& st, float dYawRad, float dPitchRad,
                                  float dFwdUnits, int frames) noexcept {
    if (!std::isfinite(dYawRad) || !std::isfinite(dPitchRad) || !std::isfinite(dFwdUnits) || frames <= 0) {
        st = CameraSweepState{};
        return false;
    }
    st.dYawRadPerFrame   = std::clamp(dYawRad, -kCamMaxStepRad, kCamMaxStepRad);
    st.dPitchRadPerFrame = std::clamp(dPitchRad, -kCamMaxStepRad, kCamMaxStepRad);
    st.dFwdUnitsPerFrame = std::clamp(dFwdUnits, -kCamMaxStepFwd, kCamMaxStepFwd);
    st.framesRemaining   = static_cast<uint32_t>(std::clamp(frames, 1, 3600));
    st.framesExecuted    = 0u;
    st.framesApplied     = 0u;
    st.active            = true;
    return true;
}

inline void StopCameraSweepState(CameraSweepState& st) noexcept {
    st.framesRemaining = 0u;
    st.active          = false;
}

// Advances one frame of an active sweep. Returns true on the exact frame the sweep finishes.
inline bool AdvanceCameraSweepState(CameraSweepState& st, bool stepApplied) noexcept {
    if (!st.active || st.framesRemaining == 0u) return false;
    ++st.framesExecuted;
    if (stepApplied) ++st.framesApplied;
    --st.framesRemaining;
    if (st.framesRemaining == 0u) {
        st.active = false;
        return true;
    }
    return false;
}

inline bool ParseCamSweepArgs(const std::string& a, const std::string& b,
                              float& outDYaw, float& outDPitch, float& outDFwd, int& outFrames) {
    std::istringstream ss(a + " " + b);
    double dy = 0.0, dp = 0.0, df = 0.0;
    int fr = 0;
    if (!(ss >> dy >> dp >> df >> fr)) return false;
    if (!std::isfinite(dy) || !std::isfinite(dp) || !std::isfinite(df) || fr <= 0) return false;
    outDYaw   = static_cast<float>(dy);
    outDPitch = static_cast<float>(dp);
    outDFwd   = static_cast<float>(df);
    outFrames = std::clamp(fr, 1, 3600);
    return true;
}

inline bool ParseSetPoseArgs(const std::string& a, const std::string& b,
                             float& outX, float& outY, float& outZ, float& outPitch, float& outYaw) {
    std::istringstream ss(a + " " + b);
    double x = 0.0, y = 0.0, z = 0.0, p = 0.0, yw = 0.0;
    if (!(ss >> x >> y >> z >> p >> yw)) return false;
    if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(z) ||
        !std::isfinite(p) || !std::isfinite(yw)) return false;
    outX     = static_cast<float>(x);
    outY     = static_cast<float>(y);
    outZ     = static_cast<float>(z);
    outPitch = static_cast<float>(p);
    outYaw   = static_cast<float>(yw);
    return true;
}

struct EngineView {
    float camPos[3];       // world units
    float camRot[9];       // the camera root's world rotation, row major
    float fov;             // degrees, third person
    float fovFirstPerson;  // degrees
    float playerPos[3];
    float gameHour;        // 0 to 24
    float daysPassed;
    uint32_t cellFormId, worldspaceFormId;
    uint32_t interior;     // 1 in an interior cell
    uint32_t screen[2];    // the renderer's screen size
    uint32_t viewport[2];  // the frame buffer viewport
    uint32_t ok;           // bit 0 screen, 1 camera, 2 cell, 3 time
    float    waterHeight;  // TESObjectCELL::waterHeight (or TESWorldSpace::defaultWaterHeight)
    uint32_t camStateId;   // PlayerCamera::currentState->id (0=1stP, 3=Free, 9=3rdP, etc.)
    uint32_t menuPaused;   // UI::Singleton->numPausesGame > 0 (MapMenu/Inventory/Pause active)
    // Wave 39: Full binary-verified Calendar::Singleton (0x21A05D0) TESGlobal (+0x34) state:
    float    gameYear;     // Calendar+0x08 -> +0x34 (e.g. 201.0f = 4E 201)
    float    gameMonth;    // Calendar+0x10 -> +0x34 (0..11, 7 = Last Seed)
    float    gameDay;      // Calendar+0x18 -> +0x34 (1..31)
    float    timeScale;    // Calendar+0x30 -> +0x34 (default 20.0f)
    uint32_t menuStackDepth; // UI::Singleton->menuStack._size (+0x120, binary-verified 1.7.104)
};

void ViewInit(const Image& img);
void ViewOnPresent();
// Wave 35: Live memory-level Creation Engine tidal water-height coupling (modulates exterior TESObjectCELL::waterHeight
// cleanly around each cell's saved baseline for live swimming/buoyancy/splash alignment, restored on shutdown).
void ViewApplyLiveTide(float tideHeightU);
void ViewRestoreAllTides();
// the last present's view as JSON fields (no braces), for live_state.jsonl and `view`
std::string ViewJson();

// Wave 65/66: SEH-guarded headless in-process camera/player motion and game-hour setter on verified
// PlayerCharacter (+0x48 rot, +0x54 pos), PlayerCamera, Calendar (+0x20->+0x34), and Sky (+0x1B0).
bool ApplyCameraSweepStep(float dYawRad, float dPitchRad, float dFwdUnits);
bool SetPlayerPose(float x, float y, float z, float pitchRad, float yawRad);
bool SetGameHour(float hour);
bool IsMenuPausedOrModal() noexcept;
bool IsCameraSweepActive() noexcept;
void SetCameraSweepActiveFlag(bool active) noexcept;
// Wave 68: Deterministic in-process UIMessageQueue::AddMessage("MessageBoxMenu", kHide, nullptr)
// so Creation Club / Survival Mode modals dismiss cleanly without OS window focus or SendInput.
bool DismissMessageBoxMenuInProcess(uint32_t* outAddMsgRva = nullptr) noexcept;

}  // namespace raw::rt

// For the ray-tracing and weather workers: copies the last present's view into *out and returns its present.
extern "C" __declspec(dllexport) uint64_t RAW_EngineView(raw::rt::EngineView* out);
// Wave 39 C ABI for D3D12Bridge / RawWeatherSim / RawSeasonsThermals:
// fills out6 with [year, month, day, hour, daysPassed, timeScale] and returns 1 if valid, 0 otherwise.
extern "C" __declspec(dllexport) int RAW_GetCalendarState(float* out6);
