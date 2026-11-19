// The D3D11 binding state a context shadows, and its dirty flags (tx_context.h). Original work, MIT.
#pragma once
#include "tx_device.h"
#include "tx_holds.h"

namespace raw::tx {

enum StageIndex { S_VS, S_HS, S_DS, S_GS, S_PS, S_CS, S_COUNT };

enum Dirty : uint32_t {
    D_PSO = 1, D_RT = 2, D_VP = 4, D_SC = 8, D_BF = 0x10, D_SREF = 0x20, D_TOPO = 0x40,
    D_VB = 0x80, D_IB = 0x100, D_UAV = 0x400, D_CSUAV = 0x800,
    D_SRV0 = 0x1000,     // << stage
    D_SAMP0 = 0x40000,   // << stage
    // State scans at the next draw (ScanGraphics): which bindings may need a transition. A vertex or index
    // buffer, or one stage's shader resources, rescans only that part; anything that can move a resource out
    // of a state the list already holds (copies, clears, targets, compute, a new list) sets D_SCAN, all parts.
    D_SCAN_IA = 0x200,
    D_SCAN_OM = 0x1000000,
    D_SCAN_SRV0 = 0x2000000,   // << graphics stage (VS..PS)
    // Every shader resource of every graphics stage, not only the slots that changed (StageState::srvScan).
    D_SCAN_ALLSRV = 0x40000000,
    D_SCAN = D_SCAN_IA | D_SCAN_OM | (D_SCAN_SRV0 * 0x1F) | D_SCAN_ALLSRV,
    D_ALL = 0xFFFFFFFFu,
};

struct ShaderSlot {
    ShaderData* sd = nullptr;
    void* obj = nullptr;
    void (*drop)(BindHolds&, void*) = nullptr;   // gives the binding's reference back
    ImmHold* hold = nullptr;                     // the shader's plain count (a segment holds it too)
};
struct BufSlot {
    TBuffer* buf = nullptr;
    const Chunk* chunk = nullptr;   // where the buffer's current memory is described
    UINT stride = 0, offset = 0;
};
struct StageState {
    ShaderSlot sh;
    BufSlot cb[14];
    TSRV* srv[128] = {};
    TSampler* samp[16] = {};
    uint32_t srvHi = 0;
    uint64_t srvScan = 0;   // slots 0..63 bound anew since the last scan; a later slot sets D_SCAN_ALLSRV
};
struct State {
    StageState st[S_COUNT];
    TInputLayout* layout = nullptr;
    BufSlot vb[32];
    uint32_t vbHi = 0;
    BufSlot ib;
    DXGI_FORMAT ibFmt = DXGI_FORMAT_UNKNOWN;
    D3D11_PRIMITIVE_TOPOLOGY topo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
    TRTV* rtv[8] = {};
    uint32_t rtCount = 0;
    TDSV* dsv = nullptr;
    TUAV* uav[8] = {};
    TUAV* csUav[8] = {};
    TBlend* blend = nullptr;
    float factor[4] = {1, 1, 1, 1};
    UINT sampleMask = 0xFFFFFFFFu;
    TDepthStencil* ds = nullptr;
    UINT stencilRef = 0;
    TRaster* rs = nullptr;
    D3D11_VIEWPORT vp[16] = {};
    UINT vpCount = 0;
    D3D11_RECT sc[16] = {};
    UINT scCount = 0;
};

}  // namespace raw::tx
