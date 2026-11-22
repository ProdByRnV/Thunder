#pragma once

// A minimal host for tests: a play head the test moves, and a synthetic
// melody played through a processor block by block, as a DAW would.

#include "core/music/Scale.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <functional>

namespace thunder::test
{

class TestPlayHead final : public juce::AudioPlayHead
{
public:
    double ppq = 0.0;
    double bpm = 120.0;
    int numerator = 4;
    int denominator = 4;
    bool playing = true;
    bool hostInfo = true; // false: the host reports nothing at all

    double quarterNotesPerBar() const noexcept { return numerator * 4.0 / denominator; }

    juce::Optional<PositionInfo> getPosition() const override
    {
        if (!hostInfo)
            return {};

        PositionInfo info;
        info.setBpm (bpm);
        info.setTimeSignature (TimeSignature{numerator, denominator});
        info.setIsPlaying (playing);
        info.setPpqPosition (ppq);
        info.setPpqPositionOfLastBarStart (std::floor (ppq / quarterNotesPerBar()) * quarterNotesPerBar());
        return info;
    }

    void advance (int samples, double sampleRate) noexcept
    {
        if (playing)
            ppq += samples * bpm / (60.0 * sampleRate);
    }
};

// A plucked melody, one note per beat (C4 E♭4 G4 B♭4), each decaying, with a
// few harmonics: something with a clear shape and a real spectrum.
inline float melodyAt (double ppq, double bpm) noexcept
{
    constexpr double notes[] = {60.0, 63.0, 67.0, 70.0};
    const auto beat = std::floor (ppq);
    const auto note = notes[static_cast<int> (std::fmod (std::fmod (beat, 4.0) + 4.0, 4.0))];
    const auto t = (ppq - beat) * 60.0 / bpm; // seconds into the note
    const auto f = music::noteToFrequency (note);
    const auto envelope = std::exp (-4.0 * t) * std::min (1.0, t * 400.0);
    auto v = 0.0;
    for (int h = 1; h <= 4; ++h)
        v += std::sin (2.0 * juce::MathConstants<double>::pi * f * h * t) / h;
    return static_cast<float> (0.35 * envelope * v);
}

// Plays `seconds` of the melody through `processor` in blocks of `blockSize`,
// moving the play head. `everyBlock` runs after each block (to let an editor
// read its frames, say).
inline void playMelody (juce::AudioProcessor& processor, TestPlayHead& head, double seconds, double sampleRate,
                        int blockSize = 512, const std::function<void()>& everyBlock = {})
{
    juce::AudioBuffer<float> buffer (2, blockSize);
    juce::MidiBuffer midi;
    const auto total = static_cast<int> (seconds * sampleRate);

    for (int done = 0; done < total; done += blockSize)
    {
        for (int i = 0; i < blockSize; ++i)
        {
            const auto ppq = head.ppq + i * head.bpm / (60.0 * sampleRate);
            const auto v = head.playing ? melodyAt (ppq, head.bpm) : 0.0f;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }
        processor.processBlock (buffer, midi);
        head.advance (blockSize, sampleRate);
        if (everyBlock)
            everyBlock();
    }
}

} // namespace thunder::test
