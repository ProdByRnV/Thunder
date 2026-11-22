#include "DuckModule.h"

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;

namespace
{
float coefficientFor (double seconds, double sampleRate) noexcept
{
    return static_cast<float> (1.0 - std::exp (-1.0 / (std::max (seconds, 1.0e-4) * sampleRate)));
}
} // namespace

float DuckModule::reductionDb (float keyDb, float share) noexcept
{
    const auto loudness = std::clamp ((keyDb - kQuietKeyDb) / (kLoudKeyDb - kQuietKeyDb), 0.0f, 1.0f);
    return std::clamp (share, 0.0f, 1.0f) * kMaxReductionDb * loudness;
}

void DuckModule::prepare (const PrepareSpec& spec)
{
    sampleRate = spec.sampleRate;
    attackCoeff = coefficientFor (kAttackSeconds, sampleRate);
    peakReleaseCoeff = coefficientFor (kPeakReleaseSeconds, sampleRate);
    amount.prepare (sampleRate, util::kDefaultRampSeconds);
    releaseMs.prepare (sampleRate, util::kDefaultRampSeconds);
    reset();
}

void DuckModule::reset() noexcept
{
    envelope = 0.0f;
    lastReductionDb = 0.0f;
    firstChunk = true;
}

void DuckModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto numSamples = static_cast<int> (block.getNumSamples());
    if (numSamples <= 0 || context.dryKey == nullptr)
        return;

    const auto& p = context.params;
    // Off is Amount 0, so switching fades the ducking in and out (PRD FR-CORE-009).
    const auto amountTarget =
        p.isOn (ParamId::duckOn) ? std::clamp (p.get (ParamId::duckAmount) / 100.0f, 0.0f, 1.0f) : 0.0f;
    const auto releaseTarget = p.get (ParamId::duckRelease);
    if (firstChunk)
    {
        amount.snapTo (amountTarget);
        releaseMs.snapTo (releaseTarget);
        firstChunk = false;
    }
    else
    {
        amount.setTarget (amountTarget);
        releaseMs.setTarget (releaseTarget);
    }

    // Fully off: nothing to do (the layer passes untouched), and the envelope
    // starts from silence when it comes back on.
    if (!amount.isSmoothing() && amount.current() <= 0.0f)
    {
        releaseMs.skip (numSamples);
        envelope = 0.0f;
        lastReductionDb = 0.0f;
        return;
    }

    // Release changes slowly; its coefficient once per chunk is plenty.
    const auto releaseCoeff = coefficientFor (static_cast<double> (releaseMs.skip (numSamples)) / 1000.0, sampleRate);

    const auto channels = static_cast<int> (block.getNumChannels());
    const auto* keyLeft = context.dryKey[0];
    const auto* keyRight = context.dryKey[1] != nullptr ? context.dryKey[1] : keyLeft;

    for (int i = 0; i < numSamples; ++i)
    {
        // A fast peak detector (fills the gaps between a note's peaks)...
        const auto key = std::max (std::abs (keyLeft[i]), std::abs (keyRight[i]));
        envelope += (key > envelope ? attackCoeff : peakReleaseCoeff) * (key - envelope);

        // ... sets the reduction it wants; the reduction itself rises in 5 ms
        // and recovers with Release.
        const auto keyDb = envelope > 1.0e-6f ? 20.0f * std::log10 (envelope) : -120.0f;
        const auto wanted = reductionDb (keyDb, amount.next());
        lastReductionDb += (wanted > lastReductionDb ? attackCoeff : releaseCoeff) * (wanted - lastReductionDb);
        const auto gain = juce::Decibels::decibelsToGain (-lastReductionDb);

        for (int ch = 0; ch < channels; ++ch)
            block.getChannelPointer (static_cast<std::size_t> (ch))[i] *= gain;
    }
}

} // namespace thunder::dsp
