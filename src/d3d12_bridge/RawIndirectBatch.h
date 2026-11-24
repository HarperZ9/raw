#pragma once
// RawIndirectBatch.h: GPU-Driven ExecuteIndirect Multi-Draw & Indirect DispatchMesh Command Signatures.
// Collapses thousands of 2011 CPU draw calls into single GPU-culled ExecuteIndirect batches.
// Original work, MIT.
#include <d3d12.h>
#include <wrl/client.h>

#include <cstdint>

namespace raw::bridge
{

class RawIndirectBatch {
public:
    static constexpr uint32_t kMaxCommands = 16384u;

    bool Init(ID3D12Device* dev, bool meshShaderSupported);
    bool Ready() const { return m_ready; }
    bool SupportsIndirectMesh() const { return m_meshSig != nullptr; }

    ID3D12CommandSignature* DrawIndexedSignature() const { return m_drawSig.Get(); }
    ID3D12CommandSignature* DispatchMeshSignature() const { return m_meshSig.Get(); }
    ID3D12Resource* ArgBuffer() const { return m_argBuf.Get(); }
    ID3D12Resource* CountBuffer() const { return m_countBuf.Get(); }

    void ExecuteIndexed(ID3D12GraphicsCommandList* cmd, uint32_t maxCount = kMaxCommands) const;
    void ExecuteMesh(ID3D12GraphicsCommandList* cmd, uint32_t maxCount = kMaxCommands) const;

private:
    bool                                          m_ready = false;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_drawSig;
    Microsoft::WRL::ComPtr<ID3D12CommandSignature> m_meshSig;
    Microsoft::WRL::ComPtr<ID3D12Resource>        m_argBuf;
    Microsoft::WRL::ComPtr<ID3D12Resource>        m_countBuf;
};

} // namespace raw::bridge
