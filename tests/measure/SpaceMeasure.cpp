// Measurements for Space's modes (hidden; run with `ThunderTests "[spacemeasure]"`):
// each mode's wet level against Cloud's on the same noise, its measured decay
// time against the setting, and its CPU.

#include "support/TestSignals.h"

#include "core/dsp/space/SpaceModule.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstdio>

using namespace thunder;
using params::ParamId;

namespace
{
constexpr double kRate = 48000.0;

params::ParamSnapshot wetOnly (params::SpaceMode mode, float decay)
{
    auto p = params::ParamSnapshot::defaults();
    p.set (ParamId::spaceMix, 100.0f);
    p.set (ParamId::spaceDecay, decay);
    test::setChoice (p, ParamId::spaceMode, mode);
    return p;
}

double rmsDb (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    return 10.0 * std::log10 (test::energy (s, from, to) / static_cast<double> (to - from) + 1.0e-30);
}
} // namespace

TEST_CASE ("Space modes: level, decay and CPU", "[.][measure][spacemeasure]")
{
    const char* names[] = {"Cloud", "Plate", "Room", "Shimmer"};
    for (int m = 0; m < 4; ++m)
    {
        const auto mode = static_cast<params::SpaceMode> (m);
        dsp::SpaceModule space;
        space.prepare ({kRate, 256, 2});

        // 2 s of noise, then 10 s of silence.
        auto input = test::whiteNoise (kRate, 2.0);
        for (auto& x : input)
            x *= 0.25f;
        input.resize (static_cast<std::size_t> (12.0 * kRate), 0.0f);

        const auto start = juce::Time::getMillisecondCounterHiRes();
        const auto out = test::runModule (space, input, wetOnly (mode, 50.0f));
        const auto ms = juce::Time::getMillisecondCounterHiRes() - start;

        const auto sustained =
            rmsDb (out, static_cast<std::size_t> (1.0 * kRate), static_cast<std::size_t> (2.0 * kRate));
        const auto inputDb = rmsDb (input, 0, static_cast<std::size_t> (2.0 * kRate));

        // Decay: the time from the end of the noise to 30 dB down, doubled (T30 → T60).
        const auto window = static_cast<std::size_t> (0.05 * kRate);
        double t30 = -1.0;
        for (auto i = static_cast<std::size_t> (2.0 * kRate); i + window < out.size(); i += window)
            if (rmsDb (out, i, i + window) < sustained - 30.0)
            {
                t30 = static_cast<double> (i) / kRate - 2.0;
                break;
            }

        std::printf ("  %-8s wet %+6.1f dB vs input, T60 ~%5.2f s (set %5.2f s), CPU %5.2f%%\n", names[m],
                     sustained - inputDb, 2.0 * t30, dsp::SpaceModule::decaySeconds (mode, 50.0f),
                     ms / 12000.0 * 100.0);
    }
    SUCCEED();
}
