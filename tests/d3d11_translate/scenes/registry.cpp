// The scene list, in suite order.
#include "../scene.h"
#include <cstring>

namespace txtest {

bool RunTriangle(SceneCtx& c, Image& out);
bool RunTexturedInstanced(SceneCtx& c, Image& out);
bool RunDepthPrepassLit(SceneCtx& c, Image& out);
bool RunAlphaBlend(SceneCtx& c, Image& out);
bool RunRttPost(SceneCtx& c, Image& out);
bool RunShadowMap(SceneCtx& c, Image& out);
bool RunCompute(SceneCtx& c, Image& out);
bool RunManyDraws(SceneCtx& c, Image& out);
bool RunDynamicVb(SceneCtx& c, Image& out);
bool RunMsaaResolve(SceneCtx& c, Image& out);
bool RunMipGeneration(SceneCtx& c, Image& out);
bool RunOcclusionQuery(SceneCtx& c, Image& out);

const std::vector<Scene>& AllScenes()
{
    // tolerance: 0 where every output value is an exact 8-bit code on any back end;
    // 1 for plain interpolation; 2 where float filtering or lighting maths is involved.
    static const std::vector<Scene> scenes = {
        {"triangle", RunTriangle, 1},
        {"textured_instanced", RunTexturedInstanced, 2},
        {"depth_prepass_lit", RunDepthPrepassLit, 2},
        {"alpha_blend", RunAlphaBlend, 1},
        {"rtt_post", RunRttPost, 2},
        {"shadow_map", RunShadowMap, 2},
        {"compute", RunCompute, 1},
        {"many_draws", RunManyDraws, 1},
        {"dynamic_vb", RunDynamicVb, 0},
        {"msaa_resolve", RunMsaaResolve, 2},
        {"mip_generation", RunMipGeneration, 2},
        {"occlusion_query", RunOcclusionQuery, 1},
    };
    return scenes;
}

const Scene* FindScene(const char* name)
{
    if (!name) return nullptr;
    for (const Scene& s : AllScenes())
        if (strcmp(s.name, name) == 0) return &s;
    return nullptr;
}

}  // namespace txtest
