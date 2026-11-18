#pragma once
// Internal helpers shared by the recorder and the replayer: object ids stored
// as private data, refcounted side-data blobs, format sizes and a texture
// description that covers 1D, 2D and 3D textures.
#include "rec_format.h"
#include <d3d11.h>
#include <vector>

namespace raw::rec
{
// ---- ids -------------------------------------------------------------------
u64 GetId(ID3D11DeviceChild* o);        // 0 when the object carries no id
u64 EnsureId(ID3D11DeviceChild* o);     // assigns the next id when missing

// FNV-1a 64, the same hash RAW's proxy logs for shader bytecode.
u64 Fnv1a64(const void* data, size_t bytes);

// ---- side data -------------------------------------------------------------
extern const GUID kGuidBytecode;        // shader bytecode copy
extern const GUID kGuidLayout;          // serialised input layout (elements + signature)
extern const GUID kGuidStreamOut;       // serialised stream-output declaration
extern const GUID kGuidRecorder;        // Recorder* on the real device

void AttachBlob(ID3D11DeviceChild* o, const GUID& guid, std::vector<u8>&& bytes);
// Returns a referenced IUnknown that keeps *data alive, or null. Release it.
IUnknown* FetchBlob(ID3D11DeviceChild* o, const GUID& guid, const u8** data, size_t* bytes);

// Serialised CreateInputLayout / stream-output arguments (layouts in the format doc).
std::vector<u8> PackLayout(const D3D11_INPUT_ELEMENT_DESC* e, u32 n, const void* sig, size_t sigBytes);
std::vector<u8> PackStreamOut(const D3D11_SO_DECLARATION_ENTRY* e, u32 n, const UINT* strides,
                              u32 numStrides, u32 rasterizedStream);
Kind AsyncKind(ID3D11Asynchronous* a);     // K_PREDICATE, K_QUERY or K_OPAQUE
// Copies tightly packed rows into a mapped subresource.
void CopyRowsOut(void* dst, u32 rowPitch, u32 slicePitch, const u8* src, u32 rowBytes, u32 rows,
                 u32 slices);

// ---- formats and geometry ---------------------------------------------------
struct FormatSize { u32 bytes; u32 block; };    // bytes per texel block; block edge 1 or 4
FormatSize FormatInfo(DXGI_FORMAT f);            // bytes == 0: unknown format

struct TexDesc
{
    u32 dim = 0;            // 1, 2 or 3
    u32 width = 0, height = 1, depth = 1, mips = 1, arraySize = 1, format = 0;
    u32 sampleCount = 1, sampleQuality = 0, usage = 0, bind = 0, cpu = 0, misc = 0;
    u32 SubCount() const { return mips * arraySize; }
};
bool GetTexDesc(ID3D11Resource* r, TexDesc* out);
HRESULT CreateTexture(ID3D11Device* dev, const TexDesc& d, const D3D11_SUBRESOURCE_DATA* init,
                      ID3D11Resource** out);

// Tightly packed size of one subresource (or a box inside it).
struct SubGeom { u32 rowBytes = 0, rows = 0, slices = 0; bool ok = false; };
SubGeom TexSubGeom(const TexDesc& d, u32 subresource, const D3D11_BOX* box);
SubGeom ResourceSubGeom(ID3D11Resource* r, u32 subresource, const D3D11_BOX* box);

template <class T> void SafeRelease(T*& p) { if (p) { p->Release(); p = nullptr; } }
} // namespace raw::rec
