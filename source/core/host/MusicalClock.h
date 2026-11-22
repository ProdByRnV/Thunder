#pragma once

#include "TransportState.h"

#include <cstdint>

namespace thunder
{

// Where a chunk of audio sits in the song, for the tempo-synced modules (Time,
// Static, Shape; PRD FR-TIME-003, FR-SHAPE-004).
//
// While the host plays, the position is the host's, so a module whose state is
// a function of it stays in phase after loops, jumps and stops, and a bounce
// matches playback. While it is stopped (or there is no host transport), the
// clock keeps counting at the current tempo from where it was, so the modules
// still move when you play into Thunder with the transport stopped.
//
// The caller passes the transport of the audio the module processes, which is
// behind the host's by the latency of the modules before it
// (TransportState::delayedBy).
class MusicalClock
{
public:
    struct Chunk
    {
        double ppq = 0.0;      // quarter notes since the song start, at the chunk's first sample
        double barStart = 0.0; // quarter-note position of the bar containing ppq
        double quarterNotesPerBar = 4.0;
        double samplesPerQuarter = 24000.0;
        double seconds = 0.0; // song time at the chunk's first sample
        bool jumped = false;  // not where the previous chunk ended: a start, stop, loop or jump

        // [RT] The start of the bar containing `position` (quarter notes).
        double barStartAt (double position) const noexcept;
    };

    // [NRT]
    void prepare (double sampleRate) noexcept;

    // [RT] The next chunk will count as a jump.
    void reset() noexcept;

    // [RT] Where a chunk of `numSamples` samples starts, given the transport of
    // the audio in it.
    Chunk next (const TransportState& transport, int numSamples) noexcept;

private:
    double sampleRate = 44100.0;
    bool started = false;
    bool wasPlaying = false;
    double expectedPpq = 0.0; // where the previous chunk ended
    double expectedSeconds = 0.0;
    double barAnchor = 0.0; // a known bar start, for counting bars while stopped
};

} // namespace thunder
