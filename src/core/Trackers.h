#pragma once
//=============================================================================
//  Trackers.h — Combined tracker headers for all game state trackers
//=============================================================================

#include "BridgeData.h"

namespace SB
{
    // Phase 1: Core trackers (domains 1-10)
    namespace CelestialTracker  { CelestialData  Update(); }
    namespace AtmosphereTracker { AtmosphereData Update(); }
    namespace FogTracker        { FogData        Update(); }
    namespace WeatherTracker    { WeatherData    Update(float a_deltaTime); }
    namespace PlayerTracker     { PlayerData     Update(); }
    namespace CameraTracker     { CameraData     Update(); }
    namespace InteriorTracker   { InteriorData   Update(); }
    namespace ShadowTracker     { ShadowData     Update(); }
    namespace EffectsTracker    { EffectsData    Update(); }
    namespace RenderTracker     { RenderData     Update(float a_deltaTime); }

    // v2 expansion trackers (domains 11-17)
    namespace ImageSpaceTracker { ImageSpaceData Update(); }
    namespace LightTracker      { LightData      Update(); }
    namespace ActorValueTracker { ActorValueData Update(); }
    namespace CrosshairTracker  { CrosshairData  Update(); }
    namespace EquipmentTracker  { EquipmentData  Update(); }
    namespace QuestTracker      { QuestData      Update(); }
    namespace UIStateTracker    { UIStateData    Update(); }
}
