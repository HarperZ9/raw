// Serialised side data attached to objects at creation: input-layout elements
// with their semantic names and signature bytecode, and stream-output
// declarations. The same bytes are written into DEFINE records unchanged.
#include "rec_util.h"
#include <cstring>

namespace raw::rec
{
namespace
{
void PutU32(std::vector<u8>& v, u32 x)
{
    const u8* b = reinterpret_cast<const u8*>(&x);
    v.insert(v.end(), b, b + 4);
}

void PutBytes(std::vector<u8>& v, const void* p, size_t n)
{
    const u8* b = static_cast<const u8*>(p);
    if (n) v.insert(v.end(), b, b + n);
    while (v.size() & 7) v.push_back(0);
}

// u32 length (0xFFFFFFFF for a null name), u32 zero, then the padded bytes.
void PutName(std::vector<u8>& v, const char* name)
{
    const u32 n = name ? static_cast<u32>(strlen(name)) : kKeep;
    PutU32(v, n);
    PutU32(v, 0);
    if (name) PutBytes(v, name, n);
}
} // namespace

std::vector<u8> PackLayout(const D3D11_INPUT_ELEMENT_DESC* e, u32 n, const void* sig, size_t sigBytes)
{
    std::vector<u8> v;
    PutU32(v, n);
    PutU32(v, 0);
    for (u32 i = 0; i < n; ++i) {
        PutU32(v, e[i].SemanticIndex);
        PutU32(v, static_cast<u32>(e[i].Format));
        PutU32(v, e[i].InputSlot);
        PutU32(v, e[i].AlignedByteOffset);
        PutU32(v, static_cast<u32>(e[i].InputSlotClass));
        PutU32(v, e[i].InstanceDataStepRate);
        PutName(v, e[i].SemanticName);
    }
    const u64 bytes = sigBytes;
    PutBytes(v, &bytes, 8);
    PutBytes(v, sig, sigBytes);
    return v;
}

std::vector<u8> PackStreamOut(const D3D11_SO_DECLARATION_ENTRY* e, u32 n, const UINT* strides,
                              u32 numStrides, u32 rasterizedStream)
{
    std::vector<u8> v;
    PutU32(v, n);
    PutU32(v, numStrides);
    PutU32(v, rasterizedStream);
    PutU32(v, 0);
    for (u32 i = 0; i < n; ++i) {
        PutU32(v, e[i].Stream);
        PutU32(v, e[i].SemanticIndex);
        PutU32(v, e[i].StartComponent | (static_cast<u32>(e[i].ComponentCount) << 8)
                      | (static_cast<u32>(e[i].OutputSlot) << 16));
        PutU32(v, 0);
        PutName(v, e[i].SemanticName);
    }
    for (u32 i = 0; i < numStrides; ++i) PutU32(v, strides ? strides[i] : 0);
    while (v.size() & 7) v.push_back(0);
    return v;
}

void CopyRowsOut(void* dst, u32 rowPitch, u32 slicePitch, const u8* src, u32 rowBytes, u32 rows,
                 u32 slices)
{
    u8* out = static_cast<u8*>(dst);
    for (u32 z = 0; z < slices; ++z)
        for (u32 y = 0; y < rows; ++y) {
            memcpy(out + static_cast<size_t>(z) * slicePitch + static_cast<size_t>(y) * rowPitch, src,
                   rowBytes);
            src += rowBytes;
        }
}

Kind AsyncKind(ID3D11Asynchronous* a)
{
    if (!a) return K_NONE;
    IUnknown* u = nullptr;
    if (SUCCEEDED(a->QueryInterface(__uuidof(ID3D11Predicate), reinterpret_cast<void**>(&u)))) {
        u->Release();
        return K_PREDICATE;
    }
    if (SUCCEEDED(a->QueryInterface(__uuidof(ID3D11Query), reinterpret_cast<void**>(&u)))) {
        u->Release();
        return K_QUERY;
    }
    return K_OPAQUE;
}
} // namespace raw::rec
