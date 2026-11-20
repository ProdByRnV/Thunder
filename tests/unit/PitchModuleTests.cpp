// The pitch module: latency it reports, the shift each mode applies, Interval
// mode on melodies and chords, click-free changes, Pitch Mix, formants and reset
// (PRD FR-PITCH-001…006, FR-PITCH-010, FR-CORE-004, FR-CORE-008).

#include "support/TestSignals.h"

#include "core/dsp/pitch/PitchModule.h"
#include "core/music/IntervalMapper.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

using namespace thunder;
using namespace thunder::test;
using Catch::Matchers::WithinAbs;
using dsp::PitchModule;
using params::ParamId;
using params::ParamSnapshot;
using params::PitchDirection;
using params::PitchInterval;
using params::PitchMode;
using params::PitchOctave;

namespace
{
constexpr double kRate = 48000.0;

ParamSnapshot withShift (int semitones, float cents = 0.0f)
{
    auto s = ParamSnapshot::defaults();
    setSemitoneShift (s, static_cast<float> (semitones), cents);
    return s;
}

ParamSnapshot withOctave (PitchOctave octave)
{
    auto s = ParamSnapshot::defaults();
    setChoice (s, ParamId::pitchMode, PitchMode::Octave);
    setChoice (s, ParamId::pitchOctave, octave);
    return s;
}

ParamSnapshot withInterval (music::Key key, PitchInterval interval, PitchDirection direction)
{
    auto s = ParamSnapshot::defaults();
    setChoice (s, ParamId::pitchMode, PitchMode::Interval);
    setChoice (s, ParamId::pitchInterval, interval);
    setChoice (s, ParamId::pitchDirection, direction);
    s.set (ParamId::keyRoot, static_cast<float> (key.root));
    setChoice (s, ParamId::keyScale, key.scale);
    return s;
}

// Amplitude of the `hz` component of s[from, to) (Goertzel), for a steady tone.
double amplitudeAt (const std::vector<float>& s, std::size_t from, std::size_t to, double hz, double sampleRate)
{
    const auto w = kTwoPi * hz / sampleRate;
    const auto coeff = 2.0 * std::cos (w);
    double s1 = 0.0, s2 = 0.0;
    for (auto i = from; i < to; ++i)
    {
        const auto s0 = s[i] + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    const auto power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
    return 2.0 * std::sqrt (std::max (power, 0.0)) / static_cast<double> (to - from);
}

// The fundamental of s[from, to), from the pitch tracker. Accurate on harmonic
// tones, where zero crossings are not.
double trackedFrequency (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    music::PitchTracker tracker;
    tracker.prepare (kRate);
    const float* channels[] = {s.data() + from};
    tracker.process (channels, 1, static_cast<int> (to - from));
    return tracker.getEstimate().frequencyHz;
}

// Spectral centroid of s[from, from + 8192), in Hz (Hann window).
double spectralCentroid (const std::vector<float>& s, std::size_t from, double sampleRate)
{
    constexpr int order = 13, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (2 * size, 0.0f);
    for (int i = 0; i < size; ++i)
    {
        const auto hann = 0.5f - 0.5f * std::cos (static_cast<float> (kTwoPi) * static_cast<float> (i) / size);
        data[static_cast<std::size_t> (i)] = s[from + static_cast<std::size_t> (i)] * hann;
    }
    fft.performFrequencyOnlyForwardTransform (data.data());

    double weighted = 0.0, total = 0.0;
    for (int bin = 1; bin < size / 2; ++bin)
    {
        const auto magnitude = static_cast<double> (data[static_cast<std::size_t> (bin)]);
        weighted += magnitude * bin * sampleRate / size;
        total += magnitude;
    }
    return weighted / total;
}

// The largest sample-to-sample step in s[from, to): a click shows up as a step
// far larger than the signal's own slope.
float largestStep (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    float largest = 0.0f;
    for (auto i = std::max<std::size_t> (from, 1); i < to; ++i)
        largest = std::max (largest, std::abs (s[i] - s[i - 1]));
    return largest;
}

// Runs `input` through `pitch`, choosing the parameters for each chunk with
// `paramsAt` (time in seconds → snapshot).
template<typename ParamsAt>
std::vector<float> runWithChanges (PitchModule& pitch, const std::vector<float>& input, ParamsAt paramsAt)
{
    const TransportState transport;
    std::vector<float> left = input, right = input;
    for (std::size_t start = 0; start < input.size(); start += 256)
    {
        const auto n = std::min<std::size_t> (256, input.size() - start);
        float* channels[] = {left.data() + start, right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        const auto snapshot = paramsAt (static_cast<double> (start) / kRate);
        pitch.process (block, ProcessContext{transport, snapshot});
    }
    return left;
}
} // namespace

TEST_CASE ("The pitch module's reported latency is exactly its delay", "[unit][pitch]")
{
    for (const auto sampleRate : {44100.0, 48000.0, 96000.0})
    {
        INFO ("sample rate " << sampleRate);
        PitchModule pitch;
        pitch.prepare ({sampleRate, 256, 2});

        // About 80 ms: under the 100 ms target (PRD NFR-PERF-004).
        CHECK_THAT (pitch.getLatencySamples() / sampleRate, WithinAbs (0.08, 0.002));

        const auto out = runModule (pitch, impulse (static_cast<std::size_t> (sampleRate * 0.5), 1000), withShift (0));
        CHECK (peakIndex (out) == 1000u + static_cast<std::size_t> (pitch.getLatencySamples()));
    }
}

TEST_CASE ("The pitch module shifts by semitones and cents", "[unit][pitch]")
{
    constexpr double sampleRate = 48000.0;

    struct Case
    {
        int semitones;
        float cents;
    };

    for (const auto c : {Case{12, 0.0f}, Case{7, 0.0f}, Case{-12, 0.0f}, Case{0, 50.0f}, Case{-5, -30.0f}})
    {
        INFO (c.semitones << " st " << c.cents << " cents");
        PitchModule pitch;
        pitch.prepare ({sampleRate, 256, 2});

        const auto out = runModule (pitch, sine (440.0, sampleRate, 1.5), withShift (c.semitones, c.cents));
        const auto expected = 440.0 * std::pow (2.0, (c.semitones + c.cents / 100.0) / 12.0);
        const auto measured = zeroCrossingFrequency (out, 24000, 72000, sampleRate);

        CHECK (std::abs (cents (measured, expected)) < 6.0);
        CHECK (allFinite (out));
    }
}

TEST_CASE ("Changing the shift glides instead of jumping", "[unit][pitch]")
{
    PitchModule pitch;
    pitch.prepare ({48000.0, 256, 2});

    const auto input = sine (440.0, 48000.0, 0.1);
    runModule (pitch, input, withShift (0));
    CHECK_THAT (pitch.getCurrentShift(), WithinAbs (0.0, 1e-6));

    // One chunk later the shift is on its way, not there yet.
    const std::vector<float> chunk (input.begin(), input.begin() + 256);
    runModule (pitch, chunk, withShift (12));
    CHECK (pitch.getCurrentShift() > 0.0f);
    CHECK (pitch.getCurrentShift() < 12.0f);

    // After the glide (15 ms) it has arrived.
    runModule (pitch, input, withShift (12));
    CHECK_THAT (pitch.getCurrentShift(), WithinAbs (12.0, 1e-4));
}

TEST_CASE ("Reset clears what the pitch module was holding", "[unit][pitch]")
{
    PitchModule pitch;
    pitch.prepare ({48000.0, 256, 2});

    runModule (pitch, sine (440.0, 48000.0, 0.5), withShift (7));
    pitch.reset();

    const auto out = runModule (pitch, std::vector<float> (24000, 0.0f), withShift (7));
    CHECK (energy (out, 0, out.size()) < 1.0e-12);
}

TEST_CASE ("Octave mode shifts by whole octaves and ignores Fine", "[unit][pitch]")
{
    struct Case
    {
        PitchOctave octave;
        int semitones;
    };

    for (const auto c : {Case{PitchOctave::Down2, -24}, Case{PitchOctave::Down1, -12}, Case{PitchOctave::Up1, 12},
                         Case{PitchOctave::Up2, 24}})
    {
        INFO ("octave " << c.semitones / 12);
        PitchModule pitch;
        pitch.prepare ({kRate, 256, 2});

        auto snapshot = withOctave (c.octave);
        snapshot.set (ParamId::pitchFine, 50.0f); // Semitone mode only

        const auto out = runModule (pitch, sine (440.0, kRate, 1.5), snapshot);
        const auto expected = 440.0 * std::pow (2.0, c.semitones / 12.0);

        CHECK_THAT (pitch.getCurrentShift(), WithinAbs (c.semitones, 1e-4));
        CHECK (std::abs (cents (zeroCrossingFrequency (out, 24000, 72000, kRate), expected)) < 6.0);
    }
}

TEST_CASE ("The default sound is one octave up", "[unit][pitch]")
{
    PitchModule pitch;
    pitch.prepare ({kRate, 256, 2});

    const auto out = runModule (pitch, sine (440.0, kRate, 1.0), ParamSnapshot::defaults());
    CHECK_THAT (pitch.getCurrentShift(), WithinAbs (12.0, 1e-4));
    CHECK (std::abs (cents (zeroCrossingFrequency (out, 24000, 48000, kRate), 880.0)) < 6.0);
}

TEST_CASE ("Interval mode shifts a held note by the in-key interval", "[unit][pitch][interval]")
{
    struct Case
    {
        music::Key key;
        int note;
        PitchInterval interval;
        PitchDirection direction;
    };

    using music::Scale;
    const Case cases[] = {
        {{0, Scale::Major}, 60, PitchInterval::Third, PitchDirection::Up},          // C → E (+4)
        {{0, Scale::Major}, 62, PitchInterval::Third, PitchDirection::Up},          // D → F (+3)
        {{9, Scale::NaturalMinor}, 69, PitchInterval::Fifth, PitchDirection::Down}, // A → D (−7)
        {{9, Scale::NaturalMinor}, 71, PitchInterval::Fifth, PitchDirection::Up},   // B → F (+6)
        {{2, Scale::Dorian}, 64, PitchInterval::Sixth, PitchDirection::Up},         // E → C (+8)
        {{7, Scale::MajorPentatonic}, 67, PitchInterval::Second, PitchDirection::Up},
        {{4, Scale::HarmonicMinor}, 63, PitchInterval::Seventh, PitchDirection::Down},
        {{0, Scale::Chromatic}, 61, PitchInterval::Fourth, PitchDirection::Up}, // fixed +5
    };

    for (const auto& c : cases)
    {
        const auto shift = music::IntervalMapper::shiftFor (c.key, c.note, c.interval, c.direction);
        INFO ("root " << c.key.root << " scale " << params::enumIndex (c.key.scale) << " note " << c.note
                      << " expected shift " << shift);

        PitchModule pitch;
        pitch.prepare ({kRate, 256, 2});

        // A harmonic tone (8 partials), like a real instrument.
        const auto out =
            runModule (pitch, steadyTone (c.note, kRate, 1.5, 8), withInterval (c.key, c.interval, c.direction));
        const auto expected = music::noteToFrequency (c.note + shift);

        CHECK (pitch.isTrackingNote());
        CHECK_THAT (pitch.getCurrentShift(), WithinAbs (shift, 1e-4));
        CHECK (std::abs (cents (trackedFrequency (out, 36000, 72000), expected)) < 6.0);
        CHECK (c.key.contains (c.note + shift));
    }
}

TEST_CASE ("Interval mode keeps every note of a melody in key, in every scale", "[unit][pitch][interval]")
{
    // A melody that walks up the scale, leaps an octave and comes back down, as
    // pure tones of 350 ms each. Each output note is measured over its settled
    // part: from 170 ms into the note (the tracker needs ~61 ms to recognise a
    // note, and the shifter's windows then blend old and new) to 20 ms before
    // its end.
    constexpr double noteSeconds = 0.35;

    for (int s = 0; s <= static_cast<int> (params::enumIndex (music::Scale::Chromatic)); ++s)
    {
        const music::Key key{9, static_cast<music::Scale> (s)}; // A
        INFO ("scale " << s);

        // The first seven scale notes from A3, then a leap and back down.
        std::vector<int> melody;
        for (int note = 57; melody.size() < 7; ++note)
            if (key.contains (note))
                melody.push_back (note);
        melody.push_back (melody.front() + 12);
        melody.push_back (melody[3]);
        melody.push_back (melody[1]);

        const auto noteAt = [&] (double t)
        {
            const auto i = std::min (static_cast<std::size_t> (t / noteSeconds), melody.size() - 1);
            return static_cast<double> (melody[i]);
        };
        const auto input = tone (noteAt, kRate, noteSeconds * static_cast<double> (melody.size()));

        for (const auto direction : {PitchDirection::Up, PitchDirection::Down})
        {
            PitchModule pitch;
            pitch.prepare ({kRate, 256, 2});
            const auto out = runModule (pitch, input, withInterval (key, PitchInterval::Third, direction));
            const auto latency = static_cast<double> (pitch.getLatencySamples());

            // The first note also waits for the tracker to start, so it is skipped.
            for (std::size_t i = 1; i < melody.size(); ++i)
            {
                const auto shift = music::IntervalMapper::shiftFor (key, melody[i], PitchInterval::Third, direction);
                const auto noteStart = static_cast<double> (i) * noteSeconds;
                const auto from = static_cast<std::size_t> (latency + (noteStart + 0.17) * kRate);
                const auto to = static_cast<std::size_t> (latency + (noteStart + noteSeconds - 0.02) * kRate);
                const auto measured = zeroCrossingFrequency (out, from, to, kRate);
                const auto expected = music::noteToFrequency (melody[i] + shift);

                INFO ("note " << melody[i] << " direction " << params::enumIndex (direction) << ": measured "
                              << measured << " Hz, expected " << expected << " Hz");
                CHECK (std::abs (cents (measured, expected)) < 15.0);
                CHECK (key.contains (melody[i] + shift));
            }
        }
    }
}

TEST_CASE ("Interval mode falls back to a steady fixed shift on chords", "[unit][pitch][interval]")
{
    const music::Key key{0, music::Scale::Major};
    PitchModule pitch;
    pitch.prepare ({kRate, 256, 2});

    const auto snapshot = withInterval (key, PitchInterval::Third, PitchDirection::Up);
    const auto fixed = music::IntervalMapper::bestFixedShift (key, PitchInterval::Third, PitchDirection::Up);

    // C major triad held for 2 s: after the first half second the shift must sit
    // on the fixed value every chunk, without wobbling.
    const auto input = chord ({60.0, 64.0, 67.0}, kRate, 2.0, 6);
    const TransportState transport;
    std::vector<float> left = input, right = input;
    int wobbles = 0;
    for (std::size_t start = 0; start + 256 <= input.size(); start += 256)
    {
        float* channels[] = {left.data() + start, right.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, 256);
        pitch.process (block, ProcessContext{transport, snapshot});

        if (start > static_cast<std::size_t> (0.5 * kRate) &&
            (pitch.isTrackingNote() || !juce::exactlyEqual (pitch.getCurrentShift(), static_cast<float> (fixed))))
            ++wobbles;
    }
    CHECK (wobbles == 0);
}

TEST_CASE ("Mode and shift changes during playback are click-free", "[unit][pitch]")
{
    // A steady tone while the mode, interval and formant setting change every
    // 200 ms. A click is a sample step much larger than the tone's own slope at
    // the highest pitch it reaches (+12 → 880 Hz).
    const auto input = steadyTone (69.0, kRate, 2.4, 1, 0.5f); // A4
    const music::Key key{9, music::Scale::NaturalMinor};

    const auto paramsAt = [&] (double t)
    {
        const auto step = static_cast<int> (t / 0.2);
        ParamSnapshot s;
        switch (step % 6)
        {
            case 0:
                s = withOctave (PitchOctave::Up1);
                break;
            case 1:
                s = withInterval (key, PitchInterval::Third, PitchDirection::Up);
                break;
            case 2:
                s = withShift (7);
                break;
            case 3:
                s = withShift (-5, 30.0f);
                break;
            case 4:
                s = withInterval (key, PitchInterval::Fifth, PitchDirection::Down);
                break;
            default:
                s = withOctave (PitchOctave::Down1);
                break;
        }
        s.set (ParamId::pitchFormant, (step % 4) < 2 ? 1.0f : 0.0f);
        return s;
    };

    PitchModule pitch;
    pitch.prepare ({kRate, 256, 2});
    const auto out = runWithChanges (pitch, input, paramsAt);

    // The slope of a 0.5-amplitude 880 Hz sine, with room for the shifter's own
    // level variations.
    const auto slope = static_cast<float> (0.5 * kTwoPi * 880.0 / kRate);
    const auto latency = static_cast<std::size_t> (pitch.getLatencySamples());
    CHECK (largestStep (out, latency, out.size()) < 2.0f * slope);
    CHECK (allFinite (out));
}

TEST_CASE ("Pitch Mix blends the shifted and unshifted signals", "[unit][pitch]")
{
    const auto input = sine (440.0, kRate, 1.5);

    SECTION ("0% is the input, delayed by the latency")
    {
        PitchModule pitch;
        pitch.prepare ({kRate, 256, 2});
        auto snapshot = withOctave (PitchOctave::Up1);
        snapshot.set (ParamId::pitchMix, 0.0f);

        const auto out = runModule (pitch, input, snapshot);
        const auto latency = static_cast<std::size_t> (pitch.getLatencySamples());

        float worst = 0.0f;
        for (std::size_t i = latency; i < out.size(); ++i)
            worst = std::max (worst, std::abs (out[i] - input[i - latency]));
        CHECK (worst < 1.0e-6f);
    }

    SECTION ("50% has both, each at about -3 dB (equal power)")
    {
        const auto measure = [&] (float mixPercent)
        {
            PitchModule pitch;
            pitch.prepare ({kRate, 256, 2});
            auto snapshot = withOctave (PitchOctave::Up1);
            snapshot.set (ParamId::pitchMix, mixPercent);
            const auto out = runModule (pitch, input, snapshot);
            return std::pair{amplitudeAt (out, 24000, 72000, 440.0, kRate),
                             amplitudeAt (out, 24000, 72000, 880.0, kRate)};
        };

        const auto [unshiftedAt0, shiftedAt0] = measure (0.0f);
        const auto [unshiftedAt100, shiftedAt100] = measure (100.0f);
        const auto [unshiftedAt50, shiftedAt50] = measure (50.0f);

        CHECK (shiftedAt0 < 0.01 * unshiftedAt0);
        CHECK (unshiftedAt100 < 0.05 * shiftedAt100);

        const auto minus3dB = std::sqrt (0.5);
        CHECK_THAT (unshiftedAt50 / unshiftedAt0, WithinAbs (minus3dB, 0.03));
        CHECK_THAT (shiftedAt50 / shiftedAt100, WithinAbs (minus3dB, 0.05));
    }
}

TEST_CASE ("Preserve Formants keeps the tone colour where it was", "[unit][pitch]")
{
    // A "vowel": 150 Hz harmonics shaped by a resonance at 1 kHz. Shifted up an
    // octave without formant preservation the resonance moves to 2 kHz too; with
    // it, the resonance stays near 1 kHz while the pitch still rises.
    std::vector<float> vowel (static_cast<std::size_t> (2.0 * kRate), 0.0f);
    for (int h = 1; 150.0 * h < 8000.0; ++h)
    {
        const auto f = 150.0 * h;
        const auto gain = 1.0 / (1.0 + std::pow ((f - 1000.0) / 250.0, 2.0));
        for (std::size_t i = 0; i < vowel.size(); ++i)
            vowel[i] += static_cast<float> (0.1 * gain * std::sin (kTwoPi * f * static_cast<double> (i) / kRate));
    }

    const auto render = [&] (bool preserve)
    {
        PitchModule pitch;
        pitch.prepare ({kRate, 256, 2});
        auto snapshot = withOctave (PitchOctave::Up1);
        snapshot.set (ParamId::pitchFormant, preserve ? 1.0f : 0.0f);
        return runModule (pitch, vowel, snapshot);
    };

    const auto inputCentroid = spectralCentroid (vowel, 24000, kRate);
    const auto preserved = render (true);
    const auto moved = render (false);
    const auto preservedCentroid = spectralCentroid (preserved, 48000, kRate);
    const auto movedCentroid = spectralCentroid (moved, 48000, kRate);
    INFO ("centroids: input " << inputCentroid << " Hz, preserved " << preservedCentroid << " Hz, moved "
                              << movedCentroid << " Hz");

    CHECK (movedCentroid > 1.6 * inputCentroid);
    CHECK (preservedCentroid < 1.3 * inputCentroid);

    // The pitch moved either way: the 150 Hz fundamental is gone.
    const auto inputFundamental = amplitudeAt (vowel, 24000, 72000, 150.0, kRate);
    CHECK (amplitudeAt (preserved, 48000, 90000, 150.0, kRate) < 0.2 * inputFundamental);
    CHECK (amplitudeAt (moved, 48000, 90000, 150.0, kRate) < 0.2 * inputFundamental);
}

TEST_CASE ("Downward shifts keep their low end with Preserve Formants on", "[unit][pitch]")
{
    // Formants are preserved on upward shifts only: preserving them downwards
    // would remove much of the new, lower fundamental. So a downward shift sounds
    // the same with the switch on or off.
    const auto input = steadyTone (57.0, kRate, 1.0, 8);

    const auto render = [&] (bool preserve)
    {
        PitchModule pitch;
        pitch.prepare ({kRate, 256, 2});
        auto snapshot = withOctave (PitchOctave::Down1);
        snapshot.set (ParamId::pitchFormant, preserve ? 1.0f : 0.0f);
        return runModule (pitch, input, snapshot);
    };

    const auto on = render (true);
    const auto off = render (false);

    float worst = 0.0f;
    for (std::size_t i = 0; i < on.size(); ++i)
        worst = std::max (worst, std::abs (on[i] - off[i]));
    CHECK (worst < 1.0e-6f);
}
