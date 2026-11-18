// DxbcContainer: see the header. Original work, MIT.
#include "DxbcContainer.h"

#include <cstring>

namespace raw::dxbc
{
namespace
{
uint32_t Rd(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
void Wr(std::vector<uint8_t>& b, uint32_t v) { const size_t n = b.size(); b.resize(n + 4); memcpy(b.data() + n, &v, 4); }

// The round function of MD5 over one 64-byte block.
void Md5Block(uint32_t s[4], const uint8_t* block)
{
    static const uint32_t K[64] = {
        0xd76aa478, 0xe8c7b756, 0x242070db, 0xc1bdceee, 0xf57c0faf, 0x4787c62a, 0xa8304613, 0xfd469501,
        0x698098d8, 0x8b44f7af, 0xffff5bb1, 0x895cd7be, 0x6b901122, 0xfd987193, 0xa679438e, 0x49b40821,
        0xf61e2562, 0xc040b340, 0x265e5a51, 0xe9b6c7aa, 0xd62f105d, 0x02441453, 0xd8a1e681, 0xe7d3fbc8,
        0x21e1cde6, 0xc33707d6, 0xf4d50d87, 0x455a14ed, 0xa9e3e905, 0xfcefa3f8, 0x676f02d9, 0x8d2a4c8a,
        0xfffa3942, 0x8771f681, 0x6d9d6122, 0xfde5380c, 0xa4beea44, 0x4bdecfa9, 0xf6bb4b60, 0xbebfbc70,
        0x289b7ec6, 0xeaa127fa, 0xd4ef3085, 0x04881d05, 0xd9d4d039, 0xe6db99e5, 0x1fa27cf8, 0xc4ac5665,
        0xf4292244, 0x432aff97, 0xab9423a7, 0xfc93a039, 0x655b59c3, 0x8f0ccc92, 0xffeff47d, 0x85845dd1,
        0x6fa87e4f, 0xfe2ce6e0, 0xa3014314, 0x4e0811a1, 0xf7537e82, 0xbd3af235, 0x2ad7d2bb, 0xeb86d391};
    static const uint8_t R[64] = {7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 7, 12, 17, 22, 5, 9,  14, 20, 5, 9,
                                  14, 20, 5, 9,  14, 20, 5, 9,  14, 20, 4, 11, 16, 23, 4, 11, 16, 23, 4, 11, 16, 23,
                                  4, 11, 16, 23, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21, 6, 10, 15, 21};
    uint32_t m[16];
    memcpy(m, block, 64);
    uint32_t a = s[0], b = s[1], c = s[2], d = s[3];
    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t f, g;
        if (i < 16)      { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d;          g = (3 * i + 5) & 15; }
        else             { f = c ^ (b | ~d);       g = (7 * i) & 15; }
        const uint32_t t = a + f + K[i] + m[g];
        a = d; d = c; c = b;
        b = b + ((t << R[i]) | (t >> (32 - R[i])));
    }
    s[0] += a; s[1] += b; s[2] += c; s[3] += d;
}
} // namespace

// The container hash is MD5's round function with its own closing block: the
// bit count leads the last block and (bits >> 2) | 1 ends it.
void ComputeHash(const uint8_t* bytes, size_t size, uint8_t out[16])
{
    uint32_t s[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    const uint8_t* p = bytes + 20;
    const size_t n = size - 20;
    const uint32_t bits = uint32_t(n * 8);
    const size_t full = n / 64, rest = n % 64;
    for (size_t i = 0; i < full; ++i) Md5Block(s, p + i * 64);
    const uint8_t* tail = p + full * 64;
    uint8_t block[64] = {};
    const uint32_t closing = (bits >> 2) | 1;
    if (rest >= 56) {
        memcpy(block, tail, rest);
        block[rest] = 0x80;
        Md5Block(s, block);
        memset(block, 0, 64);
        memcpy(block, &bits, 4);
        memcpy(block + 60, &closing, 4);
        Md5Block(s, block);
    } else {
        memcpy(block, &bits, 4);
        memcpy(block + 4, tail, rest);
        block[4 + rest] = 0x80;
        memcpy(block + 60, &closing, 4);
        Md5Block(s, block);
    }
    memcpy(out, s, 16);
}

bool HashMatches(const void* bytes, size_t size)
{
    if (size < 32) return false;
    uint8_t h[16];
    ComputeHash(static_cast<const uint8_t*>(bytes), size, h);
    return memcmp(h, static_cast<const uint8_t*>(bytes) + 4, 16) == 0;
}

bool Container::Parse(const void* bytes, size_t size)
{
    m_chunks.clear();
    const uint8_t* b = static_cast<const uint8_t*>(bytes);
    if (size < 32 || Rd(b) != FourCC('D', 'X', 'B', 'C') || Rd(b + 24) > size) return false;
    const uint32_t count = Rd(b + 28);
    if (32 + uint64_t(count) * 4 > size) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t off = Rd(b + 32 + 4 * i);
        if (uint64_t(off) + 8 > size) return false;
        const uint32_t len = Rd(b + off + 4);
        if (uint64_t(off) + 8 + len > size) return false;
        Chunk c;
        c.tag = Rd(b + off);
        c.data.assign(b + off + 8, b + off + 8 + len);
        m_chunks.push_back(std::move(c));
    }
    return true;
}

std::vector<uint8_t> Container::Build() const
{
    std::vector<uint8_t> out;
    Wr(out, FourCC('D', 'X', 'B', 'C'));
    out.resize(20, 0);
    Wr(out, 1);
    Wr(out, 0);                                    // total size, set below
    Wr(out, uint32_t(m_chunks.size()));
    uint32_t off = 32 + 4 * uint32_t(m_chunks.size());
    for (const Chunk& c : m_chunks) { Wr(out, off); off += 8 + uint32_t(c.data.size()); }
    for (const Chunk& c : m_chunks) {
        Wr(out, c.tag);
        Wr(out, uint32_t(c.data.size()));
        out.insert(out.end(), c.data.begin(), c.data.end());
    }
    const uint32_t total = uint32_t(out.size());
    memcpy(out.data() + 24, &total, 4);
    ComputeHash(out.data(), out.size(), out.data() + 4);
    return out;
}

Chunk* Container::Find(uint32_t tag)
{
    for (Chunk& c : m_chunks) if (c.tag == tag) return &c;
    return nullptr;
}
const Chunk* Container::Find(uint32_t tag) const { return const_cast<Container*>(this)->Find(tag); }
Chunk* Container::Program()
{
    Chunk* c = Find(FourCC('S', 'H', 'E', 'X'));
    return c ? c : Find(FourCC('S', 'H', 'D', 'R'));
}
const Chunk* Container::Program() const { return const_cast<Container*>(this)->Program(); }

bool Container::ReadSignature(uint32_t tag, std::vector<SignatureElement>* out) const
{
    out->clear();
    const Chunk* c = Find(tag);
    if (!c || c->data.size() < 8) return false;
    const uint8_t* d = c->data.data();
    const uint32_t count = Rd(d);
    if (8 + uint64_t(count) * 24 > c->data.size()) return false;
    for (uint32_t i = 0; i < count; ++i) {
        const uint8_t* e = d + 8 + 24 * i;
        SignatureElement s;
        const uint32_t name = Rd(e);
        if (name >= c->data.size()) return false;
        s.semantic = reinterpret_cast<const char*>(d + name);
        s.index = Rd(e + 4); s.systemValue = Rd(e + 8); s.componentType = Rd(e + 12); s.reg = Rd(e + 16);
        s.mask = e[20]; s.rwMask = e[21];
        out->push_back(std::move(s));
    }
    return true;
}

bool Container::WriteSignature(uint32_t tag, const std::vector<SignatureElement>& in)
{
    Chunk* c = Find(tag);
    if (!c) return false;
    std::vector<uint8_t> d;
    Wr(d, uint32_t(in.size()));
    Wr(d, 8);
    const uint32_t base = 8 + 24 * uint32_t(in.size());
    std::vector<uint8_t> names;
    for (const SignatureElement& s : in) {
        Wr(d, base + uint32_t(names.size()));
        Wr(d, s.index); Wr(d, s.systemValue); Wr(d, s.componentType); Wr(d, s.reg);
        d.push_back(s.mask); d.push_back(s.rwMask); d.push_back(0); d.push_back(0);
        names.insert(names.end(), s.semantic.begin(), s.semantic.end());
        names.push_back(0);
    }
    while (names.size() % 4) names.push_back(0xAB);
    d.insert(d.end(), names.begin(), names.end());
    c->data = std::move(d);
    return true;
}

} // namespace raw::dxbc
