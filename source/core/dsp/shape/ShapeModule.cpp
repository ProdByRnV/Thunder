#include "ShapeModule.h"

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;
using params::ShapeType;

namespace
{
constexpr double kMinSmoothSeconds = 0.0015;
constexpr double kMaxSmoothShare = 0.5;
constexpr double kGainSmoothingSeconds = 0.001;

double fraction (double x) noexcept
{
    return x - std::floor (x);
}
} // namespace

float ShapeModule::shapeAt (ShapeType shape, double phase) noexcept
{
    const auto p = fraction (phase);

    switch (shape)
    {
        case ShapeType::Pump:
        {
            const auto rest = 1.0 - p;
            return static_cast<float> (1.0 - rest * rest * rest);
        }
        case ShapeType::Swell:
        {
            const auto s = std::sin (juce::MathConstants<double>::halfPi * p);
            return static_cast<float> (s * s);
        }
        case ShapeType::Gate:
            return p < 0.5 ? 1.0f : 0.0f;
        case ShapeType::RampUp:
            return static_cast<float> (p);
        case ShapeType::RampDown:
            return static_cast<float> (1.0 - p);
        case ShapeType::Chop:
            return fraction (4.0 * p) < 0.6 ? 1.0f : 0.0f;
    }

    return 1.0f;
}

void ShapeModule::prepare (const PrepareSpec& spec)
{
    sampleRate = spec.sampleRate;
    numChannels = std::max (1, spec.numChannels);
    clock.prepare (sampleRate);
    depth.prepare (sampleRate, util::kDefaultRampSeconds);
    smoothing.prepare (sampleRate, util::kDefaultRampSeconds);
    gainCoeff = static_cast<float> (1.0 - std::exp (-1.0 / (kGainSmoothingSeconds * sampleRate)));

    // The running integral of each shape, sampled at the midpoint of each step.
    for (int s = 0; s < kNumShapes; ++s)
    {
        auto& table = integrals[static_cast<std::size_t> (s)];
        table.assign (kTableSize + 1, 0.0);
        for (int i = 0; i < kTableSize; ++i)
            table[static_cast<std::size_t> (i + 1)] =
                table[static_cast<std::size_t> (i)] +
                shapeAt (static_cast<ShapeType> (s), (i + 0.5) / kTableSize) / static_cast<double> (kTableSize);
    }

    reset();
}

void ShapeModule::reset() noexcept
{
    clock.reset();
    firstChunk = true;
}

double ShapeModule::integralAt (int shape, double x) const noexcept
{
    const auto& table = integrals[static_cast<std::size_t> (shape)];
    const auto whole = std::floor (x);
    const auto position = (x - whole) * kTableSize;
    const auto index = std::clamp (static_cast<int> (position), 0, kTableSize - 1);
    const auto t = position - index;
    const auto within = table[static_cast<std::size_t> (index)] +
                        t * (table[static_cast<std::size_t> (index + 1)] - table[static_cast<std::size_t> (index)]);
    return whole * table[kTableSize] + within;
}

float ShapeModule::smoothedAt (ShapeType shape, double phase, double width) const noexcept
{
    const auto s = static_cast<int> (params::enumIndex (shape));
    const auto w = std::max (width, 1.0e-6);
    return static_cast<float> ((integralAt (s, phase + 0.5 * w) - integralAt (s, phase - 0.5 * w)) / w);
}

void ShapeModule::process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept
{
    const auto numSamples = static_cast<int> (block.getNumSamples());
    if (numSamples <= 0)
        return;

    const auto& p = context.params;
    const auto shape = p.getChoice<ShapeType> (ParamId::shapeType);
    const auto rate = params::shapeRateToSynced (static_cast<std::size_t> (p.getInt (ParamId::shapeRate)));
    const auto length = params::rateLength (rate);
    const auto depthTarget = std::clamp (p.get (ParamId::shapeDepth) / 100.0f, 0.0f, 1.0f);
    const auto smoothTarget = std::clamp (p.get (ParamId::shapeSmooth) / 100.0f, 0.0f, 1.0f);

    const auto position = clock.next (context.transport, numSamples);
    const auto barLength = position.quarterNotesPerBar;
    const auto cycleQuarters = length.bars > 0.0 ? length.bars * barLength : length.quarters;
    const auto minWidth = kMinSmoothSeconds * sampleRate / (cycleQuarters * position.samplesPerQuarter);

    if (firstChunk)
    {
        depth.snapTo (depthTarget);
        smoothing.snapTo (smoothTarget);
    }
    else
    {
        depth.setTarget (depthTarget);
        smoothing.setTarget (smoothTarget);
    }

    const auto channels = static_cast<int> (block.getNumChannels());
    for (int i = 0; i < numSamples; ++i)
    {
        const auto ppq = position.ppq + i / position.samplesPerQuarter;
        const auto barStart = position.barStartAt (ppq);
        const auto intoBar = (ppq - barStart) / barLength;

        double phase = 0.0;
        if (length.bars > 0.0)
        {
            // Which bar of the cycle this is, counting from the song start.
            const auto cycleBars = static_cast<long long> (std::lround (length.bars));
            auto bar = std::llround (barStart / barLength) % cycleBars;
            if (bar < 0)
                bar += cycleBars;
            phase = (static_cast<double> (bar) + intoBar) / length.bars;
        }
        else
        {
            phase = fraction ((ppq - barStart) / cycleQuarters);
        }

        const auto width = std::max (minWidth, static_cast<double> (smoothing.next()) * kMaxSmoothShare);
        const auto level = smoothedAt (shape, phase, width);
        const auto target = 1.0f - depth.next() * (1.0f - level);

        if (firstChunk)
        {
            gain = target;
            firstChunk = false;
        }
        gain += gainCoeff * (target - gain);
        lastPhase = static_cast<float> (phase);

        for (int ch = 0; ch < channels; ++ch)
            block.getChannelPointer (static_cast<std::size_t> (ch))[i] *= gain;
    }
}

} // namespace thunder::dsp
