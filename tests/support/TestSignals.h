#pragma once

#include "core/engine/Module.h"
#include "core/music/Scale.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <vector>

// Test signals and measurements shared by the module and engine tests
// (ARCHITECTURE §15.2).

namespace thunder::test
{

// Puts the pitch module in Semitone mode at `semitones` + `cents`. (The default
// mode is Octave +1.)
inline void setSemitoneShift (params::ParamSnapshot& snapshot, float semitones, float cents = 0.0f)
{
    snapshot.set (params::ParamId::pitchMode, static_cast<float> (params::enumIndex (params::PitchMode::Semitone)));
    snapshot.set (params::ParamId::pitchSemitones, semitones);
    snapshot.set (params::ParamId::pitchFine, cents);
}

// Puts a choice parameter on `value`.
template<typename Enum> void setChoice (params::ParamSnapshot& snapshot, params::ParamId id, Enum value)
{
    snapshot.set (id, static_cast<float> (params::enumIndex (value)));
}

inline std::vector<float> sine (double hz, double sampleRate, double seconds, float amplitude = 0.5f)
{
    std::vector<float> s (static_cast<std::size_t> (sampleRate * seconds));
    for (std::size_t i = 0; i < s.size(); ++i)
        s[i] = amplitude * static_cast<float> (std::sin (2.0 * juce::MathConstants<double>::pi * hz *
                                                         static_cast<double> (i) / sampleRate));
    return s;
}

inline std::vector<float> impulse (std::size_t length, std::size_t position, float value = 1.0f)
{
    std::vector<float> s (length, 0.0f);
    s[position] = value;
    return s;
}

// Frequency from upward zero crossings between `from` and `to`, with linear
// interpolation of each crossing. Accurate for clean, sine-like signals.
inline double zeroCrossingFrequency (const std::vector<float>& s, std::size_t from, std::size_t to, double sampleRate)
{
    double first = -1.0, last = -1.0;
    int crossings = 0;
    for (auto i = std::max<std::size_t> (from, 1); i < to && i < s.size(); ++i)
    {
        if (s[i - 1] < 0.0f && s[i] >= 0.0f)
        {
            const auto fraction = static_cast<double> (-s[i - 1]) / static_cast<double> (s[i] - s[i - 1]);
            const auto position = static_cast<double> (i - 1) + fraction;
            if (first < 0.0)
                first = position;
            last = position;
            ++crossings;
        }
    }
    return crossings > 1 ? sampleRate * (crossings - 1) / (last - first) : 0.0;
}

inline double cents (double measuredHz, double expectedHz)
{
    return 1200.0 * std::log2 (measuredHz / expectedHz);
}

inline double energy (const std::vector<float>& s, std::size_t from, std::size_t to)
{
    double e = 0.0;
    for (auto i = from; i < to && i < s.size(); ++i)
        e += static_cast<double> (s[i]) * static_cast<double> (s[i]);
    return e;
}

inline std::size_t peakIndex (const std::vector<float>& s)
{
    std::size_t peak = 0;
    for (std::size_t i = 1; i < s.size(); ++i)
        if (std::abs (s[i]) > std::abs (s[peak]))
            peak = i;
    return peak;
}

inline bool allFinite (const std::vector<float>& s)
{
    return std::all_of (s.begin(), s.end(), [] (float x) { return std::isfinite (x); });
}

// Runs a mono signal through `module` as identical left and right channels, in
// chunks of `chunk` samples. Returns the left output, or both if `right` is given.
inline std::vector<float> runModule (Module& module, const std::vector<float>& input,
                                     const params::ParamSnapshot& snapshot, std::size_t chunk = 256,
                                     std::vector<float>* right = nullptr)
{
    const TransportState transport;
    std::vector<float> left = input, rightChannel = input;

    for (std::size_t start = 0; start < input.size(); start += chunk)
    {
        const auto n = std::min (chunk, input.size() - start);
        float* channels[] = {left.data() + start, rightChannel.data() + start};
        juce::dsp::AudioBlock<float> block (channels, 2, n);
        module.process (block, ProcessContext{transport, snapshot});
    }

    if (right != nullptr)
        *right = std::move (rightChannel);
    return left;
}

// ---- Synthetic melodies, chords and noise (for the pitch tracker)
// ----------------------------------------------------

inline constexpr double kTwoPi = 2.0 * juce::MathConstants<double>::pi;

// A tone with `harmonics` partials at 1/n amplitude (a band-limited sawtooth)
// whose frequency follows `noteAt` (fractional note number per time in seconds).
template<typename NoteAt>
inline std::vector<float> tone (NoteAt noteAt, double sampleRate, double seconds, int harmonics = 1,
                                float amplitude = 0.3f)
{
    std::vector<float> s (static_cast<std::size_t> (sampleRate * seconds));
    double phase = 0.0;
    for (std::size_t i = 0; i < s.size(); ++i)
    {
        const auto f0 = music::noteToFrequency (noteAt (static_cast<double> (i) / sampleRate));
        double v = 0.0;
        for (int h = 1; h <= harmonics; ++h)
            if (f0 * h < 0.45 * sampleRate)
                v += std::sin (phase * h) / h;
        s[i] = amplitude * static_cast<float> (v);
        phase = std::fmod (phase + kTwoPi * f0 / sampleRate, kTwoPi);
    }
    return s;
}

inline std::vector<float> steadyTone (double note, double sampleRate, double seconds, int harmonics = 1,
                                      float amplitude = 0.3f)
{
    return tone ([note] (double) { return note; }, sampleRate, seconds, harmonics, amplitude);
}

inline std::vector<float> chord (std::initializer_list<double> notes, double sampleRate, double seconds,
                                 int harmonics = 1)
{
    std::vector<float> sum (static_cast<std::size_t> (sampleRate * seconds), 0.0f);
    for (const auto note : notes)
    {
        const auto voice = steadyTone (note, sampleRate, seconds, harmonics, 0.2f);
        for (std::size_t i = 0; i < sum.size(); ++i)
            sum[i] += voice[i];
    }
    return sum;
}

inline std::vector<float> whiteNoise (double sampleRate, double seconds)
{
    juce::Random random (1);
    std::vector<float> s (static_cast<std::size_t> (sampleRate * seconds));
    for (auto& x : s)
        x = 0.3f * (2.0f * random.nextFloat() - 1.0f);
    return s;
}

} // namespace thunder::test
