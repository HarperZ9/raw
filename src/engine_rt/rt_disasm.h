#pragma once
// disasm: a listing of one function for local analysis (RAW engine runtime). Original work, MIT. See rt_disasm.cpp.

#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

// The function holding rva (its .pdata bounds), or maxBytes from rva when given, to <outDir>\disasm_<rva>.txt.
std::string DisasmFunction(const Image& img, uint32_t rva, uint32_t maxBytes, const std::string& outDir);

}  // namespace raw::rt
