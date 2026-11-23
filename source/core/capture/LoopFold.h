#pragma once

#include "CaptureTypes.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cstdint>

// Loop Mode (PRD FR-CAPT-004, ARCHITECTURE §8.3): a capture recorded with its
// tail becomes exactly N bars that loop seamlessly, with the tail folded back
// onto the start.
//
// What the tail holds depends on what played after the last bar, so the fold
// looks before it acts:
// - **A tail** (the melody stopped, the reverb and grains rang on): the tail is
//   added onto the start, wrapping round if it is longer than the loop. That
//   is exactly what the start sounds like when the loop repeats: the end rings
//   into it.
// - **A replay** (the DAW looped back and the melody played again, so what
//   followed the last bar is the start again, now with the end's tail in it):
//   adding it would play the start twice. Instead the recording after the last
//   bar replaces the start, crossfading back into the original start at its
//   end. Detected by the start and what followed the last bar matching
//   (normalised correlation of at least kReplayCorrelation over the first
//   0.5 s).
// Either way the folded tail fades out over its last 10 ms, in case it was cut
// short.

namespace thunder::capture
{

enum class FoldKind
{
    none,    // no tail to fold
    added,   // the tail was added onto the start
    replaced // a replay replaced the start
};

inline constexpr double kReplayCorrelation = 0.5;

// Folds `tail` onto `head`, which holds the first frames of an N-bar loop of
// `loopFrames` frames: at least min(loopFrames, tail length) of them. Pure.
FoldKind foldTail (juce::AudioBuffer<float>& head, std::int64_t loopFrames, const juce::AudioBuffer<float>& tail,
                   double sampleRate);

// [BG] Rewrites a finished capture file (N bars followed by the tail) as
// exactly `loopFrames` frames with the tail folded in, in the same format.
// Returns false (leaving `file` as it was) if it couldn't.
bool foldLoopFile (const juce::File& file, std::int64_t loopFrames, CaptureFormat format, FoldKind* kind = nullptr);

} // namespace thunder::capture
