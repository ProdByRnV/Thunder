#pragma once

#include "core/params/ParamSnapshot.h"
#include "core/util/TripleBuffer.h"

#include <array>

// The effective-parameter stage (ARCHITECTURE §5.10, PRD DEC-041): between the
// host's values and the modules. The performance controls write here, so no
// module changes for them:
//
//   host snapshot ─► Morph (blend with sound B) ─► effective snapshot ─► modules
//
// Storm, the strikes, the Riser and Variation join it later, in that order.
// With no performance control active the host snapshot passes straight through
// (the same object, untouched). Everything is a pure function of the
// parameters and sound B, so a bounce sounds exactly like playback.

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
// whatever sound B is; and Morph never moves itself.
constexpr bool isMorphed (params::ParamId id) noexcept
{
    using params::ParamId;
    return id != ParamId::keyRoot && id != ParamId::keyScale && id != ParamId::outputMode &&
           id != ParamId::safetyLimiter && id != ParamId::morphAmount;
}

// The value `t` (0–1) of the way from `a` to `b`, in the control's own taper:
// linear controls in a straight line, logarithmic ones (frequencies, times)
// by equal ratios, whole-number controls rounded. Switches and choices change
// at the halfway point (the modules' own crossfades make that click-free).
// t ≤ 0 gives `a` and t ≥ 1 gives `b`, exactly.
float morphValue (const params::ParamDef& def, float a, float b, float t) noexcept;

// [RT] `out` = A (`a`) morphed `t` (0–1) of the way to `b`.
void applyMorph (const params::ParamSnapshot& a, const MorphSound& b, float t, params::ParamSnapshot& out) noexcept;

class PerformanceStage
{
public:
    // [MSG] Sets or clears sound B. One writer at a time (the processor holds a
    // lock); the audio thread picks it up at its next block.
    void setMorphSound (const MorphSound& sound) noexcept { morph.write (sound); }

    // [RT] The values the modules use for this block: `host` itself when
    // nothing performs, else a copy the performance controls have moved.
    const params::ParamSnapshot& apply (const params::ParamSnapshot& host) noexcept;

    // [RT] Sound B as the audio thread last took it.
    const MorphSound& getMorphSound() const noexcept { return morph.get(); }

private:
    util::TripleBuffer<MorphSound> morph;
    params::ParamSnapshot effective;
};

} // namespace thunder::perform
