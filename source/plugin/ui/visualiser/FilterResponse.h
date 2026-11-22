#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <cmath>

namespace thunder::ui
{

// The Tone module's high-pass and low-pass as the spectrum view draws them
// (PRD FR-TONE-004, UI-VIS-003). The filters are Butterworth (2 poles for
// 12 dB/oct, 4 for 24 dB/oct; PRD FR-TONE-001/002) built as TPT state-variable
// filters, which are bilinear: their response at f is the analogue prototype's
// at the pre-warped frequency tan(π·f / rate). So this is exact, not a sketch,
// and −3 dB at each cutoff.
struct FilterResponse
{
    float highPassHz = 200.0f;
    bool highPass24 = true;
    float lowPassHz = 16000.0f;
    bool lowPass24 = false;

    // The module keeps cutoffs below this fraction of the sample rate.
    static constexpr double kMaxNormalisedCutoff = 0.45;

    // The combined response at `hz`, in dB.
    float decibelsAt (double hz, double sampleRate) const noexcept
    {
        if (sampleRate <= 0.0 || hz <= 0.0)
            return 0.0f;

        const auto warp = [sampleRate] (double f)
        {
            const auto normalised = std::clamp (f / sampleRate, 1.0 / sampleRate, kMaxNormalisedCutoff);
            return std::tan (juce::MathConstants<double>::pi * normalised);
        };

        // Frequencies at or above Nyquist don't exist in the signal; draw the
        // curve's value just below it.
        const auto w = std::tan (juce::MathConstants<double>::pi * std::min (hz / sampleRate, 0.4999));

        // Butterworth power responses: high-pass r²ⁿ / (1 + r²ⁿ), low-pass
        // 1 / (1 + r²ⁿ), with r the frequency over the cutoff and n the poles.
        const auto highR = std::pow (w / warp (highPassHz), highPass24 ? 8.0 : 4.0);
        const auto lowR = std::pow (w / warp (lowPassHz), lowPass24 ? 8.0 : 4.0);
        const auto power = (highR / (1.0 + highR)) * (1.0 / (1.0 + lowR));

        return static_cast<float> (10.0 * std::log10 (std::max (power, 1.0e-12)));
    }
};

} // namespace thunder::ui
