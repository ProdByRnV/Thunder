// The engine as a whole, independent of any host: any block size, bad samples,
// gains and reset (PRD FR-CORE-002, FR-CORE-012, NFR-REL-003, NFR-REL-005).

#include "support/TestSignals.h"

#include "core/dsp/gain/LoudnessMeter.h"
#include "core/dsp/tone/Svf.h"
#include "core/engine/ThunderEngine.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

using namespace thunder;
using Catch::Matchers::WithinAbs;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
constexpr double kSampleRate = 48000.0;

struct Stereo
{
    std::vector<float> left, right;

    explicit Stereo (std::size_t n, float value = 0.0f)
        : left (n, value)
        , right (n, value)
    {
    }

    std::size_t size() const { return left.size(); }
};

// Deterministic test noise in -1..1 (its own generator, so it is identical on
// every platform).
Stereo makeNoise (std::size_t n)
{
    Stereo s (n);
    std::uint32_t state = 12345u;
    auto next = [&state]
    {
        state = state * 1664525u + 1013904223u;
        return static_cast<float> (state >> 8) / static_cast<float> (1u << 24) * 2.0f - 1.0f;
    };
    for (std::size_t i = 0; i < n; ++i)
    {
        s.left[i] = next();
        s.right[i] = next();
    }
    return s;
}

// Runs `input` through a freshly prepared engine in blocks of `blockSize`.
Stereo render (const Stereo& input, int blockSize, const ParamSnapshot& snapshot)
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);

    auto output = input;
    for (std::size_t start = 0; start < output.size(); start += static_cast<std::size_t> (blockSize))
    {
        const auto n = std::min (static_cast<std::size_t> (blockSize), output.size() - start);
        float* channels[] = {output.left.data() + start, output.right.data() + start};
        engine.process (channels, static_cast<int> (n), {}, snapshot);
    }
    return output;
}

float maxAbsDifference (const std::vector<float>& a, const std::vector<float>& b)
{
    float worst = 0.0f;
    for (std::size_t i = 0; i < a.size(); ++i)
        worst = std::max (worst, std::abs (a[i] - b[i]));
    return worst;
}
} // namespace

TEST_CASE ("The result doesn't depend on the host's block size", "[unit][engine]")
{
    // Pitch shifted and reverb on, so every stage of the chain is covered.
    auto snapshot = ParamSnapshot::defaults();
    test::setSemitoneShift (snapshot, 7.0f);
    snapshot.set (ParamId::inputGain, -6.0f);
    snapshot.set (ParamId::outputGain, 3.0f);

    const auto input = makeNoise (24000);
    const auto reference = render (input, static_cast<int> (input.size()), snapshot);

    for (const auto blockSize : {1, 17, 64, 512, 4096})
    {
        INFO ("block size " << blockSize);
        const auto output = render (input, blockSize, snapshot);
        CHECK (maxAbsDifference (output.left, reference.left) < 1.0e-5f);
        CHECK (maxAbsDifference (output.right, reference.right) < 1.0e-5f);
    }
}

TEST_CASE ("Output Gain scales the whole output", "[unit][engine]")
{
    auto snapshot = ParamSnapshot::defaults();
    test::setSemitoneShift (snapshot, 12.0f);
    snapshot.set (ParamId::safetyLimiter, 0.0f); // the full-scale noise would be limited

    const auto input = makeNoise (12000);
    const auto unity = render (input, 512, snapshot);

    snapshot.set (ParamId::outputGain, 6.0f);
    const auto louder = render (input, 512, snapshot);

    const auto gain = juce::Decibels::decibelsToGain (6.0f);
    float worst = 0.0f;
    for (std::size_t i = 0; i < unity.size(); ++i)
        worst = std::max (worst, std::abs (louder.left[i] - unity.left[i] * gain));
    CHECK (worst < 1.0e-5f);
}

TEST_CASE ("NaN and infinite input never reaches the output", "[unit][engine]")
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);

    Stereo audio (512, 0.25f);
    audio.left[3] = std::numeric_limits<float>::quiet_NaN();
    audio.right[10] = std::numeric_limits<float>::infinity();
    audio.right[11] = -std::numeric_limits<float>::infinity();

    float* channels[] = {audio.left.data(), audio.right.data()};
    engine.process (channels, 512, {}, ParamSnapshot::defaults());

    CHECK (std::all_of (audio.left.begin(), audio.left.end(), [] (float x) { return std::isfinite (x); }));
    CHECK (std::all_of (audio.right.begin(), audio.right.end(), [] (float x) { return std::isfinite (x); }));
    CHECK (engine.getNonFiniteSamplesReplaced() == 3u);
}

TEST_CASE ("Absurdly loud input can't break the engine", "[unit][engine]")
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);

    auto loud = ParamSnapshot::defaults();
    loud.set (ParamId::inputGain, 24.0f);
    test::setSemitoneShift (loud, 7.0f);

    // Finite, but far beyond anything real: the gain overflows it, and what is
    // left overflows the pitch engine's maths. Whatever happens inside, the
    // output stays finite and a module that went bad is reset.
    Stereo audio (4096, 1.0e30f);
    float* channels[] = {audio.left.data(), audio.right.data()};
    engine.process (channels, 4096, {}, loud);
    CHECK (std::all_of (audio.left.begin(), audio.left.end(), [] (float x) { return std::isfinite (x); }));

    // Normal audio afterwards plays normally.
    auto normal = makeNoise (24000);
    float* normalChannels[] = {normal.left.data(), normal.right.data()};
    engine.process (normalChannels, 24000, {}, ParamSnapshot::defaults());
    CHECK (std::all_of (normal.left.begin(), normal.left.end(), [] (float x) { return std::isfinite (x); }));
    CHECK (std::abs (normal.left[20000]) > 0.0f);
}

TEST_CASE ("Reset clears every tail", "[unit][engine]")
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);

    auto snapshot = ParamSnapshot::defaults();
    snapshot.set (ParamId::spaceDecay, 100.0f);

    auto noise = makeNoise (24000);
    float* channels[] = {noise.left.data(), noise.right.data()};
    engine.process (channels, 24000, {}, snapshot);

    engine.reset();

    Stereo silence (24000);
    float* silent[] = {silence.left.data(), silence.right.data()};
    engine.process (silent, 24000, {}, snapshot);
    CHECK (std::all_of (silence.left.begin(), silence.left.end(), [] (float x) { return std::abs (x) < 1.0e-9f; }));
}

TEST_CASE ("Blocks longer than the engine's chunk size keep their musical position", "[unit][engine]")
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);

    TransportState transport;
    transport.isPlaying = true;

    Stereo audio (5000);
    float* channels[] = {audio.left.data(), audio.right.data()};
    engine.process (channels, 5000, transport, ParamSnapshot::defaults());

    // The last chunk starts 19 × 256 samples into the block.
    const auto lastChunkStart = 19.0 * ThunderEngine::kMaxChunkSize;
    CHECK_THAT (engine.getTransport().ppqPosition, WithinAbs (lastChunkStart / 24000.0, 1e-9));
}

TEST_CASE ("An engine that was never prepared outputs silence", "[unit][engine]")
{
    ThunderEngine engine;
    Stereo audio (32, 0.5f);
    float* channels[] = {audio.left.data(), audio.right.data()};

    engine.process (channels, 32, {}, ParamSnapshot::defaults());

    CHECK (std::all_of (audio.left.begin(), audio.left.end(), [] (float x) { return std::abs (x) < 1.0e-12f; }));
}

TEST_CASE ("Two engines given the same input sound identical", "[unit][engine]")
{
    // A reopened session or an offline bounce makes a new engine; it must sound
    // exactly like the one before (PRD NFR-REL-001, NFR-REL-005).
    auto snapshot = ParamSnapshot::defaults();
    test::setSemitoneShift (snapshot, 7.0f);
    snapshot.set (ParamId::spaceMix, 100.0f);

    const auto input = makeNoise (24000);
    const auto first = render (input, 512, snapshot);
    const auto second = render (input, 512, snapshot);

    CHECK (first.left == second.left);
    CHECK (first.right == second.right);
}

TEST_CASE ("Grains sit in the chain after Pitch, with their own switch", "[unit][engine][grains]")
{
    // PRD DEC-005, ROADMAP T-0.3-06. Grains add no latency, and their tail
    // comes before the reverb's.
    ThunderEngine engine;
    engine.prepare (kSampleRate);
    dsp::PitchModule pitch;
    dsp::SpaceModule space;
    dsp::SafetyLimiter limiter;
    pitch.prepare ({kSampleRate, ThunderEngine::kMaxChunkSize, 2});
    space.prepare ({kSampleRate, ThunderEngine::kMaxChunkSize, 2});
    limiter.prepare (kSampleRate);
    CHECK (engine.getLatencySamples() ==
           pitch.getLatencySamples() + space.getLatencySamples() + limiter.getLatencySamples());
    CHECK_THAT (engine.getTailSeconds(),
                WithinAbs (dsp::TimeModule::kMaxSegmentSeconds + dsp::GrainModule::kBufferSeconds +
                               3.0 * dsp::GrainModule::kMaxGrainSeconds + space.getTailSeconds(),
                           1e-9));

    // With the reverb dry, Grains on (the default) changes the sound, and
    // switching them off gives a different result whose latency is the same.
    auto snapshot = ParamSnapshot::defaults();
    snapshot.set (ParamId::spaceMix, 0.0f);
    const auto input = makeNoise (48000);
    const auto withGrains = render (input, 256, snapshot);
    snapshot.set (ParamId::grainOn, 0.0f);
    const auto without = render (input, 256, snapshot);

    CHECK (maxAbsDifference (withGrains.left, without.left) > 0.01f);
    CHECK (test::allFinite (withGrains.left));
    CHECK (test::allFinite (withGrains.right));
}

TEST_CASE ("The grains in the engine start from the first sample after a reset", "[unit][engine][grains]")
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);

    auto noise = makeNoise (48000);
    float* channels[] = {noise.left.data(), noise.right.data()};
    engine.process (channels, 48000, {}, ParamSnapshot::defaults());
    CHECK (engine.getGrains().getGrainsStarted() > 0);

    engine.reset();
    CHECK (engine.getGrains().getActiveGrains() == 0);
    CHECK (engine.getGrains().getBuffer().getWriteCount() == 0);
}

// ---- Subphase 2E: Output Mode, Mix, module switches (PRD FR-CORE-005/006/009,
// FR-GAIN-005, DEC-015; ROADMAP T-0.4-03/06)

namespace
{
// Every module off, and Auto Gain and the limiter too, so the layer is the
// input, untouched.
ParamSnapshot allModulesOff()
{
    auto s = ParamSnapshot::defaults();
    for (const auto id : {ParamId::pitchOn, ParamId::grainOn, ParamId::spaceOn, ParamId::toneOn, ParamId::widthOn,
                          ParamId::autoGain, ParamId::safetyLimiter})
        s.set (id, 0.0f);
    return s;
}

float largestStep (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    float largest = 0.0f;
    for (auto i = std::max<std::size_t> (from, 1); i < std::min (to, s.size()); ++i)
        largest = std::max (largest, std::abs (s[i] - s[i - 1]));
    return largest;
}

// Renders `input` with the snapshot chosen per 256-sample chunk.
template<typename SnapshotFor> Stereo renderChanging (const Stereo& input, SnapshotFor snapshotFor)
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);
    auto output = input;
    for (std::size_t start = 0, chunk = 0; start < output.size(); start += 256, ++chunk)
    {
        const auto n = std::min<std::size_t> (256, output.size() - start);
        float* channels[] = {output.left.data() + start, output.right.data() + start};
        engine.process (channels, static_cast<int> (n), {}, snapshotFor (chunk));
    }
    return output;
}

Stereo makeSine (std::size_t n, double hz = 220.0)
{
    Stereo s (n);
    s.left = test::sine (hz, kSampleRate, static_cast<double> (n) / kSampleRate, 0.25f);
    s.right = s.left;
    return s;
}
} // namespace

TEST_CASE ("The Mix law is unity at the centre", "[unit][engine][output]")
{
    using params::OutputMode;
    const auto check = [] (OutputMode mode, float mix, float dry, float layer)
    {
        INFO ("mix " << mix);
        const auto [d, l] = OutputStage::gainsFor (mode, mix);
        CHECK_THAT (d, WithinAbs (dry, 1e-7));
        CHECK_THAT (l, WithinAbs (layer, 1e-7));
    };
    check (OutputMode::Blend, 0.0f, 1.0f, 0.0f);
    check (OutputMode::Blend, 0.25f, 1.0f, 0.5f);
    check (OutputMode::Blend, 0.5f, 1.0f, 1.0f);
    check (OutputMode::Blend, 0.75f, 0.5f, 1.0f);
    check (OutputMode::Blend, 1.0f, 0.0f, 1.0f);
    for (const auto mix : {0.0f, 0.5f, 1.0f})
        check (OutputMode::LayerOnly, mix, 0.0f, 1.0f);
}

TEST_CASE ("Mix at 0% outputs the dry signal alone, exactly aligned", "[unit][engine][output]")
{
    auto snapshot = ParamSnapshot::defaults();
    snapshot.set (ParamId::mix, 0.0f);
    snapshot.set (ParamId::safetyLimiter, 0.0f); // the full-scale noise would be limited
    const auto input = makeNoise (24000);
    const auto out = render (input, 512, snapshot);

    ThunderEngine engine;
    engine.prepare (kSampleRate);
    const auto latency = static_cast<std::size_t> (engine.getLatencySamples());
    for (std::size_t i = latency; i < out.size(); ++i)
    {
        REQUIRE (juce::exactlyEqual (out.left[i], input.left[i - latency]));
        REQUIRE (juce::exactlyEqual (out.right[i], input.right[i - latency]));
    }
}

TEST_CASE ("With every module off the latency stays, and Layer Only is the delayed input", "[unit][engine][output]")
{
    // The switched-off Pitch module still delays its input by its latency, so
    // the layer stays aligned with the dry signal (ARCHITECTURE §5.3, AD-02).
    ThunderEngine engine;
    engine.prepare (kSampleRate);
    const auto latency = static_cast<std::size_t> (engine.getLatencySamples());
    REQUIRE (latency > 0);

    const auto input = makeNoise (24000);

    auto layerOnly = allModulesOff();
    test::setChoice (layerOnly, ParamId::outputMode, params::OutputMode::LayerOnly);
    const auto layer = render (input, 512, layerOnly);
    for (std::size_t i = latency; i < layer.size(); ++i)
        REQUIRE (juce::exactlyEqual (layer.left[i], input.left[i - latency]));

    // Blend at 50%: dry and layer both at unity, so twice the delayed input.
    const auto blend = render (input, 512, allModulesOff());
    for (std::size_t i = latency; i < blend.size(); ++i)
        REQUIRE_THAT (blend.left[i], WithinAbs (2.0f * input.left[i - latency], 1e-6));
}

TEST_CASE ("Mix 100% sounds like Layer Only", "[unit][engine][output]")
{
    auto full = ParamSnapshot::defaults();
    full.set (ParamId::mix, 100.0f);
    auto layerOnly = ParamSnapshot::defaults();
    test::setChoice (layerOnly, ParamId::outputMode, params::OutputMode::LayerOnly);

    const auto input = makeNoise (24000);
    const auto a = render (input, 512, full);
    const auto b = render (input, 512, layerOnly);
    CHECK (a.left == b.left);
    CHECK (a.right == b.right);
}

TEST_CASE ("Switching modules, Output Mode or Mix during playback never clicks", "[unit][engine][output]")
{
    // A 220 Hz sine with the reverb dry (its tail makes steps hard to judge).
    // Each switch toggles every 0.25 s; the largest step anywhere must stay
    // close to the largest step of the same render settled on or off.
    auto base = ParamSnapshot::defaults();
    base.set (ParamId::spaceMix, 0.0f);
    const auto input = makeSine (static_cast<std::size_t> (2.0 * kSampleRate));
    const auto period = static_cast<std::size_t> (0.25 * kSampleRate) / 256;

    struct Switch
    {
        const char* name;
        ParamId id;
        float on, off;
    };
    const Switch switches[] = {
        {"pitchOn", ParamId::pitchOn, 1.0f, 0.0f}, {"grainOn", ParamId::grainOn, 1.0f, 0.0f},
        {"spaceOn", ParamId::spaceOn, 1.0f, 0.0f}, {"toneOn", ParamId::toneOn, 1.0f, 0.0f},
        {"widthOn", ParamId::widthOn, 1.0f, 0.0f}, {"outputMode", ParamId::outputMode, 0.0f, 1.0f},
        {"mix", ParamId::mix, 50.0f, 100.0f},
    };

    for (const auto& sw : switches)
    {
        INFO (sw.name);
        auto on = base, off = base;
        on.set (sw.id, sw.on);
        off.set (sw.id, sw.off);

        const auto settledOn = render (input, 256, on);
        const auto settledOff = render (input, 256, off);
        const auto toggling =
            renderChanging (input, [&] (std::size_t chunk) { return (chunk / period) % 2 == 0 ? on : off; });

        const auto from = static_cast<std::size_t> (0.5 * kSampleRate); // past the start-up
        const auto steady = std::max (largestStep (settledOn.left, from, input.size()),
                                      largestStep (settledOff.left, from, input.size()));
        CHECK (test::allFinite (toggling.left));
        CHECK (largestStep (toggling.left, from, input.size()) < 1.25f * steady + 0.005f);
    }
}

// ---- Subphase 2F: gain staging, limiter, reset and tails (PRD FR-GAIN-001…004,
// FR-CORE-011/012, DEC-043; ROADMAP T-0.4-04/05/07)

namespace
{
Stereo makeMono (const std::vector<float>& s)
{
    Stereo out (s.size());
    out.left = s;
    out.right = s;
    return out;
}

// K-weighted loudness (dB) of a signal from `from` to the end.
double loudnessDb (const std::vector<float>& s, std::size_t from)
{
    dsp::LoudnessMeter meter;
    meter.prepare (kSampleRate);
    double sum = 0.0;
    std::size_t n = 0;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const auto ms = meter.push (&s[i], 1);
        if (i >= from)
        {
            sum += ms;
            ++n;
        }
    }
    return 10.0 * std::log10 (sum / static_cast<double> (n) + 1e-20);
}

// Low notes (A1 to E2) with harmonics: plenty of energy below 100 Hz.
std::vector<float> bassMelody (double seconds)
{
    return test::tone ([] (double t) { return 33.0 + 2.0 * std::floor (std::fmod (t * 2.0, 4.0)); }, kSampleRate,
                       seconds, 8);
}

// Energy below about 100 Hz: three passes through a 4-pole low-pass at the
// Tone module's lowest low-pass cutoff (1 kHz) would be too gentle, so a
// dedicated 100 Hz 4-pole low-pass made from the same filter sections.
double energyBelow100Hz (const std::vector<float>& s, std::size_t from)
{
    dsp::Svf first, second;
    const auto c1 = dsp::Svf::Coefficients::make (100.0 / kSampleRate, 1.8477590650225735);
    const auto c2 = dsp::Svf::Coefficients::make (100.0 / kSampleRate, 0.7653668647301796);
    double energy = 0.0;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const auto y = second.process (0, first.process (0, s[i], c1, false), c2, false);
        if (i >= from)
            energy += static_cast<double> (y) * static_cast<double> (y);
    }
    return energy;
}
} // namespace

TEST_CASE ("At the defaults nothing leaves above -0.3 dBFS", "[unit][engine][gain]")
{
    // PRD FR-GAIN-003, ROADMAP v0.4 exit criterion: a melody, a chord, noise and
    // a bass line, all hot (peaks near full scale), also with the input +12 dB.
    std::vector<std::vector<float>> signals{
        test::tone ([] (double t) { return 57.0 + 2.0 * std::floor (std::fmod (t * 4.0, 6.0)); }, kSampleRate, 4.0, 8),
        test::chord ({57.0, 61.0, 64.0}, kSampleRate, 4.0), test::whiteNoise (kSampleRate, 4.0), bassMelody (4.0)};

    for (auto& s : signals)
    {
        float peak = 0.0f;
        for (const auto x : s)
            peak = std::max (peak, std::abs (x));
        for (auto& x : s)
            x *= 0.99f / peak;
    }

    for (const auto inputDb : {0.0f, 12.0f})
    {
        auto snapshot = ParamSnapshot::defaults();
        snapshot.set (ParamId::inputGain, inputDb);
        for (std::size_t k = 0; k < signals.size(); ++k)
        {
            INFO ("signal " << k << ", input " << inputDb << " dB");
            const auto out = render (makeMono (signals[k]), 512, snapshot);
            float peak = 0.0f;
            for (std::size_t i = 0; i < out.size(); ++i)
                peak = std::max ({peak, std::abs (out.left[i]), std::abs (out.right[i])});
            CHECK (peak <= dsp::SafetyLimiter::kCeiling);
        }
    }
}

TEST_CASE ("Layer Level scales the layer alone", "[unit][engine][gain]")
{
    // Layer Only, Auto Gain and the limiter off: the output is the layer.
    auto snapshot = ParamSnapshot::defaults();
    test::setChoice (snapshot, ParamId::outputMode, params::OutputMode::LayerOnly);
    snapshot.set (ParamId::autoGain, 0.0f);
    snapshot.set (ParamId::safetyLimiter, 0.0f);

    const auto input = makeSine (24000);
    const auto unity = render (input, 512, snapshot);
    snapshot.set (ParamId::layerLevel, -12.0f);
    const auto quieter = render (input, 512, snapshot);

    const auto g = juce::Decibels::decibelsToGain (-12.0f);
    float worst = 0.0f;
    for (std::size_t i = 0; i < unity.size(); ++i)
        worst = std::max (worst, std::abs (quieter.left[i] - unity.left[i] * g));
    CHECK (worst < 1e-6f);
}

TEST_CASE ("At the defaults the layer sits 6 dB under the dry signal, without its low end", "[unit][engine][gain]")
{
    // ROADMAP v0.4 exit criterion, measured on synthetic material (listening
    // follows at T-1.0-03). The layer alone (Layer Only) against the dry alone
    // (Mix 0%), over the last 4 s, after Auto Gain has settled.
    const auto melody = makeMono (bassMelody (10.0));
    auto layerOnly = ParamSnapshot::defaults();
    test::setChoice (layerOnly, ParamId::outputMode, params::OutputMode::LayerOnly);
    layerOnly.set (ParamId::safetyLimiter, 0.0f);
    auto dryOnly = ParamSnapshot::defaults();
    dryOnly.set (ParamId::mix, 0.0f);
    dryOnly.set (ParamId::safetyLimiter, 0.0f);

    const auto layer = render (melody, 512, layerOnly);
    const auto dryOut = render (melody, 512, dryOnly);
    const auto from = static_cast<std::size_t> (6.0 * kSampleRate);

    const auto difference = loudnessDb (layer.left, from) - loudnessDb (dryOut.left, from);
    CHECK_THAT (difference, WithinAbs (dsp::AutoGain::kOffsetDb, 1.5));

    // Below 100 Hz the layer has at least 20 dB less than the dry signal: the
    // high-pass keeps it out of the bass.
    CHECK (10.0 * std::log10 (energyBelow100Hz (layer.left, from) / energyBelow100Hz (dryOut.left, from)) < -20.0);
}

TEST_CASE ("Reset clears everything: afterwards the engine sounds like a fresh one", "[unit][engine][reset]")
{
    // PRD FR-CORE-012. With the reverb still ringing at the reset, the full
    // clear runs, so the result is exactly a fresh engine's.
    const auto input = makeNoise (24000);
    const auto snapshot = ParamSnapshot::defaults();
    const auto fresh = render (input, 512, snapshot);

    ThunderEngine engine;
    engine.prepare (kSampleRate);
    auto first = makeNoise (48000);
    float* firstChannels[] = {first.left.data(), first.right.data()};
    engine.process (firstChannels, 48000, {}, snapshot);
    engine.reset();

    auto second = input;
    for (std::size_t start = 0; start < second.size(); start += 512)
    {
        const auto n = std::min<std::size_t> (512, second.size() - start);
        float* channels[] = {second.left.data() + start, second.right.data() + start};
        engine.process (channels, static_cast<int> (n), {}, snapshot);
    }
    CHECK (second.left == fresh.left);
    CHECK (second.right == fresh.right);
}

TEST_CASE ("The reverb knows when it is quiet, so a reset can skip the slow clear", "[unit][engine][reset]")
{
    // PRD DEC-043: reset() clears CloudSeed's delay lines (7–10 ms) only while
    // something is still in them.
    dsp::SpaceModule space;
    space.prepare ({kSampleRate, 256, 2});
    const auto snapshot = ParamSnapshot::defaults();
    CHECK (space.isQuiet()); // freshly prepared

    test::runModule (space, test::whiteNoise (kSampleRate, 0.5), snapshot);
    CHECK_FALSE (space.isQuiet());

    // Silence until the tail has died away (decay 50%: a few seconds).
    const auto tail =
        test::runModule (space, std::vector<float> (static_cast<std::size_t> (20.0 * kSampleRate), 0.0f), snapshot);
    CHECK (space.isQuiet());
    CHECK (std::abs (tail.back()) < 1e-6f);

    space.reset();
    CHECK (space.isQuiet());
}

TEST_CASE ("Tails ring out and stop within the reported tail length", "[unit][engine][reset]")
{
    // PRD FR-CORE-011: after the input stops, grains and reverb decay naturally
    // and are gone well within what the host is told.
    auto input = makeNoise (static_cast<std::size_t> (12.0 * kSampleRate));
    std::fill (input.left.begin() + 48000, input.left.end(), 0.0f);
    std::fill (input.right.begin() + 48000, input.right.end(), 0.0f);
    const auto out = render (input, 512, ParamSnapshot::defaults());

    ThunderEngine engine;
    engine.prepare (kSampleRate);
    CHECK (engine.getTailSeconds() >= 5.5);

    // It rings on after the input stops...
    CHECK (test::energy (out.left, 48000 + 24000, 48000 + 48000) > 0.0);
    // ...keeps falling, and 10 s later is below -80 dBFS (the input was full
    // scale), inside the reported tail. The exact level depends on rounding
    // (debug and release builds differ slightly), so the margin is generous.
    const auto peakBetween = [&] (double from, double to)
    {
        float peak = 0.0f;
        for (auto i = static_cast<std::size_t> (from * kSampleRate); i < static_cast<std::size_t> (to * kSampleRate);
             ++i)
            peak = std::max (peak, std::abs (out.left[i]));
        return peak;
    };
    CHECK (peakBetween (11.0, 12.0) < peakBetween (5.0, 6.0));
    CHECK (peakBetween (11.0, 12.0) < 1e-4f);
}
