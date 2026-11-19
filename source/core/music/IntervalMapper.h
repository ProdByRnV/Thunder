#pragma once

#include "Scale.h"

namespace thunder::music
{

using params::PitchDirection;
using params::PitchInterval;

// Turns Interval mode's choices (interval and direction) into a pitch shift in
// semitones that keeps the layer in key (PRD FR-PITCH-003/004, DEC-014). Pure
// and allocation-free; safe on the audio thread.
//
// How an interval is counted:
// - 7-note scales: in scale steps. A 3rd up is two scale notes up, so it is a
//   major 3rd on some notes and a minor 3rd on others, and always in key.
// - Pentatonics: the interval of the 7-note scale they belong to (Major
//   Pentatonic → Major, Minor Pentatonic → Natural Minor). When that note isn't
//   in the pentatonic, the next pentatonic note in the same direction is used,
//   so the interval widens rather than collapsing towards the melody note.
// - Chromatic: every note is in key, so a fixed size: 2nd = 2 semitones,
//   3rd = 4, 4th = 5, 5th = 7, 6th = 9, 7th = 11 (the major-scale sizes).
// - Unison is 0 and Octave is 12 semitones in every scale.
//
// A melody note outside the key counts from the nearest note in the key (the
// lower one on a tie), so the shifted note still lands in key.
struct IntervalMapper
{
    // Size in semitones of each interval in a major key (and in Chromatic).
    static constexpr int nominalSemitones (PitchInterval interval) noexcept
    {
        constexpr std::array<int, 8> sizes{0, 2, 4, 5, 7, 9, 11, 12};
        return sizes[params::enumIndex (interval)];
    }

    // Scale steps for an interval: Unison 0, 2nd 1, … Octave 7.
    static constexpr int scaleSteps (PitchInterval interval) noexcept
    {
        return static_cast<int> (params::enumIndex (interval));
    }

    // The shift that moves `note` by the interval in the key. `note` + the
    // result is always in the key.
    static int shiftFor (const Key& key, int note, PitchInterval interval, PitchDirection direction) noexcept;

    // The single fixed shift that keeps the most notes of the scale in key for
    // the interval (PRD FR-PITCH-004): the fallback when the input isn't a
    // trackable melody. It is always the per-note shift of at least one scale
    // note. Ties go to the shift more scale notes use, then to the wider one.
    static int bestFixedShift (const Key& key, PitchInterval interval, PitchDirection direction) noexcept;

    // How many notes of one octave of the scale stay in key under a fixed shift.
    static int notesKeptInKey (const Key& key, int shift) noexcept;
};

} // namespace thunder::music
