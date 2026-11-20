#include "WidthModule.h"

#include <algorithm>

namespace thunder::dsp
{

using params::ParamId;

void WidthModule::prepare (const PrepareSpec& spec)
{
    width.prepare (spec.sampleRate, util::kDefaultRampSeconds);
    reset();
}

void WidthModule::reset() noexcept
{
    firstChunk = true;
}

void WidthModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto target = std::clamp (context.params.get (ParamId::widthAmount) / 100.0f, 0.0f, 2.0f);
    if (firstChunk)
    {
        width.snapTo (target);
        firstChunk = false;
    }
    else
    {
        width.setTarget (target);
    }

    // Width needs two channels; at exactly 100% there is nothing to do.
    if (block.getNumChannels() < 2 || (!width.isSmoothing() && juce::exactlyEqual (width.current(), 1.0f)))
        return;

    auto* left = block.getChannelPointer (0);
    auto* right = block.getChannelPointer (1);
    const auto numSamples = static_cast<int> (block.getNumSamples());

    for (int i = 0; i < numSamples; ++i)
    {
        const auto w = width.next();
        const auto mid = 0.5f * (left[i] + right[i]);
        const auto side = 0.5f * (left[i] - right[i]) * w;
        left[i] = mid + side;
        right[i] = mid - side;
    }
}

} // namespace thunder::dsp
