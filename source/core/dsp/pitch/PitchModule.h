#pragma once

#include "IntervalFollower.h"

#include "core/engine/Module.h"
#include "core/music/PitchTracker.h"
#include "core/music/Scale.h"
#include "core/util/LatencyDelay.h"
#include "core/util/Smoother.h"

#include <signalsmith-stretch/signalsmith-stretch.h>

#include <array>
#include <vector>

namespace thunder::dsp
{

// Block and hop sizes for the pitch engine (Signalsmith Stretch). Longer blocks
// resolve low notes better but add latency: the engine's latency is about one
// block (PRD DEC-016, ROADMAP T-0.1-11).
struct PitchEngineConfig
{
    double blockSeconds = 0.08;
    double intervalSeconds = 0.01;
    bool splitComputation = false; // spreads the FFT work across calls; adds one interval of latency
};

// Standard quality, chosen by measurement (tests/measure/PitchConfigMeasure.cpp):
// an 80 ms block keeps the latency at 80 ms, under the 100 ms target (PRD
// NFR-PERF-004), where the library's default (120 ms block) does not. The hop is
// 10 ms (8× overlap) rather than the default quarter-block: with 4× overlap,
// shorter blocks shifted notes up to ~20 cents sharp; with 8× the error stays
// within ~5 cents above 150 Hz and attacks smear less. The cost is CPU (about
// twice the default's).
inline constexpr PitchEngineConfig kStandardPitchConfig{0.08, 0.01, false};

// Shifts the pitch of the layer (PRD §7.2) with Signalsmith Stretch.
//
// Modes (`pitchMode`):
// - Octave: −2, −1, +1 or +2 octaves.
// - Semitone: −24…+24 semitones plus `pitchFine` cents (Fine applies only here).
// - Interval: a diatonic interval up or down that stays in the key set by Key
//   Root and Scale. The pitch tracker follows the melody note by note, and each
//   note gets its own in-key shift; without a clear note (chords, noise) the best
//   single fixed shift is used (IntervalFollower).
//
// Every change of shift, whether from automation, a new melody note or a mode
// change, glides over kShiftGlideSeconds. One Signalsmith instance does all
// modes: its overlapping 80 ms analysis windows blend the old and new
// transposition, which is what makes a mode change click-free. Two instances
// crossfaded would not work: a fresh instance is silent for its first 80 ms.
//
// Preserve Formants keeps the voice's spectral envelope where it was, so large
// upward shifts don't sound "chipmunky" (Signalsmith's formant compensation).
// It applies to upward shifts only; see process() for why.
//
// Pitch Mix blends the shifted signal with the unshifted input (delayed by the
// same latency, so the two line up), with an equal-power law.
class PitchModule final : public Module
{
public:
    explicit PitchModule (PitchEngineConfig engineConfig = kStandardPitchConfig);

    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

    // Input latency + output latency of the pitch engine: the delay of the
    // shifted signal relative to its input.
    int getLatencySamples() const noexcept override { return latencySamples; }

    const PitchEngineConfig& getConfig() const noexcept { return config; }

    // Shift changes (automation, new notes, mode changes) glide over this time
    // (PRD FR-PITCH-010: ~10–20 ms).
    static constexpr double kShiftGlideSeconds = 0.015;

    // The shift currently applied, in semitones (after gliding).
    float getCurrentShift() const noexcept { return appliedShift; }

    // The shift the module is gliding towards.
    float getTargetShift() const noexcept { return shift.target(); }

    // The key set by Key Root and Scale, read every chunk.
    const music::Key& getKey() const noexcept { return key; }

    // Interval mode: whether a melody note is being followed (otherwise the
    // fixed fallback shift is in use).
    bool isTrackingNote() const noexcept { return trackerActive && follower.isTracking(); }

private:
    static constexpr int kMaxChannels = 2;

    float targetShift (const ProcessContext& context, params::PitchMode mode) const noexcept;
    void applyFormants (bool preserve) noexcept;
    void mixWithUnshifted (juce::dsp::AudioBlock<float>& block, int numSamples) noexcept;

    PitchEngineConfig config;

    // Seeded so the engine never asks the OS for entropy. (Its randomness is only
    // used when time-stretching, which Thunder doesn't do; pitch output is
    // deterministic either way.)
    signalsmith::stretch::SignalsmithStretch<float> stretch{0L};

    std::array<std::vector<float>, kMaxChannels> input;
    int numChannels = kMaxChannels;
    int latencySamples = 0;

    util::LinearSmoother shift;
    float appliedShift = 0.0f;
    music::Key key;
    bool firstChunk = true;

    // Interval mode. The tracker only runs in Interval mode.
    music::PitchTracker tracker;
    IntervalFollower follower;
    bool trackerActive = false;
    int intervalShift = 0; // the follower's latest decision

    int formantState = -1; // -1 = not set yet, 0 = off, 1 = on

    // Pitch Mix: the unshifted input, delayed to line up with the shifted signal.
    util::LatencyDelay unshiftedDelay;
    util::LinearSmoother mix; // 0–1
};

} // namespace thunder::dsp
