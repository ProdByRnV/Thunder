// Measures the pitch tracker (ROADMAP T-0.2-09): how close chords and noise
// come to the 0.8 confidence a note needs, what fifths and octaves read as, and
// the CPU cost. Hidden from the normal test run; run it by hand:
//
//   ThunderTests "[measure]"

#include "support/TestSignals.h"

#include "core/music/PitchTracker.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>
#include <set>
#include <utility>
#include <vector>

using namespace thunder;
using namespace thunder::test;
using music::PitchTracker;

TEST_CASE ("Measure: pitch tracker confidence margins and CPU", "[.][measure]")
{
    constexpr double sr = 48000.0;

    const std::pair<const char*, std::vector<float>> cases[] = {
        {"C major triad", chord ({60, 64, 67}, sr, 1.0)},
        {"C major triad, rich", chord ({60, 64, 67}, sr, 1.0, 8)},
        {"C major triad, high", chord ({72, 76, 79}, sr, 1.0)},
        {"C major triad, low rich", chord ({48, 52, 55}, sr, 1.0, 8)},
        {"A minor triad", chord ({57, 60, 64}, sr, 1.0)},
        {"A minor triad, rich", chord ({57, 60, 64}, sr, 1.0, 8)},
        {"major third", chord ({60, 64}, sr, 1.0)},
        {"perfect fourth", chord ({67, 72}, sr, 1.0)},
        {"perfect fifth", chord ({60, 67}, sr, 1.0, 6)},
        {"octave", chord ({60, 72}, sr, 1.0, 6)},
        {"C major 7th", chord ({60, 64, 67, 71}, sr, 1.0, 4)},
        {"white noise", whiteNoise (sr, 2.0)},
        {"sine A4", steadyTone (69, sr, 1.0)},
        {"sawtooth E2", steadyTone (40, sr, 1.0, 20)},
    };

    std::printf ("\n%-24s %-15s %-9s %s\n", "input", "confidence", "voiced", "notes");
    for (const auto& [name, signal] : cases)
    {
        PitchTracker tracker;
        tracker.prepare (sr);

        float lowest = 2.0f, highest = 0.0f;
        int voiced = 0, analysed = 0;
        std::set<int> notes;
        for (std::size_t start = 0; start + 256 <= signal.size(); start += 256)
        {
            const float* channels[] = {signal.data() + start};
            tracker.process (channels, 1, 256);
            if (start < static_cast<std::size_t> (0.05 * sr)) // skip the start-up window
                continue;

            const auto& e = tracker.getEstimate();
            lowest = std::min (lowest, e.confidence);
            highest = std::max (highest, e.confidence);
            voiced += e.voiced ? 1 : 0;
            ++analysed;
            if (e.voiced)
                notes.insert (e.note);
        }

        std::printf ("%-24s %.3f..%.3f    %3d/%-5d", name, static_cast<double> (lowest), static_cast<double> (highest),
                     voiced, analysed);
        for (const auto note : notes)
            std::printf (" %d", note);
        std::printf ("\n");
    }

    // CPU: a rich tone gliding over an octave, so notes keep changing.
    PitchTracker tracker;
    tracker.prepare (sr);
    const auto signal = tone ([] (double t) { return 57.0 + 1.2 * t; }, sr, 10.0, 8);

    constexpr int repeats = 3;
    const auto start = juce::Time::getHighResolutionTicks();
    for (int r = 0; r < repeats; ++r)
        for (std::size_t i = 0; i + 256 <= signal.size(); i += 256)
        {
            const float* channels[] = {signal.data() + i};
            tracker.process (channels, 1, 256);
        }
    const auto seconds = juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - start);
    std::printf ("\nCPU: %.2f%% of one core at 48 kHz (mono)\n", 100.0 * seconds / (10.0 * repeats));
    std::printf ("Decision delay: %.1f ms\n", 1000.0 * tracker.getDecisionDelaySamples() / sr);

    SUCCEED();
}
