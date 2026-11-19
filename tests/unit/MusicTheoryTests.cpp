// Keys, scales, note maths and in-key intervals (PRD FR-KEY-001, FR-KEY-004,
// FR-PITCH-003/004; ROADMAP T-0.2-01, T-0.2-02).

#include "support/TestSignals.h"

#include "core/dsp/pitch/PitchModule.h"
#include "core/music/IntervalMapper.h"
#include "core/music/KeyParams.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cstdlib>
#include <set>
#include <vector>

using namespace thunder;
using namespace thunder::music;
using Catch::Matchers::WithinAbs;
using params::enumIndex;
using params::KeyRoot;
using params::ParamId;
using params::ParamSnapshot;

namespace
{
constexpr std::array kAllScales{Scale::Major,           Scale::NaturalMinor,    Scale::HarmonicMinor,
                                Scale::Dorian,          Scale::Phrygian,        Scale::Mixolydian,
                                Scale::MajorPentatonic, Scale::MinorPentatonic, Scale::Chromatic};
static_assert (kAllScales.size() == params::kScaleNames.size());

constexpr std::array kAllIntervals{PitchInterval::Unison,  PitchInterval::Second, PitchInterval::Third,
                                   PitchInterval::Fourth,  PitchInterval::Fifth,  PitchInterval::Sixth,
                                   PitchInterval::Seventh, PitchInterval::Octave};

constexpr std::array kBothDirections{PitchDirection::Up, PitchDirection::Down};

bool isSevenNote (Scale scale)
{
    return notesPerOctave (scale) == 7;
}

// How many scale steps lie between two in-key notes (signed).
int stepsBetween (const Key& key, int from, int to)
{
    int steps = 0;
    const int direction = to >= from ? 1 : -1;
    for (int note = from; note != to; note += direction)
        if (key.contains (note + direction))
            steps += direction;
    return steps;
}
} // namespace

TEST_CASE ("Pitch classes wrap for any note", "[unit][music]")
{
    CHECK (pitchClass (60) == 0);
    CHECK (pitchClass (69) == 9);
    CHECK (pitchClass (71) == 11);
    CHECK (pitchClass (-1) == 11);
    CHECK (pitchClass (-12) == 0);
    CHECK (pitchClass (127) == 7);
}

TEST_CASE ("Each scale has the notes of FR-KEY-004", "[unit][music]")
{
    const auto notesOf = [] (Key key)
    {
        std::vector<int> pcs;
        for (int pc = 0; pc < 12; ++pc)
            if (key.contains (pc))
                pcs.push_back (pc);
        return pcs;
    };

    // In C, so the pitch classes are the scale's steps.
    CHECK (notesOf ({0, Scale::Major}) == std::vector<int>{0, 2, 4, 5, 7, 9, 11});
    CHECK (notesOf ({0, Scale::NaturalMinor}) == std::vector<int>{0, 2, 3, 5, 7, 8, 10});
    CHECK (notesOf ({0, Scale::HarmonicMinor}) == std::vector<int>{0, 2, 3, 5, 7, 8, 11});
    CHECK (notesOf ({0, Scale::Dorian}) == std::vector<int>{0, 2, 3, 5, 7, 9, 10});
    CHECK (notesOf ({0, Scale::Phrygian}) == std::vector<int>{0, 1, 3, 5, 7, 8, 10});
    CHECK (notesOf ({0, Scale::Mixolydian}) == std::vector<int>{0, 2, 4, 5, 7, 9, 10});
    CHECK (notesOf ({0, Scale::MajorPentatonic}) == std::vector<int>{0, 2, 4, 7, 9});
    CHECK (notesOf ({0, Scale::MinorPentatonic}) == std::vector<int>{0, 3, 5, 7, 10});
    CHECK (notesOf ({0, Scale::Chromatic}).size() == 12u);

    // A natural minor is the white keys; E Phrygian too.
    const std::vector<int> whiteKeys{0, 2, 4, 5, 7, 9, 11};
    CHECK (notesOf ({9, Scale::NaturalMinor}) == whiteKeys);
    CHECK (notesOf ({4, Scale::Phrygian}) == whiteKeys);
    CHECK (notesOf ({2, Scale::Dorian}) == whiteKeys);
    CHECK (notesOf ({7, Scale::Mixolydian}) == whiteKeys);

    for (const auto scale : kAllScales)
    {
        INFO (params::kScaleNames[enumIndex (scale)]);
        const auto expected = scale == Scale::Chromatic                                              ? 12
                              : (scale == Scale::MajorPentatonic || scale == Scale::MinorPentatonic) ? 5
                                                                                                     : 7;
        CHECK (notesPerOctave (scale) == expected);

        // Every key contains its own root, in every octave.
        for (int root = 0; root < 12; ++root)
            for (int octave = -1; octave <= 9; ++octave)
                CHECK (Key{root, scale}.contains (root + 12 * octave));

        // Pentatonics sit inside their parent scale.
        const Key parent{0, parentScale (scale)};
        for (int pc = 0; pc < 12; ++pc)
            if (Key{0, scale}.contains (pc))
                CHECK (parent.contains (pc));
    }
}

TEST_CASE ("The nearest note in a key", "[unit][music]")
{
    const Key cMajor{0, Scale::Major};

    // In-key notes are their own nearest note.
    for (const auto note : {60, 62, 64, 65, 67, 69, 71, 72})
        CHECK (cMajor.nearestNote (note) == note);

    // Equally near: the lower one.
    CHECK (cMajor.nearestNote (61) == 60); // C# → C
    CHECK (cMajor.nearestNote (63) == 62); // D# → D
    CHECK (cMajor.nearestNote (70) == 69); // A# → A

    // C major pentatonic C D E G A: F is nearer E, F# nearer G.
    const Key cMajorPent{0, Scale::MajorPentatonic};
    CHECK (cMajorPent.nearestNote (65) == 64);
    CHECK (cMajorPent.nearestNote (66) == 67);
    CHECK (cMajorPent.nearestNote (70) == 69); // A# → A (1 away; C is 2)
    CHECK (cMajorPent.nearestNote (71) == 72); // B → C

    for (const auto scale : kAllScales)
        for (int root = 0; root < 12; ++root)
            for (int note = 0; note < 128; ++note)
            {
                const Key key{root, scale};
                const auto nearest = key.nearestNote (note);
                CHECK (key.contains (nearest));
                CHECK (std::abs (nearest - note) <= 2);
            }
}

TEST_CASE ("Stepping through a key", "[unit][music]")
{
    const Key cMajor{0, Scale::Major};
    CHECK (cMajor.stepFrom (64, 2) == 67);  // E + 2 steps = G
    CHECK (cMajor.stepFrom (60, -1) == 59); // C − 1 step = B below
    CHECK (cMajor.stepFrom (60, 7) == 72);  // a whole octave of steps
    CHECK (cMajor.stepFrom (60, 0) == 60);

    const Key aMinorPent{9, Scale::MinorPentatonic}; // A C D E G
    CHECK (aMinorPent.stepFrom (57, 1) == 60);
    CHECK (aMinorPent.stepFrom (57, 5) == 69);
    CHECK (aMinorPent.stepFrom (57, -1) == 55);
}

TEST_CASE ("Notes and frequencies", "[unit][music]")
{
    CHECK_THAT (noteToFrequency (69.0), WithinAbs (440.0, 1e-9));
    CHECK_THAT (noteToFrequency (81.0), WithinAbs (880.0, 1e-9));
    CHECK_THAT (noteToFrequency (60.0), WithinAbs (261.6256, 1e-3));
    CHECK_THAT (frequencyToNote (440.0), WithinAbs (69.0, 1e-9));
    CHECK_THAT (frequencyToNote (110.0), WithinAbs (45.0, 1e-9));
    CHECK_THAT (frequencyToNote (noteToFrequency (60.37)), WithinAbs (60.37, 1e-9));
}

TEST_CASE ("Interval shifts on familiar examples", "[unit][music][pitch]")
{
    using enum PitchInterval;
    const auto up = PitchDirection::Up;
    const auto down = PitchDirection::Down;

    const Key cMajor{0, Scale::Major};
    CHECK (IntervalMapper::shiftFor (cMajor, 60, Third, up) == 4);     // C → E
    CHECK (IntervalMapper::shiftFor (cMajor, 64, Third, up) == 3);     // E → G
    CHECK (IntervalMapper::shiftFor (cMajor, 71, Third, up) == 3);     // B → D
    CHECK (IntervalMapper::shiftFor (cMajor, 64, Third, down) == -4);  // E → C
    CHECK (IntervalMapper::shiftFor (cMajor, 67, Fifth, up) == 7);     // G → D
    CHECK (IntervalMapper::shiftFor (cMajor, 71, Fifth, up) == 6);     // B → F (diminished, but in key)
    CHECK (IntervalMapper::shiftFor (cMajor, 65, Fourth, up) == 6);    // F → B
    CHECK (IntervalMapper::shiftFor (cMajor, 60, Sixth, up) == 9);     // C → A
    CHECK (IntervalMapper::shiftFor (cMajor, 60, Seventh, up) == 11);  // C → B
    CHECK (IntervalMapper::shiftFor (cMajor, 62, Second, down) == -2); // D → C

    const Key aMinor{9, Scale::NaturalMinor};
    CHECK (IntervalMapper::shiftFor (aMinor, 57, Third, up) == 3); // A → C
    CHECK (IntervalMapper::shiftFor (aMinor, 60, Third, up) == 4); // C → E
    CHECK (IntervalMapper::shiftFor (aMinor, 64, Third, up) == 3); // E → G

    const Key aHarmonicMinor{9, Scale::HarmonicMinor};
    CHECK (IntervalMapper::shiftFor (aHarmonicMinor, 64, Third, up) == 4);  // E → G#
    CHECK (IntervalMapper::shiftFor (aHarmonicMinor, 65, Second, up) == 3); // F → G# (augmented 2nd)

    // Pentatonics use their parent scale's interval, or the next pentatonic note.
    const Key cMajorPent{0, Scale::MajorPentatonic};
    CHECK (IntervalMapper::shiftFor (cMajorPent, 60, Third, up) == 4);    // C → E
    CHECK (IntervalMapper::shiftFor (cMajorPent, 62, Third, up) == 5);    // D → (F) → G
    CHECK (IntervalMapper::shiftFor (cMajorPent, 64, Second, up) == 3);   // E → (F) → G
    CHECK (IntervalMapper::shiftFor (cMajorPent, 62, Third, down) == -5); // D → (B) → A

    const Key aMinorPent{9, Scale::MinorPentatonic};
    CHECK (IntervalMapper::shiftFor (aMinorPent, 60, Third, up) == 4); // C → E
    CHECK (IntervalMapper::shiftFor (aMinorPent, 57, Fifth, up) == 7); // A → E

    // Chromatic: fixed sizes.
    const Key chromatic{0, Scale::Chromatic};
    for (int note = 50; note < 70; ++note)
    {
        CHECK (IntervalMapper::shiftFor (chromatic, note, Third, up) == 4);
        CHECK (IntervalMapper::shiftFor (chromatic, note, Fifth, down) == -7);
    }

    // A melody note outside the key counts from the nearest note in it.
    CHECK (IntervalMapper::shiftFor (cMajor, 61, Third, up) == 3);   // C# → (C) → E
    CHECK (IntervalMapper::shiftFor (cMajor, 61, Unison, up) == -1); // C# → C
    CHECK (IntervalMapper::shiftFor (cMajor, 66, Octave, up) == 11); // F# → (F) → F an octave up
}

TEST_CASE ("Every interval lands in key, from every note, in every key", "[unit][music][pitch]")
{
    for (const auto scale : kAllScales)
    {
        for (int root = 0; root < 12; ++root)
        {
            const Key key{root, scale};
            for (const auto interval : kAllIntervals)
            {
                for (const auto direction : kBothDirections)
                {
                    const int dir = direction == PitchDirection::Up ? 1 : -1;
                    int wrong = 0, wrongSteps = 0, wrongSize = 0;

                    for (int note = 24; note <= 108; ++note)
                    {
                        const auto shift = IntervalMapper::shiftFor (key, note, interval, direction);
                        const auto target = note + shift;

                        // FR-PITCH-003: the shifted note is always in key …
                        wrong += key.contains (target) ? 0 : 1;

                        // … close to the interval's nominal size …
                        wrongSize += std::abs (shift - dir * IntervalMapper::nominalSemitones (interval)) <= 3 ? 0 : 1;

                        if (!key.contains (note))
                            continue;

                        // … and, from an in-key note in a 7-note scale, exactly the
                        // interval's number of scale steps away.
                        if (isSevenNote (scale))
                            wrongSteps +=
                                stepsBetween (key, note, target) == dir * IntervalMapper::scaleSteps (interval) ? 0 : 1;

                        // Unison and Octave are exact for every in-key note.
                        if (interval == PitchInterval::Unison)
                            wrongSteps += shift == 0 ? 0 : 1;
                        if (interval == PitchInterval::Octave)
                            wrongSteps += shift == dir * 12 ? 0 : 1;
                    }

                    INFO (params::kScaleNames[enumIndex (scale)] << " root " << root << " "
                                                                 << params::kPitchIntervalNames[enumIndex (interval)]
                                                                 << (dir > 0 ? " up" : " down"));
                    CHECK (wrong == 0);
                    CHECK (wrongSteps == 0);
                    CHECK (wrongSize == 0);
                }
            }
        }
    }
}

TEST_CASE ("Wider intervals never give smaller shifts", "[unit][music][pitch]")
{
    for (const auto scale : kAllScales)
        for (int root = 0; root < 12; ++root)
            for (int note = 48; note < 72; ++note)
            {
                const Key key{root, scale};
                if (!key.contains (note))
                    continue;

                int previousUp = -1, previousDown = 1;
                for (const auto interval : kAllIntervals)
                {
                    const auto up = IntervalMapper::shiftFor (key, note, interval, PitchDirection::Up);
                    const auto down = IntervalMapper::shiftFor (key, note, interval, PitchDirection::Down);
                    CHECK (up >= previousUp);
                    CHECK (down <= previousDown);
                    previousUp = up;
                    previousDown = down;
                }
            }
}

TEST_CASE ("The best fixed shift keeps the most scale notes in key", "[unit][music][pitch]")
{
    using enum PitchInterval;
    const auto up = PitchDirection::Up;
    const auto down = PitchDirection::Down;

    // PRD DEC-014: a 3rd up in A minor. +3 keeps 4 of 7 notes in key, +4 only 3.
    const Key aMinor{9, Scale::NaturalMinor};
    CHECK (IntervalMapper::bestFixedShift (aMinor, Third, up) == 3);
    CHECK (IntervalMapper::notesKeptInKey (aMinor, 3) == 4);
    CHECK (IntervalMapper::notesKeptInKey (aMinor, 4) == 3);

    const Key cMajor{0, Scale::Major};
    CHECK (IntervalMapper::bestFixedShift (cMajor, Third, up) == 3);
    CHECK (IntervalMapper::bestFixedShift (cMajor, Third, down) == -3);
    CHECK (IntervalMapper::bestFixedShift (cMajor, Fifth, up) == 7);
    CHECK (IntervalMapper::bestFixedShift (cMajor, Fourth, up) == 5);
    CHECK (IntervalMapper::bestFixedShift (cMajor, Octave, up) == 12);
    CHECK (IntervalMapper::bestFixedShift (cMajor, Octave, down) == -12);
    CHECK (IntervalMapper::bestFixedShift (cMajor, Unison, up) == 0);

    const Key chromatic{5, Scale::Chromatic};
    CHECK (IntervalMapper::bestFixedShift (chromatic, Third, up) == 4);
    CHECK (IntervalMapper::bestFixedShift (chromatic, Sixth, down) == -9);

    // Against brute force: no per-note shift of the interval keeps more notes
    // in key, and the choice is always one of them.
    for (const auto scale : kAllScales)
        for (int root = 0; root < 12; ++root)
            for (const auto interval : kAllIntervals)
                for (const auto direction : kBothDirections)
                {
                    const Key key{root, scale};
                    const auto best = IntervalMapper::bestFixedShift (key, interval, direction);

                    std::set<int> perNote;
                    for (int note = 60; note < 72; ++note)
                        if (key.contains (note))
                            perNote.insert (IntervalMapper::shiftFor (key, note, interval, direction));

                    INFO (params::kScaleNames[enumIndex (scale)] << " root " << root << " interval "
                                                                 << enumIndex (interval));
                    CHECK (perNote.count (best) == 1u);
                    for (const auto candidate : perNote)
                        CHECK (IntervalMapper::notesKeptInKey (key, best) >=
                               IntervalMapper::notesKeptInKey (key, candidate));
                }
}

TEST_CASE ("Key Root and Scale parameters set the key", "[unit][music][params]")
{
    // Defaults (PRD §9.2): C, Natural Minor.
    auto snapshot = ParamSnapshot::defaults();
    CHECK (keyFromParams (snapshot) == Key{0, Scale::NaturalMinor});

    snapshot.set (ParamId::keyRoot, static_cast<float> (enumIndex (KeyRoot::A)));
    snapshot.set (ParamId::keyScale, static_cast<float> (enumIndex (Scale::Dorian)));
    CHECK (keyFromParams (snapshot) == Key{9, Scale::Dorian});

    // Out-of-range values are clamped rather than trusted.
    snapshot.set (ParamId::keyRoot, 40.0f);
    snapshot.set (ParamId::keyScale, -3.0f);
    CHECK (keyFromParams (snapshot) == Key{11, Scale::Major});
}

TEST_CASE ("The pitch module follows Key Root and Scale", "[unit][music][pitch]")
{
    dsp::PitchModule pitch;
    pitch.prepare ({48000.0, 256, 2});

    auto snapshot = ParamSnapshot::defaults();
    snapshot.set (ParamId::keyRoot, static_cast<float> (enumIndex (KeyRoot::FSharp)));
    snapshot.set (ParamId::keyScale, static_cast<float> (enumIndex (Scale::HarmonicMinor)));

    test::runModule (pitch, std::vector<float> (512, 0.0f), snapshot);
    CHECK (pitch.getKey() == Key{6, Scale::HarmonicMinor});

    snapshot.set (ParamId::keyRoot, static_cast<float> (enumIndex (KeyRoot::D)));
    snapshot.set (ParamId::keyScale, static_cast<float> (enumIndex (Scale::MajorPentatonic)));
    test::runModule (pitch, std::vector<float> (256, 0.0f), snapshot);
    CHECK (pitch.getKey() == Key{2, Scale::MajorPentatonic});
}
