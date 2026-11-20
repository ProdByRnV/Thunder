#pragma once

#include "core/music/IntervalMapper.h"
#include "core/music/PitchTracker.h"

namespace thunder::dsp
{

// Interval mode's decision of which shift to apply (PRD FR-PITCH-003/004/010,
// DEC-014).
//
// - While the pitch tracker holds a clear note, the shift is that note's in-key
//   shift (`IntervalMapper::shiftFor`), so every note of a monophonic melody
//   lands in key.
// - When there is no clear note (a chord, noise, silence), the shift is the best
//   single fixed shift for the interval (`IntervalMapper::bestFixedShift`).
//
// Hysteresis, so the two don't flip back and forth:
// - the tracker's own: a note needs confidence 0.8 to start and is only lost
//   below 0.6;
// - here: after the tracker loses its note, the last note's shift is held for
//   kFallbackHoldSeconds before giving way to the fixed shift. The tracker
//   briefly reports no note at every legato note change (its window holds two
//   notes), and this keeps those moments on the per-note shift.
//
// Real-time safe: no allocation, no locks.
class IntervalFollower
{
public:
    static constexpr double kFallbackHoldSeconds = 0.25;

    // [NRT]
    void prepare (double sampleRate) noexcept;

    // [RT] Back to no note: the fixed shift until the tracker finds one.
    void reset() noexcept;

    // [RT] Advances by `numSamples` with the tracker's latest estimate and
    // returns the shift, in semitones, to apply now.
    int update (const music::PitchEstimate& estimate, const music::Key& key, params::PitchInterval interval,
                params::PitchDirection direction, int numSamples) noexcept;

    // True while following a note (including the hold after it is lost).
    bool isTracking() const noexcept { return tracking; }

    // The note being followed, or -1.
    int getNote() const noexcept { return tracking ? note : -1; }

private:
    int fixedShift (const music::Key& key, params::PitchInterval interval, params::PitchDirection direction) noexcept;

    int holdSamples = 0;
    int unvoicedSamples = 0;
    bool tracking = false;
    int note = -1;

    // bestFixedShift() is cheap, but only changes with the key or interval.
    music::Key cachedKey;
    params::PitchInterval cachedInterval = params::PitchInterval::Unison;
    params::PitchDirection cachedDirection = params::PitchDirection::Up;
    int cachedFixedShift = 0;
    bool cacheValid = false;
};

} // namespace thunder::dsp
