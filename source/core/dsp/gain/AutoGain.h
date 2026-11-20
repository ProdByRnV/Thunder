#pragma once

#include "LoudnessMeter.h"

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

// Auto Gain (PRD FR-GAIN-001, ARCHITECTURE §5.9): holds the layer at a fixed
// loudness below the dry signal, whatever the modules do to its level (formant
// preservation, sparse grains, reverb, filters).
//
// - Two LoudnessMeters, one on the aligned dry signal and one on the layer
//   before any gain.
// - The wanted correction is (dry loudness + offset) − layer loudness, clamped
//   to ±18 dB. The offset is −6 dB (PRD OQ-01: tuned by ear at the v1.0 test
//   pass).
// - The applied gain moves towards it slowly, with a 1.5 s time constant, so it
//   follows the music's level, not its notes: no pumping.
// - While either signal is below −60 dB (silence, a gap between phrases) the
//   gain holds, so tails and gaps are never boosted.
// - Switched off, the gain returns to 0 dB with a 50 ms time constant, fast
//   but click-free; switched on, it starts again from where it is.
//
// The meters run whether Auto Gain is on or off, so it is ready the moment it
// is switched on.
class AutoGain
{
public:
    static constexpr double kOffsetDb = -6.0;
    static constexpr double kRangeDb = 18.0;
    static constexpr double kGateDb = -60.0;
    static constexpr double kGatePower = 1.0e-6; // kGateDb as a mean square
    static constexpr double kFollowSeconds = 1.5;
    static constexpr double kReleaseSeconds = 0.050; // back to 0 dB when switched off

    // [NRT]
    void prepare (double sampleRate) noexcept
    {
        dry.prepare (sampleRate);
        layer.prepare (sampleRate);
        follow = 1.0 - std::exp (-1.0 / (kFollowSeconds * sampleRate));
        offset = std::pow (10.0, kOffsetDb / 20.0);
        minGain = std::pow (10.0, -kRangeDb / 20.0);
        maxGain = std::pow (10.0, kRangeDb / 20.0);
        release = 1.0 - std::exp (-1.0 / (kReleaseSeconds * sampleRate));
        reset();
    }

    // [RT]
    void reset() noexcept
    {
        dry.reset();
        layer.reset();
        gain = 1.0;
    }

    // [RT] Measures both signals and multiplies the layer by the gain, in place.
    void process (float* const* layerChannels, const float* const* dryChannels, int numChannels, int numSamples,
                  bool enabled) noexcept
    {
        const auto channels = std::min (numChannels, LoudnessMeter::kMaxChannels);
        float frame[LoudnessMeter::kMaxChannels] = {};

        for (int i = 0; i < numSamples; ++i)
        {
            for (int ch = 0; ch < channels; ++ch)
                frame[ch] = dryChannels[ch][i];
            const auto dryPower = dry.push (frame, channels);

            for (int ch = 0; ch < channels; ++ch)
                frame[ch] = layerChannels[ch][i];
            const auto layerPower = layer.push (frame, channels);

            // In the linear domain the wanted gain is √(dry power / layer power) times
            // the offset: a square root per sample, no logarithms.
            if (!enabled)
                gain += release * (1.0 - gain);
            else if (dryPower > kGatePower && layerPower > kGatePower)
                gain += follow * (std::clamp (std::sqrt (dryPower / layerPower) * offset, minGain, maxGain) - gain);

            const auto g = static_cast<float> (gain);
            for (int ch = 0; ch < channels; ++ch)
                layerChannels[ch][i] *= g;
        }
    }

    double getGainDb() const noexcept { return 20.0 * std::log10 (gain); }
    const LoudnessMeter& getDryMeter() const noexcept { return dry; }
    const LoudnessMeter& getLayerMeter() const noexcept { return layer; }

private:
    LoudnessMeter dry, layer;
    double follow = 0.0, release = 0.0;
    double offset = 1.0, minGain = 1.0, maxGain = 1.0; // linear
    double gain = 1.0;                                 // linear
};

} // namespace thunder::dsp
