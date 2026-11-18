#pragma once
// Replayer: creates the objects of a call-stream file on any ID3D11Device and
// issues the recorded calls on its immediate context. It uses only base
// ID3D11Device / ID3D11DeviceContext methods: no QueryInterface, no DXGI and
// no private data on the replay device.
#include "rec_reader.h"
#include <d3d11.h>
#include <string>
#include <unordered_map>
#include <vector>

namespace raw::rec
{
struct TexDesc;

struct ReplayOptions
{
    bool strict = true;             // stop at the first failed HRESULT or bad record
    bool skipInitialState = false;  // do not issue the Set records of the initial-state block
    bool clearStateFirst = true;    // ClearState on the replay context before the first record
    bool uploadThroughStaging = false;  // restore texture contents with staging copies instead of
                                        // initial data (immutable textures keep initial data)
    u32  getDataTimeoutMs = 2000;   // upper bound for one blocking GetData poll
};

struct ReplayResult
{
    bool    ok = false;
    HRESULT hr = S_OK;              // first failure
    u64     failedRecord = 0;       // index of the record that failed
    u32     failedOp = 0;
    std::string message;
    u64 records = 0;                // records processed
    u32 opCalls[OP_MAX] = {};       // records issued, per opcode
    u32 objects = 0;                // objects created
    u32 softFailures = 0;           // failures passed over in non-strict mode
    u32 contentsNotRestored = 0;    // subresources left without their recorded contents
    u32 stagingUploads = 0;         // subresources restored through a staging copy
    u32 depthRestored = 0;          // depth-stencil subresources restored
    u32 depthNotRestored = 0;
    u32 unsupported = 0;            // command-list markers
    u32 getDataPolls = 0, getDataTimeouts = 0, getDataNotIssued = 0;
};

class Replayer
{
public:
    ~Replayer() { ReleaseObjects(); }

    bool Open(const wchar_t* path);                 // false: see Error()
    bool OpenMemory(std::vector<u8>&& bytes);
    const std::string& Error() const { return m_reader.Error(); }
    const Reader& File() const { return m_reader; }

    // Replays the whole file. Objects stay alive until ReleaseObjects, the
    // next Run or destruction, so the caller can read targets back.
    bool Run(ID3D11Device* device, ID3D11DeviceContext* context, const ReplayOptions& options = {});
    const ReplayResult& Result() const { return m_res; }

    ID3D11Resource*  FindResource(u64 id) const;    // not AddRef'd; null when absent
    ID3D11Texture2D* FindTexture(u64 id) const;     // null unless the id is a 2D texture
    u64 LastRenderTarget() const { return m_reader.GetSummary().lastRenderTarget; }
    void ReleaseObjects();

private:
    struct Obj { ID3D11DeviceChild* p; u32 kind; };
    struct Mapped { ID3D11Resource* res; u32 sub; D3D11_MAPPED_SUBRESOURCE m; bool write; };

    HRESULT Exec(const Record& r);
    HRESULT Fail(HRESULT hr, const char* what);
    HRESULT Short();
    void NoteFailure(size_t index, const Record& r, HRESULT hr);
    HRESULT Add(u64 id, u32 kind, ID3D11DeviceChild* p, HRESULT hr);
    ID3D11DeviceChild* Get(u64 id, u32 kindLo, u32 kindHi);
    bool Ids(Cursor& c, u32 n, ID3D11DeviceChild** out, u32 kindLo, u32 kindHi);
    ID3D11Resource* Res(Cursor& c);

    HRESULT Define(const Record& r);
    HRESULT DefineBuffer(Cursor& c);
    HRESULT DefineTexture(Cursor& c);
    void UploadTexture(ID3D11Resource* tex, const TexDesc& t, const std::vector<SubData>& subs);
    HRESULT DefineView(Cursor& c);
    HRESULT DefineShader(Cursor& c);
    HRESULT CreateShader(u32 kind, const u8* code, u64 bytes, const u8* so, u64 soBytes,
                         ID3D11DeviceChild** out);
    HRESULT DefineInputLayout(Cursor& c);
    HRESULT DefineState(Cursor& c);
    HRESULT DefineQuery(Cursor& c);

    HRESULT State(const Record& r);
    HRESULT StageSlots(u32 op, Cursor& c);
    HRESULT SetShader(Cursor& c);
    HRESULT InputAssembler(u32 op, Cursor& c);
    HRESULT OmTargets(u32 op, Cursor& c);
    HRESULT OmRsState(u32 op, Cursor& c);
    HRESULT MiscState(u32 op, Cursor& c);

    HRESULT Draw(const Record& r);
    HRESULT Clear(u32 op, Cursor& c);
    HRESULT Data(const Record& r);
    HRESULT MapCall(Cursor& c);
    HRESULT UnmapCall(Cursor& c);
    HRESULT UpdateCall(Cursor& c);
    HRESULT CopyCall(u32 op, Cursor& c);
    HRESULT CopyRegion(ID3D11Resource* dst, Cursor& c);
    HRESULT Query(u32 op, Cursor& c);
    HRESULT GetDataCall(Cursor& c);

    Reader               m_reader;
    ID3D11Device*        m_dev = nullptr;
    ID3D11DeviceContext* m_ctx = nullptr;
    ReplayOptions        m_opt;
    ReplayResult         m_res;
    std::unordered_map<u64, Obj> m_objects;
    std::vector<Mapped>  m_maps;
    std::vector<u64>     m_ended;       // queries that had End issued in this replay
    bool                 m_inState = false;
    bool                 m_badRef = false;
    const char*          m_why = nullptr;   // reason for the failure of the current record
};
} // namespace raw::rec
