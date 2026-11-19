#pragma once

#include "core/params/Enums.h"

#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>

// Keys, scales and note maths (PRD FR-KEY-001, FR-KEY-004). Pure functions with
// no state; safe on any thread.
//
// Notes are MIDI note numbers (60 = middle C, 69 = A4 = 440 Hz). A pitch class
// is a note without its octave: 0 = C … 11 = B.

namespace thunder::music
{

using params::Scale;

inline constexpr int kSemitonesPerOctave = 12;

// The pitch class of a note (0 = C … 11 = B), also for negative notes.
constexpr int pitchClass (int note) noexcept
{
    return ((note % kSemitonesPerOctave) + kSemitonesPerOctave) % kSemitonesPerOctave;
}

namespace detail
{
constexpr std::uint16_t pitchClassMask (std::initializer_list<int> steps) noexcept
{
    std::uint16_t mask = 0;
    for (const auto step : steps)
        mask = static_cast<std::uint16_t> (static_cast<unsigned> (mask) | (1u << static_cast<unsigned> (step)));
    return mask;
}
} // namespace detail

// Each scale as a set of semitone steps above its root (bit n = n semitones),
// in params::Scale order.
inline constexpr std::array<std::uint16_t, 9> kScaleMasks{
    detail::pitchClassMask ({0, 2, 4, 5, 7, 9, 11}),                  // Major
    detail::pitchClassMask ({0, 2, 3, 5, 7, 8, 10}),                  // Natural Minor
    detail::pitchClassMask ({0, 2, 3, 5, 7, 8, 11}),                  // Harmonic Minor
    detail::pitchClassMask ({0, 2, 3, 5, 7, 9, 10}),                  // Dorian
    detail::pitchClassMask ({0, 1, 3, 5, 7, 8, 10}),                  // Phrygian
    detail::pitchClassMask ({0, 2, 4, 5, 7, 9, 10}),                  // Mixolydian
    detail::pitchClassMask ({0, 2, 4, 7, 9}),                         // Major Pentatonic
    detail::pitchClassMask ({0, 3, 5, 7, 10}),                        // Minor Pentatonic
    detail::pitchClassMask ({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11})}; // Chromatic
static_assert (kScaleMasks.size() == params::kScaleNames.size());

constexpr std::uint16_t scaleMask (Scale scale) noexcept
{
    return kScaleMasks[params::enumIndex (scale)];
}

// The number of notes per octave in a scale.
constexpr int notesPerOctave (Scale scale) noexcept
{
    int count = 0;
    for (auto mask = scaleMask (scale); mask != 0; mask = static_cast<std::uint16_t> (mask & (mask - 1)))
        ++count;
    return count;
}

// The scale whose intervals a scale's harmony uses. The pentatonics have no
// 3rds or 7ths of their own on every degree, so their intervals come from the
// 7-note scale they are part of (see IntervalMapper).
constexpr Scale parentScale (Scale scale) noexcept
{
    switch (scale)
    {
        case Scale::MajorPentatonic:
            return Scale::Major;
        case Scale::MinorPentatonic:
            return Scale::NaturalMinor;
        case Scale::Major:
        case Scale::NaturalMinor:
        case Scale::HarmonicMinor:
        case Scale::Dorian:
        case Scale::Phrygian:
        case Scale::Mixolydian:
        case Scale::Chromatic:
            break;
    }
    return scale;
}

// A key: a root pitch class and a scale (PRD §9.2: Key Root and Scale).
struct Key
{
    int root = 0; // pitch class of the tonic, 0 = C
    Scale scale = Scale::NaturalMinor;

    constexpr bool contains (int note) const noexcept
    {
        return ((static_cast<unsigned> (scaleMask (scale)) >> static_cast<unsigned> (pitchClass (note - root))) & 1u) !=
               0;
    }

    // The nearest note in the key: `note` itself if it is in the key. When two
    // are equally near, the lower one (a deterministic, documented choice).
    constexpr int nearestNote (int note) const noexcept
    {
        for (int distance = 0; distance < kSemitonesPerOctave; ++distance)
        {
            if (contains (note - distance))
                return note - distance;
            if (contains (note + distance))
                return note + distance;
        }
        return note; // unreachable: every scale contains its root
    }

    // The note `steps` scale steps above `note` (below when negative), counting
    // only notes in the key. `note` should be in the key; if it isn't, the
    // steps count from it all the same.
    constexpr int stepFrom (int note, int steps) const noexcept
    {
        const int direction = steps < 0 ? -1 : 1;
        for (int remaining = steps < 0 ? -steps : steps; remaining > 0; --remaining)
        {
            note += direction;
            while (!contains (note))
                note += direction;
        }
        return note;
    }

    constexpr bool operator== (const Key&) const noexcept = default;
};

// Equal temperament, A4 (note 69) = 440 Hz. Notes may be fractional.
inline double noteToFrequency (double note) noexcept
{
    return 440.0 * std::exp2 ((note - 69.0) / 12.0);
}

inline double frequencyToNote (double frequencyHz) noexcept
{
    return 69.0 + 12.0 * std::log2 (frequencyHz / 440.0);
}

} // namespace thunder::music
