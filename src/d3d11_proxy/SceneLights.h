#pragma once
// SceneLights: the frame's lights, sun, ambient and shadow cascades, decoded live from the
// constant buffers the game binds for its own lit draws (docs/LIGHTING.md). The offline form
// of the same decode is tools/frame_buffers/lights.py, which checked it on captured frames.
//
// The proxy keeps a CPU copy of every dynamic constant buffer the game maps (CBDirtyTracker),
// so a draw's constants are read at the draw with no GPU read:
//
//   lit draw (pixel buffer 2 of 30 vectors, vertex buffer 2 of 15)
//       [0] sun direction, [1] sun colour, [2] which shadow-mask channel shadows light i,
//       [11..13] directional ambient, [15 + i] light position and radius, [22 + i] colour,
//       [29].x lights, [29].y of them shadowed; all in the draw's model space, taken to the
//       camera-relative world by the world matrix in rows 0 to 2 of vertex buffer 2
//   shadow-mask draw (pixel buffer 2 of 25 vectors)
//       [16..18], [19..21] cascade matrices, [2].y and [2].z their bias; pixel buffer 0 [2].x
//       the depth where cascade 1 takes over and [2].w the cascade count
//
// Original work, MIT.
#include <d3d11.h>
#include "SceneLighting.h"

namespace SB::Proxy
{
inline bool g_sceneLightsActive = false;   // set by the bridge hook when Pass=lighting

void SceneLights_BeforeDraw(ID3D11DeviceContext* real);
// Lit draws count only while the game's opaque set is bound: the frame's main camera.
void SceneLights_SetOpaque(bool open);
bool SceneLights_Opaque();   // the opaque set is bound now
// The main camera from the per-frame constants (vertex slot 12) as bound now: call at the end
// of the opaque pass.
void SceneLights_SetCamera(ID3D11DeviceContext* real);
// What the frame has decoded so far (lights are merged when read).
const raw::bridge::SceneLighting& SceneLights_Frame();
// The textures the shadow-mask draw used this frame: its target and the cascade array. Null
// when no shadow-mask draw was seen. Borrowed for the frame.
ID3D11Texture2D* SceneLights_ShadowMask();
ID3D11Texture2D* SceneLights_Cascades();
void SceneLights_OnPresent();
// The current draw's world matrix (3 rows) when SceneLights_BeforeDraw counted it as a lit draw of
// the opaque set; null otherwise. Valid until the next draw.
const float* SceneLights_DrawWorld();
}
