#pragma once
// Buffered record writer. Records can nest: a record begun while another is
// open (a DEFINE emitted while a call's payload is being written) is finished
// first and so lands in the file before the record that references it.
#include "rec_format.h"
#include <cstddef>
#include <vector>

namespace raw::rec
{
class Writer
{
public:
    ~Writer() { Close(); }

    bool Open(const wchar_t* path);
    void Close();                       // flushes; safe to call twice
    bool IsOpen() const { return m_file != nullptr; }
    bool Failed() const { return m_failed; }

    void WriteDirect(const void* p, size_t n);   // header bytes, outside any record

    void Begin(u32 op);
    void End();

    void U32(u32 v) { Raw(&v, 4); }
    void U64(u64 v) { Raw(&v, 8); }
    void F32(float v) { Raw(&v, 4); }
    void Raw(const void* p, size_t n);
    void Pad8();
    void BlobBegin(u64 bytes) { U64(bytes); }    // follow with Raw() calls, then Pad8()
    void Blob(const void* p, size_t n) { BlobBegin(n); Raw(p, n); Pad8(); }

    u64 RecordCount() const { return m_records; }
    u32 OpCount(u32 op) const { return op < OP_MAX ? m_opCounts[op] : 0; }

private:
    static constexpr int kMaxDepth = 8;
    static constexpr size_t kFlushBytes = 4u << 20;

    void Flush();

    void*           m_file = nullptr;            // HANDLE
    bool            m_failed = false;
    std::vector<u8> m_out;
    std::vector<u8> m_scratch[kMaxDepth];
    u32             m_op[kMaxDepth] = {};
    int             m_depth = -1;
    u64             m_records = 0;
    u32             m_opCounts[OP_MAX] = {};
};
} // namespace raw::rec
