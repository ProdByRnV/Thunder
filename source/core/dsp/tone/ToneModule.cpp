#include "ToneModule.h"

#include <algorithm>

namespace thunder::dsp
{

using params::FilterSlope;
using params::ParamId;

namespace
{
// Damping (1/Q) of the Butterworth sections.
constexpr double kDampingSingle = 1.4142135623730951; // Q 0.7071
constexpr double kDampingFirst = 1.8477590650225735;  // Q 0.5412
constexpr double kDampingSecond = 0.7653668647301796; // Q 1.3066

constexpr double kSlopeFadeSeconds = 0.030; // ARCHITECTURE §5.5: mode crossfades ~30 ms
} // namespace

void ToneModule::Filter::setCutoff (double hz, double rate) noexcept
{
    const auto normalised = std::clamp (hz / rate, 1.0 / rate, kMaxNormalisedCutoff);
    cSingle = Svf::Coefficients::make (normalised, kDampingSingle);
    cFirst = Svf::Coefficients::make (normalised, kDampingFirst);
    cSecond = Svf::Coefficients::make (normalised, kDampingSecond);
}

void ToneModule::Filter::reset() noexcept
{
    single.reset();
    first.reset();
    second.reset();
}

float ToneModule::Filter::process (int channel, float x, float steep) noexcept
{
    const auto twoPole = single.process (channel, x, cSingle, highPass);
    const auto fourPole = second.process (channel, first.process (channel, x, cFirst, highPass), cSecond, highPass);

    // The two outputs are nearly the same signal, so a linear blend keeps the
    // level steady.
    return twoPole + steep * (fourPole - twoPole);
}

void ToneModule::prepare (const PrepareSpec& spec)
{
    sampleRate = spec.sampleRate;
    numChannels = std::clamp (spec.numChannels, 1, Svf::kMaxChannels);

    highPass.highPass = true;
    lowPass.highPass = false;
    for (auto* filter : {&highPass, &lowPass})
    {
        filter->cutoff.prepare (sampleRate, util::kFrequencyRampSeconds);
        filter->steepness.prepare (sampleRate, kSlopeFadeSeconds);
    }

    reset();
}

void ToneModule::reset() noexcept
{
    highPass.reset();
    lowPass.reset();
    firstChunk = true;
}

void ToneModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto numSamples = static_cast<int> (block.getNumSamples());
    if (numSamples == 0)
        return;

    const auto& p = context.params;
    const auto hpTarget = p.get (ParamId::hpFreq);
    const auto lpTarget = p.get (ParamId::lpFreq);
    const auto hpSteep = p.getChoice<FilterSlope> (ParamId::hpSlope) == FilterSlope::Db24 ? 1.0f : 0.0f;
    const auto lpSteep = p.getChoice<FilterSlope> (ParamId::lpSlope) == FilterSlope::Db24 ? 1.0f : 0.0f;

    if (firstChunk)
    {
        highPass.cutoff.snapTo (hpTarget);
        lowPass.cutoff.snapTo (lpTarget);
        highPass.steepness.snapTo (hpSteep);
        lowPass.steepness.snapTo (lpSteep);
        highPass.setCutoff (hpTarget, sampleRate);
        lowPass.setCutoff (lpTarget, sampleRate);
        firstChunk = false;
    }
    else
    {
        highPass.cutoff.setTarget (hpTarget);
        lowPass.cutoff.setTarget (lpTarget);
        highPass.steepness.setTarget (hpSteep);
        lowPass.steepness.setTarget (lpSteep);
    }

    const auto channels = std::min (numChannels, static_cast<int> (block.getNumChannels()));
    float* data[Svf::kMaxChannels] = {};
    for (int ch = 0; ch < channels; ++ch)
        data[ch] = block.getChannelPointer (static_cast<std::size_t> (ch));

    for (int i = 0; i < numSamples; ++i)
    {
        // Coefficients are recomputed only while a cutoff is moving.
        for (auto* filter : {&highPass, &lowPass})
            if (filter->cutoff.isSmoothing())
                filter->setCutoff (filter->cutoff.next(), sampleRate);

        const auto hpSlope = highPass.steepness.next();
        const auto lpSlope = lowPass.steepness.next();

        for (int ch = 0; ch < channels; ++ch)
            data[ch][i] = lowPass.process (ch, highPass.process (ch, data[ch][i], hpSlope), lpSlope);
    }
}

} // namespace thunder::dsp
