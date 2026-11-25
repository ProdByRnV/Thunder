#pragma once

#include "core/host/MusicalClock.h"
#include "core/params/ParamSnapshot.h"
#include "core/util/TripleBuffer.h"

#include <array>
#include <atomic>
#include <cstdint>

// The effective-parameter stage (ARCHITECTURE §5.10, PRD DEC-041): between the
// host's values and the modules. The performance controls write here, so no
// module changes for them:
//
//   host snapshot ─► Morph ─► Storm (+ Riser) ─► strikes ─► Variation ─► effective snapshot ─► modules
//
// Morph works once per host block (apply). The rest works per chunk
// (process), on a control grid of kControlInterval samples counted like
// Static's: each step's values come from the song position at the step's start
// (at the chain's output, so they line up with what is heard), the parameters
// and the strikes' glides, so they change at the same samples whatever the
// host's block size, and a bounce sounds exactly like playback. A rise's drop
// starts a step of its own, so the peak lands exactly on the bar line.
//
// With no performance control active the host snapshot passes straight through
// (the same object, untouched).

namespace thunder::perform
{

// Sound B for Morph (PRD FR-PLAY-002): a complete set of parameter values in
// real units. `present` false = no B, and Morph does nothing.
struct MorphSound
{
    bool present = false;
    std::array<float, params::kNumParams> values{};
};

// Whether Morph moves a parameter. Key, Scale and Output Mode describe the
// song, not the sound (FR-PRESET-006); the safety limiter protects the output
// whatever sound B is; Morph never moves itself; and a morph never presses a
// strike or starts a rise.
constexpr bool isMorphed (params::ParamId id) noexcept
{
    using params::ParamId;
    return id != ParamId::keyRoot && id != ParamId::keyScale && id != ParamId::outputMode &&
           id != ParamId::safetyLimiter && id != ParamId::morphAmount && id != ParamId::strikeClap &&
           id != ParamId::strikeScatter && id != ParamId::riserGo;
}

// The value `t` (0–1) of the way from `a` to `b`, in the control's own taper:
// linear controls in a straight line, logarithmic ones (frequencies, times)
// by equal ratios, whole-number controls rounded. Switches and choices change
// at the halfway point (the modules' own crossfades make that click-free).
// t ≤ 0 gives `a` and t ≥ 1 gives `b`, exactly.
float morphValue (const params::ParamDef& def, float a, float b, float t) noexcept;

// [RT] `out` = A (`a`) morphed `t` (0–1) of the way to `b`.
void applyMorph (const params::ParamSnapshot& a, const MorphSound& b, float t, params::ParamSnapshot& out) noexcept;

// What the editor shows of the stage: the values it sent in the last block.
struct PerformedValues
{
    params::ParamSnapshot values;
    bool moved = false; // false: the host's values went through untouched
};

class PerformanceStage
{
public:
    // Steps of the control grid, the same as Static's.
    static constexpr int kControlInterval = 256;

    // [NRT]
    void prepare (double sampleRate) noexcept;

    // [RT] Forgets the strikes' glides and any rise (a host reset or a new prepare).
    void reset() noexcept;

    // [MSG] Sets or clears sound B. One writer at a time (the processor holds a
    // lock); the audio thread picks it up at its next block.
    void setMorphSound (const MorphSound& sound) noexcept { morph.write (sound); }

    // [RT] Morph, once per host block: `host` itself when there is no B or
    // Morph is at 0, else a copy moved towards B.
    const params::ParamSnapshot& apply (const params::ParamSnapshot& host) noexcept;

    // [RT] Once per host block, after apply(): notices a press of Riser Go.
    void beginBlock (const params::ParamSnapshot& morphed) noexcept;

    // [RT] Whether Storm, a strike, a rise or Variation moves anything now
    // (then the engine ends chunks on the control grid).
    bool isActive (const params::ParamSnapshot& morphed) const noexcept;

    // [RT] `chunk` shortened so that a rise's drop starts a chunk of its own.
    int limitChunk (std::int64_t startSample, int chunk) const noexcept;

    // [RT] The values for a chunk of `numSamples` starting at `startSample`
    // (counted like Static's grid; it must not cross a grid line). `transport`
    // is the song position of the chain's output.
    const params::ParamSnapshot& process (const params::ParamSnapshot& morphed, const TransportState& transport,
                                          std::int64_t startSample, int numSamples) noexcept;

    // [RT] Hands the editor the values of the block's last chunk.
    void publish (const params::ParamSnapshot& values, bool moved) noexcept;

    // [MSG] The values the stage last sent. False (and `out` untouched) when no
    // block has run since the last call. One reader: the editor.
    bool readPublished (PerformedValues& out) noexcept;

    // [any thread] Starts a rise at the next block, even from a click shorter
    // than a host block (RISE in the editor).
    void triggerRiser() noexcept { riserTrigger.store (true, std::memory_order_release); }

    // [any thread] How far the current rise is (0–1, then 1 through the drop),
    // or −1 when none is running.
    float getRiserProgress() const noexcept { return riserProgress.load (std::memory_order_relaxed); }

    // [RT] Sound B as the audio thread last took it.
    const MorphSound& getMorphSound() const noexcept { return morph.get(); }

private:
    struct Rise
    {
        bool running = false;
        double start = 0.0; // quarter notes
        double end = 0.0;   // the bar line the peak lands on
        float from = 0.0f;  // the rise's value at the press (a press during a rise carries on from it)
    };

    // The rise's share of the way from the knob to 100% at `ppq`; ends the rise after its drop.
    float riseAt (double ppq, double secondsPerQuarter) noexcept;

    util::TripleBuffer<MorphSound> morph;
    params::ParamSnapshot effective; // Morph's result
    params::ParamSnapshot performed; // ... and the rest, for the current step

    double sampleRate = 44100.0;
    MusicalClock clock;
    Rise rise;
    float riseNow = 0.0f;      // the rise at the last step
    bool pressPending = false; // Riser Go pressed; starts at the next step
    bool goWasOn = false;
    bool primed = false; // the first block after a reset doesn't count Go as a press
    std::atomic<bool> riserTrigger{false};
    std::atomic<float> riserProgress{-1.0f};

    float clapGlide = 0.0f; // 0–1 towards the strike
    float scatterGlide = 0.0f;
    std::int64_t stepKey = -1;       // the sample the cached step starts at
    std::int64_t dropSample = -1;    // where the current rise's drop starts, when known
    double samplesPerQuarter = 24000.0;

    // Where the stage counts the song position from (see process()).
    std::int64_t anchorSample = -1;
    double anchorPpq = 0.0;
    double anchorBar = 0.0;
    double anchorSpq = 24000.0;
    double anchorBarLength = 4.0;

    util::TripleBuffer<PerformedValues> published;
};

} // namespace thunder::perform
