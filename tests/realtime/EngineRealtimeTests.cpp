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
#include <memory>
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

TEST_CASE ("Switching every module, Output Mode, Mix and the gains, and a reset, do not allocate or block",
           "[realtime][engine]")
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
                snapshot.set (ParamId::autoGain, (chunk / 41) % 2 == 0 ? 1.0f : 0.0f);
                snapshot.set (ParamId::safetyLimiter, (chunk / 53) % 2 == 0 ? 1.0f : 0.0f);
                snapshot.set (ParamId::layerLevel, -36.0f + static_cast<float> (chunk % 49));
                snapshot.set (ParamId::inputGain, (chunk / 61) % 2 == 0 ? 0.0f : 18.0f); // drives the limiter
                if (chunk == 300)
                    engine.reset(); // a host reset with the reverb ringing

                engine.process (channels, 256, {}, snapshot);
            }
        });

    CHECK (report.allocations == 0);
    CHECK (report.deallocations == 0);
}

TEST_CASE ("Time, Static and Shape do not allocate or block, playing, looping and stopped", "[realtime][rhythm]")
{
    ThunderEngine engine;
    engine.prepare (48000.0);

    const auto input = test::sine (220.0, 48000.0, 1.0);
    std::vector<float> left (512), right (512);
    float* channels[] = {left.data(), right.data()};

    auto snapshot = params::ParamSnapshot::defaults();
    snapshot.set (ParamId::timeOn, 1.0f);
    snapshot.set (ParamId::staticOn, 1.0f);
    snapshot.set (ParamId::staticAmount, 100.0f);
    snapshot.set (ParamId::staticTgtPitch, 1.0f);
    snapshot.set (ParamId::staticTgtLevel, 1.0f);
    snapshot.set (ParamId::shapeOn, 1.0f);

    TransportState transport;
    transport.isPlaying = true;
    transport.hasHostInfo = true;

    const auto report = test::runInRealtimeSection (
        [&]
        {
            for (std::size_t block = 0; block < 600; ++block)
            {
                const auto n = 64 + static_cast<int> ((block * 97) % 449); // uneven blocks, split on Static's grid
                const auto start = (block * 512) % (input.size() - 512);
                std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), n, left.begin());
                std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), n, right.begin());

                // Every setting of each, switching on and off, a loop and a stop.
                snapshot.set (ParamId::halftimeSpeed, static_cast<float> ((block / 37) % 2));
                snapshot.set (ParamId::halftimeSmooth, static_cast<float> (block % 101));
                snapshot.set (ParamId::staticCharacter, static_cast<float> ((block / 23) % 3));
                snapshot.set (ParamId::staticSync, (block / 51) % 2 == 0 ? 1.0f : 0.0f);
                snapshot.set (ParamId::staticRateSynced, static_cast<float> (block % 15));
                snapshot.set (ParamId::staticRate, 0.05f + static_cast<float> (block % 20));
                snapshot.set (ParamId::shapeType, static_cast<float> ((block / 19) % 6));
                snapshot.set (ParamId::shapeRate, static_cast<float> (block % 14));
                snapshot.set (ParamId::shapeSmooth, static_cast<float> (block % 101));
                for (const auto id : {ParamId::timeOn, ParamId::staticOn, ParamId::shapeOn})
                    snapshot.set (id, (block / (60 + 13 * static_cast<std::size_t> (id))) % 2 == 0 ? 1.0f : 0.0f);

                transport.isPlaying = (block / 150) % 2 == 0;
                if (block == 200)
                    transport.ppqPosition = 0.0; // the loop jumps back
                transport.ppqLastBarStart = std::floor (transport.ppqPosition / 4.0) * 4.0;

                engine.process (channels, n, transport, snapshot);
                transport = transport.advancedBy (n, 48000.0);
            }
        });

    CHECK (report.allocations == 0);
    CHECK (report.deallocations == 0);
}

TEST_CASE ("Duck, Autopan, the grain extras and every reverb mode do not allocate or block", "[realtime][engine]")
{
    for (const auto quality : {params::Quality::Eco, params::Quality::High})
    {
        ThunderEngine engine;
        engine.prepare (48000.0, quality);

        const auto input = test::sine (220.0, 48000.0, 1.0);
        std::vector<float> left (256), right (256);
        float* channels[] = {left.data(), right.data()};
        auto snapshot = params::ParamSnapshot::defaults();
        snapshot.set (ParamId::grainPitchSpread, 12.0f);
        TransportState transport;
        transport.isPlaying = true;
        transport.hasHostInfo = true;

        const auto report = test::runInRealtimeSection (
            [&]
            {
                for (std::size_t chunk = 0; chunk < 800; ++chunk)
                {
                    const auto start = (chunk * 256) % (input.size() - 256);
                    std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), 256, left.begin());
                    std::copy_n (input.begin() + static_cast<std::ptrdiff_t> (start), 256, right.begin());

                    snapshot.set (ParamId::spaceMode, static_cast<float> ((chunk / 50) % 4));
                    snapshot.set (ParamId::spaceDecay, static_cast<float> (chunk % 101));
                    for (const auto id : {ParamId::duckOn, ParamId::autopanOn, ParamId::grainScaleLock,
                                          ParamId::grainSync, ParamId::grainFreeze})
                        snapshot.set (id, (chunk / (30 + 7 * static_cast<std::size_t> (id))) % 2 == 0 ? 1.0f : 0.0f);
                    snapshot.set (ParamId::duckRelease, 10.0f + static_cast<float> (chunk % 900));
                    snapshot.set (ParamId::autopanRate, static_cast<float> (chunk % 15));
                    snapshot.set (ParamId::grainDensity, 1.0f + static_cast<float> (chunk % 100));
                    if (chunk == 400)
                        engine.reset();

                    engine.process (channels, 256, transport, snapshot);
                    transport = transport.advancedBy (256, 48000.0);
                }
            });

        CHECK (report.allocations == 0);
        CHECK (report.deallocations == 0);
    }
}

TEST_CASE ("Capturing does not allocate or block", "[realtime][capture]")
{
    // Arming, recording a Layer and a Full Mix capture, finishing, failing and
    // cancelling all happen on the audio thread. The queue is allocated
    // beforehand on the message thread, as the processor does when arming.
    ThunderEngine engine;
    engine.prepare (48000.0);
    auto& capture = engine.getCapture();
    capture.allocateQueue();

    const auto snapshot = params::ParamSnapshot::defaults();
    std::vector<float> left (512, 0.1f), right (512, 0.1f);
    float* channels[] = {left.data(), right.data()};

    TransportState transport;
    transport.isPlaying = true;
    transport.hasHostInfo = true;

    auto playSeconds = [&] (double seconds)
    {
        for (int done = 0; done < static_cast<int> (seconds * 48000.0); done += 512)
        {
            engine.process (channels, 512, transport, snapshot);
            transport = transport.advancedBy (512, 48000.0);
        }
    };

    // The queue is drained between sections, as the writer thread would.
    auto drain = [&capture]
    {
        auto block = std::make_unique<capture::CaptureBlock>();
        while (capture.getQueue().pop (block.get(), 1) == 1)
        {
        }
    };

    test::RealtimeReport total;
    for (const auto source : {capture::CaptureSource::Layer, capture::CaptureSource::FullMix})
    {
        capture.arm (1, source);
        accumulate (total, test::runInRealtimeSection ([&] { playSeconds (1.0); }));
        drain();
        accumulate (total, test::runInRealtimeSection ([&] { playSeconds (1.5); })); // finishes
        drain();
    }

    capture.arm (2, capture::CaptureSource::Layer);
    accumulate (total, test::runInRealtimeSection ([&] { playSeconds (2.5); }));
    capture.cancel();
    accumulate (total, test::runInRealtimeSection ([&] { playSeconds (0.1); }));
    drain();

    capture.arm (2, capture::CaptureSource::FullMix);
    accumulate (total, test::runInRealtimeSection (
                           [&]
                           {
                               playSeconds (2.5);
                               engine.reset(); // fails the recording
                           }));
    drain();

    CHECK (total.allocations == 0);
    CHECK (total.deallocations == 0);
    CHECK (capture.getPhase() == capture::CaptureController::Phase::Idle);
}

TEST_CASE ("Switching presets while playing does not allocate or block on the audio thread", "[realtime][presets]")
{
    // Presets load on the message thread; the audio thread then only glides to
    // the new values and crossfades modes, which must stay allocation-free.
    ThunderProcessor processor;
    processor.getPresetManager().setUserFolder (
        juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("ThunderNoUserPresets"));
    processor.prepareToPlay (48000.0, 512);

    juce::AudioBuffer<float> buffer (2, 512);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 512; ++i)
            buffer.setSample (ch, i, 0.2f * std::sin (0.05f * static_cast<float> (i)));

    juce::MidiBuffer midi;
    test::RealtimeReport total;
    const auto entries = processor.getPresetManager().getListing().entries;

    for (int round = 0; round < 2; ++round)
        for (const auto& entry : entries)
        {
            REQUIRE (processor.getPresetManager().load (entry).isEmpty());
            accumulate (total, test::runInRealtimeSection (
                                   [&]
                                   {
                                       for (int b = 0; b < 4; ++b)
                                           processor.processBlock (buffer, midi);
                                   }));
        }

    CHECK (total.allocations == 0);
    CHECK (total.deallocations == 0);
}

TEST_CASE ("Feeding the visualiser does not allocate or block", "[realtime][visual]")
{
    // An editor showing everything: frames, spectrum samples and grain
    // snapshots, read on this thread between blocks as the editor's timer would.
    ThunderProcessor processor;
    processor.prepareToPlay (48000.0, 512);
    auto& bridge = processor.getUiBridge();
    bridge.setWanted (UiBridge::kFrames | UiBridge::kSpectrum | UiBridge::kGrains);

    juce::AudioBuffer<float> normal (2, 512), large (2, 4096);
    for (auto* b : {&normal, &large})
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < b->getNumSamples(); ++i)
                b->setSample (ch, i, 0.2f * std::sin (0.05f * static_cast<float> (i)));

    juce::MidiBuffer midi;
    test::RealtimeReport total;
    std::vector<visual::WaveFrame> frames (2048);
    std::vector<visual::SpectrumSample> samples (8192);
    visual::GrainSnapshot grains;

    for (int round = 0; round < 20; ++round)
    {
        accumulate (total, test::runInRealtimeSection (
                               [&]
                               {
                                   for (int b = 0; b < 8; ++b)
                                       processor.processBlock (normal, midi);
                                   processor.processBlock (large, midi);
                               }));

        while (bridge.readFrames (frames.data(), 2048) > 0)
        {
        }
        while (bridge.readSpectrum (samples.data(), 8192) > 0)
        {
        }
        while (bridge.readGrains (grains))
        {
        }
    }

    CHECK (total.allocations == 0);
    CHECK (total.deallocations == 0);
    bridge.setWanted (0);
}
