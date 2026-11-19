#pragma once
// Overlay: a block of text drawn over the top-left of the frame before Present, for hot-reload
// errors (Overlay.cpp). An empty string removes it. Original work, MIT.
#include <string>

namespace SB::Proxy
{
void Overlay_Set(const std::string& text);
void Overlay_Draw();   // on the render thread, before the game's Present
}
