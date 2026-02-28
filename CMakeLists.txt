cmake_minimum_required(VERSION 3.24)

# Use static triplet for vcpkg
set(VCPKG_TARGET_TRIPLET "x64-windows-static" CACHE STRING "")

# Use static runtime (/MT instead of /MD)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")

project(
    SkyrimBridge_v3
    VERSION 3.0.0
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find dependencies
find_package(CommonLibSSE REQUIRED CONFIG)
find_package(imgui CONFIG REQUIRED)

# ============================================================================
# SkyrimBridge v3.0 Plugin — Self-Contained Build
#
# All source files are under src/:
#   src/core/    — Phase 1: Base SkyrimBridge (10 trackers + main + hooks)
#   src/         — Phases 2-4: Weather params, shared memory, ParmLink compat
# ============================================================================

add_commonlibsse_plugin(
    ${PROJECT_NAME}
    AUTHOR "Zain Dana Harper"
    SOURCES
        # Phase 1: Core SkyrimBridge
        src/core/main.cpp
        src/core/BridgeData.cpp
        src/core/ENBInterface.cpp
        src/core/DebugGUI.cpp
        src/core/D3D11Hook.cpp
        src/core/CelestialTracker.cpp
        src/core/AtmosphereTracker.cpp
        src/core/FogTracker.cpp
        src/core/WeatherTracker.cpp
        src/core/PlayerTracker.cpp
        src/core/CameraTracker.cpp
        src/core/InteriorTracker.cpp
        src/core/ShadowTracker.cpp
        src/core/EffectsTracker.cpp
        src/core/RenderTracker.cpp

        # Phase 2: Weather Parameter Computer
        src/WeatherParameterComputer.cpp

        # Phase 3: Shared Memory Bridge
        src/SharedMemoryBridge.cpp

        # Phase 4: ParmLink Compatibility Layer
        src/ParmLinkCompat.cpp
)

target_link_libraries(${PROJECT_NAME} PRIVATE
    imgui::imgui
    d3d11
    dxgi
)

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/src/core
)

target_compile_definitions(${PROJECT_NAME} PRIVATE
    ENABLE_SKYRIM_SE=1
    ENABLE_SKYRIM_AE=1
    SKYRIMBRIDGE_V3=1
)

# Precompiled header
target_precompile_headers(${PROJECT_NAME} PRIVATE
    src/core/PCH.h
)

# Create config directory in output (must be before copy)
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config"
    COMMENT "Creating config directory"
)

# Install configuration files to the build output
add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/config/WeatherParams.ini"
        "$<TARGET_FILE_DIR:${PROJECT_NAME}>/config/WeatherParams.ini"
    COMMENT "Copying WeatherParams.ini to build directory"
)
