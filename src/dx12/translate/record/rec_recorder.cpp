#include "rec_recorder.h"
#include "rec_emit.h"
#include "rec_util.h"
#include <dxgi.h>

namespace raw::rec
{
Recorder::Recorder(ID3D11Device* real, ID3D11DeviceContext* realCtx) : m_dev(real), m_ctx(realCtx) {}

bool Recorder::BeginCapture(const wchar_t* path)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!path || m_armed.load() || m_capturing.load()) return false;
    m_path = path;
    m_armed.store(true);
    return true;
}

void Recorder::SetBackbufferInfo(u32 width, u32 height, DXGI_FORMAT format)
{
    m_bbWidth = width;
    m_bbHeight = height;
    m_bbFormat = static_cast<u32>(format);
}

void Recorder::OnPresent()
{
    if (m_capturing.load()) Finish();
    else if (m_armed.exchange(false)) Start();
}

void Recorder::Start()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_writer.Open(m_path.c_str())) { m_lastOk = false; return; }
    m_defined.assign(m_defined.size(), 0);
    m_maps.clear();
    m_undefined.clear();
    for (u32& c : m_kindCounts) c = 0;
    m_contentBytes = 0;
    m_lastRenderTarget = 0;
    WriteHeader();
    m_capturing.store(true);
    { Emit e(*this, OP_FRAME_BEGIN); }
    Snapshot();
}

void Recorder::Finish()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    { Emit e(*this, OP_FRAME_END); }
    m_capturing.store(false);
    WriteSummary();
    m_writer.Close();
    m_lastOk = !m_writer.Failed();
    m_maps.clear();
    m_completed.fetch_add(1);
}

void Recorder::WriteHeader()
{
    char adapter[512] = {};
    int adapterBytes = 0;
    IDXGIDevice* dxgi = nullptr;
    if (SUCCEEDED(m_dev->QueryInterface(__uuidof(IDXGIDevice), reinterpret_cast<void**>(&dxgi)))) {
        IDXGIAdapter* a = nullptr;
        DXGI_ADAPTER_DESC d{};
        if (SUCCEEDED(dxgi->GetAdapter(&a)) && SUCCEEDED(a->GetDesc(&d)))
            adapterBytes = WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, adapter,
                                               sizeof(adapter) - 8, nullptr, nullptr);
        SafeRelease(a);
        dxgi->Release();
    }
    if (adapterBytes > 0) --adapterBytes;                 // drop the terminator
    const u32 padded = (static_cast<u32>(adapterBytes) + 7u) & ~7u;
    const u32 fixed[8] = {kVersion, kHeaderFixedBytes + padded, m_bbWidth, m_bbHeight, m_bbFormat,
                          static_cast<u32>(m_dev->GetFeatureLevel()),
                          static_cast<u32>(adapterBytes), 0};
    m_writer.WriteDirect(kMagic, 8);
    m_writer.WriteDirect(fixed, sizeof(fixed));
    m_writer.WriteDirect(adapter, padded);                // zero padded by the initialiser
}

void Recorder::WriteSummary()
{
    const u64 records = m_writer.RecordCount();
    u32 ops = 0, kinds = 0;
    for (u32 op = 0; op < OP_MAX; ++op) ops += m_writer.OpCount(op) ? 1 : 0;
    for (u32 k = 0; k < K_COUNT; ++k) kinds += m_kindCounts[k] ? 1 : 0;
    u32 opCounts[OP_MAX];
    for (u32 op = 0; op < OP_MAX; ++op) opCounts[op] = m_writer.OpCount(op);

    Emit e(*this, OP_SUMMARY);
    e.q(m_lastRenderTarget).q(m_contentBytes).q(records);
    e.u(ops);
    for (u32 op = 0; op < OP_MAX; ++op)
        if (opCounts[op]) e.u(op).u(opCounts[op]);
    e.u(kinds);
    for (u32 k = 0; k < K_COUNT; ++k)
        if (m_kindCounts[k]) e.u(k).u(m_kindCounts[k]);
    e.u(static_cast<u32>(m_undefined.size()));
    for (const Undefined& u : m_undefined) e.q(u.id).u(u.sub).u(u.reason);
}

void Recorder::OnCreated(ID3D11DeviceChild* o, Kind k)
{
    EnsureId(o);
    if (!Capturing()) return;
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (Capturing()) Ref(o, k);
}

u64 Recorder::Ref(ID3D11DeviceChild* o, Kind k)
{
    if (!o) return 0;
    const u64 id = EnsureId(o);
    if (id >= m_defined.size()) m_defined.resize(static_cast<size_t>(id) + 4096, 0);
    if (!m_defined[static_cast<size_t>(id)]) {
        m_defined[static_cast<size_t>(id)] = 1;     // set first: a view defines its resource
        Define(o, id, k);
    }
    return id;
}

void Recorder::NoteRenderTarget(ID3D11RenderTargetView* rtv)
{
    if (!rtv) return;
    ID3D11Resource* res = nullptr;
    rtv->GetResource(&res);
    if (res) {
        m_lastRenderTarget = GetId(res);
        res->Release();
    }
}

void Recorder::Touch(ID3D11DeviceChild* o, Kind k)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (Capturing()) Ref(o, k);
}

// Called after a successful Map. The mapped pointer is kept so that Unmap can
// record the bytes the application wrote.
void Recorder::EmitMap(ID3D11Resource* r, u32 sub, D3D11_MAP type, u32 flags,
                       const D3D11_MAPPED_SUBRESOURCE& m)
{
    Emit e(*this, OP_MAP);
    e.id(r, K_RESOURCE).u(sub).u(type).u(flags);
    m_maps.push_back({r, sub, type != D3D11_MAP_READ, static_cast<const u8*>(m.pData), m.RowPitch,
                      m.DepthPitch});
}
} // namespace raw::rec
