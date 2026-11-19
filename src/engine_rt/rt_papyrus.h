#pragma once
// Papyrus: the VM's update timed, its budget fixed or adaptive (RAW engine runtime). Original work, MIT.
#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

// VirtualMachine::Update and UpdateTasklets (vtable slots 4 and 5) timed; a positive override replaces the budget.
bool InstallPapyrusTiming(const Image& img, uint32_t vmVtable, float budgetOverrideMs);
// The opcode loop (loopRva) timed at every direct call to it from VM slots 4 and 5. Install before the VM runs.
// Returns the number of call sites patched; sites lists them.
size_t InstallOpcodeLoopTiming(const Image& img, uint32_t vmVtable, uint32_t loopRva, std::string* sites);
// Each present: the frame's time, for the adaptive budget.
void PapyrusOnPresent(double frameMs);
void PapyrusReset();
// ",\"vm_ms_total\":...": VM time and calls since the last reset, and the adaptive budget when it is on.
std::string PapyrusStats();
// adaptive <target> <min> <max> | fixed <ms> | off; returns the result line.
std::string PapyrusCommand(const std::string& mode, const std::string& rest);

}  // namespace raw::rt
