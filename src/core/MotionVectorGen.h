#pragma once
//=============================================================================
//  MotionVectorGen — Compute shader depth-reprojection motion vectors
//
//  Generates per-pixel screen-space motion vectors from the depth buffer
//  and camera matrices (current InvViewProj + previous ViewProj).
//
//  Output: R16G16_FLOAT texture where .xy = currentUV - previousUV
//  Convention matches TemporalSuperRes: positive motion = pixel moved right/down.
//
//  Dispatch once per frame before TemporalSuperRes::Execute().
//=============================================================================

#include <d3d11.h>
#include <cstdint>

namespace SB
{

class MotionVectorGen
{
public:
    static MotionVectorGen& Get()
    {
        static MotionVectorGen inst;
        return inst;
    }

    bool Initialize(ID3D11Device* dev, ID3D11DeviceContext* ctx,
                    uint32_t width, uint32_t height);
    void Shutdown();

    /// Generate motion vectors from depth + camera matrices.
    /// @param ctx           Device context for dispatch
    /// @param depthSRV      Current frame depth buffer (R32_FLOAT or R24_UNORM)
    /// @param invViewProj   Current frame inverse view-projection (row-major float[16])
    /// @param prevViewProj  Previous frame view-projection (row-major float[16])
    void Dispatch(ID3D11DeviceContext* ctx,
                  ID3D11ShaderResourceView* depthSRV,
                  const float* invViewProj,
                  const float* prevViewProj);

    /// Resize output texture (e.g. on backbuffer resize).
    void Resize(ID3D11Device* dev, uint32_t width, uint32_t height);

    // ── Accessors ────────────────────────────────────────────────────
    bool IsInitialized() const { return m_initialized; }
    ID3D11ShaderResourceView* GetMotionSRV() const { return m_motionSRV; }
    uint32_t GetWidth()  const { return m_width; }
    uint32_t GetHeight() const { return m_height; }

private:
    MotionVectorGen() = default;

    bool CompileCS(ID3D11Device* dev);
    bool CreateResources(ID3D11Device* dev, uint32_t w, uint32_t h);
    void ReleaseResources();

    bool m_initialized = false;

    ID3D11Device*        m_device  = nullptr;
    ID3D11DeviceContext* m_context = nullptr;

    // Compute shader
    ID3D11ComputeShader* m_cs = nullptr;

    // Output: R16G16_FLOAT motion vectors
    ID3D11Texture2D*            m_motionTex = nullptr;
    ID3D11ShaderResourceView*   m_motionSRV = nullptr;
    ID3D11UnorderedAccessView*  m_motionUAV = nullptr;

    // Constant buffer (matrices + dimensions)
    ID3D11Buffer* m_cb = nullptr;

    uint32_t m_width  = 0;
    uint32_t m_height = 0;
};

} // namespace SB
