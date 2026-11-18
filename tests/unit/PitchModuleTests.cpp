// The pitch module: latency it reports, the shift it applies, smoothing and
// reset (PRD FR-PITCH-005/007, FR-CORE-004).

#include "support/TestSignals.h"

#include "core/dsp/pitch/PitchModule.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;
using dsp::PitchModule;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
ParamSnapshot withShift (int semitones, float cents = 0.0f)
{
    auto s = ParamSnapshot::defaults();
    s.set (ParamId::pitchSemitones, static_cast<float> (semitones));
    s.set (ParamId::pitchFine, cents);
    return s;
}
} // namespace

TEST_CASE ("The pitch module's reported latency is exactly its delay", "[unit][pitch]")
{
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        INFO ("sample rate " << sampleRate);
        PitchModule pitch;
        pitch.prepare ({sampleRate, 256, 2});

        // About 80 ms: under the 100 ms target (PRD NFR-PERF-004).
        CHECK_THAT (pitch.getLatencySamples() / sampleRate, WithinAbs (0.08, 0.002));

        const auto out = runModule (pitch, impulse (static_cast<std::size_t> (sampleRate * 0.5), 1000), withShift (0));
        CHECK (peakIndex (out) == 1000u + static_cast<std::size_t> (pitch.getLatencySamples()));
    }
}

TEST_CASE ("The pitch module shifts by semitones and cents", "[unit][pitch]")
{
    constexpr double sampleRate = 48000.0;

    struct Case
    {
        int semitones;
        float cents;
    };

    for (const auto c : {Case{12, 0.0f}, Case{7, 0.0f}, Case{-12, 0.0f}, Case{0, 50.0f}, Case{-5, -30.0f}})
    {
        INFO (c.semitones << " st " << c.cents << " cents");
        PitchModule pitch;
        pitch.prepare ({sampleRate, 256, 2});

        const auto out = runModule (pitch, sine (440.0, sampleRate, 1.5), withShift (c.semitones, c.cents));
        const auto expected = 440.0 * std::pow (2.0, (c.semitones + c.cents / 100.0) / 12.0);
        const auto measured = zeroCrossingFrequency (out, 24000, 72000, sampleRate);

        CHECK (std::abs (cents (measured, expected)) < 6.0);
        CHECK (allFinite (out));
    }
}

TEST_CASE ("Changing the shift glides instead of jumping", "[unit][pitch]")
{
    PitchModule pitch;
    pitch.prepare ({48000.0, 256, 2});

    const auto input = sine (440.0, 48000.0, 0.1);
    runModule (pitch, input, withShift (0));
    CHECK_THAT (pitch.getCurrentShift(), WithinAbs (0.0, 1e-6));

    // One chunk later the shift is on its way, not there yet.
    const std::vector<float> chunk (input.begin(), input.begin() + 256);
    runModule (pitch, chunk, withShift (12));
    CHECK (pitch.getCurrentShift() > 0.0f);
    CHECK (pitch.getCurrentShift() < 12.0f);

    // After the ramp (50 ms) it has arrived.
    runModule (pitch, input, withShift (12));
    CHECK_THAT (pitch.getCurrentShift(), WithinAbs (12.0, 1e-4));
}

TEST_CASE ("Reset clears what the pitch module was holding", "[unit][pitch]")
{
    PitchModule pitch;
    pitch.prepare ({48000.0, 256, 2});

    runModule (pitch, sine (440.0, 48000.0, 0.5), withShift (7));
    pitch.reset();

    const auto out = runModule (pitch, std::vector<float> (24000, 0.0f), withShift (7));
    CHECK (energy (out, 0, out.size()) < 1.0e-12);
}
