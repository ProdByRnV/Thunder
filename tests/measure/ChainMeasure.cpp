// Measurements for the module chain (hidden; run with
// `ThunderTests "[chainmeasure]"`): the CPU of Tone, Width and the whole engine,
// and how long each module's reset takes on the audio thread (a module is reset
// when its switch turns it back on; ROADMAP T-0.4-06).

#include "support/TestSignals.h"

#include "core/dsp/grains/GrainModule.h"
#include "core/dsp/pitch/PitchModule.h"
#include "core/dsp/space/SpaceModule.h"
#include "core/dsp/tone/ToneModule.h"
#include "core/dsp/width/WidthModule.h"
#include "core/engine/ThunderEngine.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace thunder;
using namespace thunder::test;
using params::ParamId;

namespace
{
constexpr double kRate = 48000.0;

double percentOfCore (Module& module, const std::vector<float>& input, const params::ParamSnapshot& snapshot)
{
    runModule (module, std::vector<float> (input.begin(), input.begin() + 48000), snapshot); // warm up
    const auto start = juce::Time::getMillisecondCounterHiRes();
    runModule (module, input, snapshot);
    const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
    return ms / (static_cast<double> (input.size()) / kRate * 1000.0) * 100.0;
}

template<typename M> double resetMilliseconds()
{
    M module;
    module.prepare ({kRate, 256, 2});
    runModule (module, whiteNoise (kRate, 1.0), params::ParamSnapshot::defaults());

    double worst = 0.0;
    for (int i = 0; i < 20; ++i)
    {
        const auto start = juce::Time::getMillisecondCounterHiRes();
        module.reset();
        worst = std::max (worst, juce::Time::getMillisecondCounterHiRes() - start);
    }
    return worst;
}
} // namespace

TEST_CASE ("Chain CPU and reset times", "[.][measure][chainmeasure]")
{
    const auto input =
        tone ([] (double t) { return 57.0 + 2.0 * std::floor (std::fmod (t * 4.0, 6.0)); }, kRate, 10.0, 8);
    const auto defaults = params::ParamSnapshot::defaults();

    std::printf ("\nModule CPU (stereo, 48 kHz, one core):\n");
    {
        dsp::ToneModule tone;
        tone.prepare ({kRate, 256, 2});
        std::printf ("  %-40s %5.2f%%\n", "Tone (defaults)", percentOfCore (tone, input, defaults));
    }
    {
        dsp::WidthModule width;
        width.prepare ({kRate, 256, 2});
        auto s = defaults;
        s.set (ParamId::widthAmount, 150.0f);
        std::printf ("  %-40s %5.2f%%\n", "Width (150%)", percentOfCore (width, input, s));
    }
    {
        ThunderEngine engine;
        engine.prepare (kRate);
        std::vector<float> left = input, right = input;
        const auto run = [&]
        {
            for (std::size_t start = 0; start < left.size(); start += 256)
            {
                const auto n = std::min<std::size_t> (256, left.size() - start);
                float* channels[] = {left.data() + start, right.data() + start};
                engine.process (channels, static_cast<int> (n), {}, defaults);
            }
        };
        run(); // warm up
        left = input;
        right = input;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        run();
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        std::printf ("  %-40s %5.2f%%\n", "Whole engine (defaults)", ms / 10000.0 * 100.0);
    }

    std::printf ("\nReset time on the audio thread (worst of 20):\n");
    std::printf ("  %-40s %6.3f ms\n", "Pitch", resetMilliseconds<dsp::PitchModule>());
    std::printf ("  %-40s %6.3f ms\n", "Grains", resetMilliseconds<dsp::GrainModule>());
    std::printf ("  %-40s %6.3f ms\n", "Space (CloudSeed)", resetMilliseconds<dsp::SpaceModule>());
    std::printf ("  %-40s %6.3f ms\n", "Tone", resetMilliseconds<dsp::ToneModule>());
    std::printf ("  %-40s %6.3f ms\n", "Width", resetMilliseconds<dsp::WidthModule>());
    std::printf ("  (one 256-sample chunk at 48 kHz lasts 5.33 ms)\n");
    SUCCEED();
}
