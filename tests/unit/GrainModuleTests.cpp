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

TEST_CASE ("The grain module reads all its parameters", "[unit][grains]")
{
    using params::ParamId;

    const auto compare = [] (const params::ParamSnapshot& snapshot, const GrainModule::Settings& settings)
    {
        GrainModule viaParams, direct;
        viaParams.prepare ({kRate, 256, 2});
        direct.prepare ({kRate, 256, 2});

        const auto input = whiteNoise (kRate, 1.0);
        const auto a = runModule (viaParams, input, snapshot);
        const auto b = runGrains (direct, input, settings);

        float worst = 0.0f;
        for (std::size_t i = 0; i < a.size(); ++i)
            worst = std::max (worst, std::abs (a[i] - b[i]));
        CHECK (juce::exactlyEqual (worst, 0.0f));
    };

    SECTION ("Defaults (PRD §9)")
    {
        compare (params::ParamSnapshot::defaults(), {0.08, 20.0, 0.5f, 0.25f, 0.0f, 0.4f, 0.0f});
    }

    SECTION ("Every grain parameter changed")
    {
        auto snapshot = params::ParamSnapshot::defaults();
        snapshot.set (ParamId::grainSize, 120.0f);
        snapshot.set (ParamId::grainDensity, 30.0f);
        snapshot.set (ParamId::grainAmount, 70.0f);
        snapshot.set (ParamId::grainPosSpread, 10.0f);
        snapshot.set (ParamId::grainPitchSpread, 3.0f);
        snapshot.set (ParamId::grainPanSpread, 60.0f);
        snapshot.set (ParamId::grainReverse, 20.0f);
        compare (snapshot, {0.12, 30.0, 0.7f, 0.1f, 3.0f, 0.6f, 0.2f});
    }
}

// ---- Subphase 2D: Amount, spreads, reverse, interpolation, level
// (PRD FR-GRAIN-002, FR-GRAIN-008, DEC-040; ROADMAP T-0.3-04/05/07)

namespace
{
// A grain as it was when it started, and where its output begins.
struct StartedGrain
{
    std::int64_t start = 0; // absolute output sample of its first sample
    GrainModule::Grain grain;
};

// Runs a mono signal through the grains in chunks of `chunk` samples and
// records every grain that starts. Grains must be longer than a chunk to be
// seen (one that starts and ends inside a chunk never shows up as active).
std::vector<float> runAndRecord (GrainModule& grains, const std::vector<float>& input,
                                 const GrainModule::Settings& settings, std::vector<StartedGrain>& started,
                                 std::size_t chunk = 256, std::vector<float>* right = nullptr)
{
    std::vector<float> left = input, rightChannel = input;
    for (std::size_t start = 0; start < input.size(); start += chunk)
    {
        const auto n = std::min (chunk, input.size() - start);
        float* channels[] = {left.data() + start, rightChannel.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        grains.process (block, settings);

        // A grain that started in this chunk has played at most n samples; one
        // from an earlier chunk has played more.
        for (const auto& g : grains.getGrains())
            if (g.active && g.elapsed <= static_cast<int> (n))
                started.push_back ({static_cast<std::int64_t> (start + n) - g.elapsed, g});
    }
    if (right != nullptr)
        *right = std::move (rightChannel);
    return left;
}

double levelDb (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    return 10.0 * std::log10 (energy (s, from, to) / static_cast<double> (to - from) + 1e-30);
}
} // namespace

TEST_CASE ("Amount blends the input and the grains, equal-power", "[unit][grains]")
{
    const auto input = whiteNoise (kRate, 1.0);
    const GrainModule::Settings cloudOnly{0.08, 20.0, 1.0f, 0.25f, 2.0f, 0.4f, 0.2f};

    GrainModule reference;
    reference.prepare ({kRate, 256, 2});
    const auto cloud = runGrains (reference, input, cloudOnly);

    SECTION ("0% is the input, untouched")
    {
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        auto settings = cloudOnly;
        settings.amount = 0.0f;
        const auto out = runGrains (grains, input, settings);
        for (std::size_t i = 0; i < out.size(); ++i)
            REQUIRE (juce::exactlyEqual (out[i], input[i]));
    }

    SECTION ("50% is cos·input + sin·grains at π/4")
    {
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        auto settings = cloudOnly;
        settings.amount = 0.5f;
        const auto out = runGrains (grains, input, settings);

        const auto g = std::sqrt (0.5f);
        float worst = 0.0f;
        for (std::size_t i = 0; i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - (g * input[i] + g * cloud[i])));
        CHECK (worst < 1e-6f);
    }
}

TEST_CASE ("Changing Amount during playback is smooth", "[unit][grains]")
{
    // A sine through evenly overlapping grains (the cloud is the same sine,
    // delayed); Amount jumps 0 → 100% → 0 between chunks.
    GrainModule grains;
    grains.prepare ({kRate, 256, 2});
    const auto input = sine (440.0, kRate, 1.5);
    std::vector<float> left = input, right = input;

    for (std::size_t start = 0; start + 256 <= input.size(); start += 256)
    {
        const auto t = static_cast<double> (start) / kRate;
        const auto amount = (t >= 0.5 && t < 1.0) ? 1.0f : 0.0f;
        float* channels[] = {left.data() + start, right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, 256);
        grains.process (block, {0.08, 25.0, amount});
    }

    // The steepest a 0.5 sine moves is 0.5·2π·440/48000 ≈ 0.029 per sample; a
    // click would be a far bigger step.
    CHECK (largestStep (left, 1, left.size()) < 0.035f);
}

TEST_CASE ("Position Spread reaches back up to spread squared times 4 s, never into the future", "[unit][grains]")
{
    CHECK_THAT (GrainModule::positionSpreadSeconds (0.0f), WithinAbs (0.0, 1e-12));
    CHECK_THAT (GrainModule::positionSpreadSeconds (0.25f), WithinAbs (0.25, 1e-9));
    CHECK_THAT (GrainModule::positionSpreadSeconds (0.5f), WithinAbs (1.0, 1e-9));
    CHECK_THAT (GrainModule::positionSpreadSeconds (1.0f), WithinAbs (4.0, 1e-9));

    // One click at 1 s in silence. A grain reads one grain length (L) behind
    // "now" plus its random offset, so the click comes back from 1 s + L
    // onwards, up to the spread later, and never before.
    constexpr double size = 0.1;
    const auto impulseAt = static_cast<std::size_t> (kRate);
    const auto input = impulse (static_cast<std::size_t> (7.0 * kRate), impulseAt);
    const auto length = static_cast<std::size_t> (size * kRate);

    for (const auto spread : {0.0f, 0.25f, 1.0f})
    {
        INFO ("spread " << spread);
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        const auto out = runGrains (grains, input, {size, 100.0, 1.0f, spread});

        std::size_t first = out.size(), last = 0;
        for (std::size_t i = 0; i < out.size(); ++i)
            if (!juce::exactlyEqual (out[i], 0.0f))
            {
                first = std::min (first, i);
                last = i;
            }

        REQUIRE (last > 0);
        CHECK (first >= impulseAt + length);
        const auto reach = static_cast<std::size_t> (GrainModule::positionSpreadSeconds (spread) * kRate);
        CHECK (last <= impulseAt + length + reach);
        if (spread >= 1.0f)
            CHECK (last > impulseAt + static_cast<std::size_t> (3.0 * kRate)); // it really reaches back
        if (spread <= 0.0f)
            CHECK (last == impulseAt + length); // no spread: exactly one grain length later
    }
}

TEST_CASE ("Pitch Spread plays each grain at its own pitch, within plus or minus the spread", "[unit][grains]")
{
    // 200 ms grains, two per second (they never overlap), on a 500 Hz sine.
    // Each grain's frequency must be 500 Hz times its playback rate.
    GrainModule grains;
    grains.prepare ({kRate, 256, 2});
    const auto input = sine (500.0, kRate, 12.0);
    std::vector<StartedGrain> started;
    const auto out = runAndRecord (grains, input, {0.2, 2.0, 1.0f, 0.0f, 12.0f}, started);

    REQUIRE (started.size() >= 20);
    double lowest = 10.0, highest = 0.0;
    for (const auto& s : started)
    {
        const auto rate = std::abs (s.grain.step);
        lowest = std::min (lowest, rate);
        highest = std::max (highest, rate);
        CHECK (rate >= 0.5 - 1e-9);
        CHECK (rate <= 2.0 + 1e-9);
        CHECK (s.grain.interpolate == !juce::exactlyEqual (rate, 1.0));

        const auto from = static_cast<std::size_t> (s.start) + static_cast<std::size_t> (s.grain.length / 4);
        const auto to = static_cast<std::size_t> (s.start) + static_cast<std::size_t> (3 * s.grain.length / 4);
        if (to > out.size())
            continue;
        INFO ("rate " << rate);
        CHECK (std::abs (cents (zeroCrossingFrequency (out, from, to, kRate), 500.0 * rate)) < 5.0);
    }

    // The rates really spread over the range.
    CHECK (lowest < 0.75);
    CHECK (highest > 1.4);
}

TEST_CASE ("Reverse Probability plays grains backwards", "[unit][grains]")
{
    SECTION ("100%: every grain is the input read backwards")
    {
        // A slow ramp, so a backwards read is easy to see. 100 ms grains, two
        // per second, no spread: grain output k is window(k) × input at
        // (start − 1 − k), the same stretch a forward grain reads, reversed.
        std::vector<float> ramp (static_cast<std::size_t> (4.0 * kRate));
        for (std::size_t i = 0; i < ramp.size(); ++i)
            ramp[i] = static_cast<float> (i) / static_cast<float> (ramp.size());

        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        std::vector<StartedGrain> started;
        const auto out = runAndRecord (grains, ramp, {0.1, 2.0, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}, started);

        REQUIRE (started.size() >= 6);
        CHECK (grains.getGrainsReversed() == grains.getGrainsStarted());

        const GrainWindow window;
        for (const auto& s : started)
        {
            const auto length = s.grain.length;
            const auto step = static_cast<float> (GrainWindow::kSegments) / static_cast<float> (length);
            for (int k = 0; k < length; ++k)
            {
                const auto i = static_cast<std::size_t> (s.start + k);
                if (i >= out.size())
                    break;
                const auto expected =
                    window.atSegment (static_cast<float> (k) * step) * ramp[static_cast<std::size_t> (s.start - 1 - k)];
                REQUIRE_THAT (out[i], WithinAbs (expected, 1e-6));
            }
        }
    }

    SECTION ("50%: about half the grains")
    {
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        runGrains (grains, whiteNoise (kRate, 30.0), {0.05, 100.0, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f});
        const auto share =
            static_cast<double> (grains.getGrainsReversed()) / static_cast<double> (grains.getGrainsStarted());
        CHECK (grains.getGrainsStarted() > 2900);
        CHECK (share > 0.45);
        CHECK (share < 0.55);
    }
}

TEST_CASE ("Pan Spread places grains across the stereo field, equal-power", "[unit][grains]")
{
    const auto input = whiteNoise (kRate, 6.0);

    SECTION ("No spread: both sides identical")
    {
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        std::vector<float> right;
        std::vector<StartedGrain> started;
        const auto left = runAndRecord (grains, input, {0.08, 20.0, 1.0f, 0.25f}, started, 256, &right);
        for (std::size_t i = 0; i < left.size(); ++i)
            REQUIRE (juce::exactlyEqual (left[i], right[i]));
    }

    SECTION ("Full spread")
    {
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        std::vector<float> right;
        std::vector<StartedGrain> started;
        const auto left = runAndRecord (grains, input, {0.08, 20.0, 1.0f, 0.25f, 0.0f, 1.0f}, started, 256, &right);

        REQUIRE (started.size() > 50);
        float hardest = 1.0f;
        for (const auto& s : started)
        {
            const auto l = s.grain.gains[0], r = s.grain.gains[1];
            CHECK_THAT (l * l + r * r, WithinAbs (2.0, 1e-5)); // equal power
            hardest = std::min (hardest, std::min (l, r));
        }
        CHECK (hardest < 0.3f); // some grains are nearly hard-panned

        // Over many grains, neither side is louder.
        const auto from = static_cast<std::size_t> (kRate);
        CHECK (std::abs (levelDb (left, from, left.size()) - levelDb (right, from, right.size())) < 1.5);
    }
}

TEST_CASE ("Dense grain clouds stay at the input's level, whatever the spreads", "[unit][grains]")
{
    // The correlation between grains is measured, so noise and a melody both
    // come out at about the input's level (PRD DEC-040; the measurements are in
    // tests/measure/GrainMeasure.cpp).
    const auto noise = whiteNoise (kRate, 6.0);
    const auto melody =
        tone ([] (double t) { return 57.0 + 2.0 * std::floor (std::fmod (t * 4.0, 6.0)); }, kRate, 6.0, 8);

    struct Case
    {
        const char* name;
        GrainModule::Settings settings;
    };
    const Case cases[] = {
        {"no spread", {0.5, 100.0}},
        {"position 2%", {0.5, 100.0, 1.0f, 0.02f}},
        {"position 100%", {0.5, 100.0, 1.0f, 1.0f}},
        {"pitch 0.1", {0.5, 100.0, 1.0f, 0.0f, 0.1f}},
        {"pitch 12", {0.5, 100.0, 1.0f, 0.0f, 12.0f}},
        {"reverse 50%", {0.5, 100.0, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f}},
        {"reverse 100%", {0.5, 100.0, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
        {"everything", {0.1, 50.0, 1.0f, 1.0f, 12.0f, 1.0f, 0.5f}},
    };

    const auto from = static_cast<std::size_t> (3.0 * kRate);
    for (const auto& c : cases)
    {
        for (const auto* signal : {&noise, &melody})
        {
            INFO (c.name << (signal == &noise ? ", noise" : ", melody"));
            GrainModule grains;
            grains.prepare ({kRate, 256, 2});
            const auto out = runGrains (grains, *signal, c.settings);
            const auto difference = levelDb (out, from, out.size()) - levelDb (*signal, from, signal->size());
            CHECK (std::abs (difference) < 2.0);
            CHECK (allFinite (out));
        }
    }

    SECTION ("Identical grains measure as correlated, scattered noise as not")
    {
        GrainModule coherent, scattered;
        coherent.prepare ({kRate, 256, 2});
        scattered.prepare ({kRate, 256, 2});
        runGrains (coherent, noise, {0.5, 100.0});
        runGrains (scattered, noise, {0.5, 100.0, 1.0f, 1.0f});
        CHECK (coherent.getCorrelation() > 0.95);
        CHECK (scattered.getCorrelation() < 0.1);
    }
}

TEST_CASE ("Sparse grain clouds are never boosted", "[unit][grains]")
{
    // Fewer than ~2.7 grains overlapping: gaps are part of the sound, so the
    // cloud may be quieter than the input but never louder.
    CHECK (juce::exactlyEqual (GrainModule::gainForOverlap (1.0, 0.0), 1.0f));
    CHECK (juce::exactlyEqual (GrainModule::gainForOverlap (2.0, 1.0), 1.0f));
    CHECK (juce::exactlyEqual (GrainModule::gainForOverlap (2.6, 0.0), 1.0f));
    CHECK_THAT (GrainModule::gainForOverlap (50.0, 1.0), WithinAbs (0.04, 1e-6));
    CHECK_THAT (GrainModule::gainForOverlap (50.0, 0.0), WithinAbs (1.0 / std::sqrt (18.75), 1e-6));

    for (const auto size : {0.01, 0.08, 0.2})
    {
        INFO ("size " << size);
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        const auto input = whiteNoise (kRate, 4.0);
        const auto out = runGrains (grains, input, {size, 10.0, 1.0f, 0.25f});
        CHECK (levelDb (out, 48000, out.size()) < levelDb (input, 48000, input.size()) + 0.1);
    }
}

TEST_CASE ("Grains only read input that is written and not yet overwritten", "[unit][grains]")
{
    // Every read of every grain, worked out from its start: each sample it
    // reads (with the cubic's neighbours) must be in the buffer when it plays.
    // Low sample rate and long runs, so the buffer wraps several times, with
    // the settings that reach furthest: long grains, +12 semitones, full
    // position spread, half reversed.
    constexpr double rate = 8000.0;

    for (const std::size_t chunk : {256u, 100u})
    {
        for (const auto size : {0.5, 0.06})
        {
            INFO ("chunk " << chunk << ", size " << size);
            GrainModule grains;
            grains.prepare ({rate, 256, 2});
            const auto bufferSize = static_cast<std::int64_t> (grains.getBuffer().getSize());
            const auto c = static_cast<std::int64_t> (chunk);

            std::vector<StartedGrain> started;
            runAndRecord (grains, whiteNoise (rate, 25.0), {size, 100.0, 1.0f, 1.0f, 12.0f, 1.0f, 0.5f}, started,
                          chunk);
            REQUIRE (started.size() > 1000);

            int bad = 0;
            for (const auto& s : started)
            {
                const auto& g = s.grain;
                for (int k = 0; k < g.length; ++k)
                {
                    const auto t = s.start + k;
                    const auto written = (t / c + 1) * c; // the chunk holding t is written first
                    const auto oldest = std::max<std::int64_t> (0, written - bufferSize);

                    const auto position = std::max (0.0, g.firstPosition + static_cast<double> (k) * g.step);
                    const auto index = g.base + static_cast<std::int64_t> (position);
                    const auto lowest = index - (g.interpolate ? 1 : 0);
                    const auto highest = index + (g.interpolate ? 2 : 0);
                    if (lowest < oldest || highest >= written)
                        ++bad;
                }
            }
            CHECK (bad == 0);
        }
    }
}

TEST_CASE ("With every spread on, grains still don't depend on the block size and repeat after reset", "[unit][grains]")
{
    const auto input = whiteNoise (kRate, 3.0);
    const GrainModule::Settings settings{0.06, 40.0, 0.6f, 0.5f, 7.0f, 0.8f, 0.3f};

    GrainModule reference;
    reference.prepare ({kRate, 256, 2});
    const auto expected = runGrains (reference, input, settings, 256);

    for (const std::size_t chunk : {1u, 17u, 100u, 256u})
    {
        INFO ("chunk " << chunk);
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        if (chunk == 256u)
        {
            runGrains (grains, input, settings, chunk); // a first run, then reset
            grains.reset();
        }
        const auto out = runGrains (grains, input, settings, chunk);

        float worst = 0.0f;
        for (std::size_t i = 0; i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - expected[i]));
        CHECK (juce::exactlyEqual (worst, 0.0f));
    }
}

TEST_CASE ("A block longer than the module was prepared for is handled in pieces", "[unit][grains]")
{
    const auto input = whiteNoise (kRate, 1.0);
    const GrainModule::Settings settings{0.06, 40.0, 0.6f, 0.5f, 7.0f, 0.8f, 0.3f};

    GrainModule small, reference;
    small.prepare ({kRate, 64, 2});
    reference.prepare ({kRate, 64, 2});
    const auto a = runGrains (small, input, settings, 1000);
    const auto b = runGrains (reference, input, settings, 64);

    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));
    CHECK (juce::exactlyEqual (worst, 0.0f));
}

TEST_CASE ("The grains' tail covers how far back they can reach", "[unit][grains]")
{
    GrainModule grains;
    grains.prepare ({kRate, 256, 2});
    CHECK_THAT (grains.getTailSeconds(), WithinAbs (5.5, 1e-9));

    // Input stops at 2 s; with every spread at its maximum the output is silent
    // again within the tail.
    auto input = whiteNoise (kRate, 9.0);
    std::fill (input.begin() + static_cast<std::ptrdiff_t> (2.0 * kRate), input.end(), 0.0f);
    const auto out = runGrains (grains, input, {0.5, 100.0, 1.0f, 1.0f, 12.0f, 1.0f, 0.5f});
    const auto tailEnd = static_cast<std::size_t> ((2.0 + grains.getTailSeconds()) * kRate);
    CHECK (juce::exactlyEqual (energy (out, tailEnd, out.size()), 0.0));
    CHECK (energy (out, static_cast<std::size_t> (5.0 * kRate), tailEnd) > 0.0); // and it really rings on
}
