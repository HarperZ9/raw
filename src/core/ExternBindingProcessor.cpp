#include "ExternBindingProcessor.h"
#include "ShaderPreProcessor.h"
#include "ENBInterface.h"

#include <SKSE/SKSE.h>
#include <cstring>

namespace SB
{

ExternBindingProcessor& ExternBindingProcessor::Get()
{
    static ExternBindingProcessor inst;
    return inst;
}


Float4 ExternBindingProcessor::MatrixColumn(const Float4x4& mat, int col)
{
    // Extract column `col` from row-major storage:
    // column j = (row[0][j], row[1][j], row[2][j], row[3][j])
    const float* r0 = &mat.row[0].x;
    const float* r1 = &mat.row[1].x;
    const float* r2 = &mat.row[2].x;
    const float* r3 = &mat.row[3].x;
    return { r0[col], r1[col], r2[col], r3[col] };
}


bool ExternBindingProcessor::ResolveBinding(const std::string& bindingName,
                                            const AllData& data,
                                            Float4& outValue)
{
    // ── ViewProjection matrix columns (computed on the fly) ──────────
    // VP = View * Proj, both reconstructed from minimal camera data.
    // We build View 4x4 from 3x3 rotation + worldPos, Proj from FOV/near/far/aspect.
    if (bindingName == "WVPMatColumn0" || bindingName == "WVPMatColumn1" ||
        bindingName == "WVPMatColumn2" || bindingName == "WVPMatColumn3" ||
        bindingName == "InvWVPMatColumn0" || bindingName == "InvWVPMatColumn1" ||
        bindingName == "InvWVPMatColumn2" || bindingName == "InvWVPMatColumn3")
    {
        // Reconstruct View 4x4 from rotation rows + worldPos
        Float4x4 view{};
        view.row[0] = { data.camera.ViewRow0.x, data.camera.ViewRow0.y, data.camera.ViewRow0.z,
            -(data.camera.ViewRow0.x * data.camera.WorldPos.x +
              data.camera.ViewRow0.y * data.camera.WorldPos.y +
              data.camera.ViewRow0.z * data.camera.WorldPos.z) };
        view.row[1] = { data.camera.ViewRow1.x, data.camera.ViewRow1.y, data.camera.ViewRow1.z,
            -(data.camera.ViewRow1.x * data.camera.WorldPos.x +
              data.camera.ViewRow1.y * data.camera.WorldPos.y +
              data.camera.ViewRow1.z * data.camera.WorldPos.z) };
        view.row[2] = { data.camera.ViewRow2.x, data.camera.ViewRow2.y, data.camera.ViewRow2.z,
            -(data.camera.ViewRow2.x * data.camera.WorldPos.x +
              data.camera.ViewRow2.y * data.camera.WorldPos.y +
              data.camera.ViewRow2.z * data.camera.WorldPos.z) };
        view.row[3] = { 0.f, 0.f, 0.f, 1.f };

        // Reconstruct Proj from FOV(rad)/near/far/aspect
        float fov = data.camera.Params.x;
        float n = data.camera.Params.y;
        float f = data.camera.Params.z;
        float aspect = data.camera.Params.w;
        float tanHalf = std::tan(fov * 0.5f);
        Float4x4 proj{};
        if (tanHalf > 1e-6f && std::abs(f - n) > 1e-6f) {
            proj.row[0] = { 1.f / (aspect * tanHalf), 0.f, 0.f, 0.f };
            proj.row[1] = { 0.f, 1.f / tanHalf, 0.f, 0.f };
            proj.row[2] = { 0.f, 0.f, f / (f - n), 1.f };
            proj.row[3] = { 0.f, 0.f, -n * f / (f - n), 0.f };
        }

        // VP = View * Proj (row-major)
        Float4x4 vp{};
        for (int r = 0; r < 4; ++r)
            for (int c = 0; c < 4; ++c) {
                float sum = 0.f;
                const float* vr = &view.row[r].x;
                for (int k = 0; k < 4; ++k)
                    sum += vr[k] * (&proj.row[k].x)[c];
                (&vp.row[r].x)[c] = sum;
            }

        if (bindingName[0] == 'I') {
            // Need inverse VP — use Gauss-Jordan
            Float4x4 inv{};
            // (reuse the existing MatrixColumn on inverted vp)
            // Simple 4x4 inverse inline
            float m[16], iv[16];
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    m[r*4+c] = (&vp.row[r].x)[c];
            std::memset(iv, 0, sizeof(iv));
            for (int i = 0; i < 4; ++i) iv[i*5] = 1.f;
            bool ok = true;
            for (int col = 0; col < 4; ++col) {
                int best = col;
                float bestVal = std::abs(m[col*4+col]);
                for (int row = col+1; row < 4; ++row) {
                    float v = std::abs(m[row*4+col]);
                    if (v > bestVal) { best = row; bestVal = v; }
                }
                if (bestVal < 1e-12f) { ok = false; break; }
                if (best != col)
                    for (int j = 0; j < 4; ++j) {
                        std::swap(m[col*4+j], m[best*4+j]);
                        std::swap(iv[col*4+j], iv[best*4+j]);
                    }
                float pivot = m[col*4+col];
                for (int j = 0; j < 4; ++j) { m[col*4+j] /= pivot; iv[col*4+j] /= pivot; }
                for (int row = 0; row < 4; ++row) {
                    if (row == col) continue;
                    float fac = m[row*4+col];
                    for (int j = 0; j < 4; ++j) { m[row*4+j] -= fac * m[col*4+j]; iv[row*4+j] -= fac * iv[col*4+j]; }
                }
            }
            if (ok)
                for (int r = 0; r < 4; ++r)
                    for (int c = 0; c < 4; ++c)
                        (&inv.row[r].x)[c] = iv[r*4+c];

            int col = bindingName.back() - '0';
            outValue = MatrixColumn(inv, col);
        } else {
            int col = bindingName.back() - '0';
            outValue = MatrixColumn(vp, col);
        }
        return true;
    }

    // ── Inverse camera rotation columns (3x3) ────────────────────────
    // InvCamRot = transpose(View 3x3) — row j of R becomes column j
    if (bindingName == "InvCamRotMatColumn0") {
        outValue = { data.camera.ViewRow0.x, data.camera.ViewRow0.y, data.camera.ViewRow0.z, 0.f };
        return true;
    }
    if (bindingName == "InvCamRotMatColumn1") {
        outValue = { data.camera.ViewRow1.x, data.camera.ViewRow1.y, data.camera.ViewRow1.z, 0.f };
        return true;
    }
    if (bindingName == "InvCamRotMatColumn2") {
        outValue = { data.camera.ViewRow2.x, data.camera.ViewRow2.y, data.camera.ViewRow2.z, 0.f };
        return true;
    }

    // ── Scalar game state bindings ────────────────────────────────────
    if (bindingName == "GameTime") {
        outValue = { data.celestial.TimeData.x, 0.f, 0.f, 0.f };
        return true;
    }
    if (bindingName == "WindSpeed") {
        outValue = { data.weather.Wind.x, 0.f, 0.f, 0.f };
        return true;
    }
    if (bindingName == "IsInterior") {
        outValue = { data.interior.IsInterior.x, 0.f, 0.f, 0.f };
        return true;
    }
    if (bindingName == "FOV") {
        // ExternBinding FOV is traditionally in degrees for Extender compat
        outValue = { data.camera.Params.x * 57.2957795f, 0.f, 0.f, 0.f };
        return true;
    }
    if (bindingName == "NearClip") {
        outValue = { data.camera.Params.y, 0.f, 0.f, 0.f };
        return true;
    }
    if (bindingName == "FarClip") {
        outValue = { data.camera.Params.z, 0.f, 0.f, 0.f };
        return true;
    }
    if (bindingName == "CameraPosition") {
        outValue = { data.camera.WorldPos.x, data.camera.WorldPos.y, data.camera.WorldPos.z, 0.f };
        return true;
    }
    if (bindingName == "SunDirection") {
        outValue = data.celestial.SunDirection;
        return true;
    }
    if (bindingName == "SunColor") {
        outValue = data.celestial.SunColor;
        return true;
    }

    return false;
}


void ExternBindingProcessor::Update(const AllData& data)
{
    if (!ENBInterface::SetParameter) return;

    // Cache the extern params list — only refresh every 120 frames
    static std::vector<const ParameterMeta*> s_cachedParams;
    static int s_refreshTimer = 0;
    if (s_cachedParams.empty() || ++s_refreshTimer >= 120) {
        s_refreshTimer = 0;
        auto& db = AnnotationDatabase::Get();
        s_cachedParams = db.GetExternBoundParameters();
    }

    m_bindingCount = static_cast<int>(s_cachedParams.size());
    m_pushCount = 0;

    if (s_cachedParams.empty()) return;

    ENBInterface::ENBParameter param;
    param.Size = 16;
    param.Type = ENBInterface::ENBParameterType::ENBParam_COLOR4;

    for (const auto* meta : s_cachedParams) {
        Float4 value;
        if (!ResolveBinding(meta->externBinding, data, value))
            continue;

        std::memcpy(param.Data, &value, 16);

        // Push to the specific shader that declared this binding
        // Use UIName as keyname (ENB matches against UIName, not HLSL var name)
        const char* keyName = meta->uiName.empty()
            ? meta->varName.c_str()
            : meta->uiName.c_str();

        // Normalize shader filename to UPPERCASE for ENB lookup
        // ENB's internal shader name table uses uppercase (case-sensitive match)
        std::string shaderUpper = meta->shaderFile;
        for (auto& c : shaderUpper) c = static_cast<char>(toupper(static_cast<unsigned char>(c)));

        ENBInterface::SetParameter(nullptr, shaderUpper.c_str(), keyName, &param);
        ++m_pushCount;
    }

    static bool s_logged = false;
    if (!s_logged && m_bindingCount > 0) {
        SKSE::log::info("ExternBindingProcessor: {} bindings found, {} resolved",
                        m_bindingCount, m_pushCount);
        s_logged = true;
    }
}

} // namespace SB
