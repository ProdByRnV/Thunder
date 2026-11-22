// v1.0's remaining module features: Duck, Autopan, the grain extras, the
// reverb modes and Quality (PRD FR-DUCK-001/003, FR-WIDTH-002,
// FR-GRAIN-004…006, FR-SPACE-001/002, FR-CORE-013; PRD DEC-051).

#include "support/TestSignals.h"

#include "core/dsp/duck/DuckModule.h"
#include "core/dsp/grains/GrainModule.h"
#include "core/dsp/pitch/PitchModule.h"
#include "core/dsp/space/FdnReverb.h"
#include "core/dsp/space/SpaceModule.h"
#include "core/dsp/width/WidthModule.h"
#include "core/engine/ThunderEngine.h"
#include "core/music/Scale.h"
#include "core/state/StateIds.h"
#include "plugin/ThunderProcessor.h"

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

TransportState playingAt (double ppq)
{
    TransportState t;
    t.bpm = kBpm;
    t.isPlaying = true;
    t.hasHostInfo = true;
    t.ppqPosition = ppq;
    t.ppqLastBarStart = std::floor (ppq / 4.0) * 4.0;
    t.samplePosition = static_cast<std::int64_t> (std::llround (ppq * kRate * 60.0 / kBpm));
    return t;
}

double rmsDb (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    return 10.0 * std::log10 (test::energy (s, from, to) / static_cast<double> (to - from) + 1.0e-30);
}

// Runs a stereo engine on a mono input (both channels) in 256-sample blocks.
std::vector<float> renderEngine (ThunderEngine& engine, const std::vector<float>& input, const ParamSnapshot& p,
                                 bool playing = false)
{
    std::vector<float> left = input, right = input;
    auto transport = playing ? playingAt (0.0) : TransportState{};
    for (std::size_t from = 0; from < left.size(); from += 256)
    {
        const auto n = static_cast<int> (std::min<std::size_t> (256, left.size() - from));
        float* channels[] = {left.data() + from, right.data() + from};
        engine.process (channels, n, transport, p);
        transport = transport.advancedBy (n, kRate);
    }
    return left;
}
} // namespace

// ---- Duck ----------------------------------------------------------------------------------------------------------

TEST_CASE ("Duck turns the layer down by how loud the melody is", "[unit][duck]")
{
    using dsp::DuckModule;
    CHECK_THAT (DuckModule::reductionDb (-60.0f, 1.0f), WithinAbs (0.0, 1e-6));
    CHECK_THAT (DuckModule::reductionDb (-30.0f, 1.0f), WithinAbs (12.0, 1e-4));
    CHECK_THAT (DuckModule::reductionDb (-10.0f, 0.5f), WithinAbs (12.0, 1e-4));
    CHECK_THAT (DuckModule::reductionDb (0.0f, 1.0f), WithinAbs (24.0, 1e-4));

    // A steady layer, keyed by a melody that plays for 0.5 s and stops.
    DuckModule duck;
    duck.prepare ({kRate, 256, 2});
    auto p = ParamSnapshot::defaults();
    p.set (ParamId::duckOn, 1.0f);
    p.set (ParamId::duckAmount, 100.0f);
    p.set (ParamId::duckRelease, 100.0f);

    auto key = test::sine (440.0, kRate, 0.5, 0.5f);
    key.resize (static_cast<std::size_t> (kRate), 0.0f);
    std::vector<float> layer (key.size(), 0.25f), right = layer;

    for (std::size_t from = 0; from < layer.size(); from += 256)
    {
        float* channels[] = {layer.data() + from, right.data() + from};
        const float* keys[] = {key.data() + from, key.data() + from};
        juce::dsp::AudioBlock<float> block (channels, 2, std::min<std::size_t> (256, layer.size() - from));
        TransportState t;
        duck.process (block, ProcessContext{t, p, keys});
    }

    // While the melody plays (−6 dBFS peaks): the full 24 dB. After it, back up
    // within a few release times.
    CHECK_THAT (juce::Decibels::gainToDecibels (layer[24000] / 0.25f), WithinAbs (-24.0, 0.5));
    CHECK (layer[static_cast<std::size_t> (0.5 * kRate) + 4800] < 0.2f);   // 100 ms after: on its way back
    CHECK (layer[static_cast<std::size_t> (0.5 * kRate) + 19200] > 0.23f); // four release times: within 1 dB
    CHECK_THAT (layer.back(), WithinAbs (0.25, 0.01));
}

TEST_CASE ("Duck works in Layer Only, after Auto Gain, and off it changes nothing", "[unit][duck][engine]")
{
    const auto input = test::sine (330.0, kRate, 2.0, 0.5f);
    auto p = ParamSnapshot::defaults();
    test::setChoice (p, ParamId::outputMode, params::OutputMode::LayerOnly);

    ThunderEngine plain, ducked;
    plain.prepare (kRate);
    ducked.prepare (kRate);
    const auto off = renderEngine (plain, input, p);

    auto on = p;
    on.set (ParamId::duckOn, 1.0f);
    on.set (ParamId::duckAmount, 100.0f);
    const auto out = renderEngine (ducked, input, on);

    // Auto Gain sees the same layer either way, so the difference is the duck's
    // full 24 dB (the melody peaks at −6 dBFS), even after Auto Gain settles.
    const auto from = static_cast<std::size_t> (1.5 * kRate), to = input.size();
    CHECK_THAT (rmsDb (out, from, to) - rmsDb (off, from, to), WithinAbs (-24.0, 1.0));

    // Off (the default): exactly the layer as before.
    ThunderEngine again;
    again.prepare (kRate);
    CHECK (renderEngine (again, input, p) == off);
}

// ---- Autopan -------------------------------------------------------------------------------------------------------

TEST_CASE ("Autopan sweeps the layer on the song's beat", "[unit][width]")
{
    dsp::WidthModule width;
    width.prepare ({kRate, 256, 2});
    auto p = ParamSnapshot::defaults();
    p.set (ParamId::autopanOn, 1.0f);
    p.set (ParamId::autopanDepth, 100.0f);
    test::setChoice (p, ParamId::autopanRate, params::SyncedRate::Quarter);

    std::vector<float> left (static_cast<std::size_t> (kRate), 0.5f), right = left;
    auto t = playingAt (0.0);
    for (std::size_t from = 0; from < left.size(); from += 256)
    {
        float* channels[] = {left.data() + from, right.data() + from};
        juce::dsp::AudioBlock<float> block (channels, 2, std::min<std::size_t> (256, left.size() - from));
        width.process (block, ProcessContext{t, p});
        t = t.advancedBy (256, kRate);
    }

    // A quarter note is 24000 samples: a quarter of the way through each beat
    // it is fully right, three quarters of the way fully left.
    for (const auto beat : {0u, 1u})
    {
        CHECK_THAT (left[beat * 24000 + 6000], WithinAbs (0.0, 2e-3));
        CHECK_THAT (right[beat * 24000 + 6000], WithinAbs (0.5 * std::sqrt (2.0), 2e-3));
        CHECK_THAT (right[beat * 24000 + 18000], WithinAbs (0.0, 2e-3));
        CHECK_THAT (left[beat * 24000 + 12000], WithinAbs (0.5, 2e-3)); // centred on the half beat
    }

    // Off: the layer passes untouched.
    dsp::WidthModule plain;
    plain.prepare ({kRate, 256, 2});
    std::vector<float> l2 (256, 0.3f), r2 (256, -0.2f);
    float* ch2[] = {l2.data(), r2.data()};
    juce::dsp::AudioBlock<float> block2 (ch2, 2, 256);
    TransportState stopped;
    plain.process (block2, ProcessContext{stopped, ParamSnapshot::defaults()});
    CHECK (std::all_of (l2.begin(), l2.end(), [] (float x) { return juce::exactlyEqual (x, 0.3f); }));
}

// ---- Grains --------------------------------------------------------------------------------------------------------

TEST_CASE ("Scale Lock keeps to intervals that keep the key in key", "[unit][grains]")
{
    using dsp::GrainModule;
    const auto has = [] (std::uint32_t set, int k) { return ((set >> static_cast<unsigned> (k + 12)) & 1u) != 0; };

    const auto minor = GrainModule::scaleLockIntervals (music::scaleMask (params::Scale::NaturalMinor));
    for (const auto k : {0, 5, -5, 7, -7, 12, -12})
        CHECK (has (minor, k));
    for (const auto k : {1, 2, 3, 4, 6, 8, 9, 10, 11, -1, -2, -3, -4})
        CHECK_FALSE (has (minor, k));

    const auto chromatic = GrainModule::scaleLockIntervals (music::scaleMask (params::Scale::Chromatic));
    for (int k = -12; k <= 12; ++k)
        CHECK (has (chromatic, k));

    // Every grain of a locked cloud is transposed by one of them.
    GrainModule grains;
    grains.prepare ({kRate, 256, 2});
    GrainModule::Settings s{0.1, 40.0, 1.0f, 0.2f, 12.0f};
    s.scaleLockMask = music::scaleMask (params::Scale::NaturalMinor);
    const auto input = test::sine (220.0, kRate, 0.2);
    std::vector<float> left (256), right (256);
    int checked = 0;
    for (int block = 0; block < 200; ++block)
    {
        std::copy_n (input.begin() + (block % 30) * 256, 256, left.begin());
        std::copy_n (left.begin(), 256, right.begin());
        float* channels[] = {left.data(), right.data()};
        juce::dsp::AudioBlock<float> b (channels, 2, 256);
        grains.process (b, s);
        for (const auto& g : grains.getGrains())
            if (g.active)
            {
                const auto semitones = 12.0 * std::log2 (std::abs (g.step));
                const auto k = static_cast<int> (std::lround (semitones));
                CHECK_THAT (semitones, WithinAbs (static_cast<double> (k), 1e-6));
                CHECK (has (minor, k));
                ++checked;
            }
    }
    CHECK (checked > 100);
}

TEST_CASE ("Tempo Sync starts grains on the song's note grid", "[unit][grains]")
{
    using dsp::GrainModule;
    CHECK_THAT (GrainModule::syncDivisionQuarters (8.0, 120.0), WithinAbs (0.25, 1e-12)); // 1/16 = 8 a second
    CHECK_THAT (GrainModule::syncDivisionQuarters (2.0, 120.0), WithinAbs (1.0, 1e-12));
    CHECK_THAT (GrainModule::syncDivisionQuarters (60.0, 120.0), WithinAbs (1.0 / 32.0, 1e-12));

    GrainModule grains;
    grains.prepare ({kRate, 256, 2});
    GrainModule::Settings s{0.05, 8.0, 1.0f};
    s.sync = true;

    // Two seconds from the song start: a grain on every sixteenth after the
    // first sample's (16 lines, the first at sample 0 has no line crossing).
    const auto input = test::sine (220.0, kRate, 2.0);
    std::vector<float> left = input, right = input;
    auto t = playingAt (0.0);
    std::vector<std::uint64_t> startedBy;
    for (std::size_t from = 0; from < left.size(); from += 256)
    {
        float* channels[] = {left.data() + from, right.data() + from};
        juce::dsp::AudioBlock<float> b (channels, 2, 256);
        grains.process (b, s, &t);
        t = t.advancedBy (256, kRate);
        startedBy.push_back (grains.getGrainsStarted());
    }
    CHECK (grains.getGrainsStarted() == 15u);

    // Each started in the 256-sample block holding a sixteenth's line (every 6000 samples).
    for (std::size_t k = 1; k < 15; ++k)
    {
        const auto block = (k * 6000) / 256;
        CHECK (startedBy[block] == k);
        CHECK (startedBy[block - 1] == k - 1);
    }
}

TEST_CASE ("Freeze holds the texture while new input is ignored", "[unit][grains]")
{
    using dsp::GrainModule;
    GrainModule grains;
    grains.prepare ({kRate, 256, 2});
    GrainModule::Settings s{0.1, 30.0, 1.0f, 0.5f};

    // 2 s of a tone, then frozen while 3 s of silence come in.
    auto input = test::sine (330.0, kRate, 2.0);
    input.resize (static_cast<std::size_t> (5.0 * kRate), 0.0f);
    std::vector<float> left = input, right = input;
    std::int64_t frozenAt = -1;
    for (std::size_t from = 0; from < left.size(); from += 256)
    {
        s.freeze = from >= static_cast<std::size_t> (2.0 * kRate);
        float* channels[] = {left.data() + from, right.data() + from};
        juce::dsp::AudioBlock<float> b (channels, 2, std::min<std::size_t> (256, left.size() - from));
        grains.process (b, s);
        if (s.freeze && frozenAt < 0)
            frozenAt = grains.getBuffer().getWriteCount();
    }

    CHECK (grains.getBuffer().getWriteCount() == frozenAt); // nothing written since
    // Still sounding at the end, at the tone's level give or take.
    const auto end = left.size();
    CHECK (rmsDb (left, end - 24000, end) > rmsDb (input, 0, 48000) - 6.0);
}

TEST_CASE ("Eco quality uses fewer grains and linear reads", "[unit][grains][quality]")
{
    using dsp::GrainModule;
    for (const auto quality : {params::Quality::Eco, params::Quality::Standard})
    {
        GrainModule grains;
        grains.prepare ({kRate, 256, 2, quality});
        const auto eco = quality == params::Quality::Eco;
        CHECK (grains.getGrainLimit() == (eco ? 32 : 64));

        // Dense enough for ~50 grains at once.
        GrainModule::Settings s{0.5, 100.0, 1.0f, 0.3f, 5.0f};
        const auto input = test::sine (220.0, kRate, 2.0);
        test::runModule (grains, input, ParamSnapshot::defaults()); // warm the buffer
        std::vector<float> left = input, right = input;
        int most = 0;
        for (std::size_t from = 0; from + 256 <= left.size(); from += 256)
        {
            float* channels[] = {left.data() + from, right.data() + from};
            juce::dsp::AudioBlock<float> b (channels, 2, 256);
            grains.process (b, s);
            most = std::max (most, grains.getActiveGrains());
            for (const auto& g : grains.getGrains())
                if (g.active)
                    CHECK (g.cubic == !eco);
        }
        CHECK (most <= grains.getGrainLimit());
        CHECK (most > (eco ? 30 : 40));
    }
}

// ---- Space ---------------------------------------------------------------------------------------------------------

TEST_CASE ("Each reverb mode decays and rings at a level like Cloud's", "[unit][space]")
{
    const auto measure = [] (params::SpaceMode mode, float decay)
    {
        dsp::SpaceModule space;
        space.prepare ({kRate, 256, 2});
        auto p = ParamSnapshot::defaults();
        p.set (ParamId::spaceMix, 100.0f);
        p.set (ParamId::spaceDecay, decay);
        test::setChoice (p, ParamId::spaceMode, mode);

        auto input = test::whiteNoise (kRate, 1.0);
        for (auto& x : input)
            x *= 0.25f;
        input.resize (static_cast<std::size_t> (6.0 * kRate), 0.0f);
        return test::runModule (space, input, p);
    };

    const auto cloud = measure (params::SpaceMode::Cloud, 30.0f);
    const auto cloudLevel = rmsDb (cloud, 24000, 48000);

    for (const auto mode : {params::SpaceMode::Plate, params::SpaceMode::Room, params::SpaceMode::Shimmer})
    {
        INFO ("mode " << static_cast<int> (mode));
        const auto out = measure (mode, 30.0f);
        REQUIRE (test::allFinite (out));
        CHECK_THAT (rmsDb (out, 24000, 48000), WithinAbs (cloudLevel, 2.0));

        // After the noise stops it dies away: 60 dB down well within 5 s at
        // these settings, and never growing.
        const auto level = rmsDb (out, 24000, 48000);
        CHECK (rmsDb (out, out.size() - 24000, out.size()) < level - 60.0);
        CHECK (rmsDb (out, 72000, 96000) < level);
    }

    // The decay ranges (FR-SPACE-002).
    using dsp::SpaceModule;
    CHECK_THAT (SpaceModule::decaySeconds (params::SpaceMode::Room, 0.0f), WithinAbs (0.2, 1e-9));
    CHECK_THAT (SpaceModule::decaySeconds (params::SpaceMode::Room, 100.0f), WithinAbs (3.0, 1e-9));
    CHECK_THAT (SpaceModule::decaySeconds (params::SpaceMode::Plate, 100.0f), WithinAbs (8.0, 1e-9));
    CHECK_THAT (SpaceModule::decaySeconds (params::SpaceMode::Shimmer, 0.0f), WithinAbs (1.0, 1e-9));
    CHECK_THAT (SpaceModule::decaySeconds (params::SpaceMode::Cloud, 100.0f), WithinAbs (20.0, 1e-9));
}

TEST_CASE ("Shimmer climbs an octave, and stays stable at its longest decay", "[unit][space]")
{
    dsp::FdnReverb shimmer;
    shimmer.prepare (kRate, dsp::FdnReverb::Character::shimmer);
    shimmer.setDecaySeconds (20.0);
    shimmer.setDampingHz (18000.0);

    // A second of 330 Hz, then 30 s of silence.
    auto input = test::sine (330.0, kRate, 1.0, 0.25f);
    input.resize (static_cast<std::size_t> (31.0 * kRate), 0.0f);
    std::vector<float> left (input.size()), right (input.size());
    shimmer.process (input.data(), input.data(), left.data(), right.data(), static_cast<int> (input.size()));

    REQUIRE (test::allFinite (left));
    const auto peak =
        *std::max_element (left.begin(), left.end(), [] (float a, float b) { return std::abs (a) < std::abs (b); });
    CHECK (std::abs (peak) < 4.0f);

    // An octave up shows in the tail.
    const auto goertzel = [&left] (double hz, std::size_t from, std::size_t to)
    {
        const auto coeff = 2.0 * std::cos (2.0 * juce::MathConstants<double>::pi * hz / kRate);
        double s1 = 0.0, s2 = 0.0;
        for (auto i = from; i < to; ++i)
        {
            const auto s0 = left[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return s1 * s1 + s2 * s2 - coeff * s1 * s2;
    };
    const auto from = static_cast<std::size_t> (2.0 * kRate), to = static_cast<std::size_t> (3.0 * kRate);
    CHECK (goertzel (660.0, from, to) > 0.05 * goertzel (330.0, from, to));

    // And it dies away.
    CHECK (rmsDb (left, left.size() - 48000, left.size()) < rmsDb (left, from, to) - 40.0);
}

TEST_CASE ("Switching reverb mode lets the old one ring out without a click", "[unit][space]")
{
    dsp::SpaceModule space;
    space.prepare ({kRate, 256, 2});
    auto p = ParamSnapshot::defaults();
    p.set (ParamId::spaceMix, 100.0f);

    const auto input = test::sine (220.0, kRate, 2.0, 0.25f);
    std::vector<float> left = input, right = input;
    float biggestStep = 0.0f;
    for (std::size_t from = 0; from < left.size(); from += 256)
    {
        if (from == static_cast<std::size_t> (kRate)) // switch to Plate after a second
            test::setChoice (p, ParamId::spaceMode, params::SpaceMode::Plate);
        float* channels[] = {left.data() + from, right.data() + from};
        juce::dsp::AudioBlock<float> b (channels, 2, 256);
        TransportState t;
        space.process (b, ProcessContext{t, p});

        if (from == static_cast<std::size_t> (kRate) + 2560)
        {
            CHECK (space.isEngineRunning (params::SpaceMode::Cloud)); // its tail continues
            CHECK (space.isEngineRunning (params::SpaceMode::Plate));
            CHECK_FALSE (space.isEngineRunning (params::SpaceMode::Room)); // never used: resting
        }
    }
    for (std::size_t i = static_cast<std::size_t> (kRate) - 256; i < static_cast<std::size_t> (kRate) + 4800; ++i)
        biggestStep = std::max (biggestStep, std::abs (left[i] - left[i - 1]));
    CHECK (biggestStep < 0.05f);
}

TEST_CASE ("With every v1.0 module on, the result doesn't depend on the block size", "[unit][engine]")
{
    auto p = ParamSnapshot::defaults();
    for (const auto id : {ParamId::duckOn, ParamId::autopanOn, ParamId::grainSync, ParamId::timeOn, ParamId::shapeOn,
                          ParamId::staticOn})
        p.set (id, 1.0f);
    p.set (ParamId::grainPitchSpread, 7.0f);
    test::setChoice (p, ParamId::spaceMode, params::SpaceMode::Shimmer);

    const auto input = test::sine (262.0, kRate, 1.5);
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
    for (const auto blockSize : {17, 256, 333})
    {
        INFO ("block size " << blockSize);
        const auto out = render (blockSize);
        float worst = 0.0f;
        for (std::size_t i = 0; i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - reference[i]));
        CHECK (worst < 1.0e-5f);
    }
}

// ---- Quality -------------------------------------------------------------------------------------------------------

TEST_CASE ("Quality chooses the pitch engine; only High changes the latency", "[unit][quality]")
{
    std::array<int, 3> latency{};
    for (const auto q : {params::Quality::Eco, params::Quality::Standard, params::Quality::High})
    {
        ThunderEngine engine;
        engine.prepare (kRate, q);
        CHECK (engine.getQuality() == q);
        latency[params::enumIndex (q)] = engine.getLatencySamples();

        // Each makes sound, and is the same for any block size.
        auto p = ParamSnapshot::defaults();
        const auto input = test::sine (220.0, kRate, 1.0);
        const auto a = renderEngine (engine, input, p);
        CHECK (rmsDb (a, 24000, 48000) > -30.0);
    }
    CHECK (latency[0] == latency[1]);
    CHECK (latency[2] - latency[1] == static_cast<int> (std::lround (0.04 * kRate)));

    CHECK_THAT (dsp::pitchConfigFor (params::Quality::Eco).intervalSeconds, WithinAbs (0.016, 1e-12));
    CHECK_THAT (dsp::pitchConfigFor (params::Quality::High).blockSeconds, WithinAbs (0.12, 1e-12));
}

TEST_CASE ("Quality is saved with the project and applied when it changes", "[unit][quality][plugin][state]")
{
    juce::MemoryBlock saved;
    int standardLatency = 0;
    {
        ThunderProcessor source;
        source.prepareToPlay (kRate, 512);
        standardLatency = source.getLatencySamples();
        source.setQuality (params::Quality::High);
        CHECK (source.getQuality() == params::Quality::High);
        CHECK (source.getEngine().getQuality() == params::Quality::High);
        CHECK (source.getLatencySamples() == standardLatency + static_cast<int> (std::lround (0.04 * kRate)));
        CHECK_FALSE (source.isSuspended());
        source.getStateInformation (saved);
    }

    // A project opened before the host prepares picks it up in prepareToPlay.
    ThunderProcessor reopened;
    reopened.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (reopened.getQuality() == params::Quality::High);
    reopened.prepareToPlay (kRate, 512);
    CHECK (reopened.getEngine().getQuality() == params::Quality::High);

    // Older projects (no quality saved) are Standard.
    state::SessionState old;
    juce::ValueTree tree = old.toValueTree();
    tree.removeProperty (state::ids::quality, nullptr);
    CHECK (state::SessionState::fromValueTree (tree).quality == params::Quality::Standard);
}
