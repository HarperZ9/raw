//=============================================================================
//  main.cpp — SkyrimBridge v3.0 frame callback integration
//
//  This shows how Phases 2-4 wire into the existing per-frame update loop.
//  Drop-in additions to the existing SkyrimBridge main.cpp.
//
//  Author: Zain Dana Harper
//=============================================================================

#include "BridgeData.h"
#include "Trackers.h"
#include "ENBInterface.h"

// Phase 2: Weather parameter computer
#include "WeatherParameterComputer.h"

// Phase 3: Shared memory bridge
#include "SharedMemoryBridge.h"

// Phase 4: ParmLink compatibility layer
#include "ParmLinkCompat.h"


namespace SB
{

//=============================================================================
//  OnGameDataReady — one-time initialization after game data is loaded
//=============================================================================

void OnGameDataReady()
{
    // Existing tracker initialization...

    // ─── Phase 2: Initialize weather parameter computer ─────────────
    auto configDir = std::filesystem::path("Data/SKSE/Plugins/SkyrimBridge");
    WeatherParameterComputer::Get().Initialize(configDir);

    // ─── Phase 3: Initialize shared memory bridge ───────────────────
    SharedMemoryBridge::Get().Initialize();

    // ─── Phase 4: Initialize ParmLink compatibility layer ───────────
    // Looks for enbParmLink.cfg or SkyrimBridge_ParmLink.cfg in game root
    auto gameDir = std::filesystem::current_path();
    ParmLinkCompat::Get().Initialize(gameDir);

    SKSE::log::info("SkyrimBridge v3.0: all phases initialized");
}


//=============================================================================
//  OnENBFrame — called by ENB every frame via callback
//=============================================================================

void OnENBFrame(float deltaTime)
{
    // ─────────────────────────────────────────────────────────────────
    //  EXISTING: Collect all tracker data
    // ─────────────────────────────────────────────────────────────────

    auto& data = AllData::GetMutable();

    data.celestial  = CelestialTracker::Update();
    data.atmosphere  = AtmosphereTracker::Update();
    data.fog         = FogTracker::Update();
    data.weather     = WeatherTracker::Update(deltaTime);
    data.player      = PlayerTracker::Update();
    data.camera      = CameraTracker::Update();
    data.interior    = InteriorTracker::Update();
    data.shadow      = ShadowTracker::Update();
    data.effects     = EffectsTracker::Update();
    data.render      = RenderTracker::Update(deltaTime);

    // v2 expansion trackers (domains 11-17) — all enabled
    data.imageSpace  = ImageSpaceTracker::Update();
    data.lights      = LightTracker::Update();
    data.actorValues = ActorValueTracker::Update();
    data.crosshair   = CrosshairTracker::Update();
    data.equipment   = EquipmentTracker::Update();
    data.quests      = QuestTracker::Update();
    data.uiState     = UIStateTracker::Update();


    // ─────────────────────────────────────────────────────────────────
    //  EXISTING: Push all float4 parameters to ENB
    // ─────────────────────────────────────────────────────────────────

    ENBInterface::PushAllParameters(data);


    // ─────────────────────────────────────────────────────────────────
    //  PHASE 2: Weather parameter computation
    // ─────────────────────────────────────────────────────────────────
    //
    //  Reads current weather state, classifies it, interpolates
    //  per-parameter values from WeatherParams.ini, pushes to ENB.
    //  This replaces what ParmLink expressions did — but in native C++.
    //
    WeatherParameterComputer::Get().Update(deltaTime);


    // ─────────────────────────────────────────────────────────────────
    //  PHASE 3: Write to shared memory for external tools
    // ─────────────────────────────────────────────────────────────────
    //
    //  Copies AllData + weather params to a named shared memory region.
    //  External apps (OBS, iCUE, stream tools) read this without
    //  touching the game process.
    //
    if (SharedMemoryBridge::Get().IsActive())
    {
        uint32_t frameCount = static_cast<uint32_t>(data.render.Frame.x);
        SharedMemoryBridge::Get().WriteFrame(data, deltaTime, frameCount);
    }


    // ─────────────────────────────────────────────────────────────────
    //  PHASE 4: ParmLink expression evaluation
    // ─────────────────────────────────────────────────────────────────
    //
    //  Evaluates enbParmLink.cfg-compatible expressions using
    //  SkyrimBridge data instead of raw memory reads.
    //  Drop-in replacement: just remove enbParmLink.dll and the
    //  expressions still work via SkyrimBridge.
    //
    ParmLinkCompat::Get().Update(deltaTime);
}


//=============================================================================
//  OnGameShutdown — cleanup
//=============================================================================

void OnGameShutdown()
{
    ParmLinkCompat::Get().Shutdown();
    SharedMemoryBridge::Get().Shutdown();
    WeatherParameterComputer::Get().Shutdown();
}

}  // namespace SB
