#pragma once
// Live bit-level engine heuristic auditor & relationship mapper (RAW engine runtime). Original work, MIT.
// Inspects SkyrimSE.exe 1.7.104.0 singletons, pointers, arrays, color tables, transforms, and invariants
// under SEH protection and emits a structured JSON diagnostic report via `engine commands=audit`.
#include <string>

#include "rt_image.h"

namespace raw::rt {

std::string AuditCommand(const Image& img);
std::string EngineOpticsCommand(const Image& img);

}  // namespace raw::rt
