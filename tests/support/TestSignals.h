#pragma once

#include "core/engine/Module.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

// Test signals and measurements shared by the module and engine tests
// (ARCHITECTURE §15.2).

namespace thunder::test
{

inline std::vector<float> sine (double hz, double sampleRate, double seconds, float amplitude = 0.5f)
{
    std::vector<float> s (static_cast<std::size_t> (sampleRate * seconds));
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = amplitude * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * hz *
                                                         static_cast<double> (i) / sampleRate));
    return s;
}

inline std::vector<float> impulse (std::size_t length, std::size_t position, float value = 1.0f)
{
    std::vector<float> s (length, 0.0f);
    s[position] = value;
    return s;
}

// Frequency from upward zero crossings between `from` and `to`, with linear
// interpolation of each crossing. Accurate for clean, sine-like signals.
inline double zeroCrossingFrequency (const std::vector<float>& s, std::size_t from, std::size_t to, double sampleRate)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (auto i = std::max<std::size_t> (from, 1); i < to && i < s.size(); ++i)
    {
        if (s[i - 1] < 0.0f && s[i] >= 0.0f)
        {
            const auto fraction = static_cast<double> (-s[i - 1]) / static_cast<double> (s[i] - s[i - 1]);
            const auto position = static_cast<double> (i - 1) + fraction;
            if (first < 0.0)
                first = position;
            last = position;
            ++crossings;
        }
    }
    return crossings > 1 ? sampleRate * (crossings - 1) / (last - first) : 0.0;
}

inline double cents (double measuredHz, double expectedHz)
{
    return 1200.0 * std::log2 (measuredHz / expectedHz);
}

inline double energy (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (auto i = from; i < to && i < s.size(); ++i)
        e += static_cast<double> (s[i]) * static_cast<double> (s[i]);
    return e;
}

inline std::size_t peakIndex (const std::vector<float>& s)
{
    std::size_t peak = 0;
    for (std::size_t i = 1; i < s.size(); ++i)
        if (std::abs (s[i]) > std::abs (s[peak]))
            peak = i;
    return peak;
}

inline bool allFinite (const std::vector<float>& s)
{
    return std::all_of (s.begin(), s.end(), [] (float x) { return std::isfinite (x); });
}

// Runs a mono signal through `module` as identical left and right channels, in
// chunks of `chunk` samples. Returns the left output, or both if `right` is given.
inline std::vector<float> runModule (Module& module, const std::vector<float>& input,
                                     const params::ParamSnapshot& snapshot, std::size_t chunk = 256,
                                     std::vector<float>* right = nullptr)
{
    const TransportState transport;
    std::vector<float> left = input, rightChannel = input;

    for (std::size_t start = 0; start < input.size(); start += chunk)
    {
        const auto n = std::min (chunk, input.size() - start);
        float* channels[] = {left.data() + start, rightChannel.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        module.process (block, ProcessContext{transport, snapshot});
    }

    if (right != nullptr)
        *right = std::move (rightChannel);
    return left;
}

} // namespace thunder::test
