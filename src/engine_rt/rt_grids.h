#pragma once
// Live loaded-grid size (RAW engine runtime). Original work, MIT.
//
// TES+0x78 is the GridCellArray: worldX and worldY at +0x08, the length at +0x10 and the cell pointers
// (length x length) at +0x18. The array is allocated once, at the size uGridsToLoad has when the game starts.
// `grids <n>` changes the length up to that startup size. The length only changes while the array holds no cell,
// so a request waits for the game's GridArray::DetachAll (0x221380, inline detour: the game calls it directly when
// the worldspace changes) and lands right after it, with uGridsToLoad written in the same step; the game's next
// SetCenter fills the new grid. `grids` alone reports the array, the hook counts and any pending length.
#include <cstdint>
#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

std::string GridsCommand(const Image& img, const std::vector<RefTarget>& refs, const std::string& arg);

}  // namespace raw::rt
