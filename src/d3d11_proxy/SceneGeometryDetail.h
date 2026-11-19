#pragma once
// Shared by SceneGeometry.cpp (rigid draws) and SceneSkin.cpp (skinned draws). Original work, MIT.
#include <d3d11.h>
#include <cstdint>
#include <vector>
#include "../d3d12_bridge/D3D12Bridge.h"

namespace SB::Proxy
{
namespace geom
{
struct Elem { UINT slot = 0, offset = 0; DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN; bool set = false; };
struct Layout { Elem pos, uv, blendIdx, blendW; };
bool FindLayout(ID3D11InputLayout* il, Layout& out);
bool IsDiscardPs(ID3D11PixelShader* ps);              // the pixel shader discards (alpha-tested)
uint8_t BlendAlpha(ID3D11DeviceContext* real);         // RtAlphaCoverage, RtAlphaBlend or none
std::vector<raw::bridge::RtDraw>& Frame();             // the frame's draws so far
} // namespace geom

bool SceneSkin_OnDraw(ID3D11DeviceContext* real, UINT indexCount, UINT startIndex, INT baseVertex);   // true: a skinned layout
void SceneSkin_Log();
void SceneSkin_Took(std::vector<raw::bridge::RtDraw>& out);   // the carry from the frame before joins
void SceneSkin_Drop(std::vector<raw::bridge::RtDraw>& frame); // skinned draws after the capture are kept
} // namespace SB::Proxy
