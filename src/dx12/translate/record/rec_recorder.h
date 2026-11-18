#pragma once
// Recorder: captures one frame of D3D11 calls made through RecDevice and
// RecContext into a call-stream file. Public surface for the owner (the proxy
// or a test) is BeginCapture / OnPresent / the status getters; the rest is
// used by the wrappers.
#include "rec_format.h"
#include "rec_writer.h"
#include <d3d11.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace raw::rec
{
struct TexDesc;
struct SubGeom;

class Recorder
{
public:
    Recorder(ID3D11Device* real, ID3D11DeviceContext* realCtx);

    // Arms a capture. It starts at the next OnPresent and ends at the one after.
    // Returns false when a capture is already armed or running.
    bool BeginCapture(const wchar_t* path);
    // Frame boundary. The owner calls this once per presented frame, on the
    // thread that uses the immediate context.
    void OnPresent();
    // Optional capture info for the file header.
    void SetBackbufferInfo(u32 width, u32 height, DXGI_FORMAT format);

    bool IsArmed() const { return m_armed.load(); }
    bool IsCapturing() const { return m_capturing.load(std::memory_order_relaxed); }
    u32  CapturesCompleted() const { return m_completed.load(); }
    bool LastCaptureOk() const { return m_lastOk; }     // false: file open or write failed

    // ---- wrapper side ------------------------------------------------------
    bool Capturing() const { return m_capturing.load(std::memory_order_relaxed); }
    void OnCreated(ID3D11DeviceChild* o, Kind k);       // id always; DEFINE while capturing
    u64  Ref(ID3D11DeviceChild* o, Kind k);             // id, emitting a DEFINE on first use
    void NoteRenderTarget(ID3D11RenderTargetView* rtv);
    void Touch(ID3D11DeviceChild* o, Kind k);           // Ref under the lock, no record
    void EmitMap(ID3D11Resource* r, u32 sub, D3D11_MAP type, u32 flags,
                 const D3D11_MAPPED_SUBRESOURCE& m);
    void EmitUnmap(ID3D11Resource* r, u32 sub);
    void EmitUpdate(ID3D11Resource* r, u32 sub, const D3D11_BOX* box, const void* data,
                    u32 rowPitch, u32 depthPitch);

private:
    friend class Emit;
    struct PendingMap
    {
        ID3D11Resource* res; u32 sub; bool write; const u8* data; u32 rowPitch, depthPitch;
    };
    struct Undefined { u64 id; u32 sub; u32 reason; };

    void Start();
    void Finish();
    void WriteHeader();
    void WriteSummary();
    void Snapshot();
    void SnapshotIA();
    void SnapshotStage(u32 stage);
    void SnapshotOM();
    void SnapshotRS();
    void SnapshotMisc();

    void Define(ID3D11DeviceChild* o, u64 id, Kind k);
    void DefineBuffer(ID3D11Buffer* b, u64 id);
    void DefineTexture(ID3D11Resource* r, u64 id);
    void DefineView(ID3D11DeviceChild* o, u64 id, Kind k);
    void DefineShader(ID3D11DeviceChild* o, u64 id, Kind k);
    void DefineInputLayout(ID3D11DeviceChild* o, u64 id);
    void DefineState(ID3D11DeviceChild* o, u64 id, Kind k);
    void DefineQuery(ID3D11DeviceChild* o, u64 id, Kind k);

    void WriteBufferContents(ID3D11Buffer* b, const D3D11_BUFFER_DESC& d, u64 id);
    void WriteTextureContents(ID3D11Resource* r, const TexDesc& t, u64 id);
    void WriteSub(const u8* src, u32 rowPitch, u32 slicePitch, const SubGeom& g);
    void WriteSubUndefined(u64 id, u32 sub, u32 reason);
    void WriteSubNone();

    ID3D11Device*        m_dev;         // real objects, owned by the wrappers
    ID3D11DeviceContext* m_ctx;
    Writer               m_writer;
    std::recursive_mutex m_mutex;
    std::atomic<bool>    m_armed{false};
    std::atomic<bool>    m_capturing{false};
    std::atomic<u32>     m_completed{0};
    bool                 m_lastOk = true;
    std::wstring         m_path;
    std::vector<u8>      m_defined;     // indexed by id
    std::vector<PendingMap> m_maps;
    std::vector<Undefined>  m_undefined;
    u32                  m_kindCounts[K_COUNT] = {};
    u64                  m_contentBytes = 0;
    u64                  m_lastRenderTarget = 0;
    u32                  m_bbWidth = 0, m_bbHeight = 0, m_bbFormat = 0;
};
} // namespace raw::rec
