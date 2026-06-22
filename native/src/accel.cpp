#include "raw/accel.hpp"
namespace raw {
void LinearAccel::build(const Scene& s){
    tris.clear();
    for (const Mesh& m : s.meshes)
        for (size_t i=0;i+2<m.indices.size();i+=3)
            tris.push_back(Tri{ m.positions[m.indices[i]],
                                m.positions[m.indices[i+1]],
                                m.positions[m.indices[i+2]] });
}
bool LinearAccel::occluded(const Ray& r, float maxDist) const {
    float t,u,v;
    for (const Tri& tri : tris)
        if (intersectTri(r, tri, t,u,v) && t < maxDist) return true;
    return false;
}
}
