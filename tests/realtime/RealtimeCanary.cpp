// The real-time check has to be able to fail. This hidden test allocates inside
// a real-time section on purpose; CI runs it on its own and requires it to FAIL
// (allocation counted, or RealtimeSanitizer stopping the run). If it ever
// passes, the real-time tests are not checking anything.
//
//   ThunderTests "[canary]"

#include "support/Realtime.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace
{
// Written through a volatile pointer so the compiler can't remove the allocation.
std::vector<float>* volatile canarySink = nullptr;
} // namespace

TEST_CASE ("Real-time canary: an allocation inside a real-time section is caught", "[.][canary]")
{
    const auto report = thunder::test::runInRealtimeSection (
        []
        {
            canarySink = new std::vector<float> (1024, 1.0f);
            delete canarySink;
            canarySink = nullptr;
        });

    REQUIRE (thunder::test::allocationCountingAvailable());
    REQUIRE (report.allocations == 0); // must fail
}
