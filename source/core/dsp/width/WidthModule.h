#pragma once

#include "core/engine/Module.h"
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
// TODO(PRD): Autopan (FR-WIDTH-002) arrives in v1.0 (T-0.8-07).
class WidthModule final : public Module
{
public:
    void prepare (const PrepareSpec& spec) override;
    void reset() noexcept override;
    void process (juce::dsp::AudioBlock<float>& block, const ProcessContext& context) noexcept override;

private:
    util::LinearSmoother width; // 0 … 2
    bool firstChunk = true;
};

} // namespace thunder::dsp
