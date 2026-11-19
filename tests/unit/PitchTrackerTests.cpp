// The monophonic pitch tracker behind Interval mode (PRD FR-PITCH-010,
// ROADMAP T-0.2-09): sines, rich tones, glides, vibrato, note changes, chords,
// noise and silence, all synthesised here (ARCHITECTURE §15.2).

#include "support/TestSignals.h"

#include "core/dsp/pitch/PitchModule.h"
#include "core/music/PitchTracker.h"
#include "core/music/Scale.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <initializer_list>
#include <set>
#include <vector>

using namespace thunder;
using namespace thunder::music;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;

namespace
{

// Runs the signal through a freshly prepared tracker in chunks and returns the
// estimate after each chunk.
std::vector<PitchEstimate> track (PitchTracker& tracker, const std::vector<float>& input, std::size_t chunk = 256)
{
    std::vector<PitchEstimate> estimates;
    for (std::size_t start = 0; start < input.size(); start += chunk)
    {
        const auto n = std::min (chunk, input.size() - start);
        const float* channels[] = {input.data() + start};
        tracker.process (channels, 1, static_cast<int> (n));
        estimates.push_back (tracker.getEstimate());
    }
    return estimates;
}

// The estimates from `fromSeconds` on (to skip the start-up window).
std::vector<PitchEstimate> from (const std::vector<PitchEstimate>& estimates, double fromSeconds, double sampleRate,
                                 std::size_t chunk = 256)
{
    const auto first = static_cast<std::size_t> (fromSeconds * sampleRate) / chunk;
    return {estimates.begin() + static_cast<std::ptrdiff_t> (std::min (first, estimates.size())), estimates.end()};
}
} // namespace

TEST_CASE ("The tracker finds the pitch of steady sines", "[unit][music][tracker]")
{
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        for (const auto note : {36, 45, 57, 64, 69, 81, 93})
        {
            INFO (sampleRate << " Hz, note " << note);
            PitchTracker tracker;
            tracker.prepare (sampleRate);

            const auto estimates = track (tracker, steadyTone (note, sampleRate, 0.3));
            const auto& e = estimates.back();

            CHECK (e.voiced);
            CHECK (e.note == note);
            CHECK (e.confidence > 0.95f);
            CHECK (std::abs (test::cents (e.frequencyHz, noteToFrequency (note))) < 5.0);
        }
    }
}

TEST_CASE ("The tracker finds the pitch of harmonic-rich tones", "[unit][music][tracker]")
{
    constexpr double sampleRate = 48000.0;

    for (const auto note : {40, 52, 60, 69, 76})
    {
        INFO ("note " << note);
        PitchTracker tracker;
        tracker.prepare (sampleRate);

        const auto e = track (tracker, steadyTone (note, sampleRate, 0.3, 12)).back();
        CHECK (e.voiced);
        CHECK (e.note == note);
        CHECK (std::abs (test::cents (e.frequencyHz, noteToFrequency (note))) < 5.0);
    }
}

TEST_CASE ("The tracker is silent for silence and quiet input", "[unit][music][tracker]")
{
    PitchTracker tracker;
    tracker.prepare (48000.0);

    for (const auto& e : track (tracker, std::vector<float> (48000, 0.0f)))
    {
        CHECK_FALSE (e.voiced);
        CHECK (e.note == -1);
    }

    // -70 dBFS is below the -60 dBFS gate; -40 dBFS is tracked.
    tracker.reset();
    CHECK_FALSE (track (tracker, steadyTone (69, 48000.0, 0.3, 1, 0.0003f)).back().voiced);
    tracker.reset();
    CHECK (track (tracker, steadyTone (69, 48000.0, 0.3, 1, 0.01f)).back().note == 69);
}

TEST_CASE ("The tracker is not confident about noise", "[unit][music][tracker]")
{
    constexpr double sampleRate = 48000.0;
    PitchTracker tracker;
    tracker.prepare (sampleRate);

    int voiced = 0;
    for (const auto& e : from (track (tracker, whiteNoise (sampleRate, 2.0)), 0.05, sampleRate))
        voiced += e.voiced ? 1 : 0;
    CHECK (voiced == 0);
}

TEST_CASE ("The tracker is not confident about chords", "[unit][music][tracker]")
{
    // Interval mode must fall back to a fixed shift on chords (FR-PITCH-004),
    // so the tracker must not report one confident note for them.
    constexpr double sampleRate = 48000.0;

    struct Case
    {
        const char* name;
        std::vector<float> signal;
    };

    const Case cases[] = {
        {"C major triad", chord ({60, 64, 67}, sampleRate, 1.0)},
        {"C major triad, rich", chord ({60, 64, 67}, sampleRate, 1.0, 8)},
        {"C major triad, high", chord ({72, 76, 79}, sampleRate, 1.0)},
        {"A minor triad", chord ({57, 60, 64}, sampleRate, 1.0)},
        {"A minor triad, rich", chord ({57, 60, 64}, sampleRate, 1.0, 8)},
        {"major third", chord ({60, 64}, sampleRate, 1.0)},
        {"perfect fourth", chord ({67, 72}, sampleRate, 1.0)},
        {"C major 7th", chord ({60, 64, 67, 71}, sampleRate, 1.0, 4)},
    };

    for (const auto& c : cases)
    {
        INFO (c.name);
        PitchTracker tracker;
        tracker.prepare (sampleRate);

        int voiced = 0;
        float highest = 0.0f;
        for (const auto& e : from (track (tracker, c.signal), 0.05, sampleRate))
        {
            voiced += e.voiced ? 1 : 0;
            highest = std::max (highest, e.confidence);
        }
        INFO ("highest confidence " << highest);
        CHECK (voiced == 0);
    }
}

TEST_CASE ("The tracker holds a note through vibrato", "[unit][music][tracker]")
{
    constexpr double sampleRate = 48000.0;
    PitchTracker tracker;
    tracker.prepare (sampleRate);

    // A4 with ±40 cents of vibrato at 5.5 Hz, a rich tone.
    const auto signal = tone ([] (double t) { return 69.0 + 0.4 * std::sin (kTwoPi * 5.5 * t); }, sampleRate, 2.0, 8);

    float lowest = 200.0f, highest = 0.0f;
    for (const auto& e : from (track (tracker, signal), 0.1, sampleRate))
    {
        CHECK (e.voiced);
        CHECK (e.note == 69);
        lowest = std::min (lowest, e.noteValue);
        highest = std::max (highest, e.noteValue);
    }

    // The raw estimate does follow the vibrato.
    CHECK (highest - lowest > 0.5f);
    CHECK (highest - lowest < 1.0f);
}

TEST_CASE ("The tracker follows a glide one semitone at a time", "[unit][music][tracker]")
{
    constexpr double sampleRate = 44100.0;
    PitchTracker tracker;
    tracker.prepare (sampleRate);

    // A3 to A4 over 1.5 s, held 0.25 s at each end.
    const auto signal =
        tone ([] (double t) { return 57.0 + 12.0 * std::clamp ((t - 0.25) / 1.5, 0.0, 1.0); }, sampleRate, 2.0, 6);

    std::vector<int> notes;
    for (const auto& e : from (track (tracker, signal), 0.1, sampleRate))
    {
        CHECK (e.voiced);
        if (notes.empty() || notes.back() != e.note)
            notes.push_back (e.note);
    }

    // Every semitone from A3 to A4 in turn, no skips, no steps back.
    std::vector<int> expected;
    for (int n = 57; n <= 69; ++n)
        expected.push_back (n);
    CHECK (notes == expected);
}

TEST_CASE ("The tracker changes note within its decision delay", "[unit][music][tracker]")
{
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        INFO (sampleRate << " Hz");
        PitchTracker tracker;
        tracker.prepare (sampleRate);

        // A4, then C5, then E4, with no gap: a melody played legato.
        const auto changeAt = [&] (int index) { return static_cast<std::size_t> (sampleRate * 0.4 * index); };
        auto signal = steadyTone (69, sampleRate, 0.4, 6);
        for (const auto note : {72, 64})
        {
            const auto next = steadyTone (note, sampleRate, 0.4, 6);
            signal.insert (signal.end(), next.begin(), next.end());
        }

        constexpr std::size_t chunk = 64;
        const auto estimates = track (tracker, signal, chunk);
        const auto limit = static_cast<std::size_t> (tracker.getDecisionDelaySamples()) + chunk;

        for (const auto& [index, note] : {std::pair{1, 72}, std::pair{2, 64}})
        {
            INFO ("change " << index << " to note " << note);
            std::size_t switched = 0;
            for (std::size_t i = changeAt (index) / chunk; i < estimates.size(); ++i)
                if (estimates[i].note == note)
                {
                    switched = (i + 1) * chunk;
                    break;
                }

            CHECK (switched > changeAt (index));
            CHECK (switched - changeAt (index) <= limit);
        }

        // Only the three melody notes ever appear: no wrong notes at the changes.
        // While the window holds both notes it may report none, but only briefly.
        std::set<int> seen;
        std::size_t gap = 0, longestGap = 0;
        for (const auto& e : from (estimates, 0.1, sampleRate, chunk))
        {
            if (e.voiced)
                seen.insert (e.note);
            gap = e.voiced ? 0 : gap + chunk;
            longestGap = std::max (longestGap, gap);
        }
        CHECK (seen == std::set<int>{69, 72, 64});
        CHECK (longestGap <= limit);
    }
}

TEST_CASE ("The tracker decides within the pitch shifter's latency", "[unit][music][tracker]")
{
    // FR-PITCH-010: any look-ahead must fit inside the reported latency, so the
    // shifted note can be lined up with the start of the note it belongs to.
    for (const auto sampleRate : {44100.0, 48000.0, 88200.0, 96000.0, 192000.0})
    {
        INFO (sampleRate << " Hz");
        PitchTracker tracker;
        tracker.prepare (sampleRate);
        dsp::PitchModule pitch;
        pitch.prepare ({sampleRate, 256, 2});

        CHECK (tracker.getDecisionDelaySamples() < pitch.getLatencySamples());
        CHECK (tracker.getAnalysisRate() >= 44100.0);
        CHECK (tracker.getAnalysisRate() <= 48000.0);
    }
}

TEST_CASE ("The tracker gives the same result for any block size", "[unit][music][tracker]")
{
    constexpr double sampleRate = 48000.0;
    const auto signal = tone ([] (double t) { return 60.0 + 7.0 * std::min (t, 0.5); }, sampleRate, 0.7, 4);

    PitchTracker a, b, c;
    for (auto* t : {&a, &b, &c})
        t->prepare (sampleRate);

    track (a, signal, 1);
    track (b, signal, 17);
    track (c, signal, 4096);

    for (const auto* t : {&b, &c})
    {
        CHECK (t->getEstimate().note == a.getEstimate().note);
        CHECK (t->getEstimate().voiced == a.getEstimate().voiced);
        CHECK_THAT (t->getEstimate().frequencyHz, WithinAbs (a.getEstimate().frequencyHz, 1e-3));
    }
}

TEST_CASE ("Reset forgets the input", "[unit][music][tracker]")
{
    PitchTracker tracker;
    tracker.prepare (48000.0);
    CHECK (track (tracker, steadyTone (69, 48000.0, 0.3)).back().voiced);

    tracker.reset();
    CHECK_FALSE (tracker.getEstimate().voiced);
    CHECK (tracker.getEstimate().note == -1);

    // The old tone is gone from the window too.
    CHECK_FALSE (track (tracker, std::vector<float> (1024, 0.0f)).back().voiced);
}

TEST_CASE ("Stereo input is tracked as mono", "[unit][music][tracker]")
{
    PitchTracker tracker;
    tracker.prepare (48000.0);

    const auto left = steadyTone (64, 48000.0, 0.3);
    const std::vector<float> right (left.size(), 0.0f);
    for (std::size_t start = 0; start < left.size(); start += 256)
    {
        const auto n = std::min<std::size_t> (256, left.size() - start);
        const float* channels[] = {left.data() + start, right.data() + start};
        tracker.process (channels, 2, static_cast<int> (n));
    }
    CHECK (tracker.getEstimate().note == 64);
}
