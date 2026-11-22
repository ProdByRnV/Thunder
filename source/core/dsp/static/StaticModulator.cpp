#include "StaticModulator.h"

#include "core/util/Rng.h"

#include <algorithm>
#include <cmath>

namespace thunder::dsp
{

using params::ParamId;
using params::StaticCharacter;

namespace
{
constexpr double kFadeSeconds = 0.030;
constexpr float kSparkChance = 0.4f;
constexpr double kSparkDecaySteps = 0.15; // a burst falls to 1/e in this share of a step

constexpr std::array<ParamId, StaticModulator::kNumTargets> kTargetSwitches{
    ParamId::staticTgtPosition, ParamId::staticTgtPitch, ParamId::staticTgtFilter, ParamId::staticTgtPan,
    ParamId::staticTgtLevel};

// The generator for one step of one target: seeded from the step number, so a
// step's values never depend on what came before it.
util::Rng rngFor (long long step, int target, int salt) noexcept
{
    const auto seed = static_cast<std::uint64_t> (step) * 0x9e3779b97f4a7c15ULL + 0x5851f42d4c957f2dULL;
    const auto stream = static_cast<std::uint64_t> (target * 4 + salt + 1);
    return util::Rng (seed, stream);
}

float stepValue (long long step, int target) noexcept
{
    return rngFor (step, target, 0).nextBipolar();
}

// A Spark burst starting at `step`: 0 if this step has none.
float sparkAmplitude (long long step, int target) noexcept
{
    auto rng = rngFor (step, target, 1);
    if (rng.nextFloat() >= kSparkChance)
        return 0.0f;

    const auto b = rng.nextBipolar();
    return (b < 0.0f ? -1.0f : 1.0f) * (0.5f + 0.5f * std::abs (b)); // at least half way
}

float clampTo (ParamId id, float value) noexcept
{
    const auto& def = params::paramDef (id);
    return std::clamp (value, def.min, def.max);
}
} // namespace

bool StaticModulator::Frame::isZero() const noexcept
{
    return std::all_of (values.begin(), values.end(), [] (float v) { return std::abs (v) < 1.0e-6f; });
}

float StaticModulator::valueAt (StaticCharacter character, int target, double step) noexcept
{
    const auto whole = std::floor (step);
    const auto k = static_cast<long long> (whole);
    const auto f = step - whole;

    switch (character)
    {
        case StaticCharacter::Smooth:
        {
            const auto a = stepValue (k, target);
            const auto b = stepValue (k + 1, target);
            const auto s = static_cast<float> (f * f * (3.0 - 2.0 * f));
            return a + (b - a) * s;
        }

        case StaticCharacter::Stepped:
            return stepValue (k, target);

        case StaticCharacter::Spark:
        {
            // This step's burst and the tail of the previous one.
            auto v = 0.0;
            for (auto j = k - 1; j <= k; ++j)
                if (const auto a = sparkAmplitude (j, target); !juce::exactlyEqual (a, 0.0f))
                    v += a * std::exp (-(step - static_cast<double> (j)) / kSparkDecaySteps);
            return static_cast<float> (std::clamp (v, -1.0, 1.0));
        }
    }

    return 0.0f;
}

void StaticModulator::prepare (double sampleRate) noexcept
{
    clock.prepare (sampleRate);
    for (auto& d : depth)
        d.prepare (sampleRate, kFadeSeconds);
    reset();
}

void StaticModulator::reset() noexcept
{
    clock.reset();
    frame = {};
    firstChunk = true;
    wasActive = false;
}

float StaticModulator::depthTarget (int target, const params::ParamSnapshot& p) const noexcept
{
    if (!p.isOn (ParamId::staticOn) || !p.isOn (kTargetSwitches[static_cast<std::size_t> (target)]))
        return 0.0f;
    return std::clamp (p.get (ParamId::staticAmount) / 100.0f, 0.0f, 1.0f);
}

bool StaticModulator::isActive (const params::ParamSnapshot& p) const noexcept
{
    for (int t = 0; t < kNumTargets; ++t)
        if (depthTarget (t, p) > 0.0f || (!firstChunk && depth[static_cast<std::size_t> (t)].current() > 0.0f))
            return true;
    return false;
}

const StaticModulator::Frame& StaticModulator::next (const TransportState& transport, const params::ParamSnapshot& p,
                                                     std::int64_t sampleIndex) noexcept
{
    const auto active = isActive (p);
    if (!active)
    {
        // At rest. The fades snap, so the next start begins from here.
        for (int t = 0; t < kNumTargets; ++t)
            depth[static_cast<std::size_t> (t)].snapTo (0.0f);
        firstChunk = false;
        wasActive = false;
        frame = {};
        return frame;
    }

    // Between grid points the frame holds (the engine starts a chunk on each).
    const auto intoInterval = static_cast<int> (sampleIndex % kControlInterval);
    if (wasActive && intoInterval != 0)
        return frame;
    wasActive = true;

    // The frame lasts until the next grid point.
    const auto length = kControlInterval - intoInterval;
    const auto song = clock.next (transport, length);

    for (int t = 0; t < kNumTargets; ++t)
    {
        auto& d = depth[static_cast<std::size_t> (t)];
        if (firstChunk)
            d.snapTo (depthTarget (t, p));
        else
            d.setTarget (depthTarget (t, p));
    }
    firstChunk = false;

    double step = 0.0;
    if (p.isOn (ParamId::staticSync))
    {
        const auto cycle = params::rateLength (p.getChoice<params::SyncedRate> (ParamId::staticRateSynced));
        const auto quarters = cycle.bars > 0.0 ? cycle.bars * song.quarterNotesPerBar : cycle.quarters;
        step = song.ppq / quarters;
    }
    else
    {
        step = song.seconds * static_cast<double> (std::max (p.get (ParamId::staticRate), 0.001f));
    }

    const auto character = p.getChoice<StaticCharacter> (ParamId::staticCharacter);
    for (int t = 0; t < kNumTargets; ++t)
    {
        auto& d = depth[static_cast<std::size_t> (t)];
        const auto scale = d.current();
        d.skip (length);

        if (scale <= 0.0f)
        {
            frame.values[static_cast<std::size_t> (t)] = 0.0f;
            continue;
        }

        const auto raw = valueAt (character, t, step);
        const auto bipolar = t == filter || t == pan;
        frame.values[static_cast<std::size_t> (t)] = scale * (bipolar ? raw : 0.5f * (raw + 1.0f));
    }

    return frame;
}

void StaticModulator::applyTo (const Frame& f, params::ParamSnapshot& effective) noexcept
{
    if (!juce::exactlyEqual (f[position], 0.0f))
        effective.set (ParamId::grainPosSpread,
                       clampTo (ParamId::grainPosSpread,
                                effective.get (ParamId::grainPosSpread) + f[position] * kMaxPositionSpread));
    if (!juce::exactlyEqual (f[pitch], 0.0f))
        effective.set (ParamId::grainPitchSpread,
                       clampTo (ParamId::grainPitchSpread,
                                effective.get (ParamId::grainPitchSpread) + f[pitch] * kMaxPitchSpread));
    if (!juce::exactlyEqual (f[filter], 0.0f))
    {
        const auto ratio = std::exp2 (f[filter] * kMaxFilterOctaves);
        effective.set (ParamId::hpFreq, clampTo (ParamId::hpFreq, effective.get (ParamId::hpFreq) * ratio));
        effective.set (ParamId::lpFreq, clampTo (ParamId::lpFreq, effective.get (ParamId::lpFreq) * ratio));
    }
}

// ---- StaticStage ----------------------------------------------------------------------------------------------------

void StaticStage::prepare (double sampleRate) noexcept
{
    pan.prepare (sampleRate, util::kDefaultRampSeconds);
    level.prepare (sampleRate, util::kDefaultRampSeconds);
    reset();
}

void StaticStage::reset() noexcept
{
    firstChunk = true;
    pan.snapTo (0.0f);
    level.snapTo (1.0f);
}

void StaticStage::process (float* const* channels, int numSamples, const StaticModulator::Frame& frame) noexcept
{
    const auto panTarget = std::clamp (frame[StaticModulator::pan], -1.0f, 1.0f);
    const auto levelTarget =
        juce::Decibels::decibelsToGain (-StaticModulator::kMaxLevelDipDb * frame[StaticModulator::level]);

    if (firstChunk)
    {
        pan.snapTo (panTarget);
        level.snapTo (levelTarget);
        firstChunk = false;
    }
    else
    {
        pan.setTarget (panTarget);
        level.setTarget (levelTarget);
    }

    // Centred and at full level: nothing to do (the layer passes untouched).
    if (!pan.isSmoothing() && !level.isSmoothing() && juce::exactlyEqual (pan.current(), 0.0f) &&
        juce::exactlyEqual (level.current(), 1.0f))
        return;

    constexpr auto quarterPi = juce::MathConstants<float>::pi * 0.25f;
    const auto root2 = juce::MathConstants<float>::sqrt2;
    for (int i = 0; i < numSamples; ++i)
    {
        const auto angle = (pan.next() + 1.0f) * quarterPi;
        const auto g = level.next();
        channels[0][i] *= g * root2 * std::cos (angle);
        channels[1][i] *= g * root2 * std::sin (angle);
    }
}

} // namespace thunder::dsp
