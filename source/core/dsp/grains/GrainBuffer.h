#pragma once

#include <juce_core/juce_core.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace thunder::dsp
{

// The recent input the grains read from (PRD FR-GRAIN-001): a circular buffer
// per channel, sized once in prepare().
//
// Samples are addressed by their absolute index: the number of samples written
// before them since the last reset. That keeps the grains' read positions simple
// (no wrap-around arithmetic outside this class) and makes it easy to check that
// a grain only reads audio that has already been written and not yet overwritten.
//
// The size is rounded up to a power of two so the wrap is a bit mask.
class GrainBuffer
{
public:
    static constexpr int kMaxChannels = 2;

    // [NRT] Allocates room for at least `minimumSamples` per channel and clears it.
    void prepare (int numChannels, int minimumSamples)
    {
        jassert (numChannels >= 1 && numChannels <= kMaxChannels && minimumSamples > 0);
        channels = std::clamp (numChannels, 1, kMaxChannels);

        size = 1;
        while (size < minimumSamples)
            size <<= 1;
        mask = size - 1;

        for (auto& line : lines)
            line.assign (static_cast<std::size_t> (size), 0.0f);

        reset();
    }

    // [RT] Forgets everything written.
    void reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        written = 0;
    }

    // [RT] Appends `numSamples` samples of each channel.
    void write (const float* const* data, int numChannels, int numSamples) noexcept
    {
        jassert (numSamples <= size);
        for (int ch = 0; ch < channels; ++ch)
        {
            // A mono input feeds both lines.
            const auto* source = data[std::min (ch, numChannels - 1)];
            auto& line = lines[static_cast<std::size_t> (ch)];
            auto position = static_cast<int> (written & mask);

            for (int i = 0; i < numSamples; ++i)
            {
                line[static_cast<std::size_t> (position)] = source[i];
                position = (position + 1) & (size - 1);
            }
        }
        written += numSamples;
    }

    // [RT] The sample with absolute index `index`. It must have been written and
    // not yet overwritten: getOldestIndex() <= index < getWriteCount().
    float read (int channel, std::int64_t index) const noexcept
    {
        jassert (index >= getOldestIndex() && index < written);
        return lines[static_cast<std::size_t> (channel)][static_cast<std::size_t> (index & mask)];
    }

    // How many samples have been written since the last reset: the absolute
    // index the next sample will get.
    std::int64_t getWriteCount() const noexcept { return written; }

    // The oldest absolute index still held.
    std::int64_t getOldestIndex() const noexcept { return std::max<std::int64_t> (0, written - size); }

    int getSize() const noexcept { return size; }
    int getNumChannels() const noexcept { return channels; }

private:
    std::array<std::vector<float>, kMaxChannels> lines;
    int channels = kMaxChannels;
    int size = 1;
    std::int64_t mask = 0;
    std::int64_t written = 0;
};

} // namespace thunder::dsp
