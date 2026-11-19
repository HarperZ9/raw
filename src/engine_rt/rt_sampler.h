#pragma once
// In-process sampling profiler (RAW engine runtime). Original work, MIT.
#include <cstdint>
#include <string>

#include "rt_image.h"

namespace raw::rt {

// Samples every game thread about once a millisecond for `seconds` on a background thread, then hands one JSON
// line to `result`: the hottest functions overall and inside [lo, hi), the callers found on the stacks of the
// samples inside it, the first caller outside it, and the threads. False when a run is already going.
bool StartSampler(const Image& img, double seconds, uint32_t lo, uint32_t hi, void (*result)(const std::string&));
// Called from the present callback: the render thread, to tell it apart in the report.
void SamplerNoteRenderThread();

}  // namespace raw::rt
