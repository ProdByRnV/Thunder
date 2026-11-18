// The third-party DSP's per-block processing must be safe to call on the audio
// thread: no allocation, no locks, no blocking calls. Setup (construction,
// configuration) happens outside the real-time section, as it will in Thunder's
// prepare().

#include "support/Realtime.h"

#include <catch2/catch_test_macros.hpp>

#include <DSP/ReverbController.h>
#include <signalsmith-stretch/signalsmith-stretch.h>

#include <memory>
#include <vector>

namespace
{
constexpr int kSampleRate = 48000;
constexpr int kBlockSize = 256;
constexpr int kBlocks = 200; // ~1 s of audio
} // namespace

TEST_CASE ("CloudSeed processing does not allocate or block", "[realtime][cloudseed]")
{
    auto reverb = std::make_unique<Cloudseed::ReverbController> (kSampleRate);
    reverb->SetSamplerate (kSampleRate);
    reverb->ClearBuffers();

    std::vector<float> inL (kBlockSize, 0.0f), inR (kBlockSize, 0.0f), outL (kBlockSize), outR (kBlockSize);
    inL[0] = inR[0] = 1.0f;

    const auto report = thunder::test::runInRealtimeSection (
        [&]
        {
            for (int b = 0; b < kBlocks; ++b)
                reverb->Process (inL.data(), inR.data(), outL.data(), outR.data(), kBlockSize);
        });

    CHECK (report.allocations == 0);
    CHECK (report.deallocations == 0);
}

TEST_CASE ("Signalsmith Stretch processing does not allocate or block", "[realtime][signalsmith]")
{
    signalsmith::stretch::SignalsmithStretch<float> stretch;
    stretch.presetDefault (2, static_cast<float> (kSampleRate));
    stretch.setTransposeSemitones (7.0f);

    std::vector<float> inL (kBlockSize, 0.25f), inR (kBlockSize, 0.25f), outL (kBlockSize), outR (kBlockSize);
    float* in[] = {inL.data(), inR.data()};
    float* out[] = {outL.data(), outR.data()};

    // One block outside the section, the way the engine is primed in prepare().
    stretch.process (in, kBlockSize, out, kBlockSize);

    const auto report = thunder::test::runInRealtimeSection (
        [&]
        {
            for (int b = 0; b < kBlocks; ++b)
                stretch.process (in, kBlockSize, out, kBlockSize);
        });

    CHECK (report.allocations == 0);
    CHECK (report.deallocations == 0);
}
