// The visualiser and the meters (ROADMAP T-0.6-04 … 07, 10; PRD UI-VIS-001…008,
// FR-TONE-004, FR-WIDTH-003; UI.md §8.2, §8.5, §9.9, §9.19, §10.1, §10.3, §11.6):
// the audio thread's frames are exact and only made while an editor shows
// them, never change the sound and never wait; the waveform's bar and beat
// lines, the meters' ballistics, loudness and correlation, the spectrum's
// levels and the filter curve are right against known signals and the real
// Tone filter; the editor shows them, stops asking when hidden, and remembers
// its view and options. Headless: no window is shown.

#include "support/TestHost.h"
#include "support/TestSignals.h"

#include "core/dsp/tone/ToneModule.h"
#include "core/engine/ThunderEngine.h"
#include "core/state/SessionState.h"
#include "core/state/UserSettings.h"
#include "core/visual/VisualTap.h"
#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/visualiser/FilterResponse.h"
#include "plugin/ui/visualiser/MeterModel.h"
#include "plugin/ui/visualiser/SpectrumAnalyser.h"
#include "plugin/ui/visualiser/WaveformHistory.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

using namespace thunder;
using Catch::Matchers::WithinAbs;
using params::ParamId;
using visual::VisualTap;
using visual::WaveFrame;

namespace
{
constexpr double kRate = 48000.0;

TransportState playingAt (double ppq, double bpm = 120.0)
{
    TransportState t;
    t.hasHostInfo = true;
    t.isPlaying = true;
    t.bpm = bpm;
    t.ppqPosition = ppq;
    t.ppqLastBarStart = std::floor (ppq / 4.0) * 4.0;
    return t;
}

// Stereo buffers for one chunk of layer, dry and output.
struct Chunk
{
    explicit Chunk (int n)
        : size (n)
    {
        for (auto* v : {&layerL, &layerR, &dryL, &dryR, &outL, &outR})
            v->assign (static_cast<std::size_t> (n), 0.0f);
    }

    void feed (VisualTap& tap, const TransportState& t, float limiterGainDb = 0.0f)
    {
        const float* layer[] = {layerL.data(), layerR.data()};
        const float* dry[] = {dryL.data(), dryR.data()};
        const float* out[] = {outL.data(), outR.data()};
        tap.tapLayer (layer, dry, size);
        tap.tapOutput (out, size, t, limiterGainDb);
    }

    int size;
    std::vector<float> layerL, layerR, dryL, dryR, outL, outR;
};

std::vector<WaveFrame> readAll (VisualTap& tap)
{
    std::vector<WaveFrame> frames (static_cast<std::size_t> (VisualTap::kFrameCapacity));
    frames.resize (static_cast<std::size_t> (tap.readFrames (frames.data(), VisualTap::kFrameCapacity)));
    return frames;
}

// Runs the engine on `seconds` of the test melody while the transport plays.
void runEngine (ThunderEngine& engine, double seconds, const params::ParamSnapshot& snapshot,
                std::vector<float>* outputLeft = nullptr, const std::function<void()>& everyBlock = {})
{
    test::TestPlayHead head;
    constexpr int block = 512;
    std::vector<float> left (block), right (block);
    for (int done = 0; done < static_cast<int> (seconds * kRate); done += block)
    {
        for (int i = 0; i < block; ++i)
            left[static_cast<std::size_t> (i)] = right[static_cast<std::size_t> (i)] =
                test::melodyAt (head.ppq + i * head.bpm / (60.0 * kRate), head.bpm);
        float* channels[] = {left.data(), right.data()};
        engine.process (channels, block, playingAt (head.ppq), snapshot);
        if (outputLeft != nullptr)
            outputLeft->insert (outputLeft->end(), left.begin(), left.end());
        head.advance (block, kRate);
        if (everyBlock)
            everyBlock();
    }
}

// Frames from a stereo signal through a tap (as output, layer and dry).
std::vector<WaveFrame> framesOf (const std::vector<float>& left, const std::vector<float>& right)
{
    VisualTap tap;
    tap.prepare (kRate, 256, 0);
    tap.setWanted (VisualTap::kFrames);
    tap.beginBlock();
    std::vector<WaveFrame> all;
    for (std::size_t from = 0; from + 256 <= left.size(); from += 256)
    {
        Chunk c (256);
        std::copy_n (left.begin() + static_cast<std::ptrdiff_t> (from), 256, c.outL.begin());
        std::copy_n (right.begin() + static_cast<std::ptrdiff_t> (from), 256, c.outR.begin());
        c.feed (tap, playingAt (0.0));
        const auto some = readAll (tap);
        all.insert (all.end(), some.begin(), some.end());
    }
    return all;
}

// ---- Editor fixture

struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderVisualiserTests-" + juce::Uuid().toString());
    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
};

struct Fixture
{
    TempFolder temp;
    ThunderProcessor processor;
    test::TestPlayHead head;
    std::unique_ptr<juce::AudioProcessorEditor> editorOwner;
    ThunderEditor* editor = nullptr;

    Fixture()
    {
        processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
        processor.getPresetManager().setUserFolder (temp.folder.getChildFile ("Presets"));
        processor.prepareToPlay (kRate, 512);
        processor.setPlayHead (&head);
        open();
    }

    ~Fixture()
    {
        close();
        processor.setPlayHead (nullptr);
    }

    void open()
    {
        editorOwner.reset (processor.createEditorAndMakeActive());
        editor = dynamic_cast<ThunderEditor*> (editorOwner.get());
        REQUIRE (editor != nullptr);
    }

    void close()
    {
        if (editorOwner != nullptr)
            processor.editorBeingDeleted (editorOwner.get());
        editorOwner.reset();
        editor = nullptr;
    }

    // Plays the melody with a visualiser frame after every block.
    void play (double seconds)
    {
        editor->updateVisuals (true, 0.0);
        test::playMelody (processor, head, seconds, kRate, 512,
                          [this] { editor->updateVisuals (true, 512.0 / kRate); });
    }

    juce::RangedAudioParameter& param (ParamId id) { return ui::parameter (processor.getValueTreeState(), id); }
    float value (ParamId id) { return param (id).convertFrom0to1 (param (id).getValue()); }
};

struct GestureCounter final : public juce::AudioProcessorParameter::Listener
{
    explicit GestureCounter (juce::AudioProcessorParameter& p)
        : parameter (p)
    {
        parameter.addListener (this);
    }
    ~GestureCounter() override { parameter.removeListener (this); }

    void parameterValueChanged (int, float) override {}
    void parameterGestureChanged (int, bool starting) override { (starting ? begins : ends)++; }

    juce::AudioProcessorParameter& parameter;
    int begins = 0;
    int ends = 0;
};

juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> at)
{
    const auto now = juce::Time::getCurrentTime();
    return {juce::Desktop::getInstance().getMainMouseSource(),
            at,
            {},
            juce::MouseInputSource::defaultPressure,
            0.0f,
            0.0f,
            0.0f,
            0.0f,
            &c,
            &c,
            now,
            at,
            now,
            1,
            false};
}
} // namespace

// ---- The audio side: VisualTap (T-0.6-04)
// ----------------------------------------------------------------------------

TEST_CASE ("Visual frames describe each 64 samples exactly", "[unit][visual]")
{
    VisualTap tap;
    tap.prepare (kRate, 256, 0);
    REQUIRE (tap.getFrameSamples() == 64);
    tap.setWanted (VisualTap::kFrames);
    tap.beginBlock();

    Chunk c (256);
    for (int i = 0; i < 256; ++i)
    {
        const auto at = static_cast<std::size_t> (i);
        c.layerL[at] = static_cast<float> (i) / 256.0f;
        c.layerR[at] = -static_cast<float> (i) / 512.0f;
        c.dryL[at] = 0.1f;
        c.dryR[at] = -0.2f;
        c.outL[at] = i == 70 ? 0.3f : 0.1f;
        c.outR[at] = i == 70 ? -0.6f : -0.1f;
    }
    c.feed (tap, playingAt (4.0), -2.0f);

    const auto frames = readAll (tap);
    REQUIRE (frames.size() == 4);

    // Each frame: both channels' lowest and highest sample.
    CHECK_THAT (frames[0].layerMax, WithinAbs (63.0 / 256.0, 1e-7));
    CHECK_THAT (frames[0].layerMin, WithinAbs (-63.0 / 512.0, 1e-7));
    CHECK_THAT (frames[3].layerMax, WithinAbs (255.0 / 256.0, 1e-7));
    CHECK_THAT (frames[0].dryMin, WithinAbs (-0.2, 1e-7));
    CHECK_THAT (frames[0].dryMax, WithinAbs (0.1, 1e-7));

    // The output's peaks and sums, in the frame they fall in.
    CHECK_THAT (frames[0].peakRight, WithinAbs (0.1, 1e-7));
    CHECK_THAT (frames[1].peakLeft, WithinAbs (0.3, 1e-7));
    CHECK_THAT (frames[1].peakRight, WithinAbs (0.6, 1e-7));
    CHECK_THAT (frames[1].sumLL, WithinAbs (63 * 0.01 + 0.09, 1e-5));
    CHECK_THAT (frames[1].sumLR, WithinAbs (63 * -0.01 - 0.18, 1e-5));
    CHECK_THAT (frames[2].sumRR, WithinAbs (64 * 0.01, 1e-5));
    CHECK (frames[0].loudnessPower > 0.0f);
    CHECK_THAT (frames[2].limiterReductionDb, WithinAbs (2.0, 1e-6));

    // Where each starts in the song (120 BPM: 24000 samples per quarter note).
    for (std::size_t k = 0; k < frames.size(); ++k)
    {
        CHECK (frames[k].numSamples == 64);
        CHECK_THAT (frames[k].ppq, WithinAbs (4.0 + static_cast<double> (k) * 64.0 / 24000.0, 1e-9));
        CHECK_THAT (frames[k].barStartPpq, WithinAbs (4.0, 1e-9));
        CHECK (frames[k].isMoving());
        CHECK (frames[k].hasHostTempo());
    }
}

TEST_CASE ("Visual frames sit where they are heard: moved back by the chain's latency", "[unit][visual]")
{
    // 4800 samples at 120 BPM is 0.2 quarter notes; from 8.1 that is 7.9, in
    // the bar that started at 4.
    VisualTap tap;
    tap.prepare (kRate, 256, 4800);
    tap.setWanted (VisualTap::kFrames);
    tap.beginBlock();
    Chunk c (256);
    c.feed (tap, playingAt (8.1));

    const auto frames = readAll (tap);
    REQUIRE (!frames.empty());
    CHECK_THAT (frames[0].ppq, WithinAbs (7.9, 1e-9));
    CHECK_THAT (frames[0].barStartPpq, WithinAbs (4.0, 1e-9));
}

TEST_CASE ("Without a host transport the visualiser keeps its own 120 BPM clock", "[unit][visual]")
{
    VisualTap tap;
    tap.prepare (kRate, 256, 0);
    tap.setWanted (VisualTap::kFrames);
    tap.beginBlock();

    Chunk c (256);
    for (int chunk = 0; chunk < 3; ++chunk)
        c.feed (tap, TransportState{});

    const auto frames = readAll (tap);
    REQUIRE (frames.size() == 12);
    for (std::size_t k = 0; k < frames.size(); ++k)
    {
        CHECK (frames[k].isMoving());
        CHECK_FALSE (frames[k].hasHostTempo());
        CHECK_THAT (frames[k].ppq, WithinAbs (static_cast<double> (k) * 64.0 / 24000.0, 1e-9));
    }
}

TEST_CASE ("While the transport is stopped the frames don't move", "[unit][visual]")
{
    VisualTap tap;
    tap.prepare (kRate, 256, 0);
    tap.setWanted (VisualTap::kFrames);
    tap.beginBlock();

    auto stopped = playingAt (6.0);
    stopped.isPlaying = false;
    Chunk c (256);
    c.feed (tap, stopped);

    for (const auto& f : readAll (tap))
    {
        CHECK_FALSE (f.isMoving());
        CHECK_THAT (f.ppq, WithinAbs (6.0, 1e-9));
    }
}

TEST_CASE ("The audio thread does no visual work until an editor shows it", "[unit][visual][engine]")
{
    ThunderEngine engine;
    engine.prepare (kRate);
    auto& tap = engine.getVisualTap();
    const auto settings = params::ParamSnapshot::defaults();

    runEngine (engine, 0.5, settings);
    CHECK (readAll (tap).empty());

    tap.setWanted (VisualTap::kFrames);
    runEngine (engine, 0.5, settings);
    const auto frames = readAll (tap);
    CHECK_THAT (static_cast<double> (frames.size()), WithinAbs (0.5 * VisualTap::kFramesPerSecond, 8.0));

    // Turned off: nothing more comes. Turned on again: only fresh frames.
    tap.setWanted (0);
    runEngine (engine, 0.5, settings);
    CHECK (readAll (tap).empty());

    tap.setWanted (VisualTap::kFrames);
    runEngine (engine, 0.2, settings);
    tap.setWanted (0);
    tap.setWanted (VisualTap::kFrames);
    CHECK (readAll (tap).empty());
}

TEST_CASE ("A full visual queue drops frames instead of waiting", "[unit][visual][engine]")
{
    ThunderEngine engine;
    engine.prepare (kRate);
    auto& tap = engine.getVisualTap();
    tap.setWanted (VisualTap::kFrames | VisualTap::kSpectrum | VisualTap::kGrains);

    // Five seconds with nobody reading: more than the queues hold.
    runEngine (engine, 5.0, params::ParamSnapshot::defaults());
    CHECK (readAll (tap).size() == static_cast<std::size_t> (VisualTap::kFrameCapacity));
    CHECK (tap.getDroppedFrames() > 0);
}

TEST_CASE ("The sound is identical whether or not the visualiser reads it", "[unit][visual][engine]")
{
    auto settings = params::ParamSnapshot::defaults();
    settings.set (ParamId::grainPitchSpread, 5.0f);

    ThunderEngine quiet, watched;
    quiet.prepare (kRate);
    watched.prepare (kRate);
    watched.getVisualTap().setWanted (VisualTap::kFrames | VisualTap::kSpectrum | VisualTap::kGrains);

    std::vector<float> a, b;
    runEngine (quiet, 3.0, settings, &a);
    runEngine (watched, 3.0, settings, &b,
               [&watched]
               {
                   std::array<WaveFrame, 64> frames{};
                   while (watched.getVisualTap().readFrames (frames.data(), 64) > 0)
                   {
                   }
               });

    REQUIRE (a.size() == b.size());
    CHECK (std::equal (a.begin(), a.end(), b.begin()));
}

TEST_CASE ("Spectrum samples are the dry signal and the layer in mono", "[unit][visual]")
{
    VisualTap tap;
    tap.prepare (kRate, 256, 0);
    tap.setWanted (VisualTap::kSpectrum);
    tap.beginBlock();

    Chunk c (100);
    for (int i = 0; i < 100; ++i)
    {
        const auto at = static_cast<std::size_t> (i);
        c.layerL[at] = 0.5f;
        c.layerR[at] = -0.1f * static_cast<float> (i) / 100.0f;
        c.dryL[at] = 0.2f;
        c.dryR[at] = 0.4f;
    }
    c.feed (tap, playingAt (0.0));

    std::array<visual::SpectrumSample, 200> samples{};
    REQUIRE (tap.readSpectrum (samples.data(), 200) == 100);
    CHECK_THAT (samples[0].dry, WithinAbs (0.3, 1e-7));
    CHECK_THAT (samples[99].layer, WithinAbs (0.5 * (0.5 - 0.099), 1e-7));
    CHECK (readAll (tap).empty()); // frames weren't asked for
}

TEST_CASE ("Grain snapshots show every sounding grain about 60 times a second", "[unit][visual][grains]")
{
    ThunderEngine engine;
    engine.prepare (kRate);
    auto& tap = engine.getVisualTap();
    tap.setWanted (VisualTap::kGrains);

    auto settings = params::ParamSnapshot::defaults();
    settings.set (ParamId::grainAmount, 100.0f);
    settings.set (ParamId::grainPitchSpread, 5.0f);
    settings.set (ParamId::grainPanSpread, 100.0f);
    settings.set (ParamId::grainDensity, 60.0f);

    int snapshots = 0;
    visual::GrainSnapshot latest;
    auto anySparks = false;
    runEngine (engine, 1.0, settings, nullptr,
               [&]
               {
                   visual::GrainSnapshot s;
                   while (tap.readGrains (s))
                   {
                       ++snapshots;
                       latest = s;
                       anySparks = anySparks || s.count > 0;

                       for (int i = 0; i < s.count; ++i)
                       {
                           const auto& spark = s.sparks[static_cast<std::size_t> (i)];
                           CHECK (spark.ageSeconds >= 0.0f);
                           CHECK (spark.ageSeconds <= spark.startAgeSeconds + 1.0e-6f); // read forwards
                           CHECK (spark.startAgeSeconds < 5.0f);
                           CHECK (std::abs (spark.semitones) <= 5.0f + 1.0e-3f);
                           CHECK (std::abs (spark.pan) <= 1.0f);
                           CHECK (spark.window >= 0.0f);
                           CHECK (spark.window <= 1.0f);
                       }
                   }
               });

    // readGrains drains everything that came, so count each publication once.
    CHECK (snapshots >= 30);
    CHECK (anySparks);
    CHECK (latest.count <= engine.getGrains().kMaxGrains);

    // Grains switched off: once the module has faded out, no sparks.
    settings.set (ParamId::grainOn, 0.0f);
    runEngine (engine, 0.5, settings);
    visual::GrainSnapshot off;
    REQUIRE (tap.readGrains (off));
    CHECK (off.count == 0);
}

TEST_CASE ("Sixty grain snapshots a second, whatever the host's block size", "[unit][visual][grains]")
{
    VisualTap tap;
    tap.prepare (kRate, 256, 0);
    tap.setWanted (VisualTap::kGrains);
    tap.beginBlock();
    dsp::GrainModule grains;
    grains.prepare ({kRate, 256, 2});

    int count = 0;
    visual::GrainSnapshot s;
    for (int done = 0; done < static_cast<int> (kRate); done += 100)
    {
        tap.tapGrains (grains, true, 100);
        while (tap.readGrains (s))
            ++count;
    }
    CHECK (count >= 59);
    CHECK (count <= 61);
}

// ---- The waveform's history (T-0.6-05)
// ----------------------------------------------------------------------------

TEST_CASE ("The waveform draws bar and beat lines where the host's bars and beats fall", "[unit][visual]")
{
    ui::WaveformHistory history;
    const auto frameSeconds = 64.0 / kRate;
    history.prepare (frameSeconds);

    const auto frameAt = [] (double ppq, int numerator)
    {
        WaveFrame f;
        f.ppq = ppq;
        f.timeSigNumerator = static_cast<std::uint8_t> (numerator);
        f.barStartPpq = std::floor (ppq / numerator) * numerator;
        f.flags = WaveFrame::kMoving | WaveFrame::kHostTempo;
        return f;
    };

    const auto lines = [&history] (std::uint8_t kind)
    {
        std::vector<int> bars;
        for (int i = history.size() - 1; i >= 0; --i)
            if (history.fromNewest (i).line == kind)
                bars.push_back (history.fromNewest (i).bar);
        return bars;
    };

    // Two bars of 4/4 at 120 BPM from bar 2 (ppq 4 … 12).
    const auto step = frameSeconds * 2.0; // quarter notes per frame
    for (auto ppq = 4.0; ppq < 12.0 - 1.0e-9; ppq += step)
        history.add (frameAt (ppq, 4));

    CHECK (lines (ui::WaveformHistory::kBarLine) == std::vector<int>{2, 3});
    CHECK (lines (ui::WaveformHistory::kBeatLine).size() == 6);
    CHECK_THAT (history.secondsPerBar(), WithinAbs (2.0, 1e-9));
    CHECK_THAT (history.windowSeconds (2), WithinAbs (4.0, 1e-9));

    // A loop back to the song's start begins bar 1 there.
    history.add (frameAt (0.0, 4));
    CHECK (history.fromNewest (0).line == ui::WaveformHistory::kBarLine);
    CHECK (history.fromNewest (0).bar == 1);

    // 3/4: three beats a bar.
    history.clear();
    for (auto ppq = 3.0; ppq < 9.0 - 1.0e-9; ppq += step)
        history.add (frameAt (ppq, 3));
    CHECK (lines (ui::WaveformHistory::kBarLine) == std::vector<int>{2, 3});
    CHECK (lines (ui::WaveformHistory::kBeatLine).size() == 4);
    CHECK (history.getBeatsPerBar() == 3);

    // Restarting in the middle of a bar draws no line there.
    history.clear();
    history.add (frameAt (5.5, 4));
    CHECK (history.fromNewest (0).line == ui::WaveformHistory::kNoLine);
}

// ---- Meters (T-0.6-07)
// ----------------------------------------------------------------------------

TEST_CASE ("Meter ballistics: instant attack, 20 dB/s release, 1.5 s peak hold, 2 s clip light", "[unit][visual]")
{
    ui::MeterModel meters;
    meters.prepare (64.0 / kRate);

    WaveFrame loud;
    loud.peakLeft = loud.peakRight = 0.5f; // −6.02 dB
    loud.numSamples = 64;
    meters.add (loud);
    CHECK (meters.advance (1.0 / 60.0));
    CHECK_THAT (meters.getLevelDb (0), WithinAbs (-6.02, 0.01));
    CHECK_THAT (meters.getPeakHoldDb (1), WithinAbs (-6.02, 0.01));

    // Half a second of silence: the level falls 10 dB, the hold stays.
    for (int i = 0; i < 30; ++i)
        meters.advance (1.0 / 60.0);
    CHECK_THAT (meters.getLevelDb (0), WithinAbs (-6.02 - 10.0 * (30.0 / 30.0), 0.05));
    CHECK_THAT (meters.getPeakHoldDb (0), WithinAbs (-6.02, 0.01));

    // After 1.5 s the hold falls too.
    for (int i = 0; i < 90; ++i)
        meters.advance (1.0 / 60.0);
    CHECK (meters.getPeakHoldDb (0) < -7.0f);

    // A sample at 0 dBFS lights the clip light for 2 s; a click clears it.
    WaveFrame clip = loud;
    clip.peakRight = 1.0f;
    meters.add (clip);
    meters.advance (0.01);
    CHECK_FALSE (meters.isClipped (0));
    CHECK (meters.isClipped (1));
    meters.advance (1.9);
    CHECK (meters.isClipped (1));
    meters.advance (0.2);
    CHECK_FALSE (meters.isClipped (1));

    meters.add (clip);
    meters.advance (0.01);
    meters.resetClip();
    CHECK_FALSE (meters.isClipped (1));
}

TEST_CASE ("Short-term loudness reads a -20 dBFS 997 Hz sine as -20 LUFS", "[unit][visual]")
{
    // ITU-R BS.1770: a 997 Hz sine at −20 dBFS in both channels is −20 LUFS.
    const auto sine = test::sine (997.0, kRate, 3.5, 0.1f);
    ui::MeterModel meters;
    meters.prepare (64.0 / kRate);
    for (const auto& f : framesOf (sine, sine))
        meters.add (f);
    meters.advance (0.0);

    REQUIRE (meters.getShortTermLufs().has_value());
    CHECK_THAT (*meters.getShortTermLufs(), WithinAbs (-20.0, 0.1));

    // Silence reads nothing.
    ui::MeterModel silent;
    silent.prepare (64.0 / kRate);
    const std::vector<float> zeros (48000, 0.0f);
    for (const auto& f : framesOf (zeros, zeros))
        silent.add (f);
    CHECK_FALSE (silent.getShortTermLufs().has_value());
    CHECK_FALSE (silent.getCorrelation().has_value());
}

TEST_CASE ("Correlation: +1 for mono, -1 out of phase, near 0 for unrelated channels", "[unit][visual]")
{
    const auto measure = [] (const std::vector<float>& left, const std::vector<float>& right)
    {
        ui::MeterModel meters;
        meters.prepare (64.0 / kRate);
        for (const auto& f : framesOf (left, right))
            meters.add (f);
        return meters.getCorrelation();
    };

    const auto a = test::whiteNoise (kRate, 1.0);
    auto inverted = a;
    for (auto& x : inverted)
        x = -x;
    auto other = std::vector<float> (a.size());
    juce::Random random (99);
    for (auto& x : other)
        x = random.nextFloat() - 0.5f;

    CHECK_THAT (*measure (a, a), WithinAbs (1.0, 1e-6));
    CHECK_THAT (*measure (a, inverted), WithinAbs (-1.0, 1e-6));
    CHECK (std::abs (*measure (a, other)) < 0.1f);
}

TEST_CASE ("The limiter light shows reductions over 1 dB for half a second", "[unit][visual]")
{
    ui::MeterModel meters;
    meters.prepare (64.0 / kRate);

    WaveFrame f;
    f.numSamples = 64;
    f.limiterReductionDb = 0.8f;
    meters.add (f);
    meters.advance (0.01);
    CHECK_FALSE (meters.isLimiting());

    f.limiterReductionDb = 2.5f;
    meters.add (f);
    meters.advance (0.01);
    CHECK (meters.isLimiting());
    meters.advance (0.6);
    CHECK_FALSE (meters.isLimiting());
}

// ---- Spectrum (T-0.6-06)
// ----------------------------------------------------------------------------

TEST_CASE ("The spectrum shows a sine at its frequency and level", "[unit][visual]")
{
    for (const auto hz : {60.0, 1000.0, 7000.0})
    {
        ui::SpectrumAnalyser analyser;
        analyser.prepare (kRate);
        const auto sine = test::sine (hz, kRate, 0.3, 0.5f); // −6.02 dBFS
        std::vector<visual::SpectrumSample> samples (sine.size());
        for (std::size_t i = 0; i < sine.size(); ++i)
            samples[i] = {sine[i], sine[i]};
        analyser.push (samples.data(), static_cast<int> (samples.size()));
        CHECK (analyser.update (1.0 / 60.0));

        const auto& db = analyser.getLayerDb();
        const auto peak = std::max_element (db.begin(), db.end());
        const auto at = ui::SpectrumAnalyser::frequencyOf (static_cast<int> (peak - db.begin()));
        INFO (hz << " Hz shows its peak at " << at << " Hz, " << *peak << " dB");
        CHECK (std::abs (std::log2 (at / hz)) < 1.0 / 12.0);
        CHECK_THAT (*peak, WithinAbs (-6.02, 1.0));
        CHECK_THAT (analyser.getDryDb()[static_cast<std::size_t> (peak - db.begin())], WithinAbs (*peak, 1e-4));
    }
}

TEST_CASE ("The spectrum falls with a 300 ms release when the sound stops", "[unit][visual]")
{
    ui::SpectrumAnalyser analyser;
    analyser.prepare (kRate);
    const auto sine = test::sine (1000.0, kRate, 0.2, 0.5f);
    std::vector<visual::SpectrumSample> samples (sine.size());
    for (std::size_t i = 0; i < sine.size(); ++i)
        samples[i] = {sine[i], sine[i]};
    analyser.push (samples.data(), static_cast<int> (samples.size()));
    analyser.update (1.0 / 60.0);
    const auto& db = analyser.getLayerDb();
    const auto peak = static_cast<std::size_t> (std::max_element (db.begin(), db.end()) - db.begin());
    const auto before = db[peak];

    // Silence, 0.3 s later: 63% of the way down (in dB) to the floor.
    const std::vector<visual::SpectrumSample> silence (static_cast<std::size_t> (analyser.getFftSize()));
    analyser.push (silence.data(), static_cast<int> (silence.size()));
    analyser.update (0.3);
    const auto expected = before + (ui::SpectrumAnalyser::kFloorDb - before) * (1.0f - std::exp (-1.0f));
    CHECK_THAT (db[peak], WithinAbs (expected, 0.5));

    // Without new audio nothing changes.
    CHECK_FALSE (analyser.update (0.3));
}

TEST_CASE ("The filter curve is the Tone module's real response", "[unit][visual][tone]")
{
    const auto measured = [] (const params::ParamSnapshot& settings, double hz)
    {
        dsp::ToneModule tone;
        tone.prepare ({kRate, 256, 2});
        const auto input = test::sine (hz, kRate, 1.0);
        const auto out = test::runModule (tone, input, settings);
        const auto from = input.size() / 2;
        return 10.0 * std::log10 (test::energy (out, from, out.size()) / test::energy (input, from, input.size()));
    };

    for (const auto steep : {false, true})
    {
        auto settings = params::ParamSnapshot::defaults();
        settings.set (ParamId::hpFreq, 200.0f);
        settings.set (ParamId::lpFreq, 6000.0f);
        test::setChoice (settings, ParamId::hpSlope, steep ? params::FilterSlope::Db24 : params::FilterSlope::Db12);
        test::setChoice (settings, ParamId::lpSlope, steep ? params::FilterSlope::Db12 : params::FilterSlope::Db24);

        ui::FilterResponse curve;
        curve.highPassHz = 200.0f;
        curve.highPass24 = steep;
        curve.lowPassHz = 6000.0f;
        curve.lowPass24 = !steep;

        CHECK_THAT (curve.decibelsAt (200.0, kRate), WithinAbs (-3.01, 0.05));
        CHECK_THAT (curve.decibelsAt (6000.0, kRate), WithinAbs (-3.01, 0.05));
        for (const auto hz : {50.0, 100.0, 200.0, 1000.0, 6000.0, 12000.0, 18000.0})
        {
            INFO ((steep ? "24/12" : "12/24") << " dB/oct at " << hz << " Hz");
            CHECK_THAT (curve.decibelsAt (hz, kRate), WithinAbs (measured (settings, hz), 0.3));
        }
    }
}

// ---- The editor (T-0.6-05/06/07/10)
// ----------------------------------------------------------------------------

TEST_CASE ("While the song plays, the editor shows the waveform, the meters and the loudness",
           "[unit][visual][ui][plugin]")
{
    Fixture f;
    auto& visualiser = f.editor->getVisualiser();
    auto& waveform = visualiser.getWaveform();

    // Nothing yet: the well invites the user to play.
    (void) waveform.createComponentSnapshot (waveform.getLocalBounds());
    CHECK (waveform.wasEmpty());
    CHECK (f.editor->getOutputPanel().getLoudness().getText().contains ("LUFS"));

    f.play (5.0);
    (void) waveform.createComponentSnapshot (waveform.getLocalBounds());
    CHECK_FALSE (waveform.wasEmpty());
    CHECK (waveform.getLastBarLines() >= 2); // a 2-bar window at 120 BPM after 5 s

    const auto& meters = f.editor->getVisualModel().getMeters();
    CHECK (meters.getLevelDb (0) > -30.0f);
    CHECK (meters.getShortTermLufs().has_value());
    CHECK (meters.getCorrelation().has_value());
    CHECK_FALSE (f.editor->getOutputPanel().getLoudness().getText().contains (juce::String::fromUTF8 ("\xe2\x80\x93")));
    CHECK (f.editor->getOutputPanel().getMeter().describe().startsWith ("Left "));

    // Grains are on by default: their sparks arrive with the waveform.
    CHECK (f.processor.getUiBridge().getWanted() & UiBridge::kGrains);

    // Stopped: the waveform freezes (nothing new is added) and dims after 2 s.
    const auto size = f.editor->getVisualModel().getHistory().size();
    f.head.playing = false;
    f.play (2.5);
    CHECK (f.editor->getVisualModel().getHistory().size() == size);
    CHECK (f.editor->getVisualModel().getSecondsStopped() > 2.0);
}

TEST_CASE ("A hidden or closed editor asks the audio thread for nothing", "[unit][visual][ui][plugin]")
{
    Fixture f;
    auto& bridge = f.processor.getUiBridge();

    f.editor->updateVisuals (true, 0.0);
    CHECK ((bridge.getWanted() & UiBridge::kFrames) != 0);

    f.editor->updateVisuals (false, 0.0);
    CHECK (bridge.getWanted() == 0);

    f.editor->updateVisuals (true, 0.0);
    f.close();
    CHECK (bridge.getWanted() == 0);
}

TEST_CASE ("The spectrum and the grains are read only while they show", "[unit][visual][ui][plugin]")
{
    Fixture f;
    auto& bridge = f.processor.getUiBridge();
    auto& visualiser = f.editor->getVisualiser();

    f.editor->selectModule (0);
    f.editor->updateVisuals (true, 0.0);
    CHECK ((bridge.getWanted() & UiBridge::kSpectrum) == 0);
    CHECK ((bridge.getWanted() & UiBridge::kGrains) != 0);

    visualiser.chooseMenuItem (ui::VisualiserRegion::kMenuShowGrains);
    f.editor->updateVisuals (true, 0.0);
    CHECK ((bridge.getWanted() & UiBridge::kGrains) == 0);

    // Tone's mini curve needs the spectrum even in the waveform view.
    f.editor->selectModule (5);
    f.editor->updateVisuals (true, 0.0);
    CHECK ((bridge.getWanted() & UiBridge::kSpectrum) != 0);

    f.editor->selectModule (0);
    visualiser.getViewToggle().choose (1);
    f.editor->updateVisuals (true, 0.0);
    CHECK ((bridge.getWanted() & UiBridge::kSpectrum) != 0);
}

TEST_CASE ("The view is remembered for the project; new projects open with the Settings default",
           "[unit][visual][ui][plugin][state]")
{
    Fixture f;
    auto& visualiser = f.editor->getVisualiser();
    CHECK (visualiser.getView() == ui::VisualiserRegion::View::wave);
    CHECK (f.processor.getSessionState().uiView == state::VisualiserView::notSet);

    visualiser.getViewToggle().choose (1);
    CHECK (visualiser.getView() == ui::VisualiserRegion::View::spectrum);
    CHECK (visualiser.getSpectrum().isVisible());
    CHECK_FALSE (visualiser.getWaveform().isVisible());
    CHECK (f.processor.getSessionState().uiView == state::VisualiserView::spectrum);

    // The project's view survives saving and reopening.
    juce::MemoryBlock saved;
    f.processor.getStateInformation (saved);
    f.close();
    ThunderProcessor reopened;
    reopened.setSettingsFile (f.temp.folder.getChildFile ("settings.xml"));
    reopened.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK (reopened.getSessionState().uiView == state::VisualiserView::spectrum);
    {
        std::unique_ptr<juce::AudioProcessorEditor> e (reopened.createEditorAndMakeActive());
        CHECK (dynamic_cast<ThunderEditor&> (*e).getVisualiser().getView() == ui::VisualiserRegion::View::spectrum);
        reopened.editorBeingDeleted (e.get());
    }

    // Settings sets the view for projects that haven't chosen one.
    f.open();
    f.editor->openSettings();
    f.editor->getSettingsPanel()->setSpectrumByDefault (true);
    CHECK (state::UserSettings::load (f.temp.folder.getChildFile ("settings.xml")).spectrumByDefault);

    ThunderProcessor fresh;
    fresh.setSettingsFile (f.temp.folder.getChildFile ("settings.xml"));
    {
        std::unique_ptr<juce::AudioProcessorEditor> e (fresh.createEditorAndMakeActive());
        CHECK (dynamic_cast<ThunderEditor&> (*e).getVisualiser().getView() == ui::VisualiserRegion::View::spectrum);
        fresh.editorBeingDeleted (e.get());
    }

    // "Open in Spectrum" in the Tone module.
    f.editor->getVisualiser().setView (ui::VisualiserRegion::View::wave);
    f.editor->getTonePanel().getOpenSpectrumButton().onClick();
    CHECK (f.editor->getVisualiser().getView() == ui::VisualiserRegion::View::spectrum);
}

TEST_CASE ("The visualiser's menu sets the time window, freezes, and hides the dry signal and grains",
           "[unit][visual][ui][plugin]")
{
    Fixture f;
    auto& visualiser = f.editor->getVisualiser();
    using R = ui::VisualiserRegion;

    const auto menu = visualiser.buildMenu();
    CHECK (menu.getNumItems() >= 6);

    visualiser.chooseMenuItem (R::kMenuBars + 4);
    CHECK (visualiser.getWaveform().getBars() == 4);
    visualiser.chooseMenuItem (R::kMenuShowDry);
    visualiser.chooseMenuItem (R::kMenuShowGrains);
    CHECK_FALSE (visualiser.getOptions().showDry);
    CHECK_FALSE (visualiser.getWaveform().isShowingGrains());

    // Remembered for every project.
    const auto settings = state::UserSettings::load (f.temp.folder.getChildFile ("settings.xml"));
    CHECK (settings.visualiserBars == 4);
    CHECK_FALSE (settings.showDry);
    CHECK_FALSE (settings.showGrains);
    f.close();
    f.open();
    CHECK (f.editor->getVisualiser().getOptions().bars == 4);
    CHECK_FALSE (f.editor->getVisualiser().getOptions().showGrains);

    // Freeze: the waveform stops changing, the meters carry on.
    f.play (1.0);
    f.editor->getVisualiser().chooseMenuItem (R::kMenuFreeze);
    CHECK (f.editor->getVisualModel().isFrozen());
    const auto size = f.editor->getVisualModel().getHistory().size();
    f.play (0.5);
    CHECK (f.editor->getVisualModel().getHistory().size() == size);
    CHECK (f.editor->getVisualModel().getMeters().getLevelDb (0) > -40.0f);
}

TEST_CASE ("Dragging a filter handle sets its cutoff as one gesture", "[unit][visual][ui][plugin][tone]")
{
    Fixture f;
    f.editor->showSpectrum();
    auto& spectrum = f.editor->getVisualiser().getSpectrum();
    auto& handle = spectrum.getHighPassHandle();
    GestureCounter gestures (f.param (ParamId::hpFreq));

    // The handle sits on the curve at the cutoff.
    const auto centre = handle.getBounds().getCentre().toFloat();
    CHECK_THAT (centre.x, WithinAbs (spectrum.xForFrequency (f.value (ParamId::hpFreq)), 1.0));
    CHECK_THAT (centre.y, WithinAbs (spectrum.curveY (f.value (ParamId::hpFreq)), 1.0));

    const auto target = spectrum.xForFrequency (500.0f);
    const auto local =
        juce::Point<float> (target - static_cast<float> (handle.getX()), centre.y - static_cast<float> (handle.getY()));
    handle.mouseDown (mouseAt (handle, {12.0f, 12.0f}));
    handle.mouseDrag (mouseAt (handle, local));
    handle.mouseUp (mouseAt (handle, local));

    CHECK_THAT (f.value (ParamId::hpFreq), WithinAbs (500.0, 5.0));
    CHECK (gestures.begins == 1);
    CHECK (gestures.ends == 1);

    // The handle follows, and so does Tone's mini curve.
    CHECK_THAT (static_cast<float> (handle.getBounds().getCentreX()), WithinAbs (target, 1.0));
    auto& mini = f.editor->getTonePanel().getCurve();
    CHECK_THAT (static_cast<float> (mini.getHighPassHandle().getBounds().getCentreX()),
                WithinAbs (mini.xForFrequency (f.value (ParamId::hpFreq)), 1.0));

    // A slope change bends the curve; Tone switched off dims the handles.
    const auto before = spectrum.curveY (100.0f);
    f.param (ParamId::hpSlope).setValueNotifyingHost (0.0f); // 12 dB/oct
    CHECK (spectrum.curveY (100.0f) < before);
    f.param (ParamId::toneOn).setValueNotifyingHost (0.0f);
    CHECK (handle.getAlpha() < 1.0f);
}

TEST_CASE ("The visualiser's parts are named for screen readers", "[unit][visual][ui][plugin]")
{
    Fixture f;
    auto& visualiser = f.editor->getVisualiser();
    CHECK (visualiser.getTitle() == "Visualiser");
    CHECK (visualiser.getViewToggle().getTitle().isNotEmpty());

    auto& handle = visualiser.getSpectrum().getHighPassHandle();
    CHECK (handle.getTitle() == "High-pass cutoff");
    // Headless there is no window, so ask for the handler directly.
    const auto access = handle.createAccessibilityHandler();
    REQUIRE (access != nullptr);
    CHECK (access->getRole() == juce::AccessibilityRole::slider);
    REQUIRE (access->getValueInterface() != nullptr);
    CHECK (access->getValueInterface()->getCurrentValueAsString().contains ("Hz"));

    auto& panel = f.editor->getOutputPanel();
    for (auto* c : std::initializer_list<juce::Component*>{&panel.getMeter(), &panel.getLoudness(),
                                                           &panel.getCorrelation(), &panel.getLimiterLight()})
        CHECK (c->getTitle().isNotEmpty());
    const auto meter = panel.getMeter().createAccessibilityHandler();
    REQUIRE (meter->getValueInterface() != nullptr);
    CHECK (meter->getValueInterface()->isReadOnly());
    CHECK (meter->getRole() == juce::AccessibilityRole::progressBar);
}

TEST_CASE ("The scrolling waveform draws the same picture as drawing it all at once", "[unit][visual][ui][plugin]")
{
    // While playing, the view only renders the columns that arrive each frame
    // (T-0.6-09); the result must match a full render of the same history.
    Fixture f;
    auto& waveform = f.editor->getVisualiser().getWaveform();

    // A steady sound, so the hot tint's reference doesn't move.
    f.play (5.0);
    juce::Image image (juce::Image::ARGB, waveform.getWidth(), waveform.getHeight(), true, juce::SoftwareImageType());
    const auto paintInto = [&image] (juce::Component& c)
    {
        image.clear (image.getBounds());
        juce::Graphics g (image);
        c.paintEntireComponent (g, true);
    };

    paintInto (waveform);
    const auto renders = waveform.getFullRenders();
    for (int i = 0; i < 40; ++i)
    {
        test::playMelody (f.processor, f.head, 1.0 / 60.0, kRate, 800);
        f.editor->updateVisuals (true, 1.0 / 60.0);
        paintInto (waveform);
    }
    CHECK (waveform.getFullRenders() == renders); // scrolled, never redrawn whole

    ui::WaveformView fresh (f.editor->getVisualModel());
    fresh.setBounds (waveform.getBounds());
    fresh.setShowGrains (false);
    waveform.setShowGrains (false);
    paintInto (waveform);
    const auto incremental = image.createCopy();
    paintInto (fresh);
    CHECK (fresh.getFullRenders() == 1);

    // Allow the hot tint's reference to differ on a few columns.
    int different = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
        {
            const auto a = incremental.getPixelAt (x, y), b = image.getPixelAt (x, y);
            const auto delta =
                std::max ({std::abs (a.getRed() - b.getRed()), std::abs (a.getGreen() - b.getGreen()),
                           std::abs (a.getBlue() - b.getBlue()), std::abs (a.getAlpha() - b.getAlpha())});
            if (delta > 4)
                ++different;
        }
    int drawn = 0;
    for (int y = 0; y < image.getHeight(); ++y)
        for (int x = 0; x < image.getWidth(); ++x)
            drawn += image.getPixelAt (x, y).getAlpha() > 0 ? 1 : 0;
    INFO (different << " differ, " << drawn << " drawn");
    CHECK (drawn > image.getWidth() * image.getHeight() / 10);
    CHECK (different < image.getWidth() * image.getHeight() / 100);
}
