#include "raw/scene.hpp"
#include <utility>
namespace raw {
static void addQuad(Mesh& m, Vec3 a, Vec3 b, Vec3 c, Vec3 d, Vec3 n){
    int base = (int)m.positions.size();
    for (Vec3 p : {a,b,c,d}){ m.positions.push_back(p); m.normals.push_back(n); }
    for (int i : {0,1,2, 0,2,3}) m.indices.push_back(base + i);
}
static void addBox(Mesh& m, Vec3 c, float h){
    Vec3 p000{c.x-h,c.y-h,c.z-h}, p111{c.x+h,c.y+h,c.z+h};
    addQuad(m, {p000.x,p000.y,p111.z},{p111.x,p000.y,p111.z},{p111.x,p111.y,p111.z},{p000.x,p111.y,p111.z}, {0,0,1});
    addQuad(m, {p111.x,p000.y,p000.z},{p000.x,p000.y,p000.z},{p000.x,p111.y,p000.z},{p111.x,p111.y,p000.z}, {0,0,-1});
    addQuad(m, {p000.x,p111.y,p111.z},{p111.x,p111.y,p111.z},{p111.x,p111.y,p000.z},{p000.x,p111.y,p000.z}, {0,1,0});
    addQuad(m, {p000.x,p000.y,p000.z},{p111.x,p000.y,p000.z},{p111.x,p000.y,p111.z},{p000.x,p000.y,p111.z}, {0,-1,0});
    addQuad(m, {p111.x,p000.y,p111.z},{p111.x,p000.y,p000.z},{p111.x,p111.y,p000.z},{p111.x,p111.y,p111.z}, {1,0,0});
    addQuad(m, {p000.x,p000.y,p000.z},{p000.x,p000.y,p111.z},{p000.x,p111.y,p111.z},{p000.x,p111.y,p000.z}, {-1,0,0});
}
Scene buildTestScene(int w, int h, Arena* arena){
    Scene s(arena);
    s.meshes.reserve(2);
    Mesh plane(arena); plane.material.albedo = {0.7f,0.7f,0.7f};
    addQuad(plane, {-5,0,-5},{5,0,-5},{5,0,5},{-5,0,5}, {0,1,0});
    s.meshes.push_back(std::move(plane));
    Mesh box(arena); box.material.albedo = {0.8f,0.3f,0.2f};
    addBox(box, {0,1,0}, 1.0f);
    s.meshes.push_back(std::move(box));
    s.lights.reserve(1);
    s.lights.push_back(Light{ normalize(Vec3{-0.4f,-1.0f,-0.3f}), 1.0f });
    s.camera.eye = {4,4,6}; s.camera.center = {0,1,0}; s.camera.up = {0,1,0};
    s.camera.fovy = 0.9f; s.camera.aspect = (float)w/(float)h;
    return s;
}
}
