#include "support/Realtime.h"

#if defined(__has_feature)
#if __has_feature(realtime_sanitizer)
#define THUNDER_TEST_HAS_RTSAN 1
#endif
#endif

#if defined(THUNDER_TEST_HAS_RTSAN)
// Entry points of the RealtimeSanitizer runtime. They are what Clang itself calls
// around [[clang::nonblocking]] functions; declaring them here lets the tests
// mark a section real-time around arbitrary code.
extern "C" void __rtsan_realtime_enter();
extern "C" void __rtsan_realtime_exit();
#endif

namespace thunder::test
{
namespace
{
thread_local int realtimeDepth = 0;
thread_local std::int64_t allocationCount = 0;
thread_local std::int64_t deallocationCount = 0;
} // namespace

namespace detail
{
bool isInRealtimeSection() noexcept
{
    return realtimeDepth > 0;
}

void noteAllocation() noexcept
{
    ++allocationCount;
}

void noteDeallocation() noexcept
{
    ++deallocationCount;
}
} // namespace detail

bool allocationCountingAvailable() noexcept
{
#if defined(THUNDER_SANITIZER_BUILD)
    return false;
#else
    return true;
#endif
}

RealtimeReport runInRealtimeSection (const std::function<void()>& work)
{
    const auto allocationsBefore = allocationCount;
    const auto deallocationsBefore = deallocationCount;

    ++realtimeDepth;
#if defined(THUNDER_TEST_HAS_RTSAN)
    __rtsan_realtime_enter();
#endif

    work();

#if defined(THUNDER_TEST_HAS_RTSAN)
    __rtsan_realtime_exit();
#endif
    --realtimeDepth;

    return {allocationCount - allocationsBefore, deallocationCount - deallocationsBefore};
}

} // namespace thunder::test
