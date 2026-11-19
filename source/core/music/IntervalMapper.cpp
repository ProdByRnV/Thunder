#include "IntervalMapper.h"

#include <array>
#include <cstdlib>

namespace thunder::music
{

namespace
{
int sign (PitchDirection direction) noexcept
{
    return direction == PitchDirection::Down ? -1 : 1;
}

// One octave of the key's notes, starting at its root above middle C.
template<typename Function> void forEachScaleNote (const Key& key, Function&& function)
{
    const auto base = 60 + key.root;
    for (int step = 0; step < kSemitonesPerOctave; ++step)
        if (key.contains (base + step))
            function (base + step);
}
} // namespace

int IntervalMapper::shiftFor (const Key& key, int note, PitchInterval interval, PitchDirection direction) noexcept
{
    const auto dir = sign (direction);

    if (key.scale == Scale::Chromatic)
        return dir * nominalSemitones (interval);

    // Count from the nearest note in the key, in the parent scale (the scale
    // itself for 7-note scales; it contains every pentatonic note).
    const auto from = key.nearestNote (note);
    const Key parent{key.root, parentScale (key.scale)};
    auto target = parent.stepFrom (from, dir * scaleSteps (interval));

    // Pentatonics only: move on to the next note of the key itself.
    while (!key.contains (target))
        target += dir;

    return target - note;
}

int IntervalMapper::notesKeptInKey (const Key& key, int shift) noexcept
{
    int kept = 0;
    forEachScaleNote (key, [&] (int note) { kept += key.contains (note + shift) ? 1 : 0; });
    return kept;
}

int IntervalMapper::bestFixedShift (const Key& key, PitchInterval interval, PitchDirection direction) noexcept
{
    if (key.scale == Scale::Chromatic)
        return sign (direction) * nominalSemitones (interval);

    // Candidates: the per-note shifts of the scale's notes, with how many notes
    // use each one.
    std::array<int, kSemitonesPerOctave> shifts{};
    std::array<int, kSemitonesPerOctave> uses{};
    int numCandidates = 0;

    forEachScaleNote (key,
                      [&] (int note)
                      {
                          const auto shift = shiftFor (key, note, interval, direction);
                          for (int i = 0; i < numCandidates; ++i)
                          {
                              if (shifts[static_cast<std::size_t> (i)] == shift)
                              {
                                  ++uses[static_cast<std::size_t> (i)];
                                  return;
                              }
                          }
                          shifts[static_cast<std::size_t> (numCandidates)] = shift;
                          uses[static_cast<std::size_t> (numCandidates)] = 1;
                          ++numCandidates;
                      });

    int best = shifts[0];
    int bestKept = -1, bestUses = -1;

    for (int i = 0; i < numCandidates; ++i)
    {
        const auto shift = shifts[static_cast<std::size_t> (i)];
        const auto use = uses[static_cast<std::size_t> (i)];
        const auto kept = notesKeptInKey (key, shift);

        const auto better = kept > bestKept || (kept == bestKept && use > bestUses) ||
                            (kept == bestKept && use == bestUses && std::abs (shift) > std::abs (best));
        if (better)
        {
            best = shift;
            bestKept = kept;
            bestUses = use;
        }
    }

    return best;
}

} // namespace thunder::music
