#include "tx_reflect.h"

namespace raw::tx {

static uint32_t U32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
static constexpr uint32_t FourCC(char a, char b, char c, char d)
{
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}

static const uint8_t* FindChunk(const uint8_t* code, size_t size, uint32_t cc, uint32_t* chunkSize)
{
    if (size < 32 || U32(code) != FourCC('D', 'X', 'B', 'C')) return nullptr;
    uint32_t count = U32(code + 28);
    if (size < 32 + size_t(count) * 4) return nullptr;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t off = U32(code + 32 + i * 4);
        if (size_t(off) + 8 > size) return nullptr;
        uint32_t len = U32(code + off + 4);
        if (size_t(off) + 8 + len > size) return nullptr;
        if (U32(code + off) == cc) { *chunkSize = len; return code + off + 8; }
    }
    return nullptr;
}

static void AddSrv(ShaderData& o, uint32_t slot, uint32_t count, uint8_t dim)
{
    if (slot + count > 128) return;
    if (o.srvDim.size() < slot + count) o.srvDim.resize(slot + count, 0);
    for (uint32_t i = 0; i < count; ++i) o.srvDim[slot + i] = dim;
    if (o.bind.srvCount < slot + count) o.bind.srvCount = uint8_t(slot + count);
}
static void AddUav(ShaderData& o, uint32_t slot, uint32_t count, uint8_t dim)
{
    if (slot + count > 64) return;
    if (o.uavDim.size() < slot + count) o.uavDim.resize(slot + count, 0);
    for (uint32_t i = 0; i < count; ++i) o.uavDim[slot + i] = dim;
    if (o.bind.uavCount < slot + count) o.bind.uavCount = uint8_t(slot + count);
}
static void AddCb(ShaderData& o, uint32_t slot, uint32_t count)
{
    for (uint32_t i = 0; i < count && slot + i < 14; ++i) o.bind.cbMask |= uint16_t(1u << (slot + i));
}
static void AddSampler(ShaderData& o, uint32_t slot, uint32_t count)
{
    if (slot + count <= 16 && o.bind.sampCount < slot + count) o.bind.sampCount = uint8_t(slot + count);
}

// RDEF: resource binding table. Entry: name, type, return type, dimension,
// sample count, bind point, bind count, flags (32 bytes; 40 in SM 5.1).
static bool FromRdef(const uint8_t* code, size_t size, ShaderData& o)
{
    uint32_t len = 0;
    const uint8_t* d = FindChunk(code, size, FourCC('R', 'D', 'E', 'F'), &len);
    if (!d || len < 28) return false;
    uint32_t count = U32(d + 8), off = U32(d + 12), version = U32(d + 16);
    uint32_t minor = version & 0xFF, major = (version >> 8) & 0xFF;
    uint32_t entry = (major == 5 && minor == 1) ? 40 : 32;
    if (size_t(off) + size_t(count) * entry > len) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* b = d + off + i * entry;
        uint32_t type = U32(b + 4), dim = U32(b + 12), slot = U32(b + 20), n = U32(b + 24);
        if (n == 0) n = 1;
        switch (type) {
        case 0: AddCb(o, slot, n); break;                                    // cbuffer
        case 2: AddSrv(o, slot, n, uint8_t(dim >= 11 ? 1 : dim)); break;     // texture
        case 1: case 5: case 7: AddSrv(o, slot, n, 1); break;                // tbuffer, structured, byte address
        case 3: AddSampler(o, slot, n); break;
        case 4: AddUav(o, slot, n, uint8_t(dim == 8 ? 8 : dim <= 5 ? dim : 1)); break;   // RW typed
        case 6: case 8: case 9: case 10: case 11: AddUav(o, slot, n, 1); break;         // RW buffers
        default: break;
        }
    }
    return true;
}

// Program tokens: opcode in bits 0..10, instruction length in DWORDs in bits
// 24..30. Custom data (opcode 53) keeps its length in the next DWORD.
static uint8_t SrvDimOfToken(uint32_t rd)
{
    // D3D10_SB_RESOURCE_DIMENSION -> D3D12_SRV_DIMENSION
    static const uint8_t map[13] = {0, 1, 2, 4, 6, 8, 9, 3, 5, 7, 10, 1, 1};
    return rd < 13 ? map[rd] : 0;
}

static bool FromProgram(const uint8_t* code, size_t size, ShaderData& o)
{
    uint32_t len = 0;
    const uint8_t* d = FindChunk(code, size, FourCC('S', 'H', 'E', 'X'), &len);
    if (!d) d = FindChunk(code, size, FourCC('S', 'H', 'D', 'R'), &len);
    if (!d || len < 8) return false;
    uint32_t words = U32(d + 4);
    if (size_t(words) * 4 > len) return false;
    for (uint32_t i = 2; i < words;) {
        uint32_t tok = U32(d + i * 4);
        uint32_t op = tok & 0x7FF;
        uint32_t n = (tok >> 24) & 0x7F;
        if (op == 53) n = i + 1 < words ? U32(d + (i + 1) * 4) : 0;
        if (n == 0 || i + n > words) return false;
        bool decl = op == 88 || op == 89 || op == 90 || op == 156 || op == 157 || op == 158 || op == 161 || op == 162;
        if (decl && n >= 3) {
            uint32_t j = i + 1;
            uint32_t operand = U32(d + j * 4);
            ++j;
            while ((operand & 0x80000000u) && j < i + n) { operand = U32(d + j * 4); ++j; }   // extended operand tokens
            if (j >= i + n) return false;
            uint32_t slot = U32(d + j * 4);
            uint32_t rd = (tok >> 11) & 0x1F;
            switch (op) {
            case 89: AddCb(o, slot, 1); break;
            case 90: AddSampler(o, slot, 1); break;
            case 88: AddSrv(o, slot, 1, SrvDimOfToken(rd)); break;
            case 161: case 162: AddSrv(o, slot, 1, 1); break;
            case 156: { uint8_t s = SrvDimOfToken(rd); AddUav(o, slot, 1, uint8_t(s == 8 ? 8 : s <= 5 ? s : 1)); break; }
            default: AddUav(o, slot, 1, 1); break;
            }
        }
        i += n;
    }
    return true;
}

bool ReflectDxbc(const void* code, size_t size, ReflectSource source, ShaderData& out)
{
    auto* p = static_cast<const uint8_t*>(code);
    out.bind = StageBind{};
    out.srvDim.clear();
    out.uavDim.clear();
    bool ok = false;
    if (source != ReflectSource::Program) ok = FromRdef(p, size, out);
    if (!ok && source != ReflectSource::Rdef) {
        out.bind = StageBind{};
        out.srvDim.clear();
        out.uavDim.clear();
        ok = FromProgram(p, size, out);
    }
    out.reflected = ok;
    return ok;
}

}  // namespace raw::tx
