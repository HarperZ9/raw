// Replayer: file handling, the record loop and the object table.
#include "rec_replayer.h"
#include "rec_util.h"
#include <cstdio>

namespace raw::rec
{
bool Replayer::Open(const wchar_t* path)
{
    ReleaseObjects();
    return m_reader.Open(path);
}

bool Replayer::OpenMemory(std::vector<u8>&& bytes)
{
    ReleaseObjects();
    return m_reader.OpenMemory(std::move(bytes));
}

void Replayer::ReleaseObjects()
{
    for (const Mapped& m : m_maps)
        if (m_ctx) m_ctx->Unmap(m.res, m.sub);
    m_maps.clear();
    for (auto& kv : m_objects)
        if (kv.second.p) kv.second.p->Release();
    m_objects.clear();
    m_ended.clear();
    m_dev = nullptr;
    m_ctx = nullptr;
}

ID3D11Resource* Replayer::FindResource(u64 id) const
{
    const auto it = m_objects.find(id);
    if (it == m_objects.end() || it->second.kind < K_BUFFER || it->second.kind > K_TEX3D) return nullptr;
    return static_cast<ID3D11Resource*>(it->second.p);
}

ID3D11Texture2D* Replayer::FindTexture(u64 id) const
{
    const auto it = m_objects.find(id);
    if (it == m_objects.end() || it->second.kind != K_TEX2D) return nullptr;
    return static_cast<ID3D11Texture2D*>(it->second.p);
}

HRESULT Replayer::Fail(HRESULT hr, const char* what)
{
    m_why = what;
    return FAILED(hr) ? hr : E_FAIL;
}

HRESULT Replayer::Short() { return Fail(E_FAIL, "record payload is shorter than its layout"); }

HRESULT Replayer::Add(u64 id, u32 kind, ID3D11DeviceChild* p, HRESULT hr)
{
    if (FAILED(hr)) p = nullptr;
    Obj& slot = m_objects[id];
    if (slot.p) slot.p->Release();      // a second DEFINE of one id replaces the first
    slot = {p, kind};
    if (p) ++m_res.objects;
    if (FAILED(hr) && !m_why) m_why = "object creation failed";
    return hr;
}

// Null for id 0 and for objects that were recorded but not created (opaque
// kinds, failed creation in non-strict mode). An id that no DEFINE record
// introduced, or one of the wrong kind, marks the record as bad.
ID3D11DeviceChild* Replayer::Get(u64 id, u32 kindLo, u32 kindHi)
{
    if (id == 0) return nullptr;
    const auto it = m_objects.find(id);
    if (it == m_objects.end()) { m_badRef = true; return nullptr; }
    if (it->second.kind == K_OPAQUE || !it->second.p) return nullptr;
    if (it->second.kind < kindLo || it->second.kind > kindHi) { m_badRef = true; return nullptr; }
    return it->second.p;
}

bool Replayer::Ids(Cursor& c, u32 n, ID3D11DeviceChild** out, u32 kindLo, u32 kindHi)
{
    for (u32 i = 0; i < n; ++i) out[i] = Get(c.U64(), kindLo, kindHi);
    return c.ok;
}

ID3D11Resource* Replayer::Res(Cursor& c)
{
    return static_cast<ID3D11Resource*>(Get(c.U64(), K_BUFFER, K_TEX3D));
}

HRESULT Replayer::Exec(const Record& r)
{
    const u32 op = r.op;
    if (op == OP_STATE_BEGIN) { m_inState = true; return S_OK; }
    if (op == OP_STATE_END) { m_inState = false; return S_OK; }
    if (op >= OP_DEFINE_BUFFER && op <= OP_DEFINE_OPAQUE) {
        ++m_res.opCalls[op];
        return Define(r);
    }
    if (op < OP_DEFINE_BUFFER) {        // frame markers and the summary carry no call
        if (op == OP_UNSUPPORTED) ++m_res.unsupported;
        return S_OK;
    }
    if (m_inState && m_opt.skipInitialState) return S_OK;
    if (op >= OP_MAX) return S_OK;      // unknown opcode: skipped by its length
    ++m_res.opCalls[op];
    if (op < OP_DRAW) return State(r);
    if (op < OP_MAP) return Draw(r);
    return Data(r);
}

void Replayer::NoteFailure(size_t index, const Record& r, HRESULT hr)
{
    if (m_res.message.empty()) {
        char msg[320];
        snprintf(msg, sizeof(msg), "record %zu (%s, file offset %llu): %s, HRESULT 0x%08lX", index,
                 OpName(r.op), static_cast<unsigned long long>(r.offset),
                 m_why ? m_why : "call failed", static_cast<unsigned long>(hr));
        m_res.message = msg;
        m_res.hr = hr;
        m_res.failedRecord = index;
        m_res.failedOp = r.op;
    }
    ++m_res.softFailures;
}

bool Replayer::Run(ID3D11Device* device, ID3D11DeviceContext* context, const ReplayOptions& options)
{
    const std::vector<Record>& recs = m_reader.Records();
    ReleaseObjects();
    m_res = {};
    m_opt = options;
    m_inState = false;
    if (!device || !context || recs.empty()) {
        m_res.message = recs.empty() ? "no call stream is open" : "null device or context";
        m_res.hr = E_INVALIDARG;
        return false;
    }
    m_dev = device;
    m_ctx = context;
    if (m_opt.clearStateFirst) m_ctx->ClearState();
    bool stopped = false;
    for (size_t i = 0; i < recs.size() && !stopped; ++i) {
        m_badRef = false;
        m_why = nullptr;
        HRESULT hr = Exec(recs[i]);
        if (SUCCEEDED(hr) && m_badRef) hr = Fail(E_INVALIDARG, "reference to an undefined object");
        ++m_res.records;
        if (FAILED(hr)) {
            NoteFailure(i, recs[i], hr);
            stopped = m_opt.strict;
        }
    }
    for (const Mapped& m : m_maps) m_ctx->Unmap(m.res, m.sub);     // maps left open at frame end
    m_maps.clear();
    m_ctx->Flush();
    m_res.ok = m_res.softFailures == 0;
    return m_res.ok;
}
} // namespace raw::rec
