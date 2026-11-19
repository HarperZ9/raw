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
// Lock-free per-draw reads (SceneGeometryBound.cpp).
void RecordLayout(ID3D11InputLayout* il, const Layout* L);   // L nullptr: no position element
void RecordPixelShader(ID3D11PixelShader* ps, uint8_t kind);
const Layout* LayoutOf(ID3D11InputLayout* il);         // nullptr: not recorded, or no position
bool FindLayout(ID3D11InputLayout* il, Layout& out);
uint8_t AlphaKindOf(ID3D11PixelShader* ps);            // RtAlphaDiscard, RtAlphaFoliage or none
bool IsDiscardPs(ID3D11PixelShader* ps);              // the pixel shader discards (alpha-tested)
// The bound objects, borrowed (no reference held; valid while bound).
ID3D11InputLayout* BoundLayout(ID3D11DeviceContext* real);
ID3D11PixelShader* BoundPs(ID3D11DeviceContext* real);
uint8_t BlendAlpha(ID3D11DeviceContext* real);         // RtAlphaCoverage, RtAlphaBlend or none
// What a joined draw keeps, each with a reference the bridge releases.
ID3D11ShaderResourceView* AlphaTexture(ID3D11DeviceContext* real);   // t0 of the pixel shader, a 2D texture view
ID3D11Buffer* VertexBuffer(ID3D11DeviceContext* real, UINT slot, UINT* stride, UINT* offset);
ID3D11Buffer* IndexBuffer(ID3D11DeviceContext* real, DXGI_FORMAT* format, UINT* offset);
// What a draw captured on this context gives its references back with (RtDraw::drop).
void (*DropFor(ID3D11DeviceContext* real))(raw::bridge::RtDraw&);
std::vector<raw::bridge::RtDraw>& Frame();             // the frame's draws so far
} // namespace geom

void SceneGeometry_CensusWith(const geom::Layout* L, int call);   // the census with the layout already looked up
// true: a skinned layout. L is the bound layout's record (nullptr: none).
bool SceneSkin_OnDraw(ID3D11DeviceContext* real, const geom::Layout* L, UINT indexCount, UINT startIndex, INT baseVertex);
void SceneSkin_Log();
void SceneSkin_Took(std::vector<raw::bridge::RtDraw>& out);   // the carry from the frame before joins
void SceneSkin_Drop(std::vector<raw::bridge::RtDraw>& frame); // skinned draws after the capture are kept
bool SceneSkin_ReadView(ID3D11DeviceContext* real, float out[16]);   // vertex cb12 rows 8 to 11, the draw's view-projection
void SceneSkin_NoteMainView(ID3D11DeviceContext* real);              // the frame's main view, from a rigid lit draw
void SceneSkin_FilterViews(std::vector<raw::bridge::RtDraw>& out);   // actor draws on another view leave (RtActorView)
} // namespace SB::Proxy
