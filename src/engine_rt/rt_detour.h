#pragma once
// Inline detours on the game's own functions (RAW engine runtime). Original work, MIT.
//
// For a function the game calls directly rather than through a vtable. The first whole instructions covering
// 14 bytes move to a trampoline, followed by a jump back; the function's entry becomes an absolute jump to the
// hook. Instructions are decoded with Zydis from the code as it sits in memory. A prologue holding a relative
// branch or a RIP-relative operand is refused (named in the result), never rewritten blindly. Bytes are moved,
// never written to any file.
#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

// Installs a detour at rva. On success *original is callable as the function and the result starts with "ok".
std::string InstallDetour(const Image& img, uint32_t rva, void* hook, void** original);

}  // namespace raw::rt
