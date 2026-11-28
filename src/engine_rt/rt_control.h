#pragma once
// Engine control (RAW engine runtime). Original work, MIT.

#include <d3d11.h>
#include <dxgi.h>

#include <string>
#include <vector>

#include "ProxyAPI.h"
#include "rt_callgraph.h"
#include "rt_image.h"

namespace raw::rt {

// Settings by name from engine_rt_refs.tsv; writes go to the Setting's value field (+8).
bool SetSetting(const Image& img, const std::vector<RefTarget>& refs, const std::string& name, const std::string& value,
                std::string* before = nullptr);

// [EngineSet] in d3d11_proxy.ini: name=value pairs applied once at module start.
size_t ApplyStartupSettings(const Image& img, const std::vector<RefTarget>& refs, const std::string& ini,
                            const std::string& logPath);

// The go-file protocol: raw_engine_go.txt holds commands (set, wait, measure, capture, atlas, view,
// cam_sweep, cam_stop, set_pose, dismiss_modal, auto_dismiss, set_time); results go to
// <out>/control_results.jsonl and raw_engine_done.txt answers.
// invalidateStateCache: the proxy's, called after a command touches pipeline state on the real context.
void ControlInit(const Image& img, const std::vector<RefTarget>* refs, const std::string& gameDir, const std::string& outDir,
                 void (*invalidateStateCache)());
void ControlOnPresent(ID3D11DeviceContext* ctx, IDXGISwapChain* sc);
// The proxy's per-frame counters (draws, state calls, constant-buffer maps) join every measure.
void ControlSetProxy(SB::Proxy::ProxyInterface* proxy);

// Shadow atlas: the renderer's depth-stencil target table (BSGraphics::RenderTargetManager, member
// depthStencilTargetData at +0xC78, 0x10 per entry; entries 2 and 4 hold the shadow maps) resized before
// the targets are created. Returns a line for the log.
std::string ResizeShadowAtlas(const Image& img, const std::vector<RefTarget>& refs, uint32_t size);

// Native high-FPS Havok physics & gameclock untying (replaces SSE Display Tweaks):
// unlocks bLockFramerate:Display and dynamically tracks fMaxTime:HAVOK / fMaxTimeComplex:HAVOK to live frame time.
std::string HavokFixInit(const Image& img, const std::vector<RefTarget>& refs);
void HavokFixOnPresent();

// Native engine-level architectural upgrades (multithreaded bones/particles/facegen/trees/lip, Papyrus VM budget,
// +47% near-plane Z precision, preemptive cell unload). Applied before [EngineSet] so user overrides win.
std::string ApplyEngineTuning(const Image& img, const std::vector<RefTarget>& refs);

// Wave 25/35 Live Dynamic WorldSim GMST & planetary gravity feedback (restored cleanly on shutdown):
std::string WorldSimBindDynamicSettings(const Image& img, const std::vector<RefTarget>& refs);
void WorldSimApplyLiveGmsts(float barterMin, float barterMax, float sprintStaminaDrainMult);
void WorldSimApplyLivePlanetaryGravity(float gravityMps2);
void WorldSimRestoreDynamicSettings();

}  // namespace raw::rt
