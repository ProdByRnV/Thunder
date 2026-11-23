// Sources and key detection in the plugin (PRD FR-INPUT-001…005, FR-KEY-002/003;
// ROADMAP T-0.9-01…04): files load in every supported format off the audio
// thread, bad files are refused with a reason, the source is saved with the
// project (path and a copy) and comes back even when its file has gone, and
// the live input's key is detected but never applied by itself.

#include "support/TestHost.h"
#include "support/TestSignals.h"

#include "core/analysis/AnalysisWorker.h"
#include "core/engine/ThunderEngine.h"
#include "core/source/SourceLoader.h"
#include "core/source/SourceRef.h"
#include "core/state/StateIds.h"
#include "plugin/ThunderProcessor.h"
#include "plugin/ui/ParamUi.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <functional>

using namespace thunder;
using analysis::AnalysisWorker;
using Catch::Matchers::WithinAbs;
using params::KeyRoot;
using params::ParamId;
using params::Scale;
using source::LoadError;

namespace
{
struct TempFolder
{
    juce::File folder = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("ThunderSourceTests-" + juce::Uuid().toString());

    TempFolder() { folder.createDirectory(); }
    ~TempFolder() { folder.deleteRecursively(); }
};

// A short D minor tune (D4 = 62) as stereo audio.
juce::AudioBuffer<float> dMinorTune (double sampleRate, double seconds)
{
    constexpr double notes[] = {62, 65, 69, 67, 65, 64, 62, 57, 62, 64, 65, 67, 69, 70, 69, 62};
    juce::AudioBuffer<float> buffer (2, static_cast<int> (seconds * sampleRate));
    buffer.clear();
    const auto noteLength = static_cast<int> (0.4 * sampleRate);
    for (int start = 0, n = 0; start < buffer.getNumSamples(); start += noteLength, ++n)
    {
        const auto tone = test::steadyTone (notes[n % 16], sampleRate, 0.4, 5, 0.3f);
        for (int i = 0; i < noteLength && start + i < buffer.getNumSamples(); ++i)
        {
            const auto env = std::min (1.0f, static_cast<float> (std::min (i, noteLength - i)) / 400.0f);
            buffer.setSample (0, start + i, tone[static_cast<std::size_t> (i)] * env);
            buffer.setSample (1, start + i, 0.8f * tone[static_cast<std::size_t> (i)] * env);
        }
    }
    return buffer;
}

bool writeFile (juce::AudioFormat& format, const juce::File& file, const juce::AudioBuffer<float>& audio,
                double sampleRate, int bits = 16)
{
    std::unique_ptr<juce::OutputStream> stream = file.createOutputStream();
    if (stream == nullptr)
        return false;
    auto options = juce::AudioFormatWriterOptions{}
                       .withSampleRate (sampleRate)
                       .withNumChannels (audio.getNumChannels())
                       .withBitsPerSample (bits);
    if (dynamic_cast<juce::OggVorbisAudioFormat*> (&format) != nullptr)
        options = options.withQualityOptionIndex (8);
    auto writer = format.createWriterFor (stream, options);
    return writer != nullptr && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples());
}

bool waitFor (const std::function<bool()>& condition)
{
    for (int i = 0; i < 1000 && !condition(); ++i)
        juce::Thread::sleep (10);
    return condition();
}

// Runs the worker's pending request synchronously.
AnalysisWorker::SourceStatus serviced (AnalysisWorker& worker)
{
    worker.service();
    return worker.getSourceStatus();
}
} // namespace

// ---------------------------------------------------------------------------
// Loading files (FR-INPUT-001)
// ---------------------------------------------------------------------------

TEST_CASE ("Sources load from WAV, AIFF, FLAC and OGG files", "[unit][source]")
{
    TempFolder temp;
    const auto audio = dMinorTune (44100.0, 3.0);

    juce::WavAudioFormat wav;
    juce::AiffAudioFormat aiff;
    juce::FlacAudioFormat flac;
    juce::OggVorbisAudioFormat ogg;
    struct Case
    {
        juce::AudioFormat* format;
        const char* name;
        float tolerance;
    };
    for (const auto& c : {Case{&wav, "tune.wav", 1.0e-4f}, Case{&aiff, "tune.aiff", 1.0e-4f},
                          Case{&flac, "tune.flac", 1.0e-4f}, Case{&ogg, "tune.ogg", 0.05f}})
    {
        INFO (c.name);
        const auto file = temp.folder.getChildFile (c.name);
        REQUIRE (writeFile (*c.format, file, audio, 44100.0));

        CHECK (source::isSupportedFile (file));
        const auto result = source::loadFile (file);
        REQUIRE (result.error == LoadError::None);
        REQUIRE (result.buffer != nullptr);
        const auto& s = *result.buffer;
        CHECK (juce::exactlyEqual (s.sampleRate, 44100.0));
        CHECK (s.audio.getNumChannels() == 2);
        CHECK (std::abs (s.audio.getNumSamples() - audio.getNumSamples()) <= 2048); // Vorbis may pad
        CHECK (s.name == c.name);
        CHECK (s.file == file);

        // The same audio (lossless formats within 16-bit rounding).
        auto worst = 0.0f;
        for (int i = 2000; i < 40000; ++i)
            worst = std::max (worst, std::abs (s.audio.getSample (1, i) - audio.getSample (1, i)));
        CHECK (worst < c.tolerance);
    }
}

TEST_CASE ("Sources load from MP3 files", "[unit][source]")
{
    const auto file = juce::File (THUNDER_TEST_FIXTURES).getChildFile ("audio/tone-440hz-2s.mp3");
    REQUIRE (file.existsAsFile());

    const auto result = source::loadFile (file);
    REQUIRE (result.error == LoadError::None);
    const auto& s = *result.buffer;
    CHECK (juce::exactlyEqual (s.sampleRate, 44100.0));
    CHECK (s.audio.getNumChannels() == 2);
    CHECK (s.getSeconds() > 1.9);
    CHECK (s.getSeconds() < 2.2);

    // A 440 Hz tone: about 880 zero crossings a second, and loud.
    std::vector<float> left (s.audio.getReadPointer (0) + 4410, s.audio.getReadPointer (0) + 4410 + 44100);
    CHECK (std::abs (test::zeroCrossingFrequency (left, 0, left.size(), 44100.0) - 440.0) < 2.0);
    CHECK (s.audio.getMagnitude (0, 4410, 44100) > 0.05f); // the fixture is at -18 dBFS
}

TEST_CASE ("Files that can't be sources are refused with a reason", "[unit][source]")
{
    TempFolder temp;

    SECTION ("Missing")
    {
        CHECK (source::loadFile (temp.folder.getChildFile ("gone.wav")).error == LoadError::NotFound);
    }
    SECTION ("Not an audio file")
    {
        const auto text = temp.folder.getChildFile ("notes.txt");
        REQUIRE (text.replaceWithText ("hello"));
        CHECK_FALSE (source::isSupportedFile (text));
        CHECK (source::loadFile (text).error == LoadError::NotSupported);
    }
    SECTION ("Damaged")
    {
        const auto bad = temp.folder.getChildFile ("bad.wav");
        REQUIRE (bad.replaceWithText ("RIFF this is not really a wave file at all"));
        CHECK (source::loadFile (bad).error == LoadError::Unreadable);
    }
    SECTION ("Too long")
    {
        // 301 s at 1 kHz: small on disk, longer than Thunder loads.
        juce::AudioBuffer<float> audio (1, 301000);
        audio.clear();
        const auto file = temp.folder.getChildFile ("long.wav");
        juce::WavAudioFormat wav;
        REQUIRE (writeFile (wav, file, audio, 1000.0));
        CHECK (source::loadFile (file).error == LoadError::TooLong);
    }
    SECTION ("Empty")
    {
        juce::AudioBuffer<float> audio (2, 0);
        const auto file = temp.folder.getChildFile ("empty.wav");
        juce::WavAudioFormat wav;
        REQUIRE (writeFile (wav, file, audio, 48000.0));
        CHECK (source::loadFile (file).error == LoadError::Empty);
    }

    for (const auto error :
         {LoadError::NotFound, LoadError::NotSupported, LoadError::Unreadable, LoadError::TooLong, LoadError::Empty})
        CHECK (source::describe (error).isNotEmpty());
}

TEST_CASE ("A loaded source knows its key and its shape", "[unit][source][key]")
{
    auto buffer = std::make_shared<source::SourceBuffer>();
    buffer->audio = dMinorTune (48000.0, 12.0);
    buffer->sampleRate = 48000.0;
    source::finishSource (*buffer);

    CHECK (buffer->key.valid);
    CHECK (buffer->key.root == KeyRoot::D);
    CHECK (buffer->key.scale == Scale::NaturalMinor);
    REQUIRE (buffer->peaks.size() == static_cast<std::size_t> (source::SourceBuffer::kPeakCount));
    CHECK (*std::max_element (buffer->peaks.begin(), buffer->peaks.end()) > 0.2f);
}

// ---------------------------------------------------------------------------
// The copy saved with the project (FR-INPUT-004, OQ-05)
// ---------------------------------------------------------------------------

TEST_CASE ("A short source is copied into the project, a long one isn't", "[unit][source][state]")
{
    source::SourceBuffer shortSource;
    shortSource.audio = dMinorTune (48000.0, 6.0);
    shortSource.sampleRate = 48000.0;

    const auto copy = source::encodeEmbedded (shortSource);
    REQUIRE (copy != nullptr);
    CHECK (copy->getSize() <= source::kMaxEmbeddedBytes);

    const auto restored = source::loadEmbedded (*copy);
    REQUIRE (restored.buffer != nullptr);
    CHECK (juce::exactlyEqual (restored.buffer->sampleRate, 48000.0));
    CHECK (restored.buffer->audio.getNumSamples() == shortSource.audio.getNumSamples());
    auto worst = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < shortSource.audio.getNumSamples(); ++i)
            worst = std::max (
                worst, std::abs (restored.buffer->audio.getSample (ch, i) - shortSource.audio.getSample (ch, i)));
    CHECK (worst < 1.0e-4f); // 16-bit

    // A minute of stereo noise compresses badly: path only.
    source::SourceBuffer longSource;
    longSource.sampleRate = 48000.0;
    longSource.audio.setSize (2, 48000 * 60);
    juce::Random random (3);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < longSource.audio.getNumSamples(); ++i)
            longSource.audio.setSample (ch, i, random.nextFloat() - 0.5f);
    CHECK (source::encodeEmbedded (longSource) == nullptr);
}

TEST_CASE ("The project's source reference round-trips and tolerates damage", "[unit][source][state]")
{
    source::SourceRef ref;
    ref.path = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("loop.wav").getFullPathName();
    ref.fromCapture = true;
    ref.bpm = 92.5;
    ref.bars = 4;
    ref.timeSigNumerator = 3;
    ref.embedded = std::make_shared<juce::MemoryBlock> ("fLaC and some bytes", 19);

    const auto xml = ref.toValueTree().createXml();
    REQUIRE (xml != nullptr);
    const auto back = source::SourceRef::fromValueTree (juce::ValueTree::fromXml (*xml));
    CHECK (back.sameSourceAs (ref));
    CHECK (back.path == ref.path);
    CHECK (back.fromCapture);
    CHECK (juce::exactlyEqual (back.bpm, 92.5));
    CHECK (back.bars == 4);
    CHECK (back.timeSigNumerator == 3);
    REQUIRE (back.embedded != nullptr);
    CHECK (*back.embedded == *ref.embedded);

    CHECK_FALSE (source::SourceRef{}.toValueTree().isValid());
    CHECK (source::SourceRef::fromValueTree ({}).isEmpty());

    juce::ValueTree damaged{state::ids::source};
    damaged.setProperty (state::ids::sourcePath, "x.wav", nullptr);
    damaged.setProperty (state::ids::sourceGridOffset, -50, nullptr);
    damaged.setProperty (state::ids::sourceBpm, "fast", nullptr);
    damaged.setProperty (state::ids::sourceEmbedded, "@@@ not base64 @@@", nullptr);
    const auto tolerant = source::SourceRef::fromValueTree (damaged);
    CHECK (tolerant.path == "x.wav");
    CHECK (tolerant.gridOffsetSamples == 0);
    CHECK (juce::exactlyEqual (tolerant.bpm, 0.0));
}

// ---------------------------------------------------------------------------
// The worker (FR-INPUT-002/004/005)
// ---------------------------------------------------------------------------

TEST_CASE ("The worker loads a dropped file as the source", "[unit][source]")
{
    TempFolder temp;
    const auto file = temp.folder.getChildFile ("melody.wav");
    juce::WavAudioFormat wav;
    REQUIRE (writeFile (wav, file, dMinorTune (48000.0, 10.0), 48000.0, 24));

    analysis::AnalysisTap tap;
    AnalysisWorker worker (tap);
    worker.loadFile (file);

    // Until the thread gets to it, the UI shows it loading.
    CHECK (worker.getSourceStatus().phase == AnalysisWorker::SourcePhase::Loading);
    CHECK (worker.getSourceStatus().name == "melody.wav");

    const auto status = serviced (worker);
    REQUIRE (status.phase == AnalysisWorker::SourcePhase::Ready);
    REQUIRE (status.buffer != nullptr);
    CHECK (status.buffer->key.root == KeyRoot::D);
    CHECK (status.buffer->key.scale == Scale::NaturalMinor);
    CHECK_FALSE (status.buffer->fromCapture);
    CHECK (juce::exactlyEqual (status.progress, 1.0f));

    const auto ref = worker.getSourceRef();
    CHECK (ref.path == file.getFullPathName());
    CHECK (ref.embedded != nullptr); // 10 s fits

    worker.clearSource();
    CHECK (worker.getSourceStatus().phase == AnalysisWorker::SourcePhase::None);
    CHECK (worker.getSourceRef().isEmpty());
}

TEST_CASE ("A newer source request replaces an older one", "[unit][source]")
{
    TempFolder temp;
    juce::WavAudioFormat wav;
    const auto first = temp.folder.getChildFile ("first.wav");
    const auto second = temp.folder.getChildFile ("second.wav");
    REQUIRE (writeFile (wav, first, dMinorTune (48000.0, 4.0), 48000.0));
    REQUIRE (writeFile (wav, second, dMinorTune (48000.0, 4.0), 48000.0));

    analysis::AnalysisTap tap;
    AnalysisWorker worker (tap);
    worker.loadFile (first);
    worker.loadFile (second);
    const auto status = serviced (worker);
    REQUIRE (status.phase == AnalysisWorker::SourcePhase::Ready);
    CHECK (status.name == "second.wav");
    CHECK (worker.getSourceRef().path == second.getFullPathName());
}

TEST_CASE ("A bad dropped file is reported and nothing else changes", "[unit][source]")
{
    TempFolder temp;
    const auto bad = temp.folder.getChildFile ("bad.flac");
    REQUIRE (bad.replaceWithText ("not flac"));

    analysis::AnalysisTap tap;
    AnalysisWorker worker (tap);
    worker.loadFile (bad);
    const auto status = serviced (worker);
    CHECK (status.phase == AnalysisWorker::SourcePhase::Failed);
    CHECK (status.error == LoadError::Unreadable);
    CHECK (status.buffer == nullptr);
}

TEST_CASE ("A project's source comes back from its file, or from its copy when the file is gone",
           "[unit][source][state]")
{
    TempFolder temp;
    const auto file = temp.folder.getChildFile ("loop.wav");
    juce::WavAudioFormat wav;
    REQUIRE (writeFile (wav, file, dMinorTune (48000.0, 6.0), 48000.0));

    analysis::AnalysisTap tap;
    AnalysisWorker original (tap);
    original.loadFile (file);
    REQUIRE (serviced (original).phase == AnalysisWorker::SourcePhase::Ready);
    const auto saved = source::SourceRef::fromValueTree (original.getSourceRef().toValueTree());
    REQUIRE (saved.embedded != nullptr);

    SECTION ("The file is there")
    {
        AnalysisWorker reopened (tap);
        reopened.restore (saved);
        const auto status = serviced (reopened);
        REQUIRE (status.phase == AnalysisWorker::SourcePhase::Ready);
        CHECK_FALSE (status.usedSavedCopy);
        CHECK (status.name == "loop.wav");

        // Hosts restore state more than once: the same source isn't loaded again.
        const auto version = reopened.getSourceStatus().version;
        reopened.restore (saved);
        CHECK (reopened.getSourceStatus().version == version);
    }
    SECTION ("The file has gone: the copy loads, and the project keeps its path")
    {
        REQUIRE (file.deleteFile());
        AnalysisWorker reopened (tap);
        reopened.restore (saved);
        const auto status = serviced (reopened);
        REQUIRE (status.phase == AnalysisWorker::SourcePhase::Ready);
        CHECK (status.usedSavedCopy);
        CHECK (status.name == "loop.wav");
        CHECK (status.buffer->key.root == KeyRoot::D);
        CHECK (reopened.getSourceRef().path == file.getFullPathName());
        CHECK (reopened.getSourceRef().embedded != nullptr);
    }
    SECTION ("The file has gone and there is no copy: reported, the path kept, no crash")
    {
        REQUIRE (file.deleteFile());
        auto pathOnly = saved;
        pathOnly.embedded = nullptr;
        AnalysisWorker reopened (tap);
        reopened.restore (pathOnly);
        const auto status = serviced (reopened);
        CHECK (status.phase == AnalysisWorker::SourcePhase::Failed);
        CHECK (status.error == LoadError::NotFound);
        CHECK (status.name == "loop.wav");
        CHECK (reopened.getSourceRef().path == file.getFullPathName()); // saved again as it was
    }
    SECTION ("A session without a source clears it")
    {
        AnalysisWorker reopened (tap);
        reopened.restore (saved);
        REQUIRE (serviced (reopened).phase == AnalysisWorker::SourcePhase::Ready);
        reopened.restore ({});
        CHECK (reopened.getSourceStatus().phase == AnalysisWorker::SourcePhase::None);
        CHECK (reopened.getSourceRef().isEmpty());
    }
}

// ---------------------------------------------------------------------------
// Live key detection (FR-KEY-002/003)
// ---------------------------------------------------------------------------

TEST_CASE ("The input's key is detected live while Auto Detect is on", "[unit][key]")
{
    ThunderEngine engine;
    engine.prepare (48000.0);
    AnalysisWorker worker (engine.getAnalysisTap());
    engine.getAnalysisTap().allocate(); // what start() does, without the thread
    worker.setLiveDetection (true);

    const auto tune = dMinorTune (48000.0, 12.0);
    juce::AudioBuffer<float> block (2, 512);
    const auto snapshot = params::ParamSnapshot::defaults();
    for (int start = 0; start + 512 <= tune.getNumSamples(); start += 512)
    {
        block.copyFrom (0, 0, tune, 0, start, 512);
        block.copyFrom (1, 0, tune, 1, start, 512);
        engine.process (block.getArrayOfWritePointers(), 512, {}, snapshot);
        if ((start / 512) % 16 == 0)
            worker.service();
    }
    worker.service();

    const auto e = worker.getLiveEstimate();
    REQUIRE (e.valid);
    CHECK (e.root == KeyRoot::D);
    CHECK (e.scale == Scale::NaturalMinor);
    CHECK (e.confidence >= 0.7f);

    // Off: nothing is fed, and what was heard is forgotten.
    worker.setLiveDetection (false);
    CHECK_FALSE (engine.getAnalysisTap().isEnabled());
    CHECK_FALSE (worker.getLiveEstimate().valid);
}

TEST_CASE ("Detection never changes the key by itself", "[unit][key][plugin]")
{
    ThunderProcessor processor;
    test::TestPlayHead head;
    processor.setPlayHead (&head);
    processor.prepareToPlay (48000.0, 512);
    REQUIRE (processor.getSessionState().keyAutoDetect); // on by default (PRD §9.2)

    auto& apvts = processor.getValueTreeState();
    auto* root = apvts.getParameter (ui::paramIdString (ParamId::keyRoot));
    auto* scale = apvts.getParameter (ui::paramIdString (ParamId::keyScale));
    const auto rootBefore = root->getValue();
    const auto scaleBefore = scale->getValue();

    // The test melody (C E♭ G B♭) for a while: a key is found, the parameters stay.
    test::playMelody (processor, head, 10.0, 48000.0);
    REQUIRE (waitFor ([&] { return processor.getAnalysisWorker().getLiveEstimate().valid; }));
    CHECK (juce::exactlyEqual (root->getValue(), rootBefore));
    CHECK (juce::exactlyEqual (scale->getValue(), scaleBefore));

    // Auto Detect off for this project: saved with it, and detection stops.
    auto session = processor.getSessionState();
    session.keyAutoDetect = false;
    processor.setSessionState (session);
    CHECK_FALSE (processor.getAnalysisWorker().isLiveDetectionOn());

    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    ThunderProcessor reopened;
    reopened.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
    CHECK_FALSE (reopened.getSessionState().keyAutoDetect);
    CHECK_FALSE (reopened.getAnalysisWorker().isLiveDetectionOn());

    processor.setPlayHead (nullptr);
}

TEST_CASE ("A dropped source is saved with the project and comes back", "[unit][source][state][plugin]")
{
    TempFolder temp;
    const auto file = temp.folder.getChildFile ("hook.flac");
    juce::FlacAudioFormat flac;
    REQUIRE (writeFile (flac, file, dMinorTune (48000.0, 8.0), 48000.0));

    juce::MemoryBlock saved;
    {
        ThunderProcessor processor;
        processor.loadSourceFile (file);
        REQUIRE (waitFor (
            [&]
            { return processor.getAnalysisWorker().getSourceStatus().phase == AnalysisWorker::SourcePhase::Ready; }));
        processor.getStateInformation (saved);
    }

    const auto reopen = [&saved]
    {
        auto processor = std::make_unique<ThunderProcessor>();
        processor->setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));
        waitFor (
            [&]
            { return processor->getAnalysisWorker().getSourceStatus().phase != AnalysisWorker::SourcePhase::Loading; });
        return processor;
    };

    {
        const auto processor = reopen();
        const auto status = processor->getAnalysisWorker().getSourceStatus();
        REQUIRE (status.phase == AnalysisWorker::SourcePhase::Ready);
        CHECK (status.name == "hook.flac");
        CHECK_FALSE (status.usedSavedCopy);
        CHECK (status.buffer->key.root == KeyRoot::D);
    }

    REQUIRE (file.deleteFile());
    {
        const auto processor = reopen();
        const auto status = processor->getAnalysisWorker().getSourceStatus();
        REQUIRE (status.phase == AnalysisWorker::SourcePhase::Ready);
        CHECK (status.usedSavedCopy);
        CHECK (status.name == "hook.flac");
    }
}
