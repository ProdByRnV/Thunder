#pragma once

#include "core/engine/Module.h"
#include "core/util/Smoother.h"

#include <array>
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
// Cloud mode is CloudSeedCore (Ghost Note Audio) with Thunder's own "Cloud"
// settings: large, lush and modulated. Thunder's four controls map onto it:
//   Mix       → wet/dry inside the module, equal-power
//   Decay     → reverb time, 0.4 s … 20 s on a logarithmic curve
//   Pre-delay → 0 … 250 ms before the reverb starts (never less than one
//               256-sample processing block, about 5 ms)
//   Damping   → how fast high frequencies die away: the cutoff of the low-pass
//               inside the reverb's feedback, 18 kHz (0%) down to 1 kHz (100%)
//
// TODO(PRD): Plate, Room and Shimmer modes (v1.0, ARCHITECTURE AQ-03); until
// then every mode uses Cloud.
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

    // The parameter mappings, exposed for tests.
    static double decaySeconds (float decayPercent) noexcept;
    static double dampingCutoffHz (float dampingPercent) noexcept;

    // CloudSeed always processes blocks of exactly this many samples (see process()).
    static constexpr int kReverbBlock = 256;

private:
    void applyReverbSettings (bool force) noexcept;

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
};

} // namespace thunder::dsp
