# D3D11 test scenes for the D3D11-to-D3D12 translation layer.
# Standalone:  cmake -S tests/d3d11_translate -B build-scenes -G Ninja -DCMAKE_BUILD_TYPE=Release
# Or from the repo root with add_subdirectory(tests/d3d11_translate).
cmake_minimum_required(VERSION 3.20)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(d3d11_translate_scenes LANGUAGES CXX)
endif()

add_library(tx_scenes STATIC
    scene_util.cpp
    scene_geom.cpp
    scene_image.cpp
    scenes/registry.cpp
    scenes/triangle.cpp
    scenes/textured_instanced.cpp
    scenes/depth_prepass_lit.cpp
    scenes/alpha_blend.cpp
    scenes/rtt_post.cpp
    scenes/shadow_map.cpp
    scenes/compute.cpp
    scenes/many_draws.cpp
    scenes/many_draws_record.cpp
    scenes/dynamic_vb.cpp
    scenes/msaa_resolve.cpp
    scenes/mip_generation.cpp
    scenes/occlusion_query.cpp
    scenes/gbuffer_frame.cpp
    scenes/gbuffer_frame_res.cpp
    scenes/volumes_cubes_arrays.cpp
    scenes/volumes_cubes_arrays_res.cpp
    scenes/volumes_cubes_arrays_shaders.cpp
    scenes/volumes_cubes_arrays_passes.cpp
    scenes/gpu_cb_copy.cpp
)
target_include_directories(tx_scenes PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(tx_scenes PUBLIC d3d11 d3dcompiler dxguid)

add_executable(d3d11_scenes_baseline baseline_main.cpp)
target_link_libraries(d3d11_scenes_baseline PRIVATE tx_scenes)

foreach(tgt tx_scenes d3d11_scenes_baseline)
    target_compile_features(${tgt} PUBLIC cxx_std_20)
    target_compile_definitions(${tgt} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    set_target_properties(${tgt} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(MSVC)
        target_compile_options(${tgt} PRIVATE /W4 /permissive-)
    endif()
endforeach()
