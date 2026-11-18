#pragma once
// State shared by BridgeHook.cpp (the frame) and BridgeHookCapture.cpp (files).
#include "BridgeHook.h"
#include "D3D12Bridge.h"
#include <string>

namespace SB::Proxy::hook
{
struct Config
{
    bool enabled = false, fallbackAtPresent = true, debugLayer = false, enginePass = false;
    int  timingLogEvery = 600;
    int  captureFrame = 0;         // absolute frame for the backbuffer BMP (0 = never)
    int  captureAfter = 0;         // bridge frames before the gameplay capture set (0 = never)
    std::string captureDir;        // absolute; the gameplay capture set goes here
    raw::bridge::PassConstants k;
    raw::bridge::EngineTuning tune;
};

Config& Cfg();
raw::bridge::D3D12Bridge* Bridge();

// Writes the backbuffer (8-bit RGBA or BGRA) as a 32-bit BMP: what the game
// is about to present, read from the GPU.
void CaptureBackbuffer(const std::string& path);
// The gameplay capture set: frame with the pass, the D3D12 reads of what
// crossed, the engine's visibility, then a frame with the pass switched off.
void CaptureStep(uint64_t bridgeFrames);
void InstallShareCheck();
}
