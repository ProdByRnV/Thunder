#pragma once

#include "FdnReverb.h"

#include "core/engine/Module.h"
#include "core/util/Smoother.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace Cloudseed
{
class ReverbController;
}

namespace thunder::dsp
{

// Reverb on the layer (PRD §7.6).
//
// Four modes (FR-SPACE-001), each its own engine:
// - Cloud: CloudSeedCore (Ghost Note Audio) with Thunder's own settings:
//   large, lush and modulated.
// - Plate, Room and Shimmer: Thunder's FdnReverb (PRD DEC-051, AQ-03): bright
//   and dense; small and short; large with octave-up shimmer.
// Switching mode fades the old engine's input out and the new one's in over
// 30 ms; the old engine's tail rings out on its own, so nothing is cut off.
// An engine whose input is off and whose output has gone quiet rests (no CPU).
//
// Thunder's four controls map onto every mode:
//   Mix       → wet/dry inside the module, equal-power
//   Decay     → reverb time on a logarithmic curve, each mode its own range
//               (FR-SPACE-002): Cloud 0.4–20 s, Plate 0.5–8 s, Room 0.2–3 s,
//               Shimmer 1–20 s
//   Pre-delay → 0 … 250 ms before the reverb starts (Cloud: never less than
//               one 256-sample processing block, about 5 ms)
//   Damping   → how fast high frequencies die away: the cutoff of the low-pass
//               inside the reverb's feedback, 18 kHz (0%) down to 1 kHz (100%)
class SpaceModule final : public Module
{
public:
    SpaceModule();
    ~SpaceModule() override;

    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

    double getTailSeconds() const noexcept override;

    // Clearing CloudSeed's delay lines takes ~10 ms, longer than a 256-sample
    // chunk at 48 kHz.
    bool canResetInstantly() const noexcept override { return false; }

    // True once input and reverb output have both been below −120 dB for longer
    // than the longest pre-delay: nothing is left in the delay lines worth
    // clearing, so reset() skips the slow clear (PRD DEC-043).
    bool isQuiet() const noexcept { return quietSamples >= quietThreshold; }

    // −120 dBFS: below this the reverb's input and output count as silence.
    static constexpr float kQuietLevel = 1.0e-6f;

    // The parameter mappings, exposed for tests. decaySeconds (%) is Cloud's.
    static double decaySeconds (float decayPercent) noexcept;
    static double decaySeconds (params::SpaceMode mode, float decayPercent) noexcept;
    static double dampingCutoffHz (float dampingPercent) noexcept;

    // Whether a mode's engine is running (selected, fading, or ringing out).
    bool isEngineRunning (params::SpaceMode mode) const noexcept;

    static constexpr int kModes = 4;
    static constexpr double kModeFadeSeconds = 0.030;

    // CloudSeed always processes blocks of exactly this many samples (see process()).
    static constexpr int kReverbBlock = 256;

private:
    void applyReverbSettings (bool force) noexcept;
    void processCloud (const float* left, const float* right, int numSamples) noexcept;
    void processPlates (const float* left, const float* right, int numSamples) noexcept;

    // Plate, Room and Shimmer, in SpaceMode order after Cloud.
    std::array<FdnReverb, kModes - 1> fdn;
    std::array<util::LinearSmoother, kModes> engineInput; // 0 … 1 per mode
    std::array<std::int64_t, kModes> engineQuiet{};       // samples its output has been quiet
    std::array<std::vector<float>, 2> engineIn, engineOut;
    bool firstEngineChunk = true;

    std::unique_ptr<Cloudseed::ReverbController> reverb;
    int preparedSampleRate = 0;
    double sampleRateHz = 48000.0;

    std::array<std::array<float, kReverbBlock>, 2> reverbIn{}, reverbOut{};
    int blockPosition = 0;
    std::array<std::vector<float>, 2> wet;

    // Smoothed per chunk; pushed into the reverb only when they move.
    util::LinearSmoother decay, preDelayMs, damping;
    float appliedDecay = -1.0f, appliedPreDelay = -1.0f, appliedDamping = -1.0f;

    util::LinearSmoother mix; // 0..1, per sample
    bool firstChunk = true;

    std::int64_t quietSamples = 0;   // consecutive samples with input and output below kQuietLevel
    std::int64_t quietThreshold = 1; // the longest pre-delay plus a reverb block, in samples
};

} // namespace thunder::dsp
