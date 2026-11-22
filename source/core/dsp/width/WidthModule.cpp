#include "WidthModule.h"

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;

namespace
{
constexpr double kAutopanFadeSeconds = 0.030;
}

void WidthModule::prepare (const PrepareSpec& spec)
{
    width.prepare (spec.sampleRate, util::kDefaultRampSeconds);
    panDepth.prepare (spec.sampleRate, kAutopanFadeSeconds);
    clock.prepare (spec.sampleRate);
    reset();
}

void WidthModule::reset() noexcept
{
    clock.reset();
    firstChunk = true;
    lastPan = 0.0f;
}

void WidthModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto& p = context.params;
    const auto target = std::clamp (p.get (ParamId::widthAmount) / 100.0f, 0.0f, 2.0f);
    const auto depthTarget =
        p.isOn (ParamId::autopanOn) ? std::clamp (p.get (ParamId::autopanDepth) / 100.0f, 0.0f, 1.0f) : 0.0f;
    if (firstChunk)
    {
        width.snapTo (target);
        panDepth.snapTo (depthTarget);
        firstChunk = false;
    }
    else
    {
        width.setTarget (target);
        panDepth.setTarget (depthTarget);
    }

    // Width needs two channels.
    if (block.getNumChannels() < 2)
        return;

    auto* left = block.getChannelPointer (0);
    auto* right = block.getChannelPointer (1);
    const auto numSamples = static_cast<int> (block.getNumSamples());

    // At exactly 100% there is nothing to do.
    if (width.isSmoothing() || !juce::exactlyEqual (width.current(), 1.0f))
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const auto w = width.next();
            const auto mid = 0.5f * (left[i] + right[i]);
            const auto side = 0.5f * (left[i] - right[i]) * w;
            left[i] = mid + side;
            right[i] = mid - side;
        }
    }

    autopan (left, right, numSamples, context);
}

void WidthModule::autopan (float* left, float* right, int numSamples, const ProcessContext& context) noexcept
{
    // The clock keeps time even while Autopan is off, so switching it on picks
    // up the song's phase.
    const auto position = clock.next (context.transport, numSamples);
    if (!panDepth.isSmoothing() && panDepth.current() <= 0.0f)
    {
        lastPan = 0.0f;
        return;
    }

    const auto length = params::rateLength (context.params.getChoice<params::SyncedRate> (ParamId::autopanRate));
    const auto cycleQuarters = length.bars > 0.0 ? length.bars * position.quarterNotesPerBar : length.quarters;

    constexpr auto quarterPi = juce::MathConstants<float>::pi * 0.25f;
    const auto root2 = juce::MathConstants<float>::sqrt2;
    for (int i = 0; i < numSamples; ++i)
    {
        const auto ppq = position.ppq + i / position.samplesPerQuarter;
        const auto phase = ppq / cycleQuarters - std::floor (ppq / cycleQuarters);
        const auto pan = panDepth.next() * static_cast<float> (std::sin (juce::MathConstants<double>::twoPi * phase));
        const auto angle = (pan + 1.0f) * quarterPi;
        left[i] *= root2 * std::cos (angle);
        right[i] *= root2 * std::sin (angle);
        lastPan = pan;
    }
}

} // namespace thunder::dsp
