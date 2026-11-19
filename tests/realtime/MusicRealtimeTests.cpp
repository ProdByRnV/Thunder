// The pitch tracker and the interval maths run on the audio thread in Interval
// mode, so they must not allocate, lock or block (PRD NFR-PERF-001,
// FR-PITCH-010).

#include "support/Realtime.h"

#include "core/music/IntervalMapper.h"
#include "core/music/PitchTracker.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace thunder;
using namespace thunder::music;

TEST_CASE ("The pitch tracker and interval maths do not allocate or block", "[realtime][music][tracker]")
{
    for (const auto sampleRate : {48000.0, 96000.0})
    {
        PitchTracker tracker;
        tracker.prepare (sampleRate);

        // A rising tone, so notes change and every code path runs.
        std::vector<float> left (static_cast<std::size_t> (sampleRate)), right (left.size());
        double phase = 0.0;
        for (std::size_t i = 0; i < left.size(); ++i)
        {
            const auto t = static_cast<double> (i) / sampleRate;
            phase += 2.0 * juce::MathConstants<double>::pi * noteToFrequency (57.0 + 12.0 * t) / sampleRate;
            left[i] = 0.3f * static_cast<float> (std::sin (phase));
            right[i] = left[i];
        }

        int sum = 0;
        const auto report = test::runInRealtimeSection (
            [&]
            {
                for (std::size_t start = 0; start < left.size(); start += 256)
                {
                    const auto n = static_cast<int> (std::min<std::size_t> (256, left.size() - start));
                    const float* channels[] = {left.data() + start, right.data() + start};
                    tracker.process (channels, 2, n);

                    const auto& e = tracker.getEstimate();
                    const Key key{9, Scale::NaturalMinor};
                    if (e.voiced)
                        sum += IntervalMapper::shiftFor (key, e.note, PitchInterval::Third, PitchDirection::Up);
                    else
                        sum += IntervalMapper::bestFixedShift (key, PitchInterval::Third, PitchDirection::Up);
                }
                tracker.reset();
            });

        CHECK (sum != 0);
        CHECK (report.allocations == 0);
        CHECK (report.deallocations == 0);
    }
}
