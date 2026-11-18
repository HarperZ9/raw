// Scene 8 internals shared by many_draws.cpp (frame loop, timing) and
// many_draws_record.cpp (resources, per-draw recording, CPU reference).
#pragma once
#include "../scene_util.h"

namespace txtest::manydraws {

inline const char* const kName = "many_draws";
constexpr uint32_t kCols = 64;

// One draw's constants: centre.xy, half-size.xy, colour. Every value is a small
// dyadic rational, so sums and differences are exact in float.
struct DrawData { float ps[4]; float col[4]; };

struct CbPair { ComPtr<ID3D11Buffer> a, b; };

struct Shared {
    Target rt;
    ComPtr<ID3D11VertexShader> vs;
    ComPtr<ID3D11PixelShader> ps;
    ComPtr<ID3D11InputLayout> layout;
    ComPtr<ID3D11Buffer> vb;
    ComPtr<ID3D11RasterizerState> rs;
    ComPtr<ID3D11DepthStencilState> ds;
    float w = 0, h = 0;
    uint32_t rows = 1;
    std::vector<DrawData> draws;
};

uint32_t RowsFor(uint32_t drawCount);
bool MakeShared(SceneCtx& c, Shared& s);
// immediate: b is default usage (UpdateSubresource); deferred: both dynamic.
bool MakeCbPair(ID3D11Device* dev, bool deferred, CbPair& out);
// Sets the full pipeline state on x, then records draws [begin, end).
bool RecordRange(ID3D11DeviceContext* x, const Shared& s, const CbPair& cb, uint32_t begin,
                 uint32_t end, bool deferred);
// Compares against a CPU painter's-order reference where the grid is pixel exact.
bool CheckImage(const SceneCtx& c, const Shared& s, const Image& out);

}  // namespace txtest::manydraws
