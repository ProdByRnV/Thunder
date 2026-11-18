// Thunder's own audio path must be safe on the audio thread: no allocation, no
// locks, no blocking calls (PRD NFR-PERF-001). Setup happens outside the
// real-time sections, as it does in prepareToPlay().

#include "support/Realtime.h"

#include "core/engine/ThunderEngine.h"
#include "core/util/Crossfade.h"
#include "core/util/SpscFifo.h"
#include "plugin/ThunderProcessor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
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
        // Parameter changes between rounds, as automation would make them. The
        // engine smooths towards them inside the real-time section.
        const auto v = static_cast<float> (round % 2);
        for (const auto* id :
             {"inputGain", "pitchSemitones", "pitchFine", "spaceMix", "spaceDecay", "spacePreDelay", "spaceDamping"})
            apvts.getParameter (id)->setValueNotifyingHost (v);

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

TEST_CASE ("Turning the pitch and reverb controls does not allocate or block", "[realtime][pitch][space]")
{
    // Changes made inside the real-time section, every chunk: in particular a
    // reverb Decay change, which needs Thunder's CloudSeed patch to be safe.
    dsp::PitchModule pitch;
    dsp::SpaceModule space;
    const PrepareSpec spec{48000.0, 256, 2};
    pitch.prepare (spec);
    space.prepare (spec);

    std::vector<float> left (256, 0.1f), right (256, 0.1f);
    float* channels[] = {left.data(), right.data()};
    juce::dsp::AudioBlock<float> block (channels, 2, 256);

    auto snapshot = params::ParamSnapshot::defaults();
    const TransportState transport;

    const auto report = test::runInRealtimeSection (
        [&]
        {
            for (int i = 0; i < 200; ++i)
            {
                const auto t = static_cast<float> (i % 20) / 19.0f;
                snapshot.set (ParamId::pitchSemitones, std::round (t * 24.0f - 12.0f));
                snapshot.set (ParamId::spaceDecay, t * 100.0f);
                snapshot.set (ParamId::spacePreDelay, t * 250.0f);
                snapshot.set (ParamId::spaceDamping, 100.0f - t * 100.0f);
                snapshot.set (ParamId::spaceMix, t * 100.0f);

                const ProcessContext context{transport, snapshot};
                pitch.process (block, context);
                space.process (block, context);
            }
            pitch.reset();
            space.reset();
        });

    CHECK (report.allocations == 0);
    CHECK (report.deallocations == 0);
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
