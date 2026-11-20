// The Tone module: high-pass and low-pass with 12/24 dB/oct slopes, smooth
// cutoff and slope changes (PRD FR-TONE-001/002, FR-CORE-008; ROADMAP T-0.4-01).

#include "support/TestSignals.h"

#include "core/dsp/tone/ToneModule.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;
using params::FilterSlope;
using params::ParamId;

namespace
{
constexpr double kRate = 48000.0;

params::ParamSnapshot toneSettings (float hp, FilterSlope hpSlope, float lp, FilterSlope lpSlope)
{
    auto s = params::ParamSnapshot::defaults();
    s.set (ParamId::hpFreq, hp);
    s.set (ParamId::lpFreq, lp);
    setChoice (s, ParamId::hpSlope, hpSlope);
    setChoice (s, ParamId::lpSlope, lpSlope);
    return s;
}

// The steady-state gain (dB) of a sine at `hz` through the filters.
double gainDb (const params::ParamSnapshot& settings, double hz, double rate = kRate)
{
    dsp::ToneModule tone;
    tone.prepare ({rate, 256, 2});
    const auto input = sine (hz, rate, 1.0);
    const auto out = runModule (tone, input, settings);
    const auto from = input.size() / 2;
    return 10.0 * std::log10 (energy (out, from, out.size()) / energy (input, from, input.size()));
}

float largestStep (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    float largest = 0.0f;
    for (auto i = std::max<std::size_t> (from, 1); i < std::min (to, s.size()); ++i)
        largest = std::max (largest, std::abs (s[i] - s[i - 1]));
    return largest;
}
} // namespace

TEST_CASE ("The high-pass has a Butterworth response at 12 and 24 dB/oct", "[unit][tone]")
{
    // |H| = 1/√(1 + (fc/f)^(2n)): −3 dB at the cutoff, −12.3 dB (2-pole) or
    // −24.1 dB (4-pole) an octave below, flat well above.
    const auto steep = toneSettings (200.0f, FilterSlope::Db24, 20000.0f, FilterSlope::Db12);
    CHECK_THAT (gainDb (steep, 200.0), WithinAbs (-3.01, 0.3));
    CHECK_THAT (gainDb (steep, 100.0), WithinAbs (-24.1, 1.0));
    CHECK_THAT (gainDb (steep, 2000.0), WithinAbs (0.0, 0.2));

    const auto gentle = toneSettings (200.0f, FilterSlope::Db12, 20000.0f, FilterSlope::Db12);
    CHECK_THAT (gainDb (gentle, 200.0), WithinAbs (-3.01, 0.3));
    CHECK_THAT (gainDb (gentle, 100.0), WithinAbs (-12.3, 1.0));
    CHECK_THAT (gainDb (gentle, 2000.0), WithinAbs (0.0, 0.2));
}

TEST_CASE ("The low-pass has a Butterworth response at 12 and 24 dB/oct", "[unit][tone]")
{
    const auto gentle = toneSettings (20.0f, FilterSlope::Db12, 1000.0f, FilterSlope::Db12);
    CHECK_THAT (gainDb (gentle, 1000.0), WithinAbs (-3.01, 0.3));
    CHECK_THAT (gainDb (gentle, 2000.0), WithinAbs (-12.3, 1.0));
    CHECK_THAT (gainDb (gentle, 100.0), WithinAbs (0.0, 0.2));

    const auto steep = toneSettings (20.0f, FilterSlope::Db12, 1000.0f, FilterSlope::Db24);
    CHECK_THAT (gainDb (steep, 1000.0), WithinAbs (-3.01, 0.3));
    CHECK_THAT (gainDb (steep, 2000.0), WithinAbs (-24.1, 1.0));
}

TEST_CASE ("The Tone defaults are the PRD's: high-pass 200 Hz 24 dB, low-pass 16 kHz 12 dB", "[unit][tone]")
{
    const auto defaults = params::ParamSnapshot::defaults();
    CHECK (defaults.isOn (ParamId::toneOn));
    CHECK_THAT (gainDb (defaults, 200.0), WithinAbs (-3.01, 0.3));
    CHECK_THAT (gainDb (defaults, 1000.0), WithinAbs (0.0, 0.2));
    CHECK_THAT (gainDb (defaults, 16000.0), WithinAbs (-3.01, 0.5));
}

TEST_CASE ("Cutoffs near Nyquist stay stable at every sample rate", "[unit][tone]")
{
    for (const auto rate : {44100.0, 48000.0, 96000.0})
    {
        INFO ("rate " << rate);
        const auto settings = toneSettings (2000.0f, FilterSlope::Db24, 20000.0f, FilterSlope::Db24);
        dsp::ToneModule tone;
        tone.prepare ({rate, 256, 2});
        const auto out = runModule (tone, whiteNoise (rate, 1.0), settings);
        CHECK (allFinite (out));
        CHECK (energy (out, 0, out.size()) > 0.0);
    }
}

TEST_CASE ("Moving a cutoff or switching a slope never clicks", "[unit][tone]")
{
    // A 150 Hz sine, right where the high-pass works. The cutoff jumps between
    // 20 Hz and 2 kHz, and the slope flips, at chunk boundaries.
    dsp::ToneModule tone;
    tone.prepare ({kRate, 256, 2});
    const auto input = sine (150.0, kRate, 2.0);
    std::vector<float> left = input, right = input;
    const TransportState transport;

    for (std::size_t start = 0, chunk = 0; start + 256 <= input.size(); start += 256, ++chunk)
    {
        const auto phase = (chunk / 20) % 4;
        const auto settings = toneSettings (phase == 1 || phase == 2 ? 2000.0f : 20.0f,
                                            phase >= 2 ? FilterSlope::Db12 : FilterSlope::Db24, 20000.0f,
                                            phase % 2 == 0 ? FilterSlope::Db24 : FilterSlope::Db12);
        float* channels[] = {left.data() + start, right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, 256);
        tone.process (block, ProcessContext{transport, settings});
    }

    CHECK (allFinite (left));
    // The input's own steepest step is 0.5·2π·150/48000 ≈ 0.0098.
    CHECK (largestStep (left, 1, left.size()) < 0.012f);
}

TEST_CASE ("Tone: silence stays silent, and reset clears the filters", "[unit][tone]")
{
    dsp::ToneModule tone;
    tone.prepare ({kRate, 256, 2});
    const auto settings = params::ParamSnapshot::defaults();

    const auto quiet = runModule (tone, std::vector<float> (4800, 0.0f), settings);
    CHECK (juce::exactlyEqual (energy (quiet, 0, quiet.size()), 0.0));

    runModule (tone, whiteNoise (kRate, 0.2), settings);
    tone.reset();
    const auto after = runModule (tone, std::vector<float> (4800, 0.0f), settings);
    CHECK (juce::exactlyEqual (energy (after, 0, after.size()), 0.0));
}
