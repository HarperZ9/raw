#pragma once
// RAW drives the sun's shadow cascades per scene (RAW engine runtime). Original work, MIT.
//
// The cascade setup (BSShadowDirectionalLight slot 0x10, 0x1512230) reads fShadowDistance, fFirstSliceDistance
// and fSplitOverlap every outdoor frame, so writes to them take effect on the next frame. fInteriorShadowDistance
// governs interiors. The resolution goes through the live atlas rebuild (`atlas`), which fits the passes'
// viewports (rt_passes.h).
//
//   cascades                          the current values
//   cascades set <dist> <first> [overlap] [interiorDist]
//   cascades auto <0|1>               presets by scene, applied when the scene changes: [Cascades] Exterior,
//                                     Interior and Vista in d3d11_proxy.ini, each "dist,first,overlap,atlas"
//                                     (atlas 0 keeps the size). Vista: the camera above VistaZ (9000) world
//                                     units, a fixed height that suits Skyrim's valleys, not its peaks.
// Every change is a result line, so a following `measure` gives its GPU cost per pass.
#include <cstdint>
#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

void CascadesInit(const Image& img, const std::vector<RefTarget>* refs, const std::string& ini);
// called each present; returns a requested atlas size (0 none) for the control loop to rebuild
uint32_t CascadesOnPresent(std::string* resultLine);
std::string CascadesCommand(const std::string& op, const std::string& rest);

}  // namespace raw::rt
