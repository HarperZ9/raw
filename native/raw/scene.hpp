#pragma once
#include "raw/vec.hpp"
#include "raw/mat.hpp"
#include "raw/arena_allocator.hpp"
#include <vector>
namespace raw {
struct Material { Vec3 albedo{0.8f,0.8f,0.8f}; };
struct Mesh {
    std::vector<Vec3, ArenaAllocator<Vec3>> positions, normals;
    std::vector<int,  ArenaAllocator<int>>  indices;
    Material material;
    Mesh() = default;
    explicit Mesh(Arena* a)
        : positions(ArenaAllocator<Vec3>(a)), normals(ArenaAllocator<Vec3>(a)),
          indices(ArenaAllocator<int>(a)) {}
};
struct Light { Vec3 dir{0,-1,0}; float intensity{1.0f}; };
struct Camera {
    Vec3 eye{0,0,5}, center{0,0,0}, up{0,1,0};
    float fovy{1.0f}, aspect{1.0f}, nearZ{0.1f}, farZ{100.0f};
    Mat4 view() const { return lookAt(eye, center, up); }
    Mat4 proj() const { return perspective(fovy, aspect, nearZ, farZ); }
};
struct Scene {
    std::vector<Mesh,  ArenaAllocator<Mesh>>  meshes;
    std::vector<Light, ArenaAllocator<Light>> lights;
    Camera camera;
    Scene() = default;
    explicit Scene(Arena* a)
        : meshes(ArenaAllocator<Mesh>(a)), lights(ArenaAllocator<Light>(a)) {}
};
Scene buildTestScene(int w, int h, Arena* arena = nullptr);
}
