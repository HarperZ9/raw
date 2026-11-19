// Indirect draws and compute dispatches.
#include "tx_context.h"

namespace raw::tx {

static constexpr uint32_t kShaderRead =
    D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;

// kind 0: draw, 1: indexed draw, 2: dispatch. The D3D11 argument layouts are the D3D12 ones.
void Context::Indirect(ID3D11Buffer* args, UINT offset, int kind)
{
    RunBreak(__func__);
    TBuffer* b = static_cast<TBuffer*>(args);
    if (!b) return;
    if (kind == 2 ? !PrepareDispatch() : !PrepareDraw(kind == 1)) return;
    ID3D12Resource* res = b->core.chunk.res;
    UINT64 off = offset;
    if (b->core.store == Store::Renamed) off += Pin(b)->offset, res = Pin(b)->res;
    else if (rec_.tracker.Require(b->core.tr, 0, D3D12_RESOURCE_STATE_INDIRECT_ARGUMENT)) dirty_ |= D_SCAN;
    rec_.tracker.Flush(Cl());
    ID3D12CommandSignature* sig = kind == 0 ? dev_->sigDraw.Get() : kind == 1 ? dev_->sigDrawIndexed.Get() : dev_->sigDispatch.Get();
    Cl()->ExecuteIndirect(sig, 1, res, off, nullptr, 0);
}
STDMETHODIMP_(void) Context::DrawInstancedIndirect(ID3D11Buffer* b, UINT off) { Indirect(b, off, 0); }
STDMETHODIMP_(void) Context::DrawIndexedInstancedIndirect(ID3D11Buffer* b, UINT off) { Indirect(b, off, 1); }
STDMETHODIMP_(void) Context::DispatchIndirect(ID3D11Buffer* b, UINT off) { Indirect(b, off, 2); }

bool Context::PrepareDispatch()
{
    StageState& s = s_.st[S_CS];
    ShaderData* sd = s.sh.sd;
    if (!sd) return false;
    StageBind sb[5]{};
    sb[0] = sd->bind;
    RootSig* root = dev_->GetRootSig(sb, true);
    ID3D12PipelineState* pso = root ? dev_->GetComputePso(sd, root) : nullptr;
    if (!pso) return false;
    ID3D12GraphicsCommandList* cl = Cl();
    if (croot_ != root) {
        cl->SetComputeRootSignature(root->rs.Get());
        croot_ = root;
        memset(cbVa_[S_CS], 0xFF, sizeof(cbVa_[S_CS]));
        sampKey_[S_CS] = ~0ull;
        dirty_ |= (D_SRV0 << S_CS) | (D_SAMP0 << S_CS) | D_CSUAV;
    }
    if (pso_ != pso) { cl->SetPipelineState(pso); pso_ = pso; }
    Tracker& t = rec_.tracker;
    for (uint32_t i = 0; i < sd->bind.uavCount && i < 8; ++i) {
        TUAV* v = s_.csUav[i];
        if (!v || !v->core->tr) continue;
        if (!t.RequireRange(v->core->tr, v->range, v->core->mips, v->core->slices, D3D12_RESOURCE_STATE_UNORDERED_ACCESS))
            t.UavBarrier(v->core->Res());
    }
    uint32_t n = sd->bind.srvCount < s.srvHi ? sd->bind.srvCount : s.srvHi;
    for (uint32_t i = 0; i < n; ++i)
        if (TSRV* v = s.srv[i]; v && v->core->tr) t.RequireRange(v->core->tr, v->range, v->core->mips, v->core->slices, kShaderRead);
    t.Flush(cl);
    ApplyTables(root, S_CS, S_CS, true);
    dirty_ &= ~((D_SRV0 << S_CS) | (D_SAMP0 << S_CS) | D_CSUAV);
    dirty_ |= D_SCAN;
    ++rec_.cmdCount;
    return true;
}

STDMETHODIMP_(void) Context::Dispatch(UINT x, UINT y, UINT z)
{
    RunBreak(__func__);
    TX_TRACE("Context::Dispatch");
    if (PrepareDispatch()) Cl()->Dispatch(x, y, z);
}

}  // namespace raw::tx
