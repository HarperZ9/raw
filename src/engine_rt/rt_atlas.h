#pragma once
// Live shadow-atlas reallocation (RAW engine runtime). Original work, MIT.
#include <d3d11.h>

#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

// Rebuilds depth-stencil entry `entry` (2: the sun's cascades) at size x size, keeping format, array size
// and every view description. Call at a present. Returns a line for the results file.
std::string ResizeShadowAtlasLive(ID3D11DeviceContext* ctx, const Image& img, const std::vector<RefTarget>& refs,
                                  int entry, uint32_t size);
void AtlasOnPresent();
// The render-target table's depth entries and the textures behind them, as one JSON line.
std::string DumpDepthTargets(const Image& img, const std::vector<RefTarget>& refs);

}  // namespace raw::rt
