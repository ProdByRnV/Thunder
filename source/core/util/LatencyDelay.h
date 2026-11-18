#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <vector>

// A fixed delay of a whole number of samples, used to keep signals time-aligned
// with a path that has latency: the dry signal against the pitch-shifted layer
// (PRD FR-CORE-006), and a bypassed module against its latency.

namespace thunder::util
{

class LatencyDelay
{
public:
    // [NRT] Allocates the delay memory and clears it.
    void prepare (int numChannels, int delaySamples)
    {
        jassert (numChannels > 0 && delaySamples >= 0);
        channels = numChannels;
        delay = delaySamples;
        buffer.assign (static_cast<std::size_t> (channels) * static_cast<std::size_t> (delay), 0.0f);
        writePosition = 0;
    }

    // [RT]
    void reset() noexcept
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writePosition = 0;
    }

    int getDelaySamples() const noexcept { return delay; }

    // [RT] Delays `numSamples` samples of each channel in place.
    void process (float* const* data, int numChannels, int numSamples) noexcept
    {
        if (delay == 0)
            return;

        jassert (numChannels <= channels);
        auto position = writePosition;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* line = buffer.data() + static_cast<std::size_t> (ch) * static_cast<std::size_t> (delay);
            auto* samples = data[ch];
            position = writePosition;

            for (int i = 0; i < numSamples; ++i)
            {
                const auto delayed = line[position];
                line[position] = samples[i];
                samples[i] = delayed;

                if (++position == delay)
                    position = 0;
            }
        }

        writePosition = position;
    }

private:
    std::vector<float> buffer;
    int channels = 0;
    int delay = 0;
    int writePosition = 0;
};

} // namespace thunder::util
