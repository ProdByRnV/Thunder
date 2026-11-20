#include "PitchModule.h"

#include "core/music/KeyParams.h"

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;
using params::PitchMode;

namespace
{
int octaveShift (params::PitchOctave octave) noexcept
{
    switch (octave)
    {
        case params::PitchOctave::Down2:
            return -24;
        case params::PitchOctave::Down1:
            return -12;
        case params::PitchOctave::Up1:
            return 12;
        case params::PitchOctave::Up2:
            return 24;
    }
    return 12;
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

    tracker.prepare (spec.sampleRate);
    follower.prepare (spec.sampleRate);
    unshiftedDelay.prepare (numChannels, latencySamples);

    shift.prepare (spec.sampleRate, kShiftGlideSeconds);
    mix.prepare (spec.sampleRate, util::kDefaultRampSeconds);
    formantState = -1;
    reset();
}

void PitchModule::reset() noexcept
{
    stretch.reset();
    tracker.reset();
    follower.reset();
    unshiftedDelay.reset();
    trackerActive = false;
    firstChunk = true;
}

float PitchModule::targetShift (const ProcessContext& context, PitchMode mode) const noexcept
{
    const auto& p = context.params;

    switch (mode)
    {
        case PitchMode::Octave:
            return static_cast<float> (octaveShift (p.getChoice<params::PitchOctave> (ParamId::pitchOctave)));

        case PitchMode::Semitone:
            return static_cast<float> (p.getInt (ParamId::pitchSemitones)) + p.get (ParamId::pitchFine) / 100.0f;

        case PitchMode::Interval:
            return static_cast<float> (intervalShift);
    }

    return 0.0f;
}

void PitchModule::applyFormants (bool preserve) noexcept
{
    const auto state = preserve ? 1 : 0;
    if (state == formantState)
        return;

    // A formant factor of 1 with pitch compensation keeps the spectral envelope
    // where it was while the harmonics move.
    stretch.setFormantFactor (1.0f, preserve);
    formantState = state;
}

void PitchModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto numSamples = static_cast<int> (block.getNumSamples());
    if (numSamples == 0)
        return;

    jassert (static_cast<std::size_t> (numSamples) <= input[0].size());
    jassert (static_cast<int> (block.getNumChannels()) >= numChannels);

    const auto& p = context.params;
    key = music::keyFromParams (p);
    const auto mode = p.getChoice<PitchMode> (ParamId::pitchMode);

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

    // Interval mode follows the melody. The tracker starts fresh each time the
    // mode is entered; until it finds a note, the fixed shift applies.
    if (mode == PitchMode::Interval)
    {
        if (!trackerActive)
        {
            tracker.reset();
            follower.reset();
            trackerActive = true;
        }

        tracker.process (inputs, numChannels, numSamples);
        intervalShift =
            follower.update (tracker.getEstimate(), key, p.getChoice<params::PitchInterval> (ParamId::pitchInterval),
                             p.getChoice<params::PitchDirection> (ParamId::pitchDirection), numSamples);
    }
    else
    {
        trackerActive = false;
    }

    // The shift glides per chunk: Signalsmith applies it per analysis hop anyway.
    const auto target = targetShift (context, mode);
    const auto mixTarget = std::clamp (p.get (ParamId::pitchMix) / 100.0f, 0.0f, 1.0f);

    if (firstChunk)
    {
        shift.snapTo (target);
        mix.snapTo (mixTarget);
        firstChunk = false;
    }
    else
    {
        shift.setTarget (target);
        mix.setTarget (mixTarget);
    }

    const auto newShift = shift.skip (numSamples);
    if (!juce::exactlyEqual (newShift, appliedShift))
    {
        stretch.setTransposeSemitones (newShift);
        appliedShift = newShift;
    }

    // Formants are preserved on upward shifts only. Signalsmith's spectral
    // envelope is nearly empty below the input's fundamental, so preserving it on
    // a downward shift removes much of the new, lower fundamental (1–22 dB in
    // tests/measure/PitchModesMeasure.cpp): the low end a downward layer is for.
    // Upward is where shifting sounds "chipmunky" (PRD FR-PITCH-006). Switching
    // at 0 is seamless: there the compensation does nothing either way.
    applyFormants (p.isOn (ParamId::pitchFormant) && appliedShift > 0.0f);

    stretch.process (inputs, numSamples, outputs, numSamples);

    mixWithUnshifted (block, numSamples);
}

void PitchModule::mixWithUnshifted (juce::dsp::AudioBlock<float>& block, int numSamples) noexcept
{
    // The unshifted input, delayed by the shifter's latency. Always kept running
    // so that turning Pitch Mix down never plays stale audio.
    float* unshifted[kMaxChannels] = {};
    for (int ch = 0; ch < numChannels; ++ch)
        unshifted[ch] = input[static_cast<std::size_t> (ch)].data();
    unshiftedDelay.process (unshifted, numChannels, numSamples);

    // Fully shifted (the default): nothing to blend.
    if (!mix.isSmoothing() && mix.current() >= 1.0f)
        return;

    // Equal-power: the shifted and unshifted signals are different pitches, so
    // this keeps the level steady across the knob.
    constexpr auto halfPi = juce::MathConstants<float>::halfPi;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto amount = mix.next();
        const auto shiftedGain = std::sin (amount * halfPi);
        const auto unshiftedGain = std::cos (amount * halfPi);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* out = block.getChannelPointer (static_cast<std::size_t> (ch));
            out[i] = out[i] * shiftedGain + unshifted[ch][i] * unshiftedGain;
        }
    }
}

} // namespace thunder::dsp
