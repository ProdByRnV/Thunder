// Key detection (PRD FR-KEY-002/003; ROADMAP T-0.9-01).

#include "support/TestSignals.h"

#include "core/analysis/KeyDetector.h"
#include "core/music/Scale.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cstdio>

using namespace thunder;
using namespace thunder::test;
using analysis::KeyDetector;
using analysis::KeyEstimate;
using params::KeyRoot;
using params::Scale;

namespace
{
struct Note
{
    double note;  // note number (60 = middle C); < 0 = rest
    double beats; // length at 120 BPM (a beat = 0.5 s)
};

// Notes played one after another as harmonic tones with a short attack and
// release, repeated to fill `seconds`.
std::vector<float> melody (const std::vector<Note>& notes, double sampleRate, double seconds, double transpose = 0.0,
                           int harmonics = 6)
{
    std::vector<float> out (static_cast<std::size_t> (sampleRate * seconds), 0.0f);
    std::size_t pos = 0;
    while (pos < out.size())
    {
        for (const auto& n : notes)
        {
            const auto length = static_cast<std::size_t> (n.beats * 0.5 * sampleRate);
            if (n.note >= 0.0)
            {
                const auto voice = steadyTone (n.note + transpose, sampleRate, n.beats * 0.5, harmonics, 0.3f);
                const auto ramp = static_cast<std::size_t> (0.01 * sampleRate);
                for (std::size_t i = 0; i < voice.size() && pos + i < out.size(); ++i)
                {
                    const auto in = std::min (1.0, static_cast<double> (i) / static_cast<double> (ramp));
                    const auto outRamp =
                        std::min (1.0, static_cast<double> (voice.size() - i) / static_cast<double> (ramp));
                    out[pos + i] += voice[i] * static_cast<float> (in * outRamp);
                }
            }
            pos += length;
            if (pos >= out.size())
                break;
        }
    }
    return out;
}

// Triads with a bass note, one per `beats`.
std::vector<float> progression (const std::vector<std::vector<double>>& chords, double beats, double sampleRate,
                                double seconds, double transpose = 0.0)
{
    std::vector<float> out (static_cast<std::size_t> (sampleRate * seconds), 0.0f);
    const auto length = static_cast<std::size_t> (beats * 0.5 * sampleRate);
    std::size_t pos = 0;
    for (std::size_t c = 0; pos < out.size(); ++c, pos += length)
    {
        for (const auto note : chords[c % chords.size()])
        {
            const auto voice = steadyTone (note + transpose, sampleRate, beats * 0.5, 5, 0.15f);
            for (std::size_t i = 0; i < voice.size() && pos + i < out.size(); ++i)
                out[pos + i] += voice[i];
        }
    }
    return out;
}

KeyEstimate analyse (const std::vector<float>& mono, double sampleRate)
{
    juce::AudioBuffer<float> buffer (1, static_cast<int> (mono.size()));
    buffer.copyFrom (0, 0, mono.data(), static_cast<int> (mono.size()));
    return KeyDetector::analyse (buffer, sampleRate);
}

// A minor tune that leans on its tonic (A4 = 69).
const std::vector<Note> kMinorTune{{69, 1},   {72, 1},   {76, 1},   {74, 0.5}, {72, 0.5}, {71, 1}, {69, 1},
                                   {64, 1},   {69, 0.5}, {71, 0.5}, {72, 1},   {74, 1},   {76, 2}, {77, 0.5},
                                   {76, 0.5}, {74, 1},   {72, 1},   {71, 1},   {69, 2}};

// A major tune (C5 = 72).
const std::vector<Note> kMajorTune{{72, 1}, {72, 1}, {79, 1}, {79, 1}, {81, 1}, {81, 1}, {79, 2},
                                   {77, 1}, {77, 1}, {76, 1}, {76, 1}, {74, 1}, {74, 1}, {72, 2}};

const char* keyName (const KeyEstimate& e)
{
    static thread_local char text[32];
    static constexpr const char* roots[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    std::snprintf (text, sizeof (text), "%s %s", roots[static_cast<int> (e.root)],
                   e.scale == Scale::Major ? "major" : "minor");
    return text;
}
} // namespace

TEST_CASE ("Key detection finds the key of a minor tune in every key", "[unit][key]")
{
    const auto root = GENERATE (range (0, 12));
    const auto e = analyse (melody (kMinorTune, 48000.0, 16.0, root - 9.0), 48000.0);
    INFO ("expected root " << root << ", got " << keyName (e) << " at " << e.confidence);
    REQUIRE (e.valid);
    CHECK (static_cast<int> (e.root) == root);
    CHECK (e.scale == Scale::NaturalMinor);
    CHECK (e.confidence >= 0.4f);
}

TEST_CASE ("Key detection finds the key of a major tune in every key", "[unit][key]")
{
    const auto root = GENERATE (range (0, 12));
    const auto e = analyse (melody (kMajorTune, 48000.0, 16.0, root - 12.0), 48000.0);
    INFO ("expected root " << root << ", got " << keyName (e) << " at " << e.confidence);
    REQUIRE (e.valid);
    CHECK (static_cast<int> (e.root) == root);
    CHECK (e.scale == Scale::Major);
    CHECK (e.confidence >= 0.4f);
}

TEST_CASE ("Key detection finds the key of chord progressions", "[unit][key]")
{
    SECTION ("I–vi–IV–V in G major")
    {
        // G B D / E G B / C E G / D F# A, each with its root an octave below.
        const std::vector<std::vector<double>> chords{
            {43, 55, 59, 62}, {40, 52, 55, 59}, {36, 48, 52, 55}, {38, 50, 54, 57}};
        const auto e = analyse (progression (chords, 2.0, 48000.0, 16.0), 48000.0);
        INFO (keyName (e) << " at " << e.confidence);
        CHECK (e.root == KeyRoot::G);
        CHECK (e.scale == Scale::Major);
        CHECK (e.confidence >= 0.4f);
    }
    SECTION ("i–iv–v–i in E minor")
    {
        // E G B / A C E / B D F# / E G B.
        const std::vector<std::vector<double>> chords{
            {40, 52, 55, 59}, {45, 57, 60, 64}, {47, 59, 62, 66}, {40, 52, 55, 59}};
        const auto e = analyse (progression (chords, 2.0, 48000.0, 16.0), 48000.0);
        INFO (keyName (e) << " at " << e.confidence);
        CHECK (e.root == KeyRoot::E);
        CHECK (e.scale == Scale::NaturalMinor);
        CHECK (e.confidence >= 0.4f);
    }
}

TEST_CASE ("Key detection gives no confident key for material without one", "[unit][key]")
{
    SECTION ("A chromatic scale")
    {
        std::vector<Note> chromatic;
        for (int n = 60; n < 72; ++n)
            chromatic.push_back ({static_cast<double> (n), 0.5});
        const auto e = analyse (melody (chromatic, 48000.0, 16.0), 48000.0);
        INFO (keyName (e) << " at " << e.confidence);
        CHECK (e.confidence < 0.4f);
    }
    SECTION ("White noise")
    {
        const auto e = analyse (whiteNoise (48000.0, 16.0), 48000.0);
        INFO (keyName (e) << " at " << e.confidence);
        CHECK (e.confidence < 0.4f);
    }
    SECTION ("Silence")
    {
        const auto e = analyse (std::vector<float> (48000 * 8, 0.0f), 48000.0);
        CHECK_FALSE (e.valid);
        CHECK (juce::exactlyEqual (e.confidence, 0.0f));
    }
}

TEST_CASE ("Key detection works the same at every sample rate", "[unit][key]")
{
    const auto rate = GENERATE (22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0);
    const auto e = analyse (melody (kMinorTune, rate, 12.0, -2.0), rate); // G minor
    INFO (rate << " Hz: " << keyName (e) << " at " << e.confidence);
    CHECK (e.root == KeyRoot::G);
    CHECK (e.scale == Scale::NaturalMinor);
    CHECK (e.confidence >= 0.4f);
}

TEST_CASE ("Key detection needs a few seconds and grows more sure", "[unit][key]")
{
    KeyDetector detector;
    detector.prepare (48000.0);
    const auto audio = melody (kMinorTune, 48000.0, 16.0);

    // Under two seconds: nothing shown yet.
    detector.push (audio.data(), 48000);
    CHECK_FALSE (detector.getEstimate().valid);

    detector.push (audio.data() + 48000, 48000 * 3);
    const auto early = detector.getEstimate();
    REQUIRE (early.valid);

    detector.push (audio.data() + 48000 * 4, static_cast<int> (audio.size()) - 48000 * 4);
    const auto later = detector.getEstimate();
    CHECK (later.root == KeyRoot::A);
    CHECK (later.scale == Scale::NaturalMinor);
    CHECK (later.confidence > early.confidence);
    CHECK (later.confidence >= 0.7f);
}

TEST_CASE ("Key detection forgets old audio when it should", "[unit][key]")
{
    KeyDetector detector;
    detector.prepare (48000.0);
    detector.setMemorySeconds (8.0);

    const auto first = melody (kMajorTune, 48000.0, 20.0, -12.0 + 2.0); // D major
    const auto second = melody (kMinorTune, 48000.0, 40.0, -4.0);       // F minor
    detector.push (first.data(), static_cast<int> (first.size()));
    CHECK (detector.getEstimate().root == KeyRoot::D);

    detector.push (second.data(), static_cast<int> (second.size()));
    const auto e = detector.getEstimate();
    INFO (keyName (e));
    CHECK (e.root == KeyRoot::F);
    CHECK (e.scale == Scale::NaturalMinor);
}

TEST_CASE ("Key detection's confidence rule", "[unit][key]")
{
    // A clear lead after 8 s is fully confident; a weak fit or a tie isn't.
    CHECK (KeyDetector::confidenceFor (0.85, 0.6, 8.0) > 0.99f);
    CHECK (KeyDetector::confidenceFor (0.85, 0.85, 8.0) < 0.55f);
    CHECK (juce::exactlyEqual (KeyDetector::confidenceFor (0.45, 0.2, 8.0), 0.0f));
    CHECK (KeyDetector::confidenceFor (0.85, 0.6, 4.0) < 0.55f);
    CHECK (KeyDetector::decimationFor (44100.0) == 4);
    CHECK (KeyDetector::decimationFor (48000.0) == 4);
    CHECK (KeyDetector::decimationFor (96000.0) == 8);
    CHECK (KeyDetector::decimationFor (192000.0) == 16);
    CHECK (KeyDetector::decimationFor (22050.0) == 2);
}
