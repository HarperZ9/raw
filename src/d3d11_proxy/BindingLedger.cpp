//=============================================================================
//  BindingLedger.cpp — see BindingLedger.h.
//
//  No locking: the D3D11 immediate context is single-threaded, and RAW's
//  RegisterResourceName / SetPassMarker run on the same render thread. The host
//  reader tolerates a partial trailing line, so per-line fflush is enough.
//  Never throws into the frame loop (C-style IO; the one STL insert is guarded).
//=============================================================================
#include "BindingLedger.h"
#include "RenderPhaseDetector.h"

#include <unordered_map>
#include <string>

namespace SB::Proxy
{
    // Pointer -> human name, populated by RAW via RegisterResourceName. File-static
    // so the header stays STL-free. Single-threaded access (see file header).
    static std::unordered_map<void*, std::string> g_names;

    BindingLedger& BindingLedger::Get()
    {
        static BindingLedger inst;
        return inst;
    }

    void BindingLedger::Initialize(const char* liveDir)
    {
        if (m_init) return;
        const char* dir = (liveDir && liveDir[0]) ? liveDir : "Data/SKSE/Plugins/RAW/live";
        // best-effort mkdir chain (ignore failures; CreateDirectory is idempotent)
        CreateDirectoryA("Data/SKSE/Plugins/RAW", nullptr);
        CreateDirectoryA(dir, nullptr);
        snprintf(m_dir, sizeof(m_dir), "%s", dir);
        char path[600];
        snprintf(path, sizeof(path), "%s/bindings.jsonl", m_dir);
        fopen_s(&m_fp, path, "w");   // truncate at session start; rolls via host tail
        m_init = true;
    }

    void BindingLedger::BeginFrame()
    {
        ++m_frame;
        m_hazWritesThisFrame = 0;
        for (int i = 0; i < kMaxPhases; ++i) m_phaseSnapped[i] = false;
        if (m_fp) fflush(m_fp);
    }

    void BindingLedger::SetPassMarker(const char* name)
    {
        if (!name) { m_marker[0] = 0; return; }
        snprintf(m_marker, sizeof(m_marker), "%s", name);
    }

    void BindingLedger::RegisterResourceName(void* res, const char* name)
    {
        if (!res || !name) return;
        try { g_names[res] = name; } catch (...) {}   // rare; never propagate
    }

    const char* BindingLedger::ResolveName(void* res, char* tmp, size_t cap)
    {
        if (!res) { snprintf(tmp, cap, "null"); return tmp; }
        auto it = g_names.find(res);
        if (it != g_names.end()) { snprintf(tmp, cap, "%s", it->second.c_str()); return tmp; }
        snprintf(tmp, cap, "0x%llx", (unsigned long long)res);
        return tmp;
    }

    // ── shadow updaters ──────────────────────────────────────────────────────
    void BindingLedger::OnSRV(int stage, UINT start, UINT num, ID3D11ShaderResourceView* const* ppSRV)
    {
        if (stage < 0 || stage >= LS_STAGES) return;
        for (UINT i = 0; i < num; ++i)
        {
            UINT slot = start + i;
            if (slot < (UINT)kSrvSlots)
                m_srv[stage][slot] = ppSRV ? ppSRV[i] : nullptr;
        }
    }

    void BindingLedger::OnRTV(UINT num, ID3D11RenderTargetView* const* ppRTV, ID3D11DepthStencilView* dsv)
    {
        for (int i = 0; i < kRtvSlots; ++i)
            m_rtv[i] = (ppRTV && (UINT)i < num) ? ppRTV[i] : nullptr;
        m_dsv = dsv;
    }

    void BindingLedger::OnOMUAV(UINT start, UINT num, ID3D11UnorderedAccessView* const* ppUAV)
    {
        for (UINT i = 0; i < num; ++i)
        {
            UINT slot = start + i;
            if (slot < (UINT)kUavSlots) m_uav[slot] = ppUAV ? ppUAV[i] : nullptr;
        }
    }

    void BindingLedger::OnCSUAV(UINT start, UINT num, ID3D11UnorderedAccessView* const* ppUAV)
    {
        OnOMUAV(start, num, ppUAV);   // same shadow array; hazard cares about the resource, not the slot kind
    }

    void BindingLedger::OnCB(int stage, UINT start, UINT num, ID3D11Buffer* const* ppCB)
    {
        if (stage < 0 || stage >= LS_STAGES) return;
        for (UINT i = 0; i < num; ++i)
        {
            UINT slot = start + i;
            if (slot < (UINT)kCbSlots)
                m_cb[stage][slot] = ppCB ? ppCB[i] : nullptr;
        }
    }

    bool BindingLedger::LookupCB(void* cb, char* outBuf, size_t cap) const
    {
        if (!cb) return false;
        static const char* kStg[LS_STAGES] = { "PS", "VS", "GS", "HS", "DS", "CS" };
        for (int s = 0; s < LS_STAGES; ++s)
            for (int slot = 0; slot < kCbSlots; ++slot)
                if (m_cb[s][slot] == cb) { snprintf(outBuf, cap, "%s b%d", kStg[s], slot); return true; }
        return false;
    }
}

namespace SB::Proxy
{
    // RenderPhase order mirrors ARCHITECTURE.md; indexed by (int)GetCurrentPhase().
    static const char* PhaseName(int p)
    {
        static const char* kNames[] = { "Unknown", "DepthPrepass", "ShadowMap",
            "GeometryMain", "Decals", "Sky", "AlphaBlend", "PostProcess", "UI" };
        return (p >= 0 && p < (int)(sizeof(kNames) / sizeof(kNames[0]))) ? kNames[p] : "Unknown";
    }

    bool BindingLedger::HazardScan(int stage, char* buf, size_t cap)
    {
        if (stage < 0 || stage >= LS_STAGES) { if (cap) snprintf(buf, cap, "[]"); return false; }
        int n = 0; bool any = false; char tmp[80];
        n += snprintf(buf + n, cap - n, "[");
        auto emit = [&](const char* kind, void* res, const char* as) {
            if ((size_t)n > cap - 160) return;
            n += snprintf(buf + n, cap - n, "%s{\"assert\":\"%s\",\"resource\":\"%s\",\"as\":\"%s\"}",
                          any ? "," : "", kind, ResolveName(res, tmp, sizeof(tmp)), as);
            any = true;
        };
        for (int s = 0; s < kSrvSlots; ++s)
        {
            void* srv = m_srv[stage][s];
            if (!srv) continue;
            char as[48];
            for (int i = 0; i < kRtvSlots; ++i)
                if (srv == (void*)m_rtv[i]) { snprintf(as, sizeof(as), "SRV(t%d)+RTV%d", s, i); emit("HAZARD", srv, as); }
            for (int i = 0; i < kUavSlots; ++i)
                if (srv == (void*)m_uav[i]) { snprintf(as, sizeof(as), "SRV(t%d)+UAV%d", s, i); emit("HAZARD", srv, as); }
            if (m_dsv && srv == (void*)m_dsv) { snprintf(as, sizeof(as), "SRV(t%d)+DSV", s); emit("DSV_BOUND_DURING_SAMPLE", srv, as); }
        }
        snprintf(buf + n, cap - n, "]");
        return any;
    }

    void BindingLedger::Snapshot(const char* op, int stage)
    {
        if (stage < 0 || stage >= LS_STAGES) return;          // bounds guard (audit D)
        int phase = (int)RenderPhaseDetector::Get().GetCurrentPhase();
        char asserts[1024];
        bool haz = HazardScan(stage, asserts, sizeof(asserts));
        bool markerSet = m_marker[0] != 0;
        bool first = (phase >= 0 && phase < kMaxPhases) ? !m_phaseSnapped[phase] : true;
        if (!(markerSet || first || haz)) return;
        if (haz && !markerSet && !first) { if (m_hazWritesThisFrame >= 256) return; ++m_hazWritesThisFrame; }

        char line[8192]; char tmp[80]; int n = 0;
        const int cap = (int)sizeof(line);
        // Bounded append: snprintf returns would-have-written, so n MUST be saturated
        // at cap-1 or the next (cap - n) underflows (size_t) and line+n runs OOB -> a
        // stack smash, reachable via the OnDraw stage fan-out + many bound resources (audit A).
        auto app = [&](const char* fmt, auto... args) {
            if (n >= cap - 1) return;
            int w = snprintf(line + n, (size_t)(cap - n), fmt, args...);
            if (w > 0) n += w;
            if (n > cap - 1) n = cap - 1;
        };
        static const char* kStageName[LS_STAGES] = { "PS", "VS", "GS", "HS", "DS", "CS" };
        const char* stageName = kStageName[stage];
        app("{\"frame\":%llu,\"phase\":\"%s\",\"pass\":\"%s\",\"op\":\"%s\",\"stage\":\"%s\",\"srv\":{",
            (unsigned long long)m_frame, PhaseName(phase), m_marker, op ? op : "", stageName);
        bool kv = false;
        for (int s = 0; s < kSrvSlots; ++s)
        {
            if (!m_srv[stage][s]) continue;
            app("%s\"t%d\":\"%s\"", kv ? "," : "", s, ResolveName(m_srv[stage][s], tmp, sizeof(tmp))); kv = true;
        }
        app("},\"rtv\":[");
        kv = false;
        for (int i = 0; i < kRtvSlots; ++i)
            if (m_rtv[i]) { app("%s\"%s\"", kv ? "," : "", ResolveName(m_rtv[i], tmp, sizeof(tmp))); kv = true; }
        app("],\"dsv\":%s%s%s,\"uav\":[",
            m_dsv ? "\"" : "", m_dsv ? ResolveName(m_dsv, tmp, sizeof(tmp)) : "null", m_dsv ? "\"" : "");
        kv = false;
        for (int i = 0; i < kUavSlots; ++i)
            if (m_uav[i]) { app("%s\"%s\"", kv ? "," : "", ResolveName(m_uav[i], tmp, sizeof(tmp))); kv = true; }
        app("],\"asserts\":%s}\n", asserts);

        if (m_fp) { fputs(line, m_fp); }
        if (phase >= 0 && phase < kMaxPhases) m_phaseSnapped[phase] = true;
    }

    void BindingLedger::OnDraw(const char* op)
    {
        if (!(m_enabled && m_fp)) return;
        // Fan out across graphics-pipeline stages so GS/HS/DS (and VS) SRV hazards
        // are scanned, not just PS. Snapshot dedups by phase and always emits on a
        // hazard; non-PS stages are scanned only when populated, to bound volume.
        static const int kDrawStages[] = { LS_PS, LS_VS, LS_GS, LS_HS, LS_DS };
        for (int i = 0; i < 5; ++i)
        {
            int st = kDrawStages[i];
            if (st == LS_PS) { Snapshot(op, st); continue; }
            for (int s = 0; s < kSrvSlots; ++s)
                if (m_srv[st][s]) { Snapshot(op, st); break; }
        }
    }
    void BindingLedger::OnDispatch(const char* op) { if (m_enabled && m_fp) Snapshot(op, LS_CS); }
}
