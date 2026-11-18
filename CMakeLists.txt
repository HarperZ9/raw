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
# d3d12.dll and dxgi.dll are loaded at runtime; only the d3d11 import is needed.

target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/BridgeHook.cpp)
target_link_libraries(d3d11_proxy PRIVATE raw_d3d12_bridge)

add_executable(bridge_roundtrip tests/bridge_roundtrip/main.cpp)
target_link_libraries(bridge_roundtrip PRIVATE raw_d3d12_bridge d3d11)
# Own folder: next to the proxy, the test would load our d3d11.dll and get a
# wrapped device instead of the system one.
set_target_properties(bridge_roundtrip PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)

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
