#pragma once

#include "Svf.h"

#include "core/engine/Module.h"
#include "core/util/Smoother.h"

namespace thunder::dsp
{

// High-pass and low-pass filters on the layer (PRD §7.7, FR-TONE-001/002). The
// high-pass keeps the layer's low end out of the melody's way; the low-pass
// tames brightness. Each has a 12 or 24 dB/oct slope:
// - 12 dB/oct: one 2-pole Butterworth section (Q = 0.707);
// - 24 dB/oct: two sections with the 4th-order Butterworth Q values (0.541 and
//   1.307), so the response is maximally flat and exactly −3 dB at the cutoff.
//
// Both slopes run all the time (a few multiplies per sample), so switching
// between them is a 30 ms crossfade between two filters that are already
// settled, never a click. Cutoffs glide in equal ratios over 50 ms
// (util::FrequencySmoother); the TPT filters (Svf) stay clean while they move.
// The high-pass comes first, then the low-pass.
class ToneModule final : public Module
{
public:
    // Cutoffs are kept below this fraction of the sample rate.
    static constexpr double kMaxNormalisedCutoff = 0.45;

    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

private:
    // One filter with both slopes, and the crossfade between them.
    struct Filter
    {
        Svf single, first, second; // 12 dB: single; 24 dB: first → second
        Svf::Coefficients cSingle, cFirst, cSecond;
        util::FrequencySmoother cutoff;
        util::LinearSmoother steepness; // 0 = 12 dB/oct, 1 = 24 dB/oct
        bool highPass = false;

        void setCutoff (double hz, double rate) noexcept;
        void reset() noexcept;
        float process (int channel, float x, float steep) noexcept;
    };

    double sampleRate = 48000.0;
    int numChannels = 2;
    Filter highPass, lowPass;
    bool firstChunk = true;
};

} // namespace thunder::dsp
