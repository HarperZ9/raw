#pragma once
// Papyrus throughput: completed script updates counted by the test plugin's globals (RAW engine runtime). Original
// work, MIT. See rt_pcount.cpp.

#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

void PapyrusCountInit(const Image& img, const std::vector<RefTarget>* refs);
// Updates completed since the last call, per second, split by script (",\"papyrus_updates\":{...}"); empty when the
// plugin's globals are not loaded.
std::string PapyrusCountStats();
// papyrus count: the globals found and their values now.
std::string PapyrusCountJson();

}  // namespace raw::rt
