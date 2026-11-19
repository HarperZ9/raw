#pragma once
// SceneGeometry: the frame's rigid lit draws (opaque and alpha-tested), for the bridge's ray-tracing scene
// (SceneGeometry.cpp, docs/RAYTRACING.md). Original work, MIT.
#include <d3d11.h>
#include <vector>
#include "../d3d12_bridge/D3D12Bridge.h"

namespace SB::Proxy
{
inline bool g_sceneGeometryActive = false;   // set with [Lighting] RtShadows=1 on raw::tx

void SceneGeometry_SetAlpha(bool on);   // [Lighting] RtAlpha: alpha-tested draws join with their alpha
void SceneGeometry_OnInputLayout(ID3D11InputLayout* layout, const D3D11_INPUT_ELEMENT_DESC* e, UINT n);
void SceneGeometry_OnPixelShader(ID3D11PixelShader* ps, const void* bytecode, size_t n);
void SceneGeometry_OnDraw(ID3D11DeviceContext* real, UINT indexCount, UINT startIndex, INT baseVertex);
std::vector<raw::bridge::RtDraw> SceneGeometry_Take();   // the frame's draws; the buffers carry a reference
void SceneGeometry_Drop();                               // a frame whose draws never reached the bridge
}
