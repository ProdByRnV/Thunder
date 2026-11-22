#pragma once

#include "core/engine/Module.h"
#include "core/util/Smoother.h"

namespace thunder::dsp
{

// Duck (PRD §7.10, FR-DUCK-001/003; PRD DEC-051): keeps the layer behind the
// melody by turning it down while the melody is loud.
//
// - Key: the dry signal, delayed to line up with the layer (ProcessContext::
//   dryKey), so ducking follows the notes you hear. It is the internal dry
//   signal, so it works in Layer Only too (FR-DUCK-003).
// - Key level: the louder channel's peak, held between peaks by a 10 ms
//   release.
// - Wanted reduction, in decibels, grows with the key's level: none below
//   −50 dBFS, the full amount from −10 dBFS up, in a straight line (in dB)
//   between. The full amount is Amount × 24 dB.
// - The reduction follows the wanted one like a compressor's gain: down in
//   5 ms, back up with Release (10 ms … 1 s, the time constant in dB).
// - Spectral mode (v2.0) ducks like Envelope until then.
// - It runs after Auto Gain, not in the module chain, so the slow loudness
//   correction doesn't undo it (PRD DEC-051). It switches itself: off fades
//   the amount to zero over 20 ms, and fully off it costs nothing.
class DuckModule final : public Module
{
public:
    static constexpr float kMaxReductionDb = 24.0f;
    static constexpr float kQuietKeyDb = -50.0f; // no ducking below this
    static constexpr float kLoudKeyDb = -10.0f;  // the full amount from here up
    static constexpr double kAttackSeconds = 0.005;
    static constexpr double kPeakReleaseSeconds = 0.010; // the key detector between peaks

    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

    // The reduction for a key envelope at `keyDb` and Amount `share` (0 … 1).
    static float reductionDb (float keyDb, float share) noexcept;

    // The reduction applied to the last sample, for the meter (dB, ≥ 0).
    float getReductionDb() const noexcept { return lastReductionDb; }

private:
    double sampleRate = 44100.0;
    float envelope = 0.0f;
    float attackCoeff = 0.0f;
    float peakReleaseCoeff = 0.0f;
    util::LinearSmoother amount;
    util::LinearSmoother releaseMs;
    bool firstChunk = true;
    float lastReductionDb = 0.0f;
};

} // namespace thunder::dsp
