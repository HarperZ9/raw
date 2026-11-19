#pragma once
// Pass tracking: GPU time per render pass, the shadow atlas binding and its viewports (RAW engine runtime).
// Original work, MIT.
#include <d3d11.h>

#include <string>
#include <vector>

#include "ProxyAPI.h"
#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

// Registers the proxy callbacks (targets, phase changes, draws, and viewports on proxy version 2).
void PassesInit(const Image& img, const std::vector<RefTarget>* refs, SB::Proxy::ProxyInterface* proxy, bool gpuTiming);
// At each present, before the game's next frame: closes this frame's timestamps, reads frames that are done,
// refreshes the atlas views.
void PassesOnPresent(ID3D11DeviceContext* ctx);
// Clears the accumulators that PassesStats reports.
void PassesReset();
// ",\"gpu_ms\":{...},\"draws\":{...},\"gpu_frames\":n": mean GPU time and draws per pass since the last reset.
std::string PassesStats();
// One JSON line: the atlas size, the viewports the game set while the atlas was bound in the last frame,
// and how many draws and binds it had.
std::string PassesViewports();
// Fit the shadow passes to the atlas: viewports authored for `authoredSize` are scaled to the atlas's
// current width while it is bound. authoredSize 0 turns the fit off.
void PassesSetFit(uint32_t authoredSize);
uint32_t PassesFit();

}  // namespace raw::rt
