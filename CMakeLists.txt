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

# The DirectX 12 Agility SDK's headers (its build/native folder), for DXR 1.2 opacity micromaps, shader
# execution reordering and shader model 6.9. Empty: the Windows SDK's headers. The runtime itself is
# loaded at run time from [Translate] AgilityPath (docs/TX_IN_GAME.md); no SDK binary is built in.
set(RAW_AGILITY_DIR "" CACHE PATH "DirectX 12 Agility SDK build/native folder")
if(RAW_AGILITY_DIR)
    include_directories(BEFORE SYSTEM ${RAW_AGILITY_DIR}/include)
    add_compile_definitions(RAW_HAS_OMM=1)
endif()

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
    src/d3d11_proxy/ModuleHost.cpp
    src/d3d11_proxy/WrappedDevice.cpp
    src/d3d11_proxy/WrappedContext.cpp
    src/d3d11_proxy/WrappedContextDraw.cpp
    src/d3d11_proxy/WrappedContextOM.cpp
    src/d3d11_proxy/WrappedContextBind.cpp
    src/d3d11_proxy/WrappedContextData.cpp
    src/d3d11_proxy/WrappedContextGet.cpp
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

target_link_libraries(d3d11_proxy PRIVATE windowscodecs ole32)

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
    src/d3d12_bridge/HardwareProfile.cpp
    src/d3d12_bridge/StreamlineHost.cpp
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
# The proxy links the engine (RAW_ENGINE_DIR below) and delay-loads d3d12.dll and dxgi.dll.

target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/BridgeHook.cpp src/d3d11_proxy/BridgeHookConfig.cpp
    src/d3d11_proxy/BridgeHookCapture.cpp src/d3d11_proxy/BridgeHookLighting.cpp
    src/d3d11_proxy/BridgeHookLightingStats.cpp src/d3d11_proxy/LightingSchedule.cpp
    src/d3d11_proxy/WeatherDrive.cpp)
target_link_libraries(d3d11_proxy PRIVATE raw_d3d12_bridge)
set(RAW_STREAMLINE_DIR "" CACHE PATH "Optional path to NVIDIA Streamline SDK (with include/sl.h)")
if(RAW_STREAMLINE_DIR AND EXISTS "${RAW_STREAMLINE_DIR}/include/sl.h")
    target_include_directories(raw_d3d12_bridge PRIVATE "${RAW_STREAMLINE_DIR}/include")
    target_compile_definitions(raw_d3d12_bridge PRIVATE RAW_HAS_STREAMLINE=1)
    message(STATUS "RAW: NVIDIA Streamline SDK enabled from ${RAW_STREAMLINE_DIR}")
endif()

# ----------------------------------------------------------------------------
# The Playground engine behind the bridge's pass seam (docs/D3D12_BRIDGE.md).
# RAW_ENGINE_DIR is the native/ folder of a superframe checkout, and the proxy
# needs it: the bridge, the hook layer and hot reload call the engine's lighting,
# ray-tracing and water passes directly, so a build without it cannot link
# d3d11.dll. With it the bridge links the engine's D3D12 RHI and passes, and the
# proxy delay-loads d3d12.dll and dxgi.dll, which the RHI imports.
# ----------------------------------------------------------------------------
set(RAW_ENGINE_DIR "" CACHE PATH "superframe native/ folder (required): the engine the bridge and proxy link")
if(NOT RAW_ENGINE_DIR)
    message(FATAL_ERROR "RAW_ENGINE_DIR is not set. d3d11.dll links the Playground engine: pass "
                        "-DRAW_ENGINE_DIR=<superframe checkout>/native (docs/D3D12_BRIDGE.md, Build).")
endif()
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
    # Optional next-gen DXC (1.9+ / SM 6.9 & SM 6.10): prefer official Microsoft DXC 1.9+ with retail-signed dxil.dll
    # before Vulkan SDK DXC, so emitted SM 6.9 DXIL 1.9 binaries execute live on retail D3D12 hardware without Dev Mode.
    file(GLOB _RAW_MS_DXC19_DIRS "H:/dxc-1.9*/bin/x64")
    file(GLOB _RAW_MS_DXC110_DIRS "H:/dxc-1.10*/bin/x64")
    file(GLOB _RAW_VK_DXC_DIRS "C:/VulkanSDK/*/Bin")
    find_program(RAW_DXC_NEXT dxc
        HINTS ${_RAW_MS_DXC19_DIRS} "$ENV{VULKAN_SDK}/Bin" ${_RAW_VK_DXC_DIRS}
        NO_DEFAULT_PATH)
    find_program(RAW_DXC_610 dxc
        HINTS ${_RAW_MS_DXC110_DIRS}
        NO_DEFAULT_PATH)
    set(RAW_HAS_SM69_DXIL OFF)
    if(RAW_DXC_NEXT)
        execute_process(COMMAND "${RAW_DXC_NEXT}" --version
            OUTPUT_VARIABLE _RAW_DXC_NEXT_VER ERROR_VARIABLE _RAW_DXC_NEXT_ERR
            OUTPUT_STRIP_TRAILING_WHITESPACE)
        if("${_RAW_DXC_NEXT_VER} ${_RAW_DXC_NEXT_ERR}" MATCHES "dxcompiler\\.dll: 1\\.(9|1[0-9])")
            set(RAW_HAS_SM69_DXIL ON)
            get_filename_component(_RAW_DXC_NEXT_DIR "${RAW_DXC_NEXT}" DIRECTORY)
            if(EXISTS "${_RAW_DXC_NEXT_DIR}/dxil.dll")
                set(_RAW_DXIL_SIGN_NOTE "retail-signed dxil.dll")
            else()
                set(_RAW_DXIL_SIGN_NOTE "unsigned dxil")
            endif()
            message(STATUS "RAW: next-gen DXC detected (${RAW_DXC_NEXT}, ${_RAW_DXIL_SIGN_NOTE}) -> dual SM 6.6 + native SM 6.9 DXIL 1.9 enabled")
        endif()
    endif()
    if(RAW_DXC_610)
        message(STATUS "RAW: SM 6.10 preview DXC detected (${RAW_DXC_610})")
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
    set(RAW_DXC_FLAGS -HV 2021 -Gis -O3 -enable-16bit-types -Qstrip_debug -Qstrip_reflect)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/post_gtao.h ${RAW_ENGINE_GEN}/game_pack.h
               ${RAW_ENGINE_GEN}/ao_composite_vs.h ${RAW_ENGINE_GEN}/ao_composite_fs.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_6_2 -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/post_gtao.h -Vn kDxil_post_gtao ${RAW_GTAO_HLSL}
        COMMAND ${RAW_DXC} -T cs_6_2 -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/game_pack.h -Vn kDxil_game_pack ${RAW_PACK_HLSL}
        COMMAND ${RAW_DXC} -T vs_6_2 -E vs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_composite_vs.h -Vn kDxil_ao_composite_vs ${RAW_COMP_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_2 -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_composite_fs.h -Vn kDxil_ao_composite_fs ${RAW_COMP_HLSL}
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
        COMMAND ${RAW_DXC} -T vs_6_2 -E vs -DPASS=0 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_vs.h -Vn kDxil_ao_rt_vs ${RAW_AORT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_2 -E fs -DPASS=0 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_prefilter.h -Vn kDxil_ao_rt_prefilter ${RAW_AORT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_2 -E fs -DPASS=1 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_level.h -Vn kDxil_ao_rt_level ${RAW_AORT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_2 -E fs -DPASS=2 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_trace.h -Vn kDxil_ao_rt_trace ${RAW_AORT_HLSL}
        COMMAND ${RAW_DXC} -T ps_6_2 -E fs -DPASS=3 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/ao_rt_resolve.h -Vn kDxil_ao_rt_resolve ${RAW_AORT_HLSL}
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
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/SceneLights.cpp src/d3d11_proxy/GameComposite.cpp src/d3d11_proxy/Tonemap.cpp)
# RAW's tone map shader: its text embedded as a string (d3dcompiler compiles it in the game); a change reconfigures.
set(RAW_TONEMAP_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/Tonemap.hlsl)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${RAW_TONEMAP_HLSL})
file(READ ${RAW_TONEMAP_HLSL} RAW_TONEMAP_TEXT)
string(LENGTH "${RAW_TONEMAP_TEXT}" RAW_TONEMAP_LEN)
set(RAW_TONEMAP_CHUNKS "")
set(RAW_TONEMAP_POS 0)
while(RAW_TONEMAP_POS LESS RAW_TONEMAP_LEN)
    string(SUBSTRING "${RAW_TONEMAP_TEXT}" ${RAW_TONEMAP_POS} 8192 RAW_TM_PART)
    string(APPEND RAW_TONEMAP_CHUNKS "    R\"HLSL(${RAW_TM_PART})HLSL\",\n")
    math(EXPR RAW_TONEMAP_POS "${RAW_TONEMAP_POS} + 8192")
endwhile()
file(WRITE ${RAW_BRIDGE_GEN}/TonemapSrc.h "#pragma once
// Generated from src/d3d12_bridge/shaders/Tonemap.hlsl by CMakeLists.txt.
namespace SB::Proxy
{
inline constexpr const char* kTonemapHlslChunks[] = {
${RAW_TONEMAP_CHUNKS}    nullptr
};
}
")
target_include_directories(d3d11_proxy PRIVATE ${RAW_BRIDGE_GEN})
if(RAW_ENGINE_DIR)
    set(RAW_SHADER_MODEL "6_6" CACHE STRING "HLSL shader model for lighting, cloud & volumetric passes (SM 6.6 universal stock Windows D3D12 + FP16 + DXR RayQuery)")
    set(RAW_LIGHT_HLSL ${RAW_SHARE_HLSL}/Lighting.hlsl)
    set(RAW_LIGHTFOG_HLSL ${RAW_SHARE_HLSL}/LightFog.hlsl)
    set(RAW_CLOUDS_HLSL ${RAW_SHARE_HLSL}/Clouds.hlsl)
    set(RAW_VOLUMETRIC_HLSL ${RAW_SHARE_HLSL}/VolumetricPass.hlsl)
    set(RAW_BLOOM_HLSL ${RAW_SHARE_HLSL}/BloomPass.hlsl)
    set(RAW_ATMO_HLSL ${RAW_SHARE_HLSL}/AtmosphereLut.hlsl)
    set(RAW_REFL_FILT_HLSL ${RAW_SHARE_HLSL}/RtReflFilter.hlsl)
    set(RAW_DOF_HLSL ${RAW_SHARE_HLSL}/DofPass.hlsl)
    set(RAW_RAWLSS5_HLSL ${RAW_SHARE_HLSL}/Rawlss5Pass.hlsl)
    set(RAW_GI_ATROUS_HLSL ${RAW_SHARE_HLSL}/GiAtrousPass.hlsl)
    set(RAW_CLOUD_NOISE_HLSL ${RAW_SHARE_HLSL}/CloudNoiseLut.hlsl)
    set(RAW_ENV_CUBE_HLSL ${RAW_SHARE_HLSL}/EnvCubePass.hlsl)
    set(RAW_FROXEL_HLSL ${RAW_SHARE_HLSL}/FroxelPass.hlsl)
    set(RAW_WORLDSIM_HLSL ${RAW_SHARE_HLSL}/WorldFieldSim.hlsl)
    set(RAW_LIGHT_HEADERS ${RAW_ENGINE_GEN}/light_vs.h ${RAW_ENGINE_GEN}/light_fs.h ${RAW_ENGINE_GEN}/light_fog_fs.h
        ${RAW_ENGINE_GEN}/clouds_fs.h ${RAW_ENGINE_GEN}/volumetric_fs.h
        ${RAW_ENGINE_GEN}/bloom_pre_fs.h ${RAW_ENGINE_GEN}/bloom_down_fs.h ${RAW_ENGINE_GEN}/bloom_up_fs.h ${RAW_ENGINE_GEN}/bloom_comp_fs.h
        ${RAW_ENGINE_GEN}/atmo_trans_fs.h ${RAW_ENGINE_GEN}/atmo_ms_fs.h ${RAW_ENGINE_GEN}/atmo_sky_fs.h ${RAW_ENGINE_GEN}/rt_refl_filter_fs.h
        ${RAW_ENGINE_GEN}/dof_focus_fs.h ${RAW_ENGINE_GEN}/dof_coc_fs.h ${RAW_ENGINE_GEN}/dof_gather_fs.h ${RAW_ENGINE_GEN}/dof_post_fs.h ${RAW_ENGINE_GEN}/dof_comp_fs.h
        ${RAW_ENGINE_GEN}/rawlss5_fs.h
        ${RAW_ENGINE_GEN}/gi_at1_fs.h ${RAW_ENGINE_GEN}/gi_at2_fs.h ${RAW_ENGINE_GEN}/gi_at4_fs.h ${RAW_ENGINE_GEN}/cloud_noise_fs.h ${RAW_ENGINE_GEN}/env_cube_fs.h
        ${RAW_ENGINE_GEN}/froxel_inject_fs.h ${RAW_ENGINE_GEN}/froxel_block_fs.h ${RAW_ENGINE_GEN}/froxel_int_fs.h ${RAW_ENGINE_GEN}/world_field_sim_fs.h)
    add_custom_command(
        OUTPUT ${RAW_LIGHT_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T vs_${RAW_SHADER_MODEL} -E vs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/light_vs.h -Vn kDxil_light_vs ${RAW_LIGHT_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/light_fs.h -Vn kDxil_light_fs ${RAW_LIGHT_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/light_fog_fs.h -Vn kDxil_light_fog_fs ${RAW_LIGHTFOG_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/clouds_fs.h -Vn kDxil_clouds_fs ${RAW_CLOUDS_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/volumetric_fs.h -Vn kDxil_volumetric_fs ${RAW_VOLUMETRIC_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E prefilter ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/bloom_pre_fs.h -Vn kDxil_bloom_pre_fs ${RAW_BLOOM_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E down ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/bloom_down_fs.h -Vn kDxil_bloom_down_fs ${RAW_BLOOM_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E up ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/bloom_up_fs.h -Vn kDxil_bloom_up_fs ${RAW_BLOOM_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E composite ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/bloom_comp_fs.h -Vn kDxil_bloom_comp_fs ${RAW_BLOOM_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E transmittance ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/atmo_trans_fs.h -Vn kDxil_atmo_trans_fs ${RAW_ATMO_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E multiscat ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/atmo_ms_fs.h -Vn kDxil_atmo_ms_fs ${RAW_ATMO_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E skyview ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/atmo_sky_fs.h -Vn kDxil_atmo_sky_fs ${RAW_ATMO_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_refl_filter_fs.h -Vn kDxil_rt_refl_filter_fs ${RAW_REFL_FILT_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E dof_focus ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/dof_focus_fs.h -Vn kDxil_dof_focus_fs ${RAW_DOF_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E dof_coc ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/dof_coc_fs.h -Vn kDxil_dof_coc_fs ${RAW_DOF_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E dof_gather ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/dof_gather_fs.h -Vn kDxil_dof_gather_fs ${RAW_DOF_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E dof_post ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/dof_post_fs.h -Vn kDxil_dof_post_fs ${RAW_DOF_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E dof_comp ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/dof_comp_fs.h -Vn kDxil_dof_comp_fs ${RAW_DOF_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E rawlss5_fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rawlss5_fs.h -Vn kDxil_rawlss5_fs ${RAW_RAWLSS5_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E step1 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/gi_at1_fs.h -Vn kDxil_gi_at1_fs ${RAW_GI_ATROUS_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E step2 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/gi_at2_fs.h -Vn kDxil_gi_at2_fs ${RAW_GI_ATROUS_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E step4 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/gi_at4_fs.h -Vn kDxil_gi_at4_fs ${RAW_GI_ATROUS_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E bake ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/cloud_noise_fs.h -Vn kDxil_cloud_noise_fs ${RAW_CLOUD_NOISE_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E bake ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/env_cube_fs.h -Vn kDxil_env_cube_fs ${RAW_ENV_CUBE_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E froxel_inject ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/froxel_inject_fs.h -Vn kDxil_froxel_inject_fs ${RAW_FROXEL_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E froxel_block_scan -DFROXEL_BLOCK=1 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/froxel_block_fs.h -Vn kDxil_froxel_block_fs ${RAW_FROXEL_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E froxel_integrate -DFROXEL_INT=1 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/froxel_int_fs.h -Vn kDxil_froxel_int_fs ${RAW_FROXEL_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/world_field_sim_fs.h -Vn kDxil_world_field_sim_fs ${RAW_WORLDSIM_HLSL}
        DEPENDS ${RAW_LIGHT_HLSL} ${RAW_LIGHTFOG_HLSL} ${RAW_CLOUDS_HLSL} ${RAW_VOLUMETRIC_HLSL} ${RAW_BLOOM_HLSL} ${RAW_ATMO_HLSL} ${RAW_REFL_FILT_HLSL} ${RAW_DOF_HLSL} ${RAW_RAWLSS5_HLSL} ${RAW_GI_ATROUS_HLSL} ${RAW_CLOUD_NOISE_HLSL} ${RAW_ENV_CUBE_HLSL} ${RAW_FROXEL_HLSL} ${RAW_WORLDSIM_HLSL} ${RAW_SHARE_HLSL}/LightingCommon.hlsli ${RAW_SHARE_HLSL}/Weather.hlsli
                ${RAW_SHARE_HLSL}/CloudCommon.hlsli ${RAW_SHARE_HLSL}/DistantLod.hlsli ${RAW_SHARE_HLSL}/ElderOptics.hlsli ${RAW_SHARE_HLSL}/StrataBRDF.hlsli ${RAW_SHARE_HLSL}/SpectralTransport.hlsli ${RAW_SHARE_HLSL}/RawTensorMath.hlsli ${RAW_SHARE_HLSL}/RawProceduralNoise.hlsli ${RAW_SHARE_HLSL}/RawMaterials3D.hlsli ${RAW_SHARE_HLSL}/RawDecals3D.hlsli ${RAW_SHARE_HLSL}/RawSpells3D.hlsli
                ${RAW_SHARE_HLSL}/RawWeatherAtmo.hlsli ${RAW_SHARE_HLSL}/RawCosmosSky.hlsli ${RAW_SHARE_HLSL}/RawSeasonsThermals.hlsli ${RAW_SHARE_HLSL}/RawWeathering.hlsli
        COMMENT "dxc: SM${RAW_SHADER_MODEL} lighting, cloud, volumetric, bloom, atmosphere, refl denoiser, 3-pass DoF & 3-pass A-Trous GI -> DXIL headers"
        VERBATIM)
    target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/LightingPass.cpp ${RAW_LIGHT_HEADERS})
    if(RAW_HAS_SM69_DXIL)
        add_custom_command(
            OUTPUT ${RAW_ENGINE_GEN}/rawlss5_69_fs.h
            COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
            COMMAND ${RAW_DXC_NEXT} -T ps_6_9 -E rawlss5_fs ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rawlss5_69_fs.h -Vn kDxil_rawlss5_69_fs ${RAW_RAWLSS5_HLSL}
            DEPENDS ${RAW_RAWLSS5_HLSL} ${RAW_SHARE_HLSL}/LightingCommon.hlsli ${RAW_SHARE_HLSL}/RawTensorMath.hlsli
            COMMENT "dxc 1.9+: native SM 6.9 DXIL 1.9 RAWLSS 6.5 (Long Vectors + wave-quad) -> DXIL header"
            VERBATIM)
        target_sources(raw_d3d12_bridge PRIVATE ${RAW_ENGINE_GEN}/rawlss5_69_fs.h)
        target_compile_definitions(raw_d3d12_bridge PRIVATE RAW_HAS_SM69_DXIL=1)
    endif()
endif()

# ============================================================================
# D3D11-to-D3D12 translation layer (stage 2): ID3D11Device and
# ID3D11DeviceContext implemented on a D3D12 device. Windows SDK only.
# ============================================================================

set(RAW_TX_DIR ${CMAKE_CURRENT_SOURCE_DIR}/src/dx12/translate)
set(RAW_TX_GEN ${CMAKE_CURRENT_BINARY_DIR}/tx_shaders)
set(RAW_MIPGEN_HLSL ${RAW_TX_DIR}/shaders/MipGen.hlsl)
set(RAW_TXSUBDIV_HLSL ${RAW_TX_DIR}/shaders/TxSubdiv.hlsl)
add_custom_command(
    OUTPUT ${RAW_TX_GEN}/MipGen_VS.h ${RAW_TX_GEN}/MipGen_PS.h ${RAW_TX_GEN}/TxSubdiv_CS.h
    COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_TX_GEN}
    COMMAND ${RAW_FXC} /nologo /T vs_5_0 /E VSMain /O3 /Vn g_MipGenVS /Fh ${RAW_TX_GEN}/MipGen_VS.h ${RAW_MIPGEN_HLSL}
    COMMAND ${RAW_FXC} /nologo /T ps_5_0 /E PSMain /O3 /Vn g_MipGenPS /Fh ${RAW_TX_GEN}/MipGen_PS.h ${RAW_MIPGEN_HLSL}
    COMMAND ${RAW_FXC} /nologo /T cs_5_0 /E CSMain /O3 /Vn g_TxSubdivCS /Fh ${RAW_TX_GEN}/TxSubdiv_CS.h ${RAW_TXSUBDIV_HLSL}
    DEPENDS ${RAW_MIPGEN_HLSL} ${RAW_TXSUBDIV_HLSL}
    COMMENT "fxc: translation layer shaders (MipGen + TxSubdiv PN-Triangle uplift) -> bytecode headers"
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
    ${RAW_TX_DIR}/tx_subdiv.cpp
    ${RAW_TX_DIR}/tx_swapchain.cpp
    ${RAW_TX_DIR}/tx_stream.cpp
    ${RAW_TX_DIR}/tx_ctx_bind_om.cpp
    ${RAW_TX_DIR}/tx_ctx_desc.cpp
    ${RAW_TX_DIR}/tx_stream_play.cpp
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
    ${RAW_TX_GEN}/TxSubdiv_CS.h
)
target_include_directories(raw_dx12_translate PUBLIC ${RAW_TX_DIR} PRIVATE ${RAW_TX_GEN})
target_compile_definitions(raw_dx12_translate PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
target_link_libraries(raw_dx12_translate PUBLIC dxguid)
# The translate mode (docs/TX_IN_GAME.md): the game on raw::tx instead of the system runtime.
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/TxMode.cpp src/d3d11_proxy/TxHarness.cpp)
# Hot reload (docs/HOT_RELOAD.md): shaders and parameters while the game runs.
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/HotReload.cpp src/d3d11_proxy/Overlay.cpp src/d3d11_proxy/RawApi.cpp
               src/ui/RawInputEngine.cpp src/ui/RawStudioUi.cpp)
# Ray tracing on the game's geometry (docs/RAYTRACING.md), on raw::tx's device.
target_sources(d3d11_proxy PRIVATE src/d3d11_proxy/SceneGeometry.cpp src/d3d11_proxy/SceneGeometryBound.cpp src/d3d11_proxy/SceneSkin.cpp src/d3d11_proxy/SceneSkinPrepass.cpp)
if(RAW_ENGINE_DIR)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/raw_mesh_as.h ${RAW_ENGINE_GEN}/raw_mesh_ms.h ${RAW_ENGINE_GEN}/raw_subdiv_cs.h
               ${RAW_ENGINE_GEN}/raw_terrain_as.h ${RAW_ENGINE_GEN}/raw_terrain_ms.h ${RAW_ENGINE_GEN}/raw_terrain_vs.h ${RAW_ENGINE_GEN}/raw_terrain_ps.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T as_${RAW_SHADER_MODEL} -E as_main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_mesh_as.h -Vn kDxil_raw_mesh_as ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawMesh.hlsl
        COMMAND ${RAW_DXC} -T ms_${RAW_SHADER_MODEL} -E ms_main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_mesh_ms.h -Vn kDxil_raw_mesh_ms ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawMesh.hlsl
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E subdiv_main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_subdiv_cs.h -Vn kDxil_raw_subdiv_cs ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawSubdiv.hlsl
        COMMAND ${RAW_DXC} -T as_${RAW_SHADER_MODEL} -E as_terrain_main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_terrain_as.h -Vn kDxil_raw_terrain_as ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawTerrainWorld.hlsl
        COMMAND ${RAW_DXC} -T ms_${RAW_SHADER_MODEL} -E ms_terrain_main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_terrain_ms.h -Vn kDxil_raw_terrain_ms ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawTerrainWorld.hlsl
        COMMAND ${RAW_DXC} -T vs_${RAW_SHADER_MODEL} -E vs_terrain_main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_terrain_vs.h -Vn kDxil_raw_terrain_vs ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawTerrainWorld.hlsl
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E ps_terrain_main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_terrain_ps.h -Vn kDxil_raw_terrain_ps ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawTerrainWorld.hlsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawMesh.hlsl ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawSubdiv.hlsl
                ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawTerrainWorld.hlsl ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawMaterials3D.hlsli
                ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawProceduralNoise.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/LightingCommon.hlsli
                ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawFloraWind.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawSeasonsThermals.hlsli
        COMMENT "dxc: RAW-MESH 2.0 & RawTerrainWorld SM${RAW_SHADER_MODEL} Amplification, Mesh, Vertex, Pixel & PN-Triangle subdivision -> DXIL headers"
        VERBATIM)
    set(RAW_FLUID_FIRE_HLSL ${RAW_SHARE_HLSL}/RawFluidFireSim.hlsl)
    set(RAW_FLUID_WATER_HLSL ${RAW_SHARE_HLSL}/RawFluidWaterSim.hlsl)
    set(RAW_FLUID_COMPOSITE_HLSL ${RAW_SHARE_HLSL}/RawFluidFireComposite.hlsl)
    set(RAW_FLUID_HEADERS
        ${RAW_ENGINE_GEN}/raw_fluid_fire_inject_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_fire_advect_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_fire_forces_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_fire_div_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_fire_press_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_fire_proj_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_fire_rad_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_water_inject_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_water_step_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_water_normals_cs.h
        ${RAW_ENGINE_GEN}/raw_fluid_fire_composite_cs.h)
    add_custom_command(
        OUTPUT ${RAW_FLUID_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_fire_inject ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_fire_inject_cs.h -Vn kDxil_raw_fluid_fire_inject ${RAW_FLUID_FIRE_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_fire_advect ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_fire_advect_cs.h -Vn kDxil_raw_fluid_fire_advect ${RAW_FLUID_FIRE_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_fire_forces_combust ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_fire_forces_cs.h -Vn kDxil_raw_fluid_fire_forces ${RAW_FLUID_FIRE_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_fire_divergence ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_fire_div_cs.h -Vn kDxil_raw_fluid_fire_div ${RAW_FLUID_FIRE_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_fire_pressure ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_fire_press_cs.h -Vn kDxil_raw_fluid_fire_press ${RAW_FLUID_FIRE_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_fire_project ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_fire_proj_cs.h -Vn kDxil_raw_fluid_fire_proj ${RAW_FLUID_FIRE_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_fire_radiance ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_fire_rad_cs.h -Vn kDxil_raw_fluid_fire_rad ${RAW_FLUID_FIRE_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_water_inject ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_water_inject_cs.h -Vn kDxil_raw_fluid_water_inject ${RAW_FLUID_WATER_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_water_step ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_water_step_cs.h -Vn kDxil_raw_fluid_water_step ${RAW_FLUID_WATER_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_water_normals ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_water_normals_cs.h -Vn kDxil_raw_fluid_water_normals ${RAW_FLUID_WATER_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_fire_composite ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_fluid_fire_composite_cs.h -Vn kDxil_raw_fluid_fire_composite ${RAW_FLUID_COMPOSITE_HLSL}
        DEPENDS ${RAW_FLUID_FIRE_HLSL} ${RAW_FLUID_WATER_HLSL} ${RAW_FLUID_COMPOSITE_HLSL}
        COMMENT "dxc: GPU 3D Navier-Stokes Fire/Smoke, Composite & 2D Shallow-Water Fluid Solvers SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_RC_HLSL ${RAW_SHARE_HLSL}/RawRadianceCascades.hlsl)
    set(RAW_RC_SCREEN_HLSL ${RAW_SHARE_HLSL}/RawRadianceScreenPrep.hlsl)
    set(RAW_RC_HEADERS
        ${RAW_ENGINE_GEN}/raw_rc_trace_merge_cs.h
        ${RAW_ENGINE_GEN}/raw_rc_resolve_cs.h
        ${RAW_ENGINE_GEN}/raw_rc_screen_prep_cs.h
        ${RAW_ENGINE_GEN}/raw_rc_screen_apply_cs.h)
    add_custom_command(
        OUTPUT ${RAW_RC_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_rc_trace_merge ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_rc_trace_merge_cs.h -Vn kDxil_raw_rc_trace_merge ${RAW_RC_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_rc_resolve ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_rc_resolve_cs.h -Vn kDxil_raw_rc_resolve ${RAW_RC_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_rc_screen_prep ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_rc_screen_prep_cs.h -Vn kDxil_raw_rc_screen_prep ${RAW_RC_SCREEN_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_rc_screen_apply ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_rc_screen_apply_cs.h -Vn kDxil_raw_rc_screen_apply ${RAW_RC_SCREEN_HLSL}
        DEPENDS ${RAW_RC_HLSL} ${RAW_RC_SCREEN_HLSL}
        COMMENT "dxc: GPU 4-Cascade Penumbra-Exact Radiance Cascades GI + Screen Prep/Apply SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_XPBD_HLSL ${RAW_SHARE_HLSL}/RawXpbdClothSim.hlsl)
    set(RAW_XPBD_HEADERS ${RAW_ENGINE_GEN}/raw_xpbd_cloth_cs.h)
    add_custom_command(
        OUTPUT ${RAW_XPBD_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_xpbd_step ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_xpbd_cloth_cs.h -Vn kDxil_raw_xpbd_cloth ${RAW_XPBD_HLSL}
        DEPENDS ${RAW_XPBD_HLSL}
        COMMENT "dxc: GPU XPBD Multi-Chain Soft-Body/Hair/Bush-Bone Solver SM${RAW_SHADER_MODEL} -> DXIL header"
        VERBATIM)
    set(RAW_OVERLAY_HLSL ${RAW_SHARE_HLSL}/RawOverlayPass.hlsl)
    set(RAW_OVERLAY_HEADERS ${RAW_ENGINE_GEN}/raw_overlay_cs.h)
    add_custom_command(
        OUTPUT ${RAW_OVERLAY_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_overlay_draw ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_overlay_cs.h -Vn kDxil_raw_overlay ${RAW_OVERLAY_HLSL}
        DEPENDS ${RAW_OVERLAY_HLSL}
        COMMENT "dxc: Zero-Dep GPU Compute Telemetry HUD & Sparkline Overlay SM${RAW_SHADER_MODEL} -> DXIL header"
        VERBATIM)
    set(RAW_GPU_CULL_HLSL ${RAW_SHARE_HLSL}/RawGpuCull.hlsl)
    set(RAW_GPU_CULL_HEADERS
        ${RAW_ENGINE_GEN}/raw_hiz_reduce_cs.h
        ${RAW_ENGINE_GEN}/raw_cull_instances_cs.h)
    add_custom_command(
        OUTPUT ${RAW_GPU_CULL_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_hiz_reduce ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_hiz_reduce_cs.h -Vn kDxil_raw_hiz_reduce ${RAW_GPU_CULL_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_cull_instances ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_cull_instances_cs.h -Vn kDxil_raw_cull_instances ${RAW_GPU_CULL_HLSL}
        DEPENDS ${RAW_GPU_CULL_HLSL}
        COMMENT "dxc: GPU-Driven Conservative Hi-Z Pyramid & 65k-Instance Culler SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_LIGHT_CLUSTER_HLSL ${RAW_SHARE_HLSL}/RawLightClusterSim.hlsl)
    set(RAW_LIGHT_CLUSTER_HEADERS
        ${RAW_ENGINE_GEN}/raw_cluster_build_aabb_cs.h
        ${RAW_ENGINE_GEN}/raw_cluster_cull_lights_cs.h)
    add_custom_command(
        OUTPUT ${RAW_LIGHT_CLUSTER_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_cluster_build_aabb ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_cluster_build_aabb_cs.h -Vn kDxil_raw_cluster_build_aabb ${RAW_LIGHT_CLUSTER_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_cluster_cull_lights ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_cluster_cull_lights_cs.h -Vn kDxil_raw_cluster_cull_lights ${RAW_LIGHT_CLUSTER_HLSL}
        DEPENDS ${RAW_LIGHT_CLUSTER_HLSL}
        COMMENT "dxc: 3D Clustered Froxel Light Assignment SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_VOL_FROXEL_HLSL ${RAW_SHARE_HLSL}/RawVolumetricFroxelSim.hlsl)
    set(RAW_FROXEL_HEADERS
        ${RAW_ENGINE_GEN}/raw_froxel_inject_cs.h
        ${RAW_ENGINE_GEN}/raw_froxel_integrate_cs.h)
    add_custom_command(
        OUTPUT ${RAW_FROXEL_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_froxel_inject ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_froxel_inject_cs.h -Vn kDxil_raw_froxel_inject ${RAW_VOL_FROXEL_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_froxel_integrate ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_froxel_integrate_cs.h -Vn kDxil_raw_froxel_integrate ${RAW_VOL_FROXEL_HLSL}
        DEPENDS ${RAW_VOL_FROXEL_HLSL}
        COMMENT "dxc: 160x90x64 3D Volumetric Froxel Fog & Mie Scattering SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_SHADOW_PCSS_HLSL ${RAW_SHARE_HLSL}/RawShadowPcssSim.hlsl)
    set(RAW_SHADOW_PCSS_HEADERS ${RAW_ENGINE_GEN}/raw_shadow_pcss_cs.h)
    add_custom_command(
        OUTPUT ${RAW_SHADOW_PCSS_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_shadow_pcss ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_shadow_pcss_cs.h -Vn kDxil_raw_shadow_pcss ${RAW_SHADOW_PCSS_HLSL}
        DEPENDS ${RAW_SHADOW_PCSS_HLSL}
        COMMENT "dxc: GPU Variable-Penumbra PCSS Contact-Hardening Shadows SM${RAW_SHADER_MODEL} -> DXIL header"
        VERBATIM)
    set(RAW_GTAO_SIM_HLSL ${RAW_SHARE_HLSL}/RawGtaoSim.hlsl)
    set(RAW_GTAO_HEADERS
        ${RAW_ENGINE_GEN}/raw_gtao_main_cs.h
        ${RAW_ENGINE_GEN}/raw_gtao_denoise_cs.h)
    add_custom_command(
        OUTPUT ${RAW_GTAO_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_gtao_main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_gtao_main_cs.h -Vn kDxil_raw_gtao_main ${RAW_GTAO_SIM_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_gtao_denoise ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_gtao_denoise_cs.h -Vn kDxil_raw_gtao_denoise ${RAW_GTAO_SIM_HLSL}
        DEPENDS ${RAW_GTAO_SIM_HLSL}
        COMMENT "dxc: GPU Jimenez GTAO + Bent Normal & Bilateral Denoise SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_SSR_HLSL ${RAW_SHARE_HLSL}/RawSsrSim.hlsl)
    set(RAW_SSR_HEADERS ${RAW_ENGINE_GEN}/raw_ssr_trace_cs.h)
    add_custom_command(
        OUTPUT ${RAW_SSR_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_ssr_trace ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_ssr_trace_cs.h -Vn kDxil_raw_ssr_trace ${RAW_SSR_HLSL}
        DEPENDS ${RAW_SSR_HLSL}
        COMMENT "dxc: GPU GGX Stochastic Screen-Space Reflections SM${RAW_SHADER_MODEL} -> DXIL header"
        VERBATIM)
    set(RAW_AUTO_EXPOSURE_HLSL ${RAW_SHARE_HLSL}/RawAutoExposureSim.hlsl)
    set(RAW_AUTO_EXPOSURE_HEADERS
        ${RAW_ENGINE_GEN}/raw_exposure_hist_cs.h
        ${RAW_ENGINE_GEN}/raw_exposure_adapt_cs.h)
    add_custom_command(
        OUTPUT ${RAW_AUTO_EXPOSURE_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_exposure_histogram ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_exposure_hist_cs.h -Vn kDxil_raw_exposure_hist ${RAW_AUTO_EXPOSURE_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_exposure_adapt ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_exposure_adapt_cs.h -Vn kDxil_raw_exposure_adapt ${RAW_AUTO_EXPOSURE_HLSL}
        DEPENDS ${RAW_AUTO_EXPOSURE_HLSL}
        COMMENT "dxc: GPU 256-Bin Log-Luminance Histogram & Human Eye-Adaptation SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_CLOUD_RAYMARCH_HLSL ${RAW_SHARE_HLSL}/RawCloudRaymarchSim.hlsl)
    set(RAW_CLOUD_RAYMARCH_HEADERS ${RAW_ENGINE_GEN}/raw_cloud_raymarch_cs.h)
    add_custom_command(
        OUTPUT ${RAW_CLOUD_RAYMARCH_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_cloud_raymarch ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_cloud_raymarch_cs.h -Vn kDxil_raw_cloud_raymarch ${RAW_CLOUD_RAYMARCH_HLSL}
        DEPENDS ${RAW_CLOUD_RAYMARCH_HLSL}
        COMMENT "dxc: GPU Volumetric Cloud Raymarching & Silver-Lining SM${RAW_SHADER_MODEL} -> DXIL header"
        VERBATIM)
    set(RAW_POST_CINEMATIC_HLSL ${RAW_SHARE_HLSL}/RawPostCinematicSim.hlsl)
    set(RAW_POST_CINEMATIC_HEADERS
        ${RAW_ENGINE_GEN}/raw_post_tile_max_cs.h
        ${RAW_ENGINE_GEN}/raw_post_neighbor_max_cs.h
        ${RAW_ENGINE_GEN}/raw_post_cinematic_cs.h)
    add_custom_command(
        OUTPUT ${RAW_POST_CINEMATIC_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_post_tile_max ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_post_tile_max_cs.h -Vn kDxil_raw_post_tile_max ${RAW_POST_CINEMATIC_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_post_neighbor_max ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_post_neighbor_max_cs.h -Vn kDxil_raw_post_neighbor_max ${RAW_POST_CINEMATIC_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_post_cinematic_composite ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_post_cinematic_cs.h -Vn kDxil_raw_post_cinematic ${RAW_POST_CINEMATIC_HLSL}
        DEPENDS ${RAW_POST_CINEMATIC_HLSL}
        COMMENT "dxc: GPU Cinematic Motion Blur & Hexagonal Bokeh DoF SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_GPU_PARTICLES_HLSL ${RAW_SHARE_HLSL}/RawGpuParticlesSim.hlsl)
    set(RAW_GPU_PARTICLES_HEADERS
        ${RAW_ENGINE_GEN}/raw_particles_sim_cs.h
        ${RAW_ENGINE_GEN}/raw_particles_splat_cs.h)
    add_custom_command(
        OUTPUT ${RAW_GPU_PARTICLES_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_particles_sim ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_particles_sim_cs.h -Vn kDxil_raw_particles_sim ${RAW_GPU_PARTICLES_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_particles_splat ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_particles_splat_cs.h -Vn kDxil_raw_particles_splat ${RAW_GPU_PARTICLES_HLSL}
        DEPENDS ${RAW_GPU_PARTICLES_HLSL}
        COMMENT "dxc: GPU 16384-Particle 3D Physics & Additive Splat SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_TSR_UPSCALE_HLSL ${RAW_SHARE_HLSL}/RawTsrUpscaleSim.hlsl)
    set(RAW_TSR_UPSCALE_HEADERS
        ${RAW_ENGINE_GEN}/raw_tsr_resolve_cs.h
        ${RAW_ENGINE_GEN}/raw_tsr_rcas_cs.h)
    add_custom_command(
        OUTPUT ${RAW_TSR_UPSCALE_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_tsr_resolve ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_tsr_resolve_cs.h -Vn kDxil_raw_tsr_resolve ${RAW_TSR_UPSCALE_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_tsr_rcas_sharpen ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_tsr_rcas_cs.h -Vn kDxil_raw_tsr_rcas ${RAW_TSR_UPSCALE_HLSL}
        DEPENDS ${RAW_TSR_UPSCALE_HLSL}
        COMMENT "dxc: GPU RAW-TSR Temporal Super-Resolution & RCAS Sharpening SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_WATER_CAUSTICS_HLSL ${RAW_SHARE_HLSL}/RawWaterCausticsSim.hlsl)
    set(RAW_WATER_CAUSTICS_HEADERS
        ${RAW_ENGINE_GEN}/raw_water_caustics_trace_cs.h
        ${RAW_ENGINE_GEN}/raw_water_underwater_rays_cs.h)
    add_custom_command(
        OUTPUT ${RAW_WATER_CAUSTICS_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_caustics_trace ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_water_caustics_trace_cs.h -Vn kDxil_raw_water_caustics_trace ${RAW_WATER_CAUSTICS_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_underwater_rays ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_water_underwater_rays_cs.h -Vn kDxil_raw_water_underwater_rays ${RAW_WATER_CAUSTICS_HLSL}
        DEPENDS ${RAW_WATER_CAUSTICS_HLSL}
        COMMENT "dxc: GPU Physical Water Caustics & Underwater Volumetric God-Rays SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_SNOW_DEFORM_HLSL ${RAW_SHARE_HLSL}/RawSnowDeformSim.hlsl)
    set(RAW_SNOW_DEFORM_HEADERS
        ${RAW_ENGINE_GEN}/raw_snow_deform_step_cs.h
        ${RAW_ENGINE_GEN}/raw_snow_normal_height_cs.h)
    add_custom_command(
        OUTPUT ${RAW_SNOW_DEFORM_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_snow_deform_step ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_snow_deform_step_cs.h -Vn kDxil_raw_snow_deform_step ${RAW_SNOW_DEFORM_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_snow_normal_height ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_snow_normal_height_cs.h -Vn kDxil_raw_snow_normal_height ${RAW_SNOW_DEFORM_HLSL}
        DEPENDS ${RAW_SNOW_DEFORM_HLSL}
        COMMENT "dxc: GPU 512x512 Snow Accumulation, Thermal Melt & Footprint Deformation SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    set(RAW_GRASS_WIND_HLSL ${RAW_SHARE_HLSL}/RawGrassWindSim.hlsl)
    set(RAW_GRASS_WIND_HEADERS ${RAW_ENGINE_GEN}/raw_grass_wind_step_cs.h)
    add_custom_command(
        OUTPUT ${RAW_GRASS_WIND_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_grass_wind_step ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_grass_wind_step_cs.h -Vn kDxil_raw_grass_wind_step ${RAW_GRASS_WIND_HLSL}
        DEPENDS ${RAW_GRASS_WIND_HLSL}
        COMMENT "dxc: GPU 65536-Blade Dynamic Grass Wind, Snow Droop & Trample SM${RAW_SHADER_MODEL} -> DXIL header"
        VERBATIM)
    set(RAW_UILIB_HLSL ${RAW_SHARE_HLSL}/RawUiLibPass.hlsl)
    set(RAW_UILIB_HEADERS ${RAW_ENGINE_GEN}/raw_uilib_cs.h)
    add_custom_command(
        OUTPUT ${RAW_UILIB_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_uilib_draw ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_uilib_cs.h -Vn kDxil_raw_uilib ${RAW_UILIB_HLSL}
        DEPENDS ${RAW_UILIB_HLSL}
        COMMENT "dxc: GPU-Native UI Library & Debug-Layer HUD SM${RAW_SHADER_MODEL} -> DXIL header"
        VERBATIM)
    set(RAW_LIVE_WORLD_HLSL ${RAW_SHARE_HLSL}/RawLiveWorldComposite.hlsl)
    set(RAW_LIVE_WORLD_HEADERS ${RAW_ENGINE_GEN}/raw_live_world_composite_cs.h)
    add_custom_command(
        OUTPUT ${RAW_LIVE_WORLD_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E cs_live_world_composite ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_live_world_composite_cs.h -Vn kDxil_raw_live_world_composite ${RAW_LIVE_WORLD_HLSL}
        DEPENDS ${RAW_LIVE_WORLD_HLSL}
        COMMENT "dxc: Live World-Space GPU Compositor (Snow/Caustics/Grass/DebugLayers) SM${RAW_SHADER_MODEL} -> DXIL header"
        VERBATIM)
    target_sources(raw_d3d12_bridge PRIVATE ${RAW_ENGINE_GEN}/raw_mesh_as.h ${RAW_ENGINE_GEN}/raw_mesh_ms.h ${RAW_ENGINE_GEN}/raw_subdiv_cs.h
                   ${RAW_ENGINE_GEN}/raw_terrain_as.h ${RAW_ENGINE_GEN}/raw_terrain_ms.h ${RAW_ENGINE_GEN}/raw_terrain_vs.h ${RAW_ENGINE_GEN}/raw_terrain_ps.h
                   ${RAW_FLUID_HEADERS} ${RAW_RC_HEADERS} ${RAW_XPBD_HEADERS} ${RAW_OVERLAY_HEADERS} ${RAW_UILIB_HEADERS} ${RAW_LIVE_WORLD_HEADERS} ${RAW_GPU_CULL_HEADERS} ${RAW_LIGHT_CLUSTER_HEADERS} ${RAW_FROXEL_HEADERS} ${RAW_SHADOW_PCSS_HEADERS} ${RAW_GTAO_HEADERS} ${RAW_SSR_HEADERS} ${RAW_AUTO_EXPOSURE_HEADERS} ${RAW_CLOUD_RAYMARCH_HEADERS} ${RAW_POST_CINEMATIC_HEADERS} ${RAW_GPU_PARTICLES_HEADERS} ${RAW_TSR_UPSCALE_HEADERS} ${RAW_WATER_CAUSTICS_HEADERS} ${RAW_SNOW_DEFORM_HEADERS} ${RAW_GRASS_WIND_HEADERS}
                   src/d3d12_bridge/RawAssetBridge.cpp src/d3d12_bridge/RawTextureBridge.cpp src/d3d12_bridge/RawProceduralLib.cpp
                   src/d3d12_bridge/RawMeshHost.cpp src/d3d12_bridge/RawFluidSimHost.cpp src/d3d12_bridge/RawFluidCompositeHost.cpp src/d3d12_bridge/RawRadianceHost.cpp src/d3d12_bridge/RawXpbdSimHost.cpp src/d3d12_bridge/RawOverlayHost.cpp src/d3d12_bridge/RawUiLib.cpp src/d3d12_bridge/RawLiveWorldHost.cpp src/d3d12_bridge/RawGpuCullHost.cpp src/d3d12_bridge/RawLightClusterHost.cpp src/d3d12_bridge/RawVolumetricFroxelHost.cpp src/d3d12_bridge/RawShadowPcssHost.cpp src/d3d12_bridge/RawGtaoHost.cpp src/d3d12_bridge/RawSsrHost.cpp src/d3d12_bridge/RawAutoExposureHost.cpp src/d3d12_bridge/RawCloudRaymarchHost.cpp src/d3d12_bridge/RawPostCinematicHost.cpp src/d3d12_bridge/RawGpuParticlesHost.cpp src/d3d12_bridge/RawTsrUpscaleHost.cpp src/d3d12_bridge/RawWaterCausticsHost.cpp src/d3d12_bridge/RawSnowDeformHost.cpp src/d3d12_bridge/RawGrassWindHost.cpp src/d3d12_bridge/RawOverrideRegistry.cpp src/d3d12_bridge/RawBindlessHeap.cpp
                   src/d3d12_bridge/RawAsyncCompute.cpp src/d3d12_bridge/RawIndirectBatch.cpp src/d3d12_bridge/RawAssetStream.cpp
                   src/worldgen/RawTerrainGen.cpp src/worldgen/RawHydrologyGen.cpp src/worldgen/RawBiomeWorld.cpp src/worldgen/RawMeshPipeline.cpp src/worldgen/RawFloraEngine.cpp src/worldgen/RawWeatherSim.cpp src/worldgen/RawWorldShowcase.cpp src/worldgen/RawEsmWorldLoader.cpp src/worldgen/RawEsmModelResolver.cpp src/worldgen/RawEsmNpcWeatherLoader.cpp src/worldgen/RawTamrielWorldStreamer.cpp src/worldgen/RawWorldPhysics.cpp src/worldgen/RawNavMeshAi.cpp src/worldgen/RawWorldEcosystem.cpp src/worldgen/RawWildlifeBoids.cpp src/worldgen/RawSettlementSim.cpp src/worldgen/RawWorldEventDirector.cpp src/worldgen/RawFactionDiplomacy.cpp src/worldgen/RawWorldSerializer.cpp src/worldgen/RawDungeonGen.cpp src/worldgen/RawSkeletonAnimEngine.cpp src/worldgen/RawCombatSandboxEngine.cpp src/worldgen/RawQuestStoryEngine.cpp src/worldgen/RawCraftingInventoryEngine.cpp src/worldgen/RawLivingWorldSchedule.cpp src/worldgen/RawGameTheoryAi.cpp src/worldgen/RawBsaArchive.cpp src/worldgen/RawNifLoader.cpp src/worldgen/RawDdsDecoder.cpp src/worldgen/RawDestructionEngine.cpp
                   src/engine_rt/raw_job_system.cpp
                   src/materials/RawMaterialLib.cpp src/materials/RawAssetForge.cpp src/materials/RawTextureForge.cpp
                   src/assets/RawOverrideRegistry.cpp src/assets/RawAssetScaffold.cpp)
    add_executable(test_fluid_sim tests/test_fluid_sim.cpp src/d3d12_bridge/RawFluidSimHost.cpp ${RAW_FLUID_HEADERS})
    target_include_directories(test_fluid_sim PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_fluid_sim PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_fluid_sim PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_fluid_sim PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_fluid_composite_warp tests/test_fluid_composite_warp.cpp src/d3d12_bridge/RawFluidCompositeHost.cpp src/d3d12_bridge/RawFluidSimHost.cpp ${RAW_FLUID_HEADERS})
    target_include_directories(test_fluid_composite_warp PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_fluid_composite_warp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_fluid_composite_warp PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_fluid_composite_warp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_full_frame_warp
        tests/test_full_frame_warp.cpp
        src/d3d12_bridge/RawMeshHost.cpp
        src/d3d12_bridge/RawFluidSimHost.cpp
        src/d3d12_bridge/RawFluidCompositeHost.cpp
        src/materials/RawTextureForge.cpp
        src/worldgen/RawTerrainGen.cpp
        src/worldgen/RawBiomeWorld.cpp
        src/worldgen/RawMeshPipeline.cpp
        src/worldgen/RawFloraEngine.cpp
        src/worldgen/RawWeatherSim.cpp
        src/worldgen/RawWorldShowcase.cpp
        src/engine_rt/raw_job_system.cpp
        ${RAW_ENGINE_GEN}/raw_mesh_as.h ${RAW_ENGINE_GEN}/raw_mesh_ms.h ${RAW_ENGINE_GEN}/raw_subdiv_cs.h
        ${RAW_ENGINE_GEN}/raw_terrain_as.h ${RAW_ENGINE_GEN}/raw_terrain_ms.h ${RAW_ENGINE_GEN}/raw_terrain_vs.h ${RAW_ENGINE_GEN}/raw_terrain_ps.h
        ${RAW_FLUID_HEADERS})
    target_include_directories(test_full_frame_warp PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge ${CMAKE_CURRENT_SOURCE_DIR}/src/materials ${CMAKE_CURRENT_SOURCE_DIR}/src/worldgen ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_rt)
    target_compile_definitions(test_full_frame_warp PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_full_frame_warp PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_full_frame_warp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_radiance_cascades tests/test_radiance_cascades.cpp src/d3d12_bridge/RawRadianceHost.cpp ${RAW_RC_HEADERS})
    target_include_directories(test_radiance_cascades PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_radiance_cascades PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_radiance_cascades PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_radiance_cascades PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_xpbd_gpu tests/test_xpbd_gpu.cpp src/d3d12_bridge/RawXpbdSimHost.cpp ${RAW_XPBD_HEADERS})
    target_include_directories(test_xpbd_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_xpbd_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_xpbd_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_xpbd_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_overlay_gpu tests/test_overlay_gpu.cpp src/d3d12_bridge/RawOverlayHost.cpp src/d3d12_bridge/RawUiLib.cpp ${RAW_OVERLAY_HEADERS} ${RAW_UILIB_HEADERS})
    target_include_directories(test_overlay_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_overlay_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_overlay_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_overlay_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_ui_lib_gpu tests/test_ui_lib_gpu.cpp src/d3d12_bridge/RawUiLib.cpp src/d3d12_bridge/RawOverlayHost.cpp ${RAW_UILIB_HEADERS} ${RAW_OVERLAY_HEADERS})
    target_include_directories(test_ui_lib_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_ui_lib_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_ui_lib_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_ui_lib_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_live_world_composite_gpu tests/test_live_world_composite_gpu.cpp src/d3d12_bridge/RawLiveWorldHost.cpp ${RAW_LIVE_WORLD_HEADERS})
    target_include_directories(test_live_world_composite_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_live_world_composite_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_live_world_composite_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_live_world_composite_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_gpu_cull tests/test_gpu_cull.cpp src/d3d12_bridge/RawGpuCullHost.cpp ${RAW_GPU_CULL_HEADERS})
    target_include_directories(test_gpu_cull PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_gpu_cull PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_gpu_cull PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_gpu_cull PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_light_clusters_gpu tests/test_light_clusters_gpu.cpp src/d3d12_bridge/RawLightClusterHost.cpp ${RAW_LIGHT_CLUSTER_HEADERS})
    target_include_directories(test_light_clusters_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_light_clusters_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_light_clusters_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_light_clusters_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_volumetric_froxels_gpu tests/test_volumetric_froxels_gpu.cpp src/d3d12_bridge/RawVolumetricFroxelHost.cpp ${RAW_FROXEL_HEADERS})
    target_include_directories(test_volumetric_froxels_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_volumetric_froxels_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_volumetric_froxels_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_volumetric_froxels_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_shadow_pcss_gpu tests/test_shadow_pcss_gpu.cpp src/d3d12_bridge/RawShadowPcssHost.cpp ${RAW_SHADOW_PCSS_HEADERS})
    target_include_directories(test_shadow_pcss_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_shadow_pcss_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_shadow_pcss_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_shadow_pcss_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_gtao_gpu tests/test_gtao_gpu.cpp src/d3d12_bridge/RawGtaoHost.cpp ${RAW_GTAO_HEADERS})
    target_include_directories(test_gtao_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_gtao_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_gtao_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_gtao_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_ssr_gpu tests/test_ssr_gpu.cpp src/d3d12_bridge/RawSsrHost.cpp ${RAW_SSR_HEADERS})
    target_include_directories(test_ssr_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_ssr_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_ssr_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_ssr_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_auto_exposure_gpu tests/test_auto_exposure_gpu.cpp src/d3d12_bridge/RawAutoExposureHost.cpp ${RAW_AUTO_EXPOSURE_HEADERS})
    target_include_directories(test_auto_exposure_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_auto_exposure_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_auto_exposure_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_auto_exposure_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_cloud_raymarch_gpu tests/test_cloud_raymarch_gpu.cpp src/d3d12_bridge/RawCloudRaymarchHost.cpp ${RAW_CLOUD_RAYMARCH_HEADERS})
    target_include_directories(test_cloud_raymarch_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_cloud_raymarch_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_cloud_raymarch_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_cloud_raymarch_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_post_cinematic_gpu tests/test_post_cinematic_gpu.cpp src/d3d12_bridge/RawPostCinematicHost.cpp ${RAW_POST_CINEMATIC_HEADERS})
    target_include_directories(test_post_cinematic_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_post_cinematic_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_post_cinematic_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_post_cinematic_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_gpu_particles tests/test_gpu_particles.cpp src/d3d12_bridge/RawGpuParticlesHost.cpp ${RAW_GPU_PARTICLES_HEADERS})
    target_include_directories(test_gpu_particles PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_gpu_particles PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_gpu_particles PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_gpu_particles PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_tsr_upscale_gpu tests/test_tsr_upscale_gpu.cpp src/d3d12_bridge/RawTsrUpscaleHost.cpp ${RAW_TSR_UPSCALE_HEADERS})
    target_include_directories(test_tsr_upscale_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_tsr_upscale_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_tsr_upscale_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_tsr_upscale_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_water_caustics_gpu tests/test_water_caustics_gpu.cpp src/d3d12_bridge/RawWaterCausticsHost.cpp ${RAW_WATER_CAUSTICS_HEADERS})
    target_include_directories(test_water_caustics_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_water_caustics_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_water_caustics_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_water_caustics_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_snow_deform_gpu tests/test_snow_deform_gpu.cpp src/d3d12_bridge/RawSnowDeformHost.cpp ${RAW_SNOW_DEFORM_HEADERS})
    target_include_directories(test_snow_deform_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_snow_deform_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_snow_deform_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_snow_deform_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_grass_wind_gpu tests/test_grass_wind_gpu.cpp src/d3d12_bridge/RawGrassWindHost.cpp ${RAW_GRASS_WIND_HEADERS})
    target_include_directories(test_grass_wind_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge)
    target_compile_definitions(test_grass_wind_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_grass_wind_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_grass_wind_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(test_texture_atlas_gpu
        tests/test_texture_atlas_gpu.cpp
        src/d3d12_bridge/RawMeshHost.cpp
        src/materials/RawTextureForge.cpp
        ${RAW_ENGINE_GEN}/raw_mesh_as.h ${RAW_ENGINE_GEN}/raw_mesh_ms.h ${RAW_ENGINE_GEN}/raw_subdiv_cs.h
        ${RAW_ENGINE_GEN}/raw_terrain_as.h ${RAW_ENGINE_GEN}/raw_terrain_ms.h ${RAW_ENGINE_GEN}/raw_terrain_vs.h ${RAW_ENGINE_GEN}/raw_terrain_ps.h)
    target_include_directories(test_texture_atlas_gpu PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge ${CMAKE_CURRENT_SOURCE_DIR}/src/materials ${CMAKE_CURRENT_SOURCE_DIR}/src/worldgen)
    target_compile_definitions(test_texture_atlas_gpu PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(test_texture_atlas_gpu PRIVATE d3d12 dxgi dxguid)
    set_target_properties(test_texture_atlas_gpu PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    add_executable(raw_sandbox_render
        tools/raw_sandbox_render.cpp
        src/d3d12_bridge/RawMeshHost.cpp
        src/d3d12_bridge/RawFluidSimHost.cpp
        src/d3d12_bridge/RawFluidCompositeHost.cpp
        src/materials/RawTextureForge.cpp
        src/worldgen/RawTerrainGen.cpp
        src/worldgen/RawHydrologyGen.cpp
        src/worldgen/RawBiomeWorld.cpp
        src/worldgen/RawMeshPipeline.cpp
        src/worldgen/RawFloraEngine.cpp
        src/worldgen/RawWeatherSim.cpp
        src/worldgen/RawWorldShowcase.cpp
        src/worldgen/RawDestructionEngine.cpp
        src/worldgen/RawEsmWorldLoader.cpp
        src/worldgen/RawEsmModelResolver.cpp
        src/worldgen/RawBsaArchive.cpp
        src/worldgen/RawNifLoader.cpp
        src/engine_rt/raw_job_system.cpp
        ${RAW_ENGINE_GEN}/raw_mesh_as.h ${RAW_ENGINE_GEN}/raw_mesh_ms.h ${RAW_ENGINE_GEN}/raw_subdiv_cs.h
        ${RAW_ENGINE_GEN}/raw_terrain_as.h ${RAW_ENGINE_GEN}/raw_terrain_ms.h ${RAW_ENGINE_GEN}/raw_terrain_vs.h ${RAW_ENGINE_GEN}/raw_terrain_ps.h
        ${RAW_FLUID_HEADERS})
    target_include_directories(raw_sandbox_render PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge ${CMAKE_CURRENT_SOURCE_DIR}/src/materials ${CMAKE_CURRENT_SOURCE_DIR}/src/worldgen ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_rt)
    target_compile_definitions(raw_sandbox_render PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(raw_sandbox_render PRIVATE d3d12 dxgi dxguid)
    set_target_properties(raw_sandbox_render PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tools)
    set(RAW_AGX_DISPLAY_HLSL ${RAW_SHARE_HLSL}/RawAgxDisplay.hlsl)
    set(RAW_AGX_DISPLAY_HEADERS
        ${RAW_ENGINE_GEN}/raw_agx_display_vs.h
        ${RAW_ENGINE_GEN}/raw_agx_display_ps.h)
    add_custom_command(
        OUTPUT ${RAW_AGX_DISPLAY_HEADERS}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T vs_${RAW_SHADER_MODEL} -E vs_agx_display ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_agx_display_vs.h -Vn kDxil_raw_agx_display_vs ${RAW_AGX_DISPLAY_HLSL}
        COMMAND ${RAW_DXC} -T ps_${RAW_SHADER_MODEL} -E ps_agx_display ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/raw_agx_display_ps.h -Vn kDxil_raw_agx_display_ps ${RAW_AGX_DISPLAY_HLSL}
        DEPENDS ${RAW_AGX_DISPLAY_HLSL}
        COMMENT "dxc: RAW Interactive Viewer AgX Display SM${RAW_SHADER_MODEL} -> DXIL headers"
        VERBATIM)
    add_executable(raw_engine_viewer
        tools/raw_engine_viewer.cpp
        src/d3d12_bridge/RawMeshHost.cpp
        src/d3d12_bridge/RawFluidSimHost.cpp
        src/d3d12_bridge/RawFluidCompositeHost.cpp
        src/d3d12_bridge/RawRadianceHost.cpp
        src/d3d12_bridge/RawXpbdSimHost.cpp
        src/d3d12_bridge/RawOverlayHost.cpp
        src/d3d12_bridge/RawUiLib.cpp
        src/d3d12_bridge/RawGpuCullHost.cpp
        src/d3d12_bridge/RawLightClusterHost.cpp
        src/d3d12_bridge/RawVolumetricFroxelHost.cpp
        src/d3d12_bridge/RawShadowPcssHost.cpp
        src/d3d12_bridge/RawGtaoHost.cpp
        src/d3d12_bridge/RawSsrHost.cpp
        src/d3d12_bridge/RawAutoExposureHost.cpp
        src/d3d12_bridge/RawCloudRaymarchHost.cpp
        src/d3d12_bridge/RawPostCinematicHost.cpp
        src/d3d12_bridge/RawGpuParticlesHost.cpp
        src/d3d12_bridge/RawTsrUpscaleHost.cpp
        src/d3d12_bridge/RawWaterCausticsHost.cpp
        src/d3d12_bridge/RawSnowDeformHost.cpp
        src/materials/RawTextureForge.cpp
        src/audio/RawAudioDsp.cpp
        src/audio/RawAudioOutput.cpp
        src/worldgen/RawTerrainGen.cpp
        src/worldgen/RawHydrologyGen.cpp
        src/worldgen/RawBiomeWorld.cpp
        src/worldgen/RawMeshPipeline.cpp
        src/worldgen/RawFloraEngine.cpp
        src/worldgen/RawWeatherSim.cpp
        src/worldgen/RawWorldShowcase.cpp
        src/worldgen/RawDestructionEngine.cpp
        src/worldgen/RawEsmWorldLoader.cpp
        src/worldgen/RawEsmModelResolver.cpp
        src/worldgen/RawEsmNpcWeatherLoader.cpp
        src/worldgen/RawTamrielWorldStreamer.cpp
        src/worldgen/RawWorldPhysics.cpp
        src/worldgen/RawNavMeshAi.cpp
        src/worldgen/RawWorldEcosystem.cpp
        src/worldgen/RawWildlifeBoids.cpp
        src/worldgen/RawSettlementSim.cpp
        src/worldgen/RawWorldEventDirector.cpp
        src/worldgen/RawFactionDiplomacy.cpp
        src/worldgen/RawWorldSerializer.cpp
        src/worldgen/RawDungeonGen.cpp
        src/worldgen/RawSkeletonAnimEngine.cpp
        src/worldgen/RawCombatSandboxEngine.cpp
        src/worldgen/RawQuestStoryEngine.cpp
        src/worldgen/RawCraftingInventoryEngine.cpp
        src/worldgen/RawLivingWorldSchedule.cpp
        src/worldgen/RawBsaArchive.cpp
        src/worldgen/RawNifLoader.cpp
        src/engine_rt/raw_job_system.cpp
        ${RAW_ENGINE_GEN}/raw_mesh_as.h ${RAW_ENGINE_GEN}/raw_mesh_ms.h ${RAW_ENGINE_GEN}/raw_subdiv_cs.h
        ${RAW_ENGINE_GEN}/raw_terrain_as.h ${RAW_ENGINE_GEN}/raw_terrain_ms.h ${RAW_ENGINE_GEN}/raw_terrain_vs.h ${RAW_ENGINE_GEN}/raw_terrain_ps.h
        ${RAW_FLUID_HEADERS}
        ${RAW_RC_HEADERS}
        ${RAW_XPBD_HEADERS}
        ${RAW_OVERLAY_HEADERS}
        ${RAW_UILIB_HEADERS}
        ${RAW_GPU_CULL_HEADERS}
        ${RAW_LIGHT_CLUSTER_HEADERS}
        ${RAW_FROXEL_HEADERS}
        ${RAW_SHADOW_PCSS_HEADERS}
        ${RAW_GTAO_HEADERS}
        ${RAW_SSR_HEADERS}
        ${RAW_AUTO_EXPOSURE_HEADERS}
        ${RAW_CLOUD_RAYMARCH_HEADERS}
        ${RAW_POST_CINEMATIC_HEADERS}
        ${RAW_GPU_PARTICLES_HEADERS}
        ${RAW_TSR_UPSCALE_HEADERS}
        ${RAW_WATER_CAUSTICS_HEADERS}
        ${RAW_SNOW_DEFORM_HEADERS}
        ${RAW_AGX_DISPLAY_HEADERS})
    target_include_directories(raw_engine_viewer PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge ${CMAKE_CURRENT_SOURCE_DIR}/src/materials ${CMAKE_CURRENT_SOURCE_DIR}/src/worldgen ${CMAKE_CURRENT_SOURCE_DIR}/src/audio ${CMAKE_CURRENT_SOURCE_DIR}/src/engine_rt)
    target_compile_definitions(raw_engine_viewer PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(raw_engine_viewer PRIVATE d3d12 dxgi dxguid user32)
    set_target_properties(raw_engine_viewer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tools)
    add_executable(raw_bench_suite
        tools/raw_bench_suite.cpp
        src/d3d12_bridge/RawFluidSimHost.cpp
        src/d3d12_bridge/RawRadianceHost.cpp
        src/d3d12_bridge/RawXpbdSimHost.cpp
        src/d3d12_bridge/RawGpuCullHost.cpp
        src/d3d12_bridge/RawLightClusterHost.cpp
        src/d3d12_bridge/RawVolumetricFroxelHost.cpp
        src/d3d12_bridge/RawShadowPcssHost.cpp
        src/d3d12_bridge/RawGtaoHost.cpp
        src/d3d12_bridge/RawSsrHost.cpp
        src/d3d12_bridge/RawAutoExposureHost.cpp
        src/d3d12_bridge/RawCloudRaymarchHost.cpp
        src/d3d12_bridge/RawPostCinematicHost.cpp
        src/d3d12_bridge/RawGpuParticlesHost.cpp
        src/d3d12_bridge/RawTsrUpscaleHost.cpp
        src/d3d12_bridge/RawWaterCausticsHost.cpp
        src/d3d12_bridge/RawSnowDeformHost.cpp
        src/materials/RawTextureForge.cpp
        src/audio/RawAudioDsp.cpp
        src/worldgen/RawDestructionEngine.cpp
        src/worldgen/RawFloraEngine.cpp
        src/worldgen/RawHydrologyGen.cpp
        src/worldgen/RawTerrainGen.cpp
        src/worldgen/RawBiomeWorld.cpp
        src/worldgen/RawMeshPipeline.cpp
        src/worldgen/RawWeatherSim.cpp
        src/worldgen/RawWorldShowcase.cpp
        src/worldgen/RawEsmWorldLoader.cpp
        src/worldgen/RawEsmModelResolver.cpp
        src/worldgen/RawTamrielWorldStreamer.cpp
        src/worldgen/RawBsaArchive.cpp
        src/worldgen/RawNifLoader.cpp
        src/pipeline/RawAssetValidator.cpp
        src/engine_rt/raw_job_system.cpp
        ${RAW_FLUID_HEADERS}
        ${RAW_RC_HEADERS}
        ${RAW_XPBD_HEADERS}
        ${RAW_GPU_CULL_HEADERS}
        ${RAW_LIGHT_CLUSTER_HEADERS}
        ${RAW_FROXEL_HEADERS}
        ${RAW_SHADOW_PCSS_HEADERS}
        ${RAW_GTAO_HEADERS}
        ${RAW_SSR_HEADERS}
        ${RAW_AUTO_EXPOSURE_HEADERS}
        ${RAW_CLOUD_RAYMARCH_HEADERS}
        ${RAW_POST_CINEMATIC_HEADERS}
        ${RAW_GPU_PARTICLES_HEADERS}
        ${RAW_TSR_UPSCALE_HEADERS}
        ${RAW_WATER_CAUSTICS_HEADERS}
        ${RAW_SNOW_DEFORM_HEADERS})
    target_include_directories(raw_bench_suite PRIVATE ${RAW_ENGINE_GEN} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge ${CMAKE_CURRENT_SOURCE_DIR}/src/materials ${CMAKE_CURRENT_SOURCE_DIR}/src/audio ${CMAKE_CURRENT_SOURCE_DIR}/src/worldgen ${CMAKE_CURRENT_SOURCE_DIR}/src/pipeline ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(raw_bench_suite PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(raw_bench_suite PRIVATE d3d12 dxgi dxguid)
    set_target_properties(raw_bench_suite PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tools)
    target_include_directories(raw_d3d12_bridge PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/worldgen ${CMAKE_CURRENT_SOURCE_DIR}/src/materials ${CMAKE_CURRENT_SOURCE_DIR}/src/assets)
    set(RAW_RTSHADOW_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtShadow.hlsl)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_shadow.h ${RAW_ENGINE_GEN}/rt_shadow_debug.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_shadow.h -Vn kDxil_rt_shadow ${RAW_RTSHADOW_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main -DRT_DEBUG=1 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_shadow_debug.h -Vn kDxil_rt_shadow_debug ${RAW_RTSHADOW_HLSL}
        DEPENDS ${RAW_RTSHADOW_HLSL} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/LightingCommon.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/SpectralTransport.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtHit.hlsli
                ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtCacheCommon.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/Weather.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawCosmosSky.hlsli
        COMMENT "dxc: ray-traced shadows -> DXIL header"
        VERBATIM)
    if(RAW_HAS_SM69_DXIL)
        add_custom_command(
            OUTPUT ${RAW_ENGINE_GEN}/rt_shadow_69.h ${RAW_ENGINE_GEN}/rt_shadow_debug_69.h
            COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
            COMMAND ${RAW_DXC_NEXT} -T cs_6_9 -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_shadow_69.h -Vn kDxil_rt_shadow_69 ${RAW_RTSHADOW_HLSL}
            COMMAND ${RAW_DXC_NEXT} -T cs_6_9 -E main -DRT_DEBUG=1 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_shadow_debug_69.h -Vn kDxil_rt_shadow_debug_69 ${RAW_RTSHADOW_HLSL}
            DEPENDS ${RAW_RTSHADOW_HLSL} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/LightingCommon.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/SpectralTransport.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtHit.hlsli
                    ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtCacheCommon.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/Weather.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawCosmosSky.hlsli
            COMMENT "dxc 1.9+: native SM 6.9 DXIL 1.9 ray-traced shadows -> DXIL headers"
            VERBATIM)
        target_sources(raw_d3d12_bridge PRIVATE ${RAW_ENGINE_GEN}/rt_shadow_69.h ${RAW_ENGINE_GEN}/rt_shadow_debug_69.h)
    endif()
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_water.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E water ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_water.h -Vn kDxil_rt_water ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtWater.hlsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtWater.hlsl ${RAW_RTSHADOW_HLSL} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/LightingCommon.hlsli
                ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtCacheCommon.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtHit.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/Weather.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawProceduralNoise.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawSeasonsThermals.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawCosmosSky.hlsli
        COMMENT "dxc: ray-traced water -> DXIL header"
        VERBATIM)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_sss.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_sss.h -Vn kDxil_rt_sss ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/Sss.hlsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/Sss.hlsl ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/LightingCommon.hlsli
        COMMENT "dxc: skin scattering -> DXIL header"
        VERBATIM)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_normal_unpack.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_normal_unpack.h -Vn kDxil_normal_unpack ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/NormalUnpack.hlsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/NormalUnpack.hlsl
        COMMENT "dxc: DLSS-RR linear world-space normal+roughness unpack -> DXIL header"
        VERBATIM)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_hiz_downsample.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_hiz_downsample.h -Vn kDxil_hiz_downsample ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/HiZDownsample.hlsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/HiZDownsample.hlsl
        COMMENT "dxc: native DX12 compute Hi-Z depth pyramid -> DXIL header"
        VERBATIM)
    set(RAW_RTALPHA_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtAlphaUv.hlsl)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_alpha_uv.h ${RAW_ENGINE_GEN}/rt_alpha_mask.h ${RAW_ENGINE_GEN}/rt_alpha_albedo.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_alpha_uv.h -Vn kDxil_rt_alpha_uv ${RAW_RTALPHA_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E mask ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_alpha_mask.h -Vn kDxil_rt_alpha_mask ${RAW_RTALPHA_HLSL}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E mask -DALBEDO=1 ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_alpha_albedo.h -Vn kDxil_rt_alpha_albedo ${RAW_RTALPHA_HLSL}
        DEPENDS ${RAW_RTALPHA_HLSL}
        COMMENT "dxc: ray-tracing alpha coordinates and masks -> DXIL headers"
        VERBATIM)
    set(RAW_RTSKY_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtSkyFilter.hlsl)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_sky_filter.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_sky_filter.h -Vn kDxil_rt_sky_filter ${RAW_RTSKY_HLSL}
        DEPENDS ${RAW_RTSKY_HLSL} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/LightingCommon.hlsli ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RawCosmosSky.hlsli
        COMMENT "dxc: sky visibility filter -> DXIL header"
        VERBATIM)
    set(RAW_RTCACHE_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtCache.hlsl)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_cache_resolve.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E resolve ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_cache_resolve.h -Vn kDxil_rt_cache_resolve ${RAW_RTCACHE_HLSL}
        DEPENDS ${RAW_RTCACHE_HLSL} ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtCacheCommon.hlsli
        COMMENT "dxc: radiance cache resolve -> DXIL header"
        VERBATIM)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_skin.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_skin.h -Vn kDxil_rt_skin ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtSkin.hlsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtSkin.hlsl
        COMMENT "dxc: skinned positions for the ray-tracing scene -> DXIL header"
        VERBATIM)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/rt_skin_check.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_skin_check.h -Vn kDxil_rt_skin_check ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtSkinCheck.hlsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtSkinCheck.hlsl
        COMMENT "dxc: skinned triangles checked into the index pool -> DXIL header"
        VERBATIM)
    target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/RtSkin.cpp ${RAW_ENGINE_GEN}/rt_skin.h ${RAW_ENGINE_GEN}/rt_skin_check.h)
    target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/RtScene.cpp src/d3d12_bridge/RtSceneRecord.cpp src/d3d12_bridge/RtSceneStage.cpp src/d3d12_bridge/RtAlpha.cpp
                   src/d3d12_bridge/RtAlphaTex.cpp src/d3d12_bridge/RtSky.cpp src/d3d12_bridge/RtCache.cpp
                   src/d3d12_bridge/RtWater.cpp src/d3d12_bridge/BridgeWater.cpp ${RAW_ENGINE_GEN}/rt_water.h ${RAW_ENGINE_GEN}/rt_sss.h ${RAW_ENGINE_GEN}/rt_normal_unpack.h ${RAW_ENGINE_GEN}/rt_hiz_downsample.h
                   ${RAW_ENGINE_GEN}/rt_shadow.h ${RAW_ENGINE_GEN}/rt_alpha_uv.h ${RAW_ENGINE_GEN}/rt_alpha_mask.h
                   ${RAW_ENGINE_GEN}/rt_alpha_albedo.h ${RAW_ENGINE_GEN}/rt_sky_filter.h ${RAW_ENGINE_GEN}/rt_cache_resolve.h)
    if(RAW_AGILITY_DIR)
        set(RAW_RTOMM_HLSL ${CMAKE_CURRENT_SOURCE_DIR}/src/d3d12_bridge/shaders/RtOmmBake.hlsl)
        add_custom_command(
            OUTPUT ${RAW_ENGINE_GEN}/rt_omm_bake.h
            COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
            COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/rt_omm_bake.h -Vn kDxil_rt_omm_bake ${RAW_RTOMM_HLSL}
            DEPENDS ${RAW_RTOMM_HLSL}
            COMMENT "dxc: opacity micromap bake -> DXIL header"
            VERBATIM)
        target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/RtOmm.cpp ${RAW_ENGINE_GEN}/rt_omm_bake.h)
    endif()
endif()
target_sources(raw_d3d12_bridge PRIVATE src/d3d12_bridge/ShaderCompiler.cpp)
# The opacity micromap index check (tests/omm_check): needs the Agility SDK's headers and, to run, its runtime.
if(RAW_AGILITY_DIR AND RAW_ENGINE_DIR)
    add_custom_command(
        OUTPUT ${RAW_ENGINE_GEN}/omm_check_cs.h
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RAW_ENGINE_GEN}
        COMMAND ${RAW_DXC} -T cs_${RAW_SHADER_MODEL} -E main ${RAW_DXC_FLAGS} -Fh ${RAW_ENGINE_GEN}/omm_check_cs.h -Vn kDxil_omm_check ${CMAKE_CURRENT_SOURCE_DIR}/tests/omm_check/omm_check.hlsl
        DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/tests/omm_check/omm_check.hlsl
        COMMENT "dxc: opacity micromap check -> DXIL header"
        VERBATIM)
    add_executable(omm_check tests/omm_check/main.cpp ${RAW_ENGINE_GEN}/omm_check_cs.h)
    target_include_directories(omm_check PRIVATE ${RAW_ENGINE_GEN})
    target_link_libraries(omm_check PRIVATE d3d12 dxgi dxguid)
    set_target_properties(omm_check PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/tests)
endif()
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

# ============================================================================
# raw_engine.dll: the engine runtime the proxy loads from [Modules] Load.
# Call graph from the game's code in memory, vtable tracing, engine control.
# Needs Zydis (MIT): vcpkg install zydis:x64-windows-static, or RAW_ZYDIS_ROOT.
# ============================================================================
set(RAW_ZYDIS_ROOT "C:/vcpkg/installed/x64-windows-static" CACHE PATH "prefix holding include/Zydis and lib/Zydis.lib")
find_library(RAW_ZYDIS_LIB Zydis HINTS "${RAW_ZYDIS_ROOT}/lib" NO_DEFAULT_PATH)
find_library(RAW_ZYCORE_LIB Zycore HINTS "${RAW_ZYDIS_ROOT}/lib" NO_DEFAULT_PATH)
if(RAW_ZYDIS_LIB AND RAW_ZYCORE_LIB)
    add_library(raw_engine SHARED
        src/engine_rt/module_main.cpp
        src/engine_rt/rt_callgraph.cpp
        src/engine_rt/rt_vtrace.cpp
        src/engine_rt/rt_control.cpp
        src/engine_rt/rt_core_fixes.cpp
        src/engine_rt/rt_settings.cpp
        src/engine_rt/rt_drawstats.cpp
        src/engine_rt/rt_grids.cpp
        src/engine_rt/rt_audit.cpp
        src/engine_rt/rt_forensics.cpp
        src/engine_rt/rt_softbody.cpp
        src/engine_rt/rt_skeleton_ik.cpp
        src/engine_rt/rt_combat_ai.cpp
        src/engine_rt/rt_world_sim.cpp
        src/ce_runtime/CeNavMesh.cpp
        src/engine_rt/rt_weather.cpp
        src/engine_rt/rt_livestate.cpp
        src/engine_rt/rt_capture.cpp
        src/engine_rt/rt_atlas.cpp
        src/engine_rt/rt_passes.cpp
        src/engine_rt/rt_papyrus.cpp
        src/engine_rt/rt_sampler.cpp
        src/engine_rt/rt_vtime.cpp
        src/engine_rt/rt_peek.cpp
        src/engine_rt/rt_detour.cpp
        src/engine_rt/rt_view.cpp
        src/engine_rt/rt_emitters.cpp
        src/engine_rt/rt_cascades.cpp
        src/engine_rt/rt_version.cpp
        src/engine_rt/rt_zydis_offsets.cpp
        src/engine_rt/rt_terrain_lod.cpp
        src/engine_rt/rt_depth_topology.cpp
        src/engine_rt/rt_memory_guards.cpp
        src/engine_rt/ce_rebuild_arch.cpp
        src/engine_rt/rt_stealth_survival.cpp
        src/engine_rt/rt_combat_impact.cpp
        src/engine_rt/rt_economy_ledger.cpp
        src/engine_rt/rt_movement_combat.cpp
        src/engine_rt/rt_live_config.cpp
        src/engine_rt/rt_audio_bridge.cpp
        src/engine_rt/rt_encounter_balance.cpp
        src/engine_rt/rt_magic_physics.cpp
        src/engine_rt/rt_progression_economy.cpp
        src/engine_rt/rt_ai_packages.cpp
        src/engine_rt/rt_npc_behavior.cpp
        src/engine_rt/rt_crash_forensics.cpp
        src/engine_rt/rt_save_papyrus_guard.cpp
        src/engine_rt/rt_mod_load_audit.cpp
        src/engine_rt/rt_world_rules_gmst.cpp
        src/engine_rt/rt_adaptive_audio_director.cpp
        src/engine_rt/rt_physics_camera_gmst.cpp
        src/engine_rt/rt_telemetry_recorder.cpp
        src/engine_rt/rt_stealth_social_gmst.cpp
        src/engine_rt/rt_engine_health.cpp
        src/engine_rt/rt_locomotion_melee_gmst.cpp
        src/engine_rt/rt_frame_governor.cpp
        src/engine_rt/rt_alchemy_enchant_gmst.cpp
        src/engine_rt/rt_sdk_bridge.cpp
        src/engine_rt/rt_companion_hazard_gmst.cpp
        src/engine_rt/rt_console_diag.cpp
        src/engine_rt/rt_archery_mount_gmst.cpp
        src/engine_rt/rt_profile_presets.cpp
        src/engine_rt/rt_shouts_survival_gmst.cpp
        src/engine_rt/rt_shader_hotreload.cpp
        src/engine_rt/rt_crime_mount_gmst.cpp
        src/engine_rt/rt_live_dashboard.cpp
        src/engine_rt/rt_crafting_inventory_gmst.cpp
        src/engine_rt/rt_boot_selfcheck.cpp
        src/engine_rt/rt_spellcraft_gmst.cpp
        src/engine_rt/rt_hotkey_router.cpp
        src/engine_rt/rt_camera_world_gmst.cpp
        src/engine_rt/rt_safe_recovery.cpp
        src/engine_rt/rt_ai_ballistics_gmst.cpp
        src/audio/RawAudioDsp.cpp
        src/audio/RawAudioOutput.cpp
        src/engine_rt/raw_slab_allocator.cpp
        src/engine_rt/raw_job_system.cpp
        src/engine_rt/rt_patch.cpp)
    set_target_properties(raw_engine PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_include_directories(raw_engine PRIVATE include src/engine_rt src/ce_runtime src/d3d11_proxy src/audio "${RAW_ZYDIS_ROOT}/include")
    target_compile_definitions(raw_engine PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD)
    target_link_libraries(raw_engine PRIVATE ${RAW_ZYDIS_LIB} ${RAW_ZYCORE_LIB} version winmm)

    # raw_skse.dll: the SKSE plugin host (docs/SKSE_HOSTING.md). Loads Data/SKSE/Plugins with no SKSE installed.
    add_library(raw_skse SHARED
        src/skse_host/skse_host.cpp
        src/skse_host/skse_loader.cpp
        src/skse_host/skse_messaging.cpp
        src/skse_host/skse_task.cpp
        src/skse_host/skse_serialization.cpp
        src/skse_host/skse_papyrus.cpp
        src/skse_host/skse_engine.cpp
        src/skse_host/skse_addrlib.cpp
        src/engine_rt/rt_detour.cpp
        src/engine_rt/rt_patch.cpp)
    set_target_properties(raw_skse PROPERTIES
        CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_include_directories(raw_skse PRIVATE src/skse_host src/common src/engine_rt src/d3d11_proxy "${RAW_ZYDIS_ROOT}/include")
    target_compile_definitions(raw_skse PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD)
    target_link_libraries(raw_skse PRIVATE ${RAW_ZYDIS_LIB} ${RAW_ZYCORE_LIB} version shell32 ole32)

    # RAWSKSEProbe.dll: a test plugin for the host with no library under it (tests/skse_host/probe).
    add_library(raw_skse_probe SHARED tests/skse_host/probe/probe.cpp)
    set_target_properties(raw_skse_probe PROPERTIES OUTPUT_NAME RAWSKSEProbe CXX_STANDARD 20
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    target_include_directories(raw_skse_probe PRIVATE src/skse_host)
    target_compile_definitions(raw_skse_probe PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

    # ce_runtime_check: known-answer + planted-failure tests for Zydis getter offset extraction,
    # full glTF 2.0 (.glb) multi-bufferView/PBR/meshlet import, and clean-room ESM/BSA/BA2 parsers.
    add_executable(ce_runtime_check
        tests/ce_runtime_check/main.cpp
        src/ce_runtime/CeFormatReader.cpp
        src/ce_runtime/CeNifReader.cpp
        src/ce_runtime/CeNavMesh.cpp
        src/d3d12_bridge/RawAssetBridge.cpp
        src/d3d12_bridge/RawTextureBridge.cpp
        src/d3d12_bridge/RawProceduralLib.cpp
        src/d3d12_bridge/HardwareProfile.cpp
        src/d3d12_bridge/ShaderCompiler.cpp
        src/engine_rt/rt_zydis_offsets.cpp
        src/engine_rt/rt_terrain_lod.cpp
        src/engine_rt/rt_depth_topology.cpp
        src/engine_rt/rt_memory_guards.cpp
        src/engine_rt/ce_rebuild_arch.cpp
        src/engine_rt/rt_stealth_survival.cpp
        src/engine_rt/rt_combat_impact.cpp
        src/engine_rt/rt_economy_ledger.cpp
        src/engine_rt/rt_movement_combat.cpp
        src/engine_rt/rt_live_config.cpp
        src/engine_rt/rt_audio_bridge.cpp
        src/engine_rt/rt_encounter_balance.cpp
        src/engine_rt/rt_magic_physics.cpp
        src/engine_rt/rt_progression_economy.cpp
        src/engine_rt/rt_ai_packages.cpp
        src/engine_rt/rt_npc_behavior.cpp
        src/engine_rt/rt_crash_forensics.cpp
        src/engine_rt/rt_save_papyrus_guard.cpp
        src/engine_rt/rt_mod_load_audit.cpp
        src/engine_rt/rt_world_rules_gmst.cpp
        src/engine_rt/rt_adaptive_audio_director.cpp
        src/engine_rt/rt_physics_camera_gmst.cpp
        src/engine_rt/rt_telemetry_recorder.cpp
        src/engine_rt/rt_stealth_social_gmst.cpp
        src/engine_rt/rt_engine_health.cpp
        src/engine_rt/rt_locomotion_melee_gmst.cpp
        src/engine_rt/rt_frame_governor.cpp
        src/engine_rt/rt_alchemy_enchant_gmst.cpp
        src/engine_rt/rt_sdk_bridge.cpp
        src/engine_rt/rt_companion_hazard_gmst.cpp
        src/engine_rt/rt_console_diag.cpp
        src/engine_rt/rt_archery_mount_gmst.cpp
        src/engine_rt/rt_profile_presets.cpp
        src/engine_rt/rt_shouts_survival_gmst.cpp
        src/engine_rt/rt_shader_hotreload.cpp
        src/engine_rt/rt_crime_mount_gmst.cpp
        src/engine_rt/rt_live_dashboard.cpp
        src/engine_rt/rt_crafting_inventory_gmst.cpp
        src/engine_rt/rt_boot_selfcheck.cpp
        src/engine_rt/rt_spellcraft_gmst.cpp
        src/engine_rt/rt_hotkey_router.cpp
        src/engine_rt/rt_camera_world_gmst.cpp
        src/engine_rt/rt_safe_recovery.cpp
        src/engine_rt/rt_ai_ballistics_gmst.cpp
        src/engine_rt/raw_slab_allocator.cpp
        src/engine_rt/raw_job_system.cpp
        src/engine_rt/rt_softbody.cpp
        src/engine_rt/rt_skeleton_ik.cpp
        src/engine_rt/rt_combat_ai.cpp
        src/engine_rt/rt_world_sim.cpp
        src/worldgen/RawTerrainGen.cpp
        src/worldgen/RawBiomeWorld.cpp
        src/worldgen/RawMeshPipeline.cpp
        src/worldgen/RawFloraEngine.cpp
        src/worldgen/RawWeatherSim.cpp
        src/worldgen/RawWorldShowcase.cpp
        src/worldgen/RawEsmWorldLoader.cpp
        src/worldgen/RawEsmModelResolver.cpp
        src/worldgen/RawEsmNpcWeatherLoader.cpp
        src/worldgen/RawTamrielWorldStreamer.cpp
        src/worldgen/RawWorldPhysics.cpp
        src/worldgen/RawNavMeshAi.cpp
        src/worldgen/RawWorldEcosystem.cpp
        src/worldgen/RawWildlifeBoids.cpp
        src/worldgen/RawSettlementSim.cpp
        src/worldgen/RawWorldEventDirector.cpp
        src/worldgen/RawFactionDiplomacy.cpp
        src/worldgen/RawWorldSerializer.cpp
        src/worldgen/RawDungeonGen.cpp
        src/worldgen/RawSkeletonAnimEngine.cpp
        src/worldgen/RawCombatSandboxEngine.cpp
        src/worldgen/RawQuestStoryEngine.cpp
        src/worldgen/RawCraftingInventoryEngine.cpp
        src/worldgen/RawLivingWorldSchedule.cpp
        src/worldgen/RawGameTheoryAi.cpp
        src/worldgen/RawBsaArchive.cpp
        src/worldgen/RawNifLoader.cpp
        src/worldgen/RawDdsDecoder.cpp
        src/worldgen/RawHydrologyGen.cpp
        src/worldgen/RawDestructionEngine.cpp
        src/audio/RawAudioDsp.cpp
        src/audio/RawAudioOutput.cpp
        src/materials/RawMaterialLib.cpp
        src/materials/RawAssetForge.cpp
        src/materials/RawTextureForge.cpp
        src/assets/RawOverrideRegistry.cpp
        src/assets/RawAssetScaffold.cpp
        src/pipeline/RawAssetValidator.cpp
        src/pipeline/RawForgePipeline.cpp
        src/ui/RawInputEngine.cpp
        tests/test_flora_engine.cpp
        tests/test_weather_sim.cpp
        tests/test_skeleton_physics.cpp
        tests/test_gameplay_mechanics.cpp
        tests/test_pipeline_forge.cpp
        tests/test_stealth_survival.cpp
        tests/test_esm_world_loader.cpp
        tests/test_esm_model_resolver.cpp
        tests/test_esm_world_meta.cpp
        tests/test_tamriel_streamer.cpp
        tests/test_world_physics.cpp
        tests/test_navmesh_ai.cpp
        tests/test_combat_impact.cpp
        tests/test_bsa_archive.cpp
        tests/test_core_runtime.cpp
        tests/test_nif_loader.cpp
        tests/test_destruction_engine.cpp
        tests/test_economy_ledger.cpp
        tests/test_dds_decoder.cpp
        tests/test_hydrology_gen.cpp
        tests/test_movement_combat.cpp
        tests/test_live_config.cpp
        tests/test_encounter_balance.cpp
        tests/test_magic_physics.cpp
        tests/test_progression_economy.cpp
        tests/test_ai_packages.cpp
        tests/test_npc_behavior.cpp
        tests/test_crash_forensics.cpp
        tests/test_save_papyrus_guard.cpp
        tests/test_mod_load_audit.cpp
        tests/test_world_rules_gmst.cpp
        tests/test_adaptive_audio_director.cpp
        tests/test_physics_camera_gmst.cpp
        tests/test_telemetry_recorder.cpp
        tests/test_stealth_social_gmst.cpp
        tests/test_engine_health.cpp
        tests/test_locomotion_melee_gmst.cpp
        tests/test_frame_governor.cpp
        tests/test_alchemy_enchant_gmst.cpp
        tests/test_sdk_abi.cpp
        tests/test_companion_hazard_gmst.cpp
        tests/test_console_diag.cpp
        tests/test_archery_mount_gmst.cpp
        tests/test_profile_presets.cpp
        tests/test_shouts_survival_gmst.cpp
        tests/test_shader_hotreload.cpp
        tests/test_crime_mount_gmst.cpp
        tests/test_live_dashboard.cpp
        tests/test_crafting_inventory_gmst.cpp
        tests/test_boot_selfcheck.cpp
        tests/test_spellcraft_gmst.cpp
        tests/test_hotkey_router.cpp
        tests/test_camera_world_gmst.cpp
        tests/test_safe_recovery.cpp
        tests/test_ai_ballistics_gmst.cpp
        tests/test_skeleton_stencil_math.cpp
        tests/test_cam_sweep_control.cpp
        tests/test_world_ecosystem.cpp
        tests/test_wildlife_boids.cpp
        tests/test_settlement_sim.cpp
        tests/test_world_event_director.cpp
        tests/test_faction_diplomacy.cpp
        tests/test_world_serializer.cpp
        tests/test_dungeon_gen.cpp
        tests/test_skeleton_anim_engine.cpp
        tests/test_combat_sandbox_engine.cpp
        tests/test_quest_story_engine.cpp
        tests/test_crafting_inventory_engine.cpp
        tests/test_living_world_schedule.cpp
        tests/test_game_theory_ai.cpp
        tests/test_audio_dsp.cpp
        tests/test_audio_output.cpp
        tests/test_texture_forge.cpp)
    set_target_properties(ce_runtime_check PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests)
    target_include_directories(ce_runtime_check PRIVATE include src/engine_rt src/d3d12_bridge src/ce_runtime src/worldgen src/audio src/materials src/assets src/pipeline src/ui "${RAW_ZYDIS_ROOT}/include")
    target_compile_definitions(ce_runtime_check PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN ZYDIS_STATIC_BUILD ZYCORE_STATIC_BUILD RAW_SIM_PURE_TEST=1 RAW_WEATHER_SIM_NO_MAIN=1 RAW_GAMEPLAY_MECHANICS_NO_MAIN=1 RAW_STEALTH_SURVIVAL_NO_MAIN=1 RAW_COMBAT_IMPACT_NO_MAIN=1 RAW_ECONOMY_LEDGER_NO_MAIN=1 RAW_MOVEMENT_COMBAT_NO_MAIN=1 RAW_LIVE_CONFIG_NO_MAIN=1 RAW_ENCOUNTER_BALANCE_NO_MAIN=1 RAW_MAGIC_PHYSICS_NO_MAIN=1 RAW_PROGRESSION_ECONOMY_NO_MAIN=1 RAW_AI_PACKAGES_NO_MAIN=1 RAW_WORLD_RULES_GMST_NO_MAIN=1 RAW_PHYSICS_CAMERA_GMST_NO_MAIN=1 RAW_STEALTH_SOCIAL_GMST_NO_MAIN=1 RAW_LOCOMOTION_MELEE_GMST_NO_MAIN=1 RAW_ALCHEMY_ENCHANT_GMST_NO_MAIN=1 RAW_COMPANION_HAZARD_GMST_NO_MAIN=1 RAW_ARCHERY_MOUNT_GMST_NO_MAIN=1 RAW_SHOUTS_SURVIVAL_GMST_NO_MAIN=1 RAW_CRIME_MOUNT_GMST_NO_MAIN=1 RAW_CRAFTING_INVENTORY_GMST_NO_MAIN=1 RAW_SPELLCRAFT_GMST_NO_MAIN=1 RAW_CAMERA_WORLD_GMST_NO_MAIN=1 RAW_AI_BALLISTICS_GMST_NO_MAIN=1 RAW_SKELETON_STENCIL_MATH_NO_MAIN=1 RAW_CAM_SWEEP_NO_MAIN=1)
    target_link_libraries(ce_runtime_check PRIVATE ${RAW_ZYDIS_LIB} ${RAW_ZYCORE_LIB} windowscodecs ole32 dxgi)

    # raw_forge_cli: standalone C++23 first-principles authoring, batch baking & validation CLI
    add_executable(raw_forge_cli
        tools/raw_forge_cli.cpp
        src/pipeline/RawAssetValidator.cpp
        src/pipeline/RawForgePipeline.cpp
        src/worldgen/RawTerrainGen.cpp
        src/worldgen/RawHydrologyGen.cpp
        src/worldgen/RawBiomeWorld.cpp
        src/worldgen/RawMeshPipeline.cpp
        src/worldgen/RawFloraEngine.cpp
        src/worldgen/RawWeatherSim.cpp
        src/worldgen/RawWorldShowcase.cpp
        src/worldgen/RawEsmWorldLoader.cpp
        src/worldgen/RawEsmModelResolver.cpp
        src/worldgen/RawEsmNpcWeatherLoader.cpp
        src/worldgen/RawTamrielWorldStreamer.cpp
        src/worldgen/RawWorldPhysics.cpp
        src/worldgen/RawNavMeshAi.cpp
        src/worldgen/RawWorldEcosystem.cpp
        src/worldgen/RawWildlifeBoids.cpp
        src/worldgen/RawSettlementSim.cpp
        src/worldgen/RawWorldEventDirector.cpp
        src/worldgen/RawFactionDiplomacy.cpp
        src/worldgen/RawWorldSerializer.cpp
        src/worldgen/RawDungeonGen.cpp
        src/worldgen/RawSkeletonAnimEngine.cpp
        src/worldgen/RawCombatSandboxEngine.cpp
        src/worldgen/RawQuestStoryEngine.cpp
        src/worldgen/RawCraftingInventoryEngine.cpp
        src/worldgen/RawLivingWorldSchedule.cpp
        src/worldgen/RawBsaArchive.cpp
        src/worldgen/RawNifLoader.cpp
        src/worldgen/RawDdsDecoder.cpp
        src/worldgen/RawDestructionEngine.cpp
        src/engine_rt/raw_job_system.cpp
        src/materials/RawMaterialLib.cpp
        src/materials/RawTextureForge.cpp)
    set_target_properties(raw_forge_cli PROPERTIES CXX_STANDARD 23 CXX_STANDARD_REQUIRED ON
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tools)
    target_include_directories(raw_forge_cli PRIVATE src/pipeline src/worldgen src/materials)
    target_compile_definitions(raw_forge_cli PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    message(STATUS "raw_engine: Zydis not found under ${RAW_ZYDIS_ROOT}; module not built")
endif()
