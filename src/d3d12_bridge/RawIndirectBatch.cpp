// RawIndirectBatch.cpp: GPU-Driven ExecuteIndirect DrawIndexed & DispatchMesh signatures + GPU arg/counter buffers.
// Original work, MIT.
#include "RawIndirectBatch.h"

namespace raw::bridge
{

bool RawIndirectBatch::Init(ID3D12Device* dev, bool meshShaderSupported)
{
    if (!dev) return false;
    // 1) Indirect DrawIndexedInstanced signature (20 bytes per command)
    D3D12_INDIRECT_ARGUMENT_DESC drawArg{};
    drawArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DRAW_INDEXED;
    D3D12_COMMAND_SIGNATURE_DESC drawDesc{};
    drawDesc.ByteStride = sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    drawDesc.NumArgumentDescs = 1;
    drawDesc.pArgumentDescs = &drawArg;
    if (FAILED(dev->CreateCommandSignature(&drawDesc, nullptr, IID_PPV_ARGS(&m_drawSig)))) return false;

    // 2) Indirect DispatchMesh signature (12 bytes per command, when Mesh Shader Tier 1 is supported)
    if (meshShaderSupported) {
        D3D12_INDIRECT_ARGUMENT_DESC meshArg{};
        meshArg.Type = D3D12_INDIRECT_ARGUMENT_TYPE_DISPATCH_MESH;
        D3D12_COMMAND_SIGNATURE_DESC meshDesc{};
        meshDesc.ByteStride = sizeof(D3D12_DISPATCH_MESH_ARGUMENTS);
        meshDesc.NumArgumentDescs = 1;
        meshDesc.pArgumentDescs = &meshArg;
        dev->CreateCommandSignature(&meshDesc, nullptr, IID_PPV_ARGS(&m_meshSig));
    }

    // 3) GPU DEFAULT buffers for indirect argument stream + 4-byte atomic draw counter
    D3D12_HEAP_PROPERTIES hp{};
    hp.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd{};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1;
    rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;

    rd.Width = uint64_t(kMaxCommands) * sizeof(D3D12_DRAW_INDEXED_ARGUMENTS);
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_argBuf))))
        return false;

    rd.Width = 256;
    if (FAILED(dev->CreateCommittedResource(&hp, D3D12_HEAP_FLAG_NONE, &rd, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&m_countBuf))))
        return false;

    m_ready = true;
    return true;
}

void RawIndirectBatch::ExecuteIndexed(ID3D12GraphicsCommandList* cmd, uint32_t maxCount) const
{
    if (!m_ready || !cmd || !m_drawSig) return;
    cmd->ExecuteIndirect(m_drawSig.Get(), maxCount, m_argBuf.Get(), 0, m_countBuf.Get(), 0);
}

void RawIndirectBatch::ExecuteMesh(ID3D12GraphicsCommandList* cmd, uint32_t maxCount) const
{
    if (!m_ready || !cmd || !m_meshSig) return;
    cmd->ExecuteIndirect(m_meshSig.Get(), maxCount, m_argBuf.Get(), 0, m_countBuf.Get(), 0);
}

} // namespace raw::bridge
