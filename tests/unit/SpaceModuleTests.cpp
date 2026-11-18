// The Cloud reverb: mix, decay, pre-delay, damping, reset (PRD FR-SPACE-001…003).

#include "support/TestSignals.h"

#include "core/dsp/space/SpaceModule.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using dsp::SpaceModule;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
constexpr double kSampleRate = 48000.0;

std::size_t at (double seconds)
{
    return static_cast<std::size_t> (seconds * kSampleRate);
}

ParamSnapshot space (float mix, float decay = 50.0f, float preDelay = 0.0f, float damping = 50.0f)
{
    auto s = ParamSnapshot::defaults();
    s.set (ParamId::spaceMix, mix);
    s.set (ParamId::spaceDecay, decay);
    s.set (ParamId::spacePreDelay, preDelay);
    s.set (ParamId::spaceDamping, damping);
    return s;
}

// The reverb's response to an impulse, fully wet.
std::vector<float> impulseResponse (const ParamSnapshot& snapshot, double seconds = 4.0)
{
    SpaceModule reverb;
    reverb.prepare ({kSampleRate, 256, 2});
    return runModule (reverb, impulse (at (seconds), 0), snapshot);
}

// Energy of the first difference: a rough measure of high-frequency content.
double brightness (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (auto i = from + 1; i < to; ++i)
    {
        const auto d = static_cast<double> (s[i]) - static_cast<double> (s[i - 1]);
        e += d * d;
    }
    return e / (energy (s, from, to) + 1e-30);
}
} // namespace

TEST_CASE ("The parameter mappings cover the documented ranges", "[unit][space]")
{
    CHECK_THAT (SpaceModule::decaySeconds (0.0f), WithinRel (0.4, 1e-9));
    CHECK_THAT (SpaceModule::decaySeconds (100.0f), WithinRel (20.0, 1e-9));
    CHECK_THAT (SpaceModule::dampingCutoffHz (0.0f), WithinRel (18000.0, 1e-9));
    CHECK_THAT (SpaceModule::dampingCutoffHz (100.0f), WithinRel (1000.0, 1e-9));
}

TEST_CASE ("At 0% mix the signal passes through untouched", "[unit][space]")
{
    SpaceModule reverb;
    reverb.prepare ({kSampleRate, 256, 2});

    const auto input = sine (330.0, kSampleRate, 0.5);
    const auto out = runModule (reverb, input, space (0.0f));
    CHECK (out == input);
}

TEST_CASE ("The reverb tail decays, and longer Decay lasts longer", "[unit][space]")
{
    const auto shortTail = impulseResponse (space (100.0f, 20.0f));
    const auto longTail = impulseResponse (space (100.0f, 80.0f));

    CHECK (allFinite (shortTail));
    CHECK (allFinite (longTail));

    // Each tail dies away...
    CHECK (energy (longTail, at (2.5), at (3.0)) < energy (longTail, at (0.1), at (0.6)));
    // ...and the longer Decay keeps far more energy late on.
    CHECK (energy (longTail, at (2.0), at (3.0)) > 10.0 * energy (shortTail, at (2.0), at (3.0)));
}

TEST_CASE ("Pre-delay holds the reverb back", "[unit][space]")
{
    const auto immediate = impulseResponse (space (100.0f, 50.0f, 0.0f), 1.0);
    const auto delayed = impulseResponse (space (100.0f, 50.0f, 200.0f), 1.0);

    const auto total = energy (delayed, 0, delayed.size());
    CHECK (energy (delayed, 0, at (0.19)) < total * 1.0e-6); // nothing in the first 190 ms
    CHECK (energy (immediate, 0, at (0.19)) > total * 0.01);
}

TEST_CASE ("Damping darkens the tail", "[unit][space]")
{
    const auto bright = impulseResponse (space (100.0f, 60.0f, 0.0f, 0.0f), 2.0);
    const auto dark = impulseResponse (space (100.0f, 60.0f, 0.0f, 100.0f), 2.0);

    CHECK (brightness (dark, at (0.5), at (1.5)) < 0.5 * brightness (bright, at (0.5), at (1.5)));
}

TEST_CASE ("Reset silences the tail", "[unit][space]")
{
    SpaceModule reverb;
    reverb.prepare ({kSampleRate, 256, 2});
    runModule (reverb, sine (330.0, kSampleRate, 0.5), space (100.0f, 90.0f));

    reverb.reset();
    const auto out = runModule (reverb, std::vector<float> (at (0.5), 0.0f), space (100.0f, 90.0f));
    CHECK (energy (out, 0, out.size()) < 1.0e-12);
}

TEST_CASE ("The reverb is stereo", "[unit][space]")
{
    SpaceModule reverb;
    reverb.prepare ({kSampleRate, 256, 2});

    std::vector<float> right;
    const auto left = runModule (reverb, impulse (at (1.0), 0), space (100.0f), 256, &right);

    // Same input on both sides, different tails: the two channels decorrelate.
    double diff = 0.0;
    for (std::size_t i = 0; i < left.size(); ++i)
        diff += std::abs (static_cast<double> (left[i]) - static_cast<double> (right[i]));
    CHECK (diff > 1.0e-3);
}
