// Matrices, the cube mesh and the position+colour pipeline for the D3D11 test scenes.
#include "scene_util.h"
#include <cmath>

namespace txtest {

Mat4 Identity()
{
    Mat4 r{};
    for (int i = 0; i < 4; ++i) r.m[i][i] = 1.0f;
    return r;
}

Mat4 Mul(const Mat4& a, const Mat4& b)
{
    Mat4 r{};
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) r.m[i][j] += a.m[i][k] * b.m[k][j];
    return r;
}

Mat4 Translate(float x, float y, float z)
{
    Mat4 r = Identity();
    r.m[3][0] = x; r.m[3][1] = y; r.m[3][2] = z;
    return r;
}

Mat4 Scale(float x, float y, float z)
{
    Mat4 r = Identity();
    r.m[0][0] = x; r.m[1][1] = y; r.m[2][2] = z;
    return r;
}

Mat4 RotX(float rad)
{
    Mat4 r = Identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[1][1] = c; r.m[1][2] = s; r.m[2][1] = -s; r.m[2][2] = c;
    return r;
}

Mat4 RotY(float rad)
{
    Mat4 r = Identity();
    const float c = std::cos(rad), s = std::sin(rad);
    r.m[0][0] = c; r.m[0][2] = -s; r.m[2][0] = s; r.m[2][2] = c;
    return r;
}

static Vec3 Sub(Vec3 a, Vec3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
static float Dot(Vec3 a, Vec3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
static Vec3 Cross(Vec3 a, Vec3 b)
{
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static Vec3 Norm(Vec3 a)
{
    const float l = std::sqrt(Dot(a, a));
    return {a.x / l, a.y / l, a.z / l};
}

Mat4 LookAtLH(Vec3 eye, Vec3 at, Vec3 up)
{
    const Vec3 z = Norm(Sub(at, eye)), x = Norm(Cross(up, z)), y = Cross(z, x);
    Mat4 r = Identity();
    r.m[0][0] = x.x; r.m[0][1] = y.x; r.m[0][2] = z.x;
    r.m[1][0] = x.y; r.m[1][1] = y.y; r.m[1][2] = z.y;
    r.m[2][0] = x.z; r.m[2][1] = y.z; r.m[2][2] = z.z;
    r.m[3][0] = -Dot(x, eye); r.m[3][1] = -Dot(y, eye); r.m[3][2] = -Dot(z, eye);
    return r;
}

Mat4 PerspLH(float fovY, float aspect, float zn, float zf)
{
    const float ys = 1.0f / std::tan(fovY * 0.5f);
    Mat4 r{};
    r.m[0][0] = ys / aspect;
    r.m[1][1] = ys;
    r.m[2][2] = zf / (zf - zn);
    r.m[2][3] = 1.0f;
    r.m[3][2] = -zn * zf / (zf - zn);
    return r;
}

Mat4 OrthoLH(float w, float h, float zn, float zf)
{
    Mat4 r = Identity();
    r.m[0][0] = 2.0f / w;
    r.m[1][1] = 2.0f / h;
    r.m[2][2] = 1.0f / (zf - zn);
    r.m[3][2] = -zn / (zf - zn);
    return r;
}

const D3D11_INPUT_ELEMENT_DESC kPNTLayout[3] = {
    {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 24, D3D11_INPUT_PER_VERTEX_DATA, 0},
};

void MakeCube(std::vector<VtxPNT>& verts, std::vector<uint16_t>& idx)
{
    // Each face: normal n and tangent u; v = n x u, so the corner order below is
    // clockwise seen from outside in a left-handed view.
    const Vec3 faces[6][2] = {
        {{0, 0, -1}, {0, 1, 0}}, {{0, 0, 1}, {0, 1, 0}}, {{-1, 0, 0}, {0, 1, 0}},
        {{1, 0, 0}, {0, 1, 0}},  {{0, 1, 0}, {0, 0, 1}}, {{0, -1, 0}, {0, 0, 1}},
    };
    const float su[4] = {-1, 1, 1, -1}, sv[4] = {-1, -1, 1, 1};
    verts.clear();
    idx.clear();
    for (const auto& f : faces) {
        const Vec3 n = f[0], u = f[1], v = Cross(n, u);
        const uint16_t base = static_cast<uint16_t>(verts.size());
        for (int k = 0; k < 4; ++k) {
            VtxPNT p{};
            p.p[0] = n.x + su[k] * u.x + sv[k] * v.x;
            p.p[1] = n.y + su[k] * u.y + sv[k] * v.y;
            p.p[2] = n.z + su[k] * u.z + sv[k] * v.z;
            p.n[0] = n.x; p.n[1] = n.y; p.n[2] = n.z;
            p.uv[0] = su[k] * 0.5f + 0.5f;
            p.uv[1] = sv[k] * 0.5f + 0.5f;
            verts.push_back(p);
        }
        for (int i : {0, 1, 2, 0, 2, 3}) idx.push_back(static_cast<uint16_t>(base + i));
    }
}

static const char* kColorSrc = R"(
struct VI { float3 pos : POSITION; float4 col : COLOR0; };
struct VO { float4 pos : SV_Position; float4 col : COLOR0; };
VO vs(VI i) { VO o; o.pos = float4(i.pos, 1); o.col = i.col; return o; }
float4 ps(VO i) : SV_Target { return i.col; }
)";

bool MakeColorPipe(ID3D11Device* dev, ColorPipe& out)
{
    const D3D11_INPUT_ELEMENT_DESC el[2] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    ComPtr<ID3DBlob> blob;
    out.vs = MakeVS(dev, kColorSrc, "vs", &blob);
    out.ps = MakePS(dev, kColorSrc, "ps");
    out.layout = MakeLayout(dev, el, 2, blob.Get());
    return out.vs && out.ps && out.layout;
}

void BindColorPipe(ID3D11DeviceContext* ctx, const ColorPipe& p)
{
    ctx->IASetInputLayout(p.layout.Get());
    ctx->VSSetShader(p.vs.Get(), nullptr, 0);
    ctx->PSSetShader(p.ps.Get(), nullptr, 0);
}

VtxPC VertPx(float w, float h, float x, float y, float z, Rgba c)
{
    return {x / w * 2.0f - 1.0f, 1.0f - y / h * 2.0f, z, c.r, c.g, c.b, c.a};
}

void PushQuadPx(std::vector<VtxPC>& v, float w, float h, float x0, float y0, float x1, float y1,
                float z, Rgba c)
{
    const VtxPC tl = VertPx(w, h, x0, y0, z, c), tr = VertPx(w, h, x1, y0, z, c);
    const VtxPC br = VertPx(w, h, x1, y1, z, c), bl = VertPx(w, h, x0, y1, z, c);
    for (const VtxPC& p : {tl, tr, br, tl, br, bl}) v.push_back(p);
}

}  // namespace txtest
