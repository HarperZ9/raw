cmake_minimum_required(VERSION 3.24)

set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

project(
    RAW
    VERSION 1.0.0
    DESCRIPTION "Rendering Advancement Workshop"
    LANGUAGES CXX
)

# Copyright (c) 2026 Zain D. Harper (papacr0w). All rights reserved.

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# RAW_BUILD_PLUGIN=OFF builds only the d3d11 proxy, the D3D12 bridge and its
# test, which need nothing beyond the Windows SDK.
option(RAW_BUILD_PLUGIN "Build the SKSE plugin (needs CommonLibSSE via vcpkg)" ON)

if(RAW_BUILD_PLUGIN)
find_package(CommonLibSSE REQUIRED CONFIG)
find_package(imgui CONFIG REQUIRED)
find_package(RmlUi CONFIG REQUIRED)
find_package(Freetype REQUIRED)

# ============================================================================
# RAW — Rendering Advancement Workshop
# Clean rendering infrastructure + effects from published papers.
# ============================================================================

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR "Zain D. Harper"
    MINIMUM_SKSE_VERSION 0.0.0
    SOURCES
        # ── Core ──────────────────────────────────────────────────────
        src/core/main.cpp
        src/core/D3D11Hook.cpp
        src/core/DebugGUI.cpp
        src/core/BootDiagnostics.cpp

        # ── Mid-frame pipeline ────────────────────────────────────────
        src/core/PhaseDispatcher.cpp
        src/core/RenderPipeline.cpp
        src/core/RangeOracle.cpp
        src/core/RenderPassManager.cpp
        src/core/ComputeManager.cpp
        src/core/SceneData.cpp
        src/core/SharedGPUResources.cpp
        src/core/HiZPyramid.cpp
        src/core/SRVInjector.cpp
        src/core/ShaderLoader.cpp
        src/core/ShaderReload.cpp
        src/core/ConfigManager.cpp
        src/core/ImageSpaceTracker.cpp
        src/core/SceneObserver.cpp

        # ── GPU infrastructure ────────────────────────────────────────
        src/core/ShaderCache.cpp
        src/core/SB_ShaderDebug.cpp
        src/core/LuminanceHistogram.cpp
        src/core/LUTManager.cpp
        src/core/TAAManager.cpp
        src/core/ClusteredLighting.cpp
        src/core/MaterialClassifier.cpp
        src/core/DXBCPatcher.cpp

        # ── Diagnostics + debug visualization ─────────────────────────
        src/core/GPUProfiler.cpp
        src/core/FrameCapture.cpp
        src/core/DebugRenderer.cpp
        src/core/RmlD3D11.cpp
        src/core/NavMeshVisualizer.cpp
        src/core/SkeletonVisualizer.cpp

        # ── PostGeometry effects ──────────────────────────────────────
        src/core/GTAORenderer.cpp
        src/core/ContactShadowRenderer.cpp
        src/core/SkylightingRenderer.cpp
        src/core/SSRRenderer.cpp
        src/core/SSGIRenderer.cpp
        src/core/SceneCompositor.cpp
        src/core/DenoiseManager.cpp
        src/core/MotionVectorGen.cpp
        src/core/WeatherParameterManager.cpp
        src/core/TextureDump.cpp

        # ── PreUI effects (active) ───────────────────────────────────
        src/core/BloomRenderer.cpp
        src/core/ToneMapManager.cpp
        src/core/ColorPipeline.cpp

        # ══════════════════════════════════════════════════════════════
        # DISABLED — untested renderers removed from build.
        # Source files remain on disk. Re-enable one at a time as each
        # is polished to production quality.
        # ══════════════════════════════════════════════════════════════
        # src/core/SDSMCascades.cpp
        # src/core/GrassLightingRenderer.cpp
        # src/core/TreeLODLightingRenderer.cpp
        # src/core/WaterBlendingRenderer.cpp
        # src/core/DynamicCubemapRenderer.cpp
        # src/core/VolumetricClouds.cpp
        # src/core/VolumetricLightingRenderer.cpp
        # src/core/SubsurfaceScatteringRenderer.cpp
        # src/core/IndirectSpecularRenderer.cpp
        # src/core/ScreenSpaceDecalRenderer.cpp
        # src/core/ParticleLightingRenderer.cpp
        # src/core/DoFRenderer.cpp
        # src/core/LensRenderer.cpp
        # src/core/UnderwaterRenderer.cpp
        # src/core/AtmosphereRenderer.cpp
        # src/core/FrameGenerator.cpp
        # src/core/TemporalSuperRes.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    imgui::imgui
    RmlUi::RmlUi
    Freetype::Freetype
    d3d11
    dxgi
    d3dcompiler
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ENABLE_SKYRIM_SE=1
    ENABLE_SKYRIM_AE=1
    RAW=1
    HAS_SKSE=1
)

target_precompile_headers(${PROJECT_NAME} PRIVATE
    src/core/PCH.h
)

endif() # RAW_BUILD_PLUGIN

# ============================================================================
# d3d11.dll — RAW D3D11 Proxy
# ============================================================================

enable_language(ASM_MASM)

add_library(d3d11_proxy SHARED
    src/d3d11_proxy/proxy_main.cpp
    src/d3d11_proxy/WrappedDevice.cpp
    src/d3d11_proxy/WrappedContext.cpp
    src/d3d11_proxy/WrappedSwapChain.cpp
    src/d3d11_proxy/ShaderManager.cpp
    src/d3d11_proxy/RenderPhaseDetector.cpp
    src/d3d11_proxy/CBDirtyTracker.cpp
    src/d3d11_proxy/MaterialPipeline.cpp
    src/d3d11_proxy/AlbedoExtractor.cpp
    src/d3d11_proxy/OcclusionCuller.cpp
    src/d3d11_proxy/DepthOwnership.cpp
    src/d3d11_proxy/d3d11_trampolines.asm
    src/d3d11_proxy/d3d11_proxy.def
)

set_target_properties(d3d11_proxy PROPERTIES
    OUTPUT_NAME "d3d11"
    CXX_STANDARD 23
    CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_include_directories(d3d11_proxy PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d11_proxy
)

target_compile_definitions(d3d11_proxy PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_link_libraries(d3d11_proxy PRIVATE)

# Binding ledger (eyes substrate #1) — added 2026-06-05
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/BindingLedger.cpp)

# Frame-buffer inventory: every target the game writes in one frame (docs/FRAME_BUFFERS.md)
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/FrameLedger.cpp src/d3d11_proxy/FrameLedgerEvents.cpp
    src/d3d11_proxy/FrameLedgerDump.cpp)

if(RAW_BUILD_PLUGIN)
# Live bridge (coherence membrane Tier 0.1) — was added as a file but never built
target_sources(RAW PRIVATE src/core/LiveBridge.cpp)

# Build-freshness oracle (coherence membrane Tier 3.5) — source-hash manifest each build
add_custom_command(TARGET RAW POST_BUILD
    COMMAND python "${CMAKE_CURRENT_SOURCE_DIR}/tools/eyes/build_stamp.py" write
            "${CMAKE_CURRENT_SOURCE_DIR}/tools/eyes/build_manifest.json"
            "${CMAKE_CURRENT_SOURCE_DIR}/src" "${CMAKE_CURRENT_SOURCE_DIR}/Shaders"
    COMMENT "coherence membrane: build-freshness manifest"
    VERBATIM)
endif() # RAW_BUILD_PLUGIN

# ============================================================================
# D3D12 bridge (stage 1): RAW's own D3D12 device beside the game's D3D11 one,
# sharing textures and a fence. No dependencies beyond the Windows SDK.
# ============================================================================

find_program(RAW_FXC fxc
    HINTS "$ENV{WindowsSdkVerBinPath}/x64"
          "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64")
if(NOT RAW_FXC)
    message(FATAL_ERROR "fxc.exe not found; run from a Visual Studio developer prompt")
endif()

set(RAW_BRIDGE_GEN ${CMAKE_CURRENT_BINARY_DIR}/bridge_shaders)
set(RAW_FOG_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/DepthFog.hlsl)
set(RAW_DEPTHCOPY_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/DepthCopy.hlsl)
add_custom_command(
    OUTPUT ${RAW_BRIDGE_GEN}/DepthFog_VS.h ${RAW_BRIDGE_GEN}/DepthFog_PS.h ${RAW_BRIDGE_GEN}/DepthCopy_CS.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_BRIDGE_GEN}
    COMMAND ${RAW_FXC} /nologo /T vs_5_1 /E VSMain /O3 /Vn g_DepthFogVS /Fh ${RAW_BRIDGE_GEN}/DepthFog_VS.h ${RAW_FOG_HLSL}
    COMMAND ${RAW_FXC} /nologo /T ps_5_1 /E PSMain /O3 /Vn g_DepthFogPS /Fh ${RAW_BRIDGE_GEN}/DepthFog_PS.h ${RAW_FOG_HLSL}
    COMMAND ${RAW_FXC} /nologo /T cs_5_0 /E CSMain /O3 /Vn g_DepthCopyCS /Fh ${RAW_BRIDGE_GEN}/DepthCopy_CS.h ${RAW_DEPTHCOPY_HLSL}
    DEPENDS ${RAW_FOG_HLSL} ${RAW_DEPTHCOPY_HLSL}
    COMMENT "fxc: bridge shaders -> bytecode headers"
    VERBATIM)

add_library(raw_d3d12_bridge STATIC
    src/d3d12_bridge/D3D12Bridge.cpp
    src/d3d12_bridge/BridgeFrame.cpp
    src/d3d12_bridge/BridgeReadback.cpp
    src/d3d12_bridge/BridgeShare.cpp
    src/d3d12_bridge/DepthFogPass.cpp
    src/d3d12_bridge/BridgeSelfTest.cpp
    src/d3d12_bridge/FenceProbe.cpp
    ${RAW_BRIDGE_GEN}/DepthFog_VS.h
    ${RAW_BRIDGE_GEN}/DepthFog_PS.h
    ${RAW_BRIDGE_GEN}/DepthCopy_CS.h
)
target_include_directories(raw_d3d12_bridge
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge
    PRIVATE ${RAW_BRIDGE_GEN})
target_compile_definitions(raw_d3d12_bridge PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
# Without the engine, d3d12.dll and dxgi.dll are loaded at runtime and only the
# d3d11 import is needed. With it (RAW_ENGINE_DIR below) the proxy delay-loads both.

target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/BridgeHook.cpp src/d3d11_proxy/BridgeHookConfig.cpp
    src/d3d11_proxy/BridgeHookCapture.cpp src/d3d11_proxy/BridgeHookLighting.cpp
    src/d3d11_proxy/BridgeHookLightingStats.cpp src/d3d11_proxy/LightingSchedule.cpp)
target_link_libraries(d3d11_proxy PRIVATE raw_d3d12_bridge)

# ----------------------------------------------------------------------------
# The Playground engine behind the bridge's pass seam (docs/D3D12_BRIDGE.md).
# RAW_ENGINE_DIR is the native/ folder of a superframe checkout. With it set,
# the bridge links the engine's D3D12 RHI and its checked GTAO pass, and the
# proxy delay-loads d3d12.dll and dxgi.dll, which the RHI imports.
# ----------------------------------------------------------------------------
set(RAW_ENGINE_DIR "" CACHE PATH "superframe native/ folder; links the engine's RHI into the bridge")
if(RAW_ENGINE_DIR)
    if(NOT EXISTS "${RAW_ENGINE_DIR}/raw/rhi/host.hpp")
        message(FATAL_ERROR "RAW_ENGINE_DIR has no raw/rhi/host.hpp; it needs superframe native/ with host frames (ADR 0020)")
    endif()
    find_program(RAW_DXC dxc
        HINTS "$ENV{WindowsSdkVerBinPath}/x64"
              "C:/Program Files (x86)/Windows Kits/10/bin/10.0.26100.0/x64")
    if(NOT RAW_DXC)
        message(FATAL_ERROR "dxc.exe not found; the engine's passes are DXIL")
    endif()
    set(E ${RAW_ENGINE_DIR})
    add_library(raw_engine_rhi STATIC
        ${E}/src/rhi/d3d12/d3d12_device.cpp ${E}/src/rhi/d3d12/d3d12_timestamps.cpp
        ${E}/src/rhi/d3d12/d3d12_commands.cpp ${E}/src/rhi/d3d12/d3d12_raster.cpp
        ${E}/src/rhi/d3d12/d3d12_texture.cpp ${E}/src/rhi/d3d12/d3d12_swapchain.cpp
        ${E}/src/rhi/d3d12/d3d12_host.cpp
        ${E}/src/rhi/validate/validate_device.cpp ${E}/src/rhi/validate/validate_commands.cpp)
    target_include_directories(raw_engine_rhi PUBLIC ${E})
    target_compile_definitions(raw_engine_rhi PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(raw_engine_rhi PUBLIC d3d12 dxgi user32)

    # DXIL: the engine's own post_gtao, compiled with the flags its own build
    # uses, and the bridge's two passes around it.
    set(RAW_ENGINE_GEN ${CMAKE_CURRENT_BINARY_DIR}/engine_shaders)
    set(RAW_GTAO_HLSL ${E}/src/renderer/gpu/shaders/hlsl/post_gtao.hlsl)
    set(RAW_PACK_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/GamePack.hlsl)
    set(RAW_COMP_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/AoComposite.hlsl)
    set(RAW_DXC_FLAGS -HV 2021 -Gis -O3 -Qstrip_debug -Qstrip_reflect)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/post_gtao.h ${RAW_ENGINE_GEN}/game_pack.h
               ${RAW_ENGINE_GEN}/ao_composite_vs.h ${RAW_ENGINE_GEN}/ao_composite_fs.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_6_0 -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/post_gtao.h -Vn kDxil_post_gtao ${RAW_GTAO_HLSL}
        COMMAND ${RAW_DXC} -T cs_6_0 -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/game_pack.h -Vn kDxil_game_pack ${RAW_PACK_HLSL}
        COMMAND ${RAW_DXC} -T vs_6_0 -E vs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_composite_vs.h -Vn kDxil_ao_composite_vs ${RAW_COMP_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_0 -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_composite_fs.h -Vn kDxil_ao_composite_fs ${RAW_COMP_HLSL}
        DEPENDS ${RAW_GTAO_HLSL} ${RAW_PACK_HLSL} ${RAW_COMP_HLSL}
        COMMENT "dxc: engine pass shaders -> DXIL headers"
        VERBATIM)
    # The engine's real-time AO: its pass, its noise and its shaders (one HLSL file, five entries).
    set(RAW_AORT_HLSL ${E}/src/rhi/d3d12/hw_shaders/ao_rt.hlsl)
    set(RAW_AORT_HEADERS ${RAW_ENGINE_GEN}/ao_rt_vs.h ${RAW_ENGINE_GEN}/ao_rt_prefilter.h ${RAW_ENGINE_GEN}/ao_rt_level.h
        ${RAW_ENGINE_GEN}/ao_rt_trace.h ${RAW_ENGINE_GEN}/ao_rt_resolve.h)
    add_custom_command(
        OUTPUT ${RAW_AORT_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T vs_6_0 -E vs -DPASS=0 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_vs.h -Vn kDxil_ao_rt_vs ${RAW_AORT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_0 -E fs -DPASS=0 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_prefilter.h -Vn kDxil_ao_rt_prefilter ${RAW_AORT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_0 -E fs -DPASS=1 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_level.h -Vn kDxil_ao_rt_level ${RAW_AORT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_0 -E fs -DPASS=2 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_trace.h -Vn kDxil_ao_rt_trace ${RAW_AORT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_0 -E fs -DPASS=3 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_resolve.h -Vn kDxil_ao_rt_resolve ${RAW_AORT_HLSL}
        DEPENDS ${RAW_AORT_HLSL}
        COMMENT "dxc: real-time AO shaders -> DXIL headers"
        VERBATIM)
    target_sources(raw_d3d12_bridge PRIVATE
        src/d3d12_bridge/RealtimeAoPass.cpp ${E}/src/renderer/ao_realtime.cpp ${E}/src/renderer/ao_noise.cpp ${RAW_AORT_HEADERS}
        src/d3d12_bridge/EnginePass.cpp
        ${RAW_ENGINE_GEN}/post_gtao.h ${RAW_ENGINE_GEN}/game_pack.h
        ${RAW_ENGINE_GEN}/ao_composite_vs.h ${RAW_ENGINE_GEN}/ao_composite_fs.h)
    target_include_directories(raw_d3d12_bridge PRIVATE ${RAW_ENGINE_GEN} ${E}/src/renderer/gpu)
    target_compile_definitions(raw_d3d12_bridge PUBLIC RAW_ENGINE=1)
    target_link_libraries(raw_d3d12_bridge PUBLIC raw_engine_rhi)
    # The proxy is d3d11.dll: it must not pull d3d12.dll or dxgi.dll in at load.
    target_link_options(d3d11_proxy PRIVATE /DELAYLOAD:d3d12.dll /DELAYLOAD:dxgi.dll)
    target_link_libraries(d3d11_proxy PRIVATE delayimp)
    # engine_gtao_check: the pass behind the bridge against the engine's CPU
    # reference (raw::post::gtao), on a synthetic scene or a captured frame.
    file(GLOB RAW_ENGINE_CORE ${E}/src/core/*.cpp ${E}/src/scene/*.cpp)
    add_library(raw_engine_reference STATIC EXCLUDE_FROM_ALL
        ${E}/src/renderer/post_gtao.cpp ${E}/src/renderer/bvh.cpp ${RAW_ENGINE_CORE})
    target_include_directories(raw_engine_reference PUBLIC ${E})
    add_executable(engine_gtao_check EXCLUDE_FROM_ALL tests/engine_gtao/main.cpp tests/engine_gtao/gtao_precision.cpp)
    target_link_libraries(engine_gtao_check PRIVATE raw_d3d12_bridge raw_engine_reference d3d11)
    set_target_properties(engine_gtao_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    # ao_realtime_check: the real-time AO behind the bridge, scored against the 16 x 16 reference.
    add_executable(ao_realtime_check EXCLUDE_FROM_ALL tests/ao_realtime/main.cpp)
    target_link_libraries(ao_realtime_check PRIVATE raw_d3d12_bridge raw_engine_reference d3d11)
    set_target_properties(ao_realtime_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    message(STATUS "RAW: engine pass on, RHI from ${E}")
endif()

add_executable(bridge_roundtrip tests/bridge_roundtrip/main.cpp)
target_link_libraries(bridge_roundtrip PRIVATE raw_d3d12_bridge d3d11)
# Own folder: next to the proxy, the test would load our d3d11.dll and get a
# wrapped device instead of the system one.
set_target_properties(bridge_roundtrip PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)

# ----------------------------------------------------------------------------
# G-buffer completion (docs/FRAME_BUFFERS.md, "Shader patches"): the DXBC
# patcher as its own library, and its check against known scenes.
# ----------------------------------------------------------------------------
add_library(raw_dxbc STATIC
    src/core/dxbc/DxbcContainer.cpp
    src/core/dxbc/DxbcProgram.cpp
    src/core/dxbc/GBufferAnalyse.cpp
    src/core/dxbc/GBufferPatch.cpp)
set_target_properties(raw_dxbc PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
add_executable(gbuffer_patch_check tests/gbuffer_patch/main.cpp)
set_target_properties(gbuffer_patch_check PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
target_compile_definitions(gbuffer_patch_check PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(gbuffer_patch_check PRIVATE raw_dxbc d3d11 d3dcompiler)
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/GBufferComplete.cpp src/d3d11_proxy/GBufferVariants.cpp)
target_link_libraries(d3d11_proxy PRIVATE raw_dxbc)

# Stencil and 3D volumes across the bridge (BridgeShareMore.cpp).
set(RAW_SHARE_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders)
add_custom_command(
    OUTPUT ${RAW_BRIDGE_GEN}/StencilCopy_CS.h ${RAW_BRIDGE_GEN}/VolumeCopy_CS.h
    COMMAND ${RAW_FXC} /nologo /T cs_5_0 /E CSMain /O3 /Vn g_StencilCopyCS /Fh ${RAW_BRIDGE_GEN}/StencilCopy_CS.h ${RAW_SHARE_HLSL}/StencilCopy.hlsl
    COMMAND ${RAW_FXC} /nologo /T cs_5_0 /E CSMain /O3 /Vn g_VolumeCopyCS /Fh ${RAW_BRIDGE_GEN}/VolumeCopy_CS.h ${RAW_SHARE_HLSL}/VolumeCopy.hlsl
    DEPENDS ${RAW_SHARE_HLSL}/StencilCopy.hlsl ${RAW_SHARE_HLSL}/VolumeCopy.hlsl
    COMMENT "fxc: stencil and volume copies -> bytecode headers"
    VERBATIM)
target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/BridgeShareMore.cpp src/d3d12_bridge/Probe.cpp
    ${RAW_BRIDGE_GEN}/StencilCopy_CS.h ${RAW_BRIDGE_GEN}/VolumeCopy_CS.h)

# The engine's deferred lighting of the game frame (docs/LIGHTING.md): the proxy decodes the
# frame's lights (SceneLights.cpp), the bridge shares the surface targets and the sun cascades
# (BridgeLighting.cpp), and with the engine linked the pass lights the frame (LightingPass.cpp).
add_custom_command(
    OUTPUT ${RAW_BRIDGE_GEN}/ShadowAtlas_CS.h
    COMMAND ${RAW_FXC} /nologo /T cs_5_0 /E CSMain /O3 /Vn g_ShadowAtlasCS /Fh ${RAW_BRIDGE_GEN}/ShadowAtlas_CS.h ${RAW_SHARE_HLSL}/ShadowAtlas.hlsl
    DEPENDS ${RAW_SHARE_HLSL}/ShadowAtlas.hlsl
    COMMENT "fxc: shadow atlas copy -> bytecode header"
    VERBATIM)
target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/BridgeLighting.cpp src/d3d12_bridge/LightClusters.cpp
    ${RAW_BRIDGE_GEN}/ShadowAtlas_CS.h)
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/SceneLights.cpp src/d3d11_proxy/GameComposite.cpp)
if(RAW_ENGINE_DIR)
    set(RAW_LIGHT_HLSL ${RAW_SHARE_HLSL}/Lighting.hlsl)
    set(RAW_LIGHTFOG_HLSL ${RAW_SHARE_HLSL}/LightFog.hlsl)
    set(RAW_LIGHT_HEADERS ${RAW_ENGINE_GEN}/light_vs.h ${RAW_ENGINE_GEN}/light_fs.h ${RAW_ENGINE_GEN}/light_fog_fs.h)
    add_custom_command(
        OUTPUT ${RAW_LIGHT_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T vs_6_0 -E vs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/light_vs.h -Vn kDxil_light_vs ${RAW_LIGHT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_0 -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/light_fs.h -Vn kDxil_light_fs ${RAW_LIGHT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_0 -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/light_fog_fs.h -Vn kDxil_light_fog_fs ${RAW_LIGHTFOG_HLSL}
        DEPENDS ${RAW_LIGHT_HLSL} ${RAW_LIGHTFOG_HLSL} ${RAW_SHARE_HLSL}/LightingCommon.hlsli
        COMMENT "dxc: lighting pass shaders -> DXIL headers"
        VERBATIM)
    target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/LightingPass.cpp ${RAW_LIGHT_HEADERS})
endif()

# ============================================================================
# D3D11-to-D3D12 translation layer (stage 2): ID3D11Device and
# ID3D11DeviceContext implemented on a D3D12 device. Windows SDK only.
# ============================================================================

set(RAW_TX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/dx12/translate)
set(RAW_TX_GEN ${CMAKE_CURRENT_BINARY_DIR}/tx_shaders)
set(RAW_MIPGEN_HLSL ${RAW_TX_DIR}/shaders/MipGen.hlsl)
add_custom_command(
    OUTPUT ${RAW_TX_GEN}/MipGen_VS.h ${RAW_TX_GEN}/MipGen_PS.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_TX_GEN}
    COMMAND ${RAW_FXC} /nologo /T vs_5_0 /E VSMain /O3 /Vn g_MipGenVS /Fh ${RAW_TX_GEN}/MipGen_VS.h ${RAW_MIPGEN_HLSL}
    COMMAND ${RAW_FXC} /nologo /T ps_5_0 /E PSMain /O3 /Vn g_MipGenPS /Fh ${RAW_TX_GEN}/MipGen_PS.h ${RAW_MIPGEN_HLSL}
    DEPENDS ${RAW_MIPGEN_HLSL}
    COMMENT "fxc: translation layer shaders -> bytecode headers"
    VERBATIM)

add_library(raw_dx12_translate STATIC
    ${RAW_TX_DIR}/tx_alloc.cpp
    ${RAW_TX_DIR}/tx_track.cpp
    ${RAW_TX_DIR}/tx_imports.cpp
    ${RAW_TX_DIR}/tx_device.cpp
    ${RAW_TX_DIR}/tx_dxgi_device.cpp
    ${RAW_TX_DIR}/tx_host.cpp
    ${RAW_TX_DIR}/tx_submit.cpp
    ${RAW_TX_DIR}/tx_resource.cpp
    ${RAW_TX_DIR}/tx_views.cpp
    ${RAW_TX_DIR}/tx_reflect.cpp
    ${RAW_TX_DIR}/tx_state.cpp
    ${RAW_TX_DIR}/tx_pipeline.cpp
    ${RAW_TX_DIR}/tx_swapchain.cpp
    ${RAW_TX_DIR}/tx_ctx_core.cpp
    ${RAW_TX_DIR}/tx_ctx_bind.cpp
    ${RAW_TX_DIR}/tx_ctx_compute.cpp
    ${RAW_TX_DIR}/tx_query.cpp
    ${RAW_TX_DIR}/tx_ctx_get.cpp
    ${RAW_TX_DIR}/tx_ctx_draw.cpp
    ${RAW_TX_DIR}/tx_ctx_data.cpp
    ${RAW_TX_DIR}/tx_ctx_copy.cpp
    ${RAW_TX_DIR}/tx_ctx_misc.cpp
    ${RAW_TX_GEN}/MipGen_VS.h
    ${RAW_TX_GEN}/MipGen_PS.h
)
target_include_directories(raw_dx12_translate PUBLIC ${RAW_TX_DIR} PRIVATE ${RAW_TX_GEN})
target_compile_definitions(raw_dx12_translate PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(raw_dx12_translate PUBLIC dxguid)
# The translate mode (docs/TX_IN_GAME.md): the game on raw::tx instead of the system runtime.
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/TxMode.cpp src/d3d11_proxy/TxHarness.cpp)
# Hot reload (docs/HOT_RELOAD.md): shaders and parameters while the game runs.
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/HotReload.cpp src/d3d11_proxy/Overlay.cpp)
# Ray tracing on the game's geometry (docs/RAYTRACING.md), on raw::tx's device.
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/SceneGeometry.cpp)
if(RAW_ENGINE_DIR)
    set(RAW_RTSHADOW_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtShadow.hlsl)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_shadow.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_6_5 -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_shadow.h -Vn kDxil_rt_shadow ${RAW_RTSHADOW_HLSL}
        DEPENDS ${RAW_RTSHADOW_HLSL} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/LightingCommon.hlsli
        COMMENT "dxc: ray-traced shadows -> DXIL header"
        VERBATIM)
    target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/RtScene.cpp ${RAW_ENGINE_GEN}/rt_shadow.h)
endif()
target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/ShaderCompiler.cpp)
target_link_libraries(d3d11_proxy PRIVATE raw_dx12_translate)
# A map file and a PDB beside d3d11.dll, so a fault's module offset in the log names a function.
if(MSVC)
    target_compile_options(d3d11_proxy PRIVATE /Zi)
    target_compile_options(raw_dx12_translate PRIVATE /Zi)
    target_link_options(d3d11_proxy PRIVATE /DEBUG /OPT:REF /OPT:ICF /MAP)
endif()
if(MSVC)
    target_compile_options(raw_dx12_translate PRIVATE /W4)
endif()

# Call-stream recorder/replayer and the offline proof (no game involved).
add_subdirectory(src/dx12/translate/record)
add_subdirectory(tests/d3d11_translate)
add_executable(d3d11_translate_suite
    tests/d3d11_translate/main.cpp
    tests/d3d11_translate/suite_controls.cpp
    tests/d3d11_translate/suite_getters.cpp
    tests/d3d11_translate/suite_replay.cpp
    tests/d3d11_translate/suite_bench.cpp)
target_link_libraries(d3d11_translate_suite PRIVATE tx_scenes raw_dx12_translate raw_d3d11_record d3d11 d3dcompiler dxguid)
target_compile_definitions(d3d11_translate_suite PRIVATE _CRT_SECURE_NO_WARNINGS)
# Own folder: next to the proxy the exe would load RAW's d3d11.dll.
set_target_properties(d3d11_translate_suite PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
