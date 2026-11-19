#pragma once
// Tonemap: RAW's tone map and exposure in place of the game's (TonemapPs.h holds the shader). The game's
// tone map pixel shader is replaced by hash through the shader manager; for its draw the proxy binds
// RAW's values at pixel buffer 5 and puts the game's binding back after. Keys in [Lighting] (a live edit
// applies): Tonemap (1 on), Exposure (0 the game's, 1 a camera's), ExposureBias (stops), ExposureMinLum,
// ExposureMaxLum, ExposureKey, ToneCurve (0 the game's, 1 filmic), ToneWhite, ToneView (1 shows the
// exposure). Original work, MIT.
#include <d3d11.h>

namespace SB::Proxy
{
struct TonemapSettings
{
    bool enabled = false;
    int mode = 1;            // exposure: 0 the game's, 1 metered to a key
    float bias = 0.0f;       // stops
    float minLum = 0.3f, maxLum = 4.0f;    // the metered average is held in this range: a night meters near 0.1 and,
                                           // brought to the key, read as dusk (0.05 lifted Riverwood at 23:00 from 39 to 82 of 255)
    float key = 0.4f;        // the metered average maps here. The scene's units are the game's, not photometric: in Riverwood
                             // at 13:00 the game's adapted average and its target both read 0.46 (its own exposure is then 1);
                             // 0.18 left the frame far darker than the game's (42 of 255 against 98), 1.0 far brighter (164)
    int curve = 1;           // 0 the game's curve, 1 filmic
    float white = 0.6f;      // the filmic curve's input scale
    int view = 0;
};

void Tonemap_Set(const TonemapSettings& t);
void Tonemap_OnDevice(ID3D11Device* dev);   // compiles RAW's shader once and registers it (when enabled)
void Tonemap_BeforeDraw(ID3D11DeviceContext* real);
void Tonemap_AfterDraw(ID3D11DeviceContext* real);
}
