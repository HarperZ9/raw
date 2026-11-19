#pragma once
// Shared by the WrappedContext*.cpp files: the includes, the callback lists registered through
// ProxyAPI, the depth hooks from WrappedSwapChain.cpp and the state-description writer. The hooks
// themselves are split by area: WrappedContext.cpp (lifetime, IUnknown, global state),
// WrappedContextDraw.cpp (shaders, draws), WrappedContextOM.cpp (output merger, clears, pixel
// resources, Map), WrappedContextBind.cpp (compute, input assembler, other stages, rasterizer),
// WrappedContextData.cpp (copies, queries, misc) and WrappedContextGet.cpp (getters). Original work, MIT.
#include "SceneGeometry.h"
#include "WrappedContext.h"
#include "ShaderManager.h"
#include "RenderPhaseDetector.h"
#include "BindingLedger.h"
#include "FrameLedger.h"
#include "BridgeHook.h"
#include "MaterialPipeline.h"
#include "GBufferComplete.h"
#include "SceneLights.h"
#include "GameComposite.h"
#include "CBDirtyTracker.h"
#include "OcclusionCuller.h"
#include "ProxyAPI.h"
#include "ProxyLog.h"
#include "DepthOwnership.h"
#include <fstream>
#include <filesystem>
#include <system_error>
#include <cstdint>
#include <utility>
#include <initializer_list>
#include <unordered_set>
#include <d3d11_4.h>
#include <vector>
#include <cstring>

namespace SB::Proxy
{

// ── Global callback lists (registered via ProxyAPI) ─────────────────────────
inline std::vector<OnDrawCallback>       g_drawCallbacks;
inline std::vector<OnRTChangeCallback>   g_rtChangeCallbacks;
inline std::vector<OnShaderBindCallback> g_shaderBindCallbacks;
inline std::vector<OnViewportCallback>   g_viewportCallbacks;

// Defined in WrappedSwapChain.cpp — depth capture
extern void PreClearDepthCopy(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* dsv);
extern void OnDepthUnbind(ID3D11DeviceContext* ctx, ID3D11DepthStencilView* newDSV);

// Writes a blend, raster, depth or sampler description once per state object (WrappedContext.cpp).
void EmitStateLine(const char* obj, const void* ptr, std::initializer_list<std::pair<const char*, int>> kv);

} // namespace SB::Proxy
