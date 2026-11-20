// Measurements behind the pitch modes' defaults (hidden; run with
// `ThunderTests "[pitchmodes]"`). Prints the pitch error and level of the
// shifted output for each shift, with and without formant preservation.

#include "support/TestSignals.h"

#include "core/dsp/pitch/PitchModule.h"
#include "core/music/IntervalMapper.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdio>

using namespace thunder;
using namespace thunder::test;
using params::ParamId;

TEST_CASE ("Pitch accuracy by shift and formant setting", "[.][measure][pitchmodes]")
{
    constexpr double rate = 48000.0;

    for (const auto inputNote : {57.0, 69.0}) // A3, A4
    {
        const auto inputHz = music::noteToFrequency (inputNote);
        for (const auto harmonics : {1, 8})
        {
            std::printf (
                "\ninput %.1f Hz, %d harmonic(s)\n  shift   formants off (cents, rms)   formants on (cents, rms)\n",
                inputHz, harmonics);
            const auto input = steadyTone (inputNote, rate, 1.5, harmonics);

            for (const auto shift : {-24, -12, -11, -7, -5, -3, 3, 4, 7, 12, 24})
            {
                std::printf ("  %+4d", shift);
                for (const auto formants : {false, true})
                {
                    dsp::PitchModule pitch;
                    pitch.prepare ({rate, 256, 2});
                    auto snapshot = params::ParamSnapshot::defaults();
                    setSemitoneShift (snapshot, static_cast<float> (shift));
                    snapshot.set (ParamId::pitchFormant, formants ? 1.0f : 0.0f);

                    const auto out = runModule (pitch, input, snapshot);

                    // For pure tones, zero crossings; for harmonic tones the
                    // tracker itself (it reports the fundamental).
                    double measured = 0.0;
                    if (harmonics == 1)
                    {
                        measured = zeroCrossingFrequency (out, 24000, 72000, rate);
                    }
                    else
                    {
                        music::PitchTracker tracker;
                        tracker.prepare (rate);
                        const float* channels[] = {out.data() + 24000};
                        tracker.process (channels, 1, 48000);
                        measured = tracker.getEstimate().frequencyHz;
                    }

                    const auto expected = inputHz * std::pow (2.0, shift / 12.0);
                    const auto rms = std::sqrt (energy (out, 24000, 72000) / 48000.0);
                    std::printf ("          %+8.1f  %.3f       ", cents (measured, expected), rms);
                }
                std::printf ("\n");
            }
        }
    }
    SUCCEED();
}

TEST_CASE ("Pitch module CPU by mode", "[.][measure][pitchmodes]")
{
    // 10 s of a harmonic melody, stereo, 48 kHz, in 256-sample chunks.
    constexpr double rate = 48000.0;
    const auto input =
        tone ([] (double t) { return 57.0 + 2.0 * std::floor (std::fmod (t * 4.0, 6.0)); }, rate, 10.0, 8);

    const auto run = [&] (const char* name, params::ParamSnapshot snapshot)
    {
        dsp::PitchModule pitch;
        pitch.prepare ({rate, 256, 2});
        runModule (pitch, std::vector<float> (input.begin(), input.begin() + 48000), snapshot); // warm up

        const auto start = juce::Time::getMillisecondCounterHiRes();
        runModule (pitch, input, snapshot);
        const auto elapsedMs = juce::Time::getMillisecondCounterHiRes() - start;
        std::printf ("  %-34s %.2f%% of one core\n", name, elapsedMs / 10000.0 * 100.0);
    };

    std::printf ("\nPitch module CPU (stereo, 48 kHz):\n");
    auto s = params::ParamSnapshot::defaults();
    s.set (ParamId::pitchFormant, 0.0f);
    run ("Octave +1, formants off", s);
    run ("Octave +1, formants on (default)", params::ParamSnapshot::defaults());

    s = params::ParamSnapshot::defaults();
    setChoice (s, ParamId::pitchMode, params::PitchMode::Interval);
    run ("Interval 3rd up, formants on", s);

    s.set (ParamId::pitchMix, 50.0f);
    run ("Interval 3rd up, formants on, mix 50%", s);
    SUCCEED();
}

TEST_CASE ("How soon Interval mode is in key after a note starts", "[.][measure][pitchmodes]")
{
    // A melody of 400 ms pure tones alternating between two notes whose 3rds
    // differ (C → E is +4, D → F is +3 in C major). For each note, the time from
    // the note's start (in the output, i.e. after the latency) until the output
    // pitch stays within 25 cents of the right note.
    constexpr double rate = 48000.0, noteSeconds = 0.4;
    const music::Key key{0, music::Scale::Major};
    const int notes[] = {60, 62, 60, 62, 64, 65, 62, 60};

    const auto input =
        tone ([&] (double t) { return static_cast<double> (notes[std::min (static_cast<int> (t / noteSeconds), 7)]); },
              rate, noteSeconds * 8.0);

    auto s = params::ParamSnapshot::defaults();
    setChoice (s, ParamId::pitchMode, params::PitchMode::Interval);
    s.set (ParamId::keyRoot, 0.0f);
    setChoice (s, ParamId::keyScale, music::Scale::Major);

    dsp::PitchModule pitch;
    pitch.prepare ({rate, 256, 2});
    const auto out = runModule (pitch, input, s);
    const auto latency = static_cast<std::size_t> (pitch.getLatencySamples());

    // Output pitch relative to the right note, in cents, in 20 ms windows every
    // 10 ms from 40 ms before the note's start to 200 ms after it.
    std::printf ("\nInterval mode (C major, 3rd up): output pitch vs the right note (cents), 20 ms windows\n");
    std::printf ("  ms from note start:");
    for (int ms = -40; ms <= 200; ms += 20)
        std::printf (" %5d", ms);
    std::printf ("\n");

    const auto window = static_cast<std::size_t> (0.02 * rate);
    for (int i = 1; i < 8; ++i)
    {
        const auto shift =
            music::IntervalMapper::shiftFor (key, notes[i], params::PitchInterval::Third, params::PitchDirection::Up);
        const auto target = music::noteToFrequency (notes[i] + shift);
        const auto noteStart = latency + static_cast<std::size_t> (i * noteSeconds * rate);

        std::printf ("  %d -> %d:          ", notes[i], notes[i] + shift);
        for (int ms = -40; ms <= 200; ms += 20)
        {
            const auto centre = static_cast<std::ptrdiff_t> (noteStart) + ms * static_cast<int> (rate) / 1000;
            const auto from = static_cast<std::size_t> (centre) - window / 2;
            std::printf (" %+5.0f", cents (zeroCrossingFrequency (out, from, from + window, rate), target));
        }
        std::printf ("\n");
    }
    SUCCEED();
}

TEST_CASE ("Fundamental kept by downward shifts with formants preserved", "[.][measure][pitchmodes]")
{
    // Formant preservation keeps the input's spectral envelope, which is nearly
    // empty below the input's fundamental. This measures how much of the new,
    // lower fundamental survives, relative to the same shift without it.
    constexpr double rate = 48000.0;

    const auto amplitudeAt = [] (const std::vector<float>& s, double hz)
    {
        const auto w = kTwoPi * hz / rate;
        const auto coeff = 2.0 * std::cos (w);
        double s1 = 0.0, s2 = 0.0;
        for (std::size_t i = 24000; i < 72000; ++i)
        {
            const auto s0 = s[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }
        return std::sqrt (std::max (s1 * s1 + s2 * s2 - coeff * s1 * s2, 0.0));
    };

    for (const auto inputNote : {45.0, 57.0, 69.0}) // A2, A3, A4
    {
        std::printf ("\ninput %.1f Hz (8 harmonics): fundamental with formants on vs off\n",
                     music::noteToFrequency (inputNote));
        const auto input = steadyTone (inputNote, rate, 1.5, 8);

        for (const auto shift : {-3, -5, -7, -12, -17, -24})
        {
            double fundamental[2] = {};
            for (const auto formants : {false, true})
            {
                dsp::PitchModule pitch;
                pitch.prepare ({rate, 256, 2});
                auto snapshot = params::ParamSnapshot::defaults();
                setSemitoneShift (snapshot, static_cast<float> (shift));
                snapshot.set (ParamId::pitchFormant, formants ? 1.0f : 0.0f);
                const auto out = runModule (pitch, input, snapshot);
                fundamental[formants ? 1 : 0] = amplitudeAt (out, music::noteToFrequency (inputNote + shift));
            }
            std::printf ("  %+4d st: %+6.1f dB\n", shift,
                         juce::Decibels::gainToDecibels (fundamental[1] / fundamental[0], -120.0));
        }
    }
    SUCCEED();
}
