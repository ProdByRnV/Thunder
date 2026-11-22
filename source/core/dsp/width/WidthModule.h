#pragma once

#include "core/engine/Module.h"
#include "core/host/MusicalClock.h"
#include "core/util/Smoother.h"

namespace thunder::dsp
{

// Stereo width of the layer (PRD §7.8, FR-WIDTH-001), by mid/side gain:
//   mid = (L + R) / 2,  side = (L − R) / 2 × width,  L = mid + side,  R = mid − side
// 0% is mono, 100% leaves the layer as it is (exactly: nothing is computed),
// 200% doubles the side signal. The mid is never changed, so mono playback of
// the layer sounds the same at every width (FR-WIDTH-003). Width glides over
// 20 ms.
//
// Autopan (FR-WIDTH-002; PRD DEC-051), after the width: the layer moves from
// side to side on a sine, one cycle per Autopan Rate (the synced list, PRD
// §9.13), as far as Autopan Depth says (100% = fully left and right), panned
// with equal power and unity in the centre. The phase is the song position
// divided by the cycle length (bars counted from the song start), so the sweep
// is continuous, repeats in a loop and matches a bounce. While the host is
// stopped it keeps moving at the tempo (MusicalClock). Switching it on or off
// fades its depth in or out over 30 ms.
class WidthModule final : public Module
{
public:
    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

    // The pan (−1 … +1) at the last sample, for tests.
    float getLastPan() const noexcept { return lastPan; }

private:
    void autopan (float* left, float* right, int numSamples, const ProcessContext& context) noexcept;

    util::LinearSmoother width;    // 0 … 2
    util::LinearSmoother panDepth; // 0 … 1, 0 while Autopan is off
    MusicalClock clock;
    bool firstChunk = true;
    float lastPan = 0.0f;
};

} // namespace thunder::dsp
