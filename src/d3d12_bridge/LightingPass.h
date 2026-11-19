#pragma once
#include <string>
#include <vector>
// LightingPass: the Playground engine lights the game frame itself (docs/LIGHTING.md).
// The bridge runs it at the end of the game's opaque pass on the HDR scene colour; its result
// replaces what the game's lit shader wrote there, so the game's water, effects, fog, bloom,
// tone map and anti-aliasing all run on it, and the HUD draws over it.
//
// Passes, each timed: the engine's real-time AO (improved mode with Ao on), the lighting of
// every described opaque pixel, and the fog lit by every light. Original work, MIT.
#include "D3D12Bridge.h"

namespace raw::bridge
{

constexpr uint32_t kTilesX = 16, kTilesY = 9, kSlices = 16;
constexpr uint32_t kClusterCount = kTilesX * kTilesY * kSlices;

// What the lighting pass does. Mode 0 never reaches the pass: the hook leaves the game's
// lighting alone. Each improvement has its own switch so its before and after can be shown.
struct LightingTuning
{
    int   mode = 1;                 // 1 match the game, 2 improved
    bool  softShadows = true;       // improved: screen-space soft shadows for every point light
    bool  ggxSpecular = true;       // improved: energy-conserving GGX in place of the game's lobe
    bool  volumetricFog = true;     // improved: fog lit by the sun (shadowed) and every point light
    bool  contactShadows = true;    // improved: a short march along the sun direction
    bool  ao = true;                // improved: the engine's real-time AO on the ambient term
    bool  cascades = false;         // the sun's shadow from the decoded cascades; off: the game's mask
    int   view = 0;                 // 1 world normal, 2 sun shadow, 3 ambient
    float fogDensity = 0.00006f;    // extinction per game unit
    float fogAnisotropy = 0.35f;    // Henyey-Greenstein g
    float fogLightScale = 1.0f;     // point-light in-scatter gain
    float fogSunScale = 0.35f;      // sun in-scatter gain (1.0 washed near ground warm on top of the game's haze)
    bool  fogReplacesGame = false;  // our fog replaces the game's distance fog; off: the game's art-directed
                                    // haze stays and ours adds lit shafts and lamp glow only
    float fogMaxDistance = 6000.0f; // game units
    float softRange = 400.0f;       // soft-shadow march length cap, game units
    float contactLength = 40.0f;    // game units
    float aoStrength = 0.7f;        // the game's own AO still multiplies the frame after this pass
    float clusterFar = 40000.0f;    // the last depth slice ends here
};

// Lights into clusters: out[cluster] holds one bit per light index.
void BuildClusters(const SceneLighting& s, float nearZ, float farZ, uint32_t (*out)[4]);

std::unique_ptr<IBridgePass> MakeLightingPass();
bool LightingPassTune(IBridgePass* pass, const LightingTuning& t);
bool LightingPassReload(IBridgePass* pass, const std::vector<uint8_t>& vs, const std::vector<uint8_t>& fs,
                        const std::vector<uint8_t>& fogFs, std::string& err);

} // namespace raw::bridge
