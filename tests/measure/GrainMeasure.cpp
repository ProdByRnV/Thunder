// Measurements behind the grain engine's level scaling and its CPU cost
// (hidden; run with `ThunderTests "[grainmeasure]"`; ROADMAP T-0.3-07,
// PRD QA-PERF-001).

#include "support/TestSignals.h"

#include "core/dsp/grains/GrainModule.h"
#include "core/engine/ModuleSlot.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace thunder;
using namespace thunder::test;
using dsp::GrainModule;

namespace
{
constexpr double kRate = 48000.0;

std::vector<float> runGrains (GrainModule& grains, const std::vector<float>& input,
                              const GrainModule::Settings& settings, std::vector<float>* right = nullptr)
{
    std::vector<float> left = input, rightChannel = input;
    for (std::size_t start = 0; start < input.size(); start += 256)
    {
        const auto n = std::min<std::size_t> (256, input.size() - start);
        float* channels[] = {left.data() + start, rightChannel.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        grains.process (block, settings);
    }
    if (right != nullptr)
        *right = std::move (rightChannel);
    return left;
}

double rmsDb (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    return 10.0 * std::log10 (energy (s, from, to) / static_cast<double> (to - from) + 1e-30);
}
} // namespace

TEST_CASE ("Grain cloud level by settings", "[.][measure][grainmeasure]")
{
    const auto noise = whiteNoise (kRate, 8.0);
    const auto melody =
        tone ([] (double t) { return 57.0 + 2.0 * std::floor (std::fmod (t * 4.0, 6.0)); }, kRate, 8.0, 8);

    struct Case
    {
        const char* name;
        GrainModule::Settings settings;
    };

    const Case cases[] = {
        {"default (80 ms, 20/s, pos 25%, pan 40%)", {0.08, 20.0, 1.0f, 0.25f, 0.0f, 0.4f, 0.0f}},
        {"80 ms, 25/s, no spread", {0.08, 25.0}},
        {"500 ms, 100/s, no spread", {0.5, 100.0}},
        {"500 ms, 100/s, pos 100%", {0.5, 100.0, 1.0f, 1.0f}},
        {"500 ms, 100/s, pos 10%", {0.5, 100.0, 1.0f, 0.1f}},
        {"500 ms, 100/s, pos 5%", {0.5, 100.0, 1.0f, 0.05f}},
        {"500 ms, 100/s, pos 2%", {0.5, 100.0, 1.0f, 0.02f}},
        {"500 ms, 100/s, pitch 12", {0.5, 100.0, 1.0f, 0.0f, 12.0f}},
        {"500 ms, 100/s, pitch 0.5", {0.5, 100.0, 1.0f, 0.0f, 0.5f}},
        {"500 ms, 100/s, pitch 0.1", {0.5, 100.0, 1.0f, 0.0f, 0.1f}},
        {"500 ms, 100/s, reverse 100%", {0.5, 100.0, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f}},
        {"500 ms, 100/s, reverse 50%", {0.5, 100.0, 1.0f, 0.0f, 0.0f, 0.0f, 0.5f}},
        {"500 ms, 100/s, reverse 10%", {0.5, 100.0, 1.0f, 0.0f, 0.0f, 0.0f, 0.1f}},
        {"500 ms, 100/s, pan 100%", {0.5, 100.0, 1.0f, 0.0f, 0.0f, 1.0f}},
        {"100 ms, 50/s, all spreads max", {0.1, 50.0, 1.0f, 1.0f, 12.0f, 1.0f, 0.5f}},
        {"10 ms, 100/s, pos 25%", {0.01, 100.0, 1.0f, 0.25f}},
        {"200 ms, 10/s, pos 25%", {0.2, 10.0, 1.0f, 0.25f}},
    };

    const auto from = static_cast<std::size_t> (5.0 * kRate);
    const auto to = noise.size();
    std::printf ("\nGrain cloud level relative to the input (dB, left channel, seconds 5-8)\n");
    std::printf ("  %-42s %8s %8s\n", "settings", "noise", "melody");
    for (const auto& c : cases)
    {
        GrainModule a, b;
        a.prepare ({kRate, 256, 2});
        b.prepare ({kRate, 256, 2});
        const auto outNoise = runGrains (a, noise, c.settings);
        const auto outMelody = runGrains (b, melody, c.settings);
        std::printf ("  %-42s %+8.2f %+8.2f\n", c.name, rmsDb (outNoise, from, to) - rmsDb (noise, from, to),
                     rmsDb (outMelody, from, to) - rmsDb (melody, from, to));
    }
    SUCCEED();
}

TEST_CASE ("Grain engine CPU", "[.][measure][grainmeasure]")
{
    // 10 s of a harmonic melody, stereo, 48 kHz, in 256-sample chunks.
    const auto input =
        tone ([] (double t) { return 57.0 + 2.0 * std::floor (std::fmod (t * 4.0, 6.0)); }, kRate, 10.0, 8);

    const auto run = [&] (const char* name, const GrainModule::Settings& settings)
    {
        GrainModule grains;
        grains.prepare ({kRate, 256, 2});
        runGrains (grains, std::vector<float> (input.begin(), input.begin() + 96000), settings); // warm up

        const auto start = juce::Time::getMillisecondCounterHiRes();
        runGrains (grains, input, settings);
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;
        std::printf ("  %-52s %5.2f%% of one core\n", name, elapsedMs / 10000.0 * 100.0);
    };

    std::printf ("\nGrain module CPU (stereo, 48 kHz):\n");
    run ("default (80 ms, 20/s, pos 25%, pan 40%, amount 50%)", {0.08, 20.0, 0.5f, 0.25f, 0.0f, 0.4f, 0.0f});
    run ("max density, min size (10 ms, 100/s), no pitch", {0.01, 100.0, 0.5f, 0.25f, 0.0f, 0.4f, 0.0f});
    run ("max density, min size (10 ms, 100/s), pitch 12", {0.01, 100.0, 0.5f, 0.25f, 12.0f, 0.4f, 0.0f});
    run ("max overlap (500 ms, 100/s), no pitch", {0.5, 100.0, 0.5f, 0.25f, 0.0f, 0.4f, 0.0f});
    run ("max overlap (500 ms, 100/s), pitch 12, reverse 50%", {0.5, 100.0, 0.5f, 1.0f, 12.0f, 1.0f, 0.5f});

    // The slot when Grains is switched off.
    {
        ModuleSlot<GrainModule> slot{params::ParamId::grainOn};
        slot.prepare ({kRate, 256, 2});
        auto snapshot = params::ParamSnapshot::defaults();
        snapshot.set (params::ParamId::grainOn, 0.0f);
        const auto start = juce::Time::getMillisecondCounterHiRes();
        runModule (slot, input, snapshot);
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;
        std::printf ("  %-52s %5.2f%% of one core\n", "Grains off", elapsedMs / 10000.0 * 100.0);
    }
    SUCCEED();
}
