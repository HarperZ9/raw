#pragma once
// Tonemap: RAW's tone map and exposure in place of the game's (TonemapPs.h holds the shader). The game's
// tone map pixel shader is replaced by hash through the shader manager; for its draw the proxy binds
// RAW's values at pixel buffer 5 and puts the game's binding back after. Keys in [Lighting] (a live edit
// applies): Tonemap (1 on), Exposure (0 the game's, 1 a camera's), ExposureBias (stops), ExposureMinLum,
// ExposureMaxLum, ExposureKey, ExposureAdapt, ExposureRef, ToneCurve (0 the game's, 1 filmic), ToneWhite, ToneView (1 shows
// the exposure), ToneX0 to ToneX5 (free values for the shader, for live tuning). With [HotReload] on, Tonemap.hlsl in
// the shader folder replaces the built-in shader and a change to it recompiles at the next tone map draw.
// Original work, MIT.
#include <d3d11.h>
#include <string>

namespace SB::Proxy
{
struct TonemapSettings
{
    // Defaults tuned live in one session (rt8 panels 1 to 7, Riverwood at dawn, midday, dusk and night, and the inn;
    // docs/RAYTRACING.md, "The tone map, tuned live"). Units are the game's: its adapted average reads 0.39 at a clear
    // midday, 0.22 at night and 0.094 in the Bannered Mare, and the game's own exposure is 1 in all of them.
    bool enabled = false;
    int mode = 1;            // exposure: 0 the game's (target / adapted average), 1 metered (below)
    float bias = 0.0f;       // stops
    float minLum = 0.02f, maxLum = 8.0f;   // the metered average is held in this range
    float key = 0.41f;       // the reference average maps to this exposure x reference
    int curve = 3;           // 0 the game's, 1 fitted ACES, 2 extended Reinhard per channel, 3 the game's curve on luminance
                             // with a raised white point and a two-part contrast (Tonemap.hlsl)
    float white = 1.0f;      // the curve's input scale
    int view = 0;
    float adapt = 0.15f;     // the share of a change of average above the reference the exposure follows
    float ref = 0.3f;        // the reference average
    // ToneX0: the share below the reference (0.001: about none; the game itself does not compensate, and full
    // compensation lifted the inn from 19 to 47 of 255). ToneX1: the white point (3.5). ToneX2: curve 2's hue keep.
    // ToneX3: contrast above the key (1.55). ToneX4: the power at black, rising to ToneX3 at the key (0.6). ToneX5: the exposure factor in
    // interiors (cells with no sun cascades; 0 or 1: none).
    float x[6] = {0.001f, 3.5f, 0.0f, 1.55f, 0.6f, 0.0f};
};

void Tonemap_Set(const TonemapSettings& t);
TonemapSettings Tonemap_Get();
void Tonemap_OnDevice(ID3D11Device* dev);   // compiles RAW's shader once and registers it (when enabled)
void Tonemap_SetSource(const std::wstring& file);   // [HotReload] ShaderDir\Tonemap.hlsl: watched, compiled live
void Tonemap_BeforeDraw(ID3D11DeviceContext* real);
void Tonemap_AfterDraw(ID3D11DeviceContext* real);
}
