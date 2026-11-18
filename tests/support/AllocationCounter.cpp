// Replacement global operator new/delete for the test runner only (never the
// plugin). Each call made inside a real-time section is counted, so tests can
// prove that audio-path code doesn't allocate or free memory.
//
// Sanitizer builds skip this file's contents: ASan replaces the allocator itself,
// and RTSan reports allocations directly.

#if !defined(THUNDER_SANITIZER_BUILD)

#include "support/Realtime.h"

#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace
{
void* allocate (std::size_t size)
{
    if (thunder::test::detail::isInRealtimeSection())
        thunder::test::detail::noteAllocation();

    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;

    throw std::bad_alloc();
}

void* allocateAligned (std::size_t size, std::align_val_t alignment)
{
    if (thunder::test::detail::isInRealtimeSection())
        thunder::test::detail::noteAllocation();

    const auto align = static_cast<std::size_t> (alignment);
    const auto bytes = size == 0 ? align : size;

#if defined(_MSC_VER)
    if (void* p = _aligned_malloc (bytes, align))
        return p;
#else
    void* p = nullptr;
    if (posix_memalign (&p, align < sizeof (void*) ? sizeof (void*) : align, bytes) == 0)
        return p;
#endif

    throw std::bad_alloc();
}

void release (void* p) noexcept
{
    if (p == nullptr)
        return;

    if (thunder::test::detail::isInRealtimeSection())
        thunder::test::detail::noteDeallocation();

    std::free (p);
}

void releaseAligned (void* p) noexcept
{
    if (p == nullptr)
        return;

    if (thunder::test::detail::isInRealtimeSection())
        thunder::test::detail::noteDeallocation();

#if defined(_MSC_VER)
    _aligned_free (p);
#else
    std::free (p);
#endif
}
} // namespace

// clang-format off
void* operator new (std::size_t size) { return allocate (size); }
void* operator new[] (std::size_t size) { return allocate (size); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return allocate (size); } catch (...) { return nullptr; }
}
void* operator new (std::size_t size, std::align_val_t al) { return allocateAligned (size, al); }
void* operator new[] (std::size_t size, std::align_val_t al) { return allocateAligned (size, al); }
void* operator new (std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept
{
    try { return allocateAligned (size, al); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t size, std::align_val_t al, const std::nothrow_t&) noexcept
{
    try { return allocateAligned (size, al); } catch (...) { return nullptr; }
}

void operator delete (void* p) noexcept { release (p); }
void operator delete[] (void* p) noexcept { release (p); }
void operator delete (void* p, std::size_t) noexcept { release (p); }
void operator delete[] (void* p, std::size_t) noexcept { release (p); }
void operator delete (void* p, const std::nothrow_t&) noexcept { release (p); }
void operator delete[] (void* p, const std::nothrow_t&) noexcept { release (p); }
void operator delete (void* p, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete (void* p, std::size_t, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::size_t, std::align_val_t) noexcept { releaseAligned (p); }
void operator delete (void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned (p); }
void operator delete[] (void* p, std::align_val_t, const std::nothrow_t&) noexcept { releaseAligned (p); }
// clang-format on

#endif
