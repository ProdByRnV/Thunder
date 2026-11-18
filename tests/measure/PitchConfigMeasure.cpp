// Measures candidate pitch-engine configurations (ROADMAP T-0.1-11, PRD
// DEC-016): latency, pitch accuracy, spectral purity, attack smearing and CPU.
// Hidden from the normal test run; run it by hand:
//
//   ThunderTests "[measure]"

#include "core/dsp/pitch/PitchModule.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>
#include <vector>

using namespace thunder;
using thunder::dsp::PitchEngineConfig;
using thunder::dsp::PitchModule;

namespace
{
std::vector<float> runPitch (const PitchEngineConfig& config, double sampleRate, const std::vector<float>& input,
                             int semitones, int* latency = nullptr)
{
    PitchModule pitch (config);
    pitch.prepare ({sampleRate, 256, 2});
    if (latency != nullptr)
        *latency = pitch.getLatencySamples();

    auto snapshot = params::ParamSnapshot::defaults();
    snapshot.set (params::ParamId::pitchSemitones, static_cast<float> (semitones));
    const TransportState transport;

    std::vector<float> left = input, right = input;
    for (std::size_t start = 0; start < input.size(); start += 256)
    {
        const auto n = std::min<std::size_t> (256, input.size() - start);
        float* channels[] = {left.data() + start, right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        pitch.process (block, {transport, snapshot});
    }
    return left;
}

// Frequency of the strongest peak and the share of energy outside ±3% of the
// expected frequency (in dB: lower is cleaner).
std::pair<double, double> analyse (const std::vector<float>& signal, std::size_t start, double sampleRate,
                                   double expectedHz)
{
    constexpr int order = 15;
    constexpr std::size_t size = std::size_t{1} << order;
    std::vector<float> data (2 * size, 0.0f);
    for (std::size_t i = 0; i < size; ++i)
    {
        const auto phase = static_cast<double> (i) / static_cast<double> (size);
        const auto window = static_cast<float> (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * phase));
        data[i] = signal[start + i] * window;
    }

    juce::dsp::FFT fft (order);
    fft.performFrequencyOnlyForwardTransform (data.data());

    const auto binHz = sampleRate / static_cast<double> (size);
    std::size_t peak = 1;
    double inBand = 0.0, total = 0.0;
    for (std::size_t bin = 1; bin < size / 2; ++bin)
    {
        const auto power = static_cast<double> (data[bin]) * static_cast<double> (data[bin]);
        total += power;
        if (std::abs (static_cast<double> (bin) * binHz - expectedHz) <= expectedHz * 0.03)
            inBand += power;
        if (data[bin] > data[peak])
            peak = bin;
    }

    // Parabolic interpolation around the peak bin.
    const auto a = std::log (static_cast<double> (data[peak - 1]) + 1e-12);
    const auto b = std::log (static_cast<double> (data[peak]) + 1e-12);
    const auto c = std::log (static_cast<double> (data[peak + 1]) + 1e-12);
    const auto offset = 0.5 * (a - c) / (a - 2.0 * b + c);
    const auto peakHz = (static_cast<double> (peak) + offset) * binHz;

    return {peakHz, 10.0 * std::log10 (std::max (1e-12, (total - inBand) / total))};
}

double energy (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (auto i = from; i < to && i < s.size(); ++i)
        e += static_cast<double> (s[i]) * static_cast<double> (s[i]);
    return e;
}

struct Candidate
{
    const char* name;
    PitchEngineConfig config;
};
} // namespace

TEST_CASE ("Measure pitch engine configurations", "[.][measure]")
{
    const Candidate candidates[] = {
        {"default 120/30 ms", {0.12, 0.03, false}}, {"cheaper 100/40 ms split", {0.10, 0.04, true}},
        {"100/25 ms", {0.10, 0.025, false}},        {"80/20 ms", {0.08, 0.02, false}},
        {"60/15 ms", {0.06, 0.015, false}},         {"100/12.5 ms", {0.10, 0.0125, false}},
        {"80/10 ms", {0.08, 0.01, false}},          {"80/13.3 ms", {0.08, 0.08 / 6.0, false}},
    };

    for (const auto& candidate : candidates)
    {
        std::printf ("\n== %s ==\n", candidate.name);

        for (const auto sampleRate : {44100.0, 48000.0})
        {
            int latency = 0;
            std::vector<float> impulse (static_cast<std::size_t> (sampleRate), 0.0f);
            impulse[2000] = 1.0f;
            const auto out = runPitch (candidate.config, sampleRate, impulse, 0, &latency);

            std::size_t peak = 0;
            for (std::size_t i = 0; i < out.size(); ++i)
                if (std::abs (out[i]) > std::abs (out[peak]))
                    peak = i;

            std::printf ("  %.0f Hz: reported latency %d samples (%.1f ms), impulse peak at %+d samples from it\n",
                         sampleRate, latency, 1000.0 * static_cast<double> (latency) / sampleRate,
                         static_cast<int> (peak) - 2000 - latency);
        }

        const double sampleRate = 48000.0;
        int latency = 0;
        runPitch (candidate.config, sampleRate, std::vector<float> (256, 0.0f), 0, &latency);

        // Pitch accuracy and purity: sines shifted by 0 (the analysis itself), a fifth and an octave.
        for (const auto hz : {82.41, 110.0, 146.83, 220.0, 329.63, 440.0, 880.0})
            for (const auto shiftSt : {0, 7, 12})
            {
                std::vector<float> sine (static_cast<std::size_t> (sampleRate * 2.0));
                for (std::size_t i = 0; i < sine.size(); ++i)
                    sine[i] = 0.5f * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * hz *
                                                                   static_cast<double> (i) / sampleRate));

                const auto out = runPitch (candidate.config, sampleRate, sine, shiftSt);
                const auto expected = hz * std::pow (2.0, shiftSt / 12.0);
                const auto [peakHz, offBandDb] =
                    analyse (out, static_cast<std::size_t> (latency) + static_cast<std::size_t> (sampleRate * 0.5),
                             sampleRate, expected);
                std::printf ("  %6.1f Hz %+3d st: %+6.2f cents, off-pitch energy %6.1f dB\n", hz, shiftSt,
                             1200.0 * std::log2 (peakHz / expected), offBandDb);
            }

        // Attack smearing: a pluck shifted up an octave; energy that arrives before
        // the (latency-compensated) onset, relative to the 20 ms after it.
        {
            const std::size_t onset = 10000;
            std::vector<float> pluck (static_cast<std::size_t> (sampleRate), 0.0f);
            for (auto i = onset; i < pluck.size(); ++i)
            {
                const auto t = static_cast<double> (i - onset) / sampleRate;
                pluck[i] = static_cast<float> (0.5 * std::exp (-t / 0.3) *
                                               std::sin (2.0 * juce::MathConstants<double>::pi * 440.0 * t));
            }
            const auto out = runPitch (candidate.config, sampleRate, pluck, 12);
            const auto at = onset + static_cast<std::size_t> (latency);
            const auto window = static_cast<std::size_t> (sampleRate * 0.02);
            std::printf (
                "  pre-echo before a pluck: %.1f dB\n",
                10.0 * std::log10 ((energy (out, at - window, at) + 1e-12) / (energy (out, at, at + window) + 1e-12)));
        }

        // CPU: 10 s of noise, shifted up a fifth.
        {
            std::vector<float> noise (static_cast<std::size_t> (sampleRate * 10.0));
            juce::Random random (1);
            for (auto& x : noise)
                x = random.nextFloat() - 0.5f;

            const auto start = juce::Time::getMillisecondCounterHiRes();
            runPitch (candidate.config, sampleRate, noise, 7);
            const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;
            std::printf ("  CPU: %.2f%% of one core (stereo, 48 kHz)\n", elapsedMs / 10000.0 * 100.0);
        }
    }

    SUCCEED();
}
