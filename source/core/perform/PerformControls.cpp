#include "PerformControls.h"

#include "core/util/Rng.h"

#include <algorithm>
#include <cmath>

namespace thunder::perform
{

using params::Kind;
using params::ParamId;
using params::paramDef;

// ---- Travel ---------------------------------------------------------------------------------------------------------

float toTravel (const params::ParamDef& def, float value) noexcept
{
    const auto v = std::clamp (value, def.min, def.max);
    if (def.taper == params::Taper::Log)
        return std::log (v / def.min) / std::log (def.max / def.min);
    return (v - def.min) / (def.max - def.min);
}

float fromTravel (const params::ParamDef& def, float travel) noexcept
{
    const auto t = std::clamp (travel, 0.0f, 1.0f);
    auto value = def.taper == params::Taper::Log ? def.min * std::pow (def.max / def.min, t)
                                                 : def.min + (def.max - def.min) * t;
    if (def.kind != Kind::Float)
        value = std::round (value);
    return std::clamp (value, def.min, def.max);
}

float moveBy (const params::ParamDef& def, float value, float delta) noexcept
{
    return fromTravel (def, toTravel (def, value) + delta);
}

// ---- Storm ----------------------------------------------------------------------------------------------------------

void applyStorm (params::ParamSnapshot& values, float storm) noexcept
{
    const auto s = std::clamp (storm / 100.0f, 0.0f, 1.0f);
    if (s <= 0.0f)
        return;

    for (const auto& curve : kStormCurves)
    {
        const auto& def = paramDef (curve.param);
        const auto delta = curve.amount * std::pow (s, curve.shape);
        values.set (curve.param, moveBy (def, values.get (curve.param), delta));
    }
}

// ---- Strikes --------------------------------------------------------------------------------------------------------

namespace
{
float lerp (float a, float b, float t) noexcept
{
    return a + (b - a) * t;
}

// Pitch an octave below where it is now, as Pitch's own controls: Octave and
// Semitone modes become a semitone shift 12 lower (Pitch's range allowing);
// an in-key interval up becomes its complement down (a 3rd up → a 6th down),
// still in key; an interval down goes to the octave below.
void diveAnOctave (params::ParamSnapshot& values, float amount) noexcept
{
    const auto mode = values.getChoice<params::PitchMode> (ParamId::pitchMode);
    if (mode == params::PitchMode::Interval)
    {
        if (amount < 0.5f) // a choice changes halfway; Pitch glides the shift
            return;
        const auto interval = values.getInt (ParamId::pitchInterval);
        const auto octave = static_cast<int> (params::enumIndex (params::PitchInterval::Octave));
        const auto up = values.getChoice<params::PitchDirection> (ParamId::pitchDirection) == params::PitchDirection::Up;
        values.set (ParamId::pitchInterval, static_cast<float> (up ? octave - interval : octave));
        values.set (ParamId::pitchDirection, static_cast<float> (params::enumIndex (params::PitchDirection::Down)));
        return;
    }

    float now = values.get (ParamId::pitchSemitones);
    if (mode == params::PitchMode::Octave)
    {
        static constexpr std::array<float, 4> kOctaveSemitones{-24.0f, -12.0f, 12.0f, 24.0f};
        now = kOctaveSemitones[static_cast<std::size_t> (std::clamp (values.getInt (ParamId::pitchOctave), 0, 3))];
        values.set (ParamId::pitchFine, 0.0f); // octaves are exact; a Fine left from Semitone mode mustn't detune
    }

    const auto& def = paramDef (ParamId::pitchSemitones);
    const auto target = std::max (def.min, now - 12.0f);
    values.set (ParamId::pitchMode, static_cast<float> (params::enumIndex (params::PitchMode::Semitone)));
    values.set (ParamId::pitchSemitones, std::clamp (std::round (lerp (now, target, amount)), def.min, def.max));
}
} // namespace

void applyThunderclap (params::ParamSnapshot& values, float amount) noexcept
{
    const auto t = std::clamp (amount, 0.0f, 1.0f);
    if (t <= 0.0f)
        return;

    diveAnOctave (values, t);

    const auto mix = values.get (ParamId::spaceMix);
    const auto decay = values.get (ParamId::spaceDecay);
    values.set (ParamId::spaceMix, lerp (mix, std::max (mix, kClapSpaceMix), t));
    values.set (ParamId::spaceDecay, lerp (decay, std::max (decay, kClapSpaceDecay), t));
}

void applyScatter (params::ParamSnapshot& values, float amount) noexcept
{
    const auto t = std::clamp (amount, 0.0f, 1.0f);
    if (t <= 0.0f)
        return;

    for (const auto id : {ParamId::grainPosSpread, ParamId::grainPitchSpread, ParamId::grainPanSpread})
        values.set (id, lerp (values.get (id), paramDef (id).max, t));
}

// ---- Variation ------------------------------------------------------------------------------------------------------

std::array<float, kVariationRanges.size()> variationForBar (std::int64_t bar) noexcept
{
    // A seed made from the bar number alone (FR-PLAY-005), on a stream of its own.
    constexpr std::uint64_t kStream = 0x5641524941ull; // "VARIA"
    util::Rng rng (static_cast<std::uint64_t> (bar) * 0x9e3779b97f4a7c15ull + 0x1234567ull, kStream);

    std::array<float, kVariationRanges.size()> changes{};
    for (auto& c : changes)
        c = rng.nextBipolar();
    return changes;
}

void applyVariation (params::ParamSnapshot& values, float variation, std::int64_t bar, float glide) noexcept
{
    const auto v = std::clamp (variation / 100.0f, 0.0f, 1.0f);
    if (v <= 0.0f)
        return;

    const auto now = variationForBar (bar);
    const auto g = std::clamp (glide, 0.0f, 1.0f);
    const auto before = g < 1.0f ? variationForBar (bar - 1) : now;

    for (std::size_t i = 0; i < kVariationRanges.size(); ++i)
    {
        const auto& range = kVariationRanges[i];
        const auto change = lerp (before[i], now[i], g) * range.amount * v;
        values.set (range.param, moveBy (paramDef (range.param), values.get (range.param), change));
    }
}

// ---- Riser ----------------------------------------------------------------------------------------------------------

int riserBars (const params::ParamSnapshot& values) noexcept
{
    static constexpr std::array<int, 4> kBars{1, 2, 4, 8};
    return kBars[static_cast<std::size_t> (std::clamp (values.getInt (ParamId::riserBars), 0, 3))];
}

double riserEnd (double ppq, double barStart, double quarterNotesPerBar, int bars) noexcept
{
    const auto barLength = std::max (quarterNotesPerBar, 1.0e-6);
    const auto barsIn = (ppq - barStart) / barLength; // 0 … 1 within the bar
    const auto nearest = barStart + (barsIn < 0.5 ? 0.0 : barLength);
    return nearest + static_cast<double> (bars) * barLength;
}

} // namespace thunder::perform
