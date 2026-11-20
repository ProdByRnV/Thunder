#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace thunder::dsp
{

// A running loudness estimate for Auto Gain (PRD FR-GAIN-001, ARCHITECTURE §5.9).
//
// The signal is K-weighted as in ITU-R BS.1770 (a high-shelf of about +4 dB
// above 1.5 kHz, then a high-pass around 38 Hz: roughly how loud the ear finds
// each frequency), squared, summed over the channels and averaged with a
// 400 ms time constant, the window of BS.1770's "momentary" loudness. An
// exponential average rather than a sliding window: it costs two multiplies
// per sample and gives the same result whatever the block size.
//
// The filter coefficients come from the analogue prototypes (as in libebur128),
// so they are right at every sample rate; at 48 kHz they equal BS.1770's table.
class LoudnessMeter
{
public:
    static constexpr int kMaxChannels = 2;
    static constexpr double kAverageSeconds = 0.4;

    struct Biquad
    {
        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    };

    // The two K-weighting stages for a sample rate.
    static std::array<Biquad, 2> kWeighting (double sampleRate) noexcept
    {
        constexpr auto pi = juce::MathConstants<double>::pi;
        std::array<Biquad, 2> stages{};

        {
            // Stage 1: high shelf.
            constexpr double f0 = 1681.974450955533, gainDb = 3.999843853973347, q = 0.7071752369554196;
            const auto k = std::tan (pi * f0 / sampleRate);
            const auto vh = std::pow (10.0, gainDb / 20.0);
            const auto vb = std::pow (vh, 0.4996667741545416);
            const auto a0 = 1.0 + k / q + k * k;
            stages[0] = {(vh + vb * k / q + k * k) / a0, 2.0 * (k * k - vh) / a0, (vh - vb * k / q + k * k) / a0,
                         2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
        }
        {
            // Stage 2: high-pass (RLB weighting). BS.1770 uses an unnormalised
            // numerator of 1, -2, 1.
            constexpr double f0 = 38.13547087602444, q = 0.5003270373238773;
            const auto k = std::tan (pi * f0 / sampleRate);
            const auto a0 = 1.0 + k / q + k * k;
            stages[1] = {1.0, -2.0, 1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0};
        }
        return stages;
    }

    // [NRT]
    void prepare (double sampleRate) noexcept
    {
        stages = kWeighting (sampleRate);
        coefficient = 1.0 - std::exp (-1.0 / (kAverageSeconds * sampleRate));
        reset();
    }

    // [RT]
    void reset() noexcept
    {
        state = {};
        meanSquare = 0.0;
    }

    // [RT] Adds one sample per channel; returns the updated mean square.
    double push (const float* samples, int numChannels) noexcept
    {
        double sum = 0.0;
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto x = static_cast<double> (samples[ch]);
            for (std::size_t s = 0; s < stages.size(); ++s)
            {
                auto& z = state[static_cast<std::size_t> (ch)][s];
                const auto& c = stages[s];
                // Transposed direct form II.
                const auto y = c.b0 * x + z[0];
                z[0] = c.b1 * x - c.a1 * y + z[1];
                z[1] = c.b2 * x - c.a2 * y;
                x = y;
            }
            sum += x * x;
        }
        meanSquare += coefficient * (sum - meanSquare);
        return meanSquare;
    }

    double getMeanSquare() const noexcept { return meanSquare; }

    // The estimate in dB (relative to a full-scale sine-ish level; only
    // differences between two meters matter here).
    double getDecibels() const noexcept { return 10.0 * std::log10 (meanSquare + 1.0e-20); }

private:
    std::array<Biquad, 2> stages{};
    std::array<std::array<std::array<double, 2>, 2>, kMaxChannels> state{};
    double coefficient = 0.0;
    double meanSquare = 0.0;
};

} // namespace thunder::dsp
