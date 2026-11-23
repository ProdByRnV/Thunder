// Measurements for the module chain (hidden; run with
// `ThunderTests "[chainmeasure]"`): the CPU of Tone, Width and the whole engine,
// and how long each module's reset takes on the audio thread (a module is reset
// when its switch turns it back on; ROADMAP T-0.4-06).

#include "support/TestSignals.h"

#include "core/dsp/gain/AutoGain.h"
#include "core/dsp/gain/SafetyLimiter.h"
#include "core/dsp/grains/GrainModule.h"
#include "core/dsp/pitch/PitchModule.h"
#include "core/dsp/shape/ShapeModule.h"
#include "core/dsp/space/SpaceModule.h"
#include "core/dsp/time/TimeModule.h"
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
        dsp::TimeModule time;
        time.prepare ({kRate, 256, 2});
        std::printf ("  %-40s %5.2f%%\n", "Time (halftime 1/2)", percentOfCore (time, input, defaults));
    }
    {
        dsp::ShapeModule shape;
        shape.prepare ({kRate, 256, 2});
        std::printf ("  %-40s %5.2f%%\n", "Shape (Pump 1/4)", percentOfCore (shape, input, defaults));
    }
    {
        dsp::AutoGain autoGain;
        dsp::SafetyLimiter limiter;
        autoGain.prepare (kRate);
        limiter.prepare (kRate);
        std::vector<float> l = input, r = input, dl = input, dr = input;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        for (std::size_t s = 0; s + 256 <= l.size(); s += 256)
        {
            float* layer[] = {l.data() + s, r.data() + s};
            const float* dry[] = {dl.data() + s, dr.data() + s};
            autoGain.process (layer, dry, 2, 256, true);
        }
        const auto mid = juce::Time::getMillisecondCounterHiRes();
        for (std::size_t s = 0; s + 256 <= l.size(); s += 256)
        {
            float* out[] = {dl.data() + s, dr.data() + s};
            limiter.process (out, 2, 256, true);
        }
        const auto end = juce::Time::getMillisecondCounterHiRes();
        std::printf ("  %-40s %5.2f%%\n", "Auto Gain", (mid - start) / 10000.0 * 100.0);
        std::printf ("  %-40s %5.2f%%\n", "Safety limiter", (end - mid) / 10000.0 * 100.0);
    }
    auto rhythm = defaults;
    for (const auto id :
         {ParamId::timeOn, ParamId::staticOn, ParamId::shapeOn, ParamId::staticTgtPitch, ParamId::staticTgtLevel})
        rhythm.set (id, 1.0f);
    rhythm.set (ParamId::staticRate, 8.0f);

    struct EngineCase
    {
        const char* name;
        const params::ParamSnapshot* settings;
        params::Quality quality;
    };
    for (const auto& c : {EngineCase{"Whole engine (defaults)", &defaults, params::Quality::Standard},
                          EngineCase{"Whole engine (defaults, Eco)", &defaults, params::Quality::Eco},
                          EngineCase{"Whole engine (defaults, High)", &defaults, params::Quality::High},
                          EngineCase{"Whole engine + Time, Static (all), Shape", &rhythm, params::Quality::Standard}})
    {
        const auto* settings = c.settings;
        ThunderEngine engine;
        engine.prepare (kRate, c.quality);
        std::vector<float> left = input, right = input;
        const auto run = [&]
        {
            for (std::size_t start = 0; start < left.size(); start += 256)
            {
                const auto n = std::min<std::size_t> (256, left.size() - start);
                float* channels[] = {left.data() + start, right.data() + start};
                engine.process (channels, static_cast<int> (n), {}, *settings);
            }
        };
        run(); // warm up
        left = input;
        right = input;
        const auto start = juce::Time::getMillisecondCounterHiRes();
        run();
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;
        std::printf ("  %-40s %5.2f%%\n", c.name, ms / 10000.0 * 100.0);
    }

    std::printf ("\nReset time on the audio thread (worst of 20):\n");
    std::printf ("  %-40s %6.3f ms\n", "Pitch", resetMilliseconds<dsp::PitchModule>());
    std::printf ("  %-40s %6.3f ms\n", "Grains", resetMilliseconds<dsp::GrainModule>());
    std::printf ("  %-40s %6.3f ms\n", "Space (CloudSeed)", resetMilliseconds<dsp::SpaceModule>());
    std::printf ("  %-40s %6.3f ms\n", "Tone", resetMilliseconds<dsp::ToneModule>());
    std::printf ("  %-40s %6.3f ms\n", "Width", resetMilliseconds<dsp::WidthModule>());
    std::printf ("  %-40s %6.3f ms\n", "Time", resetMilliseconds<dsp::TimeModule>());
    std::printf ("  %-40s %6.3f ms\n", "Shape", resetMilliseconds<dsp::ShapeModule>());
    {
        // The usual case for a host reset: the tail has died away while stopped.
        dsp::SpaceModule space;
        space.prepare ({kRate, 256, 2});
        runModule (space, whiteNoise (kRate, 1.0), defaults);
        runModule (space, std::vector<float> (static_cast<std::size_t> (20.0 * kRate), 0.0f), defaults);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        space.reset();
        std::printf ("  %-40s %6.3f ms\n", "Space, after its tail died away",
                     juce::Time::getMillisecondCounterHiRes() - start);
    }
    std::printf ("  (one 256-sample chunk at 48 kHz lasts 5.33 ms)\n");
    SUCCEED();
}
