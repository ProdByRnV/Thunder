// The tempo-synced modules: the musical clock, Time (halftime), Shape and
// Static (PRD §7.3, §7.5, §7.9; FR-TIME-001…003, FR-STATIC-001…004,
// FR-SHAPE-001…004).

#include "support/TestSignals.h"

#include "core/dsp/shape/ShapeModule.h"
#include "core/dsp/static/StaticModulator.h"
#include "core/dsp/time/TimeModule.h"
#include "core/engine/ThunderEngine.h"
#include "core/host/MusicalClock.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace thunder;
using Catch::Matchers::WithinAbs;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
constexpr double kRate = 48000.0;
constexpr double kBpm = 120.0;
constexpr std::size_t kBar = 96000; // 4/4 at 120 BPM and 48 kHz
constexpr std::size_t kBeat = kBar / 4;

TransportState playingAt (double ppq, int numerator = 4)
{
    TransportState t;
    t.bpm = kBpm;
    t.timeSigNumerator = numerator;
    t.isPlaying = true;
    t.hasHostInfo = true;
    t.ppqPosition = ppq;
    t.ppqLastBarStart = std::floor (ppq / t.quarterNotesPerBar()) * t.quarterNotesPerBar();
    t.samplePosition = static_cast<std::int64_t> (std::llround (ppq * kRate * 60.0 / kBpm));
    return t;
}

// Runs a mono signal through `module` (as identical channels) while the host
// plays from `start`, chunk by chunk as the engine would.
std::vector<float> runPlaying (Module& module, const std::vector<float>& input, const ParamSnapshot& snapshot,
                               TransportState start, std::size_t chunk = 256)
{
    std::vector<float> left = input, right = input;
    auto transport = start;
    for (std::size_t from = 0; from < input.size(); from += chunk)
    {
        const auto n = std::min (chunk, input.size() - from);
        float* channels[] = {left.data() + from, right.data() + from};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        module.process (block, ProcessContext{transport, snapshot});
        transport = transport.advancedBy (static_cast<int> (n), kRate);
    }
    return left;
}

// Power of `hz` in s[from, to) (Goertzel).
double powerAt (const std::vector<float>& s, std::size_t from, std::size_t to, double hz)
{
    const auto w = 2.0 * juce::MathConstants<double>::pi * hz / kRate;
    const auto coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (auto i = from; i < to; ++i)
    {
        const auto s0 = s[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    return (s1 * s1 + s2 * s2 - coeff * s1 * s2) / static_cast<double> (to - from);
}

bool sameFrame (const dsp::StaticModulator::Frame& a, const dsp::StaticModulator::Frame& b)
{
    return std::equal (a.values.begin(), a.values.end(), b.values.begin(),
                       [] (float x, float y) { return juce::exactlyEqual (x, y); });
}

double rms (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    return std::sqrt (test::energy (s, from, to) / static_cast<double> (to - from));
}

// A sine of `first` Hz for the first `share` of every bar, `second` Hz after.
std::vector<float> barTones (std::size_t bars, double share, double first, double second)
{
    std::vector<float> s (bars * kBar);
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const auto into = static_cast<double> (i % kBar) / static_cast<double> (kBar);
        const auto hz = into < share ? first : second;
        s[i] = 0.5f * static_cast<float> (
                          std::sin (2.0 * juce::MathConstants<double>::pi * hz * static_cast<double> (i) / kRate));
    }
    return s;
}

ParamSnapshot timeSettings (params::HalftimeSpeed speed, float smooth)
{
    auto p = ParamSnapshot::defaults();
    p.set (ParamId::timeOn, 1.0f);
    test::setChoice (p, ParamId::halftimeSpeed, speed);
    p.set (ParamId::halftimeSmooth, smooth);
    return p;
}

ParamSnapshot shapeSettings (params::ShapeType type, params::SyncedRate rate, float depth, float smooth)
{
    auto p = ParamSnapshot::defaults();
    p.set (ParamId::shapeOn, 1.0f);
    test::setChoice (p, ParamId::shapeType, type);
    p.set (ParamId::shapeRate, static_cast<float> (params::enumIndex (rate) - params::kShapeRateOffset));
    p.set (ParamId::shapeDepth, depth);
    p.set (ParamId::shapeSmooth, smooth);
    return p;
}

ParamSnapshot staticSettings (float amount)
{
    auto p = ParamSnapshot::defaults();
    p.set (ParamId::staticOn, 1.0f);
    p.set (ParamId::staticAmount, amount);
    for (const auto id : {ParamId::staticTgtPosition, ParamId::staticTgtPitch, ParamId::staticTgtFilter,
                          ParamId::staticTgtPan, ParamId::staticTgtLevel})
        p.set (id, 1.0f);
    return p;
}
} // namespace

// ---- The musical clock ----------------------------------------------------------------------------------------------

TEST_CASE ("The clock follows the host while it plays and keeps time while it is stopped", "[unit][rhythm]")
{
    MusicalClock clock;
    clock.prepare (kRate);

    auto t = playingAt (2.0);
    auto c = clock.next (t, 256);
    CHECK (c.jumped); // the first chunk
    CHECK_THAT (c.ppq, WithinAbs (2.0, 1e-12));
    CHECK_THAT (c.barStart, WithinAbs (0.0, 1e-12));
    CHECK_THAT (c.samplesPerQuarter, WithinAbs (24000.0, 1e-9));

    t = t.advancedBy (256, kRate);
    CHECK_FALSE (clock.next (t, 256).jumped);

    // A loop back to the start is a jump.
    c = clock.next (playingAt (0.0), 256);
    CHECK (c.jumped);

    // Stopped: it carries on from where it was, at the tempo, counting bars.
    auto stopped = playingAt (0.0);
    stopped.isPlaying = false;
    c = clock.next (stopped, 24000);
    CHECK_FALSE (c.jumped);
    CHECK_THAT (c.ppq, WithinAbs (256.0 / 24000.0, 1e-12));
    for (int i = 0; i < 4; ++i)
        c = clock.next (stopped, 24000);
    CHECK_THAT (c.ppq, WithinAbs (4.0 + 256.0 / 24000.0, 1e-9));
    CHECK_THAT (c.barStart, WithinAbs (4.0, 1e-9));

    // Starting to play again is a jump.
    CHECK (clock.next (playingAt (8.0), 256).jumped);
}

TEST_CASE ("Without the host's sample count the clock takes song time from the song position", "[unit][rhythm]")
{
    // Some hosts report a song position but no time in samples: free-running
    // movement (Static's Hz rate) must still follow the song.
    juce::AudioPlayHead::PositionInfo info;
    info.setIsPlaying (true);
    info.setBpm (120.0);
    info.setPpqPosition (6.0);
    info.setPpqPositionOfLastBarStart (4.0);
    const auto t = TransportState::fromPosition (info);
    CHECK (t.samplePositionMissing);

    MusicalClock clock;
    clock.prepare (kRate);
    CHECK_THAT (clock.next (t, 256).seconds, WithinAbs (3.0, 1e-12)); // 6 quarters at 120 BPM
    const auto later = t.advancedBy (24000, kRate);
    CHECK_THAT (clock.next (later, 256).seconds, WithinAbs (3.5, 1e-9));

    info.setTimeInSamples (48000);
    const auto reported = TransportState::fromPosition (info);
    CHECK_FALSE (reported.samplePositionMissing);
    MusicalClock other;
    other.prepare (kRate);
    CHECK_THAT (other.next (reported, 256).seconds, WithinAbs (1.0, 1e-12)); // the host's count wins
}

TEST_CASE ("A delayed transport is where the delayed audio belongs in the song", "[unit][rhythm]")
{
    const auto t = playingAt (4.25);
    const auto earlier = t.delayedBy (12000, kRate); // half a beat
    CHECK_THAT (earlier.ppqPosition, WithinAbs (3.75, 1e-12));
    CHECK_THAT (earlier.ppqLastBarStart, WithinAbs (0.0, 1e-12)); // back in the first bar
    CHECK (earlier.samplePosition == t.samplePosition - 12000);

    CHECK_THAT (t.delayedBy (3000, kRate).ppqLastBarStart, WithinAbs (4.0, 1e-12));

    auto stopped = t;
    stopped.isPlaying = false;
    CHECK_THAT (stopped.delayedBy (12000, kRate).ppqPosition, WithinAbs (4.25, 1e-12));
}

// ---- Time ----------------------------------------------------------------------------------------------------------

TEST_CASE ("Halftime plays the first half of each bar across the whole bar, at the same pitch", "[unit][rhythm][time]")
{
    // 440 Hz in the first half of every bar, 660 Hz in the second.
    const auto input = barTones (3, 0.5, 440.0, 660.0);

    dsp::TimeModule time;
    time.prepare ({kRate, 256, 2});
    const auto out = runPlaying (time, input, timeSettings (params::HalftimeSpeed::Half, 0.0f), playingAt (0.0));

    // In the second half of bar 2 the input is 660 Hz, the output still 440 Hz.
    const auto from = kBar + kBar / 2 + 4800, to = 2 * kBar - 4800;
    CHECK (powerAt (input, from, to, 660.0) > 100.0 * powerAt (input, from, to, 440.0));
    CHECK (powerAt (out, from, to, 440.0) > 100.0 * powerAt (out, from, to, 660.0));
    CHECK (std::abs (test::cents (test::zeroCrossingFrequency (out, from, to, kRate), 440.0)) < 10.0);

    // A steady level: the grains add up in phase.
    CHECK_THAT (juce::Decibels::gainToDecibels (rms (out, from, to) / rms (input, from, to)), WithinAbs (0.0, 1.0));
    CHECK (time.getSegmentsStarted() == 3);
}

TEST_CASE ("Halftime keeps a chord's level steady", "[unit][rhythm][time]")
{
    // Three notes with harmonics: the grains can't line up with every partial
    // at once, so the level wobbles a little, but stays close.
    const auto input = test::chord ({57.0, 60.0, 64.0}, kRate, 2.0 * kBar / kRate, 4);

    dsp::TimeModule time;
    time.prepare ({kRate, 256, 2});
    const auto out = runPlaying (time, input, timeSettings (params::HalftimeSpeed::Half, 30.0f), playingAt (0.0));
    REQUIRE (test::allFinite (out));

    // 50 ms windows through the second bar, after its crossfade.
    const std::size_t window = 2400;
    for (auto from = kBar + 9600; from + window < 2 * kBar; from += window)
    {
        INFO ("at " << from);
        CHECK_THAT (juce::Decibels::gainToDecibels (rms (out, from, from + window) / rms (input, from, from + window)),
                    WithinAbs (0.0, 1.5));
    }
}

TEST_CASE ("Quarter speed plays the bar's first quarter across the bar", "[unit][rhythm][time]")
{
    const auto input = barTones (2, 0.25, 440.0, 660.0);

    dsp::TimeModule time;
    time.prepare ({kRate, 256, 2});
    const auto out = runPlaying (time, input, timeSettings (params::HalftimeSpeed::Quarter, 0.0f), playingAt (0.0));

    const auto from = kBar + kBar / 4 + 4800, to = 2 * kBar - 4800;
    CHECK (powerAt (out, from, to, 440.0) > 100.0 * powerAt (out, from, to, 660.0));
    CHECK (std::abs (test::cents (test::zeroCrossingFrequency (out, from, to, kRate), 440.0)) < 10.0);
}

TEST_CASE ("Halftime adds no latency: each bar starts with the input itself", "[unit][rhythm][time]")
{
    const auto input = test::sine (330.0, kRate, 2.5 * kBar / kRate);

    dsp::TimeModule time;
    time.prepare ({kRate, 256, 2});
    const auto out = runPlaying (time, input, timeSettings (params::HalftimeSpeed::Half, 0.0f), playingAt (0.0));

    // After the 2 ms crossfade and until the first new grain, bar 2 is the input exactly.
    const auto hop = static_cast<std::size_t> (time.getGrainLength() / 2);
    float worst = 0.0f;
    for (auto i = kBar + 100; i < kBar + hop; ++i)
        worst = std::max (worst, std::abs (out[i] - input[i]));
    CHECK (worst < 1.0e-5f);
}

TEST_CASE ("Halftime segments follow the bars, loops and the stopped transport", "[unit][rhythm][time]")
{
    const auto input = test::sine (330.0, kRate, 1.5 * kBar / kRate);
    const auto settings = timeSettings (params::HalftimeSpeed::Half, 30.0f);

    SECTION ("A loop back starts a new segment at once")
    {
        dsp::TimeModule time;
        time.prepare ({kRate, 256, 2});
        runPlaying (time, input, settings, playingAt (0.0)); // bars 1 and 2 (half)
        CHECK (time.getSegmentsStarted() == 2);
        runPlaying (time, std::vector<float> (2560, 0.1f), settings, playingAt (0.0));
        CHECK (time.getSegmentsStarted() == 3);
    }

    SECTION ("Stopped, it counts bars at the tempo")
    {
        dsp::TimeModule time;
        time.prepare ({kRate, 256, 2});
        auto stopped = playingAt (0.0);
        stopped.isPlaying = false;
        runPlaying (time, test::sine (330.0, kRate, 2.5 * kBar / kRate), settings, stopped);
        CHECK (time.getSegmentsStarted() == 3);
    }

    SECTION ("3/4 bars are three beats long")
    {
        dsp::TimeModule time;
        time.prepare ({kRate, 256, 2});
        runPlaying (time, test::sine (330.0, kRate, 2.0 * kBar / kRate), settings, playingAt (0.0, 3));
        CHECK (time.getSegmentsStarted() == 3); // bars at 0, 3 and 6 beats; 8 beats played
    }
}

TEST_CASE ("Halftime is the same for any chunk size", "[unit][rhythm][time]")
{
    const auto input = barTones (2, 0.5, 440.0, 550.0);
    const auto settings = timeSettings (params::HalftimeSpeed::Half, 50.0f);

    dsp::TimeModule a, b;
    a.prepare ({kRate, 256, 2});
    b.prepare ({kRate, 256, 2});
    const auto big = runPlaying (a, input, settings, playingAt (0.0), 256);
    const auto small = runPlaying (b, input, settings, playingAt (0.0), 37);

    float worst = 0.0f;
    for (std::size_t i = 0; i < big.size(); ++i)
        worst = std::max (worst, std::abs (big[i] - small[i]));
    CHECK (worst < 1.0e-6f);
}

// ---- Shape ---------------------------------------------------------------------------------------------------------

TEST_CASE ("The shapes over one cycle", "[unit][rhythm][shape]")
{
    using dsp::ShapeModule;
    using params::ShapeType;

    CHECK_THAT (ShapeModule::shapeAt (ShapeType::Pump, 0.0), WithinAbs (0.0, 1e-6));
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::Pump, 0.5), WithinAbs (0.875, 1e-6));
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::Swell, 0.0), WithinAbs (0.0, 1e-6));
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::Swell, 0.9999), WithinAbs (1.0, 1e-3));
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::Gate, 0.25), WithinAbs (1.0, 1e-6));
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::Gate, 0.75), WithinAbs (0.0, 1e-6));
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::RampUp, 0.25), WithinAbs (0.25, 1e-6));
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::RampDown, 0.25), WithinAbs (0.75, 1e-6));
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::Chop, 0.1), WithinAbs (1.0, 1e-6));  // first slice, on
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::Chop, 0.2), WithinAbs (0.0, 1e-6));  // first slice, off
    CHECK_THAT (ShapeModule::shapeAt (ShapeType::Chop, 0.26), WithinAbs (1.0, 1e-6)); // second slice

    // Smoothing is centred: a Gate's edge stays at half level on the grid line,
    // and a wider window only spreads it.
    ShapeModule shape;
    shape.prepare ({kRate, 256, 2});
    for (const auto width : {0.01, 0.1, 0.4})
    {
        CHECK_THAT (shape.smoothedAt (ShapeType::Gate, 0.5, width), WithinAbs (0.5, 1e-3));
        CHECK_THAT (shape.smoothedAt (ShapeType::Gate, 0.25, width), WithinAbs (1.0, 1e-3));
    }
    CHECK_THAT (shape.smoothedAt (ShapeType::Gate, 0.5 + 0.025, 0.1), WithinAbs (0.25, 1e-3));
    CHECK_THAT (shape.smoothedAt (ShapeType::RampUp, 0.3, 0.1), WithinAbs (0.3, 1e-3)); // linear stays linear
}

TEST_CASE ("Shape gates the layer on the beat grid, as deep as Depth says", "[unit][rhythm][shape]")
{
    const std::vector<float> input (2 * kBar, 0.5f);

    for (const auto depth : {100.0f, 60.0f})
    {
        INFO ("depth " << depth);
        dsp::ShapeModule shape;
        shape.prepare ({kRate, 256, 2});
        const auto out =
            runPlaying (shape, input, shapeSettings (params::ShapeType::Gate, params::SyncedRate::Quarter, depth, 0.0f),
                        playingAt (0.0));

        const auto low = 0.5f * (1.0f - depth / 100.0f);
        for (std::size_t beat = 0; beat < 8; ++beat)
        {
            CHECK_THAT (out[beat * kBeat + kBeat / 4], WithinAbs (0.5, 1e-4));
            CHECK_THAT (out[beat * kBeat + 3 * kBeat / 4], WithinAbs (low, 1e-4));
        }
    }
}

TEST_CASE ("Shape stays in phase with the song after a loop, and bar rates count bars", "[unit][rhythm][shape]")
{
    const std::vector<float> input (kBar, 0.5f);
    const auto settings = shapeSettings (params::ShapeType::Pump, params::SyncedRate::EighthTriplet, 80.0f, 20.0f);

    dsp::ShapeModule shape;
    shape.prepare ({kRate, 256, 2});
    const auto first = runPlaying (shape, input, settings, playingAt (0.0));
    const auto again = runPlaying (shape, input, settings, playingAt (0.0)); // the loop jumps back

    // After the 1 ms glide that hides the jump, the second pass is the first.
    float worst = 0.0f;
    for (std::size_t i = 480; i < kBar; ++i)
        worst = std::max (worst, std::abs (first[i] - again[i]));
    CHECK (worst < 1.0e-4f);

    // Ramp Up over 2 bars: half way at the start of the second bar.
    dsp::ShapeModule ramp;
    ramp.prepare ({kRate, 256, 2});
    const auto out = runPlaying (ramp, std::vector<float> (2 * kBar, 0.5f),
                                 shapeSettings (params::ShapeType::RampUp, params::SyncedRate::Bars2, 100.0f, 0.0f),
                                 playingAt (0.0));
    CHECK_THAT (out[kBar + 9600], WithinAbs (0.5 * (kBar + 9600.0) / (2.0 * kBar), 2e-3));
    CHECK_THAT (out[kBar / 2], WithinAbs (0.5 * 0.25, 2e-3));
}

// ---- Static --------------------------------------------------------------------------------------------------------

TEST_CASE ("Static's values depend only on the position in the song", "[unit][rhythm][static]")
{
    using dsp::StaticModulator;
    using params::StaticCharacter;

    for (const auto c : {StaticCharacter::Smooth, StaticCharacter::Stepped, StaticCharacter::Spark})
        for (int target = 0; target < StaticModulator::kNumTargets; ++target)
            for (const auto step : {-3.5, 0.0, 0.25, 7.9, 1234.5})
            {
                const auto v = StaticModulator::valueAt (c, target, step);
                CHECK (juce::exactlyEqual (v, StaticModulator::valueAt (c, target, step))); // the same every time
                CHECK (v >= -1.0f);
                CHECK (v <= 1.0f);
            }

    // Each target moves differently.
    CHECK_FALSE (juce::exactlyEqual (StaticModulator::valueAt (StaticCharacter::Stepped, 0, 5.0),
                                     StaticModulator::valueAt (StaticCharacter::Stepped, 1, 5.0)));

    // Stepped holds within a step; Smooth passes through the step values and glides between them.
    CHECK (juce::exactlyEqual (StaticModulator::valueAt (StaticCharacter::Stepped, 2, 5.1),
                               StaticModulator::valueAt (StaticCharacter::Stepped, 2, 5.9)));
    CHECK_THAT (StaticModulator::valueAt (StaticCharacter::Smooth, 2, 5.0),
                WithinAbs (StaticModulator::valueAt (StaticCharacter::Stepped, 2, 5.0), 1e-6));
    float biggestStep = 0.0f;
    for (int i = 0; i < 1000; ++i)
        biggestStep =
            std::max (biggestStep, std::abs (StaticModulator::valueAt (StaticCharacter::Smooth, 3, i * 0.01 + 0.01) -
                                             StaticModulator::valueAt (StaticCharacter::Smooth, 3, i * 0.01)));
    CHECK (biggestStep < 0.05f);

    // Spark: bursts on some steps, quiet late in a step.
    int bursts = 0;
    for (int k = 0; k < 200; ++k)
    {
        if (std::abs (StaticModulator::valueAt (StaticCharacter::Spark, 0, k + 0.001)) > 0.4f)
            ++bursts;
        CHECK (std::abs (StaticModulator::valueAt (StaticCharacter::Spark, 0, k + 0.99)) < 0.01f);
    }
    CHECK (bursts > 50);
    CHECK (bursts < 110);
}

TEST_CASE ("Static repeats its movement when the song loops", "[unit][rhythm][static]")
{
    auto settings = staticSettings (100.0f);
    settings.set (ParamId::staticSync, 1.0f);
    test::setChoice (settings, ParamId::staticRateSynced, params::SyncedRate::Eighth);

    dsp::StaticModulator modulator;
    modulator.prepare (kRate);

    // Two passes over the same bar; the second begins with a loop back.
    std::vector<dsp::StaticModulator::Frame> first, second;
    std::int64_t index = 0;
    for (auto* pass : {&first, &second})
    {
        auto t = playingAt (0.0);
        for (int i = 0; i < 1500; ++i)
        {
            pass->push_back (modulator.next (t, settings, index));
            index += dsp::StaticModulator::kControlInterval;
            t = t.advancedBy (dsp::StaticModulator::kControlInterval, kRate);
        }
    }

    // Once faded in (30 ms), every frame of the second pass is the first's.
    auto moved = false;
    for (std::size_t i = 30; i < first.size(); ++i)
    {
        CHECK (sameFrame (first[i], second[i]));
        moved = moved || !sameFrame (first[i], first[i - 1]);
    }
    CHECK (moved);

    // Within range: spreads only add, the level only dips.
    for (const auto& f : first)
    {
        CHECK (f[dsp::StaticModulator::position] >= 0.0f);
        CHECK (f[dsp::StaticModulator::level] >= 0.0f);
        CHECK (std::abs (f[dsp::StaticModulator::pan]) <= 1.0f);
    }
}

TEST_CASE ("Static moves its targets' values within their ranges", "[unit][rhythm][static]")
{
    dsp::StaticModulator::Frame frame;
    frame.values = {1.0f, 0.5f, 0.5f, 0.0f, 0.0f};

    auto p = ParamSnapshot::defaults();
    p.set (ParamId::grainPosSpread, 80.0f);
    dsp::StaticModulator::applyTo (frame, p);
    CHECK_THAT (p.get (ParamId::grainPosSpread), WithinAbs (100.0, 1e-4)); // clamped
    CHECK_THAT (p.get (ParamId::grainPitchSpread), WithinAbs (3.0, 1e-4));
    CHECK_THAT (p.get (ParamId::hpFreq), WithinAbs (400.0, 1e-2));   // one octave up
    CHECK_THAT (p.get (ParamId::lpFreq), WithinAbs (20000.0, 1e-2)); // clamped at the top

    // Pan and level act on the layer: fully right, 12 dB down.
    dsp::StaticStage stage;
    stage.prepare (kRate);
    frame.values = {0.0f, 0.0f, 0.0f, 1.0f, 1.0f};
    std::vector<float> left (4800, 1.0f), right (4800, 1.0f);
    float* channels[] = {left.data(), right.data()};
    stage.process (channels, 4800, frame);
    CHECK_THAT (left.back(), WithinAbs (0.0, 1e-6));
    CHECK_THAT (right.back(), WithinAbs (std::sqrt (2.0) * juce::Decibels::decibelsToGain (-12.0), 1e-5));
}

// ---- In the engine -------------------------------------------------------------------------------------------------

TEST_CASE ("Static off or at zero leaves the sound exactly as it was", "[unit][rhythm][static][engine]")
{
    const auto input = test::sine (220.0, kRate, 1.0);

    const auto render = [&input] (const ParamSnapshot& p)
    {
        ThunderEngine engine;
        engine.prepare (kRate);
        std::vector<float> left = input, right = input;
        for (std::size_t from = 0; from < left.size(); from += 512)
        {
            float* channels[] = {left.data() + from, right.data() + from};
            engine.process (channels, static_cast<int> (std::min<std::size_t> (512, left.size() - from)), {}, p);
            CHECK (engine.getStaticFrame().isZero());
        }
        return left;
    };

    const auto off = render (ParamSnapshot::defaults());
    const auto zero = render (staticSettings (0.0f));
    CHECK (off == zero);
}

TEST_CASE ("With Time, Static and Shape on, the result doesn't depend on the block size", "[unit][rhythm][engine]")
{
    auto p = staticSettings (100.0f);
    p.set (ParamId::staticRate, 8.0f);
    p.set (ParamId::timeOn, 1.0f);
    p.set (ParamId::shapeOn, 1.0f);
    test::setChoice (p, ParamId::shapeType, params::ShapeType::Chop);

    const auto input = test::sine (330.0, kRate, 1.5);
    const auto render = [&input, &p] (int blockSize)
    {
        ThunderEngine engine;
        engine.prepare (kRate);
        auto t = playingAt (0.0);
        std::vector<float> left = input, right = input;
        for (std::size_t from = 0; from < left.size(); from += static_cast<std::size_t> (blockSize))
        {
            const auto n =
                static_cast<int> (std::min<std::size_t> (static_cast<std::size_t> (blockSize), left.size() - from));
            float* channels[] = {left.data() + from, right.data() + from};
            engine.process (channels, n, t, p);
            t = t.advancedBy (n, kRate);
        }
        return left;
    };

    const auto reference = render (4096);
    for (const auto blockSize : {1, 17, 64, 333})
    {
        INFO ("block size " << blockSize);
        const auto out = render (blockSize);
        float worst = 0.0f;
        for (std::size_t i = 0; i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - reference[i]));
        CHECK (worst < 1.0e-5f);
    }
}
