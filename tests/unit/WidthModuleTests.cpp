// The Width module: mid/side width 0–200%, mid untouched, smooth changes
// (PRD FR-WIDTH-001/003; ROADMAP T-0.4-02).

#include "support/TestSignals.h"

#include "core/dsp/width/WidthModule.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;
using params::ParamId;

namespace
{
constexpr double kRate = 48000.0;

struct Stereo
{
    std::vector<float> left, right;
};

// Two different signals, so there is a side component.
Stereo makeStereo()
{
    return {sine (220.0, kRate, 0.5, 0.4f), sine (330.0, kRate, 0.5, 0.3f)};
}

Stereo runWidth (dsp::WidthModule& width, Stereo s, float percent)
{
    auto snapshot = params::ParamSnapshot::defaults();
    snapshot.set (ParamId::widthAmount, percent);
    const TransportState transport;
    for (std::size_t start = 0; start < s.left.size(); start += 256)
    {
        const auto n = std::min<std::size_t> (256, s.left.size() - start);
        float* channels[] = {s.left.data() + start, s.right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        width.process (block, ProcessContext{transport, snapshot});
    }
    return s;
}
} // namespace

TEST_CASE ("Width at 100% leaves the layer untouched", "[unit][width]")
{
    dsp::WidthModule width;
    width.prepare ({kRate, 256, 2});
    const auto in = makeStereo();
    const auto out = runWidth (width, in, 100.0f);
    CHECK (out.left == in.left);
    CHECK (out.right == in.right);
}

TEST_CASE ("Width scales the side and keeps the mid", "[unit][width]")
{
    const auto in = makeStereo();
    for (const auto percent : {0.0f, 50.0f, 150.0f, 200.0f})
    {
        INFO ("width " << percent);
        dsp::WidthModule width;
        width.prepare ({kRate, 256, 2});
        const auto out = runWidth (width, in, percent);
        const auto w = percent / 100.0f;

        for (std::size_t i = 0; i < in.left.size(); ++i)
        {
            REQUIRE_THAT (out.left[i] + out.right[i], WithinAbs (in.left[i] + in.right[i], 1e-6));
            REQUIRE_THAT (out.left[i] - out.right[i], WithinAbs (w * (in.left[i] - in.right[i]), 1e-6));
        }
    }
}

TEST_CASE ("Width 0% is mono", "[unit][width]")
{
    dsp::WidthModule width;
    width.prepare ({kRate, 256, 2});
    const auto out = runWidth (width, makeStereo(), 0.0f);
    for (std::size_t i = 0; i < out.left.size(); ++i)
        REQUIRE (juce::exactlyEqual (out.left[i], out.right[i]));
}

TEST_CASE ("Changing Width glides instead of jumping", "[unit][width]")
{
    dsp::WidthModule width;
    width.prepare ({kRate, 256, 2});
    const auto in = makeStereo();
    auto s = in;
    const TransportState transport;
    float largest = 0.0f;

    std::size_t processed = 0;
    for (std::size_t start = 0, chunk = 0; start + 256 <= s.left.size(); start += 256, ++chunk, processed = start)
    {
        auto snapshot = params::ParamSnapshot::defaults();
        snapshot.set (ParamId::widthAmount, (chunk / 10) % 2 == 0 ? 0.0f : 200.0f);
        float* channels[] = {s.left.data() + start, s.right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, 256);
        width.process (block, ProcessContext{transport, snapshot});
    }
    for (std::size_t i = 1; i < processed; ++i) // only the whole chunks that were processed
        largest = std::max (largest, std::abs (s.left[i] - s.left[i - 1]));

    // The inputs' own steepest steps are below 0.012 and 0.013; at 200% the
    // left channel can move up to about twice as fast, but never jump.
    CHECK (largest < 0.04f);
}
