#pragma once
//=============================================================================
//  FrameLedger: an inventory of every buffer the game writes in one frame.
//
//  For one armed frame the ledger records each span between two render-target
//  binds (an "interval"): the targets, the depth-stencil, the viewport, the
//  phase, every draw with its shaders and output-merger state, plus clears,
//  copies and compute dispatches. After each interval it copies the targets
//  out, so the files hold what the game itself rendered:
//
//    <OutDir>/NNN/manifest.json     textures, intervals, copies, dispatches
//    <OutDir>/NNN/draws.csv         one row per draw
//    <OutDir>/NNN/tNN_final.rawtex  a texture after its last write this frame
//    <OutDir>/NNN/tNN_iMMM.rawtex   a full-size colour target after interval MMM
//                                   (History=1; the coverage report reads these)
//    <OutDir>/NNN/cb_*.bin          the per-frame constant buffers (slot 12)
//    <OutDir>/NNN/ps_<hash>.dxbc    each pixel shader a draw used (local analysis)
//
//  d3d11_proxy.ini:
//    [FrameLedger]
//    Enabled=0
//    OutDir=E:\raw-capture       absolute; nothing is written beside the game
//    AfterGameplayFrames=600     arm after this many frames in a row with > MinDraws draws
//    MinDraws=500
//    Captures=1                  how many frames to capture
//    Every=600                   gameplay frames between captures
//    History=1                   keep full-size colour targets after every interval (2: only the HDR colour)
//
//  Outside an armed frame every hook is one branch on g_frameLedgerArmed.
//  Original work, MIT.
//=============================================================================
#include <d3d11.h>
#include <cstdint>

namespace SB::Proxy
{

inline bool g_frameLedgerArmed = false;

struct FrameLedgerHooks
{
    static void OnTargets(ID3D11DeviceContext* ctx, UINT n, ID3D11RenderTargetView* const* rtvs,
                          ID3D11DepthStencilView* dsv);
    static void OnDraw(ID3D11DeviceContext* ctx, const char* kind, UINT count, UINT instances);
    static void OnShaders(uint64_t psHash, uint64_t vsHash, ID3D11PixelShader* ps);
    static void OnClearTarget(ID3D11RenderTargetView* rtv, const float rgba[4]);
    static void OnClearDepth(ID3D11DepthStencilView* dsv, UINT flags, float depth, UINT8 stencil);
    static void OnCopy(ID3D11DeviceContext* ctx, ID3D11Resource* dst, ID3D11Resource* src, bool whole);
    static void OnDispatch(ID3D11DeviceContext* ctx, UINT x, UINT y, UINT z);
    // Present: finishes an armed frame, and arms the next one when its turn has come.
    static void OnPresent(ID3D11DeviceContext* ctx, uint32_t drawsThisFrame);
};

// Set by the D3D12 bridge hook: shares one slice of a texture with the D3D12
// device, reads it there and compares it with the D3D11 bytes the ledger just
// mapped. Returns 1 equal, 2 different, 3 could not be shared, 0 bridge off.
using LedgerShareCheck = int (*)(ID3D11Texture2D* tex, UINT slice, const void* rows, UINT rowPitch, UINT rowBytes,
                                 UINT height);
inline LedgerShareCheck g_ledgerShareCheck = nullptr;

// Called from Present, before the phase detector resets its per-frame counts.
void FrameLedger_OnPresent();

} // namespace SB::Proxy
