// Gain staging: the loudness meter, Auto Gain and the safety limiter
// (PRD FR-GAIN-001/003, DEC-043; ROADMAP T-0.4-04/05).

#include "support/TestSignals.h"

#include "core/dsp/gain/AutoGain.h"
#include "core/dsp/gain/LoudnessMeter.h"
#include "core/dsp/gain/SafetyLimiter.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;
using dsp::AutoGain;
using dsp::LoudnessMeter;
using dsp::SafetyLimiter;

namespace
{
constexpr double kRate = 48000.0;

// Runs Auto Gain over a dry signal and a layer that is the dry signal times
// `layerScale`; returns the gain (dB) after every 256-sample chunk.
std::vector<double> runAutoGain (AutoGain& autoGain, const std::vector<float>& dry, float layerScale,
                                 bool enabled = true)
{
    std::vector<double> gains;
    std::vector<float> layerL (256), layerR (256);
    for (std::size_t start = 0; start + 256 <= dry.size(); start += 256)
    {
        for (std::size_t i = 0; i < 256; ++i)
            layerL[i] = layerR[i] = dry[start + i] * layerScale;
        float* layer[] = {layerL.data(), layerR.data()};
        const float* dryChannels[] = {dry.data() + start, dry.data() + start};
        autoGain.process (layer, dryChannels, 2, 256, enabled);
        gains.push_back (autoGain.getGainDb());
    }
    return gains;
}

// Runs a stereo signal (the same on both sides) through the limiter in
// 256-sample chunks.
std::vector<float> runLimiter (SafetyLimiter& limiter, const std::vector<float>& input, bool enabled = true)
{
    std::vector<float> left = input, right = input;
    for (std::size_t start = 0; start < input.size(); start += 256)
    {
        const auto n = static_cast<int> (std::min<std::size_t> (256, input.size() - start));
        float* channels[] = {left.data() + start, right.data() + start};
        limiter.process (channels, 2, n, enabled);
    }
    return left;
}

float peakOf (const std::vector<float>& s)
{
    float peak = 0.0f;
    for (const auto x : s)
        peak = std::max (peak, std::abs (x));
    return peak;
}
} // namespace

TEST_CASE ("The K-weighting filter matches ITU-R BS.1770 at 48 kHz", "[unit][gain]")
{
    const auto stages = LoudnessMeter::kWeighting (48000.0);
    CHECK_THAT (stages[0].b0, WithinAbs (1.53512485958697, 1e-9));
    CHECK_THAT (stages[0].b1, WithinAbs (-2.69169618940638, 1e-9));
    CHECK_THAT (stages[0].b2, WithinAbs (1.19839281085285, 1e-9));
    CHECK_THAT (stages[0].a1, WithinAbs (-1.69065929318241, 1e-9));
    CHECK_THAT (stages[0].a2, WithinAbs (0.73248077421585, 1e-9));
    CHECK_THAT (stages[1].a1, WithinAbs (-1.99004745483398, 1e-9));
    CHECK_THAT (stages[1].a2, WithinAbs (0.99007225036621, 1e-9));
}

TEST_CASE ("A full-scale 997 Hz sine in one channel reads -3.01 LKFS", "[unit][gain]")
{
    // BS.1770's own calibration point (loudness = −0.691 + 10·log10(mean square)).
    LoudnessMeter meter;
    meter.prepare (kRate);
    const auto s = sine (997.0, kRate, 3.0, 1.0f);
    for (const auto x : s)
        meter.push (&x, 1);
    CHECK_THAT (-0.691 + meter.getDecibels(), WithinAbs (-3.01, 0.05));
}

TEST_CASE ("Auto Gain holds the layer 6 dB below the dry signal", "[unit][gain]")
{
    const auto dry = whiteNoise (kRate, 12.0);
    for (const auto layerDb : {-12.0f, 0.0f, 9.0f})
    {
        INFO ("layer at " << layerDb << " dB");
        AutoGain autoGain;
        autoGain.prepare (kRate);
        const auto gains = runAutoGain (autoGain, dry, juce::Decibels::decibelsToGain (layerDb));
        CHECK_THAT (gains.back(), WithinAbs (AutoGain::kOffsetDb - static_cast<double> (layerDb), 0.3));
    }
}

TEST_CASE ("Auto Gain stays within plus or minus 18 dB", "[unit][gain]")
{
    const auto dry = whiteNoise (kRate, 12.0);
    AutoGain quiet, loud;
    quiet.prepare (kRate);
    loud.prepare (kRate);
    CHECK_THAT (runAutoGain (quiet, dry, juce::Decibels::decibelsToGain (-40.0f)).back(), WithinAbs (18.0, 0.05));
    CHECK_THAT (runAutoGain (loud, dry, juce::Decibels::decibelsToGain (30.0f)).back(), WithinAbs (-18.0, 0.05));
}

TEST_CASE ("Auto Gain holds through silence and doesn't pump on a melody", "[unit][gain]")
{
    // A melody with a gap: four notes per second, every fourth note silent.
    auto melody = tone ([] (double t) { return 57.0 + 2.0 * std::floor (std::fmod (t * 4.0, 6.0)); }, kRate, 14.0, 6);
    for (std::size_t i = 0; i < melody.size(); ++i)
        if (static_cast<int> (static_cast<double> (i) / kRate * 4.0) % 4 == 3)
            melody[i] = 0.0f;
    // Then four seconds of silence.
    std::fill (melody.begin() + static_cast<std::ptrdiff_t> (10.0 * kRate), melody.end(), 0.0f);

    AutoGain autoGain;
    autoGain.prepare (kRate);
    const auto gains = runAutoGain (autoGain, melody, 0.25f);
    const auto chunkAt = [] (double seconds) { return static_cast<std::size_t> (seconds * kRate / 256.0); };

    // Settled, it moves by well under a dB from note to note.
    const auto [low, high] = std::minmax_element (gains.begin() + static_cast<std::ptrdiff_t> (chunkAt (6.0)),
                                                  gains.begin() + static_cast<std::ptrdiff_t> (chunkAt (10.0)));
    CHECK (*high - *low < 1.0);

    // In the silence the meters fall below the gate and the gain holds.
    CHECK_THAT (gains.back(), WithinAbs (gains[chunkAt (11.5)], 0.01));
}

TEST_CASE ("Switched off, Auto Gain returns to 0 dB quickly and smoothly", "[unit][gain]")
{
    const auto dry = whiteNoise (kRate, 8.0);
    AutoGain autoGain;
    autoGain.prepare (kRate);
    runAutoGain (autoGain, dry, juce::Decibels::decibelsToGain (-12.0f));
    REQUIRE (autoGain.getGainDb() > 5.0);

    const auto off = runAutoGain (autoGain, std::vector<float> (dry.begin(), dry.begin() + 24000),
                                  juce::Decibels::decibelsToGain (-12.0f), false);
    CHECK (std::abs (off.back()) < 0.05); // 0.5 s later
    for (std::size_t i = 1; i < off.size(); ++i)
        CHECK (std::abs (off[i] - off[i - 1]) < 1.5); // never a step: < 1.5 dB per 256 samples
}

TEST_CASE ("The safety limiter never lets a sample above -0.3 dBFS", "[unit][gain]")
{
    // Loud noise, a sine jumping to +20 dB, and lone impulses.
    std::vector<std::vector<float>> signals;
    auto noise = whiteNoise (kRate, 2.0);
    for (auto& x : noise)
        x *= 12.0f;
    signals.push_back (noise);

    auto burst = sine (1000.0, kRate, 2.0, 0.5f);
    for (std::size_t i = burst.size() / 2; i < burst.size(); ++i)
        burst[i] *= 20.0f;
    signals.push_back (burst);

    std::vector<float> clicks (96000, 0.0f);
    for (std::size_t i = 1000; i < clicks.size(); i += 4801)
        clicks[i] = (i % 2 == 0) ? 8.0f : -8.0f;
    signals.push_back (clicks);

    for (const auto& s : signals)
    {
        SafetyLimiter limiter;
        limiter.prepare (kRate);
        CHECK (peakOf (runLimiter (limiter, s)) <= SafetyLimiter::kCeiling);
    }
}

TEST_CASE ("Below the ceiling the limiter only delays, by exactly its look-ahead", "[unit][gain]")
{
    for (const auto rate : {44100.0, 48000.0, 96000.0})
    {
        INFO ("rate " << rate);
        SafetyLimiter limiter;
        limiter.prepare (rate);
        const auto delay = static_cast<std::size_t> (limiter.getLatencySamples());
        CHECK (delay == static_cast<std::size_t> (std::lround (0.0015 * rate)));

        const auto input = sine (440.0, rate, 1.0, 0.95f);
        const auto out = runLimiter (limiter, input);
        for (std::size_t i = delay; i < out.size(); ++i)
            REQUIRE (juce::exactlyEqual (out[i], input[i - delay]));
    }
}

TEST_CASE ("Switched off, the limiter passes everything, still delayed", "[unit][gain]")
{
    SafetyLimiter limiter;
    limiter.prepare (kRate);
    const auto delay = static_cast<std::size_t> (limiter.getLatencySamples());
    const auto input = sine (440.0, kRate, 0.5, 3.0f);
    const auto out = runLimiter (limiter, input, false);
    for (std::size_t i = delay; i < out.size(); ++i)
        REQUIRE (juce::exactlyEqual (out[i], input[i - delay]));
}

TEST_CASE ("The limiter turns down smoothly, without clicks", "[unit][gain]")
{
    // A 200 Hz sine jumping (at a zero crossing) from 0.5 to 4.0. The gain the
    // limiter applies, recovered as output / delayed input, must never jump: its
    // look-ahead average lets it change by at most 1/window per sample. (The
    // output itself may rise steeply right after the jump: that is the loud
    // sine's own slope, not a click.)
    auto input = sine (200.0, kRate, 1.0, 0.5f);
    for (std::size_t i = input.size() / 2; i < input.size(); ++i)
        input[i] *= 8.0f;

    SafetyLimiter limiter;
    limiter.prepare (kRate);
    const auto out = runLimiter (limiter, input);
    const auto delay = static_cast<std::size_t> (limiter.getLatencySamples());
    const auto window = static_cast<double> (delay + 1);

    double steepestGainChange = 0.0;
    for (auto i = delay + 1; i < out.size(); ++i)
    {
        const auto a = input[i - 1 - delay], b = input[i - delay];
        if (std::abs (a) < 0.05f || std::abs (b) < 0.05f)
            continue; // too close to a zero crossing to measure the gain
        const auto gainBefore = static_cast<double> (out[i - 1]) / static_cast<double> (a);
        const auto gainNow = static_cast<double> (out[i]) / static_cast<double> (b);
        steepestGainChange = std::max (steepestGainChange, std::abs (gainNow - gainBefore));
    }
    CHECK (steepestGainChange <= 1.0 / window + 1e-4);
    CHECK (peakOf (out) <= SafetyLimiter::kCeiling);
}
