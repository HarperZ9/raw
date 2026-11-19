#pragma once
// Vtable slot tracer (RAW engine runtime). Original work, MIT.

#include <string>
#include <vector>

#include "rt_image.h"

namespace raw::rt {

struct VTraceClass {
    std::string name;
    uint32_t vtable = 0;  // RVA of slot 0
    uint32_t slots = 0;
};

// Returns the number of slots now traced. outPath receives a snapshot every 300 presents.
size_t InstallVTrace(const Image& img, const std::vector<VTraceClass>& classes, const std::string& outPath);
void VTraceOnPresent();

}  // namespace raw::rt
