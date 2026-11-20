#include "GrainModule.h"

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;

void GrainModule::prepare (const PrepareSpec& spec)
{
    sampleRate = spec.sampleRate;
    numChannels = std::clamp (spec.numChannels, 1, GrainBuffer::kMaxChannels);
    maxGrainSamples = std::max (1, static_cast<int> (std::lround (kMaxGrainSeconds * sampleRate)));

    // The history grains may reach into, the longest grain, and one chunk.
    const auto needed =
        static_cast<int> (std::ceil (kBufferSeconds * sampleRate)) + maxGrainSamples + spec.maxBlockSize;
    buffer.prepare (numChannels, needed);

    reset();
}

void GrainModule::reset() noexcept
{
    buffer.reset();
    for (auto& grain : grains)
        grain = {};
    schedulerPhase = 0.0;
}

float GrainModule::gainForOverlap (double overlap) noexcept
{
    // Identical copies add in amplitude; the Hann window averages 0.5.
    constexpr double hannMean = 0.5;
    const auto sum = hannMean * overlap;
    return sum <= 1.0 ? 1.0f : static_cast<float> (1.0 / sum);
}

int GrainModule::getActiveGrains() const noexcept
{
    return static_cast<int> (std::count_if (grains.begin(), grains.end(), [] (const Grain& g) { return g.active; }));
}

void GrainModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto& p = context.params;
    process (block, Settings{p.get (ParamId::grainSize) / 1000.0, p.get (ParamId::grainDensity)});
}

void GrainModule::startGrain (int offset, std::int64_t now, int length, float gain) noexcept
{
    const auto slot = std::find_if (grains.begin(), grains.end(), [] (const Grain& g) { return !g.active; });
    if (slot == grains.end())
    {
        ++grainsSkipped;
        return;
    }

    // One grain length behind "now": at rate 1 every sample it reads has
    // already been written by the time it is played.
    slot->active = true;
    slot->startOffset = offset;
    slot->readStart = now - length;
    slot->length = length;
    slot->elapsed = 0;
    slot->windowStep = static_cast<float> (GrainWindow::kSegments) / static_cast<float> (length);
    slot->gain = gain;
    ++grainsStarted;
}

void GrainModule::process (juce::dsp::AudioBlock<float>& block, const Settings& settings) noexcept
{
    const auto numSamples = static_cast<int> (block.getNumSamples());
    if (numSamples == 0)
        return;

    jassert (static_cast<int> (block.getNumChannels()) >= numChannels);

    float* channels[GrainBuffer::kMaxChannels] = {};
    for (int ch = 0; ch < numChannels; ++ch)
        channels[ch] = block.getChannelPointer (static_cast<std::size_t> (ch));

    // The whole chunk goes into the buffer first: a grain starting at sample i
    // reads only indices before (chunk start + i), all of which are now written.
    const auto chunkStart = buffer.getWriteCount();
    buffer.write (channels, numChannels, numSamples);

    const auto length =
        std::clamp (static_cast<int> (std::lround (settings.sizeSeconds * sampleRate)), 1, maxGrainSamples);
    const auto density = std::max (0.0, settings.density);
    const auto gain = gainForOverlap (density * static_cast<double> (length) / sampleRate);

    // Schedule: one grain each time the accumulator passes 1.
    const auto increment = density / sampleRate;
    for (int i = 0; i < numSamples; ++i)
    {
        schedulerPhase += increment;
        while (schedulerPhase >= 1.0)
        {
            schedulerPhase -= 1.0;
            const auto now = chunkStart + i;
            // Before the buffer holds a full grain there is nothing to play yet.
            if (now - length >= buffer.getOldestIndex())
                startGrain (i, now, length, gain);
        }
    }

    // Render: the chunk becomes the sum of the sounding grains.
    for (int ch = 0; ch < numChannels; ++ch)
        juce::FloatVectorOperations::clear (channels[ch], numSamples);

    for (auto& grain : grains)
    {
        if (!grain.active)
            continue;

        const auto first = grain.startOffset;
        const auto count = std::min (numSamples - first, grain.length - grain.elapsed);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = channels[ch];
            for (int k = 0; k < count; ++k)
            {
                const auto position = grain.elapsed + k;
                const auto w = window.atSegment (static_cast<float> (position) * grain.windowStep);
                out[first + k] += grain.gain * w * buffer.read (ch, grain.readStart + position);
            }
        }

        grain.elapsed += count;
        grain.startOffset = 0;
        if (grain.elapsed >= grain.length)
            grain.active = false;
    }
}

} // namespace thunder::dsp
