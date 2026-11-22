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
    chainLatency = 0;
    const auto modules = chain();
    for (std::size_t i = 0; i < modules.size(); ++i)
    {
        upstreamLatency[i] = chainLatency;
        chainLatency += modules[i]->getLatencySamples();
    }
    dryDelay.prepare (kNumChannels, chainLatency);
    staticModulator.prepare (sampleRate);
    staticStage.prepare (sampleRate);

    limiter.prepare (sampleRate);
    latencySamples = chainLatency + limiter.getLatencySamples();

    for (auto& channel : dry)
        channel.assign (static_cast<std::size_t> (kMaxChunkSize), 0.0f);
    for (auto& channel : layerTap)
        channel.assign (static_cast<std::size_t> (kMaxChunkSize), 0.0f);

    layerTapLimiter.prepare (sampleRate);
    layerTapActive = false;
    capture.prepare (sampleRate, latencySamples);
    visual.prepare (sampleRate, kMaxChunkSize, chainLatency);

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
    samplesProcessed = 0;

    if (prepared)
    {
        for (auto* module : chain())
            module->reset();
        dryDelay.reset();
        staticModulator.reset();
        staticStage.reset();
        autoGain.reset();
        output.reset();
        limiter.reset();
        layerTapLimiter.reset();
        capture.reset();
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

    // The layer tap runs only while a Layer capture is armed or recording. Its
    // limiter starts fresh; the recording starts at a bar line at least one
    // latency later, by which time the limiter's look-ahead is full.
    capture.handleRequests();
    if (capture.wantsLayer() && !layerTapActive)
        layerTapLimiter.reset();
    layerTapActive = capture.wantsLayer();
    visual.beginBlock();

    // While Static moves anything, chunks also end on its control grid, so its
    // values change at the same samples whatever the host's block size.
    const auto staticActive = staticModulator.isActive (snapshot);

    for (int offset = 0; offset < numSamples;)
    {
        auto chunk = std::min (kMaxChunkSize, numSamples - offset);
        if (staticActive)
        {
            const auto interval = dsp::StaticModulator::kControlInterval;
            chunk = std::min (chunk, interval - static_cast<int> (samplesProcessed % interval));
        }
        float* chunkChannels[kNumChannels] = {channels[0] + offset, channels[1] + offset};

        processChunk (chunkChannels, chunk, ProcessContext{transport, snapshot});

        offset += chunk;
        samplesProcessed += chunk;
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

    // Static moves its targets' values for this chunk. Everything it touches
    // comes after the chain's latency (only Pitch has any), so it follows the
    // position of the chain's output.
    const auto& staticFrame =
        staticModulator.next (context.transport.delayedBy (chainLatency, sampleRate), context.params, samplesProcessed);
    effective = context.params;
    if (!staticFrame.isZero())
        dsp::StaticModulator::applyTo (staticFrame, effective);

    // Layer path, processed in place in the output buffer. Each module gets the
    // song position of the audio it processes.
    juce::dsp::AudioBlock<float> layer (channels, kNumChannels, static_cast<std::size_t> (numSamples));
    const auto modules = chain();
    auto moduleTransport = context.transport;
    auto transportLatency = 0;
    for (std::size_t i = 0; i < modules.size(); ++i)
    {
        if (upstreamLatency[i] != transportLatency)
        {
            transportLatency = upstreamLatency[i];
            moduleTransport = context.transport.delayedBy (transportLatency, sampleRate);
        }
        runModule (*modules[i], layer, ProcessContext{moduleTransport, effective});
    }

    // Gain staging (ARCHITECTURE §5.8 OutputStage): Auto Gain holds the layer
    // below the dry signal, then Layer Level and Static's pan and level, then
    // Output Mode and Mix combine the layer with the aligned dry signal, then
    // Output Gain.
    autoGain.process (channels, dryChannels, kNumChannels, numSamples, context.params.isOn (ParamId::autoGain));
    layerLevel.apply (channels, kNumChannels, numSamples);
    staticStage.process (channels, numSamples, staticFrame);

    // The visualiser sees the aligned dry signal and the layer before the mix.
    if (visual.isActive())
        visual.tapLayer (channels, dryChannels, numSamples);

    float* tapChannels[kNumChannels] = {layerTap[0].data(), layerTap[1].data()};
    if (layerTapActive)
        for (int ch = 0; ch < kNumChannels; ++ch)
            juce::FloatVectorOperations::copy (tapChannels[ch], channels[ch], numSamples);

    output.process (channels, dryChannels, kNumChannels, numSamples, context.params);

    // One smoother for both, so the tap gets exactly the output's gain curve.
    float* gained[2 * kNumChannels] = {channels[0], channels[1], tapChannels[0], tapChannels[1]};
    outputGain.apply (gained, layerTapActive ? 2 * kNumChannels : kNumChannels, numSamples);

    // Gains can push extreme (but finite) input to Inf; never let that reach
    // the limiter or the output.
    sanitise (channels, numSamples);

    // Nothing leaves above −0.3 dBFS.
    const auto limiterOn = context.params.isOn (ParamId::safetyLimiter);
    limiter.process (channels, kNumChannels, numSamples, limiterOn);

    if (layerTapActive)
    {
        for (auto* tap : tapChannels)
            util::replaceNonFinite (tap, numSamples);
        layerTapLimiter.process (tapChannels, kNumChannels, numSamples, limiterOn);
    }

    capture.process (channels, layerTapActive ? tapChannels : nullptr, numSamples, context.transport, context.params);

    // ... and the final output, for the meters, and where the grains read.
    if (visual.isActive())
    {
        visual.tapOutput (channels, numSamples, context.transport, limiter.getGainReductionDb());
        visual.tapGrains (grains.get(), grains.isActive(), numSamples);
    }
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
