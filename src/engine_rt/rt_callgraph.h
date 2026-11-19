#pragma once
// In-process call graph (RAW engine runtime). Original work, MIT.

#include <string>
#include <vector>

#include "rt_image.h"

namespace raw::rt {

struct RefTarget {
    uint32_t rva = 0;
    uint32_t span = 8;       // bytes that count as this target (24 for a Setting object)
    bool setting = false;
    std::string name;
};

struct CallGraphStats {
    uint64_t functions = 0, leafFunctions = 0, instructions = 0, edges = 0, vcalls = 0, refs = 0,
             clampSites = 0, jumpTables = 0;
    double ms = 0;
};

// Every RIP-relative memory operand and direct call in one function, as one JSON line (addresses,
// mnemonics and access only).
std::string ProbeFunction(const Image& img, uint32_t rva);

// refs must be sorted by rva. Writes the cg_*.tsv files into dir.
CallGraphStats RunCallGraph(const Image& img, const std::vector<RefTarget>& refs, const std::string& dir);

}  // namespace raw::rt
