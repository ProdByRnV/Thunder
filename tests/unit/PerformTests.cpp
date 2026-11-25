// The performance controls (PRD FR-PLAY-001, 003, 004, 005; DEC-041; ROADMAP
// T-0.9-14…17): Storm's curves, the strikes, Variation and the Riser, as pure
// functions and through the engine (exact at rest, click-free, a bounce sounds
// like playback, the Riser's peak on the bar line).

#include "support/TestSignals.h"

#include "core/engine/ThunderEngine.h"
#include "core/perform/PerformControls.h"
#include "core/perform/PerformanceStage.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>

using namespace thunder;
using Catch::Matchers::WithinAbs;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
constexpr double kSampleRate = 48000.0;

bool sameValues (const ParamSnapshot& a, const ParamSnapshot& b)
{
    for (std::size_t i = 0; i < params::kNumParams; ++i)
        if (!juce::exactlyEqual (a.values[i], b.values[i]))
            return false;
    return true;
}

bool inRange (const ParamSnapshot& s)
{
    for (const auto& def : params::kParamDefs)
    {
        const auto v = s.get (def.param);
        if (v < def.min || v > def.max || !std::isfinite (v))
            return false;
    }
    return true;
}

ParamSnapshot withEverythingAt (bool max)
{
    auto s = ParamSnapshot::defaults();
    for (const auto& def : params::kParamDefs)
        if (def.kind == params::Kind::Float)
            s.set (def.param, max ? def.max : def.min);
    return s;
}
} // namespace

// ---- Storm (T-0.9-14) -----------------------------------------------------------------------------------------------

TEST_CASE ("Storm at 0% leaves the sound exactly as the knobs set it", "[unit][perform][storm]")
{
    const auto knobs = ParamSnapshot::defaults();
    auto s = knobs;
    perform::applyStorm (s, 0.0f);
    CHECK (sameValues (s, knobs));
}

TEST_CASE ("Storm moves its controls up along its curves and stays in range", "[unit][perform][storm]")
{
    const auto knobs = ParamSnapshot::defaults();
    auto half = knobs, full = knobs;
    perform::applyStorm (half, 50.0f);
    perform::applyStorm (full, 100.0f);

    for (const auto& curve : perform::kStormCurves)
    {
        INFO (params::paramDef (curve.param).id.data());
        CHECK (half.get (curve.param) >= knobs.get (curve.param));
        CHECK (full.get (curve.param) >= half.get (curve.param));
        CHECK (full.get (curve.param) > knobs.get (curve.param));
    }
    CHECK (inRange (full));

    // Everything else is untouched.
    for (const auto& def : params::kParamDefs)
    {
        const auto moved = std::any_of (perform::kStormCurves.begin(), perform::kStormCurves.end(),
                                        [&] (const auto& c) { return c.param == def.param; });
        if (!moved)
            CHECK (juce::exactlyEqual (full.get (def.param), knobs.get (def.param)));
    }

    // Knobs already at the top stay there.
    auto top = withEverythingAt (true);
    perform::applyStorm (top, 100.0f);
    CHECK (inRange (top));
    CHECK (juce::exactlyEqual (top.get (ParamId::spaceMix), 100.0f));

    // Layer Level rises 3 dB at 100%.
    CHECK_THAT (full.get (ParamId::layerLevel) - knobs.get (ParamId::layerLevel), WithinAbs (3.0, 1e-3));
}

TEST_CASE ("Travel follows each control's taper", "[unit][perform]")
{
    const auto& lp = params::paramDef (ParamId::lpFreq);
    const auto& mix = params::paramDef (ParamId::spaceMix);
    CHECK_THAT (perform::toTravel (mix, 25.0f), WithinAbs (0.25, 1e-6));
    CHECK_THAT (perform::fromTravel (lp, 0.5f), WithinAbs (std::sqrt (1000.0 * 20000.0), 0.5));
    for (const auto v : {1000.0f, 3000.0f, 16000.0f})
        CHECK_THAT (perform::fromTravel (lp, perform::toTravel (lp, v)), WithinAbs (v, v * 1e-4));
    CHECK (juce::exactlyEqual (perform::moveBy (mix, 90.0f, 0.5f), 100.0f)); // clamped
}

// ---- Strikes (T-0.9-15) ---------------------------------------------------------------------------------------------

TEST_CASE ("Thunderclap dives the layer an octave and swells the reverb", "[unit][perform][strikes]")
{
    auto at = [] (auto setup, float amount)
    {
        auto s = ParamSnapshot::defaults();
        setup (s);
        perform::applyThunderclap (s, amount);
        return s;
    };
    const auto semitoneMode = static_cast<float> (params::enumIndex (params::PitchMode::Semitone));

    // Octave +1 (the default) → no shift; the mode becomes a semitone shift.
    const auto octave = at ([] (ParamSnapshot&) {}, 1.0f);
    CHECK (juce::exactlyEqual (octave.get (ParamId::pitchMode), semitoneMode));
    CHECK (juce::exactlyEqual (octave.get (ParamId::pitchSemitones), 0.0f));
    CHECK (juce::exactlyEqual (octave.get (ParamId::pitchFine), 0.0f));
    CHECK (octave.get (ParamId::spaceMix) >= perform::kClapSpaceMix);
    CHECK (octave.get (ParamId::spaceDecay) >= perform::kClapSpaceDecay);

    // Halfway in: halfway down, in whole semitones.
    const auto halfway = at ([] (ParamSnapshot&) {}, 0.5f);
    CHECK (juce::exactlyEqual (halfway.get (ParamId::pitchSemitones), 6.0f));

    // Semitones: 12 lower, never below the range.
    const auto up5 = at (
        [&] (ParamSnapshot& s)
        {
            s.set (ParamId::pitchMode, semitoneMode);
            s.set (ParamId::pitchSemitones, 5.0f);
        },
        1.0f);
    CHECK (juce::exactlyEqual (up5.get (ParamId::pitchSemitones), -7.0f));
    const auto low = at (
        [&] (ParamSnapshot& s)
        {
            s.set (ParamId::pitchMode, semitoneMode);
            s.set (ParamId::pitchSemitones, -20.0f);
        },
        1.0f);
    CHECK (juce::exactlyEqual (low.get (ParamId::pitchSemitones), -24.0f));

    // An interval up becomes its complement down, still in key: a 3rd up → a 6th down.
    const auto interval = at (
        [] (ParamSnapshot& s)
        {
            s.set (ParamId::pitchMode, static_cast<float> (params::enumIndex (params::PitchMode::Interval)));
            s.set (ParamId::pitchInterval, static_cast<float> (params::enumIndex (params::PitchInterval::Third)));
        },
        1.0f);
    CHECK (interval.getChoice<params::PitchInterval> (ParamId::pitchInterval) == params::PitchInterval::Sixth);
    CHECK (interval.getChoice<params::PitchDirection> (ParamId::pitchDirection) == params::PitchDirection::Down);

    // Not pressed: nothing changes.
    CHECK (sameValues (at ([] (ParamSnapshot&) {}, 0.0f), ParamSnapshot::defaults()));
}

TEST_CASE ("Scatter sends the grain spreads to their maximum", "[unit][perform][strikes]")
{
    auto s = ParamSnapshot::defaults();
    perform::applyScatter (s, 1.0f);
    for (const auto id : {ParamId::grainPosSpread, ParamId::grainPitchSpread, ParamId::grainPanSpread})
        CHECK (juce::exactlyEqual (s.get (id), params::paramDef (id).max));

    auto half = ParamSnapshot::defaults();
    perform::applyScatter (half, 0.5f);
    CHECK_THAT (half.get (ParamId::grainPanSpread), WithinAbs ((40.0 + 100.0) / 2.0, 1e-4));
}

// ---- Variation (T-0.9-17) -------------------------------------------------------------------------------------------

TEST_CASE ("Variation changes the grains bar by bar, the same way for the same bar", "[unit][perform][variation]")
{
    const auto knobs = ParamSnapshot::defaults();

    auto none = knobs;
    perform::applyVariation (none, 0.0f, 5, 1.0f);
    CHECK (sameValues (none, knobs));

    auto bar5 = knobs, again = knobs, bar6 = knobs;
    perform::applyVariation (bar5, 100.0f, 5, 1.0f);
    perform::applyVariation (again, 100.0f, 5, 1.0f);
    perform::applyVariation (bar6, 100.0f, 6, 1.0f);
    CHECK (sameValues (bar5, again));
    CHECK_FALSE (sameValues (bar5, bar6));
    CHECK_FALSE (sameValues (bar5, knobs));
    CHECK (inRange (bar5));

    // At the start of bar 6 the changes are still bar 5's, then glide to bar 6's.
    auto start6 = knobs;
    perform::applyVariation (start6, 100.0f, 6, 0.0f);
    CHECK (sameValues (start6, bar5));

    // Small: within each control's range of travel.
    for (const auto& range : perform::kVariationRanges)
    {
        const auto& def = params::paramDef (range.param);
        const auto moved = std::abs (perform::toTravel (def, bar5.get (range.param)) -
                                     perform::toTravel (def, knobs.get (range.param)));
        CHECK (moved <= range.amount + 1e-5f);
    }

    // Only grain controls move.
    for (const auto& def : params::kParamDefs)
        if (def.group != params::Group::Grains)
            CHECK (juce::exactlyEqual (bar5.get (def.param), knobs.get (def.param)));
}

// ---- Riser (T-0.9-16) -----------------------------------------------------------------------------------------------

TEST_CASE ("A rise ends N bars after the bar line nearest the press", "[unit][perform][riser]")
{
    // 4/4: bars of 4 quarter notes; bar 3 starts at 8.
    CHECK_THAT (perform::riserEnd (8.0, 8.0, 4.0, 4), WithinAbs (24.0, 1e-9));
    CHECK_THAT (perform::riserEnd (9.0, 8.0, 4.0, 4), WithinAbs (24.0, 1e-9));  // a little late: from bar 3
    CHECK_THAT (perform::riserEnd (11.0, 8.0, 4.0, 1), WithinAbs (16.0, 1e-9)); // nearly bar 4: from bar 4
    CHECK_THAT (perform::riserEnd (0.0, 0.0, 3.0, 2), WithinAbs (6.0, 1e-9));   // 3/4

    auto s = ParamSnapshot::defaults();
    CHECK (perform::riserBars (s) == 4);
    s.set (ParamId::riserBars, 3.0f);
    CHECK (perform::riserBars (s) == 8);
}

// ---- Through the stage and the engine -------------------------------------------------------------------------------

namespace
{
TransportState playingAt (std::int64_t sample, double bpm)
{
    TransportState t;
    t.isPlaying = true;
    t.hasHostInfo = true;
    t.bpm = bpm;
    t.samplePosition = sample;
    t.ppqPosition = static_cast<double> (sample) / (kSampleRate * 60.0 / bpm);
    t.ppqLastBarStart = std::floor (t.ppqPosition / 4.0) * 4.0;
    return t;
}

// Drives a stage the way the engine does (grid, drop split) for `total`
// samples, Riser Go on from `pressAt`, and returns Storm as it acts per sample.
std::vector<float> stormPerSample (double bpm, int blockSize, std::int64_t pressAt, std::int64_t total,
                                   const ParamSnapshot& knobs, perform::PerformanceStage& stage)
{
    stage.prepare (kSampleRate);
    std::vector<float> storm;
    storm.reserve (static_cast<std::size_t> (total));
    for (std::int64_t start = 0; start < total; start += blockSize)
    {
        const auto n = static_cast<int> (std::min<std::int64_t> (blockSize, total - start));
        auto host = knobs;
        host.set (ParamId::riserGo, start >= pressAt ? 1.0f : 0.0f);
        const auto& morphed = stage.apply (host);
        stage.beginBlock (morphed);
        const auto active = stage.isActive (morphed);

        for (int offset = 0; offset < n;)
        {
            const auto s = start + offset;
            auto chunk = std::min (256, n - offset);
            if (active)
                chunk = stage.limitChunk (s, std::min (chunk, 256 - static_cast<int> (s % 256)));
            const auto& v = active ? stage.process (morphed, playingAt (s, bpm), s, chunk) : morphed;
            storm.insert (storm.end(), static_cast<std::size_t> (chunk), v.get (ParamId::stormAmount));
            offset += chunk;
        }
    }
    return storm;
}

struct Stereo
{
    std::vector<float> left, right;
};

// Renders a sine through an engine playing at 120 BPM, the snapshot chosen per block.
template<typename SnapshotFor>
Stereo render (double seconds, int blockSize, SnapshotFor snapshotFor)
{
    Stereo out;
    out.left = test::sine (220.0, kSampleRate, seconds, 0.25f);
    out.right = out.left;

    ThunderEngine engine;
    engine.prepare (kSampleRate);
    for (std::size_t start = 0; start < out.left.size(); start += static_cast<std::size_t> (blockSize))
    {
        const auto n = std::min (static_cast<std::size_t> (blockSize), out.left.size() - start);
        float* channels[] = {out.left.data() + start, out.right.data() + start};
        engine.process (channels, static_cast<int> (n), playingAt (static_cast<std::int64_t> (start), 120.0),
                        snapshotFor (start));
    }
    return out;
}

float largestStep (const std::vector<float>& s, std::size_t from)
{
    float largest = 0.0f;
    for (auto i = std::max<std::size_t> (from, 1); i < s.size(); ++i)
        largest = std::max (largest, std::abs (s[i] - s[i - 1]));
    return largest;
}

bool identical (const std::vector<float>& a, const std::vector<float>& b)
{
    return a.size() == b.size() &&
           std::equal (a.begin(), a.end(), b.begin(), [] (float x, float y) { return juce::exactlyEqual (x, y); });
}
} // namespace

TEST_CASE ("A rise peaks exactly on the bar line at any tempo and block size", "[unit][perform][riser]")
{
    for (const auto bpm : {90.0, 120.0, 137.5})
        for (const auto blockSize : {64, 333, 512, 1024})
        {
            INFO (bpm << " BPM, blocks of " << blockSize);
            const auto samplesPerQuarter = kSampleRate * 60.0 / bpm;
            const auto pressAt = static_cast<std::int64_t> (0.3 * 4.0 * samplesPerQuarter); // early in bar 1
            const auto peak = static_cast<std::int64_t> (std::llround (16.0 * samplesPerQuarter)); // 4 bars on
            const auto total = peak + static_cast<std::int64_t> (0.2 * kSampleRate);

            perform::PerformanceStage stage;
            const auto storm = stormPerSample (bpm, blockSize, pressAt, total, ParamSnapshot::defaults(), stage);

            const auto at = static_cast<std::size_t> (peak);
            CHECK_THAT (storm[at], WithinAbs (100.0, 0.01));
            CHECK (storm[at - 1] < storm[at]);
            CHECK (*std::max_element (storm.begin(), storm.begin() + static_cast<std::ptrdiff_t> (at)) < 100.0f);

            // It climbs, then drops back to the knob within the short fade.
            CHECK (storm[static_cast<std::size_t> (pressAt) + 512] < storm[at / 2]);
            const auto settled = at + static_cast<std::size_t> ((perform::kRiserDropSeconds + 0.01) * kSampleRate);
            CHECK (juce::exactlyEqual (storm[settled], 0.0f));
            CHECK (stage.getRiserProgress() < 0.0f);
        }
}

TEST_CASE ("A Go left on in a project doesn't start a rise when it opens", "[unit][perform][riser]")
{
    perform::PerformanceStage stage;
    const auto storm = stormPerSample (120.0, 512, 0, 48000, ParamSnapshot::defaults(), stage);
    CHECK (*std::max_element (storm.begin(), storm.end()) <= 0.0f);

    // RISE in the editor starts one even from a click shorter than a block.
    stage.triggerRiser();
    const auto host = ParamSnapshot::defaults();
    const auto& morphed = stage.apply (host);
    stage.beginBlock (morphed);
    CHECK (stage.isActive (morphed));
}

TEST_CASE ("Storm, Variation and the strikes sound the same in a bounce as in playback", "[unit][perform][engine]")
{
    auto s = ParamSnapshot::defaults();
    s.set (ParamId::stormAmount, 60.0f);
    s.set (ParamId::variationAmount, 100.0f);
    s.set (ParamId::strikeScatter, 1.0f);

    const auto offline = render (2.5, 4096, [&] (std::size_t) { return s; });
    const auto playback = render (2.5, 256, [&] (std::size_t) { return s; });
    const auto irregular = render (2.5, 333, [&] (std::size_t) { return s; });
    CHECK (identical (offline.left, playback.left));
    CHECK (identical (offline.right, playback.right));
    // Uneven host blocks (as some DAWs send): the stage sends exactly the same
    // values at every sample, and the sound matches to within float rounding
    // (the grains round differently when a control changes mid-chunk).
    float worst = 0.0f;
    for (std::size_t i = 0; i < irregular.left.size(); ++i)
        worst = std::max (worst, std::abs (irregular.left[i] - playback.left[i]));
    CHECK (worst < 1.0e-6f); // −120 dB

    auto valuesPerSample = [&] (int blockSize)
    {
        perform::PerformanceStage stage;
        stage.prepare (kSampleRate);
        std::vector<float> out;
        const std::int64_t total = 120000;
        for (std::int64_t start = 0; start < total; start += blockSize)
        {
            const auto n = static_cast<int> (std::min<std::int64_t> (blockSize, total - start));
            const auto& morphed = stage.apply (s);
            stage.beginBlock (morphed);
            for (int offset = 0; offset < n;)
            {
                const auto at = start + offset;
                auto chunk = std::min (256, n - offset);
                chunk = stage.limitChunk (at, std::min (chunk, 256 - static_cast<int> (at % 256)));
                auto t = playingAt (start, 120.0);
                if (offset > 0)
                    t = t.advancedBy (offset, kSampleRate);
                const auto& v = stage.process (morphed, t.delayedBy (3900, kSampleRate), at, chunk);
                for (int i = 0; i < chunk; ++i)
                    for (const auto id : {ParamId::grainSize, ParamId::grainDensity, ParamId::grainReverse})
                        out.push_back (v.get (id));
                offset += chunk;
            }
        }
        return out;
    };
    CHECK (identical (valuesPerSample (256), valuesPerSample (333)));
}

TEST_CASE ("The same bars vary the same way every time they play", "[unit][perform][variation]")
{
    auto knobs = ParamSnapshot::defaults();
    knobs.set (ParamId::variationAmount, 100.0f);

    auto valuesAt = [&] (perform::PerformanceStage& stage, std::int64_t sample)
    {
        const auto& morphed = stage.apply (knobs);
        stage.beginBlock (morphed);
        return stage.process (morphed, playingAt (sample, 120.0), sample, 256);
    };

    // Bar 3 (from 8 quarter notes), reached twice: by playing and after a loop.
    perform::PerformanceStage stage;
    stage.prepare (kSampleRate);
    const auto bar3 = static_cast<std::int64_t> (8 * 24000 + 9728);
    const auto first = valuesAt (stage, bar3);
    valuesAt (stage, 4 * 24000);                // somewhere else
    const auto looped = valuesAt (stage, bar3); // the loop comes back
    CHECK (sameValues (first, looped));

    perform::PerformanceStage fresh; // a bounce started elsewhere
    fresh.prepare (kSampleRate);
    CHECK (sameValues (valuesAt (fresh, bar3), first));
}

TEST_CASE ("Holding and letting go of the strikes never clicks", "[unit][perform][strikes][engine]")
{
    auto base = ParamSnapshot::defaults();
    base.set (ParamId::spaceMix, 0.0f);
    auto held = base;
    held.set (ParamId::strikeClap, 1.0f);
    held.set (ParamId::strikeScatter, 1.0f);

    // Held from 1 s to 2 s.
    const auto pressed = render (3.0, 256,
                                 [&] (std::size_t start)
                                 {
                                     const auto t = static_cast<double> (start) / kSampleRate;
                                     return t >= 1.0 && t < 2.0 ? held : base;
                                 });
    const auto settledOff = render (3.0, 256, [&] (std::size_t) { return base; });
    const auto settledOn = render (3.0, 256, [&] (std::size_t) { return held; });

    const auto from = static_cast<std::size_t> (0.5 * kSampleRate);
    const auto steady = std::max (largestStep (settledOff.left, from), largestStep (settledOn.left, from));
    CHECK (test::allFinite (pressed.left));
    CHECK (largestStep (pressed.left, from) < 1.25f * steady + 0.005f);
}
