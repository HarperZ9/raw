#pragma once
// Papyrus opcode quota: how many opcodes the VM runs on a stack before it queues the stack for the next update
// (RAW engine runtime). Original work, MIT. See rt_pquota.cpp.

#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

// [EngineRT] PapyrusQuota=<n>: patches the loop at module start (before any script runs); the game's own value is 100.
std::string PapyrusQuotaInstall(const Image& img, uint32_t quota);
// papyrus quota <n>: changes the quota live once installed; reports the state.
std::string PapyrusQuotaCommand(const std::string& arg);

}  // namespace raw::rt
