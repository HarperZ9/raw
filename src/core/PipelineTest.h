#pragma once
//=============================================================================
//  PipelineTest — Experimental render passes via RenderPassManager + Pipeline
//
//  Two test passes registered at PrePresent:
//    1. Vignette: Radial darkening at screen edges
//    2. Film Grain: Animated procedural noise overlay
//
//  Both read the backbuffer as SRV input and write back — demonstrating
//  fullscreen pass compilation, backbuffer I/O, CB binding, and pipeline
//  orchestration in a single self-contained module.
//
//  Toggle individually via SetVignetteEnabled / SetFilmGrainEnabled.
//=============================================================================

namespace SB::PipelineTest
{
    /// Initialize test passes. Call after RenderPipeline::Initialize.
    void Initialize();

    /// Tear down (releases D3D11 resources and removes pipeline passes).
    void Shutdown();

    bool IsInitialized();

    void SetVignetteEnabled(bool enabled);
    void SetFilmGrainEnabled(bool enabled);
    bool IsVignetteEnabled();
    bool IsFilmGrainEnabled();
}
