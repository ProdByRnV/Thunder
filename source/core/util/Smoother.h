#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

// Parameter smoothing (PRD FR-CORE-008, ARCHITECTURE §13.1).
//
// Every continuous parameter is smoothed so automation and knob moves never
// produce zipper noise or clicks. The audio thread sets a new target once per
// block from the parameter snapshot, then reads one value per sample (or skips
// ahead per chunk for slowly-changing values).
//
// - LinearSmoother: straight-line ramp. For amounts, mixes and linear gains.
// - FrequencySmoother: ramps in equal ratios (multiplicative), so a filter sweep
//   sounds even across octaves. Values must be > 0.
// - GainSmoother: takes decibels, ramps the linear gain, and applies it to audio.

namespace thunder::util
{

inline constexpr double kDefaultRampSeconds = 0.020;   // most parameters
inline constexpr double kFrequencyRampSeconds = 0.050; // cutoffs and rates

template<typename SmoothingType> class Smoother
{
public:
    // [NRT] Sets the ramp time and jumps to the target (no ramp in progress).
    void prepare (double sampleRate, double rampSeconds) noexcept { value.reset (sampleRate, rampSeconds); }

    // [RT] Starts a ramp from the current value to `target` (no-op if unchanged).
    void setTarget (float target) noexcept { value.setTargetValue (target); }

    // [RT] Jumps straight to `v`, cancelling any ramp. For prepare()/reset() and
    // for the first block, where there is nothing to ramp from.
    void snapTo (float v) noexcept { value.setCurrentAndTargetValue (v); }

    // [RT] Advances one sample and returns the new value.
    float next() noexcept { return value.getNextValue(); }

    // [RT] Advances `numSamples` samples and returns the value reached.
    float skip (int numSamples) noexcept { return value.skip (numSamples); }

    float current() const noexcept { return value.getCurrentValue(); }
    float target() const noexcept { return value.getTargetValue(); }
    bool isSmoothing() const noexcept { return value.isSmoothing(); }

private:
    juce::SmoothedValue<float, SmoothingType> value;
};

using LinearSmoother = Smoother<juce::ValueSmoothingTypes::Linear>;
using FrequencySmoother = Smoother<juce::ValueSmoothingTypes::Multiplicative>;

class GainSmoother
{
public:
    // [NRT] Sets the ramp time and jumps to the target gain.
    void prepare (double sampleRate, double rampSeconds = kDefaultRampSeconds) noexcept
    {
        gain.prepare (sampleRate, rampSeconds);
    }

    // [RT]
    void setTargetDecibels (float decibels) noexcept { gain.setTarget (juce::Decibels::decibelsToGain (decibels)); }

    // [RT]
    void snapToDecibels (float decibels) noexcept { gain.snapTo (juce::Decibels::decibelsToGain (decibels)); }

    float currentGain() const noexcept { return gain.current(); }
    bool isSmoothing() const noexcept { return gain.isSmoothing(); }

    // [RT] Multiplies `numSamples` samples of every channel by the smoothed gain.
    void apply (float* const* channels, int numChannels, int numSamples) noexcept
    {
        if (!gain.isSmoothing())
        {
            const auto g = gain.current();

            if (!juce::approximatelyEqual (g, 1.0f))
                for (int ch = 0; ch < numChannels; ++ch)
                    juce::FloatVectorOperations::multiply (channels[ch], g, numSamples);

            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const auto g = gain.next();

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= g;
        }
    }

private:
    LinearSmoother gain;
};

} // namespace thunder::util
