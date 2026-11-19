#pragma once
// Live loaded-grid size (RAW engine runtime). Original work, MIT.
//
// TES+0x78 is the GridCellArray: worldX and worldY at +0x08, the length at +0x10 and the cell pointers
// (length x length) at +0x18. The array is allocated once, at the size uGridsToLoad has when the game starts.
// `grids <n>` changes the length up to that startup size and writes uGridsToLoad to match. The length only
// changes while the array holds no cell. Interiors keep the exterior grid loaded (measured: 49 of 49 cells in
// the Bannered Mare), so a request made with cells loaded waits for the game's own GridCellArray::DetachAll or
// KillAll (vtable 0x178B160 slots 1 and 2, hooked on first use) and lands right after it; the game's next
// SetCenter fills the new grid. `grids` alone reports the array and any pending length.
#include <cstdint>
#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

std::string GridsCommand(const Image& img, const std::vector<RefTarget>& refs, const std::string& arg);

}  // namespace raw::rt
