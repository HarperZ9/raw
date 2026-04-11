#pragma once
//=============================================================================
//  ENBGuiIntegration.h — Native ENB Shift+Enter GUI panels
//
//  ENB's GUI is built on AntTweakBar (ATB). Boris exports the full ATB API
//  from d3d11.dll (TwNewBar, TwAddVarRO, TwDefine, etc.). We resolve these
//  at runtime and create:
//
//  1. "SkyrimBridge" bar — read-only game state (AllData) with smart widgets
//     (color swatches for RGB params, direction arrows for vectors, etc.)
//
//  2. Per-shader annotated bars — read-write params from AnnotationDatabase
//     with proper widget types per UIWidget annotation, UIGroup hierarchy,
//     and UIBinding-driven visibility.
//
//  Author: Zain Dana Harper
//  License: MIT
//=============================================================================

#include "BridgeData.h"

namespace SB
{

class ENBGuiIntegration
{
public:
    static ENBGuiIntegration& Get();

    // Resolve ATB functions from ENB's d3d11.dll
    // Call after ENBInterface::Init()
    bool Init();

    // Update the static data backing the bar's variables.
    // Also creates bars when ATB is ready, rebuilds annotated bars
    // when AnnotationDatabase changes, syncs callback values, and
    // refreshes UIBinding visibility state.
    void Update(const AllData& data);

    void Shutdown();

    bool IsAvailable() const { return m_available; }
    bool IsBarCreated() const { return m_barCreated; }
    int  GetAnnotatedBarCount() const;
    int  GetCallbackCount() const;

private:
    // AllData game-state bar
    void EnsureBar();
    void RegisterParams();

    // Per-shader annotated parameter bars
    void BuildAnnotatedBars();
    void RebuildAnnotatedBarsIfNeeded();
    void DestroyAnnotatedBars();
    void RefreshBindingState();

    // Weather editor ATB bar
    void EnsureWeatherEditorBar();
    void UpdateWeatherEditorBarLabel();
    void DestroyWeatherEditorBar();

    static constexpr int kCategoryBarCount = 6;
    void* m_bar                = nullptr;              // backward compat (first category bar)
    void* m_categoryBars[kCategoryBarCount] = {};      // themed game-state bars
    int   m_float4Type  = 0;             // Custom TwType: generic float4
    int   m_color4Type  = 0;             // Custom TwType: COLOR3F + alpha float
    int   m_dir4Type    = 0;             // Custom TwType: DIR3F + w float
    bool  m_available   = false;
    bool  m_barCreated  = false;
    int   m_retryCount  = 0;
    int   m_lastGeneration = 0;          // AnnotationDatabase generation for rebuild

    // Weather editor bar state
    void* m_weatherBar       = nullptr;
    bool  m_weatherBarCreated = false;
    uint32_t m_lastWeatherBarID = 0;
};

} // namespace SB
