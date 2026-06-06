#pragma once
//=============================================================================
//  BindingLedger — externalized GPU symbol table for the "eyes".
//
//  Proxy-side half of DOCTRINE substrate #1 (see tools/eyes/BINDING_LEDGER.md).
//  Shadows the currently-bound SRV/RTV/DSV/UAV state (updated from WrappedContext's
//  existing Set* overrides), snapshots it at draw/dispatch tagged with phase + pass
//  marker, and appends live/bindings.jsonl with cheap HAZARD / DSV asserts. The host
//  (tools/eyes/bindings.py) reads it and recomputes SLOT_MISMATCH.
//
//  Contract: immediate context is single-threaded; C-style file IO; never throws
//  into the frame loop. Opt-in — off by default, zero IO until SetEnabled(true).
//
//  Copyright (c) 2026 Zain D. Harper. All rights reserved.
//=============================================================================
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <Windows.h>
#include <d3d11.h>
#include <cstdint>
#include <cstdio>

namespace SB::Proxy
{
    // Shader stages we shadow SRVs for. Index into m_srv[stage][slot].
    enum LedgerStage { LS_PS = 0, LS_VS = 1, LS_GS = 2, LS_HS = 3, LS_DS = 4, LS_CS = 5, LS_STAGES = 6 };

    class BindingLedger
    {
    public:
        static BindingLedger& Get();

        // liveDir defaults to "Data/SKSE/Plugins/RAW/live" if null. Creates the file lazily.
        void Initialize(const char* liveDir);
        void SetEnabled(bool e) { m_enabled = e; }
        bool IsEnabled() const { return m_enabled; }

        // ── shadow updaters — call from the matching WrappedContext Set* override,
        //    right after the PG_IsSafeMode() guard, using the SAME args. ───────────
        void OnSRV(int stage, UINT start, UINT num, ID3D11ShaderResourceView* const* ppSRV);
        void OnRTV(UINT num, ID3D11RenderTargetView* const* ppRTV, ID3D11DepthStencilView* dsv);
        void OnOMUAV(UINT start, UINT num, ID3D11UnorderedAccessView* const* ppUAV);
        void OnCSUAV(UINT start, UINT num, ID3D11UnorderedAccessView* const* ppUAV);
        void OnCB(int stage, UINT start, UINT num, ID3D11Buffer* const* ppCB);
        // CB pointer -> first slot/stage where bound, fills outBuf as "PS b3"; true if found (1.2-p3)
        bool LookupCB(void* cb, char* outBuf, size_t cap) const;

        // ── draw / dispatch sites — snapshot + hazard scan. Call before m_real->Draw*. ─
        void OnDraw(const char* op);
        void OnDispatch(const char* op);

        // ── frame + enrichment ───────────────────────────────────────────────────
        void BeginFrame();                                 // from WrappedContext::ResetFrameStats
        void SetPassMarker(const char* name);              // RAW brackets each pass; "" clears
        void RegisterResourceName(void* res, const char* name);  // RAW maps its resources to names

        static constexpr int   kSrvSlots = 64;   // covers t0..t63 (RAW uses up to t38)
        static constexpr int   kCbSlots = 14;    // D3D11 constant-buffer API slots (b0..b13)
        static constexpr int   kRtvSlots = 8;
        static constexpr int   kUavSlots = 8;
        static constexpr int   kMaxPhases = 16;

    private:
        void Snapshot(const char* op, int stage);
        bool HazardScan(int stage, char* assertBuf, size_t cap);   // true if any hazard; fills JSON array fragment
        const char* ResolveName(void* res, char* tmp, size_t cap);

        bool   m_enabled = false;
        bool   m_init = false;
        uint64_t m_frame = 0;
        int    m_hazWritesThisFrame = 0;
        bool   m_phaseSnapped[kMaxPhases] = {};
        char   m_marker[64] = {};
        char   m_dir[512] = {};
        FILE*  m_fp = nullptr;

        ID3D11ShaderResourceView*  m_srv[LS_STAGES][kSrvSlots] = {};
        ID3D11Buffer*              m_cb[LS_STAGES][kCbSlots]  = {};   // 1.2-p3 CB slot shadow
        ID3D11RenderTargetView*    m_rtv[kRtvSlots] = {};
        ID3D11DepthStencilView*    m_dsv = nullptr;
        ID3D11UnorderedAccessView* m_uav[kUavSlots] = {};
    };
}
