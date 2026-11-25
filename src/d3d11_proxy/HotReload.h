#pragma once
// HotReload: shaders and [Lighting] parameters reload while the game runs (HotReload.cpp,
// docs/HOT_RELOAD.md). Original work, MIT.
#include <string>

namespace SB::Proxy
{
void HotReload_LoadConfig(const std::string& ini);
void HotReload_OnPresent();
void HotReload_SyncIniTime();   // before the game's Present, on the render thread

// Live in-memory HLSL/GLSL shader edit without disk writes or file-watcher polling:
// Compiles `source` asynchronously on a worker thread and swaps the target pass PSO on the next Present.
// Supported passName values: "Lighting", "LightFog", "Clouds", "VolumetricPass", "RtShadow",
// "RtSkyFilter", "RtWater", "Sss", "RtReflFilter", "Rawlss5Pass", "FroxelPass".
bool HotReload_SubmitLiveShader(const std::string& passName, const std::string& source, bool isGlsl);
std::string HotReload_LiveStatus();
}
