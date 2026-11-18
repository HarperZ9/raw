// Replayer: DEFINE records for shaders and input layouts.
#include "rec_replayer.h"
#include "rec_util.h"

namespace raw::rec
{
namespace
{
// Name as written by the recorder: u32 length (0xFFFFFFFF = null), u32 zero,
// then the bytes padded to 8.
bool ReadName(Cursor& c, std::string* out, bool* isNull)
{
    const u32 n = c.U32();
    c.U32();
    *isNull = n == kKeep;
    if (*isNull) return c.ok;
    const u8* p = c.Raw(n);
    c.Raw((8 - (n & 7)) & 7);
    if (!c.ok) return false;
    out->assign(reinterpret_cast<const char*>(p), n);
    return true;
}

struct StreamOut
{
    std::vector<std::string> names;
    std::vector<D3D11_SO_DECLARATION_ENTRY> entries;
    std::vector<UINT> strides;
    u32 rasterizedStream = 0;
};

bool ParseStreamOut(const u8* data, u64 bytes, StreamOut* so)
{
    Cursor c(data, static_cast<size_t>(bytes));
    const u32 n = c.U32(), numStrides = c.U32();
    so->rasterizedStream = c.U32();
    c.U32();
    if (!c.ok || n > 512 || numStrides > 16) return false;
    so->names.resize(n);
    so->entries.resize(n);
    for (u32 i = 0; i < n; ++i) {
        D3D11_SO_DECLARATION_ENTRY& e = so->entries[i];
        e.Stream = c.U32();
        e.SemanticIndex = c.U32();
        const u32 packed = c.U32();
        c.U32();
        e.StartComponent = static_cast<BYTE>(packed & 255);
        e.ComponentCount = static_cast<BYTE>((packed >> 8) & 255);
        e.OutputSlot = static_cast<BYTE>((packed >> 16) & 255);
        bool isNull = false;
        if (!ReadName(c, &so->names[i], &isNull)) return false;
        e.SemanticName = isNull ? nullptr : so->names[i].c_str();
    }
    for (u32 i = 0; i < numStrides; ++i) so->strides.push_back(c.U32());
    return c.ok;
}
} // namespace

HRESULT Replayer::CreateShader(u32 kind, const u8* code, u64 bytes, const u8* so, u64 soBytes,
                               ID3D11DeviceChild** out)
{
    const SIZE_T n = static_cast<SIZE_T>(bytes);
    if (kind == K_GS && so) {
        StreamOut s;
        if (!ParseStreamOut(so, soBytes, &s)) return Fail(E_FAIL, "malformed stream-output declaration");
        return m_dev->CreateGeometryShaderWithStreamOutput(
            code, n, s.entries.data(), static_cast<UINT>(s.entries.size()), s.strides.data(),
            static_cast<UINT>(s.strides.size()), s.rasterizedStream, nullptr,
            reinterpret_cast<ID3D11GeometryShader**>(out));
    }
    switch (kind) {
    case K_VS: return m_dev->CreateVertexShader(code, n, nullptr, reinterpret_cast<ID3D11VertexShader**>(out));
    case K_HS: return m_dev->CreateHullShader(code, n, nullptr, reinterpret_cast<ID3D11HullShader**>(out));
    case K_DS: return m_dev->CreateDomainShader(code, n, nullptr, reinterpret_cast<ID3D11DomainShader**>(out));
    case K_GS: return m_dev->CreateGeometryShader(code, n, nullptr, reinterpret_cast<ID3D11GeometryShader**>(out));
    case K_PS: return m_dev->CreatePixelShader(code, n, nullptr, reinterpret_cast<ID3D11PixelShader**>(out));
    case K_CS: return m_dev->CreateComputeShader(code, n, nullptr, reinterpret_cast<ID3D11ComputeShader**>(out));
    default: return Fail(E_INVALIDARG, "unknown shader kind");
    }
}

HRESULT Replayer::DefineShader(Cursor& c)
{
    const u64 id = c.U64();
    const u32 kind = c.U32(), flags = c.U32();
    const u64 hash = c.U64();
    u64 bytes = 0, soBytes = 0;
    const u8* code = c.Blob(&bytes);
    const u8* so = (flags & 2) ? c.Blob(&soBytes) : nullptr;
    if (!c.ok) return Short();
    if (!(flags & 1) || bytes == 0)
        return Add(id, kind, nullptr, Fail(E_FAIL, "shader was recorded without its bytecode"));
    if (Fnv1a64(code, static_cast<size_t>(bytes)) != hash)
        return Add(id, kind, nullptr, Fail(E_FAIL, "shader bytecode does not match its recorded hash"));
    ID3D11DeviceChild* shader = nullptr;
    const HRESULT hr = CreateShader(kind, code, bytes, so, soBytes, &shader);
    return Add(id, kind, shader, hr);
}

HRESULT Replayer::DefineInputLayout(Cursor& c)
{
    const u64 id = c.U64();
    const u32 state = c.U32();
    c.U32();
    if (!c.ok) return Short();
    if (state != SUB_DATA)
        return Add(id, K_INPUT_LAYOUT, nullptr, Fail(E_FAIL, "input layout was recorded without its elements"));
    const u32 n = c.U32();
    c.U32();
    if (!c.ok || n > 256) return Fail(E_INVALIDARG, "malformed input layout");
    std::vector<std::string> names(n);
    std::vector<D3D11_INPUT_ELEMENT_DESC> elems(n);
    for (u32 i = 0; i < n; ++i) {
        D3D11_INPUT_ELEMENT_DESC& e = elems[i];
        e.SemanticIndex = c.U32();
        e.Format = static_cast<DXGI_FORMAT>(c.U32());
        e.InputSlot = c.U32();
        e.AlignedByteOffset = c.U32();
        e.InputSlotClass = static_cast<D3D11_INPUT_CLASSIFICATION>(c.U32());
        e.InstanceDataStepRate = c.U32();
        bool isNull = false;
        if (!ReadName(c, &names[i], &isNull)) return Short();
        e.SemanticName = isNull ? nullptr : names[i].c_str();
    }
    u64 sigBytes = 0;
    const u8* sig = c.Blob(&sigBytes);
    if (!c.ok) return Short();
    ID3D11InputLayout* il = nullptr;
    const HRESULT hr = m_dev->CreateInputLayout(elems.data(), n, sig, static_cast<SIZE_T>(sigBytes), &il);
    return Add(id, K_INPUT_LAYOUT, il, hr);
}
} // namespace raw::rec
