// The engine's host-independent behaviour: any block size, bad samples,
// smoothed gains (PRD FR-CORE-002, FR-CORE-008, NFR-REL-003, NFR-REL-005).

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

TEST_CASE ("At default settings the engine passes audio through unchanged", "[unit][engine]")
{
    const auto input = makeNoise (4096);
    const auto output = render (input, 512, ParamSnapshot::defaults());

    CHECK (output.left == input.left);
    CHECK (output.right == input.right);
}

TEST_CASE ("The result doesn't depend on the host's block size", "[unit][engine]")
{
    auto snapshot = ParamSnapshot::defaults();
    snapshot.set (ParamId::inputGain, -6.0f);
    snapshot.set (ParamId::outputGain, 3.0f);

    const auto input = makeNoise (48000);
    const auto reference = render (input, static_cast<int> (input.size()), snapshot);

    for (const auto blockSize : {1, 17, 64, 512, 4096})
    {
        INFO ("block size " << blockSize);
        const auto output = render (input, blockSize, snapshot);
        CHECK (maxAbsDifference (output.left, reference.left) < 1.0e-6f);
        CHECK (maxAbsDifference (output.right, reference.right) < 1.0e-6f);
    }

    // Both gains applied: -6 dB + 3 dB.
    const auto expectedGain = juce::Decibels::decibelsToGain (-3.0f);
    CHECK_THAT (reference.left[1000], WithinAbs (input.left[1000] * expectedGain, 1e-5));
}

TEST_CASE ("NaN and infinite input comes out as silence, never as noise", "[unit][engine]")
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);

    Stereo audio (64, 0.25f);
    audio.left[3] = std::numeric_limits<float>::quiet_NaN();
    audio.right[10] = std::numeric_limits<float>::infinity();
    audio.right[11] = -std::numeric_limits<float>::infinity();

    float* channels[] = {audio.left.data(), audio.right.data()};
    engine.process (channels, 64, {}, ParamSnapshot::defaults());

    CHECK (std::all_of (audio.left.begin(), audio.left.end(), [] (float x) { return std::isfinite (x); }));
    CHECK (std::all_of (audio.right.begin(), audio.right.end(), [] (float x) { return std::isfinite (x); }));
    CHECK_THAT (audio.left[3], WithinAbs (0.0, 1e-12));
    CHECK_THAT (audio.left[4], WithinAbs (0.25, 1e-12));
    CHECK (engine.getNonFiniteSamplesReplaced() == 3u);

    // Huge but finite input pushed past the float range by the gain is caught too.
    auto loud = ParamSnapshot::defaults();
    loud.set (ParamId::inputGain, 24.0f);
    std::fill (audio.left.begin(), audio.left.end(), std::numeric_limits<float>::max());
    engine.process (channels, 64, {}, loud);
    CHECK (std::all_of (audio.left.begin(), audio.left.end(), [] (float x) { return std::isfinite (x); }));
}

TEST_CASE ("Gain changes are smoothed, never stepped", "[unit][engine]")
{
    ThunderEngine engine;
    engine.prepare (kSampleRate);

    Stereo audio (4096, 1.0f);
    float* channels[] = {audio.left.data(), audio.right.data()};

    engine.process (channels, 256, {}, ParamSnapshot::defaults());

    auto quieter = ParamSnapshot::defaults();
    quieter.set (ParamId::inputGain, -24.0f);
    engine.process (channels, 4096, {}, quieter);

    const auto rampSamples = static_cast<float> (util::kDefaultRampSeconds * kSampleRate);
    const auto maxStep =
        (1.0f - juce::Decibels::decibelsToGain (-24.0f)) / rampSamples * 1.01f; // 1% rounding allowance

    float worstStep = 0.0f;
    for (std::size_t i = 1; i < audio.size(); ++i)
        worstStep = std::max (worstStep, std::abs (audio.left[i] - audio.left[i - 1]));

    CHECK (worstStep <= maxStep);
    CHECK_THAT (audio.left.back(), WithinAbs (juce::Decibels::decibelsToGain (-24.0f), 1e-6));
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
