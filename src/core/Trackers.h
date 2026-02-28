#pragma once
//=============================================================================
//  Trackers.h — Combined tracker headers for all game state trackers
//=============================================================================

#include "BridgeData.h"

namespace SB
{
    namespace CelestialTracker { CelestialData Update(); }
    namespace AtmosphereTracker { AtmosphereData Update(); }
    namespace FogTracker { FogData Update(); }
    namespace WeatherTracker { WeatherData Update(float a_deltaTime); }
    namespace PlayerTracker { PlayerData Update(float a_deltaTime); }
    namespace CameraTracker { CameraData Update(); }
    namespace InteriorTracker { InteriorData Update(); }
    namespace ShadowTracker { ShadowData Update(); }
    namespace EffectsTracker { EffectsData Update(); }
    namespace RenderTracker { RenderData Update(float a_deltaTime); }
}
