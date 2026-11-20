// Interval mode's choice between the per-note shift and the fixed fallback, and
// its hysteresis (PRD FR-PITCH-003/004/010).

#include "core/dsp/pitch/IntervalFollower.h"

#include <catch2/catch_test_macros.hpp>

using namespace thunder;
using namespace thunder::music;
using dsp::IntervalFollower;

namespace
{
constexpr double kRate = 48000.0;
constexpr int kChunk = 256;

PitchEstimate voicedOn (int note)
{
    PitchEstimate e;
    e.voiced = true;
    e.note = note;
    e.confidence = 0.95f;
    return e;
}

const PitchEstimate unvoiced{};

// C major, a 3rd up: C → E is +4, D → F is +3. The best fixed shift is +3: it
// keeps D, E, A and B in key, where +4 keeps only C, F and G.
const Key cMajor{0, Scale::Major};
} // namespace

TEST_CASE ("Interval mode follows each melody note with its own in-key shift", "[unit][pitch][interval]")
{
    IntervalFollower follower;
    follower.prepare (kRate);

    CHECK (follower.update (voicedOn (60), cMajor, PitchInterval::Third, PitchDirection::Up, kChunk) == 4);
    CHECK (follower.isTracking());
    CHECK (follower.getNote() == 60);

    CHECK (follower.update (voicedOn (62), cMajor, PitchInterval::Third, PitchDirection::Up, kChunk) == 3);
    CHECK (follower.getNote() == 62);

    // A 3rd down from E is C.
    CHECK (follower.update (voicedOn (64), cMajor, PitchInterval::Third, PitchDirection::Down, kChunk) == -4);
}

TEST_CASE ("Without a clear note Interval mode uses the best fixed shift", "[unit][pitch][interval]")
{
    IntervalFollower follower;
    follower.prepare (kRate);

    const auto fixed = IntervalMapper::bestFixedShift (cMajor, PitchInterval::Third, PitchDirection::Up);
    CHECK (follower.update (unvoiced, cMajor, PitchInterval::Third, PitchDirection::Up, kChunk) == fixed);
    CHECK_FALSE (follower.isTracking());
    CHECK (follower.getNote() == -1);

    // The fallback follows the key and interval.
    const Key aMinor{9, Scale::NaturalMinor};
    CHECK (follower.update (unvoiced, aMinor, PitchInterval::Fifth, PitchDirection::Down, kChunk) ==
           IntervalMapper::bestFixedShift (aMinor, PitchInterval::Fifth, PitchDirection::Down));
}

TEST_CASE ("Interval mode holds the last note's shift through brief gaps", "[unit][pitch][interval]")
{
    IntervalFollower follower;
    follower.prepare (kRate);

    // C, a 3rd up = +4, which differs from the fixed +3.
    const auto fixed = IntervalMapper::bestFixedShift (cMajor, PitchInterval::Third, PitchDirection::Up);
    REQUIRE (fixed == 3);
    REQUIRE (follower.update (voicedOn (60), cMajor, PitchInterval::Third, PitchDirection::Up, kChunk) == 4);

    // Just under the hold time with no clear note (a legato change, a breath):
    // still C's shift.
    const auto holdSamples = static_cast<int> (kRate * IntervalFollower::kFallbackHoldSeconds);
    int elapsed = 0;
    while (elapsed + kChunk < holdSamples)
    {
        CHECK (follower.update (unvoiced, cMajor, PitchInterval::Third, PitchDirection::Up, kChunk) == 4);
        elapsed += kChunk;
    }
    CHECK (follower.isTracking());

    // A note again before the hold ran out: straight back to following.
    CHECK (follower.update (voicedOn (65), cMajor, PitchInterval::Third, PitchDirection::Up, kChunk) == 4);

    // Past the hold time: the fixed shift.
    elapsed = 0;
    int last = 0;
    while (elapsed <= holdSamples)
    {
        last = follower.update (unvoiced, cMajor, PitchInterval::Third, PitchDirection::Up, kChunk);
        elapsed += kChunk;
    }
    CHECK_FALSE (follower.isTracking());
    CHECK (last == fixed);
}

TEST_CASE ("Reset forgets the followed note", "[unit][pitch][interval]")
{
    IntervalFollower follower;
    follower.prepare (kRate);

    follower.update (voicedOn (60), cMajor, PitchInterval::Third, PitchDirection::Up, kChunk);
    follower.reset();

    CHECK_FALSE (follower.isTracking());
    CHECK (follower.update (unvoiced, cMajor, PitchInterval::Third, PitchDirection::Up, kChunk) ==
           IntervalMapper::bestFixedShift (cMajor, PitchInterval::Third, PitchDirection::Up));
}
