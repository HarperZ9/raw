#pragma once
// GameComposite: switches off the game's own AO and distance fog where the engine's lighting
// replaces them, so the two never stack (docs/LIGHTING.md, "Ours replaces the game's").
//
// After the opaque pass the game draws one full-screen composite into its HDR scene colour,
// read from its disassembly: colour = copy of the scene (t0) plus reflections (t6), times the
// AO texture (t1), then the distance fog lerp(colour, fog colour, f) with
// f = min(pow(saturate(z * c0.y - c0.x), c0.z), c0.w) from pixel buffer 2 (11 vectors). It is
// found by its shader's hash (a9facbe07de6f5d0, the same in every captured scene).
// The fog applies only where the depth it reads (t2) is below 1. For that one draw the proxy
// binds a white texture in place of the AO and a texture of depth 1 in place of t2, then puts
// the game's own bindings back.
//
// Original work, MIT.
#include <d3d11.h>

namespace SB::Proxy
{
struct GameCompositeSwitches { bool noGameAo = false, noGameFog = false; };

void GameComposite_Set(const GameCompositeSwitches& s);
void GameComposite_SetScene(ID3D11Resource* sceneHdr);    // the opaque set's HDR target this frame
void GameComposite_BeforeDraw(ID3D11DeviceContext* real, UINT vertexCount);
void GameComposite_AfterDraw(ID3D11DeviceContext* real);
}
