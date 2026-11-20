#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <cmath>

namespace thunder::dsp
{

// The grain envelope (PRD FR-GRAIN-003): a Hann window, 0.5·(1 − cos 2πx) for
// x from 0 to 1, read from a table with linear interpolation.
//
// It rises from and returns to exactly zero, so a grain never starts or stops
// with a step (a click). Overlapping copies spaced evenly sum to a constant,
// which keeps a dense, steady cloud free of amplitude ripple.
//
// 4096 segments: linear interpolation between them is accurate to about 1e-7,
// far below anything audible, and the table (16 KB) stays in cache.
class GrainWindow
{
public:
    static constexpr int kSegments = 4096;

    GrainWindow() noexcept
    {
        for (int i = 0; i <= kSegments; ++i)
        {
            const auto x = static_cast<double> (i) / kSegments;
            table[static_cast<std::size_t> (i)] =
                static_cast<float> (0.5 - 0.5 * std::cos (2.0 * juce::MathConstants<double>::pi * x));
        }
    }

    // [RT] The window at `position` segments into the table (0 … kSegments).
    float atSegment (float position) const noexcept
    {
        const auto clamped = juce::jlimit (0.0f, static_cast<float> (kSegments), position);
        const auto index = std::min (static_cast<int> (clamped), kSegments - 1);
        const auto fraction = clamped - static_cast<float> (index);
        const auto a = table[static_cast<std::size_t> (index)];
        const auto b = table[static_cast<std::size_t> (index) + 1];
        return a + fraction * (b - a);
    }

    // [RT] The window at x (0 … 1).
    float at (float x) const noexcept { return atSegment (x * static_cast<float> (kSegments)); }

private:
    std::array<float, kSegments + 1> table{};
};

} // namespace thunder::dsp
