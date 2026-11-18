#pragma once

#include "core/engine/Module.h"
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
// Current scope: a plain shift of `pitchSemitones` + `pitchFine` cents.
// TODO(PRD): Octave / Interval modes, formants and Pitch Mix (ROADMAP T-0.2-03…06).
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

    // The shift currently applied, in semitones (after smoothing).
    float getCurrentShift() const noexcept { return appliedShift; }

private:
    static constexpr int kMaxChannels = 2;

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
    bool firstChunk = true;
};

} // namespace thunder::dsp
