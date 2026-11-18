#include "rec_reader.h"
#include <windows.h>
#include <cstdio>

namespace raw::rec
{
SubData ReadSubData(Cursor& c)
{
    SubData s;
    s.state = c.U32();
    s.reason = c.U32();
    s.rowBytes = c.U32();
    s.rows = c.U32();
    s.slices = c.U32();
    c.U32();
    if (s.state == SUB_DATA) {
        s.data = c.Blob(&s.bytes);
        if (s.bytes != static_cast<u64>(s.rowBytes) * s.rows * s.slices) c.ok = false;
    }
    return s;
}

bool Reader::Fail(const char* what, u64 at)
{
    char msg[200];
    snprintf(msg, sizeof(msg), "%s (file offset %llu of %llu)", what,
             static_cast<unsigned long long>(at), static_cast<unsigned long long>(m_bytes.size()));
    m_error = msg;
    m_records.clear();
    return false;
}

bool Reader::Open(const wchar_t* path)
{
    HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) { m_error = "cannot open file"; return false; }
    LARGE_INTEGER size{};
    GetFileSizeEx(h, &size);
    std::vector<u8> bytes(static_cast<size_t>(size.QuadPart));
    size_t done = 0;
    while (done < bytes.size()) {
        DWORD want = static_cast<DWORD>(bytes.size() - done > (1u << 30) ? (1u << 30)
                                                                         : bytes.size() - done);
        DWORD got = 0;
        if (!ReadFile(h, bytes.data() + done, want, &got, nullptr) || got == 0) break;
        done += got;
    }
    CloseHandle(h);
    if (done != bytes.size()) { m_error = "read failed"; return false; }
    return OpenMemory(std::move(bytes));
}

bool Reader::OpenMemory(std::vector<u8>&& bytes)
{
    m_bytes = std::move(bytes);
    m_records.clear();
    m_error.clear();
    m_header = {};
    m_summary = {};
    u64 at = 0;
    return ParseHeader(&at) && ParseRecords(at) && ParseSummary();
}

bool Reader::ParseHeader(u64* next)
{
    if (m_bytes.size() < kHeaderFixedBytes) return Fail("file shorter than the header", 0);
    if (memcmp(m_bytes.data(), kMagic, 8) != 0) return Fail("bad magic, not a RAWD3D11 stream", 0);
    u32 f[8];
    memcpy(f, m_bytes.data() + 8, sizeof(f));
    m_header.version = f[0];
    if (f[0] != kVersion) return Fail("unsupported stream version", 8);
    const u32 headerBytes = f[1], adapterBytes = f[6];
    if (headerBytes < kHeaderFixedBytes || headerBytes > m_bytes.size()
        || adapterBytes > headerBytes - kHeaderFixedBytes)
        return Fail("header extends beyond the end of the file", 12);
    m_header.backbufferWidth = f[2];
    m_header.backbufferHeight = f[3];
    m_header.backbufferFormat = f[4];
    m_header.featureLevel = f[5];
    m_header.adapter.assign(reinterpret_cast<const char*>(m_bytes.data()) + kHeaderFixedBytes,
                            adapterBytes);
    *next = headerBytes;
    return true;
}

bool Reader::ParseRecords(u64 at)
{
    const u64 size = m_bytes.size();
    while (at < size) {
        if (size - at < 8) return Fail("truncated record header", at);
        u32 head[2];
        memcpy(head, m_bytes.data() + at, 8);
        if (head[1] > size - at - 8) return Fail("record payload extends beyond the end of the file", at);
        m_records.push_back({head[0], head[1], m_bytes.data() + at + 8, at});
        at += 8 + static_cast<u64>(head[1]);
    }
    if (m_records.empty() || m_records.back().op != OP_SUMMARY)
        return Fail("file does not end with a summary record (truncated capture)", size);
    return true;
}

bool Reader::ParseSummary()
{
    Cursor c(m_records.back());
    m_summary.lastRenderTarget = c.U64();
    m_summary.contentBytes = c.U64();
    m_summary.recordCount = c.U64();
    for (u32 n = c.U32(), i = 0; c.ok && i < n; ++i) {
        u32 op = c.U32(), count = c.U32();
        m_summary.opCounts.push_back({op, count});
    }
    for (u32 n = c.U32(), i = 0; c.ok && i < n; ++i) {
        u32 kind = c.U32(), count = c.U32();
        m_summary.kindCounts.push_back({kind, count});
    }
    for (u32 n = c.U32(), i = 0; c.ok && i < n; ++i) {
        Summary::Undefined u{};
        u.id = c.U64();
        u.subresource = c.U32();
        u.reason = c.U32();
        m_summary.undefined.push_back(u);
    }
    if (!c.ok) return Fail("malformed summary record", m_records.back().offset);
    return true;
}
} // namespace raw::rec
