// The engine as a whole, independent of any host: any block size, bad samples,
// gains and reset (PRD FR-CORE-002, FR-CORE-012, NFR-REL-003, NFR-REL-005).

#include "support/TestSignals.h"

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
