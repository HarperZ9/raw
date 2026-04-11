cmake_minimum_required(VERSION 3.20)

project(ENB_GTAV VERSION 0.492 LANGUAGES CXX ASM_MASM)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# ============================================================================
# ENBSeries v0.492 for GTA V Legacy (DX11)
#
# Standalone d3d11.dll wrapper. Zero external dependencies.
# Real d3d11.dll loaded from System32 at runtime.
# ============================================================================

add_library(enb_gtav SHARED
    # Phase 1: Core skeleton
    enb_main.cpp
    ENBState.cpp
    ENBSwapChain.cpp
    ENBExports_SDK.cpp
    ENBExports_API.cpp
    ENBExports_TwShim.cpp
    enb_trampolines.asm
    enb_gtav.def
    # Phase 2: Configuration system
    ConfigManager.cpp
    TimeOfDaySystem.cpp
    WeatherSystem.cpp
    # Phase 3: ENBHelper bridge
    ENBHelperBridge.cpp
    # Phase 4: Shader effects pipeline
    EffectCompiler.cpp
    RenderTargetPool.cpp
    PostProcessPipeline.cpp
    # Phase 5: UI + Input
    InputManager.cpp
    ScreenshotCapture.cpp
    # Phase 6: Plugin system
    PluginLoader.cpp
    # Additional systems
    FPSLimiter.cpp
    ProxyLibrary.cpp
    ENBParamsCompute.cpp
    ENBLog.cpp
    # Version resource (file properties in Windows Explorer)
    enb_version.rc
)

set_target_properties(enb_gtav PROPERTIES
    OUTPUT_NAME "d3d11"
    SUFFIX ".dll"
)

target_include_directories(enb_gtav PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_compile_definitions(enb_gtav PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
    ENB_GTAV=1
    _CRT_SECURE_NO_WARNINGS   # sscanf/fopen are safe in our controlled usage
)

# Strict warning level + treat warnings as errors for release quality
# Only apply to C++ files (not MASM assembler)
target_compile_options(enb_gtav PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:/W4>          # Warning level 4
    $<$<COMPILE_LANGUAGE:CXX>:/WX>          # Warnings as errors
    $<$<COMPILE_LANGUAGE:CXX>:/wd4100>      # unreferenced formal parameter (stubs)
    $<$<COMPILE_LANGUAGE:CXX>:/wd4201>      # nameless struct/union (Windows headers)
    $<$<COMPILE_LANGUAGE:CXX>:/wd4324>      # structure padded due to alignment
    $<$<COMPILE_LANGUAGE:CXX>:/GS>          # Buffer security checks
    $<$<COMPILE_LANGUAGE:CXX>:/sdl>         # Additional security checks
    $<$<COMPILE_LANGUAGE:CXX>:/guard:cf>    # Control flow guard
)

# Do NOT link d3d11.lib or dxgi.lib — we ARE d3d11.dll.
# Only implicit kernel32/user32 from Windows SDK.

# ============================================================================
# version.dll — Bootstrap proxy
#
# GTA V v1.0.3751+ blocks d3d11.dll from the app directory.
# This version.dll proxy forces our d3d11.dll to load early.
# Deploy BOTH version.dll AND d3d11.dll to the game directory.
# ============================================================================

add_library(enb_bootstrap SHARED
    enb_bootstrap.cpp
    enb_bootstrap.def
)

set_target_properties(enb_bootstrap PROPERTIES
    OUTPUT_NAME "version"
    SUFFIX ".dll"
    CXX_STANDARD 20
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

target_compile_definitions(enb_bootstrap PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

# Test executables
add_executable(test_enb test_enb.cpp)
target_compile_definitions(test_enb PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)

add_executable(test_shaders test_shaders.cpp)
target_compile_definitions(test_shaders PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
