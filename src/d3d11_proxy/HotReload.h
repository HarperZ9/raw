#pragma once
// HotReload: shaders and [Lighting] parameters reload while the game runs (HotReload.cpp,
// docs/HOT_RELOAD.md). Original work, MIT.
#include <string>

namespace SB::Proxy
{
void HotReload_LoadConfig(const std::string& ini);
void HotReload_OnPresent();
void HotReload_SyncIniTime();   // before the game's Present, on the render thread
}
