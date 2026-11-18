#pragma once
// Emit: scoped builder for one record. Holds the recorder lock for its
// lifetime. id() and ids() may emit DEFINE records first; the writer nests.
#include "rec_recorder.h"

namespace raw::rec
{
class Emit
{
public:
    Emit(Recorder& r, u32 op) : m_r(r), m_w(r.m_writer)
    {
        m_r.m_mutex.lock();
        m_w.Begin(op);
    }
    ~Emit()
    {
        m_w.End();
        m_r.m_mutex.unlock();
    }
    Emit(const Emit&) = delete;
    Emit& operator=(const Emit&) = delete;

    Emit& u(u32 v) { m_w.U32(v); return *this; }
    Emit& q(u64 v) { m_w.U64(v); return *this; }
    Emit& f(float v) { m_w.F32(v); return *this; }
    Emit& raw(const void* p, size_t n) { m_w.Raw(p, n); return *this; }
    Emit& blob(const void* p, size_t n) { m_w.Blob(p, n); return *this; }
    Emit& id(ID3D11DeviceChild* o, Kind k) { m_w.U64(m_r.Ref(o, k)); return *this; }

    template <class T> Emit& ids(u32 n, T* const* a, Kind k)
    {
        for (u32 i = 0; i < n; ++i) m_w.U64(a ? m_r.Ref(a[i], k) : 0);
        return *this;
    }
    Emit& us(u32 n, const u32* a, u32 fallback)
    {
        for (u32 i = 0; i < n; ++i) m_w.U32(a ? a[i] : fallback);
        return *this;
    }
    Emit& fs(u32 n, const float* a, float fallback)
    {
        for (u32 i = 0; i < n; ++i) m_w.F32(a ? a[i] : fallback);
        return *this;
    }

private:
    Recorder& m_r;
    Writer&   m_w;
};
} // namespace raw::rec
