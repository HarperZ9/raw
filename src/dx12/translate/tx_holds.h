// Internal references for one context's bindings (tx_ctx_bind.cpp). Original work, MIT.
//
// A deferred context counts them with the object's atomic internal count at every bind, as before: it
// records on its own thread while the immediate context may bind the same objects. The immediate
// context counts its bindings in a plain field each object carries (Child::imm), touched only on the
// immediate context's thread, which D3D11 requires the application to serialise. The object's first
// binding there takes one atomic internal reference, the pin, and the pin stays while the application
// holds the object; Drain gives it back once the object is unbound and the application has released
// it. A bind or an unbind on the immediate context so runs no locked instruction: each one made the CPU
// drain the write-combined bytes the game had just copied into upload memory.
#pragma once
#include "tx_common.h"

namespace raw::tx {

class BindHolds {
public:
    enum Mode { kAtomic, kPlain, kNone };   // None: a replay context, whose segment holds what it binds (tx_stream.h)
    explicit BindHolds(Mode m) : plain_(m == kPlain), none_(m == kNone) {}
    BindHolds(const BindHolds&) = delete;
    BindHolds& operator=(const BindHolds&) = delete;

    template <class T>
    void Assign(T*& slot, T* v)
    {
        if (slot == v) return;
        if (none_) { slot = v; return; }
        if (!plain_) { IntAssign(slot, v); return; }
        if (v && v->imm.n++ == 0 && !v->imm.pinned) PinNew(v);
        if (slot) --slot->imm.n;
        slot = v;
    }

    // A host's reference on the immediate context's thread (raw::tx::HoldBuffer, HoldView): counted with
    // the bindings, so it keeps the object alive the same way. Only on the immediate context.
    template <class T>
    void Add(T* v)
    {
        if (v->imm.n++ == 0 && !v->imm.pinned) PinNew(v);
    }
    template <class T>
    void Drop(T* v) { --v->imm.n; }
    bool Plain() const { return plain_; }

    // Gives back the pins of objects that are unbound and released by the application, visiting at
    // most `budget` pins from where the last call stopped. An object can die here.
    void Drain(size_t budget)
    {
        for (size_t k = 0; k < budget && !pins_.empty(); ++k) {
            if (cursor_ >= pins_.size()) cursor_ = 0;
            Pin& p = pins_[cursor_];
            if (p.h->n != 0 || !p.gone(p.obj)) { ++cursor_; continue; }
            const Pin dead = p;
            p = pins_.back();
            pins_.pop_back();
            dead.h->pinned = false;
            dead.release(dead.obj);
        }
    }

    // Every pin given back (the context is going away; its bindings are already reset).
    void ReleaseAll()
    {
        std::vector<Pin> all;
        all.swap(pins_);
        cursor_ = 0;
        for (const Pin& p : all) { p.h->n = 0; p.h->pinned = false; p.release(p.obj); }
    }

    size_t Pinned() const { return pins_.size(); }

private:
    struct Pin {
        ImmHold* h;
        void* obj;
        bool (*gone)(void*);      // the application holds no reference
        void (*release)(void*);   // gives the pin back
    };

    template <class T>
    void PinNew(T* v)
    {
        v->imm.pinned = true;
        v->IntAddRef();
        pins_.push_back(Pin{&v->imm, v,
                            [](void* p) { return static_cast<T*>(p)->rc.load(std::memory_order_acquire) == 0; },
                            [](void* p) { static_cast<T*>(p)->IntRelease(); }});
    }

    bool plain_, none_;
    std::vector<Pin> pins_;
    size_t cursor_ = 0;
};

}  // namespace raw::tx
