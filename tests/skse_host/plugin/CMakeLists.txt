# RAWSKSETest: RAW's SKSE test plugin for the host (docs/SKSE_HOSTING.md). Original work, MIT.
# A standalone project, so the RAW build itself needs no CommonLibSSE:
#   cmake -S tests/skse_host/plugin -B <build> -G Ninja -DCMAKE_BUILD_TYPE=Release
#         -DCMAKE_PREFIX_PATH=<prefix holding CommonLibSSE-NG, fmt, spdlog (x64-windows-static)>
cmake_minimum_required(VERSION 3.24)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "")
project(RAWSKSETest VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(CommonLibSSE REQUIRED CONFIG)
add_commonlibsse_plugin(RAWSKSETest AUTHOR "RAW" SOURCES main.cpp)
target_compile_definitions(RAWSKSETest PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
target_precompile_headers(RAWSKSETest PRIVATE PCH.h)
