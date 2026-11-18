#pragma once

#include <cstdint>
#include <functional>

namespace thunder::test
{

// Runs `work` as if it were on the audio thread and reports what it did that
// the audio thread must never do.
//
// - In normal builds, every C++ allocation and deallocation (operator new /
//   delete) made on this thread while `work` runs is counted; the caller
//   REQUIREs the count to be zero.
// - In the RealtimeSanitizer build, the section is also marked real-time for
//   RTSan, which stops the run with a stack trace on any malloc/free, lock or
//   blocking system call anywhere in the call tree — including in third-party
//   code.
// - In ASan/UBSan builds, counting is unavailable (the sanitizer owns the
//   allocator) and the count is always zero; those builds run the [unit] tests.
//
// Keep test assertions outside `work`: Catch2's own bookkeeping allocates.
struct RealtimeReport
{
    std::int64_t allocations = 0;
    std::int64_t deallocations = 0;
};

RealtimeReport runInRealtimeSection (const std::function<void()>& work);

// True when allocations are actually being counted in this build.
bool allocationCountingAvailable() noexcept;

namespace detail
{
// Used by the allocation counter (AllocationCounter.cpp).
bool isInRealtimeSection() noexcept;
void noteAllocation() noexcept;
void noteDeallocation() noexcept;
} // namespace detail

} // namespace thunder::test
