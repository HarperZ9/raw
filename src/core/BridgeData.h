#pragma once
//=============================================================================
//  BridgeData.h — Minimal stub for game-state data structures
//
//  Original BridgeData.h defined 22 tracker domain structs + AllData.
//  This stub provides the minimum definitions needed by remaining systems:
//    - Float4, AllData, per-domain data structs
//    - kParamTable, kParamCount for SanitizeAllData
//=============================================================================

#include <cstddef>
#include <cstdint>

namespace SB
{

// ── Core vector type ────────────────────────────────────────────────────
struct Float4
{
    float x = 0.f, y = 0.f, z = 0.f, w = 0.f;
};

// ── Per-domain data structs ─────────────────────────────────────────────
// These are the structs that tracker Update() functions return.
// Each contains Float4 members matching what the trackers populated.

struct CelestialData
{
    Float4 SunDirection;
    Float4 SunColor;
    Float4 MoonDirection;
    Float4 MoonPhase;
    Float4 StarBrightness;
};

struct AtmosphereData
{
    Float4 Inscatter;
    Float4 Ambient;
    Float4 FogColor;
    Float4 SkyColor;
};

struct FogData
{
    Float4 Params;          // near, far, power, maxOpacity
    Float4 Color;
    Float4 SkyParams;
    Float4 InteriorParams;
};

struct WeatherData
{
    Float4 Wind;
    Float4 Precipitation;
    Float4 Classification;
    Float4 Transition;
    Float4 PrevWeatherFlags;
};

struct PlayerData
{
    Float4 Position;
    Float4 Rotation;
    Float4 Stats;
    Float4 State;
};

struct CameraData
{
    Float4 WorldPos;
    Float4 Params;          // fov, near, far, aspect
    Float4 ViewRow0;
    Float4 ViewRow1;
    Float4 ViewRow2;
    Float4 PrevViewRow0;
    Float4 PrevViewRow1;
    Float4 PrevWorldPos;
};

struct InteriorData
{
    Float4 Flags;
    Float4 AmbientColor;
    Float4 FogParams;
    Float4 DirectionalAmbient;
};

struct ShadowData
{
    Float4 Params;
    Float4 Color;
    Float4 Direction;
    Float4 FadeParams;
};

struct EffectsData
{
    Float4 ActiveEffects;
    Float4 ScreenEffects;
    Float4 Timers;
    Float4 Modifiers;
};

struct RenderData
{
    Float4 FrameInfo;       // frameCount, dt, width, height
    Float4 Jitter;          // TAA jitter
    Float4 Resolution;
    Float4 Viewport;
};

struct ImageSpaceData
{
    Float4 HDR;
    Float4 Cinematic;
    Float4 CineTint;
    Float4 DOF;
    Float4 IMOD;
};

struct LightData
{
    Float4 SunParams;
    Float4 AmbientParams;
    Float4 PointLightSummary;
    Float4 ShadowLightSummary;
};

struct ActorValueData
{
    Float4 Health;
    Float4 Magicka;
    Float4 Stamina;
    Float4 Combat;
};

struct CrosshairData
{
    Float4 Target;
    Float4 Position;
    Float4 Info;
    Float4 Reserved;
};

struct EquipmentData
{
    Float4 RightHand;
    Float4 LeftHand;
    Float4 Armor;
    Float4 Shout;
};

struct QuestData
{
    Float4 ActiveQuest;
    Float4 Objectives;
    Float4 Reserved1;
    Float4 Reserved2;
};

struct UIStateData
{
    Float4 MenuFlags;
    Float4 HUDState;
    Float4 DialogState;
    Float4 Reserved;
};

struct RegionData
{
    Float4 Current;
    Float4 Weather;
    Float4 Sound;
    Float4 Reserved;
};

struct AudioData
{
    Float4 Music;
    Float4 Ambient;
    Float4 Effects;
    Float4 Reserved;
};

struct NPCDetectData
{
    Float4 Nearby;
    Float4 Hostiles;
    Float4 Reserved1;
    Float4 Reserved2;
};

struct PerfData
{
    Float4 FrameTiming;
    Float4 GPUTiming;
    Float4 Budgets;
    Float4 Quality;
};

struct SceneData
{
    Float4 MaterialCounts1;
    Float4 MaterialCounts2;
    Float4 DrawStats;
    Float4 MaterialProps1;
    Float4 MaterialProps2;
    Float4 ShaderFlags;
    Float4 GeometryInfo;
    Float4 CharLight;
    Float4 AmbientSpec;
    Float4 EngineState;
    Float4 EngineTimers;
    Float4 DirAmbient1;
    Float4 DirAmbient2;
    Float4 DirAmbient3;
    Float4 SunGlare;
    Float4 WaterPlane;
    Float4 WaterColor;
    Float4 WaterParams;
    Float4 WaterWave;
    Float4 EffectShader;
    Float4 EffectColor;
};

// ── Aggregate struct ────────────────────────────────────────────────────
struct AllData
{
    CelestialData   celestial;
    AtmosphereData  atmosphere;
    FogData         fog;
    WeatherData     weather;
    PlayerData      player;
    CameraData      camera;
    InteriorData    interior;
    ShadowData      shadow;
    EffectsData     effects;
    RenderData      render;
    ImageSpaceData  imageSpace;
    LightData       lights;
    ActorValueData  actorValues;
    CrosshairData   crosshair;
    EquipmentData   equipment;
    QuestData       quest;
    UIStateData     uiState;
    RegionData      region;
    AudioData       audio;
    NPCDetectData   npcDetect;
    PerfData        perf;
    SceneData       scene;
};

// ── Parameter table (for SanitizeAllData) ─────────
struct ParamEntry
{
    const char* name;
    std::size_t offset;
};

// Total Float4 count: each domain struct has a fixed number of Float4 members.
// 5+4+4+5+4+8+4+4+4+4+5+4+4+4+4+4+4+4+4+4+4+21 = 110 Float4s
inline constexpr std::size_t kParamCount = 110;

// Generate param table at compile time — one entry per Float4 field
// For simplicity, provide a minimal table that covers AllData sequentially
inline const ParamEntry kParamTable[kParamCount] = {
    // Generate entries with offsets into AllData, stride 16 bytes (sizeof(Float4))
    #define SB_PARAM(idx) { "param_" #idx, (idx) * sizeof(Float4) }
    SB_PARAM(0),  SB_PARAM(1),  SB_PARAM(2),  SB_PARAM(3),  SB_PARAM(4),
    SB_PARAM(5),  SB_PARAM(6),  SB_PARAM(7),  SB_PARAM(8),  SB_PARAM(9),
    SB_PARAM(10), SB_PARAM(11), SB_PARAM(12), SB_PARAM(13), SB_PARAM(14),
    SB_PARAM(15), SB_PARAM(16), SB_PARAM(17), SB_PARAM(18), SB_PARAM(19),
    SB_PARAM(20), SB_PARAM(21), SB_PARAM(22), SB_PARAM(23), SB_PARAM(24),
    SB_PARAM(25), SB_PARAM(26), SB_PARAM(27), SB_PARAM(28), SB_PARAM(29),
    SB_PARAM(30), SB_PARAM(31), SB_PARAM(32), SB_PARAM(33), SB_PARAM(34),
    SB_PARAM(35), SB_PARAM(36), SB_PARAM(37), SB_PARAM(38), SB_PARAM(39),
    SB_PARAM(40), SB_PARAM(41), SB_PARAM(42), SB_PARAM(43), SB_PARAM(44),
    SB_PARAM(45), SB_PARAM(46), SB_PARAM(47), SB_PARAM(48), SB_PARAM(49),
    SB_PARAM(50), SB_PARAM(51), SB_PARAM(52), SB_PARAM(53), SB_PARAM(54),
    SB_PARAM(55), SB_PARAM(56), SB_PARAM(57), SB_PARAM(58), SB_PARAM(59),
    SB_PARAM(60), SB_PARAM(61), SB_PARAM(62), SB_PARAM(63), SB_PARAM(64),
    SB_PARAM(65), SB_PARAM(66), SB_PARAM(67), SB_PARAM(68), SB_PARAM(69),
    SB_PARAM(70), SB_PARAM(71), SB_PARAM(72), SB_PARAM(73), SB_PARAM(74),
    SB_PARAM(75), SB_PARAM(76), SB_PARAM(77), SB_PARAM(78), SB_PARAM(79),
    SB_PARAM(80), SB_PARAM(81), SB_PARAM(82), SB_PARAM(83), SB_PARAM(84),
    SB_PARAM(85), SB_PARAM(86), SB_PARAM(87), SB_PARAM(88), SB_PARAM(89),
    SB_PARAM(90), SB_PARAM(91), SB_PARAM(92), SB_PARAM(93), SB_PARAM(94),
    SB_PARAM(95), SB_PARAM(96), SB_PARAM(97), SB_PARAM(98), SB_PARAM(99),
    SB_PARAM(100), SB_PARAM(101), SB_PARAM(102), SB_PARAM(103), SB_PARAM(104),
    SB_PARAM(105), SB_PARAM(106), SB_PARAM(107), SB_PARAM(108), SB_PARAM(109),
    #undef SB_PARAM
};

} // namespace SB
