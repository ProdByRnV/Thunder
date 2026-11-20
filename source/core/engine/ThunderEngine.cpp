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
    for (auto* module : chain())
        module->prepare (spec);

    // The dry path matches the layer chain; the limiter after the mix delays both.
    auto chainLatency = 0;
    for (auto* module : chain())
        chainLatency += module->getLatencySamples();
    dryDelay.prepare (kNumChannels, chainLatency);

    limiter.prepare (sampleRate);
    latencySamples = chainLatency + limiter.getLatencySamples();

    for (auto& channel : dry)
        channel.assign (static_cast<std::size_t> (kMaxChunkSize), 0.0f);

    inputGain.prepare (sampleRate);
    layerLevel.prepare (sampleRate);
    outputGain.prepare (sampleRate);
    autoGain.prepare (sampleRate);
    output.prepare (sampleRate);

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
        for (auto* module : chain())
            module->reset();
        dryDelay.reset();
        autoGain.reset();
        output.reset();
        limiter.reset();
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
    const auto layerDb = snapshot.get (ParamId::layerLevel);
    const auto outputDb = snapshot.get (ParamId::outputGain);

    if (firstBlock)
    {
        inputGain.snapToDecibels (inputDb);
        layerLevel.snapToDecibels (layerDb);
        outputGain.snapToDecibels (outputDb);
        firstBlock = false;
    }
    else
    {
        inputGain.setTargetDecibels (inputDb);
        layerLevel.setTargetDecibels (layerDb);
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
    for (auto* module : chain())
        runModule (*module, layer, context);

    // Gain staging (ARCHITECTURE §5.8 OutputStage): Auto Gain holds the layer
    // below the dry signal, then Layer Level, then Output Mode and Mix combine
    // the layer with the aligned dry signal, then Output Gain.
    autoGain.process (channels, dryChannels, kNumChannels, numSamples, context.params.isOn (ParamId::autoGain));
    layerLevel.apply (channels, kNumChannels, numSamples);
    output.process (channels, dryChannels, kNumChannels, numSamples, context.params);
    outputGain.apply (channels, kNumChannels, numSamples);

    // Gains can push extreme (but finite) input to Inf; never let that reach
    // the limiter or the output.
    sanitise (channels, numSamples);

    // Nothing leaves above −0.3 dBFS.
    limiter.process (channels, kNumChannels, numSamples, context.params.isOn (ParamId::safetyLimiter));
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
