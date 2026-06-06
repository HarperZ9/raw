#pragma once
//=============================================================================
//  DebugGUI.h — RAW ImGui debug overlay
//
//  Displays live game state data, matrices, and debug controls in an
//  ImGui window accessible via INSERT key.
//=============================================================================

#include "BridgeData.h"

namespace SB::DebugGUI
{
    // Initialize the debug GUI system
    void Init();

    // Shutdown and cleanup
    void Shutdown();

    // Called each frame to render the debug window
    // This should be called from the Present hook callback
    void Render();

    // Update the data to display (call before Render)
    void SetData(const AllData& a_data);

    // Debug control getters
    bool IsDataPushEnabled();
    bool IsTrackerEnabled(const char* a_name);

    // Toggle data push on/off
    void SetDataPushEnabled(bool a_enabled);

    // Tracker health info (fed from main.cpp's TrackerHealth system)
    struct TrackerHealthInfo
    {
        const char* name;
        int  consecutiveErrors;
        int  totalErrors;
        bool disabled;
    };
    void SetTrackerHealth(const TrackerHealthInfo* a_info, int a_count);

    // Shader pre-processor stats
    struct PreProcessorStats
    {
        int processCount;
        int cacheHits;
        int parameterCount;
        int separatedCount;
        int shaderCount;
        int externBindingCount;
        int externPushCount;
        int weatherSepCount;
        int weatherINICount;
    };
    void SetPreProcessorStats(const PreProcessorStats& a_stats);
}
