#include "rec_writer.h"
#include <windows.h>

namespace raw::rec
{
bool Writer::Open(const wchar_t* path)
{
    Close();
    HANDLE h = CreateFileW(path, GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    m_file = h;
    m_failed = false;
    m_depth = -1;
    m_records = 0;
    for (u32& c : m_opCounts) c = 0;
    m_out.clear();
    m_out.reserve(kFlushBytes + (1u << 20));
    return true;
}

void Writer::Close()
{
    if (!m_file) return;
    Flush();
    CloseHandle(static_cast<HANDLE>(m_file));
    m_file = nullptr;
    m_depth = -1;
}

void Writer::Flush()
{
    size_t done = 0;
    while (m_file && done < m_out.size()) {
        DWORD chunk = static_cast<DWORD>(m_out.size() - done > (1u << 30) ? (1u << 30)
                                                                          : m_out.size() - done);
        DWORD wrote = 0;
        if (!WriteFile(static_cast<HANDLE>(m_file), m_out.data() + done, chunk, &wrote, nullptr)
            || wrote == 0) {
            m_failed = true;   // disk full or handle lost: the capture is incomplete
            break;
        }
        done += wrote;
    }
    m_out.clear();
}

void Writer::WriteDirect(const void* p, size_t n)
{
    if (!m_file || n == 0) return;
    const u8* b = static_cast<const u8*>(p);
    m_out.insert(m_out.end(), b, b + n);
}

void Writer::Begin(u32 op)
{
    if (m_depth + 1 >= kMaxDepth) { m_failed = true; return; }
    ++m_depth;
    m_op[m_depth] = op;
    m_scratch[m_depth].clear();
}

void Writer::End()
{
    if (m_depth < 0) return;
    Pad8();
    std::vector<u8>& s = m_scratch[m_depth];
    if (m_file) {
        const u32 head[2] = {m_op[m_depth], static_cast<u32>(s.size())};
        WriteDirect(head, sizeof(head));
        WriteDirect(s.data(), s.size());
        ++m_records;
        if (m_op[m_depth] < OP_MAX) ++m_opCounts[m_op[m_depth]];
    }
    s.clear();
    --m_depth;
    if (m_depth < 0 && m_out.size() >= kFlushBytes) Flush();
}

void Writer::Raw(const void* p, size_t n)
{
    if (m_depth < 0 || n == 0) return;
    const u8* b = static_cast<const u8*>(p);
    std::vector<u8>& s = m_scratch[m_depth];
    s.insert(s.end(), b, b + n);
}

void Writer::Pad8()
{
    if (m_depth < 0) return;
    std::vector<u8>& s = m_scratch[m_depth];
    while (s.size() & 7) s.push_back(0);
}
} // namespace raw::rec
