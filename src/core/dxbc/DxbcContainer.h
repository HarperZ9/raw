#pragma once
// DxbcContainer: read, edit and rebuild a compiled D3D shader container.
//
// A container is a header (magic, a 16-byte hash, total size, chunk count), a
// table of chunk offsets and the chunks. RAW edits two kinds of chunk: the
// signatures (ISGN, OSGN) and the program (SHDR, SHEX). The hash is recomputed
// on rebuild; the runtime refuses a container whose hash does not match.
//
// Original work, MIT.
#include <cstdint>
#include <string>
#include <vector>

namespace raw::dxbc
{

constexpr uint32_t FourCC(char a, char b, char c, char d)
{
    return uint32_t(uint8_t(a)) | uint32_t(uint8_t(b)) << 8 | uint32_t(uint8_t(c)) << 16 | uint32_t(uint8_t(d)) << 24;
}

struct Chunk
{
    uint32_t tag = 0;
    std::vector<uint8_t> data;
};

// One row of an input or output signature.
struct SignatureElement
{
    std::string semantic;
    uint32_t index = 0;
    uint32_t systemValue = 0;     // D3D_NAME: 0 none, 1 position, 64 target
    uint32_t componentType = 3;   // 3 float32
    uint32_t reg = 0;
    uint8_t  mask = 0xF;
    uint8_t  rwMask = 0;          // inputs: components read; outputs: components never written
};

class Container
{
public:
    // False when the bytes are not a container or a chunk runs past the end.
    bool Parse(const void* bytes, size_t size);
    std::vector<uint8_t> Build() const;

    Chunk* Find(uint32_t tag);
    const Chunk* Find(uint32_t tag) const;
    Chunk* Program();                           // SHEX or SHDR
    const Chunk* Program() const;

    // Signatures in the plain ISGN and OSGN forms. False for the newer forms.
    bool ReadSignature(uint32_t tag, std::vector<SignatureElement>* out) const;
    bool WriteSignature(uint32_t tag, const std::vector<SignatureElement>& in);

private:
    std::vector<Chunk> m_chunks;
};

// The container hash over bytes[20..size). out receives 16 bytes.
void ComputeHash(const uint8_t* bytes, size_t size, uint8_t out[16]);
// True when the hash stored in the container equals the computed one.
bool HashMatches(const void* bytes, size_t size);

} // namespace raw::dxbc
