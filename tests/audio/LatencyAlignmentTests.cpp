// Latency alignment (PRD QA-AUDIO-002, ROADMAP T-0.1-13): an impulse through
// the whole engine, pitch at unison, must come out at exactly the latency the
// engine reports, in both the dry path and the layer. If the report were wrong,
// hosts would misalign Thunder against other tracks, and the dry and layer
// would smear against each other.

#include "support/TestSignals.h"

#include "core/engine/ThunderEngine.h"
#include "plugin/ThunderProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
// Renders a mono signal through a fresh engine in host blocks of `blockSize`.
std::vector<float> render (double sampleRate, const std::vector<float>& input, const ParamSnapshot& snapshot,
                           std::size_t blockSize, int* latency = nullptr)
{
    ThunderEngine engine;
    engine.prepare (sampleRate);
    if (latency != nullptr)
        *latency = engine.getLatencySamples();

    std::vector<float> left = input, right = input;
    for (std::size_t start = 0; start < input.size(); start += blockSize)
    {
        const auto n = std::min (blockSize, input.size() - start);
        float* channels[] = {left.data() + start, right.data() + start};
        engine.process (channels, static_cast<int> (n), {}, snapshot);
    }
    return left;
}

// Unison, with no reverb, grains or filters: the output is the aligned dry plus
// the (unshifted) pitch path, which is what carries the latency. Auto Gain and the
// limiter are off so the impulse keeps its height (the limiter still delays
// it: its look-ahead is part of the latency).
ParamSnapshot unisonDry()
{
    auto s = ParamSnapshot::defaults();
    test::setSemitoneShift (s, 0.0f);
    s.set (ParamId::spaceMix, 0.0f);
    s.set (ParamId::grainOn, 0.0f);
    s.set (ParamId::toneOn, 0.0f);
    s.set (ParamId::autoGain, 0.0f);
    s.set (ParamId::safetyLimiter, 0.0f);
    return s;
}
} // namespace

TEST_CASE ("An impulse comes out at exactly the reported latency", "[unit][audio][latency]")
{
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        for (const auto blockSize : {std::size_t{64}, std::size_t{512}, std::size_t{1000}})
        {
            INFO ("sample rate " << sampleRate << ", block size " << blockSize);

            constexpr std::size_t position = 3000;
            int latency = 0;
            const auto out = render (sampleRate, impulse (static_cast<std::size_t> (sampleRate * 0.5), position),
                                     unisonDry(), blockSize, &latency);

            REQUIRE (latency > 0);
            const auto expectedAt = position + static_cast<std::size_t> (latency);

            // Dry and layer arrive together, so the peak is exactly there and is
            // the two summed (about 2).
            CHECK (peakIndex (out) == expectedAt);
            CHECK (out[expectedAt] > 1.5f);

            // Nothing arrives early.
            CHECK (energy (out, 0, expectedAt - 1) < 0.01);
        }
    }
}

TEST_CASE ("With every module on, nothing arrives before the latency", "[unit][audio][latency]")
{
    // Grains, reverb and filters only ever add later copies; the dry impulse
    // still lands exactly on the latency.
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        INFO ("sample rate " << sampleRate);
        constexpr std::size_t position = 3000;
        int latency = 0;
        auto snapshot = ParamSnapshot::defaults();
        test::setSemitoneShift (snapshot, 0.0f);
        snapshot.set (ParamId::safetyLimiter, 0.0f); // keep the impulse's height
        const auto out =
            render (sampleRate, impulse (static_cast<std::size_t> (sampleRate), position), snapshot, 512, &latency);

        const auto expectedAt = position + static_cast<std::size_t> (latency);
        CHECK (energy (out, 0, expectedAt - 1) < 0.01);
        CHECK (out[expectedAt] > 1.0f); // the dry impulse (1) plus the layer's first sample
        CHECK (allFinite (out));
    }
}

TEST_CASE ("The host is told the engine's latency", "[unit][audio][latency]")
{
    ThunderProcessor processor;

    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        processor.prepareToPlay (sampleRate, 512);
        CHECK (processor.getLatencySamples() == processor.getEngine().getLatencySamples());
        CHECK (processor.getLatencySamples() / sampleRate <= 0.1); // PRD NFR-PERF-004
    }
}
