// Thunder's own audio path must be safe on the audio thread: no allocation, no
// locks, no blocking calls (PRD NFR-PERF-001). Setup happens outside the
// real-time sections, as it does in prepareToPlay().

#include "support/Realtime.h"

#include "core/engine/ThunderEngine.h"
#include "core/util/Crossfade.h"
#include "core/util/SpscFifo.h"
#include "plugin/ThunderProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace thunder;
using params::ParamId;

namespace
{
void accumulate (test::RealtimeReport& total, const test::RealtimeReport& report)
{
    total.allocations += report.allocations;
    total.deallocations += report.deallocations;
}
} // namespace

TEST_CASE ("The processor's processBlock does not allocate or block", "[realtime][plugin]")
{
    ThunderProcessor processor;
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> small (2, 64), normal (2, 512), large (2, 4096);
    for (auto* b : {&small, &normal, &large})
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b->getNumSamples(); ++i)
                b->setSample (ch, i, 0.1f);

    juce::MidiBuffer midi;
    auto& apvts = processor.getValueTreeState();
    test::RealtimeReport total;

    for (int round = 0; round < 10; ++round)
    {
        // Parameter changes between rounds, as automation would make them.
        auto* gain = apvts.getParameter ("inputGain");
        gain->setValueNotifyingHost (static_cast<float> (round % 2));

        accumulate (total, test::runInRealtimeSection (
                               [&]
                               {
                                   for (int b = 0; b < 10; ++b)
                                   {
                                       processor.processBlock (normal, midi);
                                       processor.processBlock (small, midi);
                                   }
                                   processor.processBlock (large, midi); // larger than announced
                                   processor.reset();
                               }));
    }

    CHECK (total.allocations == 0);
    CHECK (total.deallocations == 0);
}

TEST_CASE ("The utilities do not allocate or block", "[realtime][util]")
{
    util::SpscFifo<float> fifo (1024);
    util::Crossfade fade;
    fade.prepare (48000.0, util::kBypassFadeSeconds);

    std::vector<float> a (256, 0.5f), b (256, -0.5f), out (256);
    float* aPtr[] = {a.data()};
    const float* bPtr[] = {b.data()};

    TransportState transport;
    transport.isPlaying = true;

    const auto report = test::runInRealtimeSection (
        [&]
        {
            for (int i = 0; i < 100; ++i)
            {
                fifo.push (a.data(), 256);
                fifo.pop (out.data(), 256);

                fade.setTarget (i % 2 == 0);
                fade.process (aPtr, bPtr, 1, 256);

                transport = transport.advancedBy (256, 48000.0);
                transport = TransportState::fromPlayHead (nullptr);
            }
        });

    CHECK (report.allocations == 0);
    CHECK (report.deallocations == 0);
}
