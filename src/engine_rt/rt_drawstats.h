#pragma once
// Draw statistics per submission pass (RAW engine runtime). Original work, MIT.
//
// Measures what draw batching can win before any batching is built. The BSShader family's SetupTechnique (slot
// 2), SetupMaterial (4) and SetupGeometry (6) are hooked on every shader class, and BSShaderAccumulator slot
// 0x2A gives the pass: its k-th outermost call on the render thread in a frame. Per pass and shader class:
//   draws          SetupGeometry calls (one per draw)
//   tech, mat      SetupTechnique and SetupMaterial calls, with how many repeat the previous call's argument
//   same_mesh      draws whose mesh (BSGeometry's renderer data) equals the previous draw's
//   same_key       draws whose technique, material and mesh all equal the previous draw's: instanceable as is
//   meshes         distinct meshes in the pass (instanceable after sorting)
//   *_ms           CPU time inside each of the three calls
#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

// Installs the hooks on the shader vtables and the accumulator. Returns a line for the results.
std::string DrawStatsInstall(const Image& img);
void DrawStatsOnPresent();
void DrawStatsReset();
// ",\"drawstats\":[...]" per frame since the last reset, or "" when not installed.
std::string DrawStatsStats();

}  // namespace raw::rt
