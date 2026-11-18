# D3D11 call-stream recorder and replayer (raw::rec).
# Standalone:  cmake -S src/dx12/translate/record -B build-record -G Ninja -DCMAKE_BUILD_TYPE=Release
# Embedded:    add_subdirectory(src/dx12/translate/record)
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(raw_d3d11_record LANGUAGES CXX)
endif()

function(raw_rec_settings target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    target_compile_definitions(${target} PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
    set_target_properties(${target} PROPERTIES
        CXX_EXTENSIONS OFF
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /EHsc)
    endif()
endfunction()

add_library(raw_d3d11_record STATIC
    rec_format.cpp
    rec_writer.cpp
    rec_reader.cpp
    rec_dump.cpp
    rec_util.cpp
    rec_serial.cpp
    rec_recorder.cpp
    rec_define.cpp
    rec_contents.cpp
    rec_snapshot.cpp
    rec_device.cpp
    rec_context_state.cpp
    rec_context_draw.cpp
    rec_replay.cpp
    rec_replay_define.cpp
    rec_replay_shader.cpp
    rec_replay_state.cpp
    rec_replay_data.cpp)
target_include_directories(raw_d3d11_record PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(raw_d3d11_record PUBLIC d3d11 dxguid)
raw_rec_settings(raw_d3d11_record)

add_executable(d3d11_stream_dump d3d11_stream_dump.cpp)
target_link_libraries(d3d11_stream_dump PRIVATE raw_d3d11_record)
raw_rec_settings(d3d11_stream_dump)

# Self-test on the WARP software rasteriser. Only the test uses d3dcompiler.
add_executable(d3d11_record_selftest
    tests/selftest_main.cpp
    tests/selftest_scene.cpp
    tests/selftest_checks.cpp)
target_link_libraries(d3d11_record_selftest PRIVATE raw_d3d11_record d3dcompiler)
raw_rec_settings(d3d11_record_selftest)
add_dependencies(d3d11_record_selftest d3d11_stream_dump)
