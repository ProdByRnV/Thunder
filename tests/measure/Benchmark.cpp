// Block-processing benchmark (hidden: run with "[benchmark]"; CI runs it on
// every push and shows the table in the run's summary).
//
// Renders 20 s of a melody through the engine in 256-sample blocks at 48 kHz
// (PRD NFR-PERF-002's conditions) and reports the average and the slowest
// block, as a share of the block's duration on one core. The slowest block is
// what can cause a dropout; the average is the CPU meter's reading. Each case
// is run three times and the best run is kept, which hides most of the noise
// from other processes. With THUNDER_BENCHMARK_OUT set, the table is also
// written to that file as Markdown.

#include "support/TestSignals.h"

#include "core/engine/ThunderEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdio>
#include <vector>

using namespace thunder;
using namespace thunder::test;
using params::ParamId;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kBlock = 256;

struct Result
{
    double averagePercent = 0.0;
    double worstPercent = 0.0;
};

Result measure (const params::ParamSnapshot& settings, params::Quality quality, const std::vector<float>& input)
{
    const auto blockMs = kBlock / kRate * 1000.0;
    Result best{1.0e9, 1.0e9};
    for (int run = 0; run < 3; ++run)
    {
        ThunderEngine engine;
        engine.prepare (kRate, quality);
        std::vector<float> left = input, right = input;

        // One second to warm up (caches, the reverb filling), then timed.
        const auto warmUp = static_cast<std::size_t> (kRate);
        double totalMs = 0.0, worstMs = 0.0;
        for (std::size_t start = 0; start + kBlock <= left.size(); start += kBlock)
        {
            float* channels[] = {left.data() + start, right.data() + start};
            const auto t0 = juce::Time::getHighResolutionTicks();
            engine.process (channels, kBlock, {}, settings);
            const auto ms =
                juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - t0) * 1000.0;
            if (start >= warmUp)
            {
                totalMs += ms;
                worstMs = std::max (worstMs, ms);
            }
        }
        const auto blocks = static_cast<double> ((left.size() - warmUp) / kBlock);
        best.averagePercent = std::min (best.averagePercent, totalMs / blocks / blockMs * 100.0);
        best.worstPercent = std::min (best.worstPercent, worstMs / blockMs * 100.0);
    }
    return best;
}
} // namespace

TEST_CASE ("Block-processing benchmark", "[.][measure][benchmark]")
{
    const auto input =
        tone ([] (double t) { return 57.0 + 2.0 * std::floor (std::fmod (t * 4.0, 6.0)); }, kRate, 21.0, 8);

    const auto defaults = params::ParamSnapshot::defaults();
    auto everything = defaults;
    for (const auto id : {ParamId::timeOn, ParamId::staticOn, ParamId::shapeOn, ParamId::duckOn, ParamId::autopanOn,
                          ParamId::staticTgtPitch, ParamId::staticTgtLevel, ParamId::staticTgtPosition,
                          ParamId::staticTgtFilter, ParamId::staticTgtPan})
        everything.set (id, 1.0f);
    everything.set (ParamId::staticRate, 8.0f);

    struct Case
    {
        const char* name;
        const params::ParamSnapshot* settings;
        params::Quality quality;
    };
    const Case cases[] = {{"Defaults, Eco", &defaults, params::Quality::Eco},
                          {"Defaults, Standard", &defaults, params::Quality::Standard},
                          {"Defaults, High", &defaults, params::Quality::High},
                          {"Every module on, Standard", &everything, params::Quality::Standard}};

    juce::String table;
    table << "| Case (48 kHz, 256 samples, one core) | Average | Slowest block |\n|---|---|---|\n";
    for (const auto& c : cases)
    {
        const auto r = measure (*c.settings, c.quality, input);
        std::printf ("  %-28s average %5.2f%%, slowest block %6.2f%%\n", c.name, r.averagePercent, r.worstPercent);
        table << "| " << c.name << " | " << juce::String (r.averagePercent, 2) << "% | "
              << juce::String (r.worstPercent, 1) << "% |\n";
    }

    if (const auto out = juce::SystemStats::getEnvironmentVariable ("THUNDER_BENCHMARK_OUT", {}); out.isNotEmpty())
        juce::File::getCurrentWorkingDirectory().getChildFile (out).replaceWithText (table);
}
