// The granular core: input buffer, window, scheduler, voice pool and level
// (PRD FR-GRAIN-001, FR-GRAIN-003, FR-GRAIN-007; ROADMAP T-0.3-02/03).

#include "support/TestSignals.h"

#include "core/dsp/grains/GrainModule.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;
using dsp::GrainBuffer;
using dsp::GrainModule;
using dsp::GrainWindow;

namespace
{
constexpr double kRate = 48000.0;

// Runs a mono signal through the grains in chunks of `chunk` samples.
std::vector<float> runGrains (GrainModule& grains, const std::vector<float>& input, GrainModule::Settings settings,
                              std::size_t chunk = 256)
{
    std::vector<float> left = input, right = input;
    for (std::size_t start = 0; start < input.size(); start += chunk)
    {
        const auto n = std::min (chunk, input.size() - start);
        float* channels[] = {left.data() + start, right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        grains.process (block, settings);
    }
    return left;
}

float largestStep (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    float largest = 0.0f;
    for (auto i = std::max<std::size_t> (from, 1); i < to; ++i)
        largest = std::max (largest, std::abs (s[i] - s[i - 1]));
    return largest;
}
} // namespace

TEST_CASE ("The grain buffer holds at least 4 s plus the longest grain", "[unit][grains]")
{
    for (const auto rate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        INFO ("sample rate " << rate);
        GrainModule grains;
        grains.prepare ({rate, 256, 2});
        CHECK (grains.getBuffer().getSize() >= static_cast<int> ((GrainModule::kBufferSeconds + 0.5) * rate));
    }
}

TEST_CASE ("The grain buffer returns what was written, across the wrap", "[unit][grains]")
{
    GrainBuffer buffer;
    buffer.prepare (2, 1000); // rounds up to 1024
    REQUIRE (buffer.getSize() == 1024);

    // Write 3000 samples in odd-sized pieces: a ramp on the left, its negative
    // on the right.
    std::vector<float> left (3000), right (3000);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = static_cast<float> (i);
        right[i] = -static_cast<float> (i);
    }
    for (std::size_t start = 0; start < left.size(); start += 77)
    {
        const auto n = static_cast<int> (std::min<std::size_t> (77, left.size() - start));
        const float* channels[] = {left.data() + start, right.data() + start};
        buffer.write (channels, 2, n);
    }

    CHECK (buffer.getWriteCount() == 3000);
    CHECK (buffer.getOldestIndex() == 3000 - 1024);
    for (auto index = buffer.getOldestIndex(); index < buffer.getWriteCount(); ++index)
    {
        REQUIRE (juce::exactlyEqual (buffer.read (0, index), static_cast<float> (index)));
        REQUIRE (juce::exactlyEqual (buffer.read (1, index), -static_cast<float> (index)));
    }
}

TEST_CASE ("A mono input fills both channels of the grain buffer", "[unit][grains]")
{
    GrainBuffer buffer;
    buffer.prepare (2, 64);
    const std::vector<float> mono{0.25f, 0.5f, 0.75f};
    const float* channels[] = {mono.data()};
    buffer.write (channels, 1, 3);
    CHECK (juce::exactlyEqual (buffer.read (0, 2), 0.75f));
    CHECK (juce::exactlyEqual (buffer.read (1, 2), 0.75f));
}

TEST_CASE ("The grain window is a Hann window", "[unit][grains]")
{
    const GrainWindow window;
    CHECK (juce::exactlyEqual (window.at (0.0f), 0.0f));
    CHECK_THAT (window.at (1.0f), WithinAbs (0.0, 1e-6));
    CHECK_THAT (window.at (0.5f), WithinAbs (1.0, 1e-6));

    for (const auto x : {0.1f, 0.2f, 0.33f, 0.47f})
    {
        INFO ("x = " << x);
        const auto expected = 0.5 - 0.5 * std::cos (kTwoPi * x);
        CHECK_THAT (window.at (x), WithinAbs (expected, 1e-6));
        CHECK_THAT (window.at (x), WithinAbs (window.at (1.0f - x), 1e-6)); // symmetric
    }

    // Two copies half a window apart sum to exactly 1.
    for (const auto x : {0.0f, 0.13f, 0.3f, 0.5f})
        CHECK_THAT (window.at (x) + window.at (x + 0.5f), WithinAbs (1.0, 1e-6));
}

TEST_CASE ("Grains start at exactly the requested density", "[unit][grains]")
{
    for (const auto density : {1.0, 7.3, 20.0, 100.0})
    {
        INFO ("density " << density);
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});

        // 10 s. The grains due before the buffer holds one full grain (size 10 ms)
        // are not started, so at most one is missing.
        runGrains (grains, std::vector<float> (static_cast<std::size_t> (10.0 * kRate), 0.1f), {0.01, density});
        const auto expected = std::floor (density * 10.0);
        CHECK (static_cast<double> (grains.getGrainsStarted()) <= expected);
        CHECK (static_cast<double> (grains.getGrainsStarted()) >= expected - 1.0);
        CHECK (grains.getGrainsSkipped() == 0);
    }
}

TEST_CASE ("Grains don't depend on the block size", "[unit][grains]")
{
    const auto input = whiteNoise (kRate, 2.0);
    const GrainModule::Settings settings{0.037, 31.0};

    GrainModule reference;
    reference.prepare ({kRate, 256, 2});
    const auto expected = runGrains (reference, input, settings, 256);

    for (const std::size_t chunk : {1u, 17u, 100u})
    {
        INFO ("chunk " << chunk);
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        const auto out = runGrains (grains, input, settings, chunk);

        float worst = 0.0f;
        for (std::size_t i = 0; i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - expected[i]));
        CHECK (juce::exactlyEqual (worst, 0.0f));
    }
}

TEST_CASE ("Evenly overlapping grains reproduce the input, delayed by one grain", "[unit][grains]")
{
    // 80 ms grains every 40 ms: each sample is covered by two Hann windows half a
    // window apart, which sum to 1, and both grains read the input one grain
    // length back. So after the first grains, the output is the input delayed by
    // 80 ms, at the same level.
    const GrainModule::Settings settings{0.08, 25.0};
    const auto length = static_cast<std::size_t> (0.08 * kRate);
    CHECK (juce::exactlyEqual (GrainModule::gainForOverlap (settings.density * settings.sizeSeconds), 1.0f));

    SECTION ("DC")
    {
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        const auto out = runGrains (grains, std::vector<float> (static_cast<std::size_t> (kRate), 0.5f), settings);
        for (std::size_t i = static_cast<std::size_t> (0.3 * kRate); i < out.size(); ++i)
            REQUIRE_THAT (out[i], WithinAbs (0.5, 2e-3));
    }

    SECTION ("A sine")
    {
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        const auto input = sine (440.0, kRate, 1.0);
        const auto out = runGrains (grains, input, settings);

        float worst = 0.0f;
        for (std::size_t i = static_cast<std::size_t> (0.3 * kRate); i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - input[i - length]));
        CHECK (worst < 2e-3f);
    }
}

TEST_CASE ("A single grain fades in and out without a click", "[unit][grains]")
{
    // One 100 ms grain per second on a constant input: the output rises from 0
    // and returns to 0 along the window, never in a step.
    GrainModule grains;
    grains.prepare ({kRate, 256, 2});
    const auto out = runGrains (grains, std::vector<float> (static_cast<std::size_t> (3.0 * kRate), 0.5f), {0.1, 1.0});

    REQUIRE (grains.getGrainsStarted() >= 2);

    // The steepest slope of a 0.5-high Hann window 4800 samples long is
    // 0.5·π/4800 per sample.
    const auto steepest = static_cast<float> (0.5 * juce::MathConstants<double>::pi / (0.1 * kRate));
    CHECK (largestStep (out, 0, out.size()) <= 1.01f * steepest);

    // Between grains, silence.
    CHECK (juce::exactlyEqual (out[static_cast<std::size_t> (1.5 * kRate)], 0.0f));
}

TEST_CASE ("Dense clouds are no louder than the input", "[unit][grains]")
{
    // With no spread every grain reads the same delayed input, so a dense cloud
    // is the input times the (scaled) sum of the windows.
    GrainModule grains;
    grains.prepare ({kRate, 256, 2});
    const auto out =
        runGrains (grains, std::vector<float> (static_cast<std::size_t> (2.0 * kRate), 0.5f), {0.5, 100.0});

    float peak = 0.0f;
    double sum = 0.0;
    const auto from = static_cast<std::size_t> (1.0 * kRate);
    for (auto i = from; i < out.size(); ++i)
    {
        peak = std::max (peak, std::abs (out[i]));
        sum += out[i];
    }
    const auto mean = sum / static_cast<double> (out.size() - from);

    CHECK_THAT (mean, WithinAbs (0.5, 0.01));
    CHECK (peak < 0.55f);
}

TEST_CASE ("The grain pool never exceeds 64 grains", "[unit][grains]")
{
    // Far beyond the parameter ranges: 1000 grains/s of 0.5 s would need 500
    // voices. New grains are skipped, sounding ones are never cut off.
    GrainModule grains;
    grains.prepare ({kRate, 256, 2});
    const auto input = sine (220.0, kRate, 1.5);

    const GrainModule::Settings settings{0.5, 1000.0};
    std::vector<float> left = input, right = input;
    int mostActive = 0;
    for (std::size_t start = 0; start + 256 <= input.size(); start += 256)
    {
        float* channels[] = {left.data() + start, right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, 256);
        grains.process (block, settings);
        mostActive = std::max (mostActive, grains.getActiveGrains());
    }

    CHECK (mostActive == GrainModule::kMaxGrains);
    CHECK (grains.getGrainsSkipped() > 0);
    CHECK (allFinite (left));
}

TEST_CASE ("Silence in, silence out; reset clears the grains and the buffer", "[unit][grains]")
{
    GrainModule grains;
    grains.prepare ({kRate, 256, 2});

    const auto silent = runGrains (grains, std::vector<float> (24000, 0.0f), {0.08, 20.0});
    CHECK (juce::exactlyEqual (energy (silent, 0, silent.size()), 0.0));

    runGrains (grains, sine (440.0, kRate, 0.5), {0.08, 20.0});
    REQUIRE (grains.getActiveGrains() > 0);

    grains.reset();
    CHECK (grains.getActiveGrains() == 0);
    CHECK (grains.getBuffer().getWriteCount() == 0);

    const auto after = runGrains (grains, std::vector<float> (24000, 0.0f), {0.08, 20.0});
    CHECK (juce::exactlyEqual (energy (after, 0, after.size()), 0.0));
}

TEST_CASE ("The grain module reads Size and Density from the parameters", "[unit][grains]")
{
    auto snapshot = params::ParamSnapshot::defaults();
    snapshot.set (params::ParamId::grainSize, 80.0f);
    snapshot.set (params::ParamId::grainDensity, 25.0f);

    GrainModule viaParams, direct;
    viaParams.prepare ({kRate, 256, 2});
    direct.prepare ({kRate, 256, 2});

    const auto input = sine (330.0, kRate, 0.5);
    const auto a = runModule (viaParams, input, snapshot);
    const auto b = runGrains (direct, input, {0.08, 25.0});

    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));
    CHECK (juce::exactlyEqual (worst, 0.0f));
}
