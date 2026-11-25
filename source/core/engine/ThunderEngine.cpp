#include "ThunderEngine.h"

#include "core/util/Math.h"

#include <algorithm>

namespace thunder
{

using params::ParamId;

void ThunderEngine::prepare (double newSampleRate, params::Quality newQuality)
{
    jassert (newSampleRate > 0.0);
    sampleRate = newSampleRate;
    quality = newQuality;

    const PrepareSpec spec{sampleRate, kMaxChunkSize, kNumChannels, quality};
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
    performance.prepare (sampleRate);
    staticStage.prepare (sampleRate);
    duck.prepare (spec);

    limiter.prepare (sampleRate);
    latencySamples = chainLatency + limiter.getLatencySamples();

    for (auto& channel : dry)
        channel.assign (static_cast<std::size_t> (kMaxChunkSize), 0.0f);
    for (auto& channel : layerTap)
        channel.assign (static_cast<std::size_t> (kMaxChunkSize), 0.0f);
    for (auto& channel : inputTap)
        channel.assign (static_cast<std::size_t> (kMaxChunkSize), 0.0f);

    layerTapLimiter.prepare (sampleRate);
    layerTapActive = false;
    capture.prepare (sampleRate, latencySamples);
    visual.prepare (sampleRate, kMaxChunkSize, chainLatency);
    analysisTap.prepare (sampleRate);

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
        performance.reset();
        staticStage.reset();
        duck.reset();
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

    // The performance controls move the host's values (ARCHITECTURE §5.10):
    // Morph once per block; Storm, the strikes, the Riser and Variation per
    // chunk below. With none active the modules get `snapshot` itself.
    const auto& morphed = performance.apply (snapshot);
    performance.beginBlock (morphed);

    transport = hostTransport;

    // The layer tap runs only while a Layer capture is armed or recording. Its
    // limiter starts fresh; the recording starts at a bar line at least one
    // latency later, by which time the limiter's look-ahead is full.
    capture.handleRequests();
    if (capture.wantsLayer() && !layerTapActive)
        layerTapLimiter.reset();
    layerTapActive = capture.wantsLayer();
    visual.beginBlock();

    // While Static or a performance control moves anything, chunks also end on
    // the control grid, so values change at the same samples whatever the
    // host's block size.
    const auto performing = performance.isActive (morphed);
    const auto onGrid = performing || staticModulator.isActive (morphed);
    static_assert (dsp::StaticModulator::kControlInterval == perform::PerformanceStage::kControlInterval);

    const params::ParamSnapshot* last = &morphed;
    for (int offset = 0; offset < numSamples;)
    {
        auto chunk = std::min (kMaxChunkSize, numSamples - offset);
        if (onGrid)
        {
            const auto interval = dsp::StaticModulator::kControlInterval;
            chunk = std::min (chunk, interval - static_cast<int> (samplesProcessed % interval));
        }
        if (performing)
            chunk = performance.limitChunk (samplesProcessed, chunk);

        const auto& performed =
            performing ? performance.process (morphed, transport.delayedBy (chainLatency, sampleRate), samplesProcessed,
                                              chunk)
                       : morphed;
        last = &performed;

        const auto inputDb = performed.get (ParamId::inputGain);
        const auto layerDb = performed.get (ParamId::layerLevel);
        const auto outputDb = performed.get (ParamId::outputGain);
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

        float* chunkChannels[kNumChannels] = {channels[0] + offset, channels[1] + offset};
        processChunk (chunkChannels, chunk, ProcessContext{transport, performed});

        offset += chunk;
        samplesProcessed += chunk;
        if (offset < numSamples)
            transport = transport.advancedBy (chunk, sampleRate);
    }

    performance.publish (*last, last != &snapshot);
}

void ThunderEngine::processChunk (float* const* channels, int numSamples, const ProcessContext& context) noexcept
{
    // The input as the host sent it: for live key detection, and for Input captures.
    analysisTap.push (channels, numSamples);
    float* inputChannels[kNumChannels] = {inputTap[0].data(), inputTap[1].data()};
    const auto inputTapActive = capture.wantsInput();
    if (inputTapActive)
    {
        for (int ch = 0; ch < kNumChannels; ++ch)
        {
            juce::FloatVectorOperations::copy (inputChannels[ch], channels[ch], numSamples);
            util::replaceNonFinite (inputChannels[ch], numSamples);
        }
    }

    inputGain.apply (channels, kNumChannels, numSamples);

    // After the input gain, so bad input and gain overflow are both caught
    // before anything reaches the DSP.
    sanitise (channels, numSamples);

    // Dry path: delayed by exactly the layer's latency so the two stay aligned.
    float* dryChannels[kNumChannels] = {dry[0].data(), dry[1].data()};
    for (int ch = 0; ch < kNumChannels; ++ch)
        juce::FloatVectorOperations::copy (dryChannels[ch], channels[ch], numSamples);
    dryDelay.process (dryChannels, kNumChannels, numSamples);

    // context.params is the performed snapshot. Static moves its targets' values for this chunk. Everything it touches
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

    // Duck keys from the aligned dry signal, after Auto Gain (PRD DEC-051).
    juce::dsp::AudioBlock<float> layerBlock (channels, kNumChannels, static_cast<std::size_t> (numSamples));
    duck.process (layerBlock, ProcessContext{context.transport, context.params, dryChannels});

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

    capture.process (channels, layerTapActive ? tapChannels : nullptr, inputTapActive ? inputChannels : nullptr,
                     numSamples, context.transport, context.params);

    // ... and the final output, for the meters, and where the grains read.
    if (visual.isActive())
    {
        visual::ModuleActivity activity;
        activity.staticValues = staticFrame.values;
        activity.duckReductionDb = duck.getReductionDb();
        activity.shapePhase = shape.get().getLastPhase();
        activity.shapeGain = shape.get().getLastGain();
        visual.tapOutput (channels, numSamples, context.transport, limiter.getGainReductionDb(), activity);
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
