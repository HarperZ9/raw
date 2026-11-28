// gbuffer_patch_check: the shader patch against known scenes.
//
// Each case compiles a test shader, patches it, draws one triangle at depth
// 0.25 into eight float targets and compares every new channel with the value
// the scene fixes. The original outputs must come back unchanged.
//
//   gbuffer_patch_check [--warp]             the cases; exit code 0 when all pass
//   gbuffer_patch_check --dir <folder> [--out file.json]
//       patch every ps_*.dxbc in a folder (a FrameLedger capture), check the
//       container hash of each, create each patched shader on the device
//
// Original work, MIT.
#include "../../src/core/dxbc/DxbcContainer.h"
#include "../../src/core/dxbc/GBufferPatch.h"
#include "fixtures.h"
#include "shaders.h"
#include "survey.h"

#include <cmath>

using namespace bench;
using namespace raw::dxbc;

namespace
{
int g_failed = 0;
bool g_dump = false;   // --dump prints the lit case before and after the patch

void Check(bool ok, const char* what)
{
    printf("  %s  %s\n", ok ? "pass" : "FAIL", what);
    g_failed += !ok;
}
bool Near(const Pixel& p, float x, float y, float z, float w, float tol = 2e-5f)
{
    const float e[4] = {x, y, z, w};
    for (int i = 0; i < 4; ++i)
        if (!(std::fabs(p.v[i] - e[i]) <= tol)) return false;
    return true;
}
void Show(const char* name, const Pixel& p) { printf("    %-10s %.6f %.6f %.6f %.6f\n", name, p.v[0], p.v[1], p.v[2], p.v[3]); }

std::vector<uint8_t> Patched(const std::vector<uint8_t>& code, PatchInfo* info, uint8_t flags = 0)
{
    std::vector<uint8_t> out;
    PatchOptions opt;
    opt.classFlags = flags;
    if (!PatchForGBuffer(code.data(), code.size(), opt, &out, info)) printf("    refused: %s\n", info->refused ? info->refused : "?");
    return out;
}

void SetScene(Bench& b)
{
    float mat[36] = {};
    const float tint[3] = {0.5f, 1.0f, 0.25f};
    memcpy(mat, tint, sizeof tint);
    mat[16] = 0.5f; mat[17] = 0.6f; mat[18] = 0.7f; mat[19] = 64.0f;     // element 4: specular colour, shininess
    mat[32] = 2.0f; mat[33] = 0.1f; mat[34] = 0.2f; mat[35] = 0.3f;      // element 8: emissive scale, colour
    mat[8] = mat[9] = mat[10] = 1.0f; mat[11] = 0.4f;                    // element 2: the effect's colour scale
    mat[24] = 1.0f;                                                      // element 6: the water's normal scale
    mat[3] = 8.0f;                                                       // element 0 w: the water's exponent
    b.SetConstants(1, mat, 9);
    float geo[120] = {};
    geo[2] = 1.0f;                                                       // element 0: light direction
    geo[4] = 1.0f; geo[5] = 0.9f; geo[6] = 0.8f;                         // element 1: light colour
    geo[14] = 1.0f;                                                      // element 3 z: alpha scale
    geo[32] = geo[33] = geo[34] = geo[35] = 1.0f;                        // element 8: the effect's tint
    geo[116] = 2.0f;                                                     // element 29 x: light count
    b.SetConstants(2, geo, 30);
    b.SetTexel(0, 0.2f, 0.4f, 0.6f, 0.75f);
    b.SetTexel(1, 0.6f, 0.5f, 1.0f, 0.7f);
}

// View rotation rows for the water case: view x is world x, view y is world z, view z is world y.
void SetView(Bench& b, bool swapped)
{
    float frame[180] = {};
    const float rows[3][3] = {{1, 0, 0}, {0, 0, 1}, {0, 1, 0}};
    for (int r = 0; r < 3; ++r) memcpy(&frame[4 * (swapped ? (r == 0 ? 1 : r == 1 ? 0 : 2) : r)], rows[r], 12);
    b.SetConstants(12, frame, 45);
}

void LitCase(Bench& b)
{
    printf("lit surface\n");
    const std::vector<uint8_t> code = Bench::Compile((std::string(kCommon) + kLit).c_str(), "ps_5_0");
    PatchInfo info;
    const std::vector<uint8_t> patched = Patched(code, &info, material::FirstPersonFlag);
    Check(HashMatches(code.data(), code.size()), "the compiler's container hash equals the computed hash");
    Check(!patched.empty() && HashMatches(patched.data(), patched.size()), "the patched container carries a matching hash");
    Check(info.kind == ShaderKind::Lighting && info.albedo && info.albedoInPlace && info.specularMask && info.gloss && info.emissive,
          "reflection finds the kind, base colour in place, mask, gloss and emissive");
    if (g_dump) printf("%s\n%s\n", Bench::Disassemble(code).c_str(), Bench::Disassemble(patched).c_str());
    Pixel o[kTargets], p[kTargets];
    if (patched.empty() || !b.Draw(code, o) || !b.Draw(patched, p)) { Check(false, "draws"); return; }
    Check(memcmp(o, p, 3 * sizeof(Pixel)) == 0, "outputs 0 to 2 are bit-identical to the original's");
    Check(o[4].v[0] == -1.0f, "the original leaves target 4 at its clear value");
    const float alb[3] = {0.2f * 0.5f, 0.4f * 1.0f, 0.6f * 0.25f};
    Check(Near(p[4], alb[0], alb[1], alb[2], 0.75f), "albedo: texture times tint, alpha in a");
    Check(Near(p[5], 0.7f, 6.0f / 13.0f, float(material::Lit | material::FirstPersonFlag) / 255.0f, 0.75f),
          "specular: mask 0.7, gloss log2(64)/13, class lit with the first-person flag");
    Check(Near(p[6], alb[0] * 0.2f, alb[1] * 0.4f, alb[2] * 0.6f, 0.75f), "emissive: albedo times colour times scale");
    Check(Near(p[7], kDepth, p[2].v[0], p[2].v[1], 1.0f), "layer: depth 0.25, the normal of output 2, covered");
    Check(!Near(p[4], 0.2f, 0.4f, 0.6f, 0.75f), "control: the texture without the tint is not accepted as albedo");
    for (int i = 4; i < 8; ++i) Show(i == 4 ? "albedo" : i == 5 ? "specular" : i == 6 ? "emissive" : "layer", p[i]);
}

// The two tints of the actors' shaders (Riverwood capture: skin ps 6a2383091a33f594, hair ps bcbe8960ef3af1ff).
void TintCases(Bench& b)
{
    float mat[36] = {};
    mat[4] = 0.8f; mat[5] = 0.3f; mat[6] = 0.1f;   // element 1: the tint
    b.SetConstants(1, mat, 9);
    const float base[3] = {0.2f, 0.4f, 0.6f}, tint[3] = {0.8f, 0.3f, 0.1f};
    for (int k = 0; k < 2; ++k) {
        const bool hair = k == 1;
        printf("%s tint\n", hair ? "hair" : "skin");
        const std::vector<uint8_t> code = Bench::Compile((std::string(kCommon) + (hair ? kHairTint : kSkinTint)).c_str(), "ps_5_0");
        PatchInfo info;
        const std::vector<uint8_t> patched = Patched(code, &info);
        if (g_dump) printf("%s\n%s\n", Bench::Disassemble(code).c_str(), Bench::Disassemble(patched).c_str());
        Check(info.kind == ShaderKind::Lighting && info.albedo && info.hairTint == hair, hair ? "the hair's tint is found" : "no hair tint on skin");
        Pixel o[kTargets], p[kTargets];
        if (patched.empty() || !b.Draw(code, o) || !b.Draw(patched, p)) { Check(false, "draws"); continue; }
        Check(memcmp(o, p, 3 * sizeof(Pixel)) == 0, "outputs 0 to 2 are bit-identical to the original's");
        float e[3];
        for (int c = 0; c < 3; ++c) e[c] = hair ? base[c] * tint[c] : base[c] * base[c] + 2 * base[c] * tint[c] * (1 - base[c]);
        Check(Near(p[4], e[0], e[1], e[2], 0.75f, 1e-4f), hair ? "albedo: texture times the tint" : "albedo: the tint blended into the texture");
        Check(!Near(p[4], base[0], base[1], base[2], 0.75f, 1e-3f), "control: the untinted texture is not accepted");
        if (!hair) Check(!Near(p[4], base[0] * tint[0], base[1] * tint[1], base[2] * tint[2], 0.75f, 1e-3f), "control: the bare product is not accepted");
        Show("albedo", p[4]);
    }
    SetScene(b);
}

void WaterCase(Bench& b)
{
    printf("water surface\n");
    const std::vector<uint8_t> code = Bench::Compile((std::string(kCommon) + kWater).c_str(), "ps_5_0");
    PatchInfo info;
    const std::vector<uint8_t> patched = Patched(code, &info);
    Check(info.kind == ShaderKind::Water && info.normal && info.depth, "reflection finds a water surface with a normal");
    Pixel o[kTargets], p[kTargets], q[kTargets];
    SetView(b, false);
    if (patched.empty() || !b.Draw(code, o) || !b.Draw(patched, p)) { Check(false, "draws"); return; }
    Check(memcmp(&o[0], &p[0], sizeof(Pixel)) == 0, "output 0 is bit-identical to the original's");
    // World normal (0.2, 0, 1) normalised; view (x, z, y) = (0.196, 0.981, 0); encoded n.xy / sqrt(8) + 0.5.
    // ShaderKind::Water writes hardware tag Layer.a = 2.0f in RGBA32F (commit 0f9dba3).
    const float len = std::sqrt(0.2f * 0.2f + 1.0f), nx = 0.2f / len, ny = 1.0f / len, s = std::sqrt(8.0f);
    Check(Near(p[7], kDepth, nx / s + 0.5f, ny / s + 0.5f, 2.0f, 1e-4f), "layer: depth, view-space normal encoded, water tag 2.0");
    Check(Near(p[5], 0, 0, float(material::Water) / 255.0f, 1.0f), "class water");
    SetView(b, true);
    b.Draw(patched, q);
    Check(!Near(q[7], kDepth, nx / s + 0.5f, ny / s + 0.5f, 2.0f, 1e-2f), "control: a view matrix with two rows swapped fails");
    Show("layer", p[7]);
    // The second form: wave normal 1 * (0.2, 0, 0) + (0, 0, 1), the same normal as above.
    const std::vector<uint8_t> code2 = Bench::Compile((std::string(kCommon) + kWaterLight).c_str(), "ps_5_0");
    PatchInfo info2;
    const std::vector<uint8_t> patched2 = Patched(code2, &info2);
    SetView(b, false);
    Check(info2.kind == ShaderKind::Water, "a water light pass with no fixed direction is found by its wave normal");
    if (patched2.empty() || !b.Draw(patched2, q)) { Check(false, "draws"); return; }
    Check(Near(q[7], kDepth, nx / s + 0.5f, ny / s + 0.5f, 2.0f, 1e-4f), "its layer carries the same normal and water tag 2.0, with buffer 12 declared by the patch");
}

void EffectCase(Bench& b)
{
    printf("effect draw\n");
    const std::vector<uint8_t> code = Bench::Compile((std::string(kCommon) + kEffect).c_str(), "ps_5_0");
    PatchInfo info;
    const std::vector<uint8_t> patched = Patched(code, &info);
    Check(info.kind == ShaderKind::Effect && !info.normal, "reflection finds an effect with no normal");
    Pixel o[kTargets], p[kTargets], q[kTargets];
    if (patched.empty() || !b.Draw(code, o) || !b.Draw(patched, p)) { Check(false, "draws"); return; }
    Check(memcmp(o, p, 3 * sizeof(Pixel)) == 0, "outputs 0 to 2 are bit-identical to the original's");
    Check(Near(p[7], kDepth, 0.3f, -1, 1), "layer: depth, no normal (alpha 0.3 in g, -1 in b), covered");
    Check(Near(p[4], 0, 0, 0, 0.3f) && Near(p[5], 0, 0, float(material::Effect) / 255.0f, 0.3f), "material targets: zero, class effect");
    // The layer blend: colour by the output's own alpha, so an uncovered draw leaves the target alone.
    D3D11_BLEND_DESC bd{};
    bd.IndependentBlendEnable = TRUE;
    for (auto& t : bd.RenderTarget) t.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    auto& layer = bd.RenderTarget[kLayerTarget];
    layer.BlendEnable = TRUE;
    layer.SrcBlend = D3D11_BLEND_SRC_ALPHA; layer.DestBlend = D3D11_BLEND_INV_SRC_ALPHA; layer.BlendOp = D3D11_BLEND_OP_ADD;
    layer.SrcBlendAlpha = D3D11_BLEND_ONE; layer.DestBlendAlpha = D3D11_BLEND_ONE; layer.BlendOpAlpha = D3D11_BLEND_OP_MAX;
    ComPtr<ID3D11BlendState> blend;
    b.dev->CreateBlendState(&bd, &blend);
    b.Draw(patched, q, blend.Get());
    Check(Near(q[7], kDepth, 0.3f, -1, 1), "with the layer blend a draw with alpha 0.3 replaces the layer");
    float mat[36] = {};                      // the same draw with alpha 0
    mat[8] = mat[9] = mat[10] = 1.0f;
    b.SetConstants(1, mat, 9);
    b.Draw(patched, q, blend.Get());
    Check(Near(q[7], -1, -1, -1, 0), "with the layer blend a draw with alpha 0 leaves the layer's colour at its clear value");
}
} // namespace

int main(int argc, char** argv)
{
    bool warp = false;
    const char* dir = nullptr;
    const char* outPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--warp")) warp = true;
        else if (!strcmp(argv[i], "--dump")) g_dump = true;
        else if (!strcmp(argv[i], "--dir") && i + 1 < argc) dir = argv[++i];
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outPath = argv[++i];
    }
    Bench b;
    if (!b.Create(warp)) { printf("no D3D11 device\n"); return 2; }
    printf("device: %s\n", b.warp ? "WARP" : "hardware");
    if (dir) return Survey(b, dir, outPath);
    SetScene(b);
    LitCase(b);
    TintCases(b);
    WaterCase(b);
    EffectCase(b);
    printf("%s (%d failed)\n", g_failed ? "FAILED" : "all checks passed", g_failed);
    return g_failed ? 1 : 0;
}
