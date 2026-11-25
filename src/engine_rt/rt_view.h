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
#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

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
};

void ViewInit(const Image& img);
void ViewOnPresent();
// Wave 35: Live memory-level Creation Engine tidal water-height coupling (modulates exterior TESObjectCELL::waterHeight
// cleanly around each cell's saved baseline for live swimming/buoyancy/splash alignment, restored on shutdown).
void ViewApplyLiveTide(float tideHeightU);
void ViewRestoreAllTides();
// the last present's view as JSON fields (no braces), for live_state.jsonl and `view`
std::string ViewJson();

}  // namespace raw::rt

// For the ray-tracing and weather workers: copies the last present's view into *out and returns its present.
extern "C" __declspec(dllexport) uint64_t RAW_EngineView(raw::rt::EngineView* out);
// Wave 39 C ABI for D3D12Bridge / RawWeatherSim / RawSeasonsThermals:
// fills out6 with [year, month, day, hour, daysPassed, timeScale] and returns 1 if valid, 0 otherwise.
extern "C" __declspec(dllexport) int RAW_GetCalendarState(float* out6);
