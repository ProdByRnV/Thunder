#pragma once

#include "core/params/ParamSnapshot.h"

#include <array>
#include <cstdint>

// The performance controls' sound (PRD FR-PLAY-001, 003, 004, 005; DEC-041),
// as pure functions of the parameters and the song position. The
// PerformanceStage calls them on its control grid; tests call them directly.
//
// Everything moves controls in their own travel (the 0–1 a host knob covers:
// linear controls in a straight line, frequencies and times by ratio), then
// clamps to the control's range, so a curve feels the same on every control.

namespace thunder::perform
{

// ---- Travel ---------------------------------------------------------------------------------------------------------

// Where `value` sits in the control's travel (0–1).
float toTravel (const params::ParamDef& def, float value) noexcept;

// The value at `travel` (clamped to 0–1); whole-number controls round.
float fromTravel (const params::ParamDef& def, float travel) noexcept;

// `value` moved `delta` of the travel, clamped to the range.
float moveBy (const params::ParamDef& def, float value, float delta) noexcept;

// ---- Storm (FR-PLAY-001) --------------------------------------------------------------------------------------------

// One line of Storm's table: at Storm `s` (0–1) the control moves
// amount × s^shape of its travel. Tuned by ear at the v1.0 test pass.
struct StormCurve
{
    params::ParamId param;
    float amount; // travel added at 100%
    float shape;  // 1 = straight; above 1 starts later
};

inline constexpr std::array<StormCurve, 9> kStormCurves{{
    {params::ParamId::grainDensity, 0.30f, 1.0f},
    {params::ParamId::grainPosSpread, 0.40f, 1.0f},
    {params::ParamId::grainPitchSpread, 0.20f, 1.5f},
    {params::ParamId::grainPanSpread, 0.40f, 1.0f},
    {params::ParamId::spaceDecay, 0.35f, 1.2f},
    {params::ParamId::spaceMix, 0.30f, 1.0f},
    {params::ParamId::layerLevel, 0.0625f, 1.0f}, // +3 dB at 100%
    {params::ParamId::lpFreq, 0.25f, 1.0f},       // brighter
    {params::ParamId::staticAmount, 0.50f, 1.0f},
}};

// [RT] Moves `values` by Storm at `storm` (0–100%). At 0 nothing changes.
void applyStorm (params::ParamSnapshot& values, float storm) noexcept;

// ---- Strikes (FR-PLAY-003) ------------------------------------------------------------------------------------------

inline constexpr double kStrikeGlideSeconds = 0.030;

// Thunderclap's reverb swell: Space Mix and Decay rise to at least these.
inline constexpr float kClapSpaceMix = 90.0f;
inline constexpr float kClapSpaceDecay = 85.0f;

// [RT] Thunderclap `amount` (0–1, its glide) of the way in: the layer's pitch
// dives an octave (within Pitch's range) and the reverb swells.
void applyThunderclap (params::ParamSnapshot& values, float amount) noexcept;

// [RT] Scatter `amount` (0–1) of the way in: grain position, pitch and pan
// spread go to their maximum.
void applyScatter (params::ParamSnapshot& values, float amount) noexcept;

// ---- Variation (FR-PLAY-005) ----------------------------------------------------------------------------------------

// One control Variation moves: up to ± `amount` of its travel at 100%.
struct VariationRange
{
    params::ParamId param;
    float amount;
};

inline constexpr std::array<VariationRange, 6> kVariationRanges{{
    {params::ParamId::grainSize, 0.12f},
    {params::ParamId::grainDensity, 0.12f},
    {params::ParamId::grainPosSpread, 0.15f},
    {params::ParamId::grainPitchSpread, 0.08f},
    {params::ParamId::grainPanSpread, 0.15f},
    {params::ParamId::grainReverse, 0.10f},
}};

// Each bar's changes glide in from the previous bar's over this long.
inline constexpr double kVariationGlideSeconds = 0.060;

// Each control's change for a bar, −1…1 (a pure function of the bar number).
std::array<float, kVariationRanges.size()> variationForBar (std::int64_t bar) noexcept;

// [RT] Moves `values` by Variation at `variation` (0–100%), `glide` (0–1) of the
// way from bar `bar − 1`'s changes to bar `bar`'s.
void applyVariation (params::ParamSnapshot& values, float variation, std::int64_t bar, float glide) noexcept;

// ---- Riser (FR-PLAY-004) --------------------------------------------------------------------------------------------

// How a rise climbs: rise = progress^kRiserShape (starts gently, ends steep).
inline constexpr float kRiserShape = 2.0f;

// After the bar line Storm falls back to the knob over this long.
inline constexpr double kRiserDropSeconds = 0.030;

// The bars of each Riser Length choice.
int riserBars (const params::ParamSnapshot& values) noexcept;

// Where a rise pressed at `ppq` ends: `bars` bars after the bar line nearest
// the press (pressing just after the one counts from it), so the rise lasts
// between bars − ½ and bars + ½.
double riserEnd (double ppq, double barStart, double quarterNotesPerBar, int bars) noexcept;

} // namespace thunder::perform
