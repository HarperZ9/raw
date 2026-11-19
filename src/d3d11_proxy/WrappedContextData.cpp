#include "ContextHooksShared.h"

namespace SB::Proxy
{

// ============================================================================
//  Copy / Update (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::CopySubresourceRegion(ID3D11Resource* pDst, UINT DstSub, UINT DstX, UINT DstY, UINT DstZ, ID3D11Resource* pSrc, UINT SrcSub, const D3D11_BOX* pSrcBox)
{
    m_real->CopySubresourceRegion(pDst, DstSub, DstX, DstY, DstZ, pSrc, SrcSub, pSrcBox);
    if (g_frameLedgerArmed) FrameLedgerHooks::OnCopy(m_real, pDst, pSrc, false);
}

void STDMETHODCALLTYPE WrappedContext::CopyResource(ID3D11Resource* pDst, ID3D11Resource* pSrc)
{
    m_real->CopyResource(pDst, pSrc);
    if (g_frameLedgerArmed) FrameLedgerHooks::OnCopy(m_real, pDst, pSrc, true);
}

// Coherence membrane (Tier 1.2): CB overflow witness — a constant buffer written past
// its ByteWidth via UpdateSubresource. char(34)/char(10) for quote/newline (patcher-safe).
static void EmitCbSizeMismatch(const void* ptr, unsigned wrote, unsigned cap)
{
    try {
        std::error_code ec;
        std::filesystem::create_directories("Data/SKSE/Plugins/RAW/live", ec);
        std::ofstream out("Data/SKSE/Plugins/RAW/live/cb_meta.jsonl", std::ios::app);
        if (!out.is_open()) return;
        const char Q = static_cast<char>(34);
        out << '{' << Q << "assert" << Q << ':' << Q << "CB_SIZE_MISMATCH" << Q
            << ',' << Q << "ptr" << Q << ':' << Q << "0x" << std::hex
            << reinterpret_cast<uintptr_t>(ptr) << std::dec << Q
            << ',' << Q << "wrote" << Q << ':' << wrote
            << ',' << Q << "capacity" << Q << ':' << cap
            << '}' << static_cast<char>(10);
    } catch (...) {}
}

void STDMETHODCALLTYPE WrappedContext::UpdateSubresource(ID3D11Resource* pDst, UINT DstSub, const D3D11_BOX* pDstBox, const void* pSrcData, UINT SrcRowPitch, UINT SrcDepthPitch)
{
    // Tier 1.2: witness a constant buffer written past its capacity
    if (pDst && pDstBox) {
        ID3D11Buffer* buf = nullptr;
        if (SUCCEEDED(pDst->QueryInterface(__uuidof(ID3D11Buffer), reinterpret_cast<void**>(&buf))) && buf) {
            D3D11_BUFFER_DESC bd; buf->GetDesc(&bd);
            if ((bd.BindFlags & D3D11_BIND_CONSTANT_BUFFER) && pDstBox->right > bd.ByteWidth)
                EmitCbSizeMismatch(pDst, pDstBox->right, bd.ByteWidth);
            buf->Release();
        }
    }
    m_real->UpdateSubresource(pDst, DstSub, pDstBox, pSrcData, SrcRowPitch, SrcDepthPitch);
}

void STDMETHODCALLTYPE WrappedContext::CopyStructureCount(ID3D11Buffer* pDstBuffer, UINT DstAlignedByteOffset, ID3D11UnorderedAccessView* pSrcView)
{
    m_real->CopyStructureCount(pDstBuffer, DstAlignedByteOffset, pSrcView);
}

// ============================================================================
//  Misc (forwarded)
// ============================================================================

void STDMETHODCALLTYPE WrappedContext::GenerateMips(ID3D11ShaderResourceView* pSRV)
{
    m_real->GenerateMips(pSRV);
}

void STDMETHODCALLTYPE WrappedContext::SetResourceMinLOD(ID3D11Resource* pResource, FLOAT MinLOD)
{
    m_real->SetResourceMinLOD(pResource, MinLOD);
}

FLOAT STDMETHODCALLTYPE WrappedContext::GetResourceMinLOD(ID3D11Resource* pResource)
{
    return m_real->GetResourceMinLOD(pResource);
}

void STDMETHODCALLTYPE WrappedContext::ResolveSubresource(ID3D11Resource* pDst, UINT DstSub, ID3D11Resource* pSrc, UINT SrcSub, DXGI_FORMAT Format)
{
    m_real->ResolveSubresource(pDst, DstSub, pSrc, SrcSub, Format);
    if (g_frameLedgerArmed) FrameLedgerHooks::OnCopy(m_real, pDst, pSrc, false);
}

void STDMETHODCALLTYPE WrappedContext::ExecuteCommandList(ID3D11CommandList* pCL, BOOL RestoreContextState)
{
    m_real->ExecuteCommandList(pCL, RestoreContextState);
}

void STDMETHODCALLTYPE WrappedContext::Begin(ID3D11Asynchronous* pAsync)
{
    m_real->Begin(pAsync);
}

void STDMETHODCALLTYPE WrappedContext::End(ID3D11Asynchronous* pAsync)
{
    m_real->End(pAsync);
}

HRESULT STDMETHODCALLTYPE WrappedContext::GetData(ID3D11Asynchronous* pAsync, void* pData, UINT DataSize, UINT GetDataFlags)
{
    return m_real->GetData(pAsync, pData, DataSize, GetDataFlags);
}

void STDMETHODCALLTYPE WrappedContext::SetPredication(ID3D11Predicate* pPredicate, BOOL PredicateValue)
{
    m_real->SetPredication(pPredicate, PredicateValue);
}

} // namespace SB::Proxy
