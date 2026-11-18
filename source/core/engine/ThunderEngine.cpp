#include "ThunderEngine.h"

#include "core/util/Math.h"

#include <algorithm>

namespace thunder
{

using params::ParamId;

void ThunderEngine::prepare (double newSampleRate)
{
    jassert (newSampleRate > 0.0);
    sampleRate = newSampleRate;

    inputGain.prepare (sampleRate);
    outputGain.prepare (sampleRate);

    nonFiniteReplaced.store (0, std::memory_order_relaxed);
    prepared = true;
    reset();
}

void ThunderEngine::reset() noexcept
{
    transport = {};
    firstBlock = true;
}

void ThunderEngine::process (float* const* channels, int numSamples, const TransportState& hostTransport,
                             const params::ParamSnapshot& snapshot) noexcept
{
    juce::ScopedNoDenormals noDenormals;

    if (numSamples <= 0)
        return;

    if (!prepared)
    {
        // A host that processes before preparing gets silence, not garbage.
        for (int ch = 0; ch < kNumChannels; ++ch)
            juce::FloatVectorOperations::clear (channels[ch], numSamples);
        return;
    }

    const auto inputDb = snapshot.get (ParamId::inputGain);
    const auto outputDb = snapshot.get (ParamId::outputGain);

    if (firstBlock)
    {
        inputGain.snapToDecibels (inputDb);
        outputGain.snapToDecibels (outputDb);
        firstBlock = false;
    }
    else
    {
        inputGain.setTargetDecibels (inputDb);
        outputGain.setTargetDecibels (outputDb);
    }

    transport = hostTransport;

    for (int offset = 0; offset < numSamples;)
    {
        const auto chunk = std::min (kMaxChunkSize, numSamples - offset);
        float* chunkChannels[kNumChannels] = {channels[0] + offset, channels[1] + offset};

        processChunk (chunkChannels, chunk);

        offset += chunk;
        if (offset < numSamples)
            transport = transport.advancedBy (chunk, sampleRate);
    }
}

void ThunderEngine::processChunk (float* const* channels, int numSamples) noexcept
{
    sanitise (channels, numSamples);
    inputGain.apply (channels, kNumChannels, numSamples);

    // TODO(PRD): dry path, module chain and output stage (v0.1–v0.4).

    outputGain.apply (channels, kNumChannels, numSamples);

    // Gains can push extreme (but finite) input to Inf; never let that out.
    sanitise (channels, numSamples);
}

void ThunderEngine::sanitise (float* const* channels, int numSamples) noexcept
{
    int replaced = 0;
    for (int ch = 0; ch < kNumChannels; ++ch)
        replaced += util::replaceNonFinite (channels[ch], numSamples);

    if (replaced > 0)
        nonFiniteReplaced.fetch_add (static_cast<std::uint64_t> (replaced), std::memory_order_relaxed);
}

} // namespace thunder
