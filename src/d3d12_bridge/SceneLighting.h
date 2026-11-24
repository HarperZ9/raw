#pragma once
// SceneLighting: the lights of one game frame, decoded from the game's own per-draw
// constants (docs/LIGHTING.md, "What the game hands its shaders"). The proxy fills it
// during the frame; the lighting pass reads it. Plain data, no API headers.
//
// Space: camera-relative world space, the game's own (world positions minus the camera
// position, in game units). Matrices are stored as the game stores them: rows that each
// dot a column vector, out[i] = dot(row[i], in).
//
// Original work, MIT.
#include <cstdint>

namespace raw::bridge
{

constexpr uint32_t kMaxSceneLights = 128;

struct SceneLight
{
    float position[3] = {};   // camera-relative world
    float radius = 0;         // the game's window: 1 - (d / radius)^2
    float colour[3] = {};     // linear, as the game's lit shader multiplies it
    int   shadowChannel = -1; // 0 to 3: the channel of the game's shadow mask that shadows it; -1 none
    uint32_t draws = 0;       // lit draws that carried it this frame
};

struct SceneLighting
{
    bool  valid = false;

    // The directional light: the sun by day, the moon's light by night. Unit direction
    // towards the light.
    bool  hasSun = false;
    bool  inWater = false;
    float sunDirection[3] = {0, 0, 1};
    float sunColour[3] = {};

    // The game's directional ambient: colour channel c = dot(ambient[c].xyz, n) + ambient[c].w
    // for a unit world normal n, from the most common value among the frame's lit draws.
    float ambient[3][4] = {};
    // The soft knee the lit shader applies to its output: x -> min(x, (1 - knee) x + 1).
    float knee = 0.8333f;
    // The material's specular colour times strength, the median over the lit draws (the
    // patched G-buffer stores the mask and the exponent, not this colour).
    float specularScale = 1.0f;

    // Sun shadow cascades, from the shadow-mask draw: camera-relative world to (u, v, depth)
    // of cascade k, its depth bias, and the depth-buffer value where cascade 1 takes over.
    bool  hasCascades = false;
    bool  hasEngineCell = false;    // true when raw_engine.dll supplied ground-truth TESObjectCELL::IsInterior()
    bool  isInterior = false;       // exact TESObjectCELL::IsInterior() (eliminates exterior->interior flicker when cascades drop)
    bool  menuPaused = false;       // exact UI::Singleton->numPausesGame > 0 (MapMenu/Inventory/Pause isolation)
    float waterHeight = -14000.0f;  // exact TESObjectCELL::waterHeight / TESWorldSpace::defaultWaterHeight
    uint32_t camStateId = 0xFFFFFFFFu; // exact TESCamera::currentState->id (0=1stP, 3=Free, 9=3rdP)
    uint32_t cascades = 0;
    float cascade[2][3][4] = {};
    float cascadeBias[2] = {};
    float splitDepth = 1.0f;

    // The game's per-frame camera (pixel buffer 12): clip to camera-relative world (jittered,
    // the inverse of matrix 2) and matrix 2 itself, the view rotation (matrix 0) and its inverse (matrix 7), and the
    // dynamic-resolution scale.
    bool  hasCamera = false;
    float clipToWorld[4][4] = {};
    float worldToClip[4][4] = {};   // matrix 2, jittered like the depth buffer
    float jitterNdc[2] = {0.0f, 0.0f};   // exact per-frame sub-pixel TAA projection jitter in NDC (from b12 viewToClip)
    float worldToView[4][4] = {};
    float viewToWorld[4][4] = {};
    float resolutionScale[2] = {1, 1};
    // The camera's move since the frame before, in world units: the median over the frame's rigid
    // lit draws of (previous world translation - current world translation); the game keeps both in
    // vertex buffer 2 (rows 3 to 5 are the previous world matrix). World space is camera-relative,
    // so summing this gives a frame that stays put (the radiance cache's). cameraSteps: the draws
    // that voted. posAdjust: float4 40 of the per-frame constants, logged to compare.
    float cameraStep[3] = {};
    uint32_t cameraSteps = 0;
    float posAdjust[4] = {};

    uint32_t lightCount = 0;
    SceneLight lights[kMaxSceneLights];

    // Counters for the log.
    uint32_t litDraws = 0, lightSightings = 0, droppedLights = 0, phantomLights = 0;
};

} // namespace raw::bridge
