#pragma once
// peek: game data along a pointer path (RAW engine runtime). Original work, MIT.
#include <cstdint>
#include <string>
#include <vector>

#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

// One 64-bit read of game memory under structured exception handling.
bool ReadU64(const void* p, uint64_t* v);
// peek <global name or 0xRVA> <off>... <bytes>; returns the result line.
std::string Peek(const Image& img, const std::vector<RefTarget>& refs, const std::string& name, const std::string& rest);

}  // namespace raw::rt
