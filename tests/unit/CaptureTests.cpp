// Capture (PRD FR-CAPT-001…009, ROADMAP T-0.5-01…04): captures start on the
// next bar, are compensated for the latency so the file's first sample is the
// bar's first sample (impulse-on-bar), last exactly N bars, survive loops and
// fail cleanly on stops and jumps, and end up as complete WAV files.

#include "support/TestSignals.h"

#include "core/capture/CaptureController.h"
#include "core/capture/CaptureNaming.h"
#include "core/capture/CaptureStatus.h"
#include "core/capture/CaptureWriter.h"
#include "core/capture/LoopFold.h"
#include "core/engine/ThunderEngine.h"
#include "core/state/UserSettings.h"
#include "plugin/ThunderEditor.h"
#include "plugin/ThunderProcessor.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <functional>
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

// Runs a function once on its own thread.
class FunctionThread final : public juce::Thread
{
public:
    explicit FunctionThread (std::function<void()> f)
        : juce::Thread ("Thunder test")
        , function (std::move (f))
    {
    }

    void run() override { function(); }

private:
    std::function<void()> function;
};
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

TEST_CASE ("Captures asking for the same name at once each get their own file", "[unit][capture]")
{
    TempFolder temp;

    // Claiming creates the `.partial` straight away, so the next claim moves on.
    auto a = claimCaptureFile (temp.folder, "Thunder_y");
    auto b = claimCaptureFile (temp.folder, "Thunder_y");
    REQUIRE (a.stream != nullptr);
    REQUIRE (b.stream != nullptr);
    CHECK (a.finalFile.getFileName() == "Thunder_y.wav");
    CHECK (b.finalFile.getFileName() == "Thunder_y_2.wav");
    CHECK (partialFileFor (a.finalFile).existsAsFile());
    a.stream->writeText ("a", false, false, nullptr);
    b.stream->writeText ("b", false, false, nullptr);
    a.stream.reset();
    b.stream.reset();

    // A file that took the name meanwhile is never replaced.
    REQUIRE (a.finalFile.replaceWithText ("someone else's"));
    const auto finishedA = finishCaptureFile (partialFileFor (a.finalFile), a.finalFile);
    const auto finishedB = finishCaptureFile (partialFileFor (b.finalFile), b.finalFile);
    CHECK (a.finalFile.loadFileAsString() == "someone else's");
    CHECK (finishedA.getFileName() == "Thunder_y_3.wav");
    CHECK (finishedA.loadFileAsString() == "a");
    CHECK (finishedB == b.finalFile);
    CHECK (finishedB.loadFileAsString() == "b");
    CHECK (temp.folder.findChildFiles (juce::File::findFiles, false, "*.partial").isEmpty());

    // Many threads at once, as several instances armed together would, into a
    // folder none of them has created yet.
    const auto newFolder = temp.folder.getChildFile ("New");
    std::vector<std::unique_ptr<juce::Thread>> threads;
    std::vector<juce::File> claimedFiles (8);
    for (std::size_t i = 0; i < claimedFiles.size(); ++i)
        threads.push_back (std::make_unique<FunctionThread> (
            [&newFolder, &claimedFiles, i]
            {
                auto c = claimCaptureFile (newFolder, "Thunder_z");
                if (c.stream != nullptr)
                    claimedFiles[i] = c.finalFile;
            }));
    for (auto& t : threads)
        t->startThread();
    for (auto& t : threads)
        CHECK (t->stopThread (5000));

    juce::StringArray names;
    for (const auto& f : claimedFiles)
        names.addIfNotAlreadyThere (f.getFileName());
    CHECK (names.size() == static_cast<int> (claimedFiles.size()));
    CHECK_FALSE (names.contains ({}));
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

// Plays one 512-sample block of quiet noise through each processor (as a DAW
// does with several tracks), then moves the play head on.
void playBlock (std::initializer_list<ThunderProcessor*> processors, TestPlayHead& head, juce::Random& random,
                double sampleRate)
{
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;

    for (auto* processor : processors)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 512; ++i)
                buffer.setSample (ch, i, 0.1f * (random.nextFloat() - 0.5f));

        processor->processBlock (buffer, midi);
    }

    if (head.playing)
        head.ppq += 512.0 * 2.0 / sampleRate; // 120 bpm: 2 quarter notes a second
}

// Plays `seconds` of quiet noise through the processor, moving the play head.
void playProcessor (ThunderProcessor& processor, TestPlayHead& head, double seconds, double sampleRate = 48000.0)
{
    juce::Random random (1);
    for (int done = 0; done < static_cast<int> (seconds * sampleRate); done += 512)
        playBlock ({&processor}, head, random, sampleRate);
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

TEST_CASE ("A finished capture is ready to drag: closed, complete and movable", "[unit][capture][plugin]")
{
    // What a DAW does with a dropped file: open it, read it to the end, and
    // often copy or move it into the project. The writer must have let go of it.
    constexpr double rate = 44100.0;
    TempFolder temp;
    state::UserSettings settings;
    settings.capturesFolder = temp.folder.getChildFile ("Captures");
    REQUIRE (settings.save (temp.folder.getChildFile ("settings.xml")));

    ThunderProcessor processor;
    processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
    TestPlayHead head;
    processor.setPlayHead (&head);
    processor.prepareToPlay (rate, 512);

    auto session = processor.getSessionState();
    session.captureBars = 1;
    processor.setSessionState (session);

    head.ppq = 3.0;
    processor.armCapture();
    playProcessor (processor, head, 3.0, rate);
    REQUIRE (waitFor ([&] { return processor.getCaptureStatus().phase == CaptureStatus::Phase::Ready; }));

    const auto file = processor.getCaptureStatus().file;
    REQUIRE (file.existsAsFile());
    CHECK (file.hasFileExtension ("wav"));
    CHECK (settings.capturesFolder.findChildFiles (juce::File::findFiles, false, "*.partial").isEmpty());

    {
        const auto reader = openWav (file);
        REQUIRE (reader != nullptr);
        CHECK (reader->bitsPerSample == 24); // the default format
        CHECK (juce::exactlyEqual (reader->sampleRate, rate));
        CHECK (reader->lengthInSamples == 88200); // one 4/4 bar at 120 bpm

        juce::AudioBuffer<float> audio (2, static_cast<int> (reader->lengthInSamples));
        REQUIRE (reader->read (&audio, 0, audio.getNumSamples(), 0, true, true));
        CHECK (audio.getMagnitude (0, audio.getNumSamples() - 4410, 4410) > 0.0f); // audio to the very end
    }

    // Movable (Windows refuses while any handle is open), and back.
    const auto moved = temp.folder.getChildFile ("moved.wav");
    REQUIRE (file.moveFileTo (moved));
    REQUIRE (moved.moveFileTo (file));

    processor.setPlayHead (nullptr);
}

TEST_CASE ("Several instances capturing together each write their own file", "[unit][capture][plugin]")
{
    // A DAW with Thunder on three tracks, all armed for the same bar.
    TempFolder temp;
    state::UserSettings settings;
    settings.capturesFolder = temp.folder.getChildFile ("Captures");
    REQUIRE (settings.save (temp.folder.getChildFile ("settings.xml")));

    TestPlayHead head;
    std::vector<std::unique_ptr<ThunderProcessor>> instances;
    for (int i = 0; i < 3; ++i)
    {
        auto& p = *instances.emplace_back (std::make_unique<ThunderProcessor>());
        p.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
        p.setPlayHead (&head);
        p.prepareToPlay (48000.0, 512);
        auto session = p.getSessionState();
        session.captureBars = 1;
        p.setSessionState (session);
    }

    head.ppq = 3.0;
    for (auto& p : instances)
        p->armCapture();

    juce::Random random (7);
    for (int done = 0; done < static_cast<int> (3.0 * 48000.0); done += 512)
        playBlock ({instances[0].get(), instances[1].get(), instances[2].get()}, head, random, 48000.0);

    const auto allReady = waitFor (
        [&]
        {
            for (auto& p : instances)
                if (p->getCaptureStatus().phase != CaptureStatus::Phase::Ready)
                    return false;
            return true;
        });
    for (auto& p : instances)
    {
        const auto status = p->getCaptureStatus();
        INFO ("phase " << static_cast<int> (status.phase) << ", failure " << static_cast<int> (status.failure) << ": "
                       << status.describe());
        CHECK (status.phase == CaptureStatus::Phase::Ready);
    }
    REQUIRE (allReady);

    juce::StringArray names;
    for (auto& p : instances)
    {
        const auto file = p->getCaptureStatus().file;
        names.addIfNotAlreadyThere (file.getFileName());
        const auto reader = openWav (file);
        REQUIRE (reader != nullptr);
        CHECK (reader->lengthInSamples == 96000);
    }
    CHECK (names.size() == 3);
    CHECK (settings.capturesFolder.findChildFiles (juce::File::findFiles, false, "*.wav").size() == 3);
    CHECK (settings.capturesFolder.findChildFiles (juce::File::findFiles, false, "*.partial").isEmpty());

    for (auto& p : instances)
        p->setPlayHead (nullptr);
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

// ---------------------------------------------------------------------------
// Input captures (FR-INPUT-003)
// ---------------------------------------------------------------------------

TEST_CASE ("An Input capture records the input itself, from the bar line", "[unit][capture][source]")
{
    // At High quality the layer is 40 ms late; the input isn't, and Input Gain
    // doesn't touch it.
    auto engine = std::make_unique<ThunderEngine>();
    engine->prepare (48000.0, params::Quality::High);
    engine->getCapture().allocateQueue();
    REQUIRE (engine->getLatencySamples() > 1000);

    auto snapshot = ParamSnapshot::defaults();
    snapshot.set (ParamId::inputGain, 6.0f);

    FakeHost host;
    host.ppq = 1.0;
    const auto barAt = host.samplesUntilBar (1);
    const auto bar = static_cast<std::size_t> (std::llround (host.samplesPerBar()));
    auto input = test::impulse (barAt + bar + kMargin, barAt, 0.5f);
    input[barAt + bar - 1] = 0.25f; // the bar's last sample

    engine->getCapture().arm (1, CaptureSource::Input, true, true); // tail and loop don't apply
    Drained d;
    play (*engine, host, input, 512, snapshot, &d);

    REQUIRE (d.completed());
    CHECK (d.start.source == CaptureSource::Input);
    CHECK_FALSE (d.start.tail);
    CHECK_FALSE (d.start.loop);
    REQUIRE (d.left.size() == bar);
    CHECK (juce::exactlyEqual (d.left[0], 0.5f));
    CHECK (juce::exactlyEqual (d.right[0], 0.5f));
    CHECK (juce::exactlyEqual (d.left[bar - 1], 0.25f));
    CHECK (test::energy (d.left, 1, bar - 1) < 1.0e-12);
}

TEST_CASE ("Input capture files are named as input", "[unit][capture][source]")
{
    CaptureStart start;
    start.bpm = 90.0;
    start.bars = 2;
    start.source = CaptureSource::Input;
    const auto name = buildCaptureName (start, "Night Static", juce::Time (2026, 10, 23, 12, 0, 0));
    CHECK (name.startsWith ("Thunder_Input_C-maj_90bpm_2bars_"));
}

// ---------------------------------------------------------------------------
// Include Tail and Loop (FR-CAPT-004)
// ---------------------------------------------------------------------------

namespace
{
// A sine from `from` for `length` samples, silence elsewhere.
std::vector<float> burst (std::size_t total, std::size_t from, std::size_t length, float amplitude = 0.3f)
{
    std::vector<float> s (total, 0.0f);
    for (std::size_t i = from; i < std::min (total, from + length); ++i)
        s[i] =
            amplitude * static_cast<float> (std::sin (test::kTwoPi * 220.0 * static_cast<double> (i - from) / 48000.0));
    return s;
}
} // namespace

TEST_CASE ("Include Tail records on until the sound has died away", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    host.ppq = 2.0;
    const auto barAt = host.samplesUntilBar (1);
    const auto bar = static_cast<std::size_t> (std::llround (host.samplesPerBar()));

    // Sound through the bar and 0.6 s past it.
    const auto ringing = static_cast<std::size_t> (0.6 * 48000.0);
    const auto input = burst (barAt + bar + ringing + 48000 + kMargin, barAt, bar + ringing);

    engine->getCapture().arm (1, CaptureSource::FullMix, true, false);
    Drained d;
    play (*engine, host, input, 512, passThrough(), &d);

    REQUIRE (d.completed());
    CHECK (d.start.tail);
    // N bars, the ringing, then a quarter of a second of quiet.
    const auto expected = bar + ringing + static_cast<std::size_t> (CaptureController::kTailQuietSeconds * 48000.0);
    CHECK (d.left.size() >= expected - 64);
    CHECK (d.left.size() <= expected + 64);
    CHECK (engine->getCapture().getPhase() == CaptureController::Phase::Idle);
}

TEST_CASE ("The tail is at most 10 s", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    host.ppq = 0.0;
    const auto bar = static_cast<std::size_t> (std::llround (host.samplesPerBar()));
    const auto input = burst (bar + 12 * 48000 + kMargin, 0, bar + 12 * 48000);

    engine->getCapture().arm (1, CaptureSource::FullMix, true, false);
    Drained d;
    play (*engine, host, input, 512, passThrough(), &d);
    REQUIRE (d.completed());
    CHECK (d.left.size() == bar + static_cast<std::size_t> (CaptureController::kMaxTailSeconds * 48000.0));
}

TEST_CASE ("Stopping playback or pressing Stop during the tail keeps the capture", "[unit][capture]")
{
    for (const auto viaStop : {false, true})
    {
        INFO ((viaStop ? "Stop pressed" : "transport stopped"));
        auto engine = preparedEngine (48000.0);
        FakeHost host;
        host.ppq = 0.0;
        const auto bar = static_cast<std::size_t> (std::llround (host.samplesPerBar()));
        const auto input = burst (bar + 12 * 48000, 0, bar + 12 * 48000);

        engine->getCapture().arm (1, CaptureSource::FullMix, true, false);
        Drained d;
        const auto stopAt = bar + 48000; // a second into the tail
        play (*engine, host, input, 512, passThrough(), &d,
              [&] (std::size_t start)
              {
                  if (start >= stopAt && start < stopAt + 512)
                  {
                      if (viaStop)
                          engine->getCapture().cancel();
                      else
                          host.playing = false;
                  }
              });

        REQUIRE (d.completed());
        CHECK (d.left.size() > bar);
        if (viaStop)
            CHECK (d.left.size() < bar + 2 * 48000);
        else
            CHECK (d.left.size() == bar + static_cast<std::size_t> (CaptureController::kMaxTailSeconds * 48000.0));
    }
}

TEST_CASE ("Stopping before the last bar still fails a capture with a tail", "[unit][capture]")
{
    auto engine = preparedEngine (48000.0);
    FakeHost host;
    host.ppq = 0.0;
    const auto input = burst (4 * 48000, 0, 4 * 48000);
    engine->getCapture().arm (2, CaptureSource::FullMix, true, true);
    Drained d;
    play (*engine, host, input, 512, passThrough(), &d,
          [&] (std::size_t start)
          {
              if (start >= 48000)
                  host.playing = false;
          });
    REQUIRE (!d.kinds.empty());
    CHECK (d.kinds.back() == CaptureBlock::Kind::Failed);
    CHECK (d.failure == CaptureFailure::TransportStopped);
}

TEST_CASE ("Loop adds a tail onto the start", "[unit][capture]")
{
    juce::AudioBuffer<float> head (2, 48000);
    juce::AudioBuffer<float> tail (2, 9600);
    head.clear();
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 9600; ++i)
            tail.setSample (
                ch, i, 0.4f * std::exp (-static_cast<float> (i) / 2000.0f) * static_cast<float> (std::sin (0.05 * i)));
    // The start of the loop has its own, different sound.
    for (int i = 0; i < 48000; ++i)
        head.setSample (0, i, 0.2f * static_cast<float> (std::sin (0.013 * i)));
    const auto original = head;

    CHECK (foldTail (head, 96000, tail, 48000.0) == FoldKind::added);
    for (const auto i : {0, 100, 5000})
        CHECK_THAT (head.getSample (0, i), WithinAbs (original.getSample (0, i) + tail.getSample (0, i), 1.0e-6));
    CHECK_THAT (head.getSample (1, 9599), WithinAbs (0.0, 1.0e-3)); // faded out
    CHECK (juce::exactlyEqual (head.getSample (0, 20000), original.getSample (0, 20000)));
}

TEST_CASE ("Loop wraps a tail longer than the loop", "[unit][capture]")
{
    juce::AudioBuffer<float> head (1, 1000);
    juce::AudioBuffer<float> tail (1, 2500);
    head.clear();
    for (int i = 0; i < 2500; ++i)
        tail.setSample (0, i, 0.001f * static_cast<float> (i % 7 + 1));

    CHECK (foldTail (head, 1000, tail, 48000.0) == FoldKind::added);
    // Sample 10 gets the tail at 10, 1010 and 2010.
    CHECK_THAT (head.getSample (0, 10),
                WithinAbs (tail.getSample (0, 10) + tail.getSample (0, 1010) + tail.getSample (0, 2010), 1.0e-6));
}

TEST_CASE ("Loop replaces the start with a replay instead of doubling it", "[unit][capture]")
{
    // The DAW looped back: what followed the last bar is the start again, now
    // with the end's ring in it.
    juce::AudioBuffer<float> head (2, 48000);
    juce::AudioBuffer<float> tail (2, 24000);
    for (int ch = 0; ch < 2; ++ch)
    {
        for (int i = 0; i < 48000; ++i)
            head.setSample (ch, i, 0.3f * static_cast<float> (std::sin (0.031 * i)));
        for (int i = 0; i < 24000; ++i)
            tail.setSample (ch, i, head.getSample (ch, i) + 0.05f * std::exp (-static_cast<float> (i) / 4000.0f));
    }
    const auto original = head;

    CHECK (foldTail (head, 96000, tail, 48000.0) == FoldKind::replaced);
    CHECK_THAT (head.getSample (0, 100), WithinAbs (tail.getSample (0, 100), 1.0e-6));
    CHECK (head.getMagnitude (0, 0, 24000) < 0.36f); // not doubled
    CHECK (juce::exactlyEqual (head.getSample (0, 30000), original.getSample (0, 30000)));
}

TEST_CASE ("A Loop capture is exactly N bars with the tail folded in", "[unit][capture]")
{
    for (const auto format : {CaptureFormat::Int24, CaptureFormat::Float32})
    {
        TempFolder temp;
        auto engine = preparedEngine (48000.0);
        CaptureWriter writer (engine->getCapture());
        writer.setSettings ({temp.folder, format, {}});

        FakeHost host;
        host.ppq = 0.0;
        const auto bar = static_cast<std::size_t> (std::llround (host.samplesPerBar()));
        // Silence, then a sound that starts half a second before the bar ends
        // and rings 0.3 s past it.
        const auto input = burst (bar + 48000 + kMargin, bar - 24000, 24000 + 14400);

        engine->getCapture().arm (1, CaptureSource::FullMix, true, true);
        play (*engine, host, input, 512, passThrough(), nullptr,
              [&writer] (std::size_t start)
              {
                  if (start % 8192 == 0)
                      writer.service();
              });
        writer.service();

        REQUIRE (writer.getState() == CaptureWriter::State::Ready);
        const auto reader = openWav (writer.getLastFile());
        REQUIRE (reader != nullptr);
        CHECK (reader->lengthInSamples == static_cast<juce::int64> (bar));

        juce::AudioBuffer<float> audio (2, static_cast<int> (bar));
        reader->read (&audio, 0, static_cast<int> (bar), 0, true, true);
        CHECK (audio.getMagnitude (0, 0, 14000) > 0.25f); // the ring, folded onto the silent start
        CHECK (audio.getMagnitude (0, 20000, 20000) < 1.0e-4f);
        CHECK (temp.folder.findChildFiles (juce::File::findFiles, false, "*.fold").isEmpty());
    }
}

TEST_CASE ("A finished Input capture becomes the source", "[unit][capture][source][plugin]")
{
    TempFolder temp;
    state::UserSettings settings;
    settings.capturesFolder = temp.folder.getChildFile ("Captures");
    REQUIRE (settings.save (temp.folder.getChildFile ("settings.xml")));

    ThunderProcessor processor;
    processor.setSettingsFile (temp.folder.getChildFile ("settings.xml"));
    TestPlayHead head;
    processor.setPlayHead (&head);
    processor.prepareToPlay (48000.0, 512);

    auto session = processor.getSessionState();
    session.captureBars = 2;
    session.captureSource = state::CaptureSource::Input;
    processor.setSessionState (session);

    head.ppq = 2.0;
    processor.armCapture();
    playProcessor (processor, head, 5.0); // to bar 2, then two bars

    REQUIRE (waitFor ([&] { return processor.getCaptureStatus().phase == CaptureStatus::Phase::Ready; }));
    const auto file = processor.getCaptureStatus().file;
    CHECK (file.getFileName().startsWith ("Thunder_Input_"));

    using analysis::AnalysisWorker;
    REQUIRE (waitFor (
        [&] { return processor.getAnalysisWorker().getSourceStatus().phase == AnalysisWorker::SourcePhase::Ready; }));
    const auto status = processor.getAnalysisWorker().getSourceStatus();
    CHECK (status.buffer->fromCapture);
    CHECK (status.buffer->bars == 2);
    CHECK (juce::exactlyEqual (status.buffer->bpm, 120.0));
    CHECK (status.buffer->audio.getNumSamples() == 2 * 96000);
    CHECK (processor.getAnalysisWorker().getSourceRef().path == file.getFullPathName());

    processor.setPlayHead (nullptr);
}
