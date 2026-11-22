#include "MusicalClock.h"

#include <cmath>

namespace thunder
{

namespace
{
// Hosts round positions and ramp tempo between blocks; anything within 2 ms of
// where the previous chunk ended counts as continuous playback.
constexpr double kJumpToleranceSeconds = 0.002;
} // namespace

double MusicalClock::Chunk::barStartAt (double position) const noexcept
{
    return barStart + std::floor ((position - barStart) / quarterNotesPerBar) * quarterNotesPerBar;
}

void MusicalClock::prepare (double newSampleRate) noexcept
{
    sampleRate = newSampleRate;
    reset();
}

void MusicalClock::reset() noexcept
{
    started = false;
    wasPlaying = false;
    expectedPpq = 0.0;
    expectedSeconds = 0.0;
    barAnchor = 0.0;
}

MusicalClock::Chunk MusicalClock::next (const TransportState& transport, int numSamples) noexcept
{
    Chunk chunk;
    chunk.quarterNotesPerBar = transport.quarterNotesPerBar();
    chunk.samplesPerQuarter = transport.samplesPerQuarterNote (sampleRate);

    if (transport.isPlaying)
    {
        chunk.ppq = transport.ppqPosition;
        chunk.barStart = transport.ppqLastBarStart;
        chunk.seconds = static_cast<double> (transport.samplePosition) / sampleRate;

        const auto tolerance = kJumpToleranceSeconds * transport.bpm / 60.0;
        chunk.jumped = !started || !wasPlaying || std::abs (chunk.ppq - expectedPpq) > tolerance;
        barAnchor = chunk.barStart;
    }
    else
    {
        // Stopped: carry on from where we were at the current tempo, counting
        // bars from the last bar line we knew.
        chunk.ppq = expectedPpq;
        chunk.seconds = expectedSeconds;
        chunk.barStart =
            barAnchor + std::floor ((chunk.ppq - barAnchor) / chunk.quarterNotesPerBar) * chunk.quarterNotesPerBar;
        chunk.jumped = !started;
    }

    started = true;
    wasPlaying = transport.isPlaying;
    expectedPpq = chunk.ppq + numSamples / chunk.samplesPerQuarter;
    expectedSeconds = chunk.seconds + numSamples / sampleRate;
    return chunk;
}

} // namespace thunder
