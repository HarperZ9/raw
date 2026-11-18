#pragma once
// State shared by BridgeHook.cpp (the frame) and BridgeHookCapture.cpp (files).
#include "BridgeHook.h"
#include "D3D12Bridge.h"
#include "LightingPass.h"
#include <string>

namespace SB::Proxy::hook
{
struct Config
{
    bool enabled = false, fallbackAtPresent = true, debugLayer = false, enginePass = false;
    bool realtimeAo = false;       // Pass=engine with AoMode=realtime: the engine's real-time AO
    bool lighting = false;         // Pass=lighting: the engine lights the frame ([Lighting], BridgeHookLighting.cpp)
    int  timingLogEvery = 600;
    int  retireTimeoutMs = 15000;  // engine pass: wait for the frame before (a tiny value tests the failure path)
    int  testStallAt = 0;          // test: at this gameplay bridge frame the pass waits for a value nobody signals
    int  captureFrame = 0;         // absolute frame for the backbuffer BMP (0 = never)
    int  captureAfter = 0;         // bridge frames before the gameplay capture set (0 = never)
    std::string captureDir;        // absolute; the gameplay capture set goes here
    raw::bridge::PassConstants k;
    raw::bridge::EngineTuning tune;
    raw::bridge::LightingTuning light;
    int  lightCaptureAfter = 0;    // [Lighting] CaptureAfter: world frames before the lighting capture set
    int  lightHold = 45;           // frames each step of the set holds before its picture
    bool lightDump = false;        // [Lighting] Dump=1: write the match step's inputs for tools/lighting
    bool harness = false;          // [Lighting] Harness=1: capture sets come from tools/harness (raw_harness_go.txt)
    std::string lightCaptureDir;
    std::string lightScene = "scene";
};

Config& Cfg();
void LoadConfig();   // reads the ini into Cfg() and sets g_bridgeHookActive
raw::bridge::D3D12Bridge* Bridge();

// Writes the backbuffer (8-bit RGBA or BGRA) as a 32-bit BMP: what the game
// is about to present, read from the GPU.
void CaptureBackbuffer(const std::string& path);
// The gameplay capture set: frame with the pass, the D3D12 reads of what
// crossed, the engine's visibility, then a frame with the pass switched off.
void CaptureStep(uint64_t bridgeFrames);
void InstallShareCheck();
// The game's view-to-clip matrix from its per-frame constants (vertex slot 12), as last written.
raw::bridge::CameraConstants ReadCamera(ID3D11DeviceContext* real);

// Lighting (BridgeHookLighting.cpp).
void LoadLightingConfig(const std::string& ini);
// The bridge in its lighting form (HDR scene colour), made on first use. False in mode 0 (the
// game's own lighting), when the bridge is off, or before the G-buffer targets exist.
bool LightingEnsure();
// End of the opaque pass, after depth, normals and motion crossed: share the surface, the
// cascades and the lights, light the frame into the HDR scene colour. True when the pass ran.
bool LightingAtOpaque(ID3D11DeviceContext* real, ID3D11Resource* sceneHdr);
// Once a frame at Present: the hotkey, the capture set and its statistics.
void LightingOnPresent(bool worldFrame);
// LightingSchedule.cpp: the steps of a capture set and the frames that run them.
void LightingMakeSteps();
// The current [Lighting] tuning to the pass, and the game's AO and fog off where ours replace them.
void LightingApplyTuning();
void LightingScheduleOnPresent(bool worldFrame);
// BridgeHookLightingStats.cpp.
void LightingLogGeometry(IDXGISwapChain* sc, const D3D11_TEXTURE2D_DESC& backbuffer);
// Ours against the game's lit buffer, with the coverage check; json gets the fields for the harness.
void LightingStats(const char* step, int mode, std::string* json);
void LightingDump();                              // the frame's inputs and outputs for tools/lighting
}
