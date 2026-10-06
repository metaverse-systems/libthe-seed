#pragma once

// Counting replacements of the global allocation functions, and a scope
// object that reports how far the live heap grew while it existed.
//
// Include this header in a program and expand SEED_DEFINE_HEAP_COUNTER()
// exactly once at namespace scope in one translation unit. Programs that
// never expand the macro still compile; HeapGrowthScope then reports 0.

#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <new>

#if defined(_WIN32)
#include <malloc.h>
#elif defined(__linux__)
#include <malloc.h>
#endif

namespace seedtest::heap
{

inline std::atomic<std::size_t> live_bytes{0};
inline std::atomic<std::size_t> peak_bytes{0};
inline std::atomic<bool> counting_enabled{false};

inline void Added(std::size_t size)
{
    std::size_t now = live_bytes.fetch_add(size) + size;
    std::size_t peak = peak_bytes.load();
    while(now > peak && !peak_bytes.compare_exchange_weak(peak, now))
    {
    }
}

inline void Removed(std::size_t size)
{
    live_bytes.fetch_sub(size);
}

inline std::size_t UsableSize(void *pointer)
{
#if defined(_WIN32)
    return _msize(pointer);
#elif defined(__linux__)
    return malloc_usable_size(pointer);
#else
    (void)pointer;
    return 0;
#endif
}

inline void *Allocate(std::size_t size)
{
    void *pointer = std::malloc(size == 0 ? 1 : size);
    if(pointer == nullptr)
    {
        throw std::bad_alloc();
    }
    counting_enabled.store(true);
    Added(UsableSize(pointer));
    return pointer;
}

inline void *AllocateNoThrow(std::size_t size) noexcept
{
    void *pointer = std::malloc(size == 0 ? 1 : size);
    if(pointer != nullptr)
    {
        counting_enabled.store(true);
        Added(UsableSize(pointer));
    }
    return pointer;
}

inline std::size_t AlignedUsableSize(void *pointer, std::size_t alignment)
{
#if defined(_WIN32)
    return _aligned_msize(pointer, alignment, 0);
#else
    (void)alignment;
    return UsableSize(pointer);
#endif
}

inline void *AllocateAligned(std::size_t size, std::size_t alignment)
{
    if(alignment < sizeof(void *))
    {
        alignment = sizeof(void *);
    }
    std::size_t rounded = (size + alignment - 1) / alignment * alignment;
#if defined(_WIN32)
    void *pointer = _aligned_malloc(rounded == 0 ? alignment : rounded, alignment);
#else
    void *pointer = std::aligned_alloc(alignment, rounded == 0 ? alignment : rounded);
#endif
    if(pointer == nullptr)
    {
        throw std::bad_alloc();
    }
    counting_enabled.store(true);
    Added(AlignedUsableSize(pointer, alignment));
    return pointer;
}

inline void Release(void *pointer) noexcept
{
    if(pointer == nullptr)
    {
        return;
    }
    Removed(UsableSize(pointer));
    std::free(pointer);
}

inline void ReleaseAligned(void *pointer, std::size_t, std::size_t alignment) noexcept
{
    if(pointer == nullptr)
    {
        return;
    }
    if(alignment < sizeof(void *))
    {
        alignment = sizeof(void *);
    }
    Removed(AlignedUsableSize(pointer, alignment));
#if defined(_WIN32)
    _aligned_free(pointer);
#else
    std::free(pointer);
#endif
}

} // namespace seedtest::heap

#define SEED_DEFINE_HEAP_COUNTER()                                                                 \
    void *operator new(std::size_t size) { return seedtest::heap::Allocate(size); }                \
    void *operator new[](std::size_t size) { return seedtest::heap::Allocate(size); }              \
    void *operator new(std::size_t size, const std::nothrow_t &) noexcept                          \
    {                                                                                              \
        return seedtest::heap::AllocateNoThrow(size);                                              \
    }                                                                                              \
    void *operator new[](std::size_t size, const std::nothrow_t &) noexcept                        \
    {                                                                                              \
        return seedtest::heap::AllocateNoThrow(size);                                              \
    }                                                                                              \
    void *operator new(std::size_t size, std::align_val_t alignment)                               \
    {                                                                                              \
        return seedtest::heap::AllocateAligned(size, static_cast<std::size_t>(alignment));         \
    }                                                                                              \
    void *operator new[](std::size_t size, std::align_val_t alignment)                             \
    {                                                                                              \
        return seedtest::heap::AllocateAligned(size, static_cast<std::size_t>(alignment));         \
    }                                                                                              \
    void *operator new(std::size_t size, std::align_val_t alignment,                               \
                       const std::nothrow_t &) noexcept                                            \
    {                                                                                              \
        try                                                                                        \
        {                                                                                          \
            return seedtest::heap::AllocateAligned(size, static_cast<std::size_t>(alignment));     \
        }                                                                                          \
        catch(...)                                                                                 \
        {                                                                                          \
            return nullptr;                                                                        \
        }                                                                                          \
    }                                                                                              \
    void *operator new[](std::size_t size, std::align_val_t alignment,                             \
                         const std::nothrow_t &) noexcept                                          \
    {                                                                                              \
        try                                                                                        \
        {                                                                                          \
            return seedtest::heap::AllocateAligned(size, static_cast<std::size_t>(alignment));     \
        }                                                                                          \
        catch(...)                                                                                 \
        {                                                                                          \
            return nullptr;                                                                        \
        }                                                                                          \
    }                                                                                              \
    void operator delete(void *pointer) noexcept { seedtest::heap::Release(pointer); }            \
    void operator delete[](void *pointer) noexcept { seedtest::heap::Release(pointer); }          \
    void operator delete(void *pointer, std::size_t) noexcept { seedtest::heap::Release(pointer); } \
    void operator delete[](void *pointer, std::size_t) noexcept                                    \
    {                                                                                              \
        seedtest::heap::Release(pointer);                                                          \
    }                                                                                              \
    void operator delete(void *pointer, const std::nothrow_t &) noexcept                           \
    {                                                                                              \
        seedtest::heap::Release(pointer);                                                          \
    }                                                                                              \
    void operator delete[](void *pointer, const std::nothrow_t &) noexcept                         \
    {                                                                                              \
        seedtest::heap::Release(pointer);                                                          \
    }                                                                                              \
    void operator delete(void *pointer, std::align_val_t alignment) noexcept                       \
    {                                                                                              \
        seedtest::heap::ReleaseAligned(pointer, 0, static_cast<std::size_t>(alignment));           \
    }                                                                                              \
    void operator delete[](void *pointer, std::align_val_t alignment) noexcept                     \
    {                                                                                              \
        seedtest::heap::ReleaseAligned(pointer, 0, static_cast<std::size_t>(alignment));           \
    }                                                                                              \
    void operator delete(void *pointer, std::size_t size, std::align_val_t alignment) noexcept     \
    {                                                                                              \
        seedtest::heap::ReleaseAligned(pointer, size, static_cast<std::size_t>(alignment));        \
    }                                                                                              \
    void operator delete[](void *pointer, std::size_t size, std::align_val_t alignment) noexcept   \
    {                                                                                              \
        seedtest::heap::ReleaseAligned(pointer, size, static_cast<std::size_t>(alignment));        \
    }                                                                                              \
    void operator delete(void *pointer, std::align_val_t alignment,                                \
                         const std::nothrow_t &) noexcept                                          \
    {                                                                                              \
        seedtest::heap::ReleaseAligned(pointer, 0, static_cast<std::size_t>(alignment));           \
    }                                                                                              \
    void operator delete[](void *pointer, std::align_val_t alignment,                              \
                           const std::nothrow_t &) noexcept                                        \
    {                                                                                              \
        seedtest::heap::ReleaseAligned(pointer, 0, static_cast<std::size_t>(alignment));           \
    }

namespace seedtest::heap
{

// Measures the largest amount by which the live heap exceeded its size at
// construction. Not reentrant: one scope at a time per process.
class HeapGrowthScope
{
public:
    HeapGrowthScope() : baseline(live_bytes.load())
    {
        peak_bytes.store(this->baseline);
    }

    // True when the counting operators are linked into the program.
    static bool Active() { return counting_enabled.load(); }

    std::size_t PeakGrowth() const
    {
        std::size_t peak = peak_bytes.load();
        return peak > this->baseline ? peak - this->baseline : 0;
    }

private:
    std::size_t baseline;
};

} // namespace seedtest::heap
