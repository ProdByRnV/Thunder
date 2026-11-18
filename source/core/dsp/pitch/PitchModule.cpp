#include "PitchModule.h"

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;

namespace
{
// Slow enough to glide rather than jump when the shift is automated.
constexpr double kShiftRampSeconds = 0.05;

float targetShift (const params::ParamSnapshot& p) noexcept
{
    return static_cast<float> (p.getInt (ParamId::pitchSemitones)) + p.get (ParamId::pitchFine) / 100.0f;
}
} // namespace

PitchModule::PitchModule (PitchEngineConfig engineConfig)
    : config (engineConfig)
{
}

void PitchModule::prepare (const PrepareSpec& spec)
{
    numChannels = std::clamp (spec.numChannels, 1, kMaxChannels);

    const auto blockSamples = static_cast<int> (std::lround (spec.sampleRate * config.blockSeconds));
    const auto intervalSamples = static_cast<int> (std::lround (spec.sampleRate * config.intervalSeconds));
    stretch.configure (numChannels, blockSamples, intervalSamples, config.splitComputation);

    latencySamples = stretch.inputLatency() + stretch.outputLatency();

    for (auto& channel : input)
        channel.assign (static_cast<std::size_t> (spec.maxBlockSize), 0.0f);

    shift.prepare (spec.sampleRate, kShiftRampSeconds);
    reset();
}

void PitchModule::reset() noexcept
{
    stretch.reset();
    firstChunk = true;
}

void PitchModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto numSamples = static_cast<int> (block.getNumSamples());
    if (numSamples == 0)
        return;

    jassert (static_cast<std::size_t> (numSamples) <= input[0].size());
    jassert (static_cast<int> (block.getNumChannels()) >= numChannels);

    // The shift glides per chunk: Signalsmith applies it per analysis hop anyway.
    const auto target = targetShift (context.params);
    if (firstChunk)
    {
        shift.snapTo (target);
        firstChunk = false;
    }
    else
    {
        shift.setTarget (target);
    }

    const auto newShift = shift.skip (numSamples);
    if (!juce::exactlyEqual (newShift, appliedShift))
    {
        stretch.setTransposeSemitones (newShift);
        appliedShift = newShift;
    }

    // Signalsmith reads its input and writes its output separately.
    float* inputs[kMaxChannels] = {};
    float* outputs[kMaxChannels] = {};
    for (int ch = 0; ch < numChannels; ++ch)
    {
        const auto index = static_cast<std::size_t> (ch);
        outputs[ch] = block.getChannelPointer (index);
        std::copy_n (outputs[ch], numSamples, input[index].data());
        inputs[ch] = input[index].data();
    }

    stretch.process (inputs, numSamples, outputs, numSamples);
}

} // namespace thunder::dsp
