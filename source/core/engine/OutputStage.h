#pragma once

#include "core/params/ParamSnapshot.h"
#include "core/util/Smoother.h"

#include <algorithm>
#include <utility>

namespace thunder
{

// How the dry signal and the layer are combined (PRD FR-CORE-005/006,
// FR-GAIN-005, DEC-015; ARCHITECTURE §5.8 OutputStage):
// - Blend: dry × min(1, 2·(1 − mix)) + layer × min(1, 2·mix). At the default 50%
//   both are at unity, so Thunder never turns the melody down; below 50% the
//   layer fades out, above 50% the dry fades out (100% sounds like Layer Only).
// - Layer Only: the layer alone.
// The dry signal arrives already delayed by the engine's latency, so the two
// are time-aligned in both modes. Both gains glide over 20 ms, so moving Mix or
// switching mode never clicks.
//
// Auto Gain and Layer Level come before this, Output Gain and the safety
// limiter after it (ThunderEngine::processChunk).
class OutputStage
{
public:
    // The dry and layer gains for a mode and mix (0 … 1).
    static std::pair<float, float> gainsFor (params::OutputMode mode, float mix) noexcept
    {
        if (mode == params::OutputMode::LayerOnly)
            return {0.0f, 1.0f};

        const auto m = std::clamp (mix, 0.0f, 1.0f);
        return {std::min (1.0f, 2.0f * (1.0f - m)), std::min (1.0f, 2.0f * m)};
    }

    // [NRT]
    void prepare (double sampleRate) noexcept
    {
        dryGain.prepare (sampleRate, util::kDefaultRampSeconds);
        layerGain.prepare (sampleRate, util::kDefaultRampSeconds);
        reset();
    }

    // [RT]
    void reset() noexcept { firstChunk = true; }

    // [RT] layer[ch][i] = layer·layerGain + dry·dryGain, in place in `layer`.
    void process (float* const* layer, const float* const* dry, int numChannels, int numSamples,
                  const params::ParamSnapshot& snapshot) noexcept
    {
        const auto [d, l] = gainsFor (snapshot.getChoice<params::OutputMode> (params::ParamId::outputMode),
                                      snapshot.get (params::ParamId::mix) / 100.0f);
        if (firstChunk)
        {
            dryGain.snapTo (d);
            layerGain.snapTo (l);
            firstChunk = false;
        }
        else
        {
            dryGain.setTarget (d);
            layerGain.setTarget (l);
        }

        if (!dryGain.isSmoothing() && !layerGain.isSmoothing())
        {
            const auto dg = dryGain.current(), lg = layerGain.current();
            for (int ch = 0; ch < numChannels; ++ch)
            {
                if (!juce::exactlyEqual (lg, 1.0f))
                    juce::FloatVectorOperations::multiply (layer[ch], lg, numSamples);
                if (juce::exactlyEqual (dg, 1.0f))
                    juce::FloatVectorOperations::add (layer[ch], dry[ch], numSamples);
                else if (dg > 0.0f)
                    juce::FloatVectorOperations::addWithMultiply (layer[ch], dry[ch], dg, numSamples);
            }
            return;
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const auto dg = dryGain.next(), lg = layerGain.next();
            for (int ch = 0; ch < numChannels; ++ch)
                layer[ch][i] = layer[ch][i] * lg + dry[ch][i] * dg;
        }
    }

private:
    util::LinearSmoother dryGain, layerGain;
    bool firstChunk = true;
};

} // namespace thunder
