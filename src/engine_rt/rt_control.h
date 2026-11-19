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

// The go-file protocol: raw_engine_go.txt holds commands (set, wait, measure, capture, atlas); results go to
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

}  // namespace raw::rt
