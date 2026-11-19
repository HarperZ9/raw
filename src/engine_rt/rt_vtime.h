#pragma once
// CPU time of engine functions, timed live (RAW engine runtime). Original work, MIT.
#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

// Times the function in a vtable slot, from now on. Up to 16 functions in all. Returns a line for the results.
std::string VTimeSlot(const Image& img, uint32_t vtable, uint32_t slot, const std::string& label);
// Times the function whose address a RIP-relative lea at `site` loads (a job or callback handed elsewhere).
std::string VTimeLea(const Image& img, uint32_t site, const std::string& label);
void VTimeOnPresent();
void VTimeReset();
// ",\"cpu_ms\":{...},\"cpu_calls\":{...}" per frame since the last reset: thread time of the outermost calls,
// summed over every thread (job threads run in parallel, so this can exceed the frame's wall time).
std::string VTimeStats();

}  // namespace raw::rt
