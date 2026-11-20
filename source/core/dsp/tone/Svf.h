#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace thunder::dsp
{

// A 2-pole state-variable filter in the topology-preserving (TPT) form described
// by Zavalishin and Simper: the analogue filter's structure discretised with
// trapezoidal integration. Unlike a direct-form biquad, it stays stable and
// quiet while its cutoff moves every sample, which is what smoothed and
// automated cutoffs do (PRD FR-CORE-008). One instance holds the state of up to
// two channels.
class Svf
{
public:
    static constexpr int kMaxChannels = 2;

    // The coefficients for one cutoff and damping; shared by filters with the
    // same settings.
    struct Coefficients
    {
        float a1 = 1.0f, a2 = 0.0f, a3 = 0.0f, k = 1.41421356f;

        // `normalisedCutoff` is cutoff / sample rate (kept below 0.5 by the
        // caller); `damping` is 1/Q.
        static Coefficients make (double normalisedCutoff, double damping) noexcept
        {
            const auto g = std::tan (juce::MathConstants<double>::pi * normalisedCutoff);
            const auto a1 = 1.0 / (1.0 + g * (g + damping));
            const auto a2 = g * a1;
            return {static_cast<float> (a1), static_cast<float> (a2), static_cast<float> (g * a2),
                    static_cast<float> (damping)};
        }
    };

    void reset() noexcept { state = {}; }

    // [RT] One sample through the filter; returns the low-pass output, or the
    // high-pass one when `highPass` is set.
    float process (int channel, float x, const Coefficients& c, bool highPass) noexcept
    {
        auto& s = state[static_cast<std::size_t> (channel)];
        const auto v3 = x - s.ic2;
        const auto v1 = c.a1 * s.ic1 + c.a2 * v3;
        const auto v2 = s.ic2 + c.a2 * s.ic1 + c.a3 * v3;
        s.ic1 = 2.0f * v1 - s.ic1;
        s.ic2 = 2.0f * v2 - s.ic2;
        return highPass ? x - c.k * v1 - v2 : v2;
    }

private:
    struct State
    {
        float ic1 = 0.0f, ic2 = 0.0f;
    };
    std::array<State, kMaxChannels> state{};
};

} // namespace thunder::dsp
