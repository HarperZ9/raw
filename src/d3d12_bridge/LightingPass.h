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
    int   mode = 2;                 // 1 match the game, 2 improved (native DX12 + DXR 1.2 default)
    bool  softShadows = false;      // screen-space soft shadows (fallback only when RT is disabled)
    bool  ggxSpecular = true;       // improved: energy-conserving GGX in place of the game's lobe
    bool  volumetricFog = true;     // improved: fog lit by the sun (shadowed) and every point light
    bool  contactShadows = false;   // screen-space contact shadow march (fallback only when RT is disabled)
    bool  ao = true;                // fallback AO when RT sky/RTAO is disabled
    bool  cascades = false;         // the sun's shadow from the decoded cascades; off: the game's mask
    int   view = 0;                 // 1 world normal, 2 sun shadow, 3 ambient
    float fogDensity = 0.00006f;    // extinction per game unit
    float fogAnisotropy = 0.35f;    // Henyey-Greenstein g
    float fogLightScale = 1.0f;     // point-light in-scatter gain
    float fogSunScale = 0.75f;      // sun in-scatter gain
    bool  fogReplacesGame = true;   // native 3D volumetric/aerial fog replaces vanilla distance fog
    float fogMaxDistance = 6000.0f; // game units
    float softRange = 400.0f;       // soft-shadow march length cap, game units
    float contactLength = 40.0f;    // game units
    float aoStrength = 0.35f;       // AoStrength; 0.35 since rt9: with the ray-traced sky as well, 0.7 crushed creases at dawn and night
    float clusterFar = 40000.0f;    // the last depth slice ends here
    bool  rtShadows = true;         // improved: ray-traced sun and point-light shadows on the game's geometry (attached to raw::tx)
    bool  rtAlpha = true;           // alpha-tested draws join the ray-tracing scene, their alpha tested by the rays
    float rtAlphaRef = 0.5f;        // the alpha cut the rays apply
    float rtTermDist = 48.0f;       // game units: near hits ignored where the surface turns from the sun (facets)
    bool  rtSky = true;             // exteriors: the ambient scaled by the ray-traced sky visibility
    float rtSkyStrength = 1.0f;     // RtSkyStrength: 0 none, 1 full hardware DXR 1.2 sky/RTAO visibility
    float rtSkyLength = 2400.0f;    // game units
    float rtBounce = 1.2f;          // exteriors: multi-bounce indirect GI from hardware DXR 1.2
    float rtBounceInterior = 1.15f; // interiors: multi-bounce indirect GI from hardware DXR 1.2
    bool  rtOmm = true;             // opacity micromaps for the alpha-tested meshes, where the runtime has them
    int   rtDebug = 0;              // the reflection output shows (Lighting view 7): 1 the primary ray, 2 the radiance cache, 3 the colour copies
    bool  rtMaterials = true;       // every mesh keeps its colour, coordinates and normals: off-screen hits are shaded
    int   rtMatSide = 128;          // the largest side of a colour copy
    bool  rtCache = true;           // the world-space radiance cache for off-screen hits and further bounces
    float rtCacheCell = 16.0f;      // game units, the finest cell
    float wetness = 0.0f;           // improved: rain on surfaces, 0 dry to 1 soaked (Weather.hlsli)
    float snow = 0.0f;              // improved: snow lying on upward surfaces the sky reaches, 0 none to 1 deep (Weather.hlsli)
    float puddles = 0.0f;           // improved: water standing on flat ground, 0 none to 1 most
    float rain = 0.0f;              // rain falling now, 0 to 1: drops ring the puddles (Weather.hlsli)
    // Clouds (Clouds.hlsl, CloudCommon.hlsli): the sky's volumetric layer, its shadow on the ground, shafts in the fog.
    bool  distantLod = true;        // DistantLod: improved mode lights distant LOD (trees, terrain) itself (FOL-04)
    bool  charLight = true;         // CharLight: the game's character fill light (actors), added to RAW's light
    bool  envMap = true;            // EnvMap: environment-mapped surfaces add the game's cube-map term, lit by RAW's light
    bool  clouds = true;            // Clouds
    float cloudCover = 0.35f;       // CloudCover: the cover under a clear sky; the weather's cloud report raises it
    float weatherCloud = 0.0f;      // the game's cloud cover now (WeatherDrive), 0 to 1
    float cloudBase = 46000.0f, cloudThickness = 44000.0f;   // True 3D towering cumulus slab (~650m floor, ~630m->1.3km+ vertical relief; Nubis Evolved SKY-02)
    float cloudShadow = 0.75f;      // CloudShadow: how much of the sun the thickest cloud takes
    float cloudDensity = 1.0f;      // CloudDensity
    float cloudWindX = 180.0f, cloudWindY = 60.0f;   // CloudWindX, CloudWindY: units a second
    float gameHour = 12.0f, lightning = 0.0f;        // live exterior time-of-day (0..24) & storm lightning from raw_engine.dll
    float cloudHistory = 0.72f;     // CloudHistory: full-res temporal reprojection for crisp noise-free cumulus
    bool  rtSkin = true;            // skinned actors join the scene with a BLAS a frame
    int   rtSkinCull = 1;           // RtSkinCull (0 none, 1 stretched and floating, 2 stretched only), RtSkinRatio, RtSkinMin: stretched skinned triangles left out (RtSkinCheck.hlsl)
    float rtSkinRatio = 4.0f, rtSkinMin = 24.0f;
    int   rtSkinBones = 10;         // the vertex constant buffer slot that holds the current bone palette
    bool  rtIncremental = true;     // RtIncremental: the scene's per-draw lookups once a frame, instances staged
    bool  rtCamSteps = false;       // RtCamSteps: the camera's place from the summed steps (RtScene::SetCamera)
    int   rtSkinFilter = 0;         // RtSkinFilter: diagnosis, which skinned draws join (SceneSkin.cpp)
    bool  rtSkinAlpha = true;       // RtSkinAlpha: alpha-tested skinned draws join, cut by their texture's alpha (SceneSkin.cpp, RtSkin.cpp)
    bool  rtAlphaFollow = true;     // RtAlphaFollow: a draw of an index range its prepass alpha-tested joins alpha-tested (SceneGeometryBound.cpp)
    bool  rtActorView = true;       // RtActorView: actor draws whose view-projection is not the main view's stay out (SceneSkin.cpp)
    bool  rtActors = true;          // skinned draws on the main depth target outside the opaque set (people, the player) join
    bool  rtFar = true;             // RtFar: distant LOD (class 7) gets sun shadows, sky and a bounce by a march through the depth (RtShadow.hlsl)
    bool  rtRestReuse = true;       // RtRestReuse: ReSTIR's temporal and spatial reuse for the shared ray (RtShadow.hlsl)
    bool  rtRest = true;            // RtRest: every light past the three traced ones shadowed by one ray to a light picked by its share (RtShadow.hlsl)
    bool  rtReflect = true;         // glossy surfaces: one ray-traced reflection ray, the hit's colour in the game's frame
    // Water (RtWater.hlsl), after the game composites its water: improved mode with RT on raw::tx.
    bool  water = true;             // Water
    // Pre-Pass DSLR / Medium Format Camera & DoF Optics (DofPass.hlsl) + RAWLSS 5.0 (Rawlss5Pass.hlsl):
    bool  rawlss5 = true;           // Rawlss5: native SM 6.9 FP16 3D-Guided Neural & Physical Reconstruction
    bool  dof = true;               // Dof: 5-pass 91-tap concentric physical DSLR / Medium Format cinema DoF
    float dofMaxDist = 360.0f;      // DofMaxDist: master DoF optical gain (0=off, 150..2500)
    float dofRadius = 6.5f;         // DofRadius: max bokeh aperture radius (2..18 px)
    float dofOrbBoost = 1.25f;      // DofOrbBoost: specular bokeh highlight orb boost (0..5)
    int   dofSensor = 1;            // DofSensor: 0=APS-C 24mm, 1=35mm Full-Frame, 2=MF 645 44mm, 3=MF 6x7 70mm
    float dofFocalMm = 35.0f;       // DofFocalMm: lens focal length in mm (24..200)
    float dofFStop = 4.0f;          // DofFStop: refined gameplay aperture f-stop (2.8..16.0)
    int   dofBokehShape = 2;        // DofBokehShape: 0=5-Blade, 1=6-Blade, 2=7-Blade, 3=9-Blade, 4=Circle, 5=Anamorphic, 6=Petzval Swirl
    int   cameraMode = 0;           // CameraMode: 0=Cinema Prime, 1=Tilt-Shift Miniature, 2=ARPG Isometric Diorama, 3=Anamorphic 2.39:1, 4=Petzval Swirl, 5=Vertical Tilt-Rise
    float dofNearScale = 0.55f;     // DofNearScale: near-plane blur multiplier (0..1.5)
    float dofFarScale = 0.85f;      // DofFarScale: far-plane blur multiplier (0..1.5)
    float dofNearBleed = 0.45f;     // DofNearBleed: foreground silhouette bleed/feather (0..1.0)
    float dofFocusSpeed = 0.09f;    // DofFocusSpeed: cinema follow-focus motor speed (0.02..0.30)
    bool  weatherLens = true;       // WeatherLens: procedural HDR rain-on-lens droplets & blizzard frost
    float waterReflect = 1.0f;      // WaterReflect: the ray-traced reflection's share of the Fresnel weight
    float waterCaustics = 1.0f;     // WaterCaustics
    float waterAbsorb = 0.010f;     // WaterAbsorb: extinction a game unit for green (red 2.2x, blue 0.7x); 0.004 read as glass
    float waterGlint = 1.0f;        // WaterGlint: the sun on the waves
    float waterCausticCell = 70.0f; // WaterCausticCell: game units across a cell of the caustic net
    float waterRefract = 0.65f;     // WaterRefract: RAW's refraction through the bed's colour in place of the game's water body, 0 to 1
    bool  waterBed = true;          // WaterBed: the bed's depth captured before the water (caustics, refraction)
    int   waterView = 0;            // WaterView: 1 the reflection, 2 the caustics, 3 Fresnel
    float sss = 0.75f;              // Sss: screen-space scattering on skin (Sss.hlsl), 0 off to 1
    float sssWidth = 0.5f;          // SssWidth: the red's scattering width, game units (green 0.42, blue 0.22 of it); 1.0 washed out the pores
    int   season = 0;               // Season: 0 Auto, 1 Spring, 2 Summer, 3 Autumn, 4 Winter
    int   hwTier = 3;               // HwTier: 0..4 GPU architectural tier (3060=1, 4070=2, 4090=3, 5090=4)
};

// Lights into clusters: out[cluster] holds one bit per light index.
void BuildClusters(const SceneLighting& s, float nearZ, float farZ, uint32_t (*out)[4]);

std::unique_ptr<IBridgePass> MakeLightingPass();
bool LightingPassTune(IBridgePass* pass, const LightingTuning& t);
bool LightingPassUniform(IBridgePass* pass, const void** data, size_t* bytes);   // cbuffer P as filled for this frame
struct LightingFluidInputs {
    float simTimeSec = 0.0f;
    float windXY[2] = {};
    float ambientTempC = 6.0f;
    float rainIntensity = 0.0f;
    float beaufortScale = 1.0f;
    float swellDirXY[2] = {0.866f, 0.5f};
    float waterPlaneZ = 0.0f;
    float footTrail[32][4] = {};
};
bool LightingPassFluidInputs(IBridgePass* pass, LightingFluidInputs* out);
struct ExtraShaders {
    std::vector<uint8_t> clouds, volumetric, bloomPre, bloomDown, bloomUp, bloomComp, reflFilter, rawlss5;
    std::vector<uint8_t> dofFocus, dofCoc, dofGather, dofPost, dofComp;
    std::vector<uint8_t> froxelInject, froxelBlock, froxelInt;
    std::vector<uint8_t> giAt1, giAt2, giAt4, worldSim;
};
bool LightingPassReload(IBridgePass* pass, const std::vector<uint8_t>& vs, const std::vector<uint8_t>& fs,
                        const std::vector<uint8_t>& fogFs, std::string& err, const ExtraShaders* extra = nullptr);

} // namespace raw::bridge
