// Smoke tests for the third-party DSP Thunder builds on: they compile on every
// platform, produce finite audio, and behave the way Thunder's design assumes.

#include <catch2/catch_test_macros.hpp>

#include <DSP/ReverbController.h>
#include <Programs.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>
#include <vector>

namespace
{
constexpr int kSampleRate = 48000;
constexpr int kBlockSize = 256; // Thunder's internal maximum chunk size
constexpr double kPi = 3.14159265358979323846;

bool allFinite (const std::vector<float>& samples)
{
    for (auto s : samples)
        if (!std::isfinite (s))
            return false;
    return true;
}

double energy (const std::vector<float>& samples, std::size_t begin, std::size_t end)
{
    double sum = 0.0;
    for (auto i = begin; i < end && i < samples.size(); ++i)
        sum += static_cast<double> (samples[i]) * static_cast<double> (samples[i]);
    return sum;
}
} // namespace

TEST_CASE ("CloudSeed reverb turns an impulse into a finite, decaying tail", "[unit][cloudseed]")
{
    Cloudseed::initPrograms();

    auto reverb = std::make_unique<Cloudseed::ReverbController> (kSampleRate);
    for (int i = 0; i < Cloudseed::Parameter::COUNT; ++i)
        reverb->SetParameter (i, static_cast<double> (Cloudseed::ProgramDarkPlate[i]));
    reverb->SetSamplerate (kSampleRate);
    reverb->ClearBuffers();

    const auto length = static_cast<std::size_t> (kSampleRate * 4);
    std::vector<float> inL (length, 0.0f), inR (length, 0.0f), outL (length, 0.0f), outR (length, 0.0f);
    inL[0] = inR[0] = 1.0f;

    for (std::size_t pos = 0; pos < length; pos += kBlockSize)
    {
        const auto n = static_cast<int> (std::min<std::size_t> (kBlockSize, length - pos));
        reverb->Process (inL.data() + pos, inR.data() + pos, outL.data() + pos, outR.data() + pos, n);
    }

    REQUIRE (allFinite (outL));
    REQUIRE (allFinite (outR));

    const auto second = static_cast<std::size_t> (kSampleRate);
    const auto early = energy (outL, 0, second) + energy (outR, 0, second);
    const auto late = energy (outL, 3 * second, 4 * second) + energy (outR, 3 * second, 4 * second);

    CHECK (early > 1.0e-6); // the reverb produced something
    CHECK (late < early);   // and it decays
}

TEST_CASE ("Signalsmith Stretch reports latency and passes a sine through at unison", "[unit][signalsmith]")
{
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault (2, static_cast<float> (kSampleRate));
    stretch.setTransposeSemitones (0.0f);

    const auto latency = stretch.inputLatency() + stretch.outputLatency();
    REQUIRE (latency > 0);

    // Recorded in the test log: this number drives the pitch latency decision.
    std::cout << "Signalsmith presetDefault latency at " << kSampleRate << " Hz: " << latency << " samples ("
              << 1000.0 * latency / kSampleRate << " ms)\n";

    const auto length = static_cast<std::size_t> (kSampleRate * 2);
    std::vector<float> inL (length), inR (length), outL (length, 0.0f), outR (length, 0.0f);
    for (std::size_t i = 0; i < length; ++i)
        inL[i] = inR[i] =
            0.5f * static_cast<float> (std::sin (2.0 * kPi * 440.0 * static_cast<double> (i) / kSampleRate));

    for (std::size_t pos = 0; pos < length; pos += kBlockSize)
    {
        const auto n = static_cast<int> (std::min<std::size_t> (kBlockSize, length - pos));
        float* in[] = {inL.data() + pos, inR.data() + pos};
        float* out[] = {outL.data() + pos, outR.data() + pos};
        stretch.process (in, n, out, n);
    }

    REQUIRE (allFinite (outL));
    REQUIRE (allFinite (outR));

    // After the latency has passed, the level should match the input closely.
    const auto start = static_cast<std::size_t> (latency) + static_cast<std::size_t> (kSampleRate / 10);
    const auto inRms = std::sqrt (energy (inL, start, length) / static_cast<double> (length - start));
    const auto outRms = std::sqrt (energy (outL, start, length) / static_cast<double> (length - start));
    const auto differenceDb = 20.0 * std::log10 (outRms / inRms);

    CHECK (std::abs (differenceDb) < 1.0);
}
