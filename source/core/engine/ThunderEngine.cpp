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

    const PrepareSpec spec{sampleRate, kMaxChunkSize, kNumChannels};
    pitch.prepare (spec);
    space.prepare (spec);

    latencySamples = pitch.getLatencySamples() + space.getLatencySamples();
    dryDelay.prepare (kNumChannels, latencySamples);

    for (auto& channel : dry)
        channel.assign (static_cast<std::size_t> (kMaxChunkSize), 0.0f);

    inputGain.prepare (sampleRate);
    outputGain.prepare (sampleRate);

    nonFiniteReplaced.store (0, std::memory_order_relaxed);
    moduleResets.store (0, std::memory_order_relaxed);
    prepared = true;
    reset();
}

void ThunderEngine::reset() noexcept
{
    transport = {};
    firstBlock = true;

    if (prepared)
    {
        pitch.reset();
        space.reset();
        dryDelay.reset();
    }
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

        processChunk (chunkChannels, chunk, ProcessContext{transport, snapshot});

        offset += chunk;
        if (offset < numSamples)
            transport = transport.advancedBy (chunk, sampleRate);
    }
}

void ThunderEngine::processChunk (float* const* channels, int numSamples, const ProcessContext& context) noexcept
{
    inputGain.apply (channels, kNumChannels, numSamples);

    // After the input gain, so bad input and gain overflow are both caught
    // before anything reaches the DSP.
    sanitise (channels, numSamples);

    // Dry path: delayed by exactly the layer's latency so the two stay aligned.
    float* dryChannels[kNumChannels] = {dry[0].data(), dry[1].data()};
    for (int ch = 0; ch < kNumChannels; ++ch)
        juce::FloatVectorOperations::copy (dryChannels[ch], channels[ch], numSamples);
    dryDelay.process (dryChannels, kNumChannels, numSamples);

    // Layer path, processed in place in the output buffer.
    juce::dsp::AudioBlock<float> layer (channels, kNumChannels, static_cast<std::size_t> (numSamples));
    runModule (pitch, layer, context);
    runModule (space, layer, context);

    for (int ch = 0; ch < kNumChannels; ++ch)
        juce::FloatVectorOperations::add (channels[ch], dryChannels[ch], numSamples);

    outputGain.apply (channels, kNumChannels, numSamples);

    // Gains can push extreme (but finite) input to Inf; never let that out.
    sanitise (channels, numSamples);
}

void ThunderEngine::runModule (Module& module, juce::dsp::AudioBlock<float>& block,
                               const ProcessContext& context) noexcept
{
    module.process (block, context);

    // A module that produced NaN/Inf has corrupted its own state (filters and
    // feedback keep recirculating it). Fail safe: reset it and output silence for
    // this chunk instead of noise (ARCHITECTURE §14).
    int bad = 0;
    for (std::size_t ch = 0; ch < block.getNumChannels(); ++ch)
        bad += util::replaceNonFinite (block.getChannelPointer (ch), static_cast<int> (block.getNumSamples()));

    if (bad > 0)
    {
        module.reset();
        block.clear();
        moduleResets.fetch_add (1, std::memory_order_relaxed);
    }
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
