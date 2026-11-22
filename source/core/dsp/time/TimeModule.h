#pragma once

#include "core/dsp/grains/GrainBuffer.h"
#include "core/engine/Module.h"
#include "core/host/MusicalClock.h"

#include <array>
#include <cstdint>
#include <vector>

namespace thunder::dsp
{

// Halftime (PRD §7.3, FR-TIME-001…003; PRD DEC-050).
//
// Each bar of the song is a segment. Within a segment the layer plays the
// segment's audio from its start at 1/2 or 1/4 speed: t samples into the bar
// you hear what came in t × speed samples into it. So at 1/2 the first half of
// every bar fills the whole bar, at 1/4 its first beat (in 4/4) does. The pitch
// is kept: only the timing changes.
//
// How (time-scale modification by WSOLA: Verhelst & Roelands, "An overlap-add
// technique based on waveform similarity (WSOLA) for high quality time-scale
// modification of speech", ICASSP 1993):
// - Grains of ~40 ms, Hann-windowed, start every ~20 ms (50% overlap, so the
//   windows sum to exactly 1). Each grain plays its source at normal speed,
//   which keeps the pitch; the grains' starting points advance at the slower
//   speed, which stretches the time.
// - Each new grain starts within ±10 ms of where the slow playhead is, at the
//   point whose preceding waveform best matches the way the previous grain
//   would have continued, so overlapping grains add up in phase instead of
//   beating against each other.
// - No latency: a grain reads its source as it plays, and its source is never
//   ahead of the input (the playhead runs slower than the input and starts at
//   the segment start, which is "now"). The first grain of a segment is the
//   input itself.
// - Segments start on the song's bar lines while the host plays, and on bars
//   counted at its tempo while it is stopped (MusicalClock). A jump or a loop
//   starts a new segment there; the next bar line is where the grid takes over
//   again.
// - At each segment start the previous segment's voice keeps playing and
//   crossfades (equal power) into the new one. Smooth sets the length: 2 ms at
//   0% (just enough to avoid a click) up to an eighth of the bar at 100%.
// - Speed changes apply from the next segment.
// - Bars longer than 8 s (very slow tempos) are cut into 8 s segments; that
//   bounds the history the module keeps.
class TimeModule final : public Module
{
public:
    static constexpr double kMaxSegmentSeconds = 8.0;

    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

    // When it stops getting input, the rest of the bar still plays from the
    // segment's history.
    double getTailSeconds() const noexcept override { return kMaxSegmentSeconds; }

    // For tests: how many segments have started since the last reset.
    std::int64_t getSegmentsStarted() const noexcept { return segmentsStarted; }

    int getGrainLength() const noexcept { return grainLength; }

private:
    struct Grain
    {
        std::int64_t source = 0; // absolute index of the grain's first sample
        int age = 0;             // samples it has played
    };

    struct Voice
    {
        bool active = false;
        std::int64_t segmentStart = 0;
        double speed = 0.5;
        Grain current, previous;
    };

    void startSegment (std::int64_t now, double speed, double crossfadeSamples) noexcept;
    void renderSample (std::int64_t now, float& left, float& right) noexcept;
    void renderVoice (Voice& voice, std::int64_t now, float& left, float& right) noexcept;
    std::int64_t chooseSource (const Voice& voice, std::int64_t now) const noexcept;
    float mid (std::int64_t index) const noexcept;
    float similarity (std::int64_t candidate, std::int64_t reference) const noexcept;

    double sampleRate = 44100.0;
    int numChannels = 2;
    GrainBuffer history;
    MusicalClock clock;
    std::vector<float> window; // periodic Hann, grainLength long

    int grainLength = 2;
    int hop = 1;
    int searchRadius = 1;
    int matchLength = 1;
    int matchStep = 1;
    std::int64_t maxSegmentSamples = 1;

    std::array<Voice, 2> voices;
    int live = 0;              // the voice of the current segment
    double fadePosition = 1.0; // 0 … 1 while the previous voice fades out
    double fadeStep = 1.0;
    std::int64_t lastSegmentStart = -1;
    std::int64_t segmentsStarted = 0;
};

} // namespace thunder::dsp
