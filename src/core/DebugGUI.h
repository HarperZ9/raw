#pragma once
//=============================================================================
//  DebugGUI.h — ENB ImGui debug window for SkyrimBridge
//
//  Displays live game state data, matrices, and debug controls in an
//  ENB-integrated ImGui window accessible via the ENB GUI (Shift+Enter).
//=============================================================================

#include "BridgeData.h"

namespace SB::DebugGUI
{
    // Initialize the debug GUI system
    void Init();

    // Shutdown and cleanup
    void Shutdown();

    // Called each frame to render the debug window
    // This should be called from the ENB GUI callback
    void Render();

    // Update the data to display (call before Render)
    void SetData(const AllData& a_data);

    // Debug control getters
    bool IsDataPushEnabled();
    bool IsTrackerEnabled(const char* a_name);

    // Toggle data push on/off
    void SetDataPushEnabled(bool a_enabled);
}
