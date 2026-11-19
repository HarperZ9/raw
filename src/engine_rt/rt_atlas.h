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

// One depth-stencil entry as the renderer holds it now: the texture, its width and every depth view
// (writable and read-only). False when the table or the entry cannot be read.
struct DepthEntryViews {
    ID3D11Texture2D* texture = nullptr;
    uint32_t width = 0, height = 0, slices = 0;
    ID3D11DepthStencilView* views[16] = {};
};
bool ReadDepthEntry(const Image& img, const std::vector<RefTarget>& refs, int entry, DepthEntryViews* out);
// The render-target table's depth entries and the textures behind them, as one JSON line.
std::string DumpDepthTargets(const Image& img, const std::vector<RefTarget>& refs);

}  // namespace raw::rt
