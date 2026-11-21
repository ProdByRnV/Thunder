// Capture (PRD FR-CAPT-001…009, ROADMAP T-0.5-01…04): captures start on the
// next bar, are compensated for the latency so the file's first sample is the
// bar's first sample (impulse-on-bar), last exactly N bars, survive loops and
// fail cleanly on stops and jumps, and end up as complete WAV files.

#include "support/TestSignals.h"

#include "core/capture/CaptureController.h"
#include "core/capture/CaptureNaming.h"
#include "core/capture/CaptureStatus.h"
#include "core/capture/CaptureWriter.h"
#include "core/engine/ThunderEngine.h"
#include "core/state/UserSettings.h"
#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <vector>

using namespace thunder;
using namespace thunder::capture;
using Catch::Matchers::WithinAbs;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
// A host transport that plays from `ppq` at a fixed tempo and meter.
struct FakeHost
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;
    double ppq = 0.0;
    bool playing = true;

    double quarterNotesPerBar() const { return numerator * 4.0 / denominator; }
    double samplesPerBar() const { return sampleRate * 60.0 / bpm * quarterNotesPerBar(); }

    TransportState now() const
    {
        TransportState t;
        t.bpm = bpm;
        t.timeSigNumerator = numerator;
        t.timeSigDenominator = denominator;
        t.isPlaying = playing;
        t.hasHostInfo = true;
        t.ppqPosition = ppq;
        t.ppqLastBarStart = std::floor (ppq / quarterNotesPerBar() + 1.0e-9) * quarterNotesPerBar();
        return t;
    }

    void advance (int samples)
    {
        if (playing)
            ppq += samples * bpm / (60.0 * sampleRate);
    }

    // The input sample (counted from the current position) where bar `bar` (0-based) starts.
    std::size_t samplesUntilBar (int bar) const
    {
        return static_cast<std::size_t> (std::llround ((bar * quarterNotesPerBar() - ppq) * sampleRate * 60.0 / bpm));
    }
};

// Everything the controller queued, in order.
struct Drained
{
    std::vector<CaptureBlock::Kind> kinds;
    CaptureStart start;
    CaptureFailure failure = CaptureFailure::None;
    std::vector<float> left, right;

    bool completed() const
    {
        return !kinds.empty() && kinds.front() == CaptureBlock::Kind::Start && kinds.back() == CaptureBlock::Kind::End;
    }
};

// Moves what the controller queued into `d`, as the writer would.
void drainInto (CaptureController& controller, Drained& d)
{
    auto block = std::make_unique<CaptureBlock>();
    while (controller.getQueue().pop (block.get(), 1) == 1)
    {
        d.kinds.push_back (block->kind);
        if (block->kind == CaptureBlock::Kind::Start)
            d.start = block->start;
        if (block->kind == CaptureBlock::Kind::Failed)
            d.failure = block->failure;
        if (block->kind == CaptureBlock::Kind::Audio)
        {
            d.left.insert (d.left.end(), block->audio[0].begin(), block->audio[0].begin() + block->numFrames);
            d.right.insert (d.right.end(), block->audio[1].begin(), block->audio[1].begin() + block->numFrames);
        }
    }
}

Drained drain (CaptureController& controller)
{
    Drained d;
    drainInto (controller, d);
    return d;
}

// Plays `input` (mono, as dual mono) through the engine in host blocks of
// `blockSize`, moving the host along. `between` runs before each block. With a
// `sink`, the queue is drained after every block, as the writer thread would.
template<typename Between>
void play (ThunderEngine& engine, FakeHost& host, std::vector<float> input, std::size_t blockSize,
           const ParamSnapshot& snapshot, Drained* sink, Between&& between)
{
    std::vector<float> right = input;
    for (std::size_t start = 0; start < input.size(); start += blockSize)
    {
        between (start);
        const auto n = std::min (blockSize, input.size() - start);
        float* channels[] = {input.data() + start, right.data() + start};
        engine.process (channels, static_cast<int> (n), host.now(), snapshot);
        host.advance (static_cast<int> (n));

        if (sink != nullptr)
            drainInto (engine.getCapture(), *sink);
    }
}

void play (ThunderEngine& engine, FakeHost& host, const std::vector<float>& input, std::size_t blockSize,
           const ParamSnapshot& snapshot, Drained* sink = nullptr)
{
    play (engine, host, input, blockSize, snapshot, sink, [] (std::size_t) {});
}

// Comfortably more than the engine's latency: the capture ends that long
// after the last bar's input.
constexpr std::size_t kMargin = 16384;

// Every module off and Output Mode "Layer Only": the output is the input,
// delayed by exactly the latency.
ParamSnapshot passThrough()
{
    auto s = ParamSnapshot::defaults();
    for (const auto id :
         {ParamId::pitchOn, ParamId::grainOn, ParamId::spaceOn, ParamId::toneOn, ParamId::widthOn, ParamId::autoGain})
        s.set (id, 0.0f);
    test::setChoice (s, ParamId::outputMode, params::OutputMode::LayerOnly);
    return s;
}

std::unique_ptr<ThunderEngine> preparedEngine (double sampleRate)
{
    auto engine = std::make_unique<ThunderEngine>();
    engine->prepare (sampleRate);
    engine->getCapture().allocateQueue();
    return engine;
}

// A fresh folder in the temp directory, removed afterwards.
struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderCaptureTests-" + juce::Uuid().toString());

    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
};

std::unique_ptr<juce::AudioFormatReader> openWav (const juce::File& file)
{
    return std::unique_ptr<juce::AudioFormatReader> (
        juce::WavAudioFormat().createReaderFor (file.createInputStream().release(), true));
}
} // namespace

// ---------------------------------------------------------------------------
// Bar detection
// ---------------------------------------------------------------------------

TEST_CASE ("Capture finds the first sample of the next bar", "[unit][capture]")
{
    FakeHost host;
    const auto samplesPerQuarter = 24000.0; // 120 BPM at 48 kHz

    host.ppq = 0.0; // exactly on a bar
    CHECK (CaptureController::findBarStart (host.now(), host.sampleRate, 512) == 0);

    host.ppq = 4.0 - 100.0 / samplesPerQuarter; // 100 samples before bar 2
    CHECK (CaptureController::findBarStart (host.now(), host.sampleRate, 512) == 100);
    CHECK (CaptureController::findBarStart (host.now(), host.sampleRate, 100) == -1);

    host.ppq = 1.0; // mid-bar, far from the next one
    CHECK (CaptureController::findBarStart (host.now(), host.sampleRate, 512) == -1);

    host.ppq = 4.0 + 0.3 / samplesPerQuarter; // the bar started 0.3 samples ago: this sample is nearest
    CHECK (CaptureController::findBarStart (host.now(), host.sampleRate, 512) == 0);

    host.ppq = 4.0 - 10.4 / samplesPerQuarter; // between samples 10 and 11: 10 is nearer
    CHECK (CaptureController::findBarStart (host.now(), host.sampleRate, 512) == 10);
}

// ---------------------------------------------------------------------------
// Alignment (FR-CAPT-006)
// ---------------------------------------------------------------------------

TEST_CASE ("An impulse on the bar is the first sample of the capture", "[unit][capture][latency]")
{
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        for (const auto blockSize : {std::size_t{64}, std::size_t{512}, std::size_t{1000}})
        {
            INFO ("sample rate " << sampleRate << ", block size " << blockSize);

            auto engine = preparedEngine (sampleRate);
            FakeHost host;
            host.sampleRate = sampleRate;
            host.ppq = 1.3; // mid-bar: the capture starts at bar 2

            const auto barAt = host.samplesUntilBar (1);
            const auto bar = static_cast<std::size_t> (std::llround (host.samplesPerBar()));
            const auto input = test::impulse (barAt + bar + kMargin, barAt, 0.5f);

            engine->getCapture().arm (1, CaptureSource::FullMix);
            play (*engine, host, input, blockSize, passThrough());

            const auto d = drain (engine->getCapture());
            REQUIRE (d.completed());
            CHECK (d.left.size() == bar);
            CHECK (test::peakIndex (d.left) == 0);
            CHECK_THAT (d.left[0], WithinAbs (0.5, 1.0e-5));
            CHECK_THAT (d.right[0], WithinAbs (0.5, 1.0e-5));
            CHECK (test::energy (d.left, 1, d.left.size()) < 1.0e-9);
        }
    }
}

TEST_CASE ("A Layer capture is aligned too, through the pitch shifter", "[unit][capture][latency]")
{
    // Pitch at unison and Blend: the layer tap holds the layer alone, and its
    // impulse is still the capture's first sample.
    auto snapshot = ParamSnapshot::defaults();
    test::setSemitoneShift (snapshot, 0.0f);
    for (const auto id : {ParamId::grainOn, ParamId::spaceOn, ParamId::toneOn, ParamId::autoGain})
        snapshot.set (id, 0.0f);

    auto engine = preparedEngine (48000.0);
    FakeHost host;
    host.ppq = 2.0;
    const auto barAt = host.samplesUntilBar (1);
    const auto bar = static_cast<std::size_t> (std::llround (host.samplesPerBar()));

    engine->getCapture().arm (1, CaptureSource::Layer);
    play (*engine, host, test::impulse (barAt + bar + kMargin, barAt, 0.5f), 512, snapshot);

    const auto d = drain (engine->getCapture());
    REQUIRE (d.completed());
    CHECK (d.start.source == CaptureSource::Layer);
    CHECK (test::peakIndex (d.left) == 0);
    CHECK (d.left[0] > 0.3f);
}

TEST_CASE ("A Layer capture is what Layer Only would play", "[unit][capture]")
{
    // The same melody through two engines: one in Blend capturing the Layer,
    // one in Layer Only capturing its output.
    const auto melody = []
    {
        std::vector<float> s;
        for (const auto hz : {220.0, 261.63, 329.63, 293.66, 246.94, 220.0})
        {
            const auto note = test::sine (hz, 48000.0, 0.8, 0.3f);
            s.insert (s.end(), note.begin(), note.end());
        }
        return s;
    }();

    auto blend = ParamSnapshot::defaults();
    auto layerOnly = blend;
    test::setChoice (layerOnly, ParamId::outputMode, params::OutputMode::LayerOnly);

    auto capture = [&melody] (const ParamSnapshot& snapshot, CaptureSource source)
    {
        auto engine = preparedEngine (48000.0);
        FakeHost host;
        host.ppq = 0.5;
        engine->getCapture().arm (1, source);
        play (*engine, host, melody, 512, snapshot);
        return drain (engine->getCapture());
    };

    const auto layer = capture (blend, CaptureSource::Layer);
    const auto output = capture (layerOnly, CaptureSource::FullMix);

    REQUIRE (layer.completed());
    REQUIRE (output.completed());
    REQUIRE (layer.left.size() == output.left.size());
    CHECK (test::energy (layer.left, 0, layer.left.size()) > 1.0);

    float worst = 0.0f;
    for (std::size_t i = 0; i < layer.left.size(); ++i)
        worst =
            std::max ({worst, std::abs (layer.left[i] - output.left[i]), std::abs (layer.right[i] - output.right[i])});
    CHECK (worst < 1.0e-6f);
}

// ---------------------------------------------------------------------------
// Length, tempo, meter (FR-CAPT-001/003)
// ---------------------------------------------------------------------------

TEST_CASE ("A capture lasts exactly N bars at any tempo, meter and block size", "[unit][capture]")
{
    struct Case
    {
        double sampleRate, bpm;
        int numerator, denominator, bars;
        std::size_t blockSize;
    };

    for (const auto& c :
         {Case{48000.0, 120.0, 4, 4, 1, 512}, Case{44100.0, 137.5, 3, 4, 2, 333}, Case{96000.0, 90.0, 7, 8, 2, 1000},
          Case{48000.0, 240.0, 2, 4, 16, 4096}, Case{48000.0, 150.0, 6, 8, 8, 31}})
    {
        INFO (c.bpm << " BPM, " << c.numerator << "/" << c.denominator << ", " << c.bars << " bars, blocks of "
                    << c.blockSize);

        auto engine = preparedEngine (c.sampleRate);
        FakeHost host;
        Drained d;
        host.sampleRate = c.sampleRate;
        host.bpm = c.bpm;
        host.numerator = c.numerator;
        host.denominator = c.denominator;
        host.ppq = 0.25;

        const auto expected = static_cast<double> (c.bars) * host.samplesPerBar();
        const auto total = host.samplesUntilBar (1) + static_cast<std::size_t> (expected) + kMargin;

        engine->getCapture().arm (c.bars, CaptureSource::FullMix);
        play (*engine, host, std::vector<float> (total, 0.1f), c.blockSize, ParamSnapshot::defaults(), &d);

        drainInto (engine->getCapture(), d);
        REQUIRE (d.completed());
        CHECK (std::abs (static_cast<double> (d.left.size()) - expected) <= 1.0);
        CHECK (d.start.bars == c.bars);
        CHECK_THAT (d.start.bpm, WithinAbs (c.bpm, 1.0e-9));
        CHECK (d.start.timeSigNumerator == c.numerator);
        CHECK_THAT (d.start.sampleRate, WithinAbs (c.sampleRate, 1.0e-9));
        CHECK (engine->getCapture().getPhase() == CaptureController::Phase::Idle);
        CHECK (juce::exactlyEqual (engine->getCapture().getProgress(), 1.0f));
    }
}

TEST_CASE ("A capture armed on a bar line starts on that bar", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host; // ppq 0: playback starts on bar 1

    engine->getCapture().arm (1, CaptureSource::FullMix);
    play (*engine, host, test::impulse (96000 + kMargin, 0, 0.5f), 256, passThrough());

    const auto d = drain (engine->getCapture());
    REQUIRE (d.completed());
    CHECK (test::peakIndex (d.left) == 0);
}

// ---------------------------------------------------------------------------
// Transport (FR-CAPT-003, ARCHITECTURE §8.2)
// ---------------------------------------------------------------------------

TEST_CASE ("An armed capture waits for playback", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    Drained d;
    host.playing = false;

    engine->getCapture().arm (2, CaptureSource::Layer);
    play (*engine, host, std::vector<float> (48000, 0.1f), 512, ParamSnapshot::defaults());

    CHECK (engine->getCapture().getPhase() == CaptureController::Phase::Armed);
    CHECK (engine->getCapture().isWaitingForPlay());
    CHECK (drain (engine->getCapture()).kinds.empty());

    // Press play: it records from bar 1.
    host.playing = true;
    play (*engine, host, std::vector<float> (2 * 96000 + kMargin, 0.1f), 512, ParamSnapshot::defaults(), &d);
    drainInto (engine->getCapture(), d);
    CHECK (d.completed());
    CHECK (d.left.size() == 2u * 96000u);
}

TEST_CASE ("Stopping playback mid-capture fails it", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    Drained d;
    engine->getCapture().arm (2, CaptureSource::FullMix);

    play (*engine, host, std::vector<float> (120000, 0.1f), 512, ParamSnapshot::defaults(), &d,
          [&host] (std::size_t start)
          {
              if (start >= 100000)
                  host.playing = false;
          });

    drainInto (engine->getCapture(), d);
    REQUIRE (d.kinds.size() >= 2);
    CHECK (d.kinds.front() == CaptureBlock::Kind::Start);
    CHECK (d.kinds.back() == CaptureBlock::Kind::Failed);
    CHECK (d.failure == CaptureFailure::TransportStopped);
    CHECK (engine->getCapture().getPhase() == CaptureController::Phase::Idle);
}

TEST_CASE ("Jumping somewhere off the bar grid fails the capture", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    Drained d;
    engine->getCapture().arm (2, CaptureSource::FullMix);

    play (*engine, host, std::vector<float> (120000, 0.1f), 512, ParamSnapshot::defaults(), &d,
          [&host, jumped = false] (std::size_t start) mutable
          {
              if (!jumped && start >= 50000)
              {
                  host.ppq += 1.5; // a cursor move by a beat and a half
                  jumped = true;
              }
          });

    drainInto (engine->getCapture(), d);
    CHECK (d.kinds.back() == CaptureBlock::Kind::Failed);
    CHECK (d.failure == CaptureFailure::TransportJumped);
}

TEST_CASE ("A loop that wraps by whole bars doesn't stop the capture", "[unit][capture]")
{
    // A 2-bar loop, a 4-bar capture: the audio is continuous and on the grid.
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    Drained d;
    engine->getCapture().arm (4, CaptureSource::FullMix);

    play (*engine, host, std::vector<float> (4 * 96000 + kMargin, 0.1f), 500, ParamSnapshot::defaults(), &d,
          [&host] (std::size_t)
          {
              if (host.ppq >= 8.0)
                  host.ppq -= 8.0; // mid-block, as hosts do: the overshoot carries over
          });

    drainInto (engine->getCapture(), d);
    REQUIRE (d.completed());
    CHECK (d.left.size() == 4u * 96000u);
}

TEST_CASE ("Small position rounding is not a jump", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    Drained d;
    engine->getCapture().arm (1, CaptureSource::FullMix);

    // A host that reports positions rounded to 1/96 of a beat.
    play (*engine, host, std::vector<float> (96000 + kMargin, 0.1f), 512, ParamSnapshot::defaults(), &d,
          [&host, exact = 0.0] (std::size_t start) mutable
          {
              exact = static_cast<double> (start) / 24000.0;
              host.ppq = std::round (exact * 96.0) / 96.0;
          });

    drainInto (engine->getCapture(), d);
    CHECK (d.completed());
}

TEST_CASE ("Cancelling disarms, or stops a recording and discards it", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    host.ppq = 1.0;
    auto& capture = engine->getCapture();

    capture.arm (1, CaptureSource::FullMix);
    play (*engine, host, std::vector<float> (1024, 0.1f), 512, ParamSnapshot::defaults());
    CHECK (capture.getPhase() == CaptureController::Phase::Armed);

    capture.cancel();
    play (*engine, host, std::vector<float> (48000 * 3, 0.1f), 512, ParamSnapshot::defaults());
    CHECK (capture.getPhase() == CaptureController::Phase::Idle);
    CHECK (drain (capture).kinds.empty());

    capture.arm (2, CaptureSource::FullMix);
    play (*engine, host, std::vector<float> (48000 * 3, 0.1f), 512, ParamSnapshot::defaults());
    CHECK (capture.getPhase() == CaptureController::Phase::Recording);
    capture.cancel();
    play (*engine, host, std::vector<float> (512, 0.1f), 512, ParamSnapshot::defaults());

    const auto d = drain (capture);
    CHECK (d.kinds.front() == CaptureBlock::Kind::Start);
    CHECK (d.kinds.back() == CaptureBlock::Kind::Cancelled);
    CHECK (capture.getPhase() == CaptureController::Phase::Idle);
}

TEST_CASE ("A host reset mid-capture fails it", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    engine->getCapture().arm (1, CaptureSource::FullMix);
    play (*engine, host, std::vector<float> (20000, 0.1f), 512, ParamSnapshot::defaults());
    REQUIRE (engine->getCapture().getPhase() == CaptureController::Phase::Recording);

    engine->reset();
    const auto d = drain (engine->getCapture());
    CHECK (d.kinds.back() == CaptureBlock::Kind::Failed);
    CHECK (d.failure == CaptureFailure::Restarted);
}

TEST_CASE ("A writer that falls behind fails the capture instead of losing audio", "[unit][capture]")
{
    // Nothing drains the queue: 16 bars at 40 BPM are far more than it holds.
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    host.bpm = 40.0;
    engine->getCapture().arm (16, CaptureSource::FullMix);
    play (*engine, host, std::vector<float> (48000 * 10, 0.1f), 512, ParamSnapshot::defaults());

    CHECK (engine->getCapture().getPhase() == CaptureController::Phase::Idle);
    const auto d = drain (engine->getCapture());
    CHECK (d.kinds.back() == CaptureBlock::Kind::Failed);
    CHECK (d.failure == CaptureFailure::TooSlow);
}

// ---------------------------------------------------------------------------
// Files (FR-CAPT-005/009)
// ---------------------------------------------------------------------------

TEST_CASE ("Capture file names are descriptive and safe", "[unit][capture]")
{
    CHECK (keyTag (9, 1) == "A-min");
    CHECK (keyTag (1, 0) == "C#-maj");
    CHECK (keyTag (5, 3) == "F-dorian");
    CHECK (bpmTag (140.0) == "140");
    CHECK (bpmTag (92.5) == "92.5");
    CHECK (bpmTag (127.999999) == "128");

    CHECK (sanitiseForFileName ("Night Static") == "Night-Static");
    CHECK (sanitiseForFileName ("  a/b\\c:d*?\"<>|  ") == "a-b-c-d");
    CHECK (sanitiseForFileName ("...") == "");
    CHECK (sanitiseForFileName (juce::String::repeatedString ("x", 100)).length() == 40);

    CaptureStart start;
    start.keyRoot = 9;
    start.keyScale = 1;
    start.bpm = 140.0;
    start.bars = 4;
    const juce::Time time (2026, 10, 21, 14, 30, 5); // months count from 0
    CHECK (buildCaptureName (start, "Night Static", time) == "Thunder_Night-Static_A-min_140bpm_4bars_20261121-143005");
    CHECK (buildCaptureName (start, {}, time) == "Thunder_A-min_140bpm_4bars_20261121-143005");
    start.bars = 1;
    CHECK (buildCaptureName (start, {}, time).contains ("_1bar_"));
}

TEST_CASE ("Capture file names never collide", "[unit][capture]")
{
    TempFolder temp;
    const auto first = uniqueCaptureFile (temp.folder, "Thunder_x");
    CHECK (first.getFileName() == "Thunder_x.wav");

    first.create();
    const auto second = uniqueCaptureFile (temp.folder, "Thunder_x");
    CHECK (second.getFileName() == "Thunder_x_2.wav");

    partialFileFor (second).create(); // being written
    CHECK (uniqueCaptureFile (temp.folder, "Thunder_x").getFileName() == "Thunder_x_3.wav");
}

TEST_CASE ("A capture becomes a complete WAV file", "[unit][capture]")
{
    for (const auto format : {CaptureFormat::Int24, CaptureFormat::Float32})
    {
        const auto isFloat = format == CaptureFormat::Float32;
        INFO ((isFloat ? "32-bit float" : "24-bit"));

        TempFolder temp;
        const auto folder = temp.folder.getChildFile ("Captures"); // created on demand
        auto engine = preparedEngine (48000.0);
        CaptureWriter writer (engine->getCapture());
        writer.setSettings ({folder, format, "My Preset"});

        FakeHost host;
        host.ppq = 3.0;
        auto snapshot = passThrough();
        snapshot.set (ParamId::keyRoot, 9.0f);
        snapshot.set (ParamId::keyScale, 1.0f);
        const auto barAt = host.samplesUntilBar (1);
        engine->getCapture().arm (2, CaptureSource::FullMix);

        // The writer drains while the engine plays, as its thread would.
        play (*engine, host, test::impulse (barAt + 2 * 96000 + kMargin, barAt, 0.5f), 512, snapshot, nullptr,
              [&writer] (std::size_t start)
              {
                  if (start % 8192 == 0)
                      writer.service();
              });
        writer.service();

        REQUIRE (writer.getState() == CaptureWriter::State::Ready);
        CHECK (writer.getCompletedCount() == 1);
        const auto file = writer.getLastFile();
        REQUIRE (file.existsAsFile());
        CHECK (file.getParentDirectory() == folder);
        CHECK (file.getFileName().startsWith ("Thunder_My-Preset_A-min_120bpm_2bars_"));
        CHECK (folder.findChildFiles (juce::File::findFiles, false, "*.partial").isEmpty());

        const auto reader = openWav (file);
        REQUIRE (reader != nullptr);
        CHECK (juce::exactlyEqual (reader->sampleRate, 48000.0));
        CHECK (reader->numChannels == 2);
        CHECK (reader->bitsPerSample == (isFloat ? 32u : 24u));
        CHECK (reader->usesFloatingPointData == isFloat);
        CHECK (reader->lengthInSamples == 2 * 96000);

        juce::AudioBuffer<float> audio (2, 16);
        reader->read (&audio, 0, 16, 0, true, true);
        CHECK_THAT (audio.getSample (0, 0), WithinAbs (0.5, isFloat ? 1.0e-5 : 1.0e-5));
        CHECK_THAT (audio.getSample (1, 0), WithinAbs (0.5, 1.0e-5));
        CHECK_THAT (audio.getSample (0, 1), WithinAbs (0.0, 1.0e-6));
    }
}

TEST_CASE ("A failed or cancelled capture leaves no file", "[unit][capture]")
{
    TempFolder temp;
    auto engine = preparedEngine (48000.0);
    CaptureWriter writer (engine->getCapture());
    writer.setSettings ({temp.folder, CaptureFormat::Int24, {}});

    FakeHost host;
    engine->getCapture().arm (2, CaptureSource::FullMix);
    play (*engine, host, std::vector<float> (120000, 0.1f), 512, ParamSnapshot::defaults(), nullptr,
          [&host, &writer] (std::size_t start)
          {
              writer.service();
              if (start >= 100000)
                  host.playing = false;
          });
    writer.service();

    CHECK (writer.getState() == CaptureWriter::State::Failed);
    CHECK (writer.getFailure() == CaptureFailure::TransportStopped);
    CHECK (writer.getLastFile() == juce::File());
    CHECK (temp.folder.getNumberOfChildFiles (juce::File::findFiles) == 0);

    host.playing = true;
    engine->getCapture().arm (1, CaptureSource::FullMix);
    play (*engine, host, std::vector<float> (96000, 0.1f), 512, ParamSnapshot::defaults(), nullptr,
          [&writer] (std::size_t) { writer.service(); });
    writer.service();
    REQUIRE (writer.getState() == CaptureWriter::State::Writing);
    engine->getCapture().cancel();
    play (*engine, host, std::vector<float> (512, 0.1f), 512, ParamSnapshot::defaults());
    writer.service();

    CHECK (writer.getState() == CaptureWriter::State::Idle);
    CHECK (temp.folder.getNumberOfChildFiles (juce::File::findFiles) == 0);
}

TEST_CASE ("A captures folder that can't be created is reported", "[unit][capture]")
{
    TempFolder temp;
    const auto blocker = temp.folder.getChildFile ("not-a-folder");
    REQUIRE (blocker.create());

    auto engine = preparedEngine (48000.0);
    CaptureWriter writer (engine->getCapture());
    writer.setSettings ({blocker.getChildFile ("Captures"), CaptureFormat::Int24, {}});

    FakeHost host;
    engine->getCapture().arm (1, CaptureSource::FullMix);
    play (*engine, host, std::vector<float> (96000 + kMargin, 0.1f), 512, ParamSnapshot::defaults(), nullptr,
          [&writer] (std::size_t) { writer.service(); });
    writer.service();

    CHECK (writer.getState() == CaptureWriter::State::Failed);
    CHECK (writer.getFailure() == CaptureFailure::CannotCreateFile);
}

TEST_CASE ("Deleting the last capture removes its file", "[unit][capture]")
{
    TempFolder temp;
    auto engine = preparedEngine (48000.0);
    CaptureWriter writer (engine->getCapture());
    writer.setSettings ({temp.folder, CaptureFormat::Int24, {}});

    FakeHost host;
    engine->getCapture().arm (1, CaptureSource::FullMix);
    play (*engine, host, std::vector<float> (96000 + kMargin, 0.1f), 512, ParamSnapshot::defaults(), nullptr,
          [&writer] (std::size_t) { writer.service(); });
    writer.service();

    const auto file = writer.getLastFile();
    REQUIRE (file.existsAsFile());
    writer.deleteLastCapture();
    CHECK_FALSE (file.exists());
    CHECK (writer.getLastFile() == juce::File());
    CHECK (writer.getState() == CaptureWriter::State::Idle);
}

// ---------------------------------------------------------------------------
// Settings and status
// ---------------------------------------------------------------------------

TEST_CASE ("User settings round-trip and fall back to defaults", "[unit][capture][state]")
{
    TempFolder temp;
    const auto file = temp.folder.getChildFile ("Thunder").getChildFile ("settings.xml");

    const auto defaults = state::UserSettings::load (file); // no file yet
    CHECK (defaults.captureFormat == CaptureFormat::Int24);
    CHECK (defaults.capturesFolder == juce::File());
    CHECK (defaults.getCapturesFolder() == state::UserSettings::defaultCapturesFolder());
    CHECK (state::UserSettings::defaultCapturesFolder().getFullPathName().contains ("Thunder"));

    state::UserSettings custom;
    custom.captureFormat = CaptureFormat::Float32;
    custom.capturesFolder = temp.folder.getChildFile ("Mine");
    REQUIRE (custom.save (file));
    CHECK (state::UserSettings::load (file) == custom);

    // Damaged values give the defaults.
    REQUIRE (file.replaceWithText ("<PROPERTIES><VALUE name=\"captureFormat\" val=\"8-bit\"/>"
                                   "<VALUE name=\"capturesFolder\" val=\"relative/path\"/></PROPERTIES>"));
    CHECK (state::UserSettings::load (file) == state::UserSettings{});

    REQUIRE (file.replaceWithText ("not xml at all"));
    CHECK (state::UserSettings::load (file) == state::UserSettings{});
}

TEST_CASE ("The capture status reads as the UI shows it", "[unit][capture]")
{
    CaptureStatus s;
    s.bars = 4;

    s.phase = CaptureStatus::Phase::WaitingForPlay;
    CHECK (s.describe() == "Press play to capture");
    CHECK (s.isActive());

    s.phase = CaptureStatus::Phase::Armed;
    CHECK (s.describe() == "Starts at the next bar");

    s.phase = CaptureStatus::Phase::Recording;
    s.progress = 0.0f;
    CHECK (s.describe() == "Recording bar 1 of 4");
    s.progress = 0.6f;
    CHECK (s.describe() == "Recording bar 3 of 4");
    s.progress = 1.0f;
    CHECK (s.currentBar() == 4);

    s.phase = CaptureStatus::Phase::Ready;
    s.file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("Thunder_A-min_120bpm_4bars.wav");
    CHECK (s.describe() == "Thunder_A-min_120bpm_4bars.wav");
    CHECK_FALSE (s.isActive());

    s.phase = CaptureStatus::Phase::Failed;
    s.failure = CaptureFailure::TransportStopped;
    CHECK (s.describe().contains ("Nothing was saved"));

    for (const auto f : {CaptureFailure::TransportJumped, CaptureFailure::Restarted, CaptureFailure::TooSlow,
                         CaptureFailure::CannotCreateFile, CaptureFailure::WriteFailed})
        CHECK (describe (f).isNotEmpty());
}

// ---------------------------------------------------------------------------
// The plugin: processor and editor (T-0.5-04)
// ---------------------------------------------------------------------------

namespace
{
class TestPlayHead final : public juce::AudioPlayHead
{
public:
    double ppq = 0.0;
    bool playing = true;

    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo info;
        info.setBpm (120.0);
        info.setTimeSignature (TimeSignature{4, 4});
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setPpqPositionOfLastBarStart (std::floor (ppq / 4.0) * 4.0);
        return info;
    }
};

// Plays `seconds` of quiet noise through the processor, moving the play head.
void playProcessor (ThunderProcessor& processor, TestPlayHead& head, double seconds)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    juce::Random random (1);

    for (int done = 0; done < static_cast<int> (seconds * 48000.0); done += 512)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, 0.1f * (random.nextFloat() - 0.5f));

        processor.processBlock (buffer, midi);
        if (head.playing)
            head.ppq += 512.0 / 24000.0;
    }
}

bool waitFor (const std::function<bool()>& condition)
{
    for (int i = 0; i < 500 && !condition(); ++i)
        juce::Thread::sleep (10);
    return condition();
}
} // namespace

TEST_CASE ("The plugin captures bars to a file on its writer thread", "[unit][capture][plugin]")
{
    TempFolder temp;
    state::UserSettings settings;
    settings.capturesFolder = temp.folder.getChildFile ("Captures");
    settings.captureFormat = CaptureFormat::Float32;
    REQUIRE (settings.save (temp.folder.getChildFile ("settings.xml")));

    ThunderProcessor processor;
    processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
    TestPlayHead head;
    processor.setPlayHead (&head);
    processor.prepareToPlay (48000.0, 512);

    auto session = processor.getSessionState();
    session.captureBars = 1;
    session.captureSource = state::CaptureSource::Layer;
    processor.setSessionState (session);

    head.ppq = 2.0;
    processor.armCapture();
    playProcessor (processor, head, 0.1);
    CHECK (processor.getCaptureStatus().phase == CaptureStatus::Phase::Armed);

    playProcessor (processor, head, 3.0); // to bar 2, then one bar

    REQUIRE (waitFor ([&] { return processor.getCaptureStatus().phase == CaptureStatus::Phase::Ready; }));
    const auto file = processor.getCaptureStatus().file;
    CHECK (file.getParentDirectory() == settings.capturesFolder);

    const auto reader = openWav (file);
    REQUIRE (reader != nullptr);
    CHECK (reader->usesFloatingPointData);
    CHECK (reader->lengthInSamples == 96000);

    processor.setPlayHead (nullptr);
}

TEST_CASE ("The capture bar arms, shows progress and offers the file for drag-out", "[unit][capture][plugin]")
{
    TempFolder temp;
    state::UserSettings settings;
    settings.capturesFolder = temp.folder;
    REQUIRE (settings.save (temp.folder.getChildFile ("settings.xml")));

    ThunderProcessor processor;
    processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
    TestPlayHead head;
    head.playing = false;
    processor.setPlayHead (&head);
    processor.prepareToPlay (48000.0, 512);

    {
        std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditorAndMakeActive());
        auto* thunderEditor = dynamic_cast<ThunderEditor*> (editor.get());
        REQUIRE (thunderEditor != nullptr);
        CHECK (editor->getHeight() > 0);
        auto& widget = thunderEditor->getCaptureWidget();
        CHECK (widget.getArmButtonText() == "Arm");
        CHECK (widget.getDraggableFile() == juce::File());

        // Arm while stopped.
        widget.toggleArm();
        playProcessor (processor, head, 0.05);
        widget.refresh();
        CHECK (widget.getStatus().phase == CaptureStatus::Phase::WaitingForPlay);
        CHECK (widget.getChipText() == "Press play to capture");
        CHECK (widget.getArmButtonText() == "Armed");

        // Disarm, re-arm, play.
        widget.toggleArm();
        playProcessor (processor, head, 0.05);
        widget.refresh();
        CHECK (widget.getStatus().phase == CaptureStatus::Phase::Idle);

        auto session = processor.getSessionState();
        session.captureBars = 1;
        processor.setSessionState (session);
        widget.toggleArm();
        head.playing = true;
        playProcessor (processor, head, 1.0);
        widget.refresh();
        CHECK (widget.getStatus().phase == CaptureStatus::Phase::Recording);
        CHECK (widget.getArmButtonText() == "Stop");
        CHECK (widget.getDraggableFile() == juce::File()); // nothing to drag mid-capture

        playProcessor (processor, head, 1.5);
        REQUIRE (waitFor ([&] { return processor.getCaptureStatus().phase == CaptureStatus::Phase::Ready; }));
        widget.refresh();
        const auto file = widget.getDraggableFile();
        CHECK (file.existsAsFile());
        CHECK (widget.getChipText() == file.getFileName());
        CHECK (widget.getArmButtonText() == "Arm");

        widget.deleteCapture();
        CHECK_FALSE (file.exists());
        CHECK (widget.getDraggableFile() == juce::File());
    }

    processor.setPlayHead (nullptr);
}
