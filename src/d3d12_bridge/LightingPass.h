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
    bool  rtShadows = true;         // improved: ray-traced sun and point-light shadows on the game's geometry (attached to raw::tx)
    bool  rtAlpha = true;           // alpha-tested draws join the ray-tracing scene, their alpha tested by the rays
    float rtAlphaRef = 0.5f;        // the alpha cut the rays apply
    float rtTermDist = 48.0f;       // game units: near hits ignored where the surface turns from the sun (facets)
    bool  rtSky = true;             // exteriors: the ambient scaled by the ray-traced sky visibility
    float rtSkyStrength = 1.0f;     // 0 none, 1 the full visibility
    float rtSkyLength = 6000.0f;    // game units
    float rtBounce = 1.0f;          // exteriors: one bounce of indirect light from the sky rays (0 off)
    float rtBounceInterior = 0.5f;  // interiors: the same bounce on top of the cell's ambient (0 off; the game's fill already holds some)
    bool  rtOmm = true;             // opacity micromaps for the alpha-tested meshes, where the runtime has them
    int   rtDebug = 0;              // the reflection output shows (Lighting view 7): 1 the primary ray, 2 the radiance cache, 3 the colour copies
    bool  rtMaterials = true;       // every mesh keeps its colour, coordinates and normals: off-screen hits are shaded
    int   rtMatSide = 128;          // the largest side of a colour copy
    bool  rtCache = true;           // the world-space radiance cache for off-screen hits and further bounces
    float rtCacheCell = 16.0f;      // game units, the finest cell
    float wetness = 0.0f;           // improved: rain on surfaces, 0 dry to 1 soaked (Weather.hlsli)
    float snow = 0.0f;              // improved: snow lying on upward surfaces the sky reaches, 0 none to 1 deep (Weather.hlsli)
    float puddles = 0.0f;           // improved: water standing on flat ground, 0 none to 1 most
    bool  rtSkin = true;            // skinned actors join the scene with a BLAS a frame
    int   rtSkinBones = 10;         // the vertex constant buffer slot that holds the current bone palette
    bool  rtIncremental = true;     // RtIncremental: the scene's per-draw lookups once a frame, instances staged
    bool  rtCamSteps = false;       // RtCamSteps: the camera's place from the summed steps (RtScene::SetCamera)
    int   rtSkinFilter = 0;         // RtSkinFilter: diagnosis, which skinned draws join (SceneSkin.cpp)
    bool  rtActors = true;          // skinned draws on the main depth target outside the opaque set (people, the player) join
    bool  rtReflect = true;         // glossy surfaces: one ray-traced reflection ray, the hit's colour in the game's frame
};

// Lights into clusters: out[cluster] holds one bit per light index.
void BuildClusters(const SceneLighting& s, float nearZ, float farZ, uint32_t (*out)[4]);

std::unique_ptr<IBridgePass> MakeLightingPass();
bool LightingPassTune(IBridgePass* pass, const LightingTuning& t);
bool LightingPassUniform(IBridgePass* pass, const void** data, size_t* bytes);   // cbuffer P as filled for this frame
bool LightingPassReload(IBridgePass* pass, const std::vector<uint8_t>& vs, const std::vector<uint8_t>& fs,
                        const std::vector<uint8_t>& fogFs, std::string& err);

} // namespace raw::bridge
