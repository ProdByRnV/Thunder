// Thunder's own audio path must be safe on the audio thread: no allocation, no
// locks, no blocking calls (PRD NFR-PERF-001). Setup happens outside the
// real-time sections, as it does in prepareToPlay().

#include "support/Realtime.h"
#include "support/TestSignals.h"

#include "core/dsp/grains/GrainModule.h"
#include "core/engine/ModuleSlot.h"
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
                test::setSemitoneShift (snapshot, std::round (t * 24.0f - 12.0f));
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

TEST_CASE ("Switching pitch modes, formants and Pitch Mix does not allocate or block", "[realtime][pitch]")
{
    // Every mode, including Interval mode's tracker starting and stopping, with
    // the key, interval, formants and Pitch Mix changing as it goes.
    dsp::PitchModule pitch;
    pitch.prepare ({48000.0, 256, 2});

    const auto melody = test::tone ([] (double t) { return 57.0 + std::floor (t * 8.0); }, 48000.0, 1.0);
    std::vector<float> left (256), right (256);
    float* channels[] = {left.data(), right.data()};
    juce::dsp::AudioBlock<float> block (channels, 2, 256);

    auto snapshot = params::ParamSnapshot::defaults();
    const TransportState transport;

    const auto report = test::runInRealtimeSection (
        [&]
        {
            for (std::size_t chunk = 0; chunk < 400; ++chunk)
            {
                const auto start = (chunk * 256) % (melody.size() - 256);
                std::copy_n (melody.begin() + static_cast<std::ptrdiff_t> (start), 256, left.begin());
                std::copy_n (melody.begin() + static_cast<std::ptrdiff_t> (start), 256, right.begin());

                const auto phase = static_cast<int> (chunk / 20);
                test::setChoice (snapshot, ParamId::pitchMode, static_cast<params::PitchMode> (phase % 3));
                test::setChoice (snapshot, ParamId::pitchInterval, static_cast<params::PitchInterval> (phase % 8));
                test::setChoice (snapshot, ParamId::pitchDirection, static_cast<params::PitchDirection> (phase % 2));
                snapshot.set (ParamId::keyRoot, static_cast<float> (phase % 12));
                test::setChoice (snapshot, ParamId::keyScale, static_cast<music::Scale> (phase % 9));
                snapshot.set (ParamId::pitchFormant, (phase % 2) == 0 ? 1.0f : 0.0f);
                snapshot.set (ParamId::pitchMix, static_cast<float> ((phase * 37) % 101));

                pitch.process (block, ProcessContext{transport, snapshot});
            }
            pitch.reset();
        });

    CHECK (report.allocations == 0);
    CHECK (report.deallocations == 0);
}

TEST_CASE ("The grain engine does not allocate or block", "[realtime][grains]")
{
    // Including the pool running full (grains skipped) and every setting,
    // spreads included, changing every chunk.
    dsp::GrainModule grains;
    grains.prepare ({48000.0, 256, 2});

    const auto input = test::sine (220.0, 48000.0, 1.0);
    std::vector<float> left (256), right (256);
    float* channels[] = {left.data(), right.data()};
    juce::dsp::AudioBlock<float> block (channels, 2, 256);

    const auto report = test::runInRealtimeSection (
        [&]
        {
            for (std::size_t chunk = 0; chunk < 400; ++chunk)
            {
                const auto start = (chunk * 256) % (input.size() - 256);
                std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), 256, left.begin());
                std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), 256, right.begin());

                const auto t = static_cast<double> (chunk % 40) / 39.0;
                const auto f = static_cast<float> (t);
                const dsp::GrainModule::Settings settings{
                    0.01 + 0.49 * t, chunk < 200 ? 1.0 + 99.0 * t : 1000.0, f, f, 12.0f * f, f, 0.5f};
                grains.process (block, settings);
            }
            grains.reset();
        });

    CHECK (grains.getGrainsSkipped() > 0);
    CHECK (report.allocations == 0);
    CHECK (report.deallocations == 0);
}

TEST_CASE ("Switching Grains on and off does not allocate or block", "[realtime][grains]")
{
    // On → off → on resets the module inside the audio callback; that must be
    // as safe as processing.
    ModuleSlot<dsp::GrainModule> slot{ParamId::grainOn};
    slot.prepare ({48000.0, 256, 2});

    const auto input = test::sine (220.0, 48000.0, 1.0);
    std::vector<float> left (256), right (256);
    float* channels[] = {left.data(), right.data()};
    juce::dsp::AudioBlock<float> block (channels, 2, 256);
    auto snapshot = params::ParamSnapshot::defaults();
    const TransportState transport;

    const auto report = test::runInRealtimeSection (
        [&]
        {
            for (std::size_t chunk = 0; chunk < 400; ++chunk)
            {
                const auto start = (chunk * 256) % (input.size() - 256);
                std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), 256, left.begin());
                std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), 256, right.begin());

                snapshot.set (ParamId::grainOn, (chunk / 30) % 2 == 0 ? 1.0f : 0.0f);
                slot.process (block, ProcessContext{transport, snapshot});
            }
            slot.reset();
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

TEST_CASE ("Switching every module, Output Mode and Mix does not allocate or block", "[realtime][engine]")
{
    // Including the reverb ringing out on silence after it is switched off.
    ThunderEngine engine;
    engine.prepare (48000.0);

    const auto input = test::sine (220.0, 48000.0, 1.0);
    std::vector<float> left (256), right (256);
    float* channels[] = {left.data(), right.data()};
    auto snapshot = params::ParamSnapshot::defaults();
    const ParamId switches[] = {ParamId::pitchOn, ParamId::grainOn, ParamId::spaceOn, ParamId::toneOn,
                                ParamId::widthOn};

    const auto report = test::runInRealtimeSection (
        [&]
        {
            for (std::size_t chunk = 0; chunk < 600; ++chunk)
            {
                const auto start = (chunk * 256) % (input.size() - 256);
                std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), 256, left.begin());
                std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), 256, right.begin());

                for (std::size_t s = 0; s < std::size (switches); ++s)
                    snapshot.set (switches[s], ((chunk / (20 + 7 * s)) % 2) == 0 ? 1.0f : 0.0f);
                snapshot.set (ParamId::outputMode, (chunk / 33) % 2 == 0 ? 0.0f : 1.0f);
                snapshot.set (ParamId::mix, static_cast<float> (chunk % 101));
                snapshot.set (ParamId::hpFreq, 20.0f + static_cast<float> (chunk % 50) * 39.0f);
                snapshot.set (ParamId::widthAmount, static_cast<float> (chunk % 201));

                engine.process (channels, 256, {}, snapshot);
            }
        });

    CHECK (report.allocations == 0);
    CHECK (report.deallocations == 0);
}
