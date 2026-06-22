#pragma once
#include "raw/arena.hpp"
#include <cstddef>
#include <new>
#include <type_traits>
namespace raw {
// A std::allocator that draws bytes from a raw::Arena (arena_ != null) or the heap
// (arena_ == null, the default — so existing containers are byte-for-byte unchanged).
// Over-budget arena allocation is fail-closed: it throws std::bad_alloc.
template<class T>
struct ArenaAllocator {
    using value_type = T;
    using propagate_on_container_copy_assignment = std::true_type;
    using propagate_on_container_move_assignment = std::true_type;
    using propagate_on_container_swap            = std::true_type;
    using is_always_equal                        = std::false_type;

    Arena* arena_{nullptr};
    ArenaAllocator() noexcept = default;                          // heap mode
    explicit ArenaAllocator(Arena* a) noexcept : arena_(a) {}     // arena mode
    template<class U> ArenaAllocator(const ArenaAllocator<U>& o) noexcept : arena_(o.arena_) {}

    T* allocate(std::size_t n){
        void* p = arena_ ? arena_->allocate(n * sizeof(T), alignof(T))
                         : ::operator new(n * sizeof(T));
        if (!p) throw std::bad_alloc();      // arena refusal (over budget) => fail-closed
        return static_cast<T*>(p);
    }
    void deallocate(T* p, std::size_t) noexcept {
        if (!arena_) ::operator delete(p);   // heap frees; arena is bump (free is a no-op)
    }
    template<class U> bool operator==(const ArenaAllocator<U>& o) const noexcept { return arena_ == o.arena_; }
    template<class U> bool operator!=(const ArenaAllocator<U>& o) const noexcept { return arena_ != o.arena_; }
};
}
