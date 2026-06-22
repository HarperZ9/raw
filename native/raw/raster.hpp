#pragma once
#include "raw/gbuffer.hpp"
#include "raw/scene.hpp"
namespace raw { GBuffer rasterize(const Scene& scene, int w, int h, Arena* arena = nullptr); }
