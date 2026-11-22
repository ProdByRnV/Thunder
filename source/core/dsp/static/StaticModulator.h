#pragma once

#include "core/host/MusicalClock.h"
#include "core/params/ParamSnapshot.h"
#include "core/util/Smoother.h"

#include <array>
#include <cstdint>

namespace thunder::dsp
{

// Static (PRD §7.5, FR-STATIC-001…004; PRD DEC-050): random movement of a few
// controls, Thunder's "electric" motion. It isn't an audio stage: once per chunk
// it works out how far each target should move, the engine moves the targets'
// values before the modules read them (grain spreads, filter cutoffs) and pans
// and dips the layer itself (StaticStage).
//
// Every value is a function of where the audio is in the song, never of how
// long the plugin has been running:
// - The song is cut into steps of the rate (a note value when synced, 1/rate
//   seconds when free). Each step's random value comes from Thunder's seeded
//   generator (util::Rng) seeded with the step number and the target, so the
//   same place in the song always moves the same way: a bounce matches
//   playback, and a loop repeats its movement.
// - Smooth glides from each step's value to the next (smoothstep). Stepped
//   holds each step's value. Spark fires a burst on about 40% of the steps: a
//   jump to a random value that fades over about a sixth of a step.
// - While the host is stopped it keeps moving at its rate (MusicalClock).
//
// Each target has its own random sequence. What a value moves (at Amount 100%):
//   Position  adds up to 50% to Position Spread       (0 … +1, never less spread)
//   Pitch     adds up to 6 semitones to Pitch Spread  (0 … +1)
//   Filter    moves both cutoffs up to 2 octaves      (−1 … +1)
//   Pan       pans the layer fully left or right      (−1 … +1)
//   Level     dips the layer by up to 12 dB           (0 … 1, never louder)
// Switching Static or a target on or off fades its movement in or out over
// 30 ms.
//
// The values are worked out every kControlInterval samples, counted from the
// engine's last reset, and the engine splits its chunks on that grid while
// Static moves anything. So the targets change at the same samples whatever
// block size the host uses, and a bounce matches playback.
class StaticModulator
{
public:
    static constexpr int kControlInterval = 256; // 5.3 ms at 48 kHz: the engine's usual chunk

    enum Target
    {
        position,
        pitch,
        filter,
        pan,
        level,
        kNumTargets
    };

    // How far each target moves now: Amount, the on/off fades and the target
    // switches already applied. Ranges as in the table above.
    struct Frame
    {
        std::array<float, kNumTargets> values{};

        float operator[] (Target t) const noexcept { return values[static_cast<std::size_t> (t)]; }
        bool isZero() const noexcept;
    };

    static constexpr float kMaxPositionSpread = 50.0f; // percentage points
    static constexpr float kMaxPitchSpread = 6.0f;     // semitones
    static constexpr float kMaxFilterOctaves = 2.0f;
    static constexpr float kMaxLevelDipDb = 12.0f;

    // [NRT]
    void prepare (double sampleRate) noexcept;

    // [RT]
    void reset() noexcept;

    // [RT] Where the targets are for a chunk starting `sampleIndex` samples
    // after the engine's last reset. `transport` is the transport of the audio
    // the targets affect. A chunk must not cross a multiple of kControlInterval
    // while isActive() (the engine splits chunks there).
    const Frame& next (const TransportState& transport, const params::ParamSnapshot& params,
                       std::int64_t sampleIndex) noexcept;

    // Whether Static moves (or is fading) anything with these settings.
    bool isActive (const params::ParamSnapshot& params) const noexcept;

    // [RT] Moves the targets' parameter values in `effective` (a copy of the
    // host's snapshot) by `frame`, kept within each parameter's range.
    static void applyTo (const Frame& frame, params::ParamSnapshot& effective) noexcept;

    // [RT] The raw, bipolar (−1 … +1) value of `target` at `step` (song
    // position in steps) for a character. Pure; for tests.
    static float valueAt (params::StaticCharacter character, int target, double step) noexcept;

    const Frame& getFrame() const noexcept { return frame; }

private:
    float depthTarget (int target, const params::ParamSnapshot& params) const noexcept;

    MusicalClock clock;
    std::array<util::LinearSmoother, kNumTargets> depth; // Amount × switches, faded
    Frame frame;
    bool firstChunk = true;
    bool wasActive = false;
};

// Static's pan and level, applied to the layer after Auto Gain so the slow
// loudness correction doesn't flatten the level movement. Per-sample smoothing
// turns the per-chunk values into continuous gains.
class StaticStage
{
public:
    // [NRT]
    void prepare (double sampleRate) noexcept;

    // [RT]
    void reset() noexcept;

    // [RT] Pans (equal power, unity in the centre) and dips the stereo layer.
    void process (float* const* channels, int numSamples, const StaticModulator::Frame& frame) noexcept;

private:
    util::LinearSmoother pan;   // −1 … +1
    util::LinearSmoother level; // linear gain
    bool firstChunk = true;
};

} // namespace thunder::dsp
