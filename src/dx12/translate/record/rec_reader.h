#pragma once
// Call-stream file reader: validates the header and the record chain, then
// hands out records as views into the loaded file.
#include "rec_format.h"
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace raw::rec
{
struct Record
{
    u32       op = 0;
    u32       bytes = 0;        // payload bytes
    const u8* data = nullptr;   // payload
    u64       offset = 0;       // file offset of the record header
};

// Bounds-checked payload cursor. After a short read ok is false and every
// later read returns zero.
struct Cursor
{
    const u8* p = nullptr;
    const u8* end = nullptr;
    bool      ok = true;

    explicit Cursor(const Record& r) : p(r.data), end(r.data + r.bytes) {}
    Cursor(const u8* data, size_t bytes) : p(data), end(data + bytes) {}

    const u8* Raw(size_t n)
    {
        if (!ok || static_cast<size_t>(end - p) < n) { ok = false; return nullptr; }
        const u8* at = p;
        p += n;
        return at;
    }
    template <class T> T Get()
    {
        T v{};
        if (const u8* at = Raw(sizeof(T))) memcpy(&v, at, sizeof(T));
        return v;
    }
    u32   U32() { return Get<u32>(); }
    u64   U64() { return Get<u64>(); }
    float F32() { return Get<float>(); }
    const u8* Blob(u64* bytes)      // u64 length, data, zero pad to 8
    {
        *bytes = U64();
        if (!ok || *bytes > static_cast<u64>(end - p)) { ok = false; *bytes = 0; return nullptr; }
        const u8* at = Raw(static_cast<size_t>(*bytes));
        Raw(static_cast<size_t>((8 - (*bytes & 7)) & 7));
        return at;
    }
};

// One subresource's recorded contents (the SUBDATA structure in the format doc).
struct SubData
{
    u32 state = 0, reason = 0, rowBytes = 0, rows = 0, slices = 0;
    const u8* data = nullptr;
    u64 bytes = 0;
};
SubData ReadSubData(Cursor& c);

struct FileHeader
{
    u32 version = 0, backbufferWidth = 0, backbufferHeight = 0, backbufferFormat = 0;
    u32 featureLevel = 0;
    std::string adapter;        // UTF-8
};

struct Summary
{
    struct Undefined { u64 id; u32 subresource; u32 reason; };
    u64 lastRenderTarget = 0, contentBytes = 0, recordCount = 0;
    std::vector<std::pair<u32, u32>> opCounts;      // opcode, count
    std::vector<std::pair<u32, u32>> kindCounts;    // kind, defined objects
    std::vector<Undefined> undefined;
};

class Reader
{
public:
    bool Open(const wchar_t* path);                 // false: see Error()
    bool OpenMemory(std::vector<u8>&& bytes);
    const std::string& Error() const { return m_error; }

    const FileHeader& Header() const { return m_header; }
    const Summary& GetSummary() const { return m_summary; }
    const std::vector<Record>& Records() const { return m_records; }
    u64 FileBytes() const { return m_bytes.size(); }

private:
    bool Fail(const char* what, u64 at);
    bool ParseHeader(u64* next);
    bool ParseRecords(u64 at);
    bool ParseSummary();

    std::vector<u8>     m_bytes;
    std::vector<Record> m_records;
    FileHeader          m_header;
    Summary             m_summary;
    std::string         m_error;
};

// Writes one text line per record to out (a FILE*). Returns false and prints
// the reader error when the file does not load.
bool DumpListing(const wchar_t* path, void* out);
} // namespace raw::rec
